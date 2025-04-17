// Copyright Epic Games, Inc. All Rights Reserved.

#include "PriorityTaskQueue.h"
#include "HAL/PlatformProcess.h"
#include "HAL/CriticalSection.h"
#include "HAL/PlatformTime.h"
#include "Math/UnrealMathSSE.h"
#include "Misc/ScopeLock.h"
#include "Stats/Stats.h"
#include "ProfilingDebugging/CsvProfiler.h"

DECLARE_CYCLE_STAT(TEXT("PriorityTaskQueue_Enqueue"), STAT_PriorityTaskQueue_Enqueue, STATGROUP_Threading);
DECLARE_CYCLE_STAT(TEXT("PriorityTaskQueue_Dequeue"), STAT_PriorityTaskQueue_Dequeue, STATGROUP_Threading);
DECLARE_CYCLE_STAT(TEXT("PriorityTaskQueue_Rebalance"), STAT_PriorityTaskQueue_Rebalance, STATGROUP_Threading);
DECLARE_CYCLE_STAT(TEXT("PriorityTaskQueue_PriorityInheritance"), STAT_PriorityTaskQueue_PriorityInheritance, STATGROUP_Threading);
DECLARE_CYCLE_STAT(TEXT("PriorityTaskQueue_PerformanceBoost"), STAT_PriorityTaskQueue_PerformanceBoost, STATGROUP_Threading);

CSV_DECLARE_CATEGORY_MODULE_EXTERN(MININGSPICECOPILOT_API, Threading);

// Static singleton instance
FPriorityTaskQueue* FPriorityTaskQueue::Instance = nullptr;

FPriorityTaskQueue::FPriorityTaskQueue()
    : bIsInitialized(false)
    , Capacity(0)
    , StarvationThresholdSeconds(5.0f)
    , PriorityAgingFactor(1.05f)
    , LastStarvationUpdateTime(0.0)
    , LastPerformanceUpdateTime(0.0)
    , PerformanceUpdateIntervalSeconds(1.0f)
    , BackgroundThrottlingFactor(0.0f)
    , TotalEnqueued(0)
    , TotalDequeued(0)
    , EnqueueFailures(0)
    , DequeueFailures(0)
    , TimeoutCount(0)
    , PeakSize(0)
    , TotalEnqueueWaitTimeMs(0.0)
    , TotalDequeueWaitTimeMs(0.0)
    , EnqueueWaitCount(0)
    , DequeueWaitCount(0)
{
    // Initialize atomic counter
    TotalSize.Set(0);
    bClosed.Set(0);
    
    // Initialize performance feedback
    PerformanceFeedback.SystemLoadFactor = 0.0f;
    PerformanceFeedback.LastUpdateTime = FPlatformTime::Seconds();
}

FPriorityTaskQueue::~FPriorityTaskQueue()
{
    Shutdown();
}

bool FPriorityTaskQueue::Initialize(int32 InCapacity)
{
    if (bIsInitialized)
    {
        return true;
    }
    
    Capacity = InCapacity;
    
    // Create default priority levels
    FScopeLock Lock(&QueueLock);
    
    // System-critical priority (255) - highest priority for system-critical operations
    FPriorityLevel* CriticalLevel = GetOrCreatePriorityLevel(255);
    CriticalLevel->bIsCritical = true;
    
    // High priority (192) - for player-facing operations
    GetOrCreatePriorityLevel(192);
    
    // Normal priority (128) - default for most operations
    GetOrCreatePriorityLevel(128);
    
    // Low priority (64) - background operations
    GetOrCreatePriorityLevel(64);
    
    // Background priority (32) - very low priority, heavily throttled under load
    GetOrCreatePriorityLevel(32);
    
    // Initialize timestamps
    LastStarvationUpdateTime = FPlatformTime::Seconds();
    LastPerformanceUpdateTime = FPlatformTime::Seconds();
    
    bIsInitialized = true;
    return true;
}

void FPriorityTaskQueue::Shutdown()
{
    if (!bIsInitialized)
    {
        return;
    }
    
    // Mark queue as closed to prevent new operations
    Close();
    
    // Clear all priority levels
    FScopeLock Lock(&QueueLock);
    
    for (auto& Pair : PriorityLevels)
    {
        delete Pair.Value;
    }
    
    PriorityLevels.Empty();
    TotalSize.Set(0);
    
    // Clear dependency and performance data
    DependencyInfo.Reset();
    PerformanceFeedback.Reset();
    
    bIsInitialized = false;
}

bool FPriorityTaskQueue::IsInitialized() const
{
    return bIsInitialized;
}

EQueueResult FPriorityTaskQueue::Enqueue(void* Item)
{
    // Default to normal priority (128)
    return EnqueueWithPriority(Item, 128);
}

EQueueResult FPriorityTaskQueue::EnqueueWithTimeout(void* Item, uint32 TimeoutMs)
{
    if (!bIsInitialized)
    {
        return EQueueResult::Error;
    }
    
    if (bClosed.GetValue() != 0)
    {
        return EQueueResult::QueueClosed;
    }
    
    // If queue is full, wait until not full or timeout
    double WaitTimeMs = 0.0;
    if (Capacity > 0 && TotalSize.GetValue() >= Capacity)
    {
        // Wait for space or timeout
        if (!WaitNotFull(TimeoutMs, WaitTimeMs))
        {
            UpdateEnqueueStats(false, WaitTimeMs);
            TimeoutCount++;
            return EQueueResult::Timeout;
        }
    }
    
    // Default to normal priority (128)
    FScopeLock Lock(&QueueLock);
    
    FPriorityLevel* Level = GetOrCreatePriorityLevel(128);
    Level->Tasks.Add(Item);
    Level->Count.Increment();
    TotalSize.Increment();
    
    UpdateEnqueueStats(true, WaitTimeMs);
    
    return EQueueResult::Success;
}

EQueueResult FPriorityTaskQueue::EnqueueWithPriority(void* Item, uint8 Priority)
{
    if (!bIsInitialized)
    {
        return EQueueResult::Error;
    }
    
    if (bClosed.GetValue() != 0)
    {
        return EQueueResult::QueueClosed;
    }
    
    if (Capacity > 0 && TotalSize.GetValue() >= Capacity)
    {
        UpdateEnqueueStats(false);
        return EQueueResult::QueueFull;
    }
    
    FScopeLock Lock(&QueueLock);
    
    FPriorityLevel* Level = GetOrCreatePriorityLevel(Priority);
    Level->Tasks.Add(Item);
    Level->Count.Increment();
    TotalSize.Increment();
    
    UpdateEnqueueStats(true);
    
    return EQueueResult::Success;
}

EQueueResult FPriorityTaskQueue::EnqueueWithPriorityAndId(void* Item, uint8 Priority, uint64 TaskId)
{
    SCOPE_CYCLE_COUNTER(STAT_PriorityTaskQueue_Enqueue);
    
    if (!bIsInitialized)
    {
        return EQueueResult::Error;
    }
    
    if (bClosed.GetValue() != 0)
    {
        return EQueueResult::QueueClosed;
    }
    
    if (Capacity > 0 && TotalSize.GetValue() >= Capacity)
    {
        UpdateEnqueueStats(false);
        return EQueueResult::QueueFull;
    }
    
    FScopeLock Lock(&QueueLock);
    
    // Apply priority inheritance from dependencies
    ApplyPriorityInheritance(TaskId, Priority);
    
    FPriorityLevel* Level = GetOrCreatePriorityLevel(Priority);
    Level->Tasks.Add(Item);
    Level->Count.Increment();
    TotalSize.Increment();
    
    UpdateEnqueueStats(true);
    
    return EQueueResult::Success;
}

EQueueResult FPriorityTaskQueue::Dequeue(void*& OutItem)
{
    SCOPE_CYCLE_COUNTER(STAT_PriorityTaskQueue_Dequeue);
    
    if (!bIsInitialized)
    {
        return EQueueResult::Error;
    }
    
    if (TotalSize.GetValue() == 0)
    {
        UpdateDequeueStats(false);
        return EQueueResult::QueueEmpty;
    }
    
    // Check for starvation and performance-based boosting periodically
    double CurrentTime = FPlatformTime::Seconds();
    
    if (CurrentTime - LastStarvationUpdateTime > 0.5)
    {
        UpdateStarvationPrevention();
        LastStarvationUpdateTime = CurrentTime;
    }
    
    if (CurrentTime - LastPerformanceUpdateTime > PerformanceUpdateIntervalSeconds)
    {
        UpdatePerformanceBasedBoosting();
        LastPerformanceUpdateTime = CurrentTime;
    }
    
    FScopeLock Lock(&QueueLock);
    
    // Select next priority level based on priority, age, and performance data
    FPriorityLevel* Level = SelectNextPriorityLevel();
    if (!Level || Level->Tasks.Num() == 0)
    {
        UpdateDequeueStats(false);
        return EQueueResult::QueueEmpty;
    }
    
    // Get item from the level
    OutItem = Level->Tasks[0];
    Level->Tasks.RemoveAt(0);
    Level->Count.Decrement();
    Level->LastDequeueTime = FPlatformTime::Seconds();
    TotalSize.Decrement();
    
    UpdateDequeueStats(true);
    
    return EQueueResult::Success;
}

EQueueResult FPriorityTaskQueue::DequeueWithTimeout(void*& OutItem, uint32 TimeoutMs)
{
    SCOPE_CYCLE_COUNTER(STAT_PriorityTaskQueue_Dequeue);
    
    if (!bIsInitialized)
    {
        return EQueueResult::Error;
    }
    
    // If queue is empty, wait until not empty or timeout
    double WaitTimeMs = 0.0;
    if (TotalSize.GetValue() == 0)
    {
        // Wait for item or timeout
        if (!WaitNotEmpty(TimeoutMs, WaitTimeMs))
        {
            UpdateDequeueStats(false, WaitTimeMs);
            TimeoutCount++;
            return EQueueResult::Timeout;
        }
    }
    
    // Check for starvation and performance-based boosting periodically
    double CurrentTime = FPlatformTime::Seconds();
    
    if (CurrentTime - LastStarvationUpdateTime > 0.5)
    {
        UpdateStarvationPrevention();
        LastStarvationUpdateTime = CurrentTime;
    }
    
    if (CurrentTime - LastPerformanceUpdateTime > PerformanceUpdateIntervalSeconds)
    {
        UpdatePerformanceBasedBoosting();
        LastPerformanceUpdateTime = CurrentTime;
    }
    
    FScopeLock Lock(&QueueLock);
    
    // Select next priority level based on priority, age, and performance data
    FPriorityLevel* Level = SelectNextPriorityLevel();
    if (!Level || Level->Tasks.Num() == 0)
    {
        UpdateDequeueStats(false, WaitTimeMs);
        return EQueueResult::QueueEmpty;
    }
    
    // Get item from the level
    OutItem = Level->Tasks[0];
    Level->Tasks.RemoveAt(0);
    Level->Count.Decrement();
    Level->LastDequeueTime = FPlatformTime::Seconds();
    TotalSize.Decrement();
    
    UpdateDequeueStats(true, WaitTimeMs);
    
    return EQueueResult::Success;
}

EQueueResult FPriorityTaskQueue::Peek(void*& OutItem) const
{
    if (!bIsInitialized)
    {
        return EQueueResult::Error;
    }
    
    if (TotalSize.GetValue() == 0)
    {
        return EQueueResult::QueueEmpty;
    }
    
    FScopeLock Lock(&QueueLock);
    
    // Select next priority level based on priority and age
    FPriorityLevel* Level = SelectNextPriorityLevel();
    if (!Level || Level->Tasks.Num() == 0)
    {
        return EQueueResult::QueueEmpty;
    }
    
    // Get item from the level without removing it
    OutItem = Level->Tasks[0];
    
    return EQueueResult::Success;
}

bool FPriorityTaskQueue::IsEmpty() const
{
    return TotalSize.GetValue() == 0;
}

bool FPriorityTaskQueue::IsFull() const
{
    return Capacity > 0 && TotalSize.GetValue() >= Capacity;
}

int32 FPriorityTaskQueue::GetSize() const
{
    return TotalSize.GetValue();
}

int32 FPriorityTaskQueue::GetCapacity() const
{
    return Capacity;
}

void FPriorityTaskQueue::Clear()
{
    if (!bIsInitialized)
    {
        return;
    }
    
    FScopeLock Lock(&QueueLock);
    
    for (auto& Pair : PriorityLevels)
    {
        FPriorityLevel* Level = Pair.Value;
        Level->Tasks.Empty();
        Level->Count.Set(0);
    }
    
    TotalSize.Set(0);
    
    // Also clear dependency info
    DependencyInfo.Reset();
}

FQueueStats FPriorityTaskQueue::GetStats() const
{
    FScopeLock Lock(&StatsLock);
    
    FQueueStats Stats;
    Stats.CurrentSize = TotalSize.GetValue();
    Stats.Capacity = Capacity;
    Stats.TotalEnqueued = TotalEnqueued;
    Stats.TotalDequeued = TotalDequeued;
    Stats.EnqueueFailures = EnqueueFailures;
    Stats.DequeueFailures = DequeueFailures;
    Stats.TimeoutCount = TimeoutCount;
    Stats.PeakSize = PeakSize;
    Stats.AverageEnqueueWaitTimeMs = EnqueueWaitCount > 0 ? TotalEnqueueWaitTimeMs / EnqueueWaitCount : 0.0;
    Stats.AverageDequeueWaitTimeMs = DequeueWaitCount > 0 ? TotalDequeueWaitTimeMs / DequeueWaitCount : 0.0;
    Stats.bIsClosed = bClosed.GetValue() != 0;
    
    return Stats;
}

void FPriorityTaskQueue::ResetStats()
{
    FScopeLock Lock(&StatsLock);
    
    TotalEnqueued = 0;
    TotalDequeued = 0;
    EnqueueFailures = 0;
    DequeueFailures = 0;
    TimeoutCount = 0;
    PeakSize = TotalSize.GetValue();
    TotalEnqueueWaitTimeMs = 0.0;
    TotalDequeueWaitTimeMs = 0.0;
    EnqueueWaitCount = 0;
    DequeueWaitCount = 0;
}

void FPriorityTaskQueue::Close()
{
    bClosed.Set(1);
}

bool FPriorityTaskQueue::IsClosed() const
{
    return bClosed.GetValue() != 0;
}

bool FPriorityTaskQueue::SetCapacity(int32 NewCapacity)
{
    if (NewCapacity < 0)
    {
        return false;
    }
    
    // Can't reduce capacity below current size
    if (NewCapacity > 0 && TotalSize.GetValue() > NewCapacity)
    {
        return false;
    }
    
    Capacity = NewCapacity;
    return true;
}

int32 FPriorityTaskQueue::EnqueueBatch(void** Items, int32 Count)
{
    SCOPE_CYCLE_COUNTER(STAT_PriorityTaskQueue_Enqueue);
    
    if (!bIsInitialized || Count <= 0 || Items == nullptr)
    {
        return 0;
    }
    
    if (bClosed.GetValue() != 0)
    {
        return 0;
    }
    
    // Calculate available capacity
    int32 AvailableSlots = Capacity > 0 ? FMath::Max(0, Capacity - TotalSize.GetValue()) : Count;
    int32 ItemsToEnqueue = FMath::Min(Count, AvailableSlots);
    
    if (ItemsToEnqueue == 0)
    {
        UpdateEnqueueStats(false);
        return 0;
    }
    
    // Default to normal priority (128)
    FScopeLock Lock(&QueueLock);
    
    FPriorityLevel* Level = GetOrCreatePriorityLevel(128);
    int32 EnqueuedCount = 0;
    
    for (int32 i = 0; i < ItemsToEnqueue; ++i)
    {
        // Skip null items
        if (Items[i] == nullptr)
        {
            continue;
        }
        
        Level->Tasks.Add(Items[i]);
        EnqueuedCount++;
    }
    
    // Update counts and stats
    if (EnqueuedCount > 0)
    {
        Level->Count.Add(EnqueuedCount);
        TotalSize.Add(EnqueuedCount);
        
        FScopeLock ScopedLock(&StatsLock);
        TotalEnqueued += EnqueuedCount;
        int32 CurrentSize = TotalSize.GetValue();
        if (CurrentSize > PeakSize)
        {
            PeakSize = CurrentSize;
        }
    }
    
    return EnqueuedCount;
}

int32 FPriorityTaskQueue::DequeueBatch(void** OutItems, int32 MaxCount)
{
    SCOPE_CYCLE_COUNTER(STAT_PriorityTaskQueue_Dequeue);
    
    if (!bIsInitialized || MaxCount <= 0 || OutItems == nullptr)
    {
        return 0;
    }
    
    int32 CurrentSize = TotalSize.GetValue();
    int32 ItemsToDequeue = FMath::Min(MaxCount, CurrentSize);
    
    if (ItemsToDequeue == 0)
    {
        UpdateDequeueStats(false);
        return 0;
    }
    
    // Check for starvation periodically
    double CurrentTime = FPlatformTime::Seconds();
    if (CurrentTime - LastStarvationUpdateTime > 0.5)
    {
        UpdateStarvationPrevention();
        LastStarvationUpdateTime = CurrentTime;
    }
    
    // Check for performance-based boosting periodically
    if (CurrentTime - LastPerformanceUpdateTime > PerformanceUpdateIntervalSeconds)
    {
        UpdatePerformanceBasedBoosting();
        LastPerformanceUpdateTime = CurrentTime;
    }
    
    FScopeLock Lock(&QueueLock);
    
    int32 DequeuedCount = 0;
    
    // Continue dequeuing until we hit our target count or run out of items
    while (DequeuedCount < ItemsToDequeue)
    {
        // Select next priority level based on priority, age, and performance
        FPriorityLevel* Level = SelectNextPriorityLevel();
        if (!Level || Level->Tasks.Num() == 0)
        {
            break;
        }
        
        // Calculate how many items to take from this level
        int32 LevelItemCount = FMath::Min(Level->Tasks.Num(), ItemsToDequeue - DequeuedCount);
        
        // Extract items from this level
        for (int32 i = 0; i < LevelItemCount; ++i)
        {
            OutItems[DequeuedCount++] = Level->Tasks[0];
            Level->Tasks.RemoveAt(0);
        }
        
        // Update level count
        Level->Count.Add(-LevelItemCount);
        Level->LastDequeueTime = FPlatformTime::Seconds();
    }
    
    // Update total size and stats
    if (DequeuedCount > 0)
    {
        TotalSize.Add(-DequeuedCount);
        
        FScopeLock ScopedLock(&StatsLock);
        TotalDequeued += DequeuedCount;
    }
    
    return DequeuedCount;
}

IThreadSafeQueue& FPriorityTaskQueue::Get()
{
    // Lazy singleton initialization
    if (Instance == nullptr)
    {
        Instance = new FPriorityTaskQueue();
        Instance->Initialize();
    }
    
    return *Instance;
}

void FPriorityTaskQueue::SetStarvationThreshold(float AgeThresholdSeconds)
{
    if (AgeThresholdSeconds >= 0.0f)
    {
        StarvationThresholdSeconds = AgeThresholdSeconds;
    }
}

void FPriorityTaskQueue::SetPriorityAgingFactor(float AgingFactor)
{
    if (AgingFactor >= 1.0f)
    {
        PriorityAgingFactor = AgingFactor;
    }
}

int32 FPriorityTaskQueue::GetCountAtPriority(uint8 Priority) const
{
    FScopeLock Lock(&QueueLock);
    
    FPriorityLevel* const* Level = PriorityLevels.Find(Priority);
    if (Level && *Level)
    {
        return (*Level)->Count.GetValue();
    }
    
    return 0;
}

TArray<uint8> FPriorityTaskQueue::GetActivePriorityLevels() const
{
    FScopeLock Lock(&QueueLock);
    
    TArray<uint8> Result;
    for (auto& Pair : PriorityLevels)
    {
        if (Pair.Value->Count.GetValue() > 0)
        {
            Result.Add(Pair.Key);
        }
    }
    
    // Sort by priority (highest first)
    Result.Sort([](uint8 A, uint8 B) { return A > B; });
    
    return Result;
}

void FPriorityTaskQueue::UpdateStarvationPrevention()
{
    SCOPE_CYCLE_COUNTER(STAT_PriorityTaskQueue_Rebalance);
    
    FScopeLock Lock(&QueueLock);
    
    double CurrentTime = FPlatformTime::Seconds();
    
    for (auto& Pair : PriorityLevels)
    {
        FPriorityLevel* Level = Pair.Value;
        
        // Skip empty levels
        if (Level->Count.GetValue() == 0)
        {
            Level->AgeFactor = 1.0f;
            continue;
        }
        
        // Skip critical levels - they're already high priority
        if (Level->bIsCritical)
        {
            Level->AgeFactor = 1.0f;
            continue;
        }
        
        // Calculate age since last dequeue
        double TimeSinceLastDequeue = CurrentTime - Level->LastDequeueTime;
        
        // Update age factor based on starvation threshold
        if (TimeSinceLastDequeue > StarvationThresholdSeconds)
        {
            // Calculate how many times over threshold we are
            float StarvationFactor = TimeSinceLastDequeue / StarvationThresholdSeconds;
            
            // Apply aging with a cap to prevent overflow
            Level->AgeFactor = FMath::Min(5.0f, PriorityAgingFactor * StarvationFactor);
        }
        else
        {
            // Reset age factor
            Level->AgeFactor = 1.0f;
        }
    }
}

void FPriorityTaskQueue::UpdateEnqueueStats(bool bSuccess, double WaitTimeMs)
{
    FScopeLock Lock(&StatsLock);
    
    if (bSuccess)
    {
        TotalEnqueued++;
        int32 CurrentSize = TotalSize.GetValue();
        if (CurrentSize > PeakSize)
        {
            PeakSize = CurrentSize;
        }
    }
    else
    {
        EnqueueFailures++;
    }
    
    if (WaitTimeMs > 0.0)
    {
        TotalEnqueueWaitTimeMs += WaitTimeMs;
        EnqueueWaitCount++;
    }
}

void FPriorityTaskQueue::UpdateDequeueStats(bool bSuccess, double WaitTimeMs)
{
    FScopeLock Lock(&StatsLock);
    
    if (bSuccess)
    {
        TotalDequeued++;
    }
    else
    {
        DequeueFailures++;
    }
    
    if (WaitTimeMs > 0.0)
    {
        TotalDequeueWaitTimeMs += WaitTimeMs;
        DequeueWaitCount++;
    }
}

FPriorityLevel* FPriorityTaskQueue::GetOrCreatePriorityLevel(uint8 Priority)
{
    // This function assumes QueueLock is already held
    
    FPriorityLevel* const* ExistingLevel = PriorityLevels.Find(Priority);
    if (ExistingLevel && *ExistingLevel)
    {
        return *ExistingLevel;
    }
    
    // Create new level
    FPriorityLevel* NewLevel = new FPriorityLevel(Priority);
    PriorityLevels.Add(Priority, NewLevel);
    
    return NewLevel;
}

FPriorityLevel* FPriorityTaskQueue::SelectNextPriorityLevel() const
{
    // This function assumes QueueLock is already held
    
    FPriorityLevel* SelectedLevel = nullptr;
    float HighestAdjustedPriority = -1.0f;
    
    for (auto& Pair : PriorityLevels)
    {
        FPriorityLevel* Level = Pair.Value;
        
        // Skip empty levels
        if (Level->Count.GetValue() == 0 || Level->Tasks.Num() == 0)
        {
            continue;
        }
        
        // Critical levels always get selected first regardless of other factors
        if (Level->bIsCritical)
        {
            return Level;
        }
        
        // Apply background throttling for low-priority tasks
        float ThrottlingFactor = 1.0f;
        if (BackgroundThrottlingFactor > 0.0f && Level->Priority < 128)
        {
            float SystemLoad = PerformanceFeedback.SystemLoadFactor;
            float PriorityFactor = Level->Priority / 128.0f;
            ThrottlingFactor = 1.0f - (BackgroundThrottlingFactor * SystemLoad * (1.0f - PriorityFactor));
            ThrottlingFactor = FMath::Max(0.1f, ThrottlingFactor); // Never throttle below 10%
        }
        
        // Performance-based boosting factor based on execution time
        float PerformanceFactor = 1.0f;
        if (Level->ExecutionTimeSamples > 0)
        {
            // Tasks that complete quickly get a boost, slow tasks get penalized
            double AvgExecTime = 0.0;
            int32 SampleCount = 0;
            
            // Calculate average execution time
            for (auto& PerfPair : PerformanceFeedback.RecentExecutionTimes)
            {
                AvgExecTime += PerfPair.Value;
                SampleCount++;
            }
            
            if (SampleCount > 0)
            {
                AvgExecTime /= SampleCount;
                
                if (AvgExecTime > 0.0)
                {
                    // Adjust based on how this level compares to the average
                    float TimeRatio = AvgExecTime / Level->AverageExecutionTimeMs;
                    PerformanceFactor = FMath::Clamp(TimeRatio, 0.5f, 2.0f);
                }
            }
        }
        
        // Calculate adjusted priority with age factor, throttling, and performance
        float AdjustedPriority = Level->Priority * Level->AgeFactor * ThrottlingFactor * PerformanceFactor;
        
        // Select level with highest adjusted priority
        if (AdjustedPriority > HighestAdjustedPriority)
        {
            HighestAdjustedPriority = AdjustedPriority;
            SelectedLevel = Level;
        }
    }
    
    return SelectedLevel;
}

bool FPriorityTaskQueue::WaitNotFull(uint32 TimeoutMs, double& OutWaitTimeMs)
{
    if (TimeoutMs == 0)
    {
        OutWaitTimeMs = 0.0;
        return false;
    }
    
    double StartTime = FPlatformTime::Seconds();
    double EndTime = StartTime + TimeoutMs / 1000.0;
    
    while (FPlatformTime::Seconds() < EndTime)
    {
        if (Capacity <= 0 || TotalSize.GetValue() < Capacity)
        {
            OutWaitTimeMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
            return true;
        }
        
        // Small sleep to avoid busy waiting
        FPlatformProcess::SleepNoStats(0.0001f);
    }
    
    OutWaitTimeMs = TimeoutMs;
    return false;
}

bool FPriorityTaskQueue::WaitNotEmpty(uint32 TimeoutMs, double& OutWaitTimeMs)
{
    if (TimeoutMs == 0)
    {
        OutWaitTimeMs = 0.0;
        return false;
    }
    
    double StartTime = FPlatformTime::Seconds();
    double EndTime = StartTime + TimeoutMs / 1000.0;
    
    while (FPlatformTime::Seconds() < EndTime)
    {
        if (TotalSize.GetValue() > 0)
        {
            OutWaitTimeMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
            return true;
        }
        
        // Small sleep to avoid busy waiting
        FPlatformProcess::SleepNoStats(0.0001f);
    }
    
    OutWaitTimeMs = TimeoutMs;
    return false;
}

bool FPriorityTaskQueue::AddTaskDependency(uint64 DependentTaskId, uint64 DependencyTaskId)
{
    SCOPE_CYCLE_COUNTER(STAT_PriorityTaskQueue_PriorityInheritance);
    
    if (DependentTaskId == DependencyTaskId)
    {
        return false; // Can't depend on itself
    }
    
    FScopeLock Lock(&QueueLock);
    
    // Add to dependencies list
    TArray<uint64>& Dependencies = DependencyInfo.Dependencies.FindOrAdd(DependentTaskId);
    Dependencies.AddUnique(DependencyTaskId);
    
    // Add to reverse lookup for dependents
    TArray<uint64>& Dependents = DependencyInfo.Dependents.FindOrAdd(DependencyTaskId);
    Dependents.AddUnique(DependentTaskId);
    
    // Check if the dependency has a higher inherited priority and propagate it
    uint8* DependencyPriority = DependencyInfo.InheritedPriorities.Find(DependencyTaskId);
    uint8* DependentPriority = DependencyInfo.InheritedPriorities.Find(DependentTaskId);
    
    if (DependencyPriority && (!DependentPriority || *DependencyPriority > *DependentPriority))
    {
        DependencyInfo.InheritedPriorities.Add(DependentTaskId, *DependencyPriority);
    }
    
    return true;
}

bool FPriorityTaskQueue::CompleteTask(uint64 TaskId)
{
    SCOPE_CYCLE_COUNTER(STAT_PriorityTaskQueue_PriorityInheritance);
    
    FScopeLock Lock(&QueueLock);
    
    // Remove task from inherited priorities
    DependencyInfo.InheritedPriorities.Remove(TaskId);
    
    // Update dependent tasks
    UpdateDependentTaskPriorities(TaskId);
    
    // Clean up dependencies
    DependencyInfo.Dependencies.Remove(TaskId);
    
    // Remove from dependents lists
    for (auto& Pair : DependencyInfo.Dependents)
    {
        Pair.Value.Remove(TaskId);
    }
    
    // Remove empty dependents entries
    TArray<uint64> EmptyKeys;
    for (auto& Pair : DependencyInfo.Dependents)
    {
        if (Pair.Value.Num() == 0)
        {
            EmptyKeys.Add(Pair.Key);
        }
    }
    
    for (uint64 Key : EmptyKeys)
    {
        DependencyInfo.Dependents.Remove(Key);
    }
    
    return true;
}

void FPriorityTaskQueue::ReportExecutionTime(uint8 Priority, double ExecutionTimeMs)
{
    SCOPE_CYCLE_COUNTER(STAT_PriorityTaskQueue_PerformanceBoost);
    
    FScopeLock Lock(&QueueLock);
    
    // Update execution time for the priority level
    FPriorityLevel* Level = GetOrCreatePriorityLevel(Priority);
    
    // Use exponential moving average to update stats
    if (Level->ExecutionTimeSamples == 0)
    {
        Level->AverageExecutionTimeMs = ExecutionTimeMs;
    }
    else
    {
        const double Alpha = 0.2; // Weight for new samples (0.0-1.0)
        Level->AverageExecutionTimeMs = Alpha * ExecutionTimeMs + (1.0 - Alpha) * Level->AverageExecutionTimeMs;
    }
    
    Level->ExecutionTimeSamples++;
    
    // Update recent execution times for feedback
    PerformanceFeedback.RecentExecutionTimes.Add(Priority, ExecutionTimeMs);
}

void FPriorityTaskQueue::SetSystemLoadFactor(float LoadFactor)
{
    FScopeLock Lock(&QueueLock);
    PerformanceFeedback.SystemLoadFactor = FMath::Clamp(LoadFactor, 0.0f, 1.0f);
}

void FPriorityTaskQueue::SetBackgroundThrottling(float ThrottlingFactor)
{
    BackgroundThrottlingFactor = FMath::Clamp(ThrottlingFactor, 0.0f, 1.0f);
}

void FPriorityTaskQueue::SetPriorityCritical(uint8 Priority, bool bCritical)
{
    FScopeLock Lock(&QueueLock);
    
    FPriorityLevel* Level = GetOrCreatePriorityLevel(Priority);
    Level->bIsCritical = bCritical;
}

uint64 FPriorityTaskQueue::GenerateTaskId()
{
    return DependencyInfo.GetNextTaskId();
}

void FPriorityTaskQueue::UpdatePerformanceBasedBoosting()
{
    SCOPE_CYCLE_COUNTER(STAT_PriorityTaskQueue_PerformanceBoost);
    
    FScopeLock Lock(&QueueLock);
    
    // Apply background throttling based on system load
    float CurrentThrottling = BackgroundThrottlingFactor * PerformanceFeedback.SystemLoadFactor;
    
    if (CurrentThrottling > 0.0f)
    {
        // Throttle low-priority tasks based on system load
        for (auto& Pair : PriorityLevels)
        {
            FPriorityLevel* Level = Pair.Value;
            
            // Skip critical levels
            if (Level->bIsCritical)
            {
                continue;
            }
            
            // Apply throttling inversely proportional to priority
            // Lower priority = more throttling
            if (Level->Priority < 128)
            {
                float PriorityFactor = Level->Priority / 128.0f;
                float ThrottlingFactor = CurrentThrottling * (1.0f - PriorityFactor);
                
                // Reduce age factor based on throttling
                Level->AgeFactor *= (1.0f - ThrottlingFactor);
            }
        }
    }
    
    // Identify performance-critical tasks based on execution time
    if (PerformanceFeedback.RecentExecutionTimes.Num() > 0)
    {
        // Find average execution time across all priorities
        double TotalTime = 0.0;
        int32 SampleCount = 0;
        
        for (const auto& Pair : PerformanceFeedback.RecentExecutionTimes)
        {
            TotalTime += Pair.Value;
            SampleCount++;
        }
        
        double AverageTime = SampleCount > 0 ? TotalTime / SampleCount : 0.0;
        
        // Boost tasks that complete quickly (efficient tasks)
        for (auto& Pair : PriorityLevels)
        {
            uint8 Priority = Pair.Key;
            FPriorityLevel* Level = Pair.Value;
            
            if (Level->ExecutionTimeSamples > 0 && Level->AverageExecutionTimeMs < AverageTime * 0.5)
            {
                // Tasks that complete quickly get a priority boost
                Level->AgeFactor *= 1.25f;
            }
        }
    }
    
    // Clear recent execution times for next interval
    PerformanceFeedback.RecentExecutionTimes.Empty();
}

void FPriorityTaskQueue::ApplyPriorityInheritance(uint64 TaskId, uint8 Priority)
{
    SCOPE_CYCLE_COUNTER(STAT_PriorityTaskQueue_PriorityInheritance);
    
    // Store the task's base priority
    uint8& StoredPriority = DependencyInfo.InheritedPriorities.FindOrAdd(TaskId);
    StoredPriority = FMath::Max(StoredPriority, Priority);
    
    // Check if this task depends on others
    TArray<uint64>* Dependencies = DependencyInfo.Dependencies.Find(TaskId);
    if (Dependencies)
    {
        for (uint64 DependencyId : *Dependencies)
        {
            // Propagate priority to dependencies if it's higher
            uint8& DependencyPriority = DependencyInfo.InheritedPriorities.FindOrAdd(DependencyId);
            
            if (StoredPriority > DependencyPriority)
            {
                DependencyPriority = StoredPriority;
                
                // Recursively propagate to other dependencies
                ApplyPriorityInheritance(DependencyId, StoredPriority);
            }
        }
    }
    
    // Check if others depend on this task
    TArray<uint64>* Dependents = DependencyInfo.Dependents.Find(TaskId);
    if (Dependents && Priority > StoredPriority)
    {
        // Update this task's priority and propagate to dependents
        StoredPriority = Priority;
        
        for (uint64 DependentId : *Dependents)
        {
            uint8& DependentPriority = DependencyInfo.InheritedPriorities.FindOrAdd(DependentId);
            
            if (Priority > DependentPriority)
            {
                DependentPriority = Priority;
            }
        }
    }
}

void FPriorityTaskQueue::UpdateDependentTaskPriorities(uint64 CompletedTaskId)
{
    // Remove this task from all dependent tasks' dependencies
    TArray<uint64>* Dependents = DependencyInfo.Dependents.Find(CompletedTaskId);
    if (!Dependents)
    {
        return;
    }
    
    for (uint64 DependentId : *Dependents)
    {
        TArray<uint64>* Dependencies = DependencyInfo.Dependencies.Find(DependentId);
        if (Dependencies)
        {
            Dependencies->Remove(CompletedTaskId);
            
            // If this was the last dependency, the task is now ready
            if (Dependencies->Num() == 0)
            {
                // We could add additional handling here for when tasks become ready
            }
        }
    }
    
    // Remove the completed task from dependents tracking
    DependencyInfo.Dependents.Remove(CompletedTaskId);
}