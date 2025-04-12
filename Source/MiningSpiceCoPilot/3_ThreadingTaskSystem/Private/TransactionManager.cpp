// Copyright Epic Games, Inc. All Rights Reserved.

#include "TransactionManager.h"
#include "Misc/ScopeLock.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"

// Initialize static members
FTransactionManager* FTransactionManager::Instance = nullptr;
uint32 FTransactionManager::CurrentTransactionTLS = FPlatformTLS::AllocTlsSlot();

// Implementation of FMiningTransactionContextImpl

FMiningTransactionContextImpl::FMiningTransactionContextImpl(uint64 InTransactionId, const FTransactionConfig& InConfig)
    : TransactionId(InTransactionId)
    , Status(ETransactionStatus::NotStarted)
    , Config(InConfig)
{
    // Initialize timestamps
    StartTime = 0.0;
    CommitStartTime = 0.0;
    CommitEndTime = 0.0;
    
    // Initialize statistics
    Stats.StartTimeMs = 0.0;
    Stats.CommitTimeMs = 0.0;
    Stats.ValidationTimeMs = 0.0;
    Stats.ConflictCount = 0;
    Stats.RetryCount = 0;
    Stats.LockWaitTimeMs = 0.0;
    Stats.ValidationCount = 0;
    Stats.TransactionSizeBytes = 0;
    Stats.PeakMemoryBytes = 0;
}

FMiningTransactionContextImpl::~FMiningTransactionContextImpl()
{
    // Nothing to clean up
}

uint64 FMiningTransactionContextImpl::GetTransactionId() const
{
    return TransactionId;
}

ETransactionStatus FMiningTransactionContextImpl::GetStatus() const
{
    FScopeLock Lock(&Lock);
    return Status;
}

bool FMiningTransactionContextImpl::AddToReadSet(int32 ZoneId, int32 MaterialId)
{
    FScopeLock Lock(&Lock);
    
    // Check if transaction is active
    if (Status != ETransactionStatus::Active)
    {
        return false;
    }
    
    // Create a version record for this read
    FVersionRecord Record;
    Record.ZoneId = ZoneId;
    Record.MaterialId = MaterialId;
    Record.Version = 0; // Will be filled by the transaction manager
    Record.bIsReadOnly = true;
    
    // Add to read set if not already present
    bool bExists = false;
    for (const FVersionRecord& Existing : ReadSet)
    {
        if (Existing.ZoneId == ZoneId && Existing.MaterialId == MaterialId)
        {
            bExists = true;
            break;
        }
    }
    
    if (!bExists)
    {
        ReadSet.Add(Record);
    }
    
    return true;
}

bool FMiningTransactionContextImpl::AddToWriteSet(int32 ZoneId, int32 MaterialId)
{
    FScopeLock Lock(&Lock);
    
    // Check if transaction is active
    if (Status != ETransactionStatus::Active)
    {
        return false;
    }
    
    // Create a version record for this write
    FVersionRecord Record;
    Record.ZoneId = ZoneId;
    Record.MaterialId = MaterialId;
    Record.Version = 0; // Will be filled by the transaction manager
    Record.bIsReadOnly = false;
    
    // Add to write set if not already present
    bool bExists = false;
    for (const FVersionRecord& Existing : WriteSet)
    {
        if (Existing.ZoneId == ZoneId && Existing.MaterialId == MaterialId)
        {
            bExists = true;
            break;
        }
    }
    
    if (!bExists)
    {
        WriteSet.Add(Record);
        
        // Any write also implies a read
        AddToReadSet(ZoneId, MaterialId);
    }
    
    return true;
}

FTransactionStats FMiningTransactionContextImpl::GetStats() const
{
    FScopeLock Lock(&Lock);
    return Stats;
}

const FTransactionConfig& FMiningTransactionContextImpl::GetConfig() const
{
    return Config;
}

TArray<FTransactionConflict> FMiningTransactionContextImpl::GetConflicts() const
{
    FScopeLock Lock(&Lock);
    return Conflicts;
}

void FMiningTransactionContextImpl::SetName(const FString& InName)
{
    FScopeLock Lock(&Lock);
    Name = InName;
}

FString FMiningTransactionContextImpl::GetName() const
{
    FScopeLock Lock(&Lock);
    return Name;
}

void FMiningTransactionContextImpl::SetStatus(ETransactionStatus NewStatus)
{
    FScopeLock Lock(&Lock);
    Status = NewStatus;
    
    // Update timestamps based on state transitions
    double CurrentTime = FPlatformTime::Seconds();
    
    if (Status == ETransactionStatus::Active && StartTime == 0.0)
    {
        StartTime = CurrentTime;
        Stats.StartTimeMs = StartTime * 1000.0;
    }
    else if (Status == ETransactionStatus::Committing && CommitStartTime == 0.0)
    {
        CommitStartTime = CurrentTime;
    }
    else if ((Status == ETransactionStatus::Committed || Status == ETransactionStatus::Aborted) && CommitEndTime == 0.0)
    {
        CommitEndTime = CurrentTime;
        
        if (CommitStartTime > 0.0)
        {
            Stats.CommitTimeMs = (CommitEndTime - CommitStartTime) * 1000.0;
        }
    }
}

const TArray<FVersionRecord>& FMiningTransactionContextImpl::GetReadSet() const
{
    return ReadSet;
}

const TArray<FVersionRecord>& FMiningTransactionContextImpl::GetWriteSet() const
{
    return WriteSet;
}

void FMiningTransactionContextImpl::AddConflict(const FTransactionConflict& Conflict)
{
    FScopeLock Lock(&Lock);
    Conflicts.Add(Conflict);
    Stats.ConflictCount++;
}

double FMiningTransactionContextImpl::GetStartTime() const
{
    return StartTime;
}

double FMiningTransactionContextImpl::GetCommitTime() const
{
    return CommitEndTime;
}

void FMiningTransactionContextImpl::SetCommitTime(double InCommitTime)
{
    CommitEndTime = InCommitTime;
}

uint32 FMiningTransactionContextImpl::IncrementRetryCount()
{
    Stats.RetryCount++;
    return Stats.RetryCount;
}

void FMiningTransactionContextImpl::RecordLockWaitTime(double WaitTimeMs)
{
    Stats.LockWaitTimeMs += WaitTimeMs;
}

void FMiningTransactionContextImpl::RecordValidation()
{
    Stats.ValidationCount++;
}

void FMiningTransactionContextImpl::SetPeakMemoryUsage(uint64 MemoryUsageBytes)
{
    Stats.PeakMemoryBytes = MemoryUsageBytes;
}

// Implementation of FTransactionManager

FTransactionManager::FTransactionManager()
    : bIsInitialized(false)
    , TotalTransactions(0)
    , CommittedTransactions(0)
    , AbortedTransactions(0)
    , ConflictCount(0)
{
    // Store singleton instance
    Instance = this;
    
    // Initialize transaction ID counter
    NextTransactionId.Set(1); // Start IDs from 1
}

FTransactionManager::~FTransactionManager()
{
    // Shutdown if still initialized
    if (bIsInitialized)
    {
        Shutdown();
    }
    
    // Clear singleton instance if it's this instance
    if (Instance == this)
    {
        Instance = nullptr;
    }
}

bool FTransactionManager::Initialize()
{
    // Check if already initialized
    if (bIsInitialized)
    {
        return true;
    }
    
    // Initialize transaction statistics
    TotalTransactions = 0;
    CommittedTransactions = 0;
    AbortedTransactions = 0;
    ConflictCount = 0;
    
    // Initialize default fast path thresholds
    FastPathThresholds.Add(0, 0.1f); // Default threshold for all transaction types
    
    // Mark as initialized
    bIsInitialized = true;
    
    UE_LOG(LogTemp, Log, TEXT("Transaction Manager initialized"));
    
    return true;
}

void FTransactionManager::Shutdown()
{
    // Check if initialized
    if (!bIsInitialized)
    {
        return;
    }
    
    // Abort all active transactions
    {
        FScopeLock Lock(&TransactionLock);
        
        // Abort all active transactions
        for (auto& Pair : ActiveTransactions)
        {
            FMiningTransactionContextImpl* Transaction = Pair.Value;
            if (Transaction)
            {
                if (Transaction->GetStatus() == ETransactionStatus::Active ||
                    Transaction->GetStatus() == ETransactionStatus::Committing)
                {
                    Transaction->SetStatus(ETransactionStatus::Aborted);
                    AbortedTransactions++;
                }
                delete Transaction;
            }
        }
        
        // Clear the active transaction map
        ActiveTransactions.Empty();
    }
    
    // Clean up zone locks and version counters
    {
        FScopeLock Lock(&ZoneLock);
        
        // Delete zone locks
        for (auto& Pair : ZoneLocks)
        {
            delete Pair.Value;
        }
        ZoneLocks.Empty();
        
        // Delete zone version counters
        for (auto& Pair : ZoneVersions)
        {
            delete Pair.Value;
        }
        ZoneVersions.Empty();
        
        // Delete material version counters
        for (auto& Pair : MaterialVersions)
        {
            delete Pair.Value;
        }
        MaterialVersions.Empty();
    }
    
    // Reset conflict stats
    ZoneConflicts.Empty();
    
    // Mark as not initialized
    bIsInitialized = false;
    
    UE_LOG(LogTemp, Log, TEXT("Transaction Manager shutdown"));
}

bool FTransactionManager::IsInitialized() const
{
    return bIsInitialized;
}

bool FTransactionManager::BeginTransaction(const FTransactionConfig& Config, FMiningTransactionContext*& OutContext)
{
    // Check if initialized
    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Error, TEXT("Transaction Manager not initialized"));
        OutContext = nullptr;
        return false;
    }
    
    // Generate a transaction ID
    uint64 TransactionId = GenerateTransactionId();
    
    // Create the transaction context
    FMiningTransactionContextImpl* Transaction = new FMiningTransactionContextImpl(TransactionId, Config);
    Transaction->SetStatus(ETransactionStatus::Active);
    
    // Store in active transactions
    {
        FScopeLock Lock(&TransactionLock);
        ActiveTransactions.Add(TransactionId, Transaction);
    }
    
    // Set as current transaction for this thread
    FPlatformTLS::SetTlsValue(CurrentTransactionTLS, Transaction);
    
    // Update statistics
    TotalTransactions++;
    
    // Set the output parameter
    OutContext = Transaction;
    
    UE_LOG(LogTemp, Verbose, TEXT("Transaction %llu started"), TransactionId);
    
    return true;
}

bool FTransactionManager::CommitTransaction(FMiningTransactionContext* Context)
{
    // Check context
    if (!Context)
    {
        UE_LOG(LogTemp, Warning, TEXT("Cannot commit null transaction"));
        return false;
    }
    
    // Cast to our implementation
    FMiningTransactionContextImpl* Transaction = static_cast<FMiningTransactionContextImpl*>(Context);
    
    // Check if transaction is active
    if (Transaction->GetStatus() != ETransactionStatus::Active)
    {
        UE_LOG(LogTemp, Warning, TEXT("Cannot commit transaction %llu: not active"), Transaction->GetTransactionId());
        return false;
    }
    
    // Set status to committing
    Transaction->SetStatus(ETransactionStatus::Committing);
    
    bool bFastPath = ShouldUseFastPath(Transaction);
    bool bSuccess = true;
    
    if (bFastPath)
    {
        // Fast path - skip validation and directly apply updates
        UE_LOG(LogTemp, Verbose, TEXT("Fast path commit for transaction %llu"), Transaction->GetTransactionId());
        
        // Take all locks in order
        const TArray<FVersionRecord>& WriteSet = Transaction->GetWriteSet();
        TArray<FSpinLock*> LocksHeld;
        TArray<int32> ZoneIdsLocked;
        
        // Sort write set by zone ID to prevent deadlocks
        TArray<FVersionRecord> SortedWriteSet = WriteSet;
        SortedWriteSet.Sort([](const FVersionRecord& A, const FVersionRecord& B) {
            return A.ZoneId < B.ZoneId;
        });
        
        // Acquire locks
        double LockStartTime = FPlatformTime::Seconds();
        bool bAllLocksAcquired = true;
        
        for (const FVersionRecord& Record : SortedWriteSet)
        {
            // Skip if zone already locked
            if (ZoneIdsLocked.Contains(Record.ZoneId))
            {
                continue;
            }
            
            // Get the lock
            FSpinLock* Lock = GetZoneLock(Record.ZoneId);
            if (!Lock)
            {
                UE_LOG(LogTemp, Warning, TEXT("Failed to get lock for zone %d"), Record.ZoneId);
                bAllLocksAcquired = false;
                break;
            }
            
            // Try to acquire the lock
            Lock->Lock();
            LocksHeld.Add(Lock);
            ZoneIdsLocked.Add(Record.ZoneId);
        }
        
        // Record lock wait time
        double LockEndTime = FPlatformTime::Seconds();
        double LockWaitTimeMs = (LockEndTime - LockStartTime) * 1000.0;
        Transaction->RecordLockWaitTime(LockWaitTimeMs);
        
        if (!bAllLocksAcquired)
        {
            // Release all locks in reverse order
            for (int32 i = LocksHeld.Num() - 1; i >= 0; --i)
            {
                LocksHeld[i]->Unlock();
            }
            
            // Abort the transaction
            Transaction->SetStatus(ETransactionStatus::Aborted);
            AbortedTransactions++;
            
            UE_LOG(LogTemp, Warning, TEXT("Transaction %llu aborted: failed to acquire all locks"), Transaction->GetTransactionId());
            return false;
        }
        
        // Update versions
        UpdateVersions(Transaction);
        
        // Release all locks in reverse order
        for (int32 i = LocksHeld.Num() - 1; i >= 0; --i)
        {
            LocksHeld[i]->Unlock();
        }
        
        // Mark transaction as committed
        Transaction->SetStatus(ETransactionStatus::Committed);
        Transaction->SetCommitTime(FPlatformTime::Seconds());
        CommittedTransactions++;
        
        UE_LOG(LogTemp, Verbose, TEXT("Transaction %llu committed (fast path)"), Transaction->GetTransactionId());
    }
    else
    {
        // Normal path - validate and commit
        
        // Validate the transaction
        TArray<FTransactionConflict> Conflicts;
        bool bValid = ValidateReadSet(Transaction, Conflicts);
        
        // Record validation
        Transaction->RecordValidation();
        
        if (!bValid)
        {
            // Add conflicts to transaction
            for (const FTransactionConflict& Conflict : Conflicts)
            {
                Transaction->AddConflict(Conflict);
                RecordConflict(Conflict.ZoneId);
            }
            
            // Abort the transaction
            Transaction->SetStatus(ETransactionStatus::Aborted);
            AbortedTransactions++;
            
            UE_LOG(LogTemp, Warning, TEXT("Transaction %llu aborted: validation failed with %d conflicts"), Transaction->GetTransactionId(), Conflicts.Num());
            return false;
        }
        
        // Take all locks in order
        const TArray<FVersionRecord>& WriteSet = Transaction->GetWriteSet();
        TArray<FSpinLock*> LocksHeld;
        TArray<int32> ZoneIdsLocked;
        
        // Sort write set by zone ID to prevent deadlocks
        TArray<FVersionRecord> SortedWriteSet = WriteSet;
        SortedWriteSet.Sort([](const FVersionRecord& A, const FVersionRecord& B) {
            return A.ZoneId < B.ZoneId;
        });
        
        // Acquire locks
        double LockStartTime = FPlatformTime::Seconds();
        bool bAllLocksAcquired = true;
        
        for (const FVersionRecord& Record : SortedWriteSet)
        {
            // Skip if zone already locked
            if (ZoneIdsLocked.Contains(Record.ZoneId))
            {
                continue;
            }
            
            // Get the lock
            FSpinLock* Lock = GetZoneLock(Record.ZoneId);
            if (!Lock)
            {
                UE_LOG(LogTemp, Warning, TEXT("Failed to get lock for zone %d"), Record.ZoneId);
                bAllLocksAcquired = false;
                break;
            }
            
            // Try to acquire the lock
            Lock->Lock();
            LocksHeld.Add(Lock);
            ZoneIdsLocked.Add(Record.ZoneId);
        }
        
        // Record lock wait time
        double LockEndTime = FPlatformTime::Seconds();
        double LockWaitTimeMs = (LockEndTime - LockStartTime) * 1000.0;
        Transaction->RecordLockWaitTime(LockWaitTimeMs);
        
        if (!bAllLocksAcquired)
        {
            // Release all locks in reverse order
            for (int32 i = LocksHeld.Num() - 1; i >= 0; --i)
            {
                LocksHeld[i]->Unlock();
            }
            
            // Abort the transaction
            Transaction->SetStatus(ETransactionStatus::Aborted);
            AbortedTransactions++;
            
            UE_LOG(LogTemp, Warning, TEXT("Transaction %llu aborted: failed to acquire all locks"), Transaction->GetTransactionId());
            return false;
        }
        
        // Validate again while holding locks to ensure consistency
        bValid = ValidateReadSet(Transaction, Conflicts);
        
        // Record validation
        Transaction->RecordValidation();
        
        if (!bValid)
        {
            // Release all locks in reverse order
            for (int32 i = LocksHeld.Num() - 1; i >= 0; --i)
            {
                LocksHeld[i]->Unlock();
            }
            
            // Add conflicts to transaction
            for (const FTransactionConflict& Conflict : Conflicts)
            {
                Transaction->AddConflict(Conflict);
                RecordConflict(Conflict.ZoneId);
            }
            
            // Abort the transaction
            Transaction->SetStatus(ETransactionStatus::Aborted);
            AbortedTransactions++;
            
            UE_LOG(LogTemp, Warning, TEXT("Transaction %llu aborted: validation failed during lock phase"), Transaction->GetTransactionId());
            return false;
        }
        
        // Update versions
        UpdateVersions(Transaction);
        
        // Release all locks in reverse order
        for (int32 i = LocksHeld.Num() - 1; i >= 0; --i)
        {
            LocksHeld[i]->Unlock();
        }
        
        // Mark transaction as committed
        Transaction->SetStatus(ETransactionStatus::Committed);
        Transaction->SetCommitTime(FPlatformTime::Seconds());
        CommittedTransactions++;
        
        UE_LOG(LogTemp, Verbose, TEXT("Transaction %llu committed"), Transaction->GetTransactionId());
    }
    
    // Clear current transaction TLS
    if (FPlatformTLS::GetTlsValue(CurrentTransactionTLS) == Transaction)
    {
        FPlatformTLS::SetTlsValue(CurrentTransactionTLS, nullptr);
    }
    
    return true;
}

void FTransactionManager::AbortTransaction(FMiningTransactionContext* Context)
{
    // Check context
    if (!Context)
    {
        UE_LOG(LogTemp, Warning, TEXT("Cannot abort null transaction"));
        return;
    }
    
    // Cast to our implementation
    FMiningTransactionContextImpl* Transaction = static_cast<FMiningTransactionContextImpl*>(Context);
    
    // Check if transaction is active or committing
    ETransactionStatus Status = Transaction->GetStatus();
    if (Status != ETransactionStatus::Active && Status != ETransactionStatus::Committing)
    {
        UE_LOG(LogTemp, Warning, TEXT("Cannot abort transaction %llu: not active or committing"), Transaction->GetTransactionId());
        return;
    }
    
    // Mark transaction as aborted
    Transaction->SetStatus(ETransactionStatus::Aborted);
    AbortedTransactions++;
    
    UE_LOG(LogTemp, Verbose, TEXT("Transaction %llu aborted"), Transaction->GetTransactionId());
    
    // Clear current transaction TLS
    if (FPlatformTLS::GetTlsValue(CurrentTransactionTLS) == Transaction)
    {
        FPlatformTLS::SetTlsValue(CurrentTransactionTLS, nullptr);
    }
}

bool FTransactionManager::ValidateTransaction(FMiningTransactionContext* Context)
{
    // Check context
    if (!Context)
    {
        UE_LOG(LogTemp, Warning, TEXT("Cannot validate null transaction"));
        return false;
    }
    
    // Cast to our implementation
    FMiningTransactionContextImpl* Transaction = static_cast<FMiningTransactionContextImpl*>(Context);
    
    // Check if transaction is active
    if (Transaction->GetStatus() != ETransactionStatus::Active)
    {
        UE_LOG(LogTemp, Warning, TEXT("Cannot validate transaction %llu: not active"), Transaction->GetTransactionId());
        return false;
    }
    
    // Validate the read set
    TArray<FTransactionConflict> Conflicts;
    bool bValid = ValidateReadSet(Transaction, Conflicts);
    
    // Record validation
    Transaction->RecordValidation();
    
    if (!bValid)
    {
        // Add conflicts to transaction
        for (const FTransactionConflict& Conflict : Conflicts)
        {
            Transaction->AddConflict(Conflict);
        }
    }
    
    return bValid;
}

FMiningTransactionContext* FTransactionManager::GetCurrentTransaction()
{
    return static_cast<FMiningTransactionContext*>(FPlatformTLS::GetTlsValue(CurrentTransactionTLS));
}

FMiningTransactionContext* FTransactionManager::GetTransaction(uint64 TransactionId)
{
    FScopeLock Lock(&TransactionLock);
    
    // Find the transaction
    FMiningTransactionContextImpl** TransactionPtr = ActiveTransactions.Find(TransactionId);
    return TransactionPtr ? *TransactionPtr : nullptr;
}

TMap<FString, double> FTransactionManager::GetGlobalStats() const
{
    TMap<FString, double> Stats;
    
    // Transaction counts
    Stats.Add(TEXT("TotalTransactions"), static_cast<double>(TotalTransactions));
    Stats.Add(TEXT("CommittedTransactions"), static_cast<double>(CommittedTransactions));
    Stats.Add(TEXT("AbortedTransactions"), static_cast<double>(AbortedTransactions));
    Stats.Add(TEXT("ConflictCount"), static_cast<double>(ConflictCount));
    
    // Calculate abort rate
    double AbortRate = (TotalTransactions > 0) ? 
        static_cast<double>(AbortedTransactions) / static_cast<double>(TotalTransactions) : 0.0;
    Stats.Add(TEXT("AbortRate"), AbortRate);
    
    // Active transaction count
    uint32 ActiveCount = GetActiveTransactionCount();
    Stats.Add(TEXT("ActiveTransactions"), static_cast<double>(ActiveCount));
    
    return Stats;
}

uint32 FTransactionManager::GetActiveTransactionCount() const
{
    FScopeLock Lock(&TransactionLock);
    return ActiveTransactions.Num();
}

float FTransactionManager::GetTransactionAbortRate() const
{
    if (TotalTransactions == 0)
    {
        return 0.0f;
    }
    
    return static_cast<float>(AbortedTransactions) / static_cast<float>(TotalTransactions);
}

TMap<int32, uint32> FTransactionManager::GetZoneConflictStats() const
{
    FScopeLock Lock(const_cast<FCriticalSection*>(&ZoneLock));
    return ZoneConflicts;
}

FSpinLock* FTransactionManager::GetZoneLock(int32 ZoneId)
{
    return GetOrCreateZoneLock(ZoneId);
}

bool FTransactionManager::UpdateFastPathThreshold(uint32 TypeId, float ConflictRate)
{
    FScopeLock Lock(&ZoneLock);
    
    // Update the fast path threshold for this transaction type
    FastPathThresholds.Add(TypeId, ConflictRate);
    return true;
}

ITransactionManager& FTransactionManager::Get()
{
    if (!Instance)
    {
        Instance = new FTransactionManager();
        Instance->Initialize();
    }
    
    return *Instance;
}

uint64 FTransactionManager::GenerateTransactionId()
{
    // Generate a unique transaction ID
    return NextTransactionId.Increment();
}

FSpinLock* FTransactionManager::GetOrCreateZoneLock(int32 ZoneId)
{
    FScopeLock Lock(&ZoneLock);
    
    // Find existing lock
    FSpinLock** LockPtr = ZoneLocks.Find(ZoneId);
    if (LockPtr)
    {
        return *LockPtr;
    }
    
    // Create a new lock
    FSpinLock* NewLock = new FSpinLock();
    ZoneLocks.Add(ZoneId, NewLock);
    return NewLock;
}

FThreadSafeCounter* FTransactionManager::GetOrCreateZoneVersion(int32 ZoneId)
{
    FScopeLock Lock(&ZoneLock);
    
    // Find existing counter
    FThreadSafeCounter** CounterPtr = ZoneVersions.Find(ZoneId);
    if (CounterPtr)
    {
        return *CounterPtr;
    }
    
    // Create a new counter
    FThreadSafeCounter* NewCounter = new FThreadSafeCounter(1); // Start at version 1
    ZoneVersions.Add(ZoneId, NewCounter);
    return NewCounter;
}

FThreadSafeCounter* FTransactionManager::GetOrCreateMaterialVersion(int32 ZoneId, int32 MaterialId)
{
    FScopeLock Lock(&ZoneLock);
    
    // Generate key
    FString Key = FString::Printf(TEXT("%d:%d"), ZoneId, MaterialId);
    
    // Find existing counter
    FThreadSafeCounter** CounterPtr = MaterialVersions.Find(Key);
    if (CounterPtr)
    {
        return *CounterPtr;
    }
    
    // Create a new counter
    FThreadSafeCounter* NewCounter = new FThreadSafeCounter(1); // Start at version 1
    MaterialVersions.Add(Key, NewCounter);
    return NewCounter;
}

FVersionRecord FTransactionManager::GetVersionRecord(int32 ZoneId, int32 MaterialId, bool bIsReadOnly)
{
    FVersionRecord Record;
    Record.ZoneId = ZoneId;
    Record.MaterialId = MaterialId;
    Record.bIsReadOnly = bIsReadOnly;
    
    // Get appropriate version counter
    if (MaterialId != INDEX_NONE)
    {
        // Material-specific version
        FThreadSafeCounter* MaterialCounter = GetOrCreateMaterialVersion(ZoneId, MaterialId);
        Record.Version = MaterialCounter->GetValue();
    }
    else
    {
        // Zone version
        FThreadSafeCounter* ZoneCounter = GetOrCreateZoneVersion(ZoneId);
        Record.Version = ZoneCounter->GetValue();
    }
    
    return Record;
}

void FTransactionManager::UpdateVersions(const FMiningTransactionContextImpl* Transaction)
{
    // Increment version for each modified zone/material
    for (const FVersionRecord& Record : Transaction->GetWriteSet())
    {
        if (Record.MaterialId != INDEX_NONE)
        {
            // Material-specific version
            FThreadSafeCounter* MaterialCounter = GetOrCreateMaterialVersion(Record.ZoneId, Record.MaterialId);
            MaterialCounter->Increment();
        }
        else
        {
            // Zone version
            FThreadSafeCounter* ZoneCounter = GetOrCreateZoneVersion(Record.ZoneId);
            ZoneCounter->Increment();
        }
    }
}

bool FTransactionManager::ValidateReadSet(const FMiningTransactionContextImpl* Transaction, TArray<FTransactionConflict>& OutConflicts)
{
    double StartTime = FPlatformTime::Seconds();
    
    // Get the read set
    const TArray<FVersionRecord>& ReadSet = Transaction->GetReadSet();
    
    // Check each record in the read set
    for (const FVersionRecord& Record : ReadSet)
    {
        uint32 CurrentVersion;
        
        if (Record.MaterialId != INDEX_NONE)
        {
            // Material-specific version
            FThreadSafeCounter* MaterialCounter = GetOrCreateMaterialVersion(Record.ZoneId, Record.MaterialId);
            CurrentVersion = MaterialCounter->GetValue();
        }
        else
        {
            // Zone version
            FThreadSafeCounter* ZoneCounter = GetOrCreateZoneVersion(Record.ZoneId);
            CurrentVersion = ZoneCounter->GetValue();
        }
        
        // Check for version mismatch
        if (CurrentVersion != Record.Version)
        {
            // Create a conflict record
            FTransactionConflict Conflict;
            Conflict.ZoneId = Record.ZoneId;
            Conflict.MaterialId = Record.MaterialId;
            Conflict.ExpectedVersion = Record.Version;
            Conflict.ActualVersion = CurrentVersion;
            Conflict.ConflictType = ETransactionConflictType::VersionMismatch;
            
            OutConflicts.Add(Conflict);
            ConflictCount++;
        }
    }
    
    // Calculate validation time
    double EndTime = FPlatformTime::Seconds();
    double ValidationTimeMs = (EndTime - StartTime) * 1000.0;
    
    // Record validation time in transaction statistics
    const_cast<FMiningTransactionContextImpl*>(Transaction)->Stats.ValidationTimeMs += ValidationTimeMs;
    
    // Return true if no conflicts were found
    return OutConflicts.Num() == 0;
}

void FTransactionManager::CleanupCompletedTransactions(double MaxAgeSeconds)
{
    const double CurrentTime = FPlatformTime::Seconds();
    
    FScopeLock Lock(&TransactionLock);
    
    TArray<uint64> TransactionsToRemove;
    
    // Find transactions that can be removed
    for (const auto& Pair : ActiveTransactions)
    {
        FMiningTransactionContextImpl* Transaction = Pair.Value;
        
        if (!Transaction)
        {
            TransactionsToRemove.Add(Pair.Key);
            continue;
        }
        
        // Check if the transaction is completed or aborted
        ETransactionStatus Status = Transaction->GetStatus();
        if (Status == ETransactionStatus::Committed || Status == ETransactionStatus::Aborted)
        {
            // Check age
            double CommitTime = Transaction->GetCommitTime();
            if (CommitTime > 0.0)
            {
                double AgeSeconds = CurrentTime - CommitTime;
                if (AgeSeconds > MaxAgeSeconds)
                {
                    TransactionsToRemove.Add(Pair.Key);
                }
            }
        }
    }
    
    // Remove transactions
    for (uint64 TransactionId : TransactionsToRemove)
    {
        FMiningTransactionContextImpl* Transaction = ActiveTransactions.FindAndRemoveChecked(TransactionId);
        delete Transaction;
    }
}

void FTransactionManager::RecordConflict(int32 ZoneId)
{
    FScopeLock Lock(&ZoneLock);
    
    // Increment conflict count for this zone
    uint32* CountPtr = ZoneConflicts.Find(ZoneId);
    if (CountPtr)
    {
        (*CountPtr)++;
    }
    else
    {
        ZoneConflicts.Add(ZoneId, 1);
    }
}

bool FTransactionManager::ShouldUseFastPath(const FMiningTransactionContextImpl* Transaction) const
{
    // If the transaction has no write operations, use the normal path
    if (Transaction->GetWriteSet().Num() == 0)
    {
        return false;
    }
    
    // Get fast path threshold for this transaction type
    uint32 TypeId = Transaction->Config.TypeId;
    float Threshold = 0.1f; // Default threshold
    
    FScopeLock Lock(const_cast<FCriticalSection*>(&ZoneLock));
    
    if (const float* ThresholdPtr = FastPathThresholds.Find(TypeId))
    {
        Threshold = *ThresholdPtr;
    }
    else if (const float* DefaultThresholdPtr = FastPathThresholds.Find(0))
    {
        // Use default threshold
        Threshold = *DefaultThresholdPtr;
    }
    
    // Check if the transaction's conflict probability is below the threshold
    float ConflictProbability = 0.0f;
    
    // Calculate conflict probability based on zone conflict history
    const TArray<FVersionRecord>& WriteSet = Transaction->GetWriteSet();
    if (WriteSet.Num() > 0)
    {
        int32 TotalConflicts = 0;
        int32 ZoneCount = 0;
        
        for (const FVersionRecord& Record : WriteSet)
        {
            const uint32* ConflictCount = ZoneConflicts.Find(Record.ZoneId);
            if (ConflictCount)
            {
                TotalConflicts += *ConflictCount;
            }
            ZoneCount++;
        }
        
        if (ZoneCount > 0)
        {
            ConflictProbability = static_cast<float>(TotalConflicts) / static_cast<float>(ZoneCount * 100);
        }
    }
    
    // Use fast path if conflict probability is below threshold
    return ConflictProbability < Threshold;
}