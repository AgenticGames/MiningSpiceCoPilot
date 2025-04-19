// Copyright Epic Games, Inc. All Rights Reserved.

#include "ThreadSafety.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformAffinity.h"
#include "HAL/ThreadManager.h"
#include "Containers/Queue.h"
#include "Misc/ScopeLock.h"
#include "Misc/OutputDeviceRedirector.h"

// Initialize static members before they're used
uint32 FHierarchicalLock::ThreadHighestLockLevelTLS = FPlatformTLS::AllocTlsSlot();
uint32 FThreadSafety::ThreadAccessedZonesTLS = FPlatformTLS::AllocTlsSlot();
FThreadSafety* FThreadSafety::Instance = nullptr;
TMap<void*, FLockContentionStats> FThreadSafety::ContentionStats;
FCriticalSection FThreadSafety::ContentionStatsLock;

// Add the missing TLS methods for FHierarchicalLock
uint32 FHierarchicalLock::GetThreadHighestLockLevel()
{
    void* TlsValue = FPlatformTLS::GetTlsValue(ThreadHighestLockLevelTLS);
    if (TlsValue == nullptr)
    {
        return 0;
    }
    return static_cast<uint32>(reinterpret_cast<UPTRINT>(TlsValue));
}

void FHierarchicalLock::SetThreadHighestLockLevel(uint32 Level)
{
    FPlatformTLS::SetTlsValue(ThreadHighestLockLevelTLS, reinterpret_cast<void*>(static_cast<UPTRINT>(Level)));
}

// Add the missing TLS methods for FThreadSafety
TArray<int32>* FThreadSafety::GetThreadAccessedZones()
{
    return static_cast<TArray<int32>*>(FPlatformTLS::GetTlsValue(ThreadAccessedZonesTLS));
}

void FThreadSafety::SetThreadAccessedZones(TArray<int32>* Zones)
{
    FPlatformTLS::SetTlsValue(ThreadAccessedZonesTLS, Zones);
}

// Implement the atomic operations for FThreadSafetyHelpers without redefining the class
// These are helper functions that are used by the thread safety system

// Helper for locking an atomic variable
bool TryLockAtomic(TAtomic<int32>& LockVar)
{
    // Cast to volatile int32* for compatibility with InterlockedCompareExchange
    volatile int32* LockPtr = reinterpret_cast<volatile int32*>(&LockVar);
    return FPlatformAtomics::InterlockedCompareExchange(LockPtr, 1, 0) == 0;
}

void LockAtomic(TAtomic<int32>& LockVar)
{
    // Cast to volatile int32* for compatibility with InterlockedCompareExchange
    volatile int32* LockPtr = reinterpret_cast<volatile int32*>(&LockVar);
    while (FPlatformAtomics::InterlockedCompareExchange(LockPtr, 1, 0) != 0)
    {
        FPlatformProcess::Yield();
    }
}

void UnlockAtomic(TAtomic<int32>& LockVar)
{
    // Cast to volatile int32* for compatibility with InterlockedExchange
    volatile int32* LockPtr = reinterpret_cast<volatile int32*>(&LockVar);
    FPlatformAtomics::InterlockedExchange(LockPtr, 0);
}

//----------------------------------------------------------------------
// FMiningReaderWriterLock Implementation
//----------------------------------------------------------------------

FMiningReaderWriterLock::FMiningReaderWriterLock()
{
    ReaderEvent = FPlatformProcess::GetSynchEventFromPool(false);
    WriterEvent = FPlatformProcess::GetSynchEventFromPool(false);
}

FMiningReaderWriterLock::~FMiningReaderWriterLock()
{
    FPlatformProcess::ReturnSynchEventToPool(ReaderEvent);
    FPlatformProcess::ReturnSynchEventToPool(WriterEvent);
    
    ReaderEvent = nullptr;
    WriterEvent = nullptr;
}

bool FMiningReaderWriterLock::ReadLock(uint32 TimeoutMs)
{
    uint32 CurrentThreadId = GetCurrentThreadId();
    
    // Fast path for reader if no writer is active or waiting
    if (WriterActive.GetValue() == 0 && WriterWaiting.GetValue() == 0)
    {
        ReaderCount.Increment();
        return true;
    }
    
    // If this thread already holds the write lock, don't try to take a read lock
    if (WriterActive.GetValue() > 0 && WriterThreadId.GetValue() == CurrentThreadId)
    {
        // Already have write access, which implies read access
        return true;
    }
    
    // Wait for any active writer to finish
    double StartTime = FPlatformTime::Seconds();
    double EndTime = (TimeoutMs > 0) ? (StartTime + TimeoutMs / 1000.0) : DBL_MAX;
    
    while (WriterActive.GetValue() > 0)
    {
        // Check if we've timed out
        if (FPlatformTime::Seconds() >= EndTime)
        {
            return false;
        }
        
        ReaderEvent->Wait(1);  // Wait with a short timeout to check again
    }
    
    // Increment reader count
    ReaderCount.Increment();
    
    return true;
}

void FMiningReaderWriterLock::ReadUnlock()
{
    uint32 CurrentThreadId = GetCurrentThreadId();
    
    // If this thread holds the write lock, don't release a read lock
    if (WriterActive.GetValue() > 0 && WriterThreadId.GetValue() == CurrentThreadId)
    {
        // Have write lock, don't decrement read count
        return;
    }
    
    // Decrement reader count
    int32 RemainingReaders = ReaderCount.Decrement();
    
    // If there are no more readers and a writer is waiting, signal the writer
    if (RemainingReaders == 0 && WriterWaiting.GetValue() > 0)
    {
        WriterEvent->Trigger();
    }
}

bool FMiningReaderWriterLock::WriteLock(uint32 TimeoutMs)
{
    uint32 CurrentThreadId = GetCurrentThreadId();
    
    // If this thread already holds the write lock, just increment the counter
    if (WriterActive.GetValue() > 0 && WriterThreadId.GetValue() == CurrentThreadId)
    {
        WriterActive.Increment();
        return true;
    }
    
    // Signal that a writer is waiting
    WriterWaiting.Increment();
    
    // Wait for all readers to finish
    double StartTime = FPlatformTime::Seconds();
    double EndTime = (TimeoutMs > 0) ? (StartTime + TimeoutMs / 1000.0) : DBL_MAX;
    
    while (ReaderCount.GetValue() > 0 || WriterActive.GetValue() > 0)
    {
        // Check if we've timed out
        if (FPlatformTime::Seconds() >= EndTime)
        {
            WriterWaiting.Decrement();
            return false;
        }
        
        WriterEvent->Wait(1);  // Wait with a short timeout to check again
    }
    
    // Mark the writer as active and store the thread ID
    WriterActive.Set(1);
    WriterThreadId.Set(CurrentThreadId);
    
    // No longer waiting
    WriterWaiting.Decrement();
    
    return true;
}

void FMiningReaderWriterLock::WriteUnlock()
{
    uint32 CurrentThreadId = GetCurrentThreadId();
    
    // Only allow the writer thread to unlock
    if (WriterThreadId.GetValue() != CurrentThreadId)
    {
        return;
    }
    
    // Decrement the write lock count
    int32 RemainingWriteLocks = WriterActive.Decrement();
    
    // If this was the last write lock, clear the writer thread ID and signal others
    if (RemainingWriteLocks == 0)
    {
        WriterThreadId.Set(0);
        
        // Signal readers
        ReaderEvent->Trigger();
        
        // Signal one waiting writer if any
        if (WriterWaiting.GetValue() > 0)
        {
            WriterEvent->Trigger();
        }
    }
}

bool FMiningReaderWriterLock::IsWriteLocked() const
{
    return WriterActive.GetValue() > 0;
}

int32 FMiningReaderWriterLock::GetReaderCount() const
{
    return ReaderCount.GetValue();
}

bool FMiningReaderWriterLock::IsWritePending() const
{
    return WriterWaiting.GetValue() > 0;
}

bool FMiningReaderWriterLock::TryUpgradeToWriteLock()
{
    uint32 CurrentThreadId = GetCurrentThreadId();
    
    // Already have a write lock
    if (WriterActive.GetValue() > 0 && WriterThreadId.GetValue() == CurrentThreadId)
    {
        return true;
    }
    
    // Try to acquire a write lock without waiting
    WriterWaiting.Increment();
    
    bool bCanUpgrade = (ReaderCount.GetValue() == 1) && (WriterActive.GetValue() == 0);
    
    if (bCanUpgrade)
    {
        // Set writer active and thread ID
        WriterActive.Set(1);
        WriterThreadId.Set(CurrentThreadId);
        
        // Decrement reader count since we're upgrading from read to write
        ReaderCount.Decrement();
    }
    
    WriterWaiting.Decrement();
    
    return bCanUpgrade;
}

void FMiningReaderWriterLock::DowngradeToReadLock()
{
    uint32 CurrentThreadId = GetCurrentThreadId();
    
    // Only the writer can downgrade
    if (WriterThreadId.GetValue() != CurrentThreadId)
    {
        return;
    }
    
    // Downgrading means adding a reader and removing a writer
    ReaderCount.Increment();
    
    // Clear writer status
    WriterActive.Set(0);
    WriterThreadId.Set(0);
    
    // Signal writers and readers
    WriterEvent->Trigger();
    ReaderEvent->Trigger();
}

uint32 FMiningReaderWriterLock::GetCurrentThreadId()
{
    return FPlatformTLS::GetCurrentThreadId();
}

//----------------------------------------------------------------------
// FScopedReadLock Implementation
//----------------------------------------------------------------------

FScopedReadLock::FScopedReadLock(FMiningReaderWriterLock& InLock, uint32 TimeoutMs)
    : Lock(InLock)
    , bLocked(false)
{
    bLocked = Lock.ReadLock(TimeoutMs);
}

FScopedReadLock::~FScopedReadLock()
{
    if (bLocked)
    {
        Lock.ReadUnlock();
    }
}

bool FScopedReadLock::IsLocked() const
{
    return bLocked;
}

//----------------------------------------------------------------------
// FScopedWriteLock Implementation
//----------------------------------------------------------------------

FScopedWriteLock::FScopedWriteLock(FMiningReaderWriterLock& InLock, uint32 TimeoutMs)
    : Lock(InLock)
    , bLocked(false)
{
    bLocked = Lock.WriteLock(TimeoutMs);
}

FScopedWriteLock::~FScopedWriteLock()
{
    if (bLocked)
    {
        Lock.WriteUnlock();
    }
}

bool FScopedWriteLock::IsLocked() const
{
    return bLocked;
}

//----------------------------------------------------------------------
// FHierarchicalLock Implementation
//----------------------------------------------------------------------

FHierarchicalLock::FHierarchicalLock(uint32 InLevel)
    : Level(InLevel)
{
    OwnerThreadId.Set(0);
    LockCount.Set(0);
}

FHierarchicalLock::~FHierarchicalLock()
{
    // Ensure the lock is not held at destruction
    check(LockCount.GetValue() == 0);
}

bool FHierarchicalLock::Lock(uint32 TimeoutMs)
{
    uint32 CurrentThreadId = FPlatformTLS::GetCurrentThreadId();
    
    // If this thread already holds the lock, increment the lock count
    if (OwnerThreadId.GetValue() == CurrentThreadId)
    {
        LockCount.Increment();
        return true;
    }
    
    // Check for hierarchical violation
    if (Level <= GetThreadHighestLockLevel())
    {
        // Use FString to ensure format safety
        FString ErrorMessage = FString::Format(TEXT("Hierarchical lock violation: Trying to lock level {0} when already holding level {1}"),
            { Level, GetThreadHighestLockLevel() });
        UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMessage);
        return false;
    }
    
    // Try to acquire the lock
    double StartTime = FPlatformTime::Seconds();
    double EndTime = (TimeoutMs > 0) ? (StartTime + TimeoutMs / 1000.0) : DBL_MAX;
    
    while (!InternalLock.TryLock())
    {
        // Check if we've timed out
        if (FPlatformTime::Seconds() >= EndTime)
        {
            return false;
        }
        
        FPlatformProcess::Sleep(0.001f);
    }
    
    // Update thread-local highest lock level
    SetThreadHighestLockLevel(FMath::Max(GetThreadHighestLockLevel(), Level));
    
    // Update owner and count
    OwnerThreadId.Set(CurrentThreadId);
    LockCount.Set(1);
    
    return true;
}

void FHierarchicalLock::Unlock()
{
    uint32 CurrentThreadId = FPlatformTLS::GetCurrentThreadId();
    
    // Only the owner can unlock
    if (OwnerThreadId.GetValue() != CurrentThreadId)
    {
        // Use FString to ensure format safety
        FString ErrorMessage = FString::Format(TEXT("Hierarchical lock unlock violation: Thread {0} trying to unlock a lock owned by thread {1}"),
            { CurrentThreadId, OwnerThreadId.GetValue() });
        UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMessage);
        return;
    }
    
    // Decrement lock count
    int32 RemainingLocks = LockCount.Decrement();
    
    // If this was the last lock, clear the owner and release the lock
    if (RemainingLocks == 0)
    {
        OwnerThreadId.Set(0);
        
        // Reset thread-local highest lock level if this was the highest
        if (GetThreadHighestLockLevel() == Level)
        {
            SetThreadHighestLockLevel(0);
        }
        
        InternalLock.Unlock();
    }
}

uint32 FHierarchicalLock::GetLevel() const
{
    return Level;
}

bool FHierarchicalLock::IsLockedByCurrentThread() const
{
    uint32 CurrentThreadId = FPlatformTLS::GetCurrentThreadId();
    return OwnerThreadId.GetValue() == CurrentThreadId;
}

//----------------------------------------------------------------------
// FHybridLock Implementation
//----------------------------------------------------------------------

FHybridLock::FHybridLock()
    : ContentionThreshold(10)
{
    ContentionCount.Set(0);
    bUseSlowLock.Set(0);
}

FHybridLock::~FHybridLock()
{
    // Nothing to clean up
}

bool FHybridLock::Lock(uint32 TimeoutMs)
{
    if (bUseSlowLock.GetValue() > 0)
    {
        // Use the critical section for high-contention scenarios
        FScopeLock Lock(&SlowLock);
        return true;
    }
    
    // Try the fast lock first
    if (TryLockAtomic(FastLock))
    {
        return true;
    }
    
    // Increment contention counter and check if we should switch to slow lock
    UpdateContentionStats();
    
    // If we're over the threshold, switch to the slow lock
    if (static_cast<uint32>(ContentionCount.GetValue()) > ContentionThreshold)
    {
        bUseSlowLock.Set(1);
        FScopeLock Lock(&SlowLock);
        return true;
    }
    
    // If no timeout specified, just spin until we get the lock
    if (TimeoutMs == 0)
    {
        LockAtomic(FastLock);
        return true;
    }
    
    // Try with timeout
    double StartTime = FPlatformTime::Seconds();
    double EndTime = StartTime + (TimeoutMs / 1000.0);
    
    while (!TryLockAtomic(FastLock))
    {
        // Check if we've timed out
        if (FPlatformTime::Seconds() >= EndTime)
        {
            return false;
        }
        
        // Yield to other threads
        FPlatformProcess::Yield();
    }
    
    return true;
}

void FHybridLock::Unlock()
{
    if (bUseSlowLock.GetValue() > 0)
    {
        SlowLock.Unlock();
    }
    else
    {
        UnlockAtomic(FastLock);
    }
}

uint32 FHybridLock::GetContentionCount() const
{
    return ContentionCount.GetValue();
}

void FHybridLock::ResetContentionStats()
{
    ContentionCount.Set(0);
    bUseSlowLock.Set(0);
}

void FHybridLock::SetContentionThreshold(uint32 Threshold)
{
    ContentionThreshold = Threshold;
}

void FHybridLock::UpdateContentionStats()
{
    int32 NewCount = ContentionCount.Increment();
    
    // If contention exceeds threshold, switch to slow lock
    if (NewCount >= static_cast<int32>(ContentionThreshold) && bUseSlowLock.GetValue() == 0)
    {
        bUseSlowLock.Set(1);
    }
}

//----------------------------------------------------------------------
// FWaitFreeCounter Implementation
//----------------------------------------------------------------------

FWaitFreeCounter::FWaitFreeCounter()
    : Counter(0)
{
}

FWaitFreeCounter::FWaitFreeCounter(int32 InitialValue)
    : Counter(InitialValue)
{
}

int32 FWaitFreeCounter::GetValue() const
{
    return Counter.load(std::memory_order_acquire);
}

void FWaitFreeCounter::SetValue(int32 Value)
{
    Counter.store(Value, std::memory_order_release);
}

int32 FWaitFreeCounter::Increment()
{
    return Counter.fetch_add(1, std::memory_order_acq_rel) + 1;
}

int32 FWaitFreeCounter::Decrement()
{
    return Counter.fetch_sub(1, std::memory_order_acq_rel) - 1;
}

int32 FWaitFreeCounter::Add(int32 Amount)
{
    return Counter.fetch_add(Amount, std::memory_order_acq_rel) + Amount;
}

int32 FWaitFreeCounter::Exchange(int32 NewValue)
{
    return Counter.exchange(NewValue, std::memory_order_acq_rel);
}

bool FWaitFreeCounter::CompareExchange(int32& OutOldValue, int32 NewValue, int32 Comparand)
{
    OutOldValue = Comparand;
    return Counter.compare_exchange_strong(OutOldValue, NewValue, std::memory_order_acq_rel);
}

//----------------------------------------------------------------------
// FSVOFieldReadLock Implementation
//----------------------------------------------------------------------

FSVOFieldReadLock::FSVOFieldReadLock()
{
    CurrentVersion.Set(1);  // Start at version 1
    UpdateInProgress.Set(0);
}

FSVOFieldReadLock::~FSVOFieldReadLock()
{
    // Nothing to clean up
}

uint32 FSVOFieldReadLock::BeginRead()
{
    // Spin while an update is in progress
    while (UpdateInProgress.GetValue() > 0)
    {
        FPlatformProcess::Yield();
    }
    
    // Get the current version
    return CurrentVersion.GetValue();
}

bool FSVOFieldReadLock::BeginWrite()
{
    // Mark that an update is in progress
    int32 ExpectedValue = 0;
    return FThreadSafetyHelpers::AtomicCompareExchange(UpdateInProgress, ExpectedValue, 1, 0);
}

void FSVOFieldReadLock::EndWrite()
{
    // Increment version to invalidate current readers
    CurrentVersion.Increment();
    
    // Mark that update is complete
    UpdateInProgress.Set(0);
}

uint32 FSVOFieldReadLock::GetCurrentVersion() const
{
    return CurrentVersion.GetValue();
}

//----------------------------------------------------------------------
// FZoneBasedLock Implementation
//----------------------------------------------------------------------

FZoneBasedLock::FZoneBasedLock(int32 InZoneCount)
    : ZoneCount(InZoneCount)
    , OwnerThreadId(INDEX_NONE)
{
    // Create individual locks for each zone
    ZoneLocks.SetNum(ZoneCount);
    ZoneOwners.SetNum(ZoneCount);
    
    for (int32 i = 0; i < ZoneCount; ++i)
    {
        ZoneLocks[i] = new FSpinLock();
        ZoneOwners[i].Set(INDEX_NONE);
    }
}

FZoneBasedLock::~FZoneBasedLock()
{
    // Clean up zone locks
    for (int32 i = 0; i < ZoneCount; ++i)
    {
        delete ZoneLocks[i];
    }
    
    ZoneLocks.Empty();
    ZoneOwners.Empty();
}

bool FZoneBasedLock::LockZone(int32 ZoneId, uint32 TimeoutMs)
{
    if (ZoneId < 0 || ZoneId >= ZoneCount)
    {
        return false;
    }
    
    int32 CurrentThreadId = FPlatformTLS::GetCurrentThreadId();
    
    // If already own this zone, increment lock count
    if (ZoneOwners[ZoneId].GetValue() == CurrentThreadId)
    {
        return true;
    }
    
    // Add to thread-local accessed zones for deadlock prevention
    if (FThreadSafety::GetThreadAccessedZones() == nullptr)
    {
        FThreadSafety::SetThreadAccessedZones(new TArray<int32>());
    }
    
    FThreadSafety::GetThreadAccessedZones()->Add(ZoneId);
    
    // Use SpinLock for the zone
    FSpinLock* ZoneLock = ZoneLocks[ZoneId];
    
    // Try to acquire the lock
    double StartTime = FPlatformTime::Seconds();
    double EndTime = (TimeoutMs > 0) ? (StartTime + TimeoutMs / 1000.0) : DBL_MAX;
    
    while (!ZoneLock->TryLock())
    {
        // Check if we've timed out
        if (FPlatformTime::Seconds() >= EndTime)
        {
            return false;
        }
        
        // Prevent excessive spinning
        FPlatformProcess::Yield();
    }
    
    // Update owner
    ZoneOwners[ZoneId].Set(CurrentThreadId);
    
    return true;
}

bool FZoneBasedLock::TryLockZone(int32 ZoneId)
{
    if (ZoneId < 0 || ZoneId >= ZoneCount)
    {
        return false;
    }
    
    int32 CurrentThreadId = FPlatformTLS::GetCurrentThreadId();
    
    // If already own this zone, increment lock count
    if (ZoneOwners[ZoneId].GetValue() == CurrentThreadId)
    {
        return true;
    }
    
    // Try to lock the zone
    FSpinLock* ZoneLock = ZoneLocks[ZoneId];
    
    if (ZoneLock->TryLock())
    {
        // Add to thread-local accessed zones
        if (FThreadSafety::GetThreadAccessedZones() == nullptr)
        {
            FThreadSafety::SetThreadAccessedZones(new TArray<int32>());
        }
        
        FThreadSafety::GetThreadAccessedZones()->Add(ZoneId);
        
        // Update owner
        ZoneOwners[ZoneId].Set(CurrentThreadId);
        
        return true;
    }
    
    return false;
}

void FZoneBasedLock::UnlockZone(int32 ZoneId)
{
    if (ZoneId < 0 || ZoneId >= ZoneCount)
    {
        return;
    }
    
    int32 CurrentThreadId = FPlatformTLS::GetCurrentThreadId();
    
    // Only owner can unlock
    if (ZoneOwners[ZoneId].GetValue() != CurrentThreadId)
    {
        return;
    }
    
    // Reset owner
    ZoneOwners[ZoneId].Set(INDEX_NONE);
    
    // Unlock the zone
    ZoneLocks[ZoneId]->Unlock();
    
    // Remove from thread-local accessed zones
    if (FThreadSafety::GetThreadAccessedZones())
    {
        FThreadSafety::GetThreadAccessedZones()->Remove(ZoneId);
    }
}

bool FZoneBasedLock::LockMultipleZones(const TArray<int32>& ZoneIds, uint32 TimeoutMs)
{
    if (ZoneIds.Num() == 0)
    {
        return true;
    }
    
    // Sort zone IDs to prevent deadlocks
    TArray<int32> SortedZoneIds = ZoneIds;
    SortedZoneIds.Sort();
    
    // Try to lock all zones
    double StartTime = FPlatformTime::Seconds();
    double EndTime = (TimeoutMs > 0) ? (StartTime + TimeoutMs / 1000.0) : DBL_MAX;
    
    // Track which zones we've locked so we can unlock them on failure
    TArray<int32> LockedZones;
    
    for (int32 ZoneId : SortedZoneIds)
    {
        // Skip duplicates
        if (LockedZones.Contains(ZoneId))
        {
            continue;
        }
        
        // Calculate remaining time for the timeout
        uint32 RemainingTimeMs = 0;
        
        if (TimeoutMs > 0)
        {
            double RemainingTime = EndTime - FPlatformTime::Seconds();
            
            if (RemainingTime <= 0)
            {
                // Timed out, unlock any acquired locks
                for (int32 LockedZoneId : LockedZones)
                {
                    UnlockZone(LockedZoneId);
                }
                
                return false;
            }
            
            RemainingTimeMs = static_cast<uint32>(RemainingTime * 1000.0);
        }
        
        // Try to lock this zone
        if (!LockZone(ZoneId, RemainingTimeMs))
        {
            // Failed to lock, unlock any acquired locks
            for (int32 LockedZoneId : LockedZones)
            {
                UnlockZone(LockedZoneId);
            }
            
            return false;
        }
        
        LockedZones.Add(ZoneId);
    }
    
    return true;
}

void FZoneBasedLock::UnlockMultipleZones(const TArray<int32>& ZoneIds)
{
    // Unlock in reverse order to prevent deadlocks when re-acquiring
    for (int32 i = ZoneIds.Num() - 1; i >= 0; --i)
    {
        UnlockZone(ZoneIds[i]);
    }
}

bool FZoneBasedLock::IsZoneLocked(int32 ZoneId) const
{
    if (ZoneId < 0 || ZoneId >= ZoneCount)
    {
        return false;
    }
    
    return ZoneOwners[ZoneId].GetValue() != INDEX_NONE;
}

int32 FZoneBasedLock::GetZoneOwner(int32 ZoneId) const
{
    if (ZoneId < 0 || ZoneId >= ZoneCount)
    {
        return INDEX_NONE;
    }
    
    return ZoneOwners[ZoneId].GetValue();
}

//----------------------------------------------------------------------
// FThreadSafety Implementation
//----------------------------------------------------------------------

FThreadSafety::FThreadSafety()
{
    // Set the singleton instance
    check(Instance == nullptr);
    Instance = this;
    
    Initialize();
}

FThreadSafety::~FThreadSafety()
{
    Shutdown();
    
    // Clear the singleton instance if it's us
    if (Instance == this)
    {
        Instance = nullptr;
    }
}

FThreadSafety& FThreadSafety::Get()
{
    // Create instance if needed
    if (Instance == nullptr)
    {
        Instance = new FThreadSafety();
    }
    
    return *Instance;
}

void FThreadSafety::Initialize()
{
    // Initialize contention tracking
    FScopeLock Lock(&ContentionStatsLock);
    ContentionStats.Empty();
}

void FThreadSafety::Shutdown()
{
    // Cleanup any thread local storage
    CleanupThreadLocalStorage();
    
    // Clear contention stats
    FScopeLock Lock(&ContentionStatsLock);
    ContentionStats.Empty();
}

FMiningReaderWriterLock* FThreadSafety::CreateReaderWriterLock()
{
    return new FMiningReaderWriterLock();
}

FHierarchicalLock* FThreadSafety::CreateHierarchicalLock(uint32 Level)
{
    return new FHierarchicalLock(Level);
}

FHybridLock* FThreadSafety::CreateHybridLock()
{
    return new FHybridLock();
}

FWaitFreeCounter* FThreadSafety::CreateWaitFreeCounter(int32 InitialValue)
{
    return new FWaitFreeCounter(InitialValue);
}

FSVOFieldReadLock* FThreadSafety::CreateSVOFieldReadLock()
{
    return new FSVOFieldReadLock();
}

FZoneBasedLock* FThreadSafety::CreateZoneBasedLock(int32 ZoneCount)
{
    return new FZoneBasedLock(ZoneCount);
}

bool FThreadSafety::DetectPotentialDeadlock(const FString& LockName, uint32 TimeoutMs)
{
    // Check the current thread's accessed zones for potential circular dependencies
    if (FThreadSafety::GetThreadAccessedZones() && FThreadSafety::GetThreadAccessedZones()->Num() > 0)
    {
        // For now, just log a warning if a thread tries to lock too many zones
        if (FThreadSafety::GetThreadAccessedZones()->Num() > 10)
        {
            // Use FString to ensure format safety
            FString WarningMessage = FString::Format(TEXT("Thread {0} is trying to lock more than 10 zones ({1}) which may lead to deadlocks"),
                { FPlatformTLS::GetCurrentThreadId(), FThreadSafety::GetThreadAccessedZones()->Num() });
            UE_LOG(LogTemp, Warning, TEXT("%s"), *WarningMessage);
            return true;
        }
    }
    
    return false;
}

TArray<uint32> FThreadSafety::GetThreadLockOrder()
{
    TArray<uint32> Result;
    
    // Return the current thread's highest lock level
    if (FHierarchicalLock::GetThreadHighestLockLevel() > 0)
    {
        Result.Add(FHierarchicalLock::GetThreadHighestLockLevel());
    }
    
    return Result;
}

struct FValidationThreadData
{
    TFunction<void()> TestFunction;
    FThreadSafeCounter* ErrorCount;
};

// Add a proper FRunnable implementation for validation thread
class FValidationThreadRunnable : public FRunnable
{
public:
    FValidationThreadRunnable(FThreadSafety::ValidationThreadFunc_t InThreadFunc, void* InParam)
        : ThreadFunc(InThreadFunc)
        , Param(InParam)
    {
    }

    virtual uint32 Run() override
    {
        if (ThreadFunc)
        {
            return ThreadFunc(Param);
        }
        return 0;
    }

private:
    FThreadSafety::ValidationThreadFunc_t ThreadFunc;
    void* Param;
};

uint32 FThreadSafety::ValidationThreadFunc(void* ParamPtr)
{
    FValidationThreadData* Data = static_cast<FValidationThreadData*>(ParamPtr);
    
    if (Data && Data->TestFunction)
    {
        try
        {
            // Execute the test function
            Data->TestFunction();
        }
        catch (...)
        {
            // Count any exception as an error
            if (Data->ErrorCount)
            {
                Data->ErrorCount->Increment();
            }
        }
    }
    
    return 0;
}

bool FThreadSafety::ValidateThreadSafety(TFunction<void()> TestFunc, int32 ThreadCount, int32 IterationCount)
{
    if (!TestFunc || ThreadCount <= 0 || IterationCount <= 0)
    {
        return false;
    }
    
    // Setup validation data
    FThreadSafeCounter ErrorCount(0);
    
    // Create and start threads
    TArray<FRunnableThread*> Threads;
    TArray<FValidationThreadData*> ThreadData;
    TArray<FValidationThreadRunnable*> Runnables;
    
    for (int32 i = 0; i < ThreadCount; ++i)
    {
        FValidationThreadData* Data = new FValidationThreadData();
        Data->ErrorCount = &ErrorCount;
        
        // Create a wrapper function that executes the test function IterationCount times
        Data->TestFunction = [TestFunc, IterationCount]()
        {
            for (int32 j = 0; j < IterationCount; ++j)
            {
                TestFunc();
            }
        };
        
        ThreadData.Add(Data);
        
        // Create runnable object and thread
        FString ThreadName = FString::Printf(TEXT("ValidationThread%d"), i);
        FValidationThreadRunnable* Runnable = new FValidationThreadRunnable(ValidationThreadFunc, Data);
        Runnables.Add(Runnable);
        
        FRunnableThread* Thread = FRunnableThread::Create(Runnable, *ThreadName);
        Threads.Add(Thread);
    }
    
    // Wait for all threads to complete
    for (FRunnableThread* Thread : Threads)
    {
        Thread->WaitForCompletion();
        delete Thread;
    }
    
    // Clean up resources
    for (FValidationThreadRunnable* Runnable : Runnables)
    {
        delete Runnable;
    }
    
    for (FValidationThreadData* Data : ThreadData)
    {
        delete Data;
    }
    
    return (ErrorCount.GetValue() == 0);
}

void FThreadSafety::RecordContention(void* LockPtr)
{
    if (!LockPtr)
    {
        return;
    }
    
    FScopeLock Lock(&ContentionStatsLock);
    
    FLockContentionStats& Stats = ContentionStats.FindOrAdd(LockPtr);
    Stats.ContentionCount++;
    Stats.LastContentionTime = FPlatformTime::Seconds();
}

void FThreadSafety::ResetContentionTracking()
{
    FScopeLock Lock(&ContentionStatsLock);
    ContentionStats.Empty();
}

FString FThreadSafety::GenerateContentionReport(void* LockPtr, float MonitoringPeriodSeconds)
{
    FScopeLock Lock(&ContentionStatsLock);
    
    FString Report = TEXT("Lock Contention Report:\n");
    double CurrentTime = FPlatformTime::Seconds();
    
    if (LockPtr)
    {
        // Report for specific lock
        FLockContentionStats* Stats = ContentionStats.Find(LockPtr);
        
        if (Stats)
        {
            double TimeSinceLastContention = CurrentTime - Stats->LastContentionTime;
            // Use FString for safer string formatting
            FString ContentionInfo = FString::Format(TEXT("Lock 0x{0}: Contentions: {1}, Last Contention: {2:.2f}s ago\n"),
                { FString::Printf(TEXT("%p"), LockPtr), Stats->ContentionCount, TimeSinceLastContention });
            Report += ContentionInfo;
        }
        else
        {
            // Use FString for safer string formatting
            Report += FString::Format(TEXT("Lock 0x{0}: No contention recorded\n"), 
                { FString::Printf(TEXT("%p"), LockPtr) });
        }
    }
    else
    {
        // Report for all locks
        if (ContentionStats.Num() == 0)
        {
            Report += TEXT("No lock contention recorded.\n");
        }
        else
        {
            // Sort locks by contention count
            TArray<TPair<void*, FLockContentionStats>> SortedStats;
            
            for (const auto& Pair : ContentionStats)
            {
                SortedStats.Add(TPair<void*, FLockContentionStats>(Pair.Key, Pair.Value));
            }
            
            SortedStats.Sort([](const TPair<void*, FLockContentionStats>& A, const TPair<void*, FLockContentionStats>& B) {
                return A.Value.ContentionCount > B.Value.ContentionCount;
            });
            
            // Report the top 10 locks with most contention
            int32 Count = FMath::Min(10, SortedStats.Num());
            
            // Use FString for safer string formatting
            Report += FString::Format(TEXT("Top {0} locks by contention (out of {1}):\n"), 
                { Count, ContentionStats.Num() });
            
            for (int32 i = 0; i < Count; ++i)
            {
                const auto& Pair = SortedStats[i];
                double TimeSinceLastContention = CurrentTime - Pair.Value.LastContentionTime;
                
                // Use FString for safer string formatting
                FString EntryInfo = FString::Format(TEXT("{0}. Lock 0x{1}: Contentions: {2}, Last Contention: {3:.2f}s ago\n"),
                    { i + 1, FString::Printf(TEXT("%p"), Pair.Key), Pair.Value.ContentionCount, TimeSinceLastContention });
                Report += EntryInfo;
            }
        }
    }
    
    return Report;
}

void FThreadSafety::CleanupThreadLocalStorage()
{
    // Clean up thread-local accessed zones for this thread
    if (FThreadSafety::GetThreadAccessedZones())
    {
        delete FThreadSafety::GetThreadAccessedZones();
        FThreadSafety::SetThreadAccessedZones(nullptr);
    }
}