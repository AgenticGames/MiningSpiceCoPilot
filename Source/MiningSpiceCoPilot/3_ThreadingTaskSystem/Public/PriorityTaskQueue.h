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
    
    /** Constructor */
    FPriorityLevel(uint8 InPriority)
        : Priority(InPriority)
        , LastDequeueTime(0.0)
        , AgeFactor(1.0f)
    {
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
    
    static IThreadSafeQueue& Get() override;
    //~ End IThreadSafeQueue Interface

    /**
     * Enqueues an item with a specific priority
     * @param Item Item to enqueue
     * @param Priority Priority level (higher = more priority)
     * @return Result of the operation
     */
    EQueueResult EnqueueWithPriority(void* Item, uint8 Priority);
    
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
     * Updates starvation prevention logic
     * Called periodically to adjust priority based on waiting time
     */
    void UpdateStarvationPrevention();

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
    
    /** Selects the next priority level based on priority and age */
    FPriorityLevel* SelectNextPriorityLevel() const;
    
    /** Waits until the queue is not full or timeout */
    bool WaitNotFull(uint32 TimeoutMs, double& OutWaitTimeMs);
    
    /** Waits until the queue is not empty or timeout */
    bool WaitNotEmpty(uint32 TimeoutMs, double& OutWaitTimeMs);

    /** Singleton instance */
    static FPriorityTaskQueue* Instance;
};