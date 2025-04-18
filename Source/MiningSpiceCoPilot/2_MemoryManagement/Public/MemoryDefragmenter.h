// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IPoolAllocator.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Math/UnrealMathUtility.h"
#include "Containers/Queue.h"

// Forward declarations
class FPoolAllocator;
class IPoolAllocator; // Add this forward declaration
class IMemoryManager;
class FEvent;

/**
 * Defragmentation priority levels
 */
enum class EDefragPriority : uint8
{
    Low,        // For non-critical allocations
    Normal,     // Standard defragmentation
    High,       // For regions with high access frequency
    Critical    // For regions that must be defragmented immediately
};

/**
 * Status of the defragmentation process
 */
enum class EDefragStatus : uint8
{
    Idle,           // No defragmentation in progress
    Scheduled,      // Defragmentation is scheduled
    InProgress,     // Defragmentation is in progress
    Paused,         // Defragmentation is paused
    Completed,      // Defragmentation completed successfully
    Failed          // Defragmentation failed
};

/**
 * Defragmentation statistics
 */
struct FDefragStats
{
    /** Number of moved allocations */
    uint32 AllocsMoved;

    /** Number of references updated */
    uint32 ReferencesUpdated;

    /** Number of bytes moved */
    uint64 BytesMoved;

    /** Total time spent defragmenting in milliseconds */
    double TotalTimeMs;
    
    /** Maximum time spent in a single defragmentation pass */
    double MaxPassTimeMs;
    
    /** Number of defragmentation passes performed */
    uint32 PassesCompleted;
    
    /** Current fragmentation percentage (0-100) */
    float FragmentationPercentage;
    
    /** Starting fragmentation percentage before defrag */
    float StartingFragmentationPercentage;
    
    /** Memory recovered in bytes */
    uint64 MemoryRecovered;
    
    /** Whether defragmentation is in progress */
    bool bInProgress;
    
    /** Default constructor */
    FDefragStats()
        : AllocsMoved(0)
        , ReferencesUpdated(0)
        , BytesMoved(0)
        , TotalTimeMs(0.0)
        , MaxPassTimeMs(0.0)
        , PassesCompleted(0)
        , FragmentationPercentage(0.0f)
        , StartingFragmentationPercentage(0.0f)
        , MemoryRecovered(0)
        , bInProgress(false)
    {
    }
    
    /** Reset all statistics */
    void Reset()
    {
        AllocsMoved = 0;
        ReferencesUpdated = 0;
        BytesMoved = 0;
        TotalTimeMs = 0.0;
        MaxPassTimeMs = 0.0;
        PassesCompleted = 0;
        FragmentationPercentage = 0.0f;
        StartingFragmentationPercentage = 0.0f;
        MemoryRecovered = 0;
        bInProgress = false;
    }
};

/**
 * Active memory defragmentation system
 * Provides incremental defragmentation with reference updating and minimal pause times
 */
class MININGSPICECOPILOT_API FMemoryDefragmenter : public FRunnable
{
public:
    /**
     * Constructor
     * @param InMemoryManager Memory manager reference for access to pools
     */
    explicit FMemoryDefragmenter(IMemoryManager* InMemoryManager);
    
    /** Destructor */
    virtual ~FMemoryDefragmenter();
    
    /**
     * Initialize the defragmenter
     * @return True if initialization was successful
     */
    bool Initialize();
    
    /**
     * Shutdown the defragmenter
     */
    void Shutdown();
    
    /**
     * Schedule defragmentation for a specific pool
     * @param PoolName Name of the pool to defragment
     * @param Priority Priority of the defragmentation
     * @param MaxTimeMs Maximum time in ms to spend on defragmentation
     * @return True if defragmentation was scheduled successfully
     */
    bool ScheduleDefragmentation(
        const FName& PoolName,
        EDefragPriority Priority = EDefragPriority::Normal,
        float MaxTimeMs = 5.0f);
    
    /**
     * Schedule defragmentation for all pools
     * @param Priority Priority of the defragmentation
     * @param MaxTimeMs Maximum time in ms to spend on defragmentation per pool
     * @return True if defragmentation was scheduled successfully
     */
    bool ScheduleDefragmentationForAllPools(
        EDefragPriority Priority = EDefragPriority::Normal,
        float MaxTimeMs = 5.0f);
    
    /**
     * Run defragmentation synchronously
     * @param PoolName Name of the pool to defragment
     * @param MaxTimeMs Maximum time in ms to spend on defragmentation
     * @return True if defragmentation completed successfully
     */
    bool DefragmentSynchronous(
        const FName& PoolName,
        float MaxTimeMs = 5.0f);
    
    /**
     * Run defragmentation on all pools synchronously
     * @param MaxTimeMs Maximum time in ms to spend on defragmentation per pool
     * @return True if defragmentation completed successfully
     */
    bool DefragmentAllPoolsSynchronous(float MaxTimeMs = 5.0f);
    
    /**
     * Pause defragmentation
     * @return True if defragmentation was paused successfully
     */
    bool PauseDefragmentation();
    
    /**
     * Resume defragmentation
     * @return True if defragmentation was resumed successfully
     */
    bool ResumeDefragmentation();
    
    /**
     * Cancel defragmentation
     * @return True if defragmentation was canceled successfully
     */
    bool CancelDefragmentation();
    
    /**
     * Get defragmentation statistics
     * @param OutStats Statistics structure to fill
     */
    void GetDefragmentationStats(FDefragStats& OutStats) const;
    
    /**
     * Get defragmentation status
     * @return Current defragmentation status
     */
    EDefragStatus GetDefragmentationStatus() const;
    
    /**
     * Register an allocation for reference tracking
     * @param Ptr Pointer to the allocation
     * @param ReferencesTo Array of pointers referenced by this allocation
     * @return True if registration was successful
     */
    bool RegisterAllocationReferences(
        void* Ptr,
        const TArray<void*>& ReferencesTo);
    
    /**
     * Update references after moving an allocation
     * @param OldPtr Old pointer to the allocation
     * @param NewPtr New pointer to the allocation
     * @return Number of references updated
     */
    uint32 UpdateReferences(void* OldPtr, void* NewPtr);
    
    /**
     * Unregister an allocation from reference tracking
     * @param Ptr Pointer to the allocation
     * @return True if unregistration was successful
     */
    bool UnregisterAllocationReferences(void* Ptr);
    
    /**
     * Calculate fragmentation metrics for a pool
     * @param PoolName Name of the pool
     * @param OutFragmentationPercent Fragmentation percentage (0-100)
     * @param OutLargestFreeBlockSize Size of the largest contiguous free block
     * @return True if metrics were calculated successfully
     */
    bool GetPoolFragmentationMetrics(
        const FName& PoolName,
        float& OutFragmentationPercent,
        uint64& OutLargestFreeBlockSize) const;
    
    /**
     * Set defragmentation threshold percentage
     * @param ThresholdPercent Fragmentation threshold percentage to trigger automatic defrag (0-100)
     */
    void SetDefragmentationThreshold(float ThresholdPercent);
    
    /**
     * Get defragmentation threshold percentage
     * @return Current fragmentation threshold percentage
     */
    float GetDefragmentationThreshold() const;
    
    /**
     * Enable or disable automatic defragmentation
     * @param bEnable Whether to enable automatic defragmentation
     */
    void SetAutoDefragmentationEnabled(bool bEnable);
    
    /**
     * Check if automatic defragmentation is enabled
     * @return True if automatic defragmentation is enabled
     */
    bool IsAutoDefragmentationEnabled() const;
    
    /**
     * Set whether defragmentation should run on a separate thread
     * @param bInThreaded Whether to run on a separate thread
     */
    void SetThreadedDefragmentation(bool bInThreaded);
    
    /**
     * Check if threaded defragmentation is enabled
     * @return True if threaded defragmentation is enabled
     */
    bool IsThreadedDefragmentation() const;
    
    /**
     * Register a type that supports versioning for defragmentation
     * @param TypeId ID of the type to register
     * @return True if the type was successfully registered
     */
    bool RegisterVersionedType(uint32 TypeId);
    
    //~ Begin FRunnable Interface
    virtual bool Init() override;
    virtual uint32 Run() override;
    virtual void Stop() override;
    virtual void Exit() override;
    //~ End FRunnable Interface

protected:
    /** Memory manager reference */
    IMemoryManager* MemoryManager;
    
    /** Thread for defragmentation */
    FRunnableThread* Thread;
    
    /** Event to signal the thread */
    FEvent* ThreadEvent;
    
    /** Whether the thread should stop */
    FThreadSafeBool bShouldStop;
    
    /** Whether defragmentation is paused */
    FThreadSafeBool bIsPaused;
    
    /** Whether defragmentation is in progress */
    FThreadSafeBool bInProgress;
    
    /** Current defragmentation status */
    EDefragStatus Status;
    
    /** Defragmentation statistics */
    FDefragStats Stats;
    
    /** Critical section for thread safety */
    mutable FCriticalSection DefragLock;
    
    /** Queue of pools to defragment */
    TQueue<TPair<FName, TPair<EDefragPriority, float>>> DefragQueue;
    
    /** Map of allocation pointers to references */
    TMap<void*, TArray<void*>> AllocationReferences;
    
    /** Map of pointers to allocations that reference them */
    TMap<void*, TArray<void*>> ReferenceToAllocations;
    
    /** Critical section for reference tracking */
    mutable FCriticalSection ReferenceLock;
    
    /** Fragmentation threshold percentage (0-100) */
    float FragmentationThreshold;
    
    /** Whether automatic defragmentation is enabled */
    bool bAutoDefragEnabled;
    
    /** Whether defragmentation runs on a separate thread */
    bool bThreadedDefragmentation;
    
    /** Set of type IDs that support versioning */
    TSet<uint32> VersionedTypes;
    
private:
    /**
     * Process defragmentation for a specific pool
     * @param PoolName Name of the pool to defragment
     * @param MaxTimeMs Maximum time in ms to spend on defragmentation
     * @return True if defragmentation completed successfully
     */
    bool ProcessPoolDefragmentation(const FName& PoolName, float MaxTimeMs);
    
    /**
     * Calculate pool fragmentation metrics
     * @param Pool Pool to calculate metrics for
     * @param OutFragmentationPercent Fragmentation percentage (0-100)
     * @param OutLargestFreeBlockSize Size of the largest contiguous free block
     * @return True if metrics were calculated successfully
     */
    bool CalculateFragmentationMetrics(
        IPoolAllocator* Pool,
        float& OutFragmentationPercent,
        uint64& OutLargestFreeBlockSize) const;
    
    /**
     * Update the defragmentation status
     * @param NewStatus New status for the defragmentation process
     */
    void UpdateStatus(EDefragStatus NewStatus);
    
    /**
     * Update statistics for completed defragmentation
     * @param Pool Pool that was defragmented
     * @param TimeSpentMs Time spent on defragmentation
     * @param BytesMoved Bytes moved during defragmentation
     */
    void UpdateDefragStats(IPoolAllocator* Pool, double TimeSpentMs, uint64 BytesMoved);
};
