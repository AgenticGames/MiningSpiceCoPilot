// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "HAL/ThreadSafeCounter.h"
#include "Containers/Array.h"
#include "HAL/Event.h"
#include "Misc/ScopeLock.h"
#include "Misc/SpinLock.h"
#include "Containers/Map.h"
#include "String/Find.h"

/**
 * Lightweight spin lock implementation for Mining system
 * Provides fast locking with minimal overhead for short-held locks
 */
class MININGSPICECOPILOT_API FSpinLock
{
public:
    /** Constructor */
    FSpinLock() : bLocked(0) {}
    
    /** Acquires the lock */
    void Lock()
    {
        while (FPlatformAtomics::InterlockedCompareExchange(&bLocked, 1, 0) != 0)
        {
            FPlatformProcess::Yield();
        }
    }
    
    /** Releases the lock */
    void Unlock()
    {
        FPlatformAtomics::InterlockedExchange(&bLocked, 0);
    }
    
    /** Tries to acquire the lock without waiting */
    bool TryLock()
    {
        return FPlatformAtomics::InterlockedCompareExchange(&bLocked, 1, 0) == 0;
    }
    
private:
    /** Lock state (0 = unlocked, 1 = locked) */
    volatile int32 bLocked;
};

/**
 * Reader-writer lock optimized for shared distance field state
 * Provides fast access for read-heavy workloads with minimal contention
 */
class MININGSPICECOPILOT_API FMiningReaderWriterLock
{
public:
    /** Constructor */
    FMiningReaderWriterLock();
    
    /** Destructor */
    ~FMiningReaderWriterLock();
    
    /**
     * Acquires a read lock (shared access)
     * @param TimeoutMs Maximum time to wait in milliseconds (0 for no timeout)
     * @return True if the lock was acquired
     */
    bool ReadLock(uint32 TimeoutMs = 0);
    
    /** Releases a read lock */
    void ReadUnlock();
    
    /**
     * Acquires a write lock (exclusive access)
     * @param TimeoutMs Maximum time to wait in milliseconds (0 for no timeout)
     * @return True if the lock was acquired
     */
    bool WriteLock(uint32 TimeoutMs = 0);
    
    /** Releases a write lock */
    void WriteUnlock();
    
    /** Checks if a thread is currently holding a write lock */
    bool IsWriteLocked() const;
    
    /** Gets the number of active readers */
    int32 GetReaderCount() const;
    
    /** Gets whether a write is pending */
    bool IsWritePending() const;
    
    /** Tries to upgrade from a read lock to a write lock */
    bool TryUpgradeToWriteLock();
    
    /** Downgrades from a write lock to a read lock */
    void DowngradeToReadLock();

private:
    /** Number of active readers */
    FThreadSafeCounter ReaderCount;
    
    /** Flag indicating if a writer is active */
    FThreadSafeCounter WriterActive;
    
    /** Flag indicating if a writer is waiting */
    FThreadSafeCounter WriterWaiting;
    
    /** ID of the thread currently holding a write lock */
    FThreadSafeCounter WriterThreadId;
    
    /** Event for writers to wait on */
    FEvent* WriterEvent;
    
    /** Event for readers to wait on */
    FEvent* ReaderEvent;
    
    /** Gets the current thread ID */
    static uint32 GetCurrentThreadId();
};

/**
 * Scoped read lock for reader-writer lock
 * Automatically acquires read lock on construction and releases on destruction
 */
class MININGSPICECOPILOT_API FScopedReadLock
{
public:
    /**
     * Constructor
     * @param InLock Reader-writer lock to acquire read lock on
     * @param TimeoutMs Maximum time to wait in milliseconds (0 for no timeout)
     */
    FScopedReadLock(FMiningReaderWriterLock& InLock, uint32 TimeoutMs = 0);
    
    /** Destructor */
    ~FScopedReadLock();
    
    /** Checks if the lock was successfully acquired */
    bool IsLocked() const;

private:
    /** The reader-writer lock */
    FMiningReaderWriterLock& Lock;
    
    /** Whether the lock was acquired */
    bool bLocked;
};

/**
 * Scoped write lock for reader-writer lock
 * Automatically acquires write lock on construction and releases on destruction
 */
class MININGSPICECOPILOT_API FScopedWriteLock
{
public:
    /**
     * Constructor
     * @param InLock Reader-writer lock to acquire write lock on
     * @param TimeoutMs Maximum time to wait in milliseconds (0 for no timeout)
     */
    FScopedWriteLock(FMiningReaderWriterLock& InLock, uint32 TimeoutMs = 0);
    
    /** Destructor */
    ~FScopedWriteLock();
    
    /** Checks if the lock was successfully acquired */
    bool IsLocked() const;

private:
    /** The reader-writer lock */
    FMiningReaderWriterLock& Lock;
    
    /** Whether the lock was acquired */
    bool bLocked;
};

/**
 * Hierarchical lock to prevent deadlocks
 * Enforces a global lock ordering by hierarchy level
 */
class MININGSPICECOPILOT_API FHierarchicalLock
{
public:
    /** Constructor */
    explicit FHierarchicalLock(uint32 InLevel);
    
    /** Destructor */
    ~FHierarchicalLock();
    
    /**
     * Acquires the lock
     * @param TimeoutMs Maximum time to wait in milliseconds (0 for no timeout)
     * @return True if the lock was acquired
     */
    bool Lock(uint32 TimeoutMs = 0);
    
    /** Releases the lock */
    void Unlock();
    
    /** Gets the hierarchy level of this lock */
    uint32 GetLevel() const;
    
    /** Gets whether this lock is currently held by the calling thread */
    bool IsLockedByCurrentThread() const;

    /** Thread-specific highest acquired lock level - stored using platform TLS */
    static uint32 GetThreadHighestLockLevel();
    static void SetThreadHighestLockLevel(uint32 Level);

private:
    /** Lock hierarchy level */
    uint32 Level;
    
    /** The actual lock */
    FCriticalSection InternalLock;
    
    /** Thread ID that currently holds the lock */
    FThreadSafeCounter OwnerThreadId;
    
    /** Whether the lock is currently held */
    FThreadSafeCounter LockCount;
    
    /** TLS slot for the ThreadHighestLockLevel */
    static uint32 ThreadHighestLockLevelTLS;
};

/**
 * A hybrid lock that can switch between spin lock and critical section based on contention
 */
class MININGSPICECOPILOT_API FHybridLock
{
public:
    /** Constructor */
    FHybridLock();
    
    /** Destructor */
    ~FHybridLock();
    
    /**
     * Acquires the lock
     * @param TimeoutMs Maximum time to wait in milliseconds (0 for no timeout)
     * @return True if the lock was acquired
     */
    bool Lock(uint32 TimeoutMs = 0);
    
    /** Releases the lock */
    void Unlock();
    
    /** Gets the contention count */
    uint32 GetContentionCount() const;
    
    /** Resets contention statistics */
    void ResetContentionStats();
    
    /** Sets the threshold for switching lock types */
    void SetContentionThreshold(uint32 Threshold);

private:
    /** Critical section for high-contention case */
    FCriticalSection SlowLock;
    
    /** Spin lock for low-contention case */
    TAtomic<int32> FastLock;
    
    /** Current contention count */
    FThreadSafeCounter ContentionCount;
    
    /** Contention threshold for switching lock types */
    uint32 ContentionThreshold;
    
    /** Whether to use the slow lock */
    FThreadSafeCounter bUseSlowLock;
    
    /** Updates contention statistics */
    void UpdateContentionStats();
};

/**
 * Wait-free shared counter for high-performance scenarios
 */
class MININGSPICECOPILOT_API FWaitFreeCounter
{
public:
    /** Constructor */
    FWaitFreeCounter();
    
    /** Constructor with initial value */
    explicit FWaitFreeCounter(int32 InitialValue);
    
    /** Gets the current value */
    int32 GetValue() const;
    
    /** Sets the counter to a specific value */
    void SetValue(int32 Value);
    
    /** Increments the counter and returns the new value */
    int32 Increment();
    
    /** Decrements the counter and returns the new value */
    int32 Decrement();
    
    /** Adds a value to the counter and returns the new value */
    int32 Add(int32 Amount);
    
    /** Sets the counter to the given value and returns the old value */
    int32 Exchange(int32 NewValue);
    
    /** Sets the counter to the new value only if it equals the comparand */
    bool CompareExchange(int32& OutOldValue, int32 NewValue, int32 Comparand);

private:
    /** Underlying atomic counter */
    std::atomic<int32> Counter;
};

/**
 * Wait-free read lock for SDF fields
 * Provides versioned access to SDF fields without blocking readers
 */
class MININGSPICECOPILOT_API FSVOFieldReadLock
{
public:
    /** Constructor */
    FSVOFieldReadLock();
    
    /** Destructor */
    ~FSVOFieldReadLock();
    
    /**
     * Begins a read operation and returns the current version
     * Ensures a consistent read by checking version before and after
     * @return The current version of the field
     */
    uint32 BeginRead();
    
    /**
     * Begins a write operation
     * @return True if the write operation can proceed
     */
    bool BeginWrite();
    
    /**
     * Ends a write operation and increments the version
     */
    void EndWrite();
    
    /**
     * Gets the current version without locking
     * @return The current version of the field
     */
    uint32 GetCurrentVersion() const;
    
private:
    /** The current version of the field */
    FThreadSafeCounter CurrentVersion;
    
    /** Whether an update is in progress */
    FThreadSafeCounter UpdateInProgress;
};

/**
 * Statistics for zone lock usage
 */
struct MININGSPICECOPILOT_API FZoneLockStats
{
    /** Number of times the lock was acquired */
    int32 AcquisitionCount;
    
    /** Number of times the lock acquisition experienced contention */
    int32 ContentionCount;
    
    /** Total wait time in milliseconds */
    double TotalWaitTimeMs;
    
    /** Maximum wait time in milliseconds */
    double MaxWaitTimeMs;
};

/**
 * Zone-based lock for efficient concurrent access to SVO+SDF zones
 * Uses one lock per zone and enforces consistent lock acquisition order to prevent deadlocks
 */
class MININGSPICECOPILOT_API FZoneBasedLock
{
public:
    /**
     * Constructor
     * @param ZoneCount Number of zones to manage
     */
    FZoneBasedLock(int32 ZoneCount);
    
    /** Destructor */
    ~FZoneBasedLock();
    
    /**
     * Locks a specific zone
     * @param ZoneId ID of the zone to lock
     * @param TimeoutMs Maximum time to wait in milliseconds (0 for no wait)
     * @return True if the lock was acquired
     */
    bool LockZone(int32 ZoneId, uint32 TimeoutMs = 0);
    
    /**
     * Tries to lock a zone without waiting
     * @param ZoneId ID of the zone to lock
     * @return True if the lock was acquired
     */
    bool TryLockZone(int32 ZoneId);
    
    /**
     * Unlocks a previously locked zone
     * @param ZoneId ID of the zone to unlock
     */
    void UnlockZone(int32 ZoneId);
    
    /**
     * Locks multiple zones in a deadlock-safe manner
     * @param ZoneIds Array of zone IDs to lock
     * @param TimeoutMs Maximum time to wait in milliseconds (0 for no wait)
     * @return True if all locks were acquired
     */
    bool LockMultipleZones(const TArray<int32>& ZoneIds, uint32 TimeoutMs = 0);
    
    /**
     * Unlocks multiple previously locked zones
     * @param ZoneIds Array of zone IDs to unlock
     */
    void UnlockMultipleZones(const TArray<int32>& ZoneIds);
    
    /**
     * Checks if a zone is currently locked
     * @param ZoneId ID of the zone to check
     * @return True if the zone is locked
     */
    bool IsZoneLocked(int32 ZoneId) const;
    
    /**
     * Gets the ID of the thread that owns a zone lock
     * @param ZoneId ID of the zone to check
     * @return Thread ID of the owner, or INDEX_NONE if unowned
     */
    int32 GetZoneOwner(int32 ZoneId) const;

private:
    /** Number of zones managed by this lock */
    int32 ZoneCount;
    
    /** ID of the thread that owns this lock */
    int32 OwnerThreadId;
    
    /** Array of individual zone locks */
    TArray<FSpinLock*> ZoneLocks;
    
    /** Array of zone owner thread IDs */
    TArray<FThreadSafeCounter> ZoneOwners;
};

/**
 * Lock contention statistics
 */
struct MININGSPICECOPILOT_API FLockContentionStats
{
    /** Number of contentions */
    int32 ContentionCount;
    
    /** Time of the last contention */
    double LastContentionTime;
    
    /** Constructor */
    FLockContentionStats()
        : ContentionCount(0)
        , LastContentionTime(0.0)
    {
    }
};

/**
 * Thread safety utilities for the Mining system
 * Provides synchronization primitives and utilities optimized for SVO+SDF operations
 */
class MININGSPICECOPILOT_API FThreadSafety
{
public:
    /** Constructor */
    FThreadSafety();
    
    /** Destructor */
    ~FThreadSafety();
    
    /**
     * Type definition for thread function
     */
    typedef uint32 (*ValidationThreadFunc_t)(void*);
    
    /**
     * Creates a reader-writer lock
     * @return New reader-writer lock
     */
    static FMiningReaderWriterLock* CreateReaderWriterLock();
    
    /**
     * Creates a hierarchical lock at the specified level
     * @param Level Lock hierarchy level
     * @return New hierarchical lock
     */
    static FHierarchicalLock* CreateHierarchicalLock(uint32 Level);
    
    /**
     * Creates a hybrid lock
     * @return New hybrid lock
     */
    static FHybridLock* CreateHybridLock();
    
    /**
     * Creates a wait-free counter
     * @param InitialValue Initial counter value
     * @return New wait-free counter
     */
    static FWaitFreeCounter* CreateWaitFreeCounter(int32 InitialValue = 0);
    
    /**
     * Creates a SVO field read lock
     * @return Newly created SVO field read lock
     */
    static FSVOFieldReadLock* CreateSVOFieldReadLock();
    
    /**
     * Creates a zone-based lock
     * @param ZoneCount Number of zones to manage
     * @return Newly created zone-based lock
     */
    static FZoneBasedLock* CreateZoneBasedLock(int32 ZoneCount);
    
    /**
     * Detects potential deadlocks
     * @param LockName Name of the lock for reporting
     * @param TimeoutMs Timeout after which to assume deadlock
     * @return True if a potential deadlock was detected
     */
    static bool DetectPotentialDeadlock(const FString& LockName, uint32 TimeoutMs);
    
    /**
     * Gets the current thread's lock order
     * @return Array of lock levels held by the current thread
     */
    static TArray<uint32> GetThreadLockOrder();
    
    /**
     * Validates thread safety of a function
     * @param TestFunc Function to test
     * @param ThreadCount Number of threads to run the test with
     * @param IterationCount Number of iterations per thread
     * @return True if no thread safety issues were detected
     */
    static bool ValidateThreadSafety(TFunction<void()> TestFunc, int32 ThreadCount, int32 IterationCount);
    
    /**
     * Records contention for a lock
     * @param LockPtr Pointer to the lock
     */
    static void RecordContention(void* LockPtr);
    
    /**
     * Resets contention tracking
     */
    static void ResetContentionTracking();
    
    /**
     * Generates a report of lock contention
     * @param LockPtr Pointer to the lock (nullptr for all locks)
     * @param MonitoringPeriodSeconds Length of the monitoring period in seconds
     * @return Contention report as a string
     */
    static FString GenerateContentionReport(void* LockPtr, float MonitoringPeriodSeconds);
    
    /**
     * Cleans up thread-local storage
     */
    static void CleanupThreadLocalStorage();
    
    /**
     * Gets the singleton instance
     * @return Reference to the thread safety utilities
     */
    static FThreadSafety& Get();
    
    /** Friend classes that need access to internal state */
    friend class FZoneBasedLock;

private:
    /** Private constructor for singleton pattern */
    // FThreadSafety(); // Commented out duplicate constructor declaration
    
    /**
     * Initializes the thread safety utilities
     */
    void Initialize();
    
    /**
     * Shuts down the thread safety utilities
     */
    void Shutdown();
    
    /** Validation state for thread safety testing */
    struct FValidationState
    {
        /** Number of validation errors */
        FThreadSafeCounter ErrorCount;
        
        /** Test function */
        TFunction<void()> TestFunction;
    };
    
    /** Validation thread function */
    static uint32 ValidationThreadFunc(void* Params);
    
    /** Records contention for thread safety analytics (instance method) */
    void RecordContentionInternal(void* LockPtr);
    
    /** Singleton instance */
    static FThreadSafety* Instance;
    
    /** Thread-specific storage for tracking accessed zones - stored using platform TLS */
    static TArray<int32>* GetThreadAccessedZones();
    static void SetThreadAccessedZones(TArray<int32>* Zones);
    
    /** Lock contention statistics */
    static TMap<void*, FLockContentionStats> ContentionStats;
    
    /** Lock for contention statistics access */
    static FCriticalSection ContentionStatsLock;
    
    /** TLS slot for ThreadAccessedZones */
    static uint32 ThreadAccessedZonesTLS;
};

/**
 * Helper class for scoped zone locking
 */
class MININGSPICECOPILOT_API FScopedZoneLock
{
public:
    /**
     * Constructor
     * @param InLock Zone-based lock to use
     * @param ZoneId Zone ID to lock
     * @param TimeoutMs Maximum time to wait in milliseconds (0 for indefinite)
     */
    FScopedZoneLock(FZoneBasedLock& InLock, int32 ZoneId, uint32 TimeoutMs = 0)
        : Lock(InLock)
        , ZoneId(ZoneId)
        , bLocked(false)
    {
        bLocked = Lock.LockZone(ZoneId, TimeoutMs);
    }
    
    /**
     * Destructor
     */
    ~FScopedZoneLock()
    {
        if (bLocked)
        {
            Lock.UnlockZone(ZoneId);
        }
    }
    
    /**
     * Checks if the lock was successfully acquired
     * @return True if the lock was acquired
     */
    bool IsLocked() const
    {
        return bLocked;
    }
    
private:
    /** The zone-based lock */
    FZoneBasedLock& Lock;
    
    /** Zone ID that was locked */
    int32 ZoneId;
    
    /** Whether the lock was acquired */
    bool bLocked;
};

/**
 * Helper class for scoped multiple zone locking
 */
class MININGSPICECOPILOT_API FScopedMultiZoneLock
{
public:
    /**
     * Constructor
     * @param InLock Zone-based lock to use
     * @param ZoneIds Array of zone IDs to lock
     * @param TimeoutMs Maximum time to wait in milliseconds (0 for indefinite)
     */
    FScopedMultiZoneLock(FZoneBasedLock& InLock, const TArray<int32>& InZoneIds, uint32 TimeoutMs = 0)
        : Lock(InLock)
        , ZoneIds(InZoneIds)
        , bLocked(false)
    {
        bLocked = Lock.LockMultipleZones(ZoneIds, TimeoutMs);
    }
    
    /**
     * Destructor
     */
    ~FScopedMultiZoneLock()
    {
        if (bLocked)
        {
            Lock.UnlockMultipleZones(ZoneIds);
        }
    }
    
    /**
     * Checks if the locks were successfully acquired
     * @return True if all locks were acquired
     */
    bool IsLocked() const
    {
        return bLocked;
    }
    
private:
    /** The zone-based lock */
    FZoneBasedLock& Lock;
    
    /** Zone IDs that were locked */
    TArray<int32> ZoneIds;
    
    /** Whether the locks were acquired */
    bool bLocked;
};

/** Helper functions for thread safety operations */
namespace FThreadSafetyHelpers
{
    // Forward declarations for template specializations or classes
    class FRunnableAdapter;
    
    /** Tries to lock a TAtomic<int32> */
    bool TryLock(TAtomic<int32>& LockVar);
    
    /** Locks a TAtomic<int32> */
    void Lock(TAtomic<int32>& LockVar);
    
    /** Unlocks a TAtomic<int32> */
    void Unlock(TAtomic<int32>& LockVar);
    
    /** Helper for atomic compare-exchange on FThreadSafeCounter */
    bool AtomicCompareExchange(FThreadSafeCounter& Counter, int32& InOutCurrentValue, int32 NewValue);
}

/**
 * Wait-free counter implementation
 */