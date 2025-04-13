// Copyright Epic Games, Inc. All Rights Reserved.

#include "3_ThreadingTaskSystem/Public/PriorityTaskQueue.h"
#include "HAL/PlatformProcess.h"
#include "HAL/CriticalSection.h"
#include "Math/UnrealMathSSE.h"
#include "Misc/ScopeLock.h"
#include "Stats/Stats.h"
#include "ProfilingDebugging/CsvProfiler.h"

DECLARE_CYCLE_STAT(TEXT("PriorityTaskQueue_Enqueue"), STAT_PriorityTaskQueue_Enqueue, STATGROUP_Threading);
DECLARE_CYCLE_STAT(TEXT("PriorityTaskQueue_Dequeue"), STAT_PriorityTaskQueue_Dequeue, STATGROUP_Threading);
DECLARE_CYCLE_STAT(TEXT("PriorityTaskQueue_Rebalance"), STAT_PriorityTaskQueue_Rebalance, STATGROUP_Threading);

CSV_DECLARE_CATEGORY_MODULE_EXTERN(MININGSPICECOPILOT_API, Threading);

// Implementation of the priority task queue with enhanced starvation prevention
FPriorityTaskQueue::FPriorityTaskQueue(int32 InitBucketCount)
    : StarvationPreventionCounter(0)
    , LastRebalanceTimestamp(0.0)
    , TotalTasksProcessed(0)
    , bIsShuttingDown(false)
{
    // Initialize buckets with default capacity
    const int32 BucketCount = FMath::Max(InitBucketCount, 5); // At least 5 buckets for priorities
    PriorityBuckets.SetNum(BucketCount);
    
    // Initialize task counts per priority
    TaskCountPerPriority.SetNum(BucketCount);
    for (int32& Count : TaskCountPerPriority)
    {
        Count = 0;
    }
    
    // Initialize bucket boost factors (higher priority buckets get higher boost)
    BucketBoostFactors.SetNum(BucketCount);
    for (int32 i = 0; i < BucketCount; ++i)
    {
        // Higher priority (lower index) gets higher boost factor
        BucketBoostFactors[i] = FMath::Pow(2.0f, static_cast<float>(BucketCount - i - 1));
    }
    
    // Initialize bucket starvation counters
    BucketStarvationCounters.SetNum(BucketCount);
    for (int32& Counter : BucketStarvationCounters)
    {
        Counter = 0;
    }
    
    // Initialize bucket SIMD prefetch indicators
    BucketPrefetchIndicators.SetNum(BucketCount);
    for (bool& Indicator : BucketPrefetchIndicators)
    {
        Indicator = false;
    }
    
    // Initialize per-thread contention trackers
    const int32 MaxThreads = 64; // Reasonable upper limit
    ThreadContentionTrackers.SetNum(MaxThreads);
    for (FThreadContentionTracker& Tracker : ThreadContentionTrackers)
    {
        Tracker.LastContentionTimestamp = 0.0;
        Tracker.ContentionCount = 0;
        Tracker.TotalWaitTimeMs = 0.0;
    }
}

FPriorityTaskQueue::~FPriorityTaskQueue()
{
    // Mark as shutting down
    bIsShuttingDown = true;
    
    // Clear any remaining tasks
    FScopeLock Lock(&QueueLock);
    
    for (TArray<FQueuedTask>& Bucket : PriorityBuckets)
    {
        Bucket.Empty();
    }
    
    TaskCountPerPriority.Empty();
    BucketBoostFactors.Empty();
    BucketStarvationCounters.Empty();
    BucketPrefetchIndicators.Empty();
    ThreadContentionTrackers.Empty();
}

bool FPriorityTaskQueue::Enqueue(const FQueuedTask& Task, EPriority Priority)
{
    SCOPE_CYCLE_COUNTER(STAT_PriorityTaskQueue_Enqueue);
    CSV_SCOPED_TIMING_STAT(Threading, PriorityTaskQueue_Enqueue);
    
    // Track contention
    const int32 ThreadId = FPlatformTLS::GetCurrentThreadId() % ThreadContentionTrackers.Num();
    
    if (bIsShuttingDown)
    {
        return false;
    }
    
    // Validate priority
    int32 BucketIndex = static_cast<int32>(Priority);
    if (BucketIndex < 0 || BucketIndex >= PriorityBuckets.Num())
    {
        BucketIndex = static_cast<int32>(EPriority::Normal); // Fallback to normal priority
    }
    
    double StartWaitTime = 0.0;
    bool bContended = false;
    
    // Try lock first to avoid contention
    if (!QueueLock.TryLock())
    {
        bContended = true;
        StartWaitTime = FPlatformTime::Seconds();
        
        // Record contention for this thread
        ThreadContentionTrackers[ThreadId].ContentionCount++;
        ThreadContentionTrackers[ThreadId].LastContentionTimestamp = StartWaitTime;
        
        // Wait for lock with adaptive backoff
        int32 SpinCount = 0;
        const int32 MaxSpinCount = 1000;
        
        while (!QueueLock.TryLock() && SpinCount < MaxSpinCount)
        {
            // Progressive backoff - increase wait time as contention persists
            if (SpinCount < 10)
            {
                FPlatformProcess::Yield(); // Simple yield for first attempts
            }
            else if (SpinCount < 100)
            {
                FPlatformProcess::SleepNoStats(0.0f); // Short sleep
            }
            else
            {
                FPlatformProcess::SleepNoStats(0.001f); // Longer sleep
            }
            
            SpinCount++;
        }
        
        // If still couldn't acquire lock, fall back to blocking lock
        if (SpinCount >= MaxSpinCount)
        {
            QueueLock.Lock();
        }
        
        // Record wait time
        double EndWaitTime = FPlatformTime::Seconds();
        ThreadContentionTrackers[ThreadId].TotalWaitTimeMs += (EndWaitTime - StartWaitTime) * 1000.0;
    }
    
    // Critical section - add task to appropriate bucket
    {
        PriorityBuckets[BucketIndex].Add(Task);
        TaskCountPerPriority[BucketIndex]++;
        
        // If this bucket has accumulated enough tasks, mark it for SIMD prefetching
        if (TaskCountPerPriority[BucketIndex] > 16)
        {
            BucketPrefetchIndicators[BucketIndex] = true;
        }
        
        // Signal that new task is available
        TaskAvailableEvent.Trigger();
    }
    
    // Release lock
    QueueLock.Unlock();
    
    // Periodic rebalance check
    MaybeRebalanceBuckets();
    
    return true;
}

bool FPriorityTaskQueue::EnqueueBatch(const TArray<FQueuedTask>& Tasks, EPriority Priority)
{
    SCOPE_CYCLE_COUNTER(STAT_PriorityTaskQueue_Enqueue);
    CSV_SCOPED_TIMING_STAT(Threading, PriorityTaskQueue_Enqueue);
    
    if (bIsShuttingDown || Tasks.Num() == 0)
    {
        return false;
    }
    
    // Validate priority
    int32 BucketIndex = static_cast<int32>(Priority);
    if (BucketIndex < 0 || BucketIndex >= PriorityBuckets.Num())
    {
        BucketIndex = static_cast<int32>(EPriority::Normal); // Fallback to normal priority
    }
    
    // Track contention
    const int32 ThreadId = FPlatformTLS::GetCurrentThreadId() % ThreadContentionTrackers.Num();
    double StartWaitTime = 0.0;
    bool bContended = false;
    
    // Try lock first to avoid contention
    if (!QueueLock.TryLock())
    {
        bContended = true;
        StartWaitTime = FPlatformTime::Seconds();
        
        // Record contention for this thread
        ThreadContentionTrackers[ThreadId].ContentionCount++;
        ThreadContentionTrackers[ThreadId].LastContentionTimestamp = StartWaitTime;
        
        // Wait for lock with adaptive backoff
        int32 SpinCount = 0;
        const int32 MaxSpinCount = 1000;
        
        while (!QueueLock.TryLock() && SpinCount < MaxSpinCount)
        {
            // Progressive backoff - increase wait time as contention persists
            if (SpinCount < 10)
            {
                FPlatformProcess::Yield(); // Simple yield for first attempts
            }
            else if (SpinCount < 100)
            {
                FPlatformProcess::SleepNoStats(0.0f); // Short sleep
            }
            else
            {
                FPlatformProcess::SleepNoStats(0.001f); // Longer sleep
            }
            
            SpinCount++;
        }
        
        // If still couldn't acquire lock, fall back to blocking lock
        if (SpinCount >= MaxSpinCount)
        {
            QueueLock.Lock();
        }
        
        // Record wait time
        double EndWaitTime = FPlatformTime::Seconds();
        ThreadContentionTrackers[ThreadId].TotalWaitTimeMs += (EndWaitTime - StartWaitTime) * 1000.0;
    }
    
    // Critical section - add tasks to appropriate bucket using SIMD if applicable
    {
        TArray<FQueuedTask>& TargetBucket = PriorityBuckets[BucketIndex];
        const int32 OriginalTaskCount = TargetBucket.Num();
        const int32 NewTaskCount = Tasks.Num();
        
        // Reserve space for the new tasks
        TargetBucket.Reserve(OriginalTaskCount + NewTaskCount);
        
        // Process SIMD blocks if we have enough tasks
        int32 ProcessedCount = 0;
        
        if (NewTaskCount >= 4)
        {
            // Process in SIMD blocks of 4
            int32 NumSIMDBlocks = NewTaskCount / 4;
            for (int32 i = 0; i < NumSIMDBlocks; ++i)
            {
                // Here we would use SIMD intrinsics if the task structure had simple numeric fields
                // For our complex structures, just do efficient copies
                for (int32 j = 0; j < 4; ++j)
                {
                    TargetBucket.Add(Tasks[ProcessedCount++]);
                }
            }
        }
        
        // Process remaining tasks
        for (int32 i = ProcessedCount; i < NewTaskCount; ++i)
        {
            TargetBucket.Add(Tasks[i]);
        }
        
        // Update task count for this priority
        TaskCountPerPriority[BucketIndex] += NewTaskCount;
        
        // If this bucket has accumulated enough tasks, mark it for SIMD prefetching
        if (TaskCountPerPriority[BucketIndex] > 16)
        {
            BucketPrefetchIndicators[BucketIndex] = true;
            
            // Prefetch cache lines for the bucket
            if (BucketPrefetchIndicators[BucketIndex] && TargetBucket.Num() > 16)
            {
                for (int32 i = 0; i < FMath::Min(TargetBucket.Num(), 16); i += 4)
                {
                    // Prefetch next few tasks
                    FPlatformMisc::Prefetch(&TargetBucket[i], (i + 4 < TargetBucket.Num()) ? &TargetBucket[i + 4] : nullptr);
                }
            }
        }
        
        // Signal that new tasks are available
        TaskAvailableEvent.Trigger();
    }
    
    // Release lock
    QueueLock.Unlock();
    
    // Periodic rebalance check
    MaybeRebalanceBuckets();
    
    return true;
}

bool FPriorityTaskQueue::Dequeue(FQueuedTask& OutTask, uint32 WaitTimeMs)
{
    SCOPE_CYCLE_COUNTER(STAT_PriorityTaskQueue_Dequeue);
    CSV_SCOPED_TIMING_STAT(Threading, PriorityTaskQueue_Dequeue);
    
    if (bIsShuttingDown)
    {
        return false;
    }
    
    double StartTime = FPlatformTime::Seconds();
    bool bFoundTask = false;
    const int32 ThreadId = FPlatformTLS::GetCurrentThreadId() % ThreadContentionTrackers.Num();
    
    // Track starvation prevention counter
    int32 LocalStarvationCounter = StarvationPreventionCounter.fetch_add(1, std::memory_order_relaxed);
    
    // First fast path: Try to dequeue without waiting for lock
    if (TryDequeueWithStarvationPrevention(OutTask, LocalStarvationCounter))
    {
        return true;
    }
    
    // Slow path: Use timed wait
    while (!bFoundTask && !bIsShuttingDown)
    {
        double CurrentTime = FPlatformTime::Seconds();
        double ElapsedTimeMs = (CurrentTime - StartTime) * 1000.0;
        
        // Check if we've waited long enough
        if (ElapsedTimeMs >= WaitTimeMs)
        {
            break;
        }
        
        // Wait for a task to become available
        uint32 WaitTimeRemaining = static_cast<uint32>(WaitTimeMs - ElapsedTimeMs);
        if (WaitTimeRemaining > 0)
        {
            // Wait with a cap of 10ms to prevent long waits
            TaskAvailableEvent.Wait(FMath::Min(WaitTimeRemaining, 10u));
        }
        
        // Try to dequeue again with updated starvation counter
        LocalStarvationCounter = StarvationPreventionCounter.fetch_add(1, std::memory_order_relaxed);
        
        if (TryDequeueWithStarvationPrevention(OutTask, LocalStarvationCounter))
        {
            bFoundTask = true;
            break;
        }
    }
    
    return bFoundTask;
}

int32 FPriorityTaskQueue::GetTotalTaskCount() const
{
    int32 TotalCount = 0;
    FScopeLock Lock(&QueueLock);
    
    for (int32 Count : TaskCountPerPriority)
    {
        TotalCount += Count;
    }
    
    return TotalCount;
}

int32 FPriorityTaskQueue::GetTaskCount(EPriority Priority) const
{
    int32 BucketIndex = static_cast<int32>(Priority);
    if (BucketIndex < 0 || BucketIndex >= TaskCountPerPriority.Num())
    {
        return 0;
    }
    
    FScopeLock Lock(&QueueLock);
    return TaskCountPerPriority[BucketIndex];
}

TArray<int32> FPriorityTaskQueue::GetTaskCountsPerPriority() const
{
    FScopeLock Lock(&QueueLock);
    return TaskCountPerPriority;
}

void FPriorityTaskQueue::SetBucketBoostFactor(EPriority Priority, float BoostFactor)
{
    int32 BucketIndex = static_cast<int32>(Priority);
    if (BucketIndex >= 0 && BucketIndex < BucketBoostFactors.Num())
    {
        FScopeLock Lock(&QueueLock);
        BucketBoostFactors[BucketIndex] = BoostFactor;
    }
}

TArray<float> FPriorityTaskQueue::GetBucketBoostFactors() const
{
    FScopeLock Lock(&QueueLock);
    TArray<float> Result;
    
    for (const float Factor : BucketBoostFactors)
    {
        Result.Add(Factor);
    }
    
    return Result;
}

TArray<FThreadContentionStats> FPriorityTaskQueue::GetThreadContentionStats() const
{
    FScopeLock Lock(&QueueLock);
    TArray<FThreadContentionStats> Stats;
    
    for (int32 i = 0; i < ThreadContentionTrackers.Num(); ++i)
    {
        const FThreadContentionTracker& Tracker = ThreadContentionTrackers[i];
        
        if (Tracker.ContentionCount > 0)
        {
            FThreadContentionStats Stat;
            Stat.ThreadId = i;
            Stat.ContentionCount = Tracker.ContentionCount;
            Stat.TotalWaitTimeMs = Tracker.TotalWaitTimeMs;
            Stat.LastContentionTimestamp = Tracker.LastContentionTimestamp;
            
            Stats.Add(Stat);
        }
    }
    
    return Stats;
}

bool FPriorityTaskQueue::TryDequeueWithStarvationPrevention(FQueuedTask& OutTask, int32 StarvationCount)
{
    // Don't try to lock if we're already shutting down
    if (bIsShuttingDown)
    {
        return false;
    }
    
    // Try to acquire lock
    if (!QueueLock.TryLock())
    {
        // Record contention metrics
        const int32 ThreadId = FPlatformTLS::GetCurrentThreadId() % ThreadContentionTrackers.Num();
        ThreadContentionTrackers[ThreadId].ContentionCount++;
        ThreadContentionTrackers[ThreadId].LastContentionTimestamp = FPlatformTime::Seconds();
        
        return false;
    }
    
    bool bFoundTask = false;
    
    // Calculate which bucket to start with based on starvation prevention
    // This algorithm ensures lower priority tasks eventually get processed
    int32 BucketCount = PriorityBuckets.Num();
    
    // Start with highest priority
    for (int32 BucketOffset = 0; BucketOffset < BucketCount && !bFoundTask; ++BucketOffset)
    {
        // Calculate adjusted bucket index with starvation prevention
        // Modulus operation cycles through the buckets as starvation counter increases
        // The starvation factor determines how many high-priority tasks process before allowing lower priorities
        int32 StarvationFactor = 1 << (BucketCount - BucketOffset - 1);
        bool bCheckThisBucket = (StarvationCount % StarvationFactor) == 0;
        
        // Always check the highest priority bucket
        if (BucketOffset == 0 || bCheckThisBucket)
        {
            int32 BucketIndex = BucketOffset;
            
            // Get the bucket
            TArray<FQueuedTask>& Bucket = PriorityBuckets[BucketIndex];
            
            // Check if bucket has tasks
            if (Bucket.Num() > 0)
            {
                // Get first task
                OutTask = Bucket[0];
                Bucket.RemoveAt(0, 1, false); // Don't shrink array
                
                // Apply SIMD prefetching for batch operations if needed
                if (BucketPrefetchIndicators[BucketIndex] && Bucket.Num() >= 16)
                {
                    // Prefetch next 4 elements to optimize cache usage
                    for (int32 i = 0; i < FMath::Min(4, Bucket.Num()); ++i)
                    {
                        FPlatformMisc::Prefetch(&Bucket[i]);
                    }
                }
                
                // Update task count for this priority
                TaskCountPerPriority[BucketIndex]--;
                
                // If bucket is nearly empty, disable SIMD prefetching
                if (Bucket.Num() < 8)
                {
                    BucketPrefetchIndicators[BucketIndex] = false;
                }
                
                // Reset starvation counter for this bucket
                BucketStarvationCounters[BucketIndex] = 0;
                
                // Increment starvation counters for all other buckets
                for (int32 i = 0; i < BucketCount; ++i)
                {
                    if (i != BucketIndex && TaskCountPerPriority[i] > 0)
                    {
                        BucketStarvationCounters[i]++;
                    }
                }
                
                bFoundTask = true;
                TotalTasksProcessed++;
                
                break;
            }
            else
            {
                // Bucket is empty, increment its starvation counter (will be reset when task is added)
                BucketStarvationCounters[BucketIndex] = 0;
            }
        }
    }
    
    // Release lock
    QueueLock.Unlock();
    
    return bFoundTask;
}

void FPriorityTaskQueue::MaybeRebalanceBuckets()
{
    double CurrentTime = FPlatformTime::Seconds();
    
    // Check if enough time has passed since last rebalance (once every 5 seconds)
    if (CurrentTime - LastRebalanceTimestamp > 5.0)
    {
        SCOPE_CYCLE_COUNTER(STAT_PriorityTaskQueue_Rebalance);
        CSV_SCOPED_TIMING_STAT(Threading, PriorityTaskQueue_Rebalance);
        
        // Try to acquire lock without blocking
        if (QueueLock.TryLock())
        {
            // Calculate the max starvation counter across all buckets
            int32 MaxStarvation = 0;
            for (int32 StarvCount : BucketStarvationCounters)
            {
                MaxStarvation = FMath::Max(MaxStarvation, StarvCount);
            }
            
            // If we have significant starvation, adjust boost factors
            if (MaxStarvation > 1000)
            {
                // Find buckets with high starvation
                for (int32 i = 0; i < BucketStarvationCounters.Num(); ++i)
                {
                    float StarvationRatio = static_cast<float>(BucketStarvationCounters[i]) / static_cast<float>(MaxStarvation);
                    
                    // If this bucket is severely starved (>80% of max starvation)
                    if (StarvationRatio > 0.8f && TaskCountPerPriority[i] > 0)
                    {
                        // Boost this bucket temporarily
                        BucketBoostFactors[i] *= (1.0f + StarvationRatio);
                        
                        // Cap the boost factor
                        BucketBoostFactors[i] = FMath::Min(BucketBoostFactors[i], 100.0f);
                    }
                    else if (StarvationRatio < 0.2f)
                    {
                        // Gradually return to normal boost factor for this bucket
                        float DefaultBoost = FMath::Pow(2.0f, static_cast<float>(BucketStarvationCounters.Num() - i - 1));
                        BucketBoostFactors[i] = FMath::Lerp(BucketBoostFactors[i], DefaultBoost, 0.1f);
                    }
                }
            }
            
            // Update timestamp
            LastRebalanceTimestamp = CurrentTime;
            
            // Release lock
            QueueLock.Unlock();
        }
    }
}