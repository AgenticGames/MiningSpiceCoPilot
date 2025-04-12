// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeCounter.h"
#include "Misc/SpinLock.h"

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
 * A hierarchical lock for ordered acquisition to prevent deadlocks
 */
class MININGSPICECOPILOT_API FHierarchicalLock
{
public:
    /**
     * Constructor
     * @param InLevel Lock hierarchy level (lower levels must be acquired first)
     */
    explicit FHierarchicalLock(uint32 InLevel);
    
    /** Destructor */
    ~FHierarchicalLock();
    
    /**
     * Acquires the lock, ensuring hierarchical order
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

private:
    /** Lock hierarchy level */
    uint32 Level;
    
    /** The actual lock */
    FCriticalSection InternalLock;
    
    /** Thread ID that currently holds the lock */
    FThreadSafeCounter OwnerThreadId;
    
    /** Whether the lock is currently held */
    FThreadSafeCounter LockCount;
    
    /** Thread-local highest acquired lock level */
    static thread_local uint32 ThreadHighestLockLevel;
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
    FSpinLock FastLock;
    
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
     * Detects potential deadlocks with timeout-based detection
     * @param LockName Name of the lock for logging
     * @param TimeoutMs Timeout in milliseconds
     * @return True if a potential deadlock was detected
     */
    static bool DetectPotentialDeadlock(const FString& LockName, uint32 TimeoutMs);
    
    /**
     * Gets the current thread's lock acquisition order for deadlock detection
     * @return Array of lock levels currently held by this thread
     */
    static TArray<uint32> GetThreadLockOrder();
    
    /**
     * Validates thread safety of a data structure by testing with multiple threads
     * @param TestFunc Function to test thread safety
     * @param ThreadCount Number of threads to test with
     * @param IterationCount Number of iterations per thread
     * @return True if no thread safety issues were detected
     */
    static bool ValidateThreadSafety(TFunction<void()> TestFunc, int32 ThreadCount, int32 IterationCount);
    
    /**
     * Generates a thread contention report for debugging
     * @param LockPtr Pointer to the lock to analyze
     * @param MonitoringPeriodSeconds Duration to monitor for
     * @return Contention report as a string
     */
    static FString GenerateContentionReport(void* LockPtr, float MonitoringPeriodSeconds);
    
    /**
     * Gets the singleton instance
     * @return Reference to the thread safety utilities
     */
    static FThreadSafety& Get();

private:
    /** Map of locks to their contention statistics */
    TMap<void*, uint32> LockContentionStats;
    
    /** Lock for contention stats map */
    FCriticalSection ContentionStatsLock;
    
    /** Thread safety validation state */
    struct FValidationState
    {
        /** Number of validation errors */
        FThreadSafeCounter ErrorCount;
        
        /** Test function */
        TFunction<void()> TestFunction;
    };
    
    /** Thread function for validation testing */
    static uint32 ValidationThreadFunc(void* Params);
    
    /** Records a lock contention event */
    void RecordContention(void* LockPtr);

    /** Singleton instance */
    static FThreadSafety* Instance;
};