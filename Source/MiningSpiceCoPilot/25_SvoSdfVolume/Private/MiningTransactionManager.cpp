// MiningTransactionManager.cpp - System 25: SVO+SDF Hybrid Volume Representation System
// Handles transactions for mining operations with optimistic concurrency control

#include "25_SvoSdfVolume/Public/MiningTransactionManager.h"
#include "25_SvoSdfVolume/Public/SVOSystem/SVOHybridVolume.h"
#include "25_SvoSdfVolume/Public/SVOSystem/ZOrderCurve.h"

// Core system dependencies
#include "1_CoreRegistry/Public/ServiceLocator.h"
#include "2_MemoryManagement/Public/MemoryManager.h"
#include "3_Threading/Public/TaskScheduler.h"
#include "3_Threading/Public/TransactionManager.h"
#include "4_EventSystem/Public/EventBus.h"
#include "6_ServiceRegistry/Public/DependencyManager.h"

// Network-related includes
#include "Net/UnrealNetwork.h"
#include "Engine/ActorChannel.h"
#include "Net/DataReplication.h"

// Atomic operations and thread safety
#include "HAL/ThreadSafeBool.h"
#include "Templates/Atomic.h"
#include "HAL/ThreadSafeCounter64.h"
#include "Misc/ScopeLock.h"

FMiningTransactionManager::FMiningTransactionManager()
    : LastTransactionID(0)
    , PendingTransactionCount(0)
    , SuccessfulTransactionCount(0)
    , FailedTransactionCount(0)
    , NetworkTransactionCount(0)
    , bIsNetworkAuthoritative(false)
{
    // Service resolution through System 6
    ServiceLocator = IServiceLocator::Get().ResolveService<IServiceLocator>();
    check(ServiceLocator);
    
    MemoryManager = ServiceLocator->ResolveService<IMemoryManager>();
    check(MemoryManager);
    
    TaskScheduler = ServiceLocator->ResolveService<ITaskScheduler>();
    check(TaskScheduler);
    
    TransactionManager = ServiceLocator->ResolveService<ITransactionManager>();
    check(TransactionManager);
    
    EventBus = ServiceLocator->ResolveService<UEventBus>();
    check(EventBus);
    
    // Register for transaction-related events
    EventBus->SubscribeToEvent<FTransactionCompletedEvent>(this, &FMiningTransactionManager::OnTransactionCompleted);
    
    // Register this service
    IServiceLocator::Get().RegisterService<IMiningTransactionManager>(this);
    
    // Register service dependencies
    FDependencyManager::Get().RegisterDependency<IMiningTransactionManager, ITransactionManager>();
    
    // Service health monitoring integration
    IServiceMonitor::Get().RegisterServiceForMonitoring("MiningTransactionManager", this);
}

FMiningTransactionManager::~FMiningTransactionManager()
{
    if (EventBus)
    {
        EventBus->UnsubscribeFromAllEvents(this);
    }
    
    IServiceLocator::Get().UnregisterService<IMiningTransactionManager>();
}

FMiningTransaction* FMiningTransactionManager::BeginMiningTransaction(
    const FMiningOperationDescriptor& OperationDesc,
    const FMaterialParameters& MaterialParams,
    const FNetworkContext& NetworkContext)
{
    // Validate network authority if in network context
    if (NetworkContext.bIsNetworked && !HasAuthorityForOperation(OperationDesc.AffectedZone, NetworkContext))
    {
        UE_LOG(LogMiningTransaction, Warning, TEXT("No authority for mining operation in zone %s"), 
            *OperationDesc.AffectedZone.ToString());
        return nullptr;
    }
    
    // Create transaction object
    FMiningTransaction* Transaction = new FMiningTransaction();
    Transaction->ID = GenerateTransactionID();
    Transaction->OperationDesc = OperationDesc;
    Transaction->MaterialParams = MaterialParams;
    Transaction->NetworkContext = NetworkContext;
    Transaction->State = EMiningTransactionState::Created;
    Transaction->StartTime = FPlatformTime::Seconds();
    
    // Begin transaction with core transaction system
    Transaction->CoreTransactionID = TransactionManager->BeginTransaction(
        NetworkContext.bIsNetworked ? ETransactionConcurrency::NetworkAware : ETransactionConcurrency::Optimistic);
    
    // Add to active transactions
    {
        FScopeLock Lock(&TransactionMutex);
        ActiveTransactions.Add(Transaction->ID, Transaction);
        PendingTransactionCount++;
    }
    
    // Track read/write sets for optimistic concurrency
    Transaction->State = EMiningTransactionState::Active;
    
    // Notify event system
    FEventContext Context;
    Context.Add("TransactionID", Transaction->ID);
    Context.Add("OperationType", (int32)OperationDesc.OperationType);
    Context.Add("IsNetworked", NetworkContext.bIsNetworked);
    
    EventBus->PublishEvent<FMiningTransactionStartedEvent>(Context);
    
    UE_LOG(LogMiningTransaction, Log, TEXT("Started mining transaction %llu of type %d"),
        Transaction->ID, (int32)OperationDesc.OperationType);
    
    return Transaction;
}

bool FMiningTransactionManager::CommitMiningTransaction(FMiningTransaction* Transaction)
{
    if (!Transaction)
    {
        UE_LOG(LogMiningTransaction, Error, TEXT("Cannot commit null transaction"));
        return false;
    }
    
    if (Transaction->State != EMiningTransactionState::Active)
    {
        UE_LOG(LogMiningTransaction, Warning, TEXT("Cannot commit transaction %llu in state %d"),
            Transaction->ID, (int32)Transaction->State);
        return false;
    }
    
    Transaction->State = EMiningTransactionState::Committing;
    
    // Record modified zones for potential conflict detection and network replication
    TArray<FZoneID> ModifiedZones = Transaction->GetModifiedZones();
    
    // Prepare version information for networked transactions
    if (Transaction->NetworkContext.bIsNetworked)
    {
        for (const FZoneID& ZoneID : ModifiedZones)
        {
            // Track version numbers for conflict detection
            uint64 OldVersion = GetZoneVersion(ZoneID);
            uint64 NewVersion = OldVersion + 1;
            
            Transaction->ZoneVersions.Add(ZoneID, NewVersion);
            
            // Record for network synchronization
            NetworkPendingUpdates.Add(FNetworkZoneUpdate{
                ZoneID,
                NewVersion,
                Transaction->OperationDesc,
                Transaction->MaterialParams,
                Transaction->NetworkContext.ClientID
            });
        }
    }
    
    // Attempt to commit the core transaction
    bool bCommitSuccess = TransactionManager->CommitTransaction(Transaction->CoreTransactionID);
    
    if (bCommitSuccess)
    {
        // Update zone versions in successful case
        for (const auto& VersionPair : Transaction->ZoneVersions)
        {
            UpdateZoneVersion(VersionPair.Key, VersionPair.Value);
        }
        
        // Update affected SVOHybridVolume version counters
        for (const auto& VolumePair : Transaction->AffectedVolumes)
        {
            if (VolumePair.Value)
            {
                VolumePair.Value->IncrementVersionCounter();
            }
        }
        
        Transaction->State = EMiningTransactionState::Committed;
        Transaction->EndTime = FPlatformTime::Seconds();
        
        // Handle networked transaction
        if (Transaction->NetworkContext.bIsNetworked)
        {
            // Network transaction successful, schedule replication
            ScheduleNetworkReplication(Transaction);
            NetworkTransactionCount++;
        }
        
        {
            FScopeLock Lock(&TransactionMutex);
            PendingTransactionCount--;
            SuccessfulTransactionCount++;
            CompletedTransactions.Add(Transaction);
        }
        
        // Notify event system
        FEventContext Context;
        Context.Add("TransactionID", Transaction->ID);
        Context.Add("Success", true);
        Context.Add("Duration", Transaction->EndTime - Transaction->StartTime);
        Context.Add("IsNetworked", Transaction->NetworkContext.bIsNetworked);
        
        EventBus->PublishEvent<FMiningTransactionCompletedEvent>(Context);
        
        UE_LOG(LogMiningTransaction, Log, TEXT("Successfully committed mining transaction %llu"), Transaction->ID);
        return true;
    }
    else
    {
        // Transaction failed due to concurrency conflict or other issues
        Transaction->State = EMiningTransactionState::Failed;
        Transaction->EndTime = FPlatformTime::Seconds();
        
        {
            FScopeLock Lock(&TransactionMutex);
            PendingTransactionCount--;
            FailedTransactionCount++;
            CompletedTransactions.Add(Transaction);
        }
        
        // Notify event system
        FEventContext Context;
        Context.Add("TransactionID", Transaction->ID);
        Context.Add("Success", false);
        Context.Add("Duration", Transaction->EndTime - Transaction->StartTime);
        Context.Add("IsNetworked", Transaction->NetworkContext.bIsNetworked);
        
        EventBus->PublishEvent<FMiningTransactionCompletedEvent>(Context);
        
        UE_LOG(LogMiningTransaction, Warning, TEXT("Failed to commit mining transaction %llu"), Transaction->ID);
        return false;
    }
}

void FMiningTransactionManager::AbortMiningTransaction(FMiningTransaction* Transaction)
{
    if (!Transaction)
    {
        UE_LOG(LogMiningTransaction, Error, TEXT("Cannot abort null transaction"));
        return;
    }
    
    if (Transaction->State != EMiningTransactionState::Active && 
        Transaction->State != EMiningTransactionState::Committing)
    {
        UE_LOG(LogMiningTransaction, Warning, TEXT("Cannot abort transaction %llu in state %d"),
            Transaction->ID, (int32)Transaction->State);
        return;
    }
    
    // Abort the core transaction
    TransactionManager->AbortTransaction(Transaction->CoreTransactionID);
    
    Transaction->State = EMiningTransactionState::Aborted;
    Transaction->EndTime = FPlatformTime::Seconds();
    
    {
        FScopeLock Lock(&TransactionMutex);
        PendingTransactionCount--;
        FailedTransactionCount++;
        CompletedTransactions.Add(Transaction);
    }
    
    // Notify event system
    FEventContext Context;
    Context.Add("TransactionID", Transaction->ID);
    Context.Add("IsNetworked", Transaction->NetworkContext.bIsNetworked);
    Context.Add("Duration", Transaction->EndTime - Transaction->StartTime);
    
    EventBus->PublishEvent<FMiningTransactionAbortedEvent>(Context);
    
    UE_LOG(LogMiningTransaction, Log, TEXT("Aborted mining transaction %llu"), Transaction->ID);
}

void FMiningTransactionManager::AddVolumeToTransaction(
    FMiningTransaction* Transaction,
    const FVolumeID& VolumeID, 
    FSVOHybridVolume* Volume)
{
    if (!Transaction || Transaction->State != EMiningTransactionState::Active)
    {
        return;
    }
    
    Transaction->AffectedVolumes.Add(VolumeID, Volume);
}

void FMiningTransactionManager::AddZoneToTransaction(
    FMiningTransaction* Transaction,
    const FZoneID& ZoneID,
    EZoneAccessMode AccessMode)
{
    if (!Transaction || Transaction->State != EMiningTransactionState::Active)
    {
        return;
    }
    
    if (AccessMode == EZoneAccessMode::Read)
    {
        Transaction->ReadZones.Add(ZoneID);
    }
    else if (AccessMode == EZoneAccessMode::Write)
    {
        Transaction->WriteZones.Add(ZoneID);
    }
    else if (AccessMode == EZoneAccessMode::ReadWrite)
    {
        Transaction->ReadZones.Add(ZoneID);
        Transaction->WriteZones.Add(ZoneID);
    }
}

bool FMiningTransactionManager::ApplyNetworkedMiningOperation(
    const FNetworkContext& NetworkContext,
    const FMiningOperationDescriptor& OperationDesc,
    const FMaterialParameters& MaterialParams,
    const TMap<FZoneID, uint64>& ZoneVersions)
{
    // Validate if this is the authoritative server for this operation
    if (!bIsNetworkAuthoritative && !NetworkContext.bIsServer)
    {
        // Only server or authoritative clients can apply network operations
        UE_LOG(LogMiningTransaction, Warning, TEXT("Non-authoritative client attempted to apply networked operation"));
        return false;
    }
    
    // Check version numbers to detect conflicts
    bool bVersionsValid = true;
    for (const auto& VersionPair : ZoneVersions)
    {
        uint64 CurrentVersion = GetZoneVersion(VersionPair.Key);
        if (VersionPair.Value <= CurrentVersion)
        {
            // Version conflict detected - this operation is outdated or duplicated
            UE_LOG(LogMiningTransaction, Warning, TEXT("Version conflict for zone %s: current %llu, received %llu"),
                *VersionPair.Key.ToString(), CurrentVersion, VersionPair.Value);
            bVersionsValid = false;
            break;
        }
    }
    
    if (!bVersionsValid)
    {
        return false;
    }
    
    // Create a new transaction for this networked operation
    FNetworkContext ModifiedContext = NetworkContext;
    ModifiedContext.bIsNetworked = true;
    
    FMiningTransaction* Transaction = BeginMiningTransaction(OperationDesc, MaterialParams, ModifiedContext);
    
    if (!Transaction)
    {
        UE_LOG(LogMiningTransaction, Error, TEXT("Failed to begin networked mining transaction"));
        return false;
    }
    
    // Copy version information
    Transaction->ZoneVersions = ZoneVersions;
    
    // Attempt to apply the operation and commit the transaction
    bool bSuccess = CommitMiningTransaction(Transaction);
    
    if (!bSuccess)
    {
        AbortMiningTransaction(Transaction);
    }
    
    return bSuccess;
}

void FMiningTransactionManager::ProcessPendingNetworkReplications()
{
    // Process network replication in batches
    constexpr int32 MaxBatchSize = 10;
    
    TArray<FNetworkZoneUpdate> UpdateBatch;
    
    {
        FScopeLock Lock(&NetworkMutex);
        
        int32 BatchSize = FMath::Min(NetworkPendingUpdates.Num(), MaxBatchSize);
        if (BatchSize == 0)
        {
            return;
        }
        
        for (int32 i = 0; i < BatchSize; ++i)
        {
            UpdateBatch.Add(NetworkPendingUpdates[i]);
        }
        
        // Remove processed updates
        if (BatchSize > 0)
        {
            NetworkPendingUpdates.RemoveAt(0, BatchSize);
        }
    }
    
    // Process each update
    for (const FNetworkZoneUpdate& Update : UpdateBatch)
    {
        ReplicateZoneUpdate(Update);
    }
}

void FMiningTransactionManager::ScheduleNetworkReplication(FMiningTransaction* Transaction)
{
    if (!Transaction || !Transaction->NetworkContext.bIsNetworked)
    {
        return;
    }
    
    // Schedule task to prepare and send delta updates
    TaskScheduler->ScheduleTask(
        [this, Transaction]()
        {
            PrepareDeltaUpdates(Transaction);
        },
        ETaskPriority::Normal);
}

void FMiningTransactionManager::PrepareDeltaUpdates(FMiningTransaction* Transaction)
{
    if (!Transaction || Transaction->State != EMiningTransactionState::Committed)
    {
        return;
    }
    
    // Get all affected zones
    TArray<FZoneID> AffectedZones = Transaction->GetModifiedZones();
    
    // For each zone, prepare delta encoding for efficient network transmission
    for (const FZoneID& ZoneID : AffectedZones)
    {
        // Create buffer for delta-encoded data
        IBufferProvider* DeltaBuffer = MemoryManager->CreateBuffer(
            "NetworkDeltaBuffer",
            NetworkDeltaBufferSize,
            false, // Not zero-copy for network operations
            false  // Not GPU writable
        );
        
        if (!DeltaBuffer)
        {
            continue;
        }
        
        // Get volume associated with this zone
        FSVOHybridVolume* Volume = nullptr;
        for (const auto& VolumePair : Transaction->AffectedVolumes)
        {
            if (VolumePair.Value->ContainsZone(ZoneID))
            {
                Volume = VolumePair.Value;
                break;
            }
        }
        
        if (!Volume)
        {
            MemoryManager->ReleaseBuffer(DeltaBuffer);
            continue;
        }
        
        // Generate delta encoding
        bool bDeltaSuccess = Volume->GenerateZoneDeltaEncoding(
            ZoneID,
            Transaction->OperationDesc,
            DeltaBuffer);
        
        if (bDeltaSuccess)
        {
            // Store for network replication
            FNetworkZoneDelta ZoneDelta;
            ZoneDelta.ZoneID = ZoneID;
            ZoneDelta.Version = Transaction->ZoneVersions.FindRef(ZoneID);
            ZoneDelta.DeltaBuffer = DeltaBuffer;
            ZoneDelta.OperationDesc = Transaction->OperationDesc;
            ZoneDelta.MaterialParams = Transaction->MaterialParams;
            ZoneDelta.ClientID = Transaction->NetworkContext.ClientID;
            
            // Add to queue for replication
            FScopeLock Lock(&NetworkMutex);
            NetworkDeltaUpdates.Add(ZoneDelta);
        }
        else
        {
            MemoryManager->ReleaseBuffer(DeltaBuffer);
        }
    }
    
    // Signal that delta updates are ready
    TriggerNetworkReplication();
}

void FMiningTransactionManager::TriggerNetworkReplication()
{
    // Schedule network replication task
    TaskScheduler->ScheduleTask(
        [this]()
        {
            ProcessNetworkDeltaReplications();
        },
        ETaskPriority::Normal);
}

void FMiningTransactionManager::ProcessNetworkDeltaReplications()
{
    // Process delta replications in batches
    constexpr int32 MaxBatchSize = 5;
    
    TArray<FNetworkZoneDelta> DeltaBatch;
    
    {
        FScopeLock Lock(&NetworkMutex);
        
        int32 BatchSize = FMath::Min(NetworkDeltaUpdates.Num(), MaxBatchSize);
        if (BatchSize == 0)
        {
            return;
        }
        
        for (int32 i = 0; i < BatchSize; ++i)
        {
            DeltaBatch.Add(NetworkDeltaUpdates[i]);
        }
        
        // Remove processed updates
        if (BatchSize > 0)
        {
            NetworkDeltaUpdates.RemoveAt(0, BatchSize);
        }
    }
    
    // Process each delta update
    for (const FNetworkZoneDelta& Delta : DeltaBatch)
    {
        ReplicateZoneDelta(Delta);
        
        // Release buffer when done
        if (Delta.DeltaBuffer)
        {
            MemoryManager->ReleaseBuffer(Delta.DeltaBuffer);
        }
    }
}

void FMiningTransactionManager::ReplicateZoneUpdate(const FNetworkZoneUpdate& Update)
{
    if (!NetworkReplicationInterface)
    {
        UE_LOG(LogMiningTransaction, Warning, TEXT("No network replication interface available"));
        return;
    }
    
    // Send update to network subsystem
    FNetworkMiningOperation NetworkOp;
    NetworkOp.ZoneID = Update.ZoneID;
    NetworkOp.Version = Update.Version;
    NetworkOp.OperationDesc = Update.OperationDesc;
    NetworkOp.MaterialParams = Update.MaterialParams;
    NetworkOp.ClientID = Update.ClientID;
    NetworkOp.Timestamp = FDateTime::UtcNow().ToUnixTimestamp();
    
    NetworkReplicationInterface->ReplicateOperation(NetworkOp);
    
    UE_LOG(LogMiningTransaction, Verbose, TEXT("Replicated zone update for %s, version %llu"),
        *Update.ZoneID.ToString(), Update.Version);
}

void FMiningTransactionManager::ReplicateZoneDelta(const FNetworkZoneDelta& Delta)
{
    if (!NetworkReplicationInterface)
    {
        UE_LOG(LogMiningTransaction, Warning, TEXT("No network replication interface available"));
        return;
    }
    
    // Send delta to network subsystem
    FNetworkMiningDelta NetworkDelta;
    NetworkDelta.ZoneID = Delta.ZoneID;
    NetworkDelta.Version = Delta.Version;
    NetworkDelta.OperationDesc = Delta.OperationDesc;
    NetworkDelta.MaterialParams = Delta.MaterialParams;
    NetworkDelta.ClientID = Delta.ClientID;
    NetworkDelta.Timestamp = FDateTime::UtcNow().ToUnixTimestamp();
    
    // Copy buffer data
    if (Delta.DeltaBuffer)
    {
        void* BufferData = Delta.DeltaBuffer->Map(EBufferAccessMode::Read);
        uint32 BufferSize = Delta.DeltaBuffer->GetSize();
        
        if (BufferData && BufferSize > 0)
        {
            NetworkDelta.DeltaData.AddUninitialized(BufferSize);
            FMemory::Memcpy(NetworkDelta.DeltaData.GetData(), BufferData, BufferSize);
        }
        
        Delta.DeltaBuffer->Unmap();
    }
    
    NetworkReplicationInterface->ReplicateDelta(NetworkDelta);
    
    UE_LOG(LogMiningTransaction, Verbose, TEXT("Replicated zone delta for %s, version %llu, size %d bytes"),
        *Delta.ZoneID.ToString(), Delta.Version, NetworkDelta.DeltaData.Num());
}

uint64 FMiningTransactionManager::GenerateTransactionID()
{
    return ++LastTransactionID;
}

bool FMiningTransactionManager::HasAuthorityForOperation(const FZoneID& ZoneID, const FNetworkContext& NetworkContext)
{
    // Server always has authority
    if (NetworkContext.bIsServer)
    {
        return true;
    }
    
    // Check if this client has been granted temporary authority
    FScopeLock Lock(&AuthorityMutex);
    if (ClientZoneAuthority.Contains(NetworkContext.ClientID))
    {
        const TSet<FZoneID>& AuthorizedZones = ClientZoneAuthority[NetworkContext.ClientID];
        return AuthorizedZones.Contains(ZoneID);
    }
    
    return false;
}

void FMiningTransactionManager::GrantClientZoneAuthority(
    const FClientID& ClientID,
    const TSet<FZoneID>& ZoneIDs,
    float DurationSeconds)
{
    FScopeLock Lock(&AuthorityMutex);
    
    // Grant authority for these zones
    TSet<FZoneID>& AuthorizedZones = ClientZoneAuthority.FindOrAdd(ClientID);
    AuthorizedZones.Append(ZoneIDs);
    
    // Schedule authority revocation
    double RevokeTime = FPlatformTime::Seconds() + DurationSeconds;
    
    for (const FZoneID& ZoneID : ZoneIDs)
    {
        AuthorityExpirations.Add(FAuthorityExpiration{
            ClientID,
            ZoneID,
            RevokeTime
        });
    }
    
    // Ensure expiration check is running
    if (!AuthorityExpirationTimerHandle.IsValid())
    {
        AuthorityExpirationTimerHandle = TaskScheduler->ScheduleRepeatingTask(
            [this]()
            {
                ProcessAuthorityExpirations();
            },
            1.0f, // Check every 1 second
            ETaskPriority::Low);
    }
}

void FMiningTransactionManager::RevokeClientZoneAuthority(
    const FClientID& ClientID,
    const TSet<FZoneID>& ZoneIDs)
{
    FScopeLock Lock(&AuthorityMutex);
    
    if (ClientZoneAuthority.Contains(ClientID))
    {
        TSet<FZoneID>& AuthorizedZones = ClientZoneAuthority[ClientID];
        
        // Remove specified zones
        for (const FZoneID& ZoneID : ZoneIDs)
        {
            AuthorizedZones.Remove(ZoneID);
            
            // Also remove from expirations
            for (int32 i = AuthorityExpirations.Num() - 1; i >= 0; --i)
            {
                if (AuthorityExpirations[i].ClientID == ClientID && AuthorityExpirations[i].ZoneID == ZoneID)
                {
                    AuthorityExpirations.RemoveAtSwap(i);
                }
            }
        }
        
        // Clean up if no more zones
        if (AuthorizedZones.Num() == 0)
        {
            ClientZoneAuthority.Remove(ClientID);
        }
    }
}

void FMiningTransactionManager::ProcessAuthorityExpirations()
{
    double CurrentTime = FPlatformTime::Seconds();
    TArray<FAuthorityExpiration> ExpiredItems;
    
    {
        FScopeLock Lock(&AuthorityMutex);
        
        // Find expired authorities
        for (int32 i = AuthorityExpirations.Num() - 1; i >= 0; --i)
        {
            if (AuthorityExpirations[i].ExpirationTime <= CurrentTime)
            {
                ExpiredItems.Add(AuthorityExpirations[i]);
                AuthorityExpirations.RemoveAtSwap(i);
            }
        }
    }
    
    // Process expired items
    for (const FAuthorityExpiration& Expired : ExpiredItems)
    {
        TSet<FZoneID> ZoneSet;
        ZoneSet.Add(Expired.ZoneID);
        RevokeClientZoneAuthority(Expired.ClientID, ZoneSet);
        
        UE_LOG(LogMiningTransaction, Log, TEXT("Authority expired for client %s on zone %s"),
            *Expired.ClientID.ToString(), *Expired.ZoneID.ToString());
    }
}

void FMiningTransactionManager::SetNetworkReplicationInterface(INetworkReplicationInterface* Interface)
{
    NetworkReplicationInterface = Interface;
}

void FMiningTransactionManager::SetNetworkAuthoritative(bool bAuthoritative)
{
    bIsNetworkAuthoritative = bAuthoritative;
}

void FMiningTransactionManager::OnTransactionCompleted(const FTransactionCompletedEvent& Event)
{
    // Handle transaction completion from the core system
    if (Event.EventType == ETransactionEventType::TransactionCompleted)
    {
        uint64 TransactionID = Event.GetTransactionID();
        
        // Update statistics
        if (Event.IsSuccessful())
        {
            UE_LOG(LogMiningTransaction, Verbose, TEXT("Core transaction %llu completed successfully"), TransactionID);
        }
        else
        {
            UE_LOG(LogMiningTransaction, Verbose, TEXT("Core transaction %llu failed"), TransactionID);
        }
    }
}

uint64 FMiningTransactionManager::GetZoneVersion(const FZoneID& ZoneID)
{
    FScopeLock Lock(&VersionMutex);
    
    if (ZoneVersions.Contains(ZoneID))
    {
        return ZoneVersions[ZoneID];
    }
    
    return 0; // Initial version
}

void FMiningTransactionManager::UpdateZoneVersion(const FZoneID& ZoneID, uint64 NewVersion)
{
    FScopeLock Lock(&VersionMutex);
    
    ZoneVersions.Add(ZoneID, NewVersion);
}

bool FMiningTransactionManager::IsServiceReady()
{
    // Check if all required services are available
    return ServiceLocator && 
           MemoryManager && 
           TaskScheduler && 
           TransactionManager && 
           EventBus;
}

void FMiningTransactionManager::NotifyServiceEvent(EServiceEvent Event)
{
    // Handle service events
    switch (Event)
    {
        case EServiceEvent::ServiceStarted:
            UE_LOG(LogMiningTransaction, Log, TEXT("MiningTransactionManager service started"));
            break;
            
        case EServiceEvent::ServiceStopping:
            UE_LOG(LogMiningTransaction, Log, TEXT("MiningTransactionManager service stopping"));
            // Complete any in-progress transactions
            ProcessPendingTransactions(true);
            break;
            
        default:
            break;
    }
}

FServiceStatistics FMiningTransactionManager::GetServiceStatistics()
{
    FServiceStatistics Stats;
    
    // Gather transaction statistics
    Stats.Add("Total Transactions", SuccessfulTransactionCount + FailedTransactionCount);
    Stats.Add("Successful Transactions", SuccessfulTransactionCount);
    Stats.Add("Failed Transactions", FailedTransactionCount);
    Stats.Add("Pending Transactions", PendingTransactionCount);
    Stats.Add("Network Transactions", NetworkTransactionCount);
    
    return Stats;
}

void FMiningTransactionManager::ProcessPendingTransactions(bool bAbortAll)
{
    FScopeLock Lock(&TransactionMutex);
    
    TArray<FMiningTransaction*> TransactionsToProcess;
    
    // Create a copy to avoid modification issues during iteration
    for (const auto& TransactionPair : ActiveTransactions)
    {
        TransactionsToProcess.Add(TransactionPair.Value);
    }
    
    // Process each transaction
    for (FMiningTransaction* Transaction : TransactionsToProcess)
    {
        if (bAbortAll || Transaction->State != EMiningTransactionState::Active)
        {
            AbortMiningTransaction(Transaction);
        }
    }
}

TArray<FMiningTransaction*> FMiningTransactionManager::GetCompletedTransactions(bool bClear)
{
    FScopeLock Lock(&TransactionMutex);
    
    TArray<FMiningTransaction*> Result = CompletedTransactions;
    
    if (bClear)
    {
        CompletedTransactions.Empty();
    }
    
    return Result;
}

TArray<FZoneID> FMiningTransaction::GetModifiedZones() const
{
    TArray<FZoneID> Result;
    Result.Append(WriteZones.Array());
    return Result;
}