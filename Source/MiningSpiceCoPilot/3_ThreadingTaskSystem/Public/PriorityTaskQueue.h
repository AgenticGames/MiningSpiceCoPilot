// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IThreadSafeQueue.h"
#include "HAL/ThreadSafeCounter.h"

/**
 * Priority level record for the queue
 */
struct FPriorityLevel
{
    /** Priority value (higher = more priority) */
    uint8 Priority;
    
    /** Tasks at this priority level */
    TArray<void*> Tasks;
    
    /** Task count at this priority level */
    FThreadSafeCounter Count;
    
    /** Time since last task was dequeued from this level */
    double LastDequeueTime;
    
    /** Age factor for starvation prevention */
    float AgeFactor;
    
    /** Execution time statistics for performance-based boosting */
    double AverageExecutionTimeMs;
    
    /** Number of samples used for execution time calculation */
    int32 ExecutionTimeSamples;
    
    /** Whether this priority level is critical (immune to throttling) */
    bool bIsCritical;
    
    /** Constructor */
    FPriorityLevel(uint8 InPriority)
        : Priority(InPriority)
        , LastDequeueTime(0.0)
        , AgeFactor(1.0f)
        , AverageExecutionTimeMs(0.0)
        , ExecutionTimeSamples(0)
        , bIsCritical(false)
    {
    }
};

/**
 * Task dependency information for priority inheritance
 */
struct FTaskDependencyInfo
{
    /** Dependencies for each task by ID */
    TMap<uint64, TArray<uint64>> Dependencies;
    
    /** Reverse lookup for dependent tasks */
    TMap<uint64, TArray<uint64>> Dependents;
    
    /** Priority inheritance map */
    TMap<uint64, uint8> InheritedPriorities;
    
    /** Next available task ID */
    FThreadSafeCounter NextTaskId;
    
    /** Gets a new unique task ID */
    uint64 GetNextTaskId() { return NextTaskId.Increment(); }
    
    /** Clears all dependency information */
    void Reset()
    {
        Dependencies.Empty();
        Dependents.Empty();
        InheritedPriorities.Empty();
    }
};

/**
 * Performance feedback data for adaptive scheduling
 */
struct FPerformanceFeedback
{
    /** Recent execution times for different priority levels */
    TMap<uint8, double> RecentExecutionTimes;
    
    /** System load factor (0.0-1.0) */
    float SystemLoadFactor;
    
    /** Time of last performance update */
    double LastUpdateTime;
    
    /** Performance sampling interval in seconds */
    float SamplingIntervalSeconds;
    
    /** Constructor */
    FPerformanceFeedback()
        : SystemLoadFactor(0.0f)
        , LastUpdateTime(0.0)
        , SamplingIntervalSeconds(0.5f)
    {
    }
    
    /** Reset feedback data */
    void Reset()
    {
        RecentExecutionTimes.Empty();
        SystemLoadFactor = 0.0f;
    }
};

/**
 * Priority-based task queue for mining operations
 * Provides priority-based task queuing with starvation prevention
 * and adaptive scheduling based on operation context
 */
class MININGSPICECOPILOT_API FPriorityTaskQueue : public IThreadSafeQueue
{
public:
    /** Constructor */
    FPriorityTaskQueue();
    
    /** Destructor */
    virtual ~FPriorityTaskQueue();

    //~ Begin IThreadSafeQueue Interface
    virtual bool Initialize(int32 InCapacity = 0) override;
    virtual void Shutdown() override;
    virtual bool IsInitialized() const override;
    
    virtual EQueueResult Enqueue(void* Item) override;
    virtual EQueueResult EnqueueWithTimeout(void* Item, uint32 TimeoutMs) override;
    virtual EQueueResult Dequeue(void*& OutItem) override;
    virtual EQueueResult DequeueWithTimeout(void*& OutItem, uint32 TimeoutMs) override;
    virtual EQueueResult Peek(void*& OutItem) const override;
    
    virtual bool IsEmpty() const override;
    virtual bool IsFull() const override;
    virtual int32 GetSize() const override;
    virtual int32 GetCapacity() const override;
    
    virtual void Clear() override;
    virtual FQueueStats GetStats() const override;
    virtual void ResetStats() override;
    
    virtual void Close() override;
    virtual bool IsClosed() const override;
    virtual bool SetCapacity(int32 NewCapacity) override;
    
    virtual int32 EnqueueBatch(void** Items, int32 Count) override;
    virtual int32 DequeueBatch(void** OutItems, int32 MaxCount) override;
    
    static IThreadSafeQueue& Get();
    //~ End IThreadSafeQueue Interface

    /**
     * Enqueues an item with a specific priority
     * @param Item Item to enqueue
     * @param Priority Priority level (higher = more priority)
     * @return Result of the operation
     */
    EQueueResult EnqueueWithPriority(void* Item, uint8 Priority);
    
    /**
     * Enqueues an item with specific priority and task ID for dependency tracking
     * @param Item Item to enqueue
     * @param Priority Priority level (higher = more priority)
     * @param TaskId Unique ID for this task
     * @return Result of the operation
     */
    EQueueResult EnqueueWithPriorityAndId(void* Item, uint8 Priority, uint64 TaskId);
    
    /**
     * Adds a dependency between tasks
     * @param DependentTaskId ID of the dependent task
     * @param DependencyTaskId ID of the task this depends on
     * @return True if dependency was added successfully
     */
    bool AddTaskDependency(uint64 DependentTaskId, uint64 DependencyTaskId);
    
    /**
     * Marks a task as completed, updating dependent tasks
     * @param TaskId ID of the completed task
     * @return True if task was found and processed
     */
    bool CompleteTask(uint64 TaskId);
    
    /**
     * Reports execution time for performance-based priority boosting
     * @param Priority Priority level that task was executed at
     * @param ExecutionTimeMs Time in milliseconds the task took to execute
     */
    void ReportExecutionTime(uint8 Priority, double ExecutionTimeMs);
    
    /**
     * Sets the starvation prevention threshold
     * @param AgeThresholdSeconds Time in seconds after which lower priority tasks get priority boosting
     */
    void SetStarvationThreshold(float AgeThresholdSeconds);
    
    /**
     * Sets priority aging factor
     * @param AgingFactor Factor by which priority increases with age (1.0 = no aging)
     */
    void SetPriorityAgingFactor(float AgingFactor);
    
    /**
     * Sets system load factor for adaptive scheduling
     * @param LoadFactor Load factor between 0.0 (idle) and 1.0 (fully loaded)
     */
    void SetSystemLoadFactor(float LoadFactor);
    
    /**
     * Sets background task throttling
     * @param ThrottlingFactor Factor to throttle background tasks (0.0 = no throttling, 1.0 = maximum throttling)
     */
    void SetBackgroundThrottling(float ThrottlingFactor);
    
    /**
     * Sets a priority level as critical (immune to throttling)
     * @param Priority Priority level to set as critical
     * @param bCritical Whether this priority is critical
     */
    void SetPriorityCritical(uint8 Priority, bool bCritical);
    
    /**
     * Gets statistics for a specific priority level
     * @param Priority Priority level to get statistics for
     * @return Number of items at that priority level
     */
    int32 GetCountAtPriority(uint8 Priority) const;
    
    /**
     * Gets all priority levels in use
     * @return Array of priority levels
     */
    TArray<uint8> GetActivePriorityLevels() const;
    
    /**
     * Generates a new unique task ID for dependency tracking
     * @return Unique task ID
     */
    uint64 GenerateTaskId();
    
    /**
     * Updates starvation prevention logic
     * Called periodically to adjust priority based on waiting time
     */
    void UpdateStarvationPrevention();
    
    /**
     * Updates performance-based priority boosting
     * Called periodically to adjust priorities based on execution statistics
     */
    void UpdatePerformanceBasedBoosting();

private:
    /** Whether the queue has been initialized */
    bool bIsInitialized;
    
    /** Whether the queue has been closed */
    FThreadSafeCounter bClosed;
    
    /** Maximum capacity (0 for unlimited) */
    int32 Capacity;
    
    /** Total size across all priority levels */
    FThreadSafeCounter TotalSize;
    
    /** Priority levels map */
    TMap<uint8, FPriorityLevel*> PriorityLevels;
    
    /** Lock for queue operations */
    mutable FCriticalSection QueueLock;
    
    /** Starvation prevention threshold in seconds */
    float StarvationThresholdSeconds;
    
    /** Priority aging factor */
    float PriorityAgingFactor;
    
    /** Last time starvation prevention was updated */
    double LastStarvationUpdateTime;
    
    /** Last time performance-based boosting was updated */
    double LastPerformanceUpdateTime;
    
    /** Performance update interval in seconds */
    float PerformanceUpdateIntervalSeconds;
    
    /** Background task throttling factor */
    float BackgroundThrottlingFactor;
    
    /** Task dependency information */
    FTaskDependencyInfo DependencyInfo;
    
    /** Performance feedback for adaptive scheduling */
    FPerformanceFeedback PerformanceFeedback;
    
    /** Queue statistics */
    mutable FCriticalSection StatsLock;
    uint64 TotalEnqueued;
    uint64 TotalDequeued;
    uint64 EnqueueFailures;
    uint64 DequeueFailures;
    uint64 TimeoutCount;
    int32 PeakSize;
    double TotalEnqueueWaitTimeMs;
    double TotalDequeueWaitTimeMs;
    uint64 EnqueueWaitCount;
    uint64 DequeueWaitCount;
    
    /** Updates statistics after an enqueue operation */
    void UpdateEnqueueStats(bool bSuccess, double WaitTimeMs = 0.0);
    
    /** Updates statistics after a dequeue operation */
    void UpdateDequeueStats(bool bSuccess, double WaitTimeMs = 0.0);
    
    /** Gets or creates a priority level */
    FPriorityLevel* GetOrCreatePriorityLevel(uint8 Priority);
    
    /** Selects the next priority level based on priority, age, and performance data */
    FPriorityLevel* SelectNextPriorityLevel() const;
    
    /** Applies priority inheritance based on task dependencies */
    void ApplyPriorityInheritance(uint64 TaskId, uint8 Priority);
    
    /** Updates priority based on task dependencies when a dependency completes */
    void UpdateDependentTaskPriorities(uint64 CompletedTaskId);
    
    /** Waits until the queue is not full or timeout */
    bool WaitNotFull(uint32 TimeoutMs, double& OutWaitTimeMs);
    
    /** Waits until the queue is not empty or timeout */
    bool WaitNotEmpty(uint32 TimeoutMs, double& OutWaitTimeMs);

    /** Singleton instance */
    static FPriorityTaskQueue* Instance;
};