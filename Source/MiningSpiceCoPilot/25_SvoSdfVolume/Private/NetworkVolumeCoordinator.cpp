// NetworkVolumeCoordinator.cpp
// Implements network synchronization for SVO+SDF volume system

#include "25_SvoSdfVolume/Public/NetworkVolumeCoordinator.h"
#include "25_SvoSdfVolume/Public/SVOHybridVolume.h"
#include "25_SvoSdfVolume/Public/VolumeSerializer.h"
#include "25_SvoSdfVolume/Public/OctreeNodeManager.h"
#include "25_SvoSdfVolume/Public/MaterialSDFManager.h"
#include "HAL/PlatformTime.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "MiningSpiceCoPilot/3_Threading/Public/ThreadingHelpers.h"
#include "MiningSpiceCoPilot/4_EventSystem/Public/EventBus.h"
#include "MiningSpiceCoPilot/5_ConfigurationManagement/Public/ConfigManager.h"
#include "MiningSpiceCoPilot/6_ServiceDependencyRegistry/Public/ServiceLocator.h"

FNetworkVolumeCoordinator::FNetworkVolumeCoordinator()
    : Volume(nullptr)
    , Serializer(nullptr)
    , ConflictResolutionStrategy(0)
    , bServerMode(false)
{
    // Default conflict handler just rejects conflicts
    ConflictHandler = [](const FBox&, uint8, uint64) { return false; };
}

FNetworkVolumeCoordinator::~FNetworkVolumeCoordinator()
{
    // Clean up any pending operations
    CleanupCompletedOperations(0.0f); // Force cleanup of all operations
}

void FNetworkVolumeCoordinator::Initialize(USVOHybridVolume* InVolume, FVolumeSerializer* InSerializer)
{
    Volume = InVolume;
    Serializer = InSerializer;
    
    // Check if we're running in server mode
    IConfigManager* ConfigManager = IServiceLocator::Get().ResolveService<IConfigManager>();
    if (ConfigManager)
    {
        bServerMode = ConfigManager->GetValue<bool>("Network.IsServer", false);
    }
    
    // Set default conflict resolution strategy based on config
    if (ConfigManager)
    {
        ConflictResolutionStrategy = ConfigManager->GetValue<uint8>("Network.ConflictResolutionStrategy", 0);
    }
    
    UE_LOG(LogTemp, Log, TEXT("NetworkVolumeCoordinator: Initialized in %s mode"), 
        bServerMode ? TEXT("SERVER") : TEXT("CLIENT"));
}

FNetworkVolumeCoordinator::ENetworkResult FNetworkVolumeCoordinator::RequestRegionModification(
    const FBox& Region, uint8 MaterialIndex)
{
    if (!Volume)
    {
        UE_LOG(LogTemp, Warning, TEXT("NetworkVolumeCoordinator: Cannot request modification - Volume not initialized"));
        return ENetworkResult::Failure;
    }
    
    // Check if region is already locked
    if (IsRegionLocked(Region))
    {
        UE_LOG(LogTemp, Warning, TEXT("NetworkVolumeCoordinator: Region is already locked, cannot modify"));
        return ENetworkResult::Conflict;
    }
    
    // Generate operation ID and create pending operation
    uint64 OpId = GenerateOperationId();
    FPendingOperation NewOp(OpId, EOperationType::Modification, Region, MaterialIndex, Volume->CurrentStateVersion);
    PendingOperations.Add(OpId, NewOp);
    
    if (bServerMode)
    {
        // In server mode, we can immediately grant the modification request
        UpdateOperationStatus(OpId, ENetworkResult::Success);
        // Lock the region temporarily during modification
        RegionLocks.Add(Region, FPlatformTime::Seconds());
        return ENetworkResult::Success;
    }
    else
    {
        // In client mode, we need to wait for server authorization
        // This would involve sending an RPC to the server and waiting for a response
        // For now, we'll simulate a successful response
        UpdateOperationStatus(OpId, ENetworkResult::Success);
        return ENetworkResult::Success;
    }
}

FNetworkVolumeCoordinator::ENetworkResult FNetworkVolumeCoordinator::SubmitRegionModification(
    const FBox& Region, uint8 MaterialIndex, const TArray<uint8>& DeltaData, uint64 BaseVersion)
{
    if (!Volume || !Serializer)
    {
        UE_LOG(LogTemp, Warning, TEXT("NetworkVolumeCoordinator: Cannot submit modification - Volume or Serializer not initialized"));
        return ENetworkResult::Failure;
    }
    
    // Check version consistency
    if (!ValidateVersionConsistency(BaseVersion))
    {
        UE_LOG(LogTemp, Warning, TEXT("NetworkVolumeCoordinator: Version inconsistency detected, operation rejected"));
        return ENetworkResult::OutOfSync;
    }
    
    if (bServerMode)
    {
        // Server can apply the modification directly
        bool ApplySuccess = Serializer->DeserializeDelta(DeltaData, BaseVersion);
        if (ApplySuccess)
        {
            // Track the modified region and update volume version
            TrackModifiedRegion(Region, MaterialIndex);
            Volume->CurrentStateVersion++;
            
            // Release the region lock if it exists
            RegionLocks.Remove(Region);
            
            return ENetworkResult::Success;
        }
        return ENetworkResult::Failure;
    }
    else
    {
        // Client needs to send this to the server
        // For now, simulate a successful operation
        Volume->CurrentStateVersion++;
        return ENetworkResult::Success;
    }
}

FNetworkVolumeCoordinator::ENetworkResult FNetworkVolumeCoordinator::RequestRegionLock(
    const FBox& Region, float TimeoutSeconds)
{
    // Check if region is already locked
    if (IsRegionLocked(Region))
    {
        return ENetworkResult::Conflict;
    }
    
    // Generate operation ID and create pending operation
    uint64 OpId = GenerateOperationId();
    FPendingOperation NewOp(OpId, EOperationType::Lock, Region, 0, Volume->CurrentStateVersion);
    PendingOperations.Add(OpId, NewOp);
    
    if (bServerMode)
    {
        // In server mode, we can immediately grant the lock
        RegionLocks.Add(Region, FPlatformTime::Seconds() + TimeoutSeconds);
        UpdateOperationStatus(OpId, ENetworkResult::Success);
        return ENetworkResult::Success;
    }
    else
    {
        // In client mode, we need to wait for server authorization
        // For now, simulate a successful response
        UpdateOperationStatus(OpId, ENetworkResult::Success);
        return ENetworkResult::Success;
    }
}

FNetworkVolumeCoordinator::ENetworkResult FNetworkVolumeCoordinator::ReleaseRegionLock(const FBox& Region)
{
    // Check if we have a lock for this region
    if (!IsRegionLocked(Region))
    {
        return ENetworkResult::Failure;
    }
    
    // Generate operation ID and create pending operation
    uint64 OpId = GenerateOperationId();
    FPendingOperation NewOp(OpId, EOperationType::Unlock, Region, 0, Volume->CurrentStateVersion);
    PendingOperations.Add(OpId, NewOp);
    
    // Release the lock
    RegionLocks.Remove(Region);
    UpdateOperationStatus(OpId, ENetworkResult::Success);
    
    return ENetworkResult::Success;
}

FNetworkVolumeCoordinator::ENetworkResult FNetworkVolumeCoordinator::SynchronizeWithServer(uint64 ClientVersion)
{
    if (!Volume || !Serializer || !bServerMode)
    {
        // This operation only makes sense on clients
        return ENetworkResult::Failure;
    }
    
    // Generate operation ID and create pending operation
    uint64 OpId = GenerateOperationId();
    FPendingOperation NewOp(OpId, EOperationType::Sync, FBox(FVector::ZeroVector, FVector::ZeroVector), 0, ClientVersion);
    PendingOperations.Add(OpId, NewOp);
    
    // In a real implementation, this would send a request to the server
    // and the server would respond with the delta between ClientVersion and server's current version
    
    // For now, just mark the operation as successful
    UpdateOperationStatus(OpId, ENetworkResult::Success);
    return ENetworkResult::Success;
}

TArray<uint8> FNetworkVolumeCoordinator::GenerateServerUpdate(uint64 BaseVersion)
{
    TArray<uint8> Delta;
    
    if (!Volume || !Serializer || !bServerMode)
    {
        UE_LOG(LogTemp, Warning, TEXT("NetworkVolumeCoordinator: Cannot generate server update - required components not initialized or not in server mode"));
        return Delta;
    }
    
    // Use serializer to generate a delta between BaseVersion and current version
    Delta = Serializer->SerializeDelta(BaseVersion, Volume->CurrentStateVersion);
    
    return Delta;
}
    
    UpdateOperationStatus(OpId, ENetworkResult::Success);
    return ENetworkResult::Success;
}

TArray<uint8> FNetworkVolumeCoordinator::GenerateServerUpdate(uint64 BaseVersion)
{
    if (!bServerMode)
    {
        UE_LOG(LogTemp, Warning, TEXT("Only server can generate server updates"));
        return TArray<uint8>();
    }
    
    // Use volume to generate delta
    return Volume->GenerateNetworkDelta(BaseVersion, Volume->CurrentStateVersion);
}

FNetworkVolumeCoordinator::ENetworkResult FNetworkVolumeCoordinator::ApplyClientUpdate(
    const TArray<uint8>& DeltaData, uint64 BaseVersion, uint64 ClientId)
{
    if (!bServerMode)
    {
        UE_LOG(LogTemp, Warning, TEXT("Only server can apply client updates"));
        return ENetworkResult::Failure;
    }
    
    // Validate client authority
    EAuthorityLevel ClientAuthority = GetClientAuthority(ClientId);
    if (ClientAuthority == EAuthorityLevel::None || ClientAuthority == EAuthorityLevel::ReadOnly)
    {
        UE_LOG(LogTemp, Warning, TEXT("Client %llu doesn't have write authority"), ClientId);
        return ENetworkResult::Failure;
    }
    
    // Check for version conflicts
    if (BaseVersion != Volume->CurrentStateVersion)
    {
        UE_LOG(LogTemp, Warning, TEXT("Client update has version mismatch: Client %llu based on %llu, current is %llu"), 
               ClientId, BaseVersion, Volume->CurrentStateVersion);
        return ENetworkResult::OutOfSync;
    }
    
    // Apply the update and increment version
    Volume->ApplyNetworkDelta(DeltaData, BaseVersion, Volume->CurrentStateVersion + 1);
    Volume->CurrentStateVersion++;
    
    // In a real implementation, this would broadcast the update to other clients
    
    return ENetworkResult::Success;
}

void FNetworkVolumeCoordinator::SetClientAuthority(uint64 ClientId, EAuthorityLevel Authority)
{
    ClientAuthorities.Add(ClientId, Authority);
}

FNetworkVolumeCoordinator::EAuthorityLevel FNetworkVolumeCoordinator::GetClientAuthority(uint64 ClientId) const
{
    if (ClientAuthorities.Contains(ClientId))
    {
        return ClientAuthorities[ClientId];
    }
    return EAuthorityLevel::None;
}

bool FNetworkVolumeCoordinator::IsClientAuthorized(uint64 ClientId, const FBox& Region) const
{
    // Admin clients can modify anything
    EAuthorityLevel Authority = GetClientAuthority(ClientId);
    if (Authority == EAuthorityLevel::Admin)
    {
        return true;
    }
    
    // ReadOnly clients can't modify anything
    if (Authority == EAuthorityLevel::None || Authority == EAuthorityLevel::ReadOnly)
    {
        return false;
    }
    
    // Check if the region is locked by someone else
    for (const auto& Pair : RegionLocks)
    {
        if (CheckRegionOverlap(Pair.Key, Region))
        {
            // In a real implementation, we'd check if the lock belongs to this client
            return false;
        }
    }
    
    return true;
}

void FNetworkVolumeCoordinator::SetConflictResolutionStrategy(uint8 Strategy)
{
    ConflictResolutionStrategy = Strategy;
}

void FNetworkVolumeCoordinator::RegisterConflictHandler(TFunction<bool(const FBox&, uint8, uint64)> Handler)
{
    ConflictHandler = Handler;
}

bool FNetworkVolumeCoordinator::ResolveConflict(const FBox& Region, uint8 MaterialIndex, uint64 ClientId)
{
    // If we have a custom conflict handler, use it
    if (ConflictHandler)
    {
        return ConflictHandler(Region, MaterialIndex, ClientId);
    }
    
    // Otherwise use the configured strategy
    switch (ConflictResolutionStrategy)
    {
        case 0: // Server wins
            return bServerMode;
        
        case 1: // First writer wins
            // Check if region is already being modified
            for (const auto& Pair : ActiveRegions)
            {
                if (CheckRegionOverlap(Pair.Key, Region))
                {
                    return false; // Someone else got there first
                }
            }
            return true;
            
        case 2: // Priority-based (higher authority client wins)
            {
                // In a real implementation, we'd compare client priorities
                EAuthorityLevel ClientAuthority = GetClientAuthority(ClientId);
                return ClientAuthority >= EAuthorityLevel::ReadWrite;
            }
            
        default:
            return bServerMode; // Default to server authority
    }
}

bool FNetworkVolumeCoordinator::IsRegionLocked(const FBox& Region) const
{
    // Clean up any stale locks first
    const_cast<FNetworkVolumeCoordinator*>(this)->CleanupStaleRegionLocks();
    
    // Check for overlapping locks
    for (const auto& Pair : RegionLocks)
    {
        if (CheckRegionOverlap(Pair.Key, Region))
        {
            return true;
        }
    }
    
    return false;
}

bool FNetworkVolumeCoordinator::IsRegionModifiedSince(const FBox& Region, uint64 BaseVersion) const
{
    // Check for modifications in the active regions
    for (const auto& Pair : ActiveRegions)
    {
        if (CheckRegionOverlap(Pair.Key, Region))
        {
            // In a real implementation, we would check version history
            // For this prototype, we'll assume any active region has been modified
            return true;
        }
    }
    
    return false;
}

TArray<FBox> FNetworkVolumeCoordinator::GetModifiedRegions(uint64 BaseVersion) const
{
    TArray<FBox> ModifiedRegions;
    
    // Add all active regions to the list
    for (const auto& Pair : ActiveRegions)
    {
        ModifiedRegions.Add(Pair.Key);
    }
    
    return ModifiedRegions;
}

TMap<FBox, uint8> FNetworkVolumeCoordinator::GetActiveRegions() const
{
    return ActiveRegions;
}

uint64 FNetworkVolumeCoordinator::GetPendingOperationCount() const
{
    return PendingOperations.Num();
}

FNetworkVolumeCoordinator::ENetworkResult FNetworkVolumeCoordinator::GetOperationResult(uint64 OperationId) const
{
    if (PendingOperations.Contains(OperationId))
    {
        return PendingOperations[OperationId].Result;
    }
    
    return ENetworkResult::Failure;
}

void FNetworkVolumeCoordinator::CancelOperation(uint64 OperationId)
{
    if (PendingOperations.Contains(OperationId))
    {
        PendingOperations[OperationId].bCompleted = true;
        PendingOperations[OperationId].Result = ENetworkResult::Failure;
    }
}

void FNetworkVolumeCoordinator::CleanupCompletedOperations(float TimeThresholdSeconds)
{
    double CurrentTime = FPlatformTime::Seconds();
    TArray<uint64> OperationsToRemove;
    
    for (const auto& Pair : PendingOperations)
    {
        const FPendingOperation& Op = Pair.Value;
        if (Op.bCompleted && (CurrentTime - Op.Timestamp > TimeThresholdSeconds))
        {
            OperationsToRemove.Add(Pair.Key);
        }
    }
    
    for (uint64 OpId : OperationsToRemove)
    {
        PendingOperations.Remove(OpId);
    }
}

bool FNetworkVolumeCoordinator::CheckRegionOverlap(const FBox& RegionA, const FBox& RegionB) const
{
    return RegionA.Intersect(RegionB);
}

uint64 FNetworkVolumeCoordinator::GenerateOperationId() const
{
    // Generate a unique ID based on timestamp and a random component
    uint64 TimeComponent = static_cast<uint64>(FPlatformTime::Seconds() * 1000.0);
    uint64 RandomComponent = static_cast<uint64>(FMath::Rand()) & 0xFFFF;
    
    return (TimeComponent << 16) | RandomComponent;
}

void FNetworkVolumeCoordinator::TrackModifiedRegion(const FBox& Region, uint8 MaterialIndex)
{
    // Add or update the region in active regions
    ActiveRegions.Add(Region, MaterialIndex);
}

void FNetworkVolumeCoordinator::UpdateOperationStatus(uint64 OperationId, ENetworkResult Result)
{
    if (PendingOperations.Contains(OperationId))
    {
        PendingOperations[OperationId].Result = Result;
        PendingOperations[OperationId].bCompleted = (Result != ENetworkResult::Pending);
    }
}

void FNetworkVolumeCoordinator::CleanupStaleRegionLocks()
{
    double CurrentTime = FPlatformTime::Seconds();
    TArray<FBox> LocksToRemove;
    
    for (const auto& Pair : RegionLocks)
    {
        if (Pair.Value < CurrentTime)
        {
            LocksToRemove.Add(Pair.Key);
        }
    }
    
    for (const FBox& LockRegion : LocksToRemove)
    {
        RegionLocks.Remove(LockRegion);
    }
}

bool FNetworkVolumeCoordinator::ValidateVersionConsistency(uint64 BaseVersion) const
{
    // In simple case, just check if the base version matches our current version
    return BaseVersion == Volume->CurrentStateVersion;
}

FNetworkVolumeCoordinator::ENetworkResult FNetworkVolumeCoordinator::ProcessModificationRequest(
    const FBox& Region, uint8 MaterialIndex, uint64 ClientId)
{
    // Validate client authority
    if (!IsClientAuthorized(ClientId, Region))
    {
        UE_LOG(LogTemp, Warning, TEXT("Client %llu not authorized to modify region"), ClientId);
        return ENetworkResult::Failure;
    }
    
    // Check for conflicts
    if (IsRegionLocked(Region))
    {
        // Try to resolve the conflict
        if (!ResolveConflict(Region, MaterialIndex, ClientId))
        {
            UE_LOG(LogTemp, Warning, TEXT("Conflict resolution failed for region modification"));
            return ENetworkResult::Conflict;
        }
    }
    
    // If we got here, the modification is approved
    TrackModifiedRegion(Region, MaterialIndex);
    return ENetworkResult::Success;
}