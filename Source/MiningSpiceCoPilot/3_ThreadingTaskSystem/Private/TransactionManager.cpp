// Copyright Epic Games, Inc. All Rights Reserved.

#include "TransactionManager.h"
#include "Misc/ScopeLock.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformAtomics.h"
#include "HAL/PlatformTLS.h"
#include "Utils/SimpleSpinLock.h" // Replace any Misc/SpinLock.h include
#include "GenericPlatform/GenericPlatformAtomics.h"

// Initialize static members
FTransactionManager* FTransactionManager::Instance = nullptr;
uint32 FTransactionManager::CurrentTransactionTLS = FPlatformTLS::AllocTlsSlot();

// Implementation of FMiningTransactionContextImpl

FMiningTransactionContextImpl::FMiningTransactionContextImpl(uint64 InTransactionId, const FTransactionConfig& InConfig)
    : TransactionId(InTransactionId)
    , Status(ETransactionStatus::NotStarted)
    , Config(InConfig)
{
    // Reserve space to avoid reallocations
    ReadSet.Reserve(16);
    WriteSet.Reserve(16);
    Conflicts.Reserve(4);
    
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
    FScopeLock ScopeLock(&Lock);
    return Status;
}

bool FMiningTransactionContextImpl::AddToReadSet(int32 ZoneId, int32 MaterialId)
{
    FScopeLock ScopeLock(&Lock);
    
    // Check if transaction is active
    if (Status != ETransactionStatus::InProgress)
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
    FScopeLock ScopeLock(&Lock);
    
    // Check if transaction is active
    if (Status != ETransactionStatus::InProgress)
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
    FScopeLock ScopeLock(&Lock);
    return Stats;
}

const FTransactionConfig& FMiningTransactionContextImpl::GetConfig() const
{
    return Config;
}

TArray<FTransactionConflict> FMiningTransactionContextImpl::GetConflicts() const
{
    FScopeLock ScopeLock(&Lock);
    return Conflicts;
}

void FMiningTransactionContextImpl::SetName(const FString& InName)
{
    FScopeLock ScopeLock(&Lock);
    Name = InName;
}

FString FMiningTransactionContextImpl::GetName() const
{
    FScopeLock ScopeLock(&Lock);
    return Name;
}

void FMiningTransactionContextImpl::SetStatus(ETransactionStatus NewStatus)
{
    FScopeLock ScopeLock(&Lock);
    Status = NewStatus;
    
    // Update timestamps based on state transitions
    double CurrentTime = FPlatformTime::Seconds();
    
    if (Status == ETransactionStatus::InProgress && StartTime == 0.0)
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
    FScopeLock ScopeLock(&Lock);
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
    FScopeLock ScopeLock(&Lock);
    Stats.LockWaitTimeMs += WaitTimeMs;
}

void FMiningTransactionContextImpl::RecordValidation()
{
    FScopeLock ScopeLock(&Lock);
    Stats.ValidationCount++;
}

void FMiningTransactionContextImpl::SetPeakMemoryUsage(uint64 MemoryUsageBytes)
{
    FScopeLock ScopeLock(&Lock);
    Stats.PeakMemoryBytes = MemoryUsageBytes;
}

void FMiningTransactionContextImpl::ClearReadWriteSets()
{
    FScopeLock ScopeLock(&Lock);
    ReadSet.Empty();
    WriteSet.Empty();
}

// Implementation of FTransactionManager

FTransactionManager::FTransactionManager()
    : bIsInitialized(false)
    , TotalTransactions(0)
    , CommittedTransactions(0)
    , AbortedTransactions(0)
    , ConflictCount(0)
{
    // Allocate TLS slot for current transaction
    CurrentTransactionTLS = FPlatformTLS::AllocTlsSlot();
}

FTransactionManager::~FTransactionManager()
{
    Shutdown();
    
    // Free TLS slot
    FPlatformTLS::FreeTlsSlot(CurrentTransactionTLS);
}

bool FTransactionManager::Initialize()
{
    if (bIsInitialized)
    {
        return true;
    }
    
    // Create instance if needed
    if (!Instance)
    {
        Instance = this;
    }
    
    bIsInitialized = true;
    return true;
}

void FTransactionManager::Shutdown()
{
    if (!bIsInitialized)
    {
        return;
    }
    
    // Clean up transactions
    CleanupCompletedTransactions(0.0); // Clean all
    
    // Clean up zone and material locks
    {
        FScopeLock ScopeLock(&ZoneLock);
        
        for (auto& Pair : ZoneLocks)
        {
            FSimpleSpinLock* LockToDelete = Pair.Value;
            Pair.Value = nullptr;
        }
        ZoneLocks.Empty();
        
        for (auto& Pair : ZoneVersions)
        {
            delete Pair.Value;
        }
        ZoneVersions.Empty();
        
        for (auto& Pair : MaterialVersions)
        {
            delete Pair.Value;
        }
        MaterialVersions.Empty();
    }
    
    // Clean up active transactions
    {
        FScopeLock ScopeLock(&TransactionLock);
        
        for (auto& Pair : ActiveTransactions)
        {
            FMiningTransactionContextImpl* Transaction = Pair.Value;
            if (Transaction)
            {
                if (Transaction->GetStatus() == ETransactionStatus::InProgress ||
                     Transaction->GetStatus() == ETransactionStatus::Committing)
                {
                    // Force abort
                    Transaction->SetStatus(ETransactionStatus::Aborted);
                }
                
                delete Transaction;
            }
        }
        
        ActiveTransactions.Empty();
    }
    
    bIsInitialized = false;
}

bool FTransactionManager::IsInitialized() const
{
    return bIsInitialized;
}

bool FTransactionManager::BeginTransaction(const FTransactionConfig& Config, FMiningTransactionContext*& OutContext)
{
    if (!bIsInitialized)
    {
        return false;
    }
    
    // Generate a unique transaction ID
    uint64 TransactionId = GenerateTransactionId();
    
    // Create transaction context
    FMiningTransactionContextImpl* Context = new FMiningTransactionContextImpl(TransactionId, Config);
    
    // Add to active transactions
    {
        FScopeLock ScopeLock(&TransactionLock);
        ActiveTransactions.Add(TransactionId, Context);
    }
    
    // Set initial status to Active
    Context->SetStatus(ETransactionStatus::InProgress);
    
    // Store in current thread's TLS
    FPlatformTLS::SetTlsValue(CurrentTransactionTLS, Context);
    
    // Return the context
    OutContext = Context;
    
    // Increment total transaction count
    FPlatformAtomics::InterlockedIncrement(reinterpret_cast<volatile int64*>(&TotalTransactions));
    
    return true;
}

bool FTransactionManager::CommitTransaction(FMiningTransactionContext* Context)
{
    if (!bIsInitialized || !Context)
    {
        return false;
    }
    
    FMiningTransactionContextImpl* TransactionImpl = static_cast<FMiningTransactionContextImpl*>(Context);
    
    // Only active transactions can be committed
    if (TransactionImpl->GetStatus() != ETransactionStatus::InProgress)
    {
        return false;
    }
    
    // Set status to committing
    TransactionImpl->SetStatus(ETransactionStatus::Committing);
    
    // Validate the transaction
    TArray<FTransactionConflict> Conflicts;
    bool bIsValid = ValidateReadSet(TransactionImpl, Conflicts);
    
    if (!bIsValid)
    {
        // Add conflicts to transaction
        for (const FTransactionConflict& Conflict : Conflicts)
        {
            TransactionImpl->AddConflict(Conflict);
            RecordConflict(Conflict.ZoneId);
        }
        
        // Check auto-retry configuration
        if (TransactionImpl->GetConfig().bAutoRetry && 
            TransactionImpl->IncrementRetryCount() <= TransactionImpl->GetConfig().MaxRetries)
        {
            // Apply retry strategy based on configuration
            double RetryDelayMs = TransactionImpl->GetConfig().BaseRetryIntervalMs;
            
            if (TransactionImpl->GetConfig().bUseExponentialBackoff)
            {
                RetryDelayMs *= (1 << (TransactionImpl->GetStats().RetryCount - 1));
            }
            
            // Sleep before retry
            FPlatformProcess::Sleep(RetryDelayMs / 1000.0);
            
            // Clear read/write sets and try again
            TransactionImpl->ClearReadWriteSets();
            TransactionImpl->SetStatus(ETransactionStatus::InProgress);
            
            return false; // Return false to signal retry needed
        }
        else if (TransactionImpl->GetConfig().ConflictStrategy == EConflictResolution::Force)
        {
            // Force commit despite conflicts
            bIsValid = true;
        }
        else if (TransactionImpl->GetConfig().ConflictStrategy == EConflictResolution::Merge)
        {
            // Try to merge changes
            bIsValid = MergeChanges(TransactionImpl);
            
            if (!bIsValid)
            {
                // If merge fails, set status to aborted
                TransactionImpl->SetStatus(ETransactionStatus::Aborted);
                FPlatformAtomics::InterlockedIncrement(reinterpret_cast<volatile int64*>(&AbortedTransactions));
                return false;
            }
        }
        else if (TransactionImpl->GetConfig().ConflictStrategy == EConflictResolution::Retry)
        {
            // Manual retry required
            
            // Reset status to active
            TransactionImpl->SetStatus(ETransactionStatus::InProgress);
            
            return false; // Signal retry needed
        }
        else
        {
            // Abort transaction
            TransactionImpl->SetStatus(ETransactionStatus::Aborted);
            FPlatformAtomics::InterlockedIncrement(reinterpret_cast<volatile int64*>(&AbortedTransactions));
            return false;
        }
    }
    
    // If we get here, the transaction is valid or forced through
    
    // Update all zone and material versions
    UpdateVersions(TransactionImpl);
    
    // Set status to committed
    TransactionImpl->SetStatus(ETransactionStatus::Committed);
    
    // Increment committed transaction count
    FPlatformAtomics::InterlockedIncrement(reinterpret_cast<volatile int64*>(&CommittedTransactions));
    
    return true;
}

void FTransactionManager::AbortTransaction(FMiningTransactionContext* Context)
{
    if (!bIsInitialized || !Context)
    {
        return;
    }
    
    FMiningTransactionContextImpl* TransactionImpl = static_cast<FMiningTransactionContextImpl*>(Context);
    
    // Only active or committing transactions can be aborted
    if (TransactionImpl->GetStatus() != ETransactionStatus::InProgress && 
         TransactionImpl->GetStatus() != ETransactionStatus::Committing)
    {
        return;
    }
    
    // Set status to aborting then aborted
    TransactionImpl->SetStatus(ETransactionStatus::Aborting);
    TransactionImpl->SetStatus(ETransactionStatus::Aborted);
    
    // Increment aborted transaction count
    FPlatformAtomics::InterlockedIncrement(reinterpret_cast<volatile int64*>(&AbortedTransactions));
}

bool FTransactionManager::ValidateTransaction(FMiningTransactionContext* Context)
{
    if (!bIsInitialized || !Context)
    {
        return false;
    }
    
    FMiningTransactionContextImpl* TransactionImpl = static_cast<FMiningTransactionContextImpl*>(Context);
    
    // Only active transactions can be validated
    if (TransactionImpl->GetStatus() != ETransactionStatus::InProgress)
    {
        return false;
    }
    
    // Validate the transaction
    TArray<FTransactionConflict> Conflicts;
    bool bIsValid = ValidateReadSet(TransactionImpl, Conflicts);
    
    // Add any conflicts to the transaction
    for (const FTransactionConflict& Conflict : Conflicts)
    {
        TransactionImpl->AddConflict(Conflict);
    }
    
    return bIsValid;
}

FMiningTransactionContext* FTransactionManager::GetCurrentTransaction()
{
    return static_cast<FMiningTransactionContext*>(FPlatformTLS::GetTlsValue(CurrentTransactionTLS));
}

FMiningTransactionContext* FTransactionManager::GetTransaction(uint64 TransactionId)
{
    FScopeLock ScopeLock(&TransactionLock);
    
    FMiningTransactionContextImpl** FoundTransaction = ActiveTransactions.Find(TransactionId);
    if (FoundTransaction)
    {
        return *FoundTransaction;
    }
    
    return nullptr;
}

TMap<FString, double> FTransactionManager::GetGlobalStats() const
{
    TMap<FString, double> Stats;
    
    FScopeLock ScopeLock(&TransactionLock);
    
    Stats.Add(TEXT("TotalTransactions"), static_cast<double>(TotalTransactions));
    Stats.Add(TEXT("CommittedTransactions"), static_cast<double>(CommittedTransactions));
    Stats.Add(TEXT("AbortedTransactions"), static_cast<double>(AbortedTransactions));
    Stats.Add(TEXT("ConflictCount"), static_cast<double>(ConflictCount));
    Stats.Add(TEXT("ActiveTransactions"), static_cast<double>(ActiveTransactions.Num()));
    
    return Stats;
}

uint32 FTransactionManager::GetActiveTransactionCount() const
{
    FScopeLock ScopeLock(&TransactionLock);
    return ActiveTransactions.Num();
}

float FTransactionManager::GetTransactionAbortRate() const
{
    FScopeLock ScopeLock(&TransactionLock);
    
    if (TotalTransactions == 0)
    {
        return 0.0f;
    }
    
    return static_cast<float>(AbortedTransactions) / static_cast<float>(TotalTransactions);
}

TMap<int32, uint32> FTransactionManager::GetZoneConflictStats() const
{
    FScopeLock ScopeLock(&ZoneLock);
    return ZoneConflicts;
}

FSimpleSpinLock* FTransactionManager::GetZoneLock(int32 ZoneId)
{
    return GetOrCreateZoneLock(ZoneId);
}

bool FTransactionManager::UpdateFastPathThreshold(uint32 TypeId, float ConflictRate)
{
    FScopeLock ScopeLock(&TransactionLock);
    
    FastPathThresholds.FindOrAdd(TypeId) = ConflictRate;
    return true;
}

ITransactionManager& FTransactionManager::Get()
{
    return *Instance;
}

uint64 FTransactionManager::GenerateTransactionId()
{
    return static_cast<uint64>(NextTransactionId.Increment());
}

FSimpleSpinLock* FTransactionManager::GetOrCreateZoneLock(int32 ZoneId)
{
    FScopeLock ScopeLock(&ZoneLock);
    
    FSimpleSpinLock** FoundLock = ZoneLocks.Find(ZoneId);
    if (FoundLock)
    {
        return *FoundLock;
    }
    
    FSimpleSpinLock* NewLock = new FSimpleSpinLock();
    ZoneLocks.Add(ZoneId, NewLock);
    
    return NewLock;
}

FThreadSafeCounter* FTransactionManager::GetOrCreateZoneVersion(int32 ZoneId)
{
    FScopeLock ScopeLock(&ZoneLock);
    
    FThreadSafeCounter** FoundCounter = ZoneVersions.Find(ZoneId);
    if (FoundCounter)
    {
        return *FoundCounter;
    }
    
    FThreadSafeCounter* NewCounter = new FThreadSafeCounter(1); // Start from version 1
    ZoneVersions.Add(ZoneId, NewCounter);
    
    return NewCounter;
}

FThreadSafeCounter* FTransactionManager::GetOrCreateMaterialVersion(int32 ZoneId, int32 MaterialId)
{
    FScopeLock ScopeLock(&ZoneLock);
    
    FString Key = FString::Printf(TEXT("%d_%d"), ZoneId, MaterialId);
    
    FThreadSafeCounter** FoundCounter = MaterialVersions.Find(Key);
    if (FoundCounter)
    {
        return *FoundCounter;
    }
    
    FThreadSafeCounter* NewCounter = new FThreadSafeCounter(1); // Start from version 1
    MaterialVersions.Add(Key, NewCounter);
    
    return NewCounter;
}

FVersionRecord FTransactionManager::GetVersionRecord(int32 ZoneId, int32 MaterialId, bool bIsReadOnly)
{
    FVersionRecord Record;
    Record.ZoneId = ZoneId;
    Record.MaterialId = MaterialId;
    Record.bIsReadOnly = bIsReadOnly;
    
    if (MaterialId == INDEX_NONE)
    {
        // Zone-level version
        FThreadSafeCounter* VersionCounter = GetOrCreateZoneVersion(ZoneId);
        Record.Version = VersionCounter->GetValue();
    }
    else
    {
        // Material-level version
        FThreadSafeCounter* VersionCounter = GetOrCreateMaterialVersion(ZoneId, MaterialId);
        Record.Version = VersionCounter->GetValue();
    }
    
    return Record;
}

void FTransactionManager::UpdateVersions(const FMiningTransactionContextImpl* Transaction)
{
    // Only update versions for entries in the write set
    for (const FVersionRecord& Record : Transaction->GetWriteSet())
    {
        if (Record.MaterialId == INDEX_NONE)
        {
            // Zone-level update
            FThreadSafeCounter* VersionCounter = GetOrCreateZoneVersion(Record.ZoneId);
            VersionCounter->Increment();
        }
        else
        {
            // Material-level update
            FThreadSafeCounter* VersionCounter = GetOrCreateMaterialVersion(Record.ZoneId, Record.MaterialId);
            VersionCounter->Increment();
        }
    }
}

bool FTransactionManager::ValidateReadSet(const FMiningTransactionContextImpl* Transaction, TArray<FTransactionConflict>& OutConflicts)
{
    // Start validation time measurement
    double StartTime = FPlatformTime::Seconds();
    
    // Check all records in the read set
    for (const FVersionRecord& Record : Transaction->GetReadSet())
    {
        uint32 CurrentVersion;
        
        if (Record.MaterialId == INDEX_NONE)
        {
            // Zone-level version
            FThreadSafeCounter* VersionCounter = GetOrCreateZoneVersion(Record.ZoneId);
            CurrentVersion = VersionCounter->GetValue();
        }
        else
        {
            // Material-level version
            FThreadSafeCounter* VersionCounter = GetOrCreateMaterialVersion(Record.ZoneId, Record.MaterialId);
            CurrentVersion = VersionCounter->GetValue();
        }
        
        // Check if version has changed
        if (CurrentVersion != Record.Version && CurrentVersion > 0)
        {
            // Create conflict record
            FTransactionConflict Conflict;
            Conflict.ZoneId = Record.ZoneId;
            Conflict.MaterialId = Record.MaterialId;
            Conflict.ExpectedVersion = Record.Version;
            Conflict.ActualVersion = CurrentVersion;
            Conflict.ConflictingTransactionId = 0; // Unknown
            Conflict.bIsReadConflict = Record.bIsReadOnly;
            Conflict.bIsCritical = true;
            Conflict.ConflictType = ETransactionConflictType::VersionMismatch;
            
            // Add to conflicts list
            OutConflicts.Add(Conflict);
        }
    }
    
    // End validation time measurement
    double EndTime = FPlatformTime::Seconds();
    double ValidationTimeMs = (EndTime - StartTime) * 1000.0;
    
    // Record validation statistics
    const_cast<FMiningTransactionContextImpl*>(Transaction)->GetMutableStats().ValidationTimeMs += ValidationTimeMs;
    const_cast<FMiningTransactionContextImpl*>(Transaction)->RecordValidation();
    
    // Return true if no conflicts
    return OutConflicts.Num() == 0;
}

void FTransactionManager::CleanupCompletedTransactions(double MaxAgeSeconds)
{
    FScopeLock ScopeLock(&TransactionLock);
    
    double CurrentTime = FPlatformTime::Seconds();
    TArray<uint64> TransactionsToRemove;
    
    // Find completed transactions older than MaxAgeSeconds
    for (const auto& Pair : ActiveTransactions)
    {
        FMiningTransactionContextImpl* Transaction = Pair.Value;
        
        if (Transaction->GetStatus() == ETransactionStatus::Committed || 
            Transaction->GetStatus() == ETransactionStatus::Aborted)
        {
            double CommitTime = Transaction->GetCommitTime();
            
            if (CommitTime > 0.0 && (MaxAgeSeconds <= 0.0 || CurrentTime - CommitTime > MaxAgeSeconds))
            {
                TransactionsToRemove.Add(Pair.Key);
            }
        }
    }
    
    // Remove and delete completed transactions
    for (uint64 TransactionId : TransactionsToRemove)
    {
        FMiningTransactionContextImpl* Transaction = ActiveTransactions[TransactionId];
        ActiveTransactions.Remove(TransactionId);
        
        // Remove from TLS if this is the current transaction
        if (FPlatformTLS::GetTlsValue(CurrentTransactionTLS) == Transaction)
        {
            FPlatformTLS::SetTlsValue(CurrentTransactionTLS, nullptr);
        }
        
        delete Transaction;
    }
}

void FTransactionManager::RecordConflict(int32 ZoneId)
{
    FScopeLock ScopeLock(&ZoneLock);
    
    // Increment global conflict counter
    FPlatformAtomics::InterlockedIncrement(reinterpret_cast<volatile int64*>(&ConflictCount));
    
    // Increment zone-specific conflict counter
    uint32& ZoneConflictCount = ZoneConflicts.FindOrAdd(ZoneId);
    ZoneConflictCount++;
}

/**
 * Should this transaction use the fast path
 */
bool FTransactionManager::ShouldUseFastPath(const FMiningTransactionContextImpl* Transaction) const
{
    // Use proper scoped lock with reference to class member
    FScopeLock ScopeLock(&this->TransactionLock);
    
    const float* FoundThreshold = FastPathThresholds.Find(Transaction->GetConfig().TypeId);
    float Threshold = FoundThreshold ? *FoundThreshold : 0.1f; // Default threshold
    
    // Calculate current conflict rate
    float ConflictRate = TotalTransactions > 0 
        ? static_cast<float>(ConflictCount) / static_cast<float>(TotalTransactions) 
        : 0.0f;
    
    // Use fast path if conflict rate is below threshold
    return ConflictRate < Threshold;
}

bool FTransactionManager::MergeChanges(FMiningTransactionContextImpl* Transaction)
{
    // Get the read set
    const TArray<FVersionRecord>& ReadSet = Transaction->GetReadSet();
    const TArray<FVersionRecord>& WriteSet = Transaction->GetWriteSet();
    
    // Simple implementation just updates all read versions to current
    // A more sophisticated implementation would actually merge data
    for (const FVersionRecord& Record : ReadSet)
    {
        FVersionRecord UpdatedRecord = GetVersionRecord(Record.ZoneId, Record.MaterialId, Record.bIsReadOnly);
        
        // Check if this record is also in the write set
        bool bIsInWriteSet = false;
        for (const FVersionRecord& WriteRecord : WriteSet)
        {
            if (WriteRecord.ZoneId == Record.ZoneId && WriteRecord.MaterialId == Record.MaterialId)
            {
                bIsInWriteSet = true;
                break;
            }
        }
        
        // Skip records that are in the write set - those will be handled differently
        if (bIsInWriteSet)
        {
            continue;
        }
        
        // For read-only records, just update the version
        // This is equivalent to re-reading the latest value
        const_cast<FVersionRecord&>(Record).Version = UpdatedRecord.Version;
    }
    
    // In a real implementation, you would also need to merge write operations
    // This would require domain-specific knowledge of how to combine conflicting changes
    
    return true;
}