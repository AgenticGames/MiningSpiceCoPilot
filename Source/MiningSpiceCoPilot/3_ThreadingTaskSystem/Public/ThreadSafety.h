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
#include "HAL/PlatformAffinity.h" // For NUMA functionality

// Forward declarations
class MININGSPICECOPILOT_API FThreadSafety;

/**
 * Registry lock hierarchy levels for deadlock prevention
 * Defines a strict ordering of registry locks to prevent circular dependencies
 */
enum class ERegistryLockLevel : uint32
{
    /** Service registry - lowest level */
    Service = 100,
    
    /** Zone registry - level above services */
    Zone = 200,
    
    /** Material registry - level above zones */
    Material = 300,
    
    /** SVO (Sparse Voxel Octree) registry - level above materials */
    SVO = 400,
    
    /** SDF (Signed Distance Field) registry - highest level */
    SDF = 500
};

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
 * Registry operation validator for deadlock prevention
 * Tracks lock acquisition history and validates lock hierarchies
 */
class MININGSPICECOPILOT_API FRegistryOperationValidator
{
public:
    /**
     * Checks if a lock can be safely acquired based on lock history
     * Prevents violation of the lock hierarchy
     * 
     * @param Level The level of the lock to acquire
     * @return True if the lock can be safely acquired
     */
    static bool CanSafelyAcquireLock(ERegistryLockLevel Level)
    {
        TArray<ERegistryLockLevel>& LockHistory = GetThreadLockHistoryInternal();
        
        // If no locks are held, any lock can be acquired
        if (LockHistory.Num() == 0)
        {
            return true;
        }
        
        // Check that the level is lower than the highest lock level already held
        // Higher value means lower in the hierarchy (SDF = 500 > Material = 300)
        uint32 CurrentLevel = static_cast<uint32>(LockHistory.Last());
        uint32 NewLevel = static_cast<uint32>(Level);
        
        return NewLevel > CurrentLevel;
    }
    
    /**
     * Adds a lock to the thread's lock history
     * 
     * @param Level The level of the lock being acquired
     */
    static void AddLockToHistory(ERegistryLockLevel Level)
    {
        TArray<ERegistryLockLevel>& LockHistory = GetThreadLockHistoryInternal();
        LockHistory.Add(Level);
    }
    
    /**
     * Removes a lock from the thread's lock history
     * 
     * @param Level The level of the lock being released
     */
    static void RemoveLockFromHistory(ERegistryLockLevel Level)
    {
        TArray<ERegistryLockLevel>& LockHistory = GetThreadLockHistoryInternal();
        
        // Find and remove the last occurrence of this lock level
        for (int32 Index = LockHistory.Num() - 1; Index >= 0; --Index)
        {
            if (LockHistory[Index] == Level)
            {
                LockHistory.RemoveAt(Index);
                break;
            }
        }
    }
    
    /**
     * Gets the thread's current lock history
     * 
     * @return Array of lock levels currently held by this thread
     */
    static TArray<ERegistryLockLevel> GetThreadLockHistory()
    {
        return GetThreadLockHistoryInternal();
    }
    
    /**
     * Clears the thread's lock history
     * Used for cleanup or when recovering from errors
     */
    static void ClearThreadLockHistory()
    {
        TArray<ERegistryLockLevel>& LockHistory = GetThreadLockHistoryInternal();
        LockHistory.Reset();
    }
    
    /**
     * Validates a lock acquisition sequence to prevent deadlocks
     * 
     * @param LockSequence The sequence of locks to validate
     * @return True if the sequence is valid, false if it could cause a deadlock
     */
    static bool ValidateRegistryOperationSequence(const TArray<ERegistryLockLevel>& LockSequence)
    {
        // Check that the sequence follows the hierarchy (ascending order of uint32 values)
        for (int32 i = 0; i < LockSequence.Num() - 1; ++i)
        {
            uint32 CurrentLevel = static_cast<uint32>(LockSequence[i]);
            uint32 NextLevel = static_cast<uint32>(LockSequence[i + 1]);
            
            // If the next lock is not higher in the hierarchy (lower in value), sequence is invalid
            if (NextLevel <= CurrentLevel)
            {
                return false;
            }
        }
        
        return true;
    }

private:
    /**
     * Gets the thread-local lock history
     * @return Reference to the thread's lock history
     */
    static TArray<ERegistryLockLevel>& GetThreadLockHistoryInternal()
    {
        // Thread-local storage ensures each thread has its own history
        // This approach avoids the "data with thread storage duration may not have dll interface" error
        static thread_local TArray<ERegistryLockLevel> LocalHistory;
        return LocalHistory;
    }
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
 * RAII style hierarchical lock
 * Automatically acquires lock on construction and releases on destruction
 * Also validates lock ordering against the registry hierarchy
 */
class MININGSPICECOPILOT_API FScopedHierarchicalLock
{
public:
    /**
     * Constructor
     * @param InLock The hierarchical lock to acquire
     * @param InLevel Registry lock level for validation
     * @param TimeoutMs Maximum time to wait in milliseconds (0 for no timeout)
     */
    FScopedHierarchicalLock(FHierarchicalLock* InLock, ERegistryLockLevel InLevel, uint32 TimeoutMs = 0)
        : Lock(InLock)
        , Level(InLevel)
        , bLocked(false)
    {
        if (Lock)
        {
            if (FRegistryOperationValidator::CanSafelyAcquireLock(Level))
            {
                bLocked = Lock->Lock(TimeoutMs);
                if (bLocked)
                {
                    FRegistryOperationValidator::AddLockToHistory(Level);
                }
            }
            else
            {
                // Log the violation in debug builds
                UE_LOG(LogTemp, Error, TEXT("Lock hierarchy violation detected! Attempting to acquire %s lock while holding incompatible locks."),
                    *UEnum::GetValueAsString(Level));
                
                // In shipping builds, we'll still try to acquire the lock but log the error
                bLocked = Lock->Lock(TimeoutMs);
                if (bLocked)
                {
                    FRegistryOperationValidator::AddLockToHistory(Level);
                }
            }
        }
    }
    
    /** Destructor */
    ~FScopedHierarchicalLock()
    {
        if (bLocked && Lock)
        {
            FRegistryOperationValidator::RemoveLockFromHistory(Level);
            Lock->Unlock();
        }
    }
    
    /** Returns whether the lock was successfully acquired */
    bool IsLocked() const
    {
        return bLocked;
    }
    
    /** Explicitly unlocks if currently locked */
    void Unlock()
    {
        if (bLocked && Lock)
        {
            FRegistryOperationValidator::RemoveLockFromHistory(Level);
            Lock->Unlock();
            bLocked = false;
        }
    }
    
private:
    /** The hierarchical lock */
    FHierarchicalLock* Lock;
    
    /** Registry lock level */
    ERegistryLockLevel Level;
    
    /** Whether the lock is currently held */
    bool bLocked;
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
 * Versioned type table for registry implementations
 * Provides atomic access to type information with optimistic concurrency
 */
template<typename TTypeInfo>
class MININGSPICECOPILOT_API FVersionedTypeTable
{
public:
    /** Constructor */
    FVersionedTypeTable()
        : Version(0)
    {
    }
    
    /** Gets the current version */
    uint32 LoadVersion() const
    {
        return Version.load(std::memory_order_acquire);
    }
    
    /** Increments the version */
    void IncrementVersion()
    {
        Version.fetch_add(1, std::memory_order_acq_rel);
    }
    
    /**
     * Atomically compares and exchanges the version
     * @param OutOldVersion The old version value if the exchange fails
     * @param NewVersion The new version to set
     * @param ExpectedVersion The expected current version
     * @return True if the exchange was successful
     */
    bool CompareExchangeVersion(uint32& OutOldVersion, uint32 NewVersion, uint32 ExpectedVersion)
    {
        OutOldVersion = ExpectedVersion;
        return Version.compare_exchange_strong(OutOldVersion, NewVersion, std::memory_order_acq_rel);
    }
    
    /**
     * Gets a type info by ID
     * @param TypeId The ID of the type to retrieve
     * @return Shared reference to the type info, or nullptr if not found
     */
    TSharedPtr<TTypeInfo> GetTypeInfo(uint32 TypeId) const
    {
        FScopedReadLock ReadLock(TypeTableLock);
        auto* Found = Types.Find(TypeId);
        if (Found)
        {
            return *Found;
        }
        return nullptr;
    }
    
    /**
     * Gets a type info by ID with version validation
     * @param TypeId The ID of the type to retrieve
     * @param OutVersion Receives the version at time of retrieval
     * @return Shared reference to the type info, or nullptr if not found
     */
    TSharedPtr<TTypeInfo> GetTypeInfoVersioned(uint32 TypeId, uint32& OutVersion) const
    {
        // Load the current version
        OutVersion = LoadVersion();
        
        // Try optimistic read first
        {
            FScopedReadLock ReadLock(TypeTableLock);
            auto* Found = Types.Find(TypeId);
            if (Found)
            {
                return *Found;
            }
        }
        
        return nullptr;
    }
    
    /**
     * Adds or updates a type info
     * @param TypeId The ID of the type to add or update
     * @param TypeInfo The type info to store
     */
    void AddOrUpdateTypeInfo(uint32 TypeId, TSharedRef<TTypeInfo> TypeInfo)
    {
        FScopedWriteLock WriteLock(TypeTableLock);
        Types.Add(TypeId, TypeInfo);
        IncrementVersion();
    }
    
    /**
     * Removes a type info
     * @param TypeId The ID of the type to remove
     * @return True if the type was found and removed
     */
    bool RemoveTypeInfo(uint32 TypeId)
    {
        FScopedWriteLock WriteLock(TypeTableLock);
        if (Types.Remove(TypeId) > 0)
        {
            IncrementVersion();
            return true;
        }
        return false;
    }
    
    /**
     * Gets all type IDs
     * @return Array of all type IDs
     */
    TArray<uint32> GetAllTypeIds() const
    {
        TArray<uint32> Result;
        FScopedReadLock ReadLock(TypeTableLock);
        Types.GetKeys(Result);
        return Result;
    }
    
    /**
     * Gets all type infos
     * @return Map of all type IDs to type infos
     */
    TMap<uint32, TSharedRef<TTypeInfo>> GetAllTypeInfos() const
    {
        FScopedReadLock ReadLock(TypeTableLock);
        return Types;
    }
    
    /**
     * Clears all type infos
     */
    void Clear()
    {
        FScopedWriteLock WriteLock(TypeTableLock);
        Types.Empty();
        IncrementVersion();
    }
    
    /**
     * Gets the number of types in the table
     * @return Number of types
     */
    int32 Num() const
    {
        FScopedReadLock ReadLock(TypeTableLock);
        return Types.Num();
    }

private:
    /** Table of type infos */
    TMap<uint32, TSharedRef<TTypeInfo>> Types;
    
    /** Current version of the table */
    std::atomic<uint32> Version;
    
    /** Lock for table modification */
    mutable FMiningReaderWriterLock TypeTableLock;
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
 * NUMA domain information structure
 * Provides memory locality information for thread optimization
 */
struct MININGSPICECOPILOT_API FNUMADomainInfo
{
    /** Domain ID */
    uint32 DomainId;
    
    /** Logical processor cores belonging to this domain */
    TArray<uint32> LogicalCores;
    
    /** Local memory capacity for this domain in bytes */
    uint64 LocalMemoryBytes;
    
    /** Distance matrix to other domains (relative access cost) */
    TArray<float> DistanceToOtherDomains;
    
    /** Constructor */
    FNUMADomainInfo()
        : DomainId(0)
        , LocalMemoryBytes(0)
    {
    }
    
    /** Checks if a thread is running on this domain */
    bool IsThreadInDomain(uint32 ThreadId) const;
    
    /** Gets the most optimal thread for this domain */
    uint32 GetOptimalThreadForDomain() const;
};

/**
 * NUMA topology detection and management
 * Provides information about hardware memory domains
 */
struct MININGSPICECOPILOT_API FNUMATopology
{
    /** Available NUMA domains */
    TArray<FNUMADomainInfo> Domains;
    
    /** Number of NUMA domains detected */
    uint32 DomainCount;
    
    /** Whether NUMA is supported on this system */
    bool bNUMASupported;
    
    /** Default constructor */
    FNUMATopology()
        : DomainCount(0)
        , bNUMASupported(false)
    {
    }
    
    /** Detects the NUMA topology */
    void DetectTopology();
    
    /** Gets a domain by ID */
    const FNUMADomainInfo* GetDomain(uint32 DomainId) const;
    
    /** Gets the domain containing a thread */
    uint32 GetDomainForThread(uint32 ThreadId) const;
    
    /** Gets the logical cores for a domain */
    TArray<uint32> GetLogicalCoresForDomain(uint32 DomainId) const;
    
    /** Gets a mask for all cores in a domain */
    uint64 GetAffinityMaskForDomain(uint32 DomainId) const;
};

/**
 * NUMA-optimized spin lock for local-domain priority
 * Prioritizes threads from the same NUMA domain when resolving contention
 */
class MININGSPICECOPILOT_API FNUMAOptimizedSpinLock
{
public:
    /** Constructor */
    FNUMAOptimizedSpinLock(uint32 PreferredDomainId = 0)
        : bLocked(0)
        , OwnerThreadId(0)
        , PreferredDomain(PreferredDomainId)
    {
    }
    
    /** Acquires the lock, optimizing for same-domain threads */
    void Lock()
    {
        uint32 CurrentThreadId = FPlatformTLS::GetCurrentThreadId();
        // Temporarily use domain 0 until FThreadSafety is fully initialized
        uint32 CurrentDomain = 0; // Will be properly implemented after FThreadSafety is fully defined
        
        // Use an exponential backoff strategy to reduce contention
        uint32 YieldCount = 0;
        
        while (FPlatformAtomics::InterlockedCompareExchange(&bLocked, 1, 0) != 0)
        {
            // Track contention for diagnostics - will implement properly later
            // FThreadSafety::Get().RecordContention(this); - Commented out to avoid circular reference
            
            // Different backoff strategies based on thread domain
            if (CurrentDomain == PreferredDomain)
            {
                // Preferred domain threads use shorter backoff
                if (YieldCount < 10)
                {
                    FPlatformProcess::YieldThread();
                }
                else
                {
                    // Brief sleep for longer waits
                    FPlatformProcess::Sleep(0.0001f);
                }
            }
            else
            {
                // Non-preferred domains use longer backoff
                if (YieldCount < 5)
                {
                    FPlatformProcess::YieldThread();
                }
                else
                {
                    // Longer sleep for non-preferred domains
                    FPlatformProcess::Sleep(0.0005f);
                }
            }
            
            YieldCount++;
        }
        
        // Lock acquired
        OwnerThreadId = CurrentThreadId;
    }
    
    /** Releases the lock */
    void Unlock()
    {
        OwnerThreadId = 0;
        FPlatformAtomics::InterlockedExchange(&bLocked, 0);
    }
    
    /** Tries to acquire the lock without waiting */
    bool TryLock()
    {
        if (FPlatformAtomics::InterlockedCompareExchange(&bLocked, 1, 0) == 0)
        {
            OwnerThreadId = FPlatformTLS::GetCurrentThreadId();
            return true;
        }
        return false;
    }
    
    /** Sets the preferred NUMA domain for this lock */
    void SetPreferredDomain(uint32 DomainId)
    {
        PreferredDomain = DomainId;
    }
    
    /** Gets the preferred NUMA domain for this lock */
    uint32 GetPreferredDomain() const
    {
        return PreferredDomain;
    }
    
private:
    /** Lock state (0 = unlocked, 1 = locked) */
    volatile int32 bLocked;
    
    /** ID of the thread that currently owns the lock */
    volatile uint32 OwnerThreadId;
    
    /** Preferred NUMA domain ID */
    uint32 PreferredDomain;
};

/**
 * NUMA-local cache for type information
 * Provides domain-local caching of frequently accessed registry types
 */
class MININGSPICECOPILOT_API FNUMALocalTypeCache
{
public:
    /** Cached type entry with version validation */
    template<typename T>
    struct FCachedTypeEntry
    {
        /** Type information */
        TSharedRef<T> TypeInfo;
        
        /** Version at time of caching */
        uint32 Version;
        
        /** Last access time */
        double LastAccessTime;
        
        /** Number of times accessed */
        uint32 AccessCount;
        
        /** Constructor */
        FCachedTypeEntry()
            : Version(0)
            , LastAccessTime(0.0)
            , AccessCount(0)
        {
        }
        
        /** Constructor with type info */
        FCachedTypeEntry(const TSharedRef<T>& InTypeInfo, uint32 InVersion)
            : TypeInfo(InTypeInfo)
            , Version(InVersion)
            , LastAccessTime(FPlatformTime::Seconds())
            , AccessCount(1)
        {
        }
    };
    
    /** Constructor */
    FNUMALocalTypeCache(uint32 InDomainId)
        : DomainId(InDomainId)
        , TotalMemoryUsed(0)
        , MaxCacheMemory(64 * 1024 * 1024) // 64MB default limit
    {
    }
    
    /**
     * Tries to get a cached type
     * @param TypeId The type ID to look up
     * @param OutTypeInfo The output type info if found
     * @return True if the type was found in cache
     */
    template<typename T>
    bool TryGetCachedType(uint32 TypeId, TSharedRef<T>& OutTypeInfo, uint32 CurrentVersion)
    {
        FScopeLock Lock(&CacheLock);
        
        FCachedTypeEntry<T>* Entry = TypeCache.Find(TypeId);
        if (Entry && Entry->Version == CurrentVersion)
        {
            OutTypeInfo = Entry->TypeInfo;
            Entry->LastAccessTime = FPlatformTime::Seconds();
            Entry->AccessCount++;
            
            // Record access for optimization
            RecordTypeAccess(TypeId);
            
            return true;
        }
        
        return false;
    }
    
    /**
     * Caches a type
     * @param TypeId The type ID
     * @param TypeInfo The type information to cache
     * @param Version The current version of the type
     */
    template<typename T>
    void CacheType(uint32 TypeId, const TSharedRef<T>& TypeInfo, uint32 Version)
    {
        FScopeLock Lock(&CacheLock);
        
        // Create or update cache entry
        FCachedTypeEntry<T>& Entry = TypeCache.FindOrAdd(TypeId);
        Entry.TypeInfo = TypeInfo;
        Entry.Version = Version;
        Entry.LastAccessTime = FPlatformTime::Seconds();
        Entry.AccessCount++;
        
        // Record access for optimization
        RecordTypeAccess(TypeId);
        
        // If memory limit exceeded, evict least accessed entries
        UpdateMemoryUsage();
        if (TotalMemoryUsed > MaxCacheMemory)
        {
            EvictLeastUsedEntries();
        }
    }
    
    /**
     * Invalidates a cached type
     * @param TypeId The type ID to invalidate, or MAX_uint32 to invalidate all
     */
    void InvalidateCache(uint32 TypeId = MAX_uint32)
    {
        FScopeLock Lock(&CacheLock);
        
        if (TypeId == MAX_uint32)
        {
            TypeCache.Empty();
            TypeAccessCounts.Empty();
            TotalMemoryUsed = 0;
        }
        else
        {
            TypeCache.Remove(TypeId);
            TypeAccessCounts.Remove(TypeId);
            UpdateMemoryUsage();
        }
    }
    
    /**
     * Records a type access for optimization
     * @param TypeId The type ID that was accessed
     */
    void RecordTypeAccess(uint32 TypeId)
    {
        uint32& Count = TypeAccessCounts.FindOrAdd(TypeId);
        Count++;
    }
    
    /**
     * Gets the most frequently accessed types
     * @param MaxCount Maximum number of types to return
     * @return Array of type IDs sorted by access frequency
     */
    TArray<uint32> GetMostAccessedTypes(uint32 MaxCount)
    {
        FScopeLock Lock(&CacheLock);
        
        // Sort types by access count
        TArray<TPair<uint32, uint32>> SortedTypes;
        for (const TPair<uint32, uint32>& Pair : TypeAccessCounts)
        {
            SortedTypes.Add(Pair);
        }
        
        SortedTypes.Sort([](const TPair<uint32, uint32>& A, const TPair<uint32, uint32>& B) {
            return A.Value > B.Value;
        });
        
        // Extract the most accessed types
        TArray<uint32> Result;
        for (int32 i = 0; i < FMath::Min((int32)MaxCount, SortedTypes.Num()); ++i)
        {
            Result.Add(SortedTypes[i].Key);
        }
        
        return Result;
    }
    
    /**
     * Gets the domain ID for this cache
     * @return NUMA domain ID
     */
    uint32 GetDomainId() const
    {
        return DomainId;
    }
    
    /**
     * Sets the maximum cache memory
     * @param MaxMemoryBytes Maximum memory in bytes
     */
    void SetMaxCacheMemory(uint64 MaxMemoryBytes)
    {
        MaxCacheMemory = MaxMemoryBytes;
    }
    
private:
    /** NUMA domain ID */
    uint32 DomainId;
    
    /** Type cache - maps type ID to cached entry */
    TMap<uint32, void*> TypeCache;
    
    /** Type access counts for optimization */
    TMap<uint32, uint32> TypeAccessCounts;
    
    /** Lock for cache access */
    FCriticalSection CacheLock;
    
    /** Total memory used by the cache in bytes */
    uint64 TotalMemoryUsed;
    
    /** Maximum memory allowed for the cache in bytes */
    uint64 MaxCacheMemory;
    
    /** Updates the total memory usage calculation */
    void UpdateMemoryUsage()
    {
        // Estimate memory usage based on cache entries
        TotalMemoryUsed = TypeCache.Num() * sizeof(void*) * 1000; // Rough estimate
    }
    
    /** Evicts the least used entries when memory limit is exceeded */
    void EvictLeastUsedEntries()
    {
        // Sort by access count and time (least accessed and oldest first)
        TArray<TPair<uint32, uint32>> SortedByAccess;
        for (const TPair<uint32, uint32>& Pair : TypeAccessCounts)
        {
            SortedByAccess.Add(Pair);
        }
        
        SortedByAccess.Sort([](const TPair<uint32, uint32>& A, const TPair<uint32, uint32>& B) {
            return A.Value < B.Value;
        });
        
        // Remove 25% of least used entries
        int32 EntriestoRemove = FMath::Max(1, TypeCache.Num() / 4);
        for (int32 i = 0; i < EntriestoRemove && i < SortedByAccess.Num(); ++i)
        {
            uint32 TypeId = SortedByAccess[i].Key;
            TypeCache.Remove(TypeId);
            TypeAccessCounts.Remove(TypeId);
        }
        
        UpdateMemoryUsage();
    }
};

/**
 * Thread safety utilities for the Mining system
 * Provides synchronization primitives and concurrency utilities
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

    /** NUMA topology information for the system */
    FNUMATopology NUMATopology;

    /**
     * Detects NUMA topology of the current system
     * @return Information about NUMA domains on this hardware
     */
    static FNUMATopology DetectNUMATopology();
    
    /**
     * Gets the NUMA domain for the current thread
     * @return Domain ID where the thread is running
     */
    uint32 GetCurrentThreadNUMADomain() const;
    
    /**
     * Assigns a thread to a specific NUMA domain
     * @param ThreadId The thread ID to assign
     * @param DomainId The target NUMA domain
     * @return True if assignment was successful
     */
    bool AssignThreadToNUMADomain(uint32 ThreadId, uint32 DomainId);
    
    /**
     * Creates a NUMA-optimized spin lock for a preferred domain
     * @param PreferredDomain The domain to optimize for
     * @return Pointer to a newly created lock
     */
    FNUMAOptimizedSpinLock* CreateNUMAOptimizedLock(uint32 PreferredDomain = 0);
    
    /**
     * Gets domain-to-thread assignments
     * @return Map of domain IDs to thread IDs
     */
    TMap<uint32, TArray<uint32>> GetDomainThreadAssignments() const;
    
    /**
     * Selects the optimal thread for an operation
     * @param DomainId The preferred NUMA domain
     * @return Thread ID most suitable for the domain
     */
    uint32 SelectOptimalThreadForDomain(uint32 DomainId) const;
    
    /**
     * Gets or creates a domain-local type cache
     * @param DomainId The NUMA domain ID
     * @return Pointer to the domain's type cache
     */
    FNUMALocalTypeCache* GetOrCreateDomainTypeCache(uint32 DomainId);
    
    /**
     * Gets memory usage statistics for NUMA domains
     * @return Map of domain IDs to memory usage information
     */
    TMap<uint32, FString> GetDomainMemoryStats() const;
    
    /**
     * Logs current NUMA topology information
     */
    void LogNUMATopology() const;

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
    
    /** Map of thread IDs to NUMA domain IDs */
    TMap<uint32, uint32> ThreadDomainMap;
    
    /** Lock for thread domain map access */
    mutable FCriticalSection ThreadDomainMapLock;
    
    /** Per-domain type caches */
    TMap<uint32, FNUMALocalTypeCache*> DomainTypeCaches;
    
    /** Lock for domain type cache access */
    mutable FCriticalSection DomainCacheLock;
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

/**
 * Helpers for thread-safe operations
 */
class MININGSPICECOPILOT_API FThreadSafetyHelpers
{
public:
    /**
     * Atomically compares a counter to a comparand and sets to a new value if equal
     * @param Counter The counter to modify
     * @param OutOldValue The previous value of the counter
     * @param NewValue The new value to set if Counter equals Comparand
     * @param Comparand The value to compare against
     * @return True if the counter was modified, false otherwise
     */
    static bool AtomicCompareExchange(FThreadSafeCounter& Counter, int32& OutOldValue, int32 NewValue, int32 Comparand)
    {
        OutOldValue = Counter.GetValue();
        volatile int32* CounterPtr = reinterpret_cast<volatile int32*>(&Counter);
        return FPlatformAtomics::InterlockedCompareExchange(CounterPtr, NewValue, Comparand) == Comparand;
    }
};

/**
 * RAII-style spin lock guard for FSpinLock
 * Automatically acquires lock on construction and releases on destruction
 */
class MININGSPICECOPILOT_API FScopedSpinLock
{
public:
    /**
     * Constructor
     * @param InLock Spin lock to acquire
     */
    FScopedSpinLock(FSpinLock& InLock)
        : Lock(InLock)
        , bLocked(true)
    {
        Lock.Lock();
    }
    
    /**
     * Constructor for const locks
     * @param InLock Const spin lock to acquire
     */
    FScopedSpinLock(const FSpinLock& InLock)
        : Lock(const_cast<FSpinLock&>(InLock))
        , bLocked(true)
    {
        Lock.Lock();
    }
    
    /** Destructor */
    ~FScopedSpinLock()
    {
        if (bLocked)
        {
            Lock.Unlock();
        }
    }
    
    /** Explicitly unlock before destruction */
    void Unlock()
    {
        if (bLocked)
        {
            Lock.Unlock();
            bLocked = false;
        }
    }
    
    /** Check if we're currently holding the lock */
    bool IsLocked() const
    {
        return bLocked;
    }
    
private:
    /** The lock we're managing */
    FSpinLock& Lock;
    
    /** Whether we currently hold the lock */
    bool bLocked;
    
    /** Disable copying */
    FScopedSpinLock(const FScopedSpinLock&) = delete;
    FScopedSpinLock& operator=(const FScopedSpinLock&) = delete;
};

/**
 * Utility class for atomic operations
 * Provides helper methods for safely working with atomic values
 */
class MININGSPICECOPILOT_API FAtomicUtils
{
public:
    /**
     * Performs an atomic compare-exchange operation on a ThreadSafeCounter
     * 
     * @param Counter The counter to operate on
     * @param OutOldValue Receives the old value from the counter
     * @param NewValue The value to set if the comparison succeeds
     * @param Comparand The value to compare against
     * @return True if the exchange was performed, false otherwise
     */
    static bool AtomicCompareExchange(FThreadSafeCounter& Counter, int32& OutOldValue, int32 NewValue, int32 Comparand)
    {
        // Access the internal counter value directly
        OutOldValue = Counter.GetValue();
        
        // Get a pointer to the counter's internal value for atomic operation
        // This is safe because we know FThreadSafeCounter wraps a volatile int32
        volatile int32* CounterPtr = reinterpret_cast<volatile int32*>(&Counter);
        
        // Perform the atomic compare-exchange operation
        int32 Result = FPlatformAtomics::InterlockedCompareExchange(CounterPtr, NewValue, Comparand);
        
        // If the result equals the comparand, the exchange was performed
        return Result == Comparand;
    }
    
    /**
     * Performs an atomic exchange operation on a ThreadSafeCounter
     * 
     * @param Counter The counter to operate on
     * @param NewValue The value to set
     * @return The previous value of the counter
     */
    static int32 AtomicExchange(FThreadSafeCounter& Counter, int32 NewValue)
    {
        volatile int32* CounterPtr = reinterpret_cast<volatile int32*>(&Counter);
        return FPlatformAtomics::InterlockedExchange(CounterPtr, NewValue);
    }
};