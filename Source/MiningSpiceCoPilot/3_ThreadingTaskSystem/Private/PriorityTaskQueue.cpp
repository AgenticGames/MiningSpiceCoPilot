// Copyright Epic Games, Inc. All Rights Reserved.

#include "PriorityTaskQueue.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"

FPriorityTaskQueue::FPriorityTaskQueue(int32 MaxSize, int32 PriorityLevels)
    : MaxQueueSize(FMath::Max(1, MaxSize))
    , bIsClosed(false)
{
    // Create priority queues
    PriorityQueues.SetNum(FMath::Max(1, PriorityLevels));
    
    // Initialize statistics
    Stats.EnqueueCount = 0;
    Stats.DequeueCount = 0;
    Stats.EnqueueWaitTimeMs = 0.0;
    Stats.DequeueWaitTimeMs = 0.0;
    Stats.PeakQueueSize = 0;
    Stats.FailedEnqueueCount = 0;
    Stats.FailedDequeueCount = 0;
    Stats.AverageWaitTimeMs = 0.0;
    Stats.AverageQueueTimeMs = 0.0;
    Stats.BatchesProcessed = 0;
    Stats.OperationsPacked = 0;
    
    // Initialize priority-specific stats
    PriorityStats.SetNum(PriorityQueues.Num());
    for (int32 i = 0; i < PriorityStats.Num(); ++i)
    {
        PriorityStats[i].Priority = i;
        PriorityStats[i].EnqueueCount = 0;
        PriorityStats[i].DequeueCount = 0;
        PriorityStats[i].AverageQueueTimeMs = 0.0;
        PriorityStats[i].StarvedTimeMs = 0.0;
        PriorityStats[i].LastDequeueTime = 0.0;
    }
    
    // Initialize starvation prevention
    StarvationPrevention.LastCheckTime = FPlatformTime::Seconds();
    StarvationPrevention.AgingIntervalMs = 1000.0; // Age priority every second
    StarvationPrevention.AgingFactor = 0.1f;       // Age 10% of priority difference
    StarvationPrevention.MaxStarvationTimeMs = 10000.0; // 10 seconds max wait
    
    // Initialize last statistics reset time
    LastStatsResetTime = FPlatformTime::Seconds();
}

FPriorityTaskQueue::~FPriorityTaskQueue()
{
    // Close the queue if not already closed
    if (!bIsClosed)
    {
        Close();
    }
}

void FPriorityTaskQueue::SetStarvationPreventionSettings(float AgingIntervalMs, float AgingFactor, float MaxStarvationTimeMs)
{
    FScopeLock Lock(&QueueLock);
    
    // Update starvation prevention settings
    StarvationPrevention.AgingIntervalMs = FMath::Max(100.0f, AgingIntervalMs);
    StarvationPrevention.AgingFactor = FMath::Clamp(AgingFactor, 0.01f, 0.5f);
    StarvationPrevention.MaxStarvationTimeMs = FMath::Max(1000.0f, MaxStarvationTimeMs);
}

EQueueOperationResult FPriorityTaskQueue::Enqueue(void* Item, int32 Priority, uint32 TimeoutMs)
{
    // Check if queue is closed
    if (bIsClosed)
    {
        Stats.FailedEnqueueCount++;
        return EQueueOperationResult::Closed;
    }
    
    // Check if item is valid
    if (!Item)
    {
        Stats.FailedEnqueueCount++;
        return EQueueOperationResult::InvalidArgument;
    }
    
    // Clamp priority to valid range
    Priority = FMath::Clamp(Priority, 0, PriorityQueues.Num() - 1);
    
    // Track enqueue start time
    double StartTime = FPlatformTime::Seconds();
    bool bTimeoutSpecified = (TimeoutMs > 0);
    
    // Acquire lock for enqueueing
    FScopeLock Lock(&QueueLock);
    
    // Check if queue is full
    int32 TotalItems = GetTotalItemCount_NoLock();
    const bool bHasCapacityLimit = (MaxQueueSize > 0);
    
    if (bHasCapacityLimit && TotalItems >= MaxQueueSize)
    {
        // Wait for space if timeout specified
        if (bTimeoutSpecified)
        {
            double EndTime = StartTime + (TimeoutMs / 1000.0);
            
            while (TotalItems >= MaxQueueSize && FPlatformTime::Seconds() < EndTime)
            {
                // Release lock temporarily
                QueueLock.Unlock();
                
                // Sleep briefly
                FPlatformProcess::Sleep(0.001f);
                
                // Reacquire lock
                QueueLock.Lock();
                
                // Recheck total items
                TotalItems = GetTotalItemCount_NoLock();
                
                // Check if queue closed while waiting
                if (bIsClosed)
                {
                    Stats.FailedEnqueueCount++;
                    return EQueueOperationResult::Closed;
                }
            }
            
            // Check if we're still full after timeout
            if (TotalItems >= MaxQueueSize)
            {
                Stats.FailedEnqueueCount++;
                return EQueueOperationResult::Timeout;
            }
        }
        else
        {
            // No timeout, fail immediately
            Stats.FailedEnqueueCount++;
            return EQueueOperationResult::Full;
        }
    }
    
    // Add item to queue at given priority
    FQueuedOperation QueuedOp;
    QueuedOp.Item = Item;
    QueuedOp.EnqueueTime = FPlatformTime::Seconds();
    QueuedOp.OriginalPriority = Priority;
    QueuedOp.CurrentPriority = Priority;
    PriorityQueues[Priority].Add(QueuedOp);
    
    // Update statistics
    Stats.EnqueueCount++;
    TotalItems = GetTotalItemCount_NoLock();
    Stats.PeakQueueSize = FMath::Max(Stats.PeakQueueSize, TotalItems);
    
    // Update priority-specific statistics
    PriorityStats[Priority].EnqueueCount++;
    
    // Calculate and update enqueue wait time
    double WaitTime = (QueuedOp.EnqueueTime - StartTime) * 1000.0;
    Stats.EnqueueWaitTimeMs += WaitTime;
    
    // Signal waiting threads
    QueueEvent.Trigger();
    
    return EQueueOperationResult::Success;
}

EQueueOperationResult FPriorityTaskQueue::Dequeue(void*& OutItem, int32& OutPriority, uint32 TimeoutMs)
{
    // Initialize output parameters
    OutItem = nullptr;
    OutPriority = -1;
    
    // Track dequeue start time
    double StartTime = FPlatformTime::Seconds();
    bool bTimeoutSpecified = (TimeoutMs > 0);
    
    // Acquire lock for dequeueing
    FScopeLock Lock(&QueueLock);
    
    // Apply aging to combat starvation (if needed)
    ApplyAgingIfNeeded();
    
    // Check if all queues are empty
    bool bAllQueuesEmpty = true;
    for (const TArray<FQueuedOperation>& Queue : PriorityQueues)
    {
        if (Queue.Num() > 0)
        {
            bAllQueuesEmpty = false;
            break;
        }
    }
    
    if (bAllQueuesEmpty)
    {
        // If closed and empty, return closed
        if (bIsClosed)
        {
            Stats.FailedDequeueCount++;
            return EQueueOperationResult::Closed;
        }
        
        // Wait for items if timeout specified
        if (bTimeoutSpecified)
        {
            double EndTime = StartTime + (TimeoutMs / 1000.0);
            
            while (bAllQueuesEmpty && !bIsClosed && FPlatformTime::Seconds() < EndTime)
            {
                // Release lock temporarily
                QueueLock.Unlock();
                
                // Wait for signal with timeout
                QueueEvent.Wait(1); // Wait with 1ms timeout
                
                // Reacquire lock
                QueueLock.Lock();
                
                // Recheck if all queues are empty
                bAllQueuesEmpty = true;
                for (const TArray<FQueuedOperation>& Queue : PriorityQueues)
                {
                    if (Queue.Num() > 0)
                    {
                        bAllQueuesEmpty = false;
                        break;
                    }
                }
                
                // Apply aging while waiting
                ApplyAgingIfNeeded();
            }
            
            // Check if we're still empty after timeout
            if (bAllQueuesEmpty)
            {
                // If closed, return closed, otherwise timeout
                Stats.FailedDequeueCount++;
                return bIsClosed ? EQueueOperationResult::Closed : EQueueOperationResult::Timeout;
            }
        }
        else
        {
            // No timeout, fail immediately
            Stats.FailedDequeueCount++;
            return EQueueOperationResult::Empty;
        }
    }
    
    // Find highest priority queue with items
    int32 SelectedPriority = -1;
    int32 SelectedIndex = -1;
    FQueuedOperation SelectedOp;
    
    for (int32 Priority = 0; Priority < PriorityQueues.Num(); ++Priority)
    {
        TArray<FQueuedOperation>& Queue = PriorityQueues[Priority];
        if (Queue.Num() > 0)
        {
            // Find item with highest effective priority
            for (int32 Index = 0; Index < Queue.Num(); ++Index)
            {
                const FQueuedOperation& Op = Queue[Index];
                
                if (SelectedPriority == -1 || Op.CurrentPriority < SelectedPriority)
                {
                    SelectedPriority = Op.CurrentPriority;
                    SelectedIndex = Index;
                    SelectedOp = Op;
                }
            }
            
            // Remove the selected item
            if (SelectedIndex != -1)
            {
                Queue.RemoveAtSwap(SelectedIndex);
                break;
            }
        }
    }
    
    // Set output parameters
    OutItem = SelectedOp.Item;
    OutPriority = SelectedOp.OriginalPriority;
    
    // Update statistics
    Stats.DequeueCount++;
    
    // Update priority-specific statistics
    PriorityStats[SelectedOp.OriginalPriority].DequeueCount++;
    PriorityStats[SelectedOp.OriginalPriority].LastDequeueTime = FPlatformTime::Seconds();
    
    // Calculate queue time
    double QueueTime = (FPlatformTime::Seconds() - SelectedOp.EnqueueTime) * 1000.0;
    
    // Update queue time statistics
    if (Stats.DequeueCount > 1)
    {
        Stats.AverageQueueTimeMs = ((Stats.AverageQueueTimeMs * (Stats.DequeueCount - 1)) + QueueTime) / Stats.DequeueCount;
    }
    else
    {
        Stats.AverageQueueTimeMs = QueueTime;
    }
    
    // Update priority-specific queue time
    int32 PriDeqCount = PriorityStats[SelectedOp.OriginalPriority].DequeueCount;
    if (PriDeqCount > 1)
    {
        PriorityStats[SelectedOp.OriginalPriority].AverageQueueTimeMs = 
            ((PriorityStats[SelectedOp.OriginalPriority].AverageQueueTimeMs * (PriDeqCount - 1)) + QueueTime) / PriDeqCount;
    }
    else
    {
        PriorityStats[SelectedOp.OriginalPriority].AverageQueueTimeMs = QueueTime;
    }
    
    // Calculate and update dequeue wait time
    double WaitTime = (FPlatformTime::Seconds() - StartTime) * 1000.0;
    Stats.DequeueWaitTimeMs += WaitTime;
    
    // Update average wait time
    if (Stats.DequeueCount > 1)
    {
        Stats.AverageWaitTimeMs = ((Stats.AverageWaitTimeMs * (Stats.DequeueCount - 1)) + WaitTime) / Stats.DequeueCount;
    }
    else
    {
        Stats.AverageWaitTimeMs = WaitTime;
    }
    
    return EQueueOperationResult::Success;
}

EQueueOperationResult FPriorityTaskQueue::DequeueBatch(TArray<void*>& OutItems, TArray<int32>& OutPriorities, int32 MaxItems, uint32 TimeoutMs)
{
    // Initialize output arrays
    OutItems.Empty();
    OutPriorities.Empty();
    
    // Validate parameters
    if (MaxItems <= 0)
    {
        Stats.FailedDequeueCount++;
        return EQueueOperationResult::InvalidArgument;
    }
    
    // Track dequeue start time
    double StartTime = FPlatformTime::Seconds();
    bool bTimeoutSpecified = (TimeoutMs > 0);
    
    // Acquire lock for dequeueing
    FScopeLock Lock(&QueueLock);
    
    // Apply aging to combat starvation (if needed)
    ApplyAgingIfNeeded();
    
    // Check if all queues are empty
    bool bAllQueuesEmpty = true;
    for (const TArray<FQueuedOperation>& Queue : PriorityQueues)
    {
        if (Queue.Num() > 0)
        {
            bAllQueuesEmpty = false;
            break;
        }
    }
    
    if (bAllQueuesEmpty)
    {
        // If closed and empty, return closed
        if (bIsClosed)
        {
            Stats.FailedDequeueCount++;
            return EQueueOperationResult::Closed;
        }
        
        // Wait for items if timeout specified
        if (bTimeoutSpecified)
        {
            double EndTime = StartTime + (TimeoutMs / 1000.0);
            
            while (bAllQueuesEmpty && !bIsClosed && FPlatformTime::Seconds() < EndTime)
            {
                // Release lock temporarily
                QueueLock.Unlock();
                
                // Wait for signal with timeout
                QueueEvent.Wait(1); // Wait with 1ms timeout
                
                // Reacquire lock
                QueueLock.Lock();
                
                // Recheck if all queues are empty
                bAllQueuesEmpty = true;
                for (const TArray<FQueuedOperation>& Queue : PriorityQueues)
                {
                    if (Queue.Num() > 0)
                    {
                        bAllQueuesEmpty = false;
                        break;
                    }
                }
                
                // Apply aging while waiting
                ApplyAgingIfNeeded();
            }
            
            // Check if we're still empty after timeout
            if (bAllQueuesEmpty)
            {
                // If closed, return closed, otherwise timeout
                Stats.FailedDequeueCount++;
                return bIsClosed ? EQueueOperationResult::Closed : EQueueOperationResult::Timeout;
            }
        }
        else
        {
            // No timeout, fail immediately
            Stats.FailedDequeueCount++;
            return EQueueOperationResult::Empty;
        }
    }
    
    // Reserve space for output
    int32 TotalItems = GetTotalItemCount_NoLock();
    int32 ItemsToDequeue = FMath::Min(TotalItems, MaxItems);
    OutItems.Reserve(ItemsToDequeue);
    OutPriorities.Reserve(ItemsToDequeue);
    
    // Track queue times for statistics
    double TotalQueueTime = 0.0;
    double CurrentTime = FPlatformTime::Seconds();
    TMap<int32, int32> PriorityDequeues;
    TMap<int32, double> PriorityQueueTimes;
    
    // Dequeue items until we reach the limit
    while (OutItems.Num() < MaxItems)
    {
        // Find highest priority item
        int32 SelectedPriority = -1;
        int32 SelectedQueue = -1;
        int32 SelectedIndex = -1;
        FQueuedOperation SelectedOp;
        
        for (int32 Queue = 0; Queue < PriorityQueues.Num(); ++Queue)
        {
            TArray<FQueuedOperation>& PriorityQueue = PriorityQueues[Queue];
            
            if (PriorityQueue.Num() > 0)
            {
                // Find item with highest effective priority in this queue
                for (int32 Index = 0; Index < PriorityQueue.Num(); ++Index)
                {
                    const FQueuedOperation& Op = PriorityQueue[Index];
                    
                    if (SelectedPriority == -1 || Op.CurrentPriority < SelectedPriority)
                    {
                        SelectedPriority = Op.CurrentPriority;
                        SelectedQueue = Queue;
                        SelectedIndex = Index;
                        SelectedOp = Op;
                    }
                }
            }
        }
        
        // If no more items found, we're done
        if (SelectedPriority == -1)
        {
            break;
        }
        
        // Remove the selected item
        PriorityQueues[SelectedQueue].RemoveAtSwap(SelectedIndex);
        
        // Add to output arrays
        OutItems.Add(SelectedOp.Item);
        OutPriorities.Add(SelectedOp.OriginalPriority);
        
        // Track statistics for this item
        double QueueTime = (CurrentTime - SelectedOp.EnqueueTime) * 1000.0;
        TotalQueueTime += QueueTime;
        
        // Track priority-specific statistics
        int32 OriginalPriority = SelectedOp.OriginalPriority;
        if (!PriorityDequeues.Contains(OriginalPriority))
        {
            PriorityDequeues.Add(OriginalPriority, 0);
            PriorityQueueTimes.Add(OriginalPriority, 0.0);
        }
        PriorityDequeues[OriginalPriority]++;
        PriorityQueueTimes[OriginalPriority] += QueueTime;
        
        // Update timestamp for this priority level
        PriorityStats[OriginalPriority].LastDequeueTime = CurrentTime;
    }
    
    // Update dequeue count
    int32 DequeuedItems = OutItems.Num();
    Stats.DequeueCount += DequeuedItems;
    
    // Update priority-specific statistics
    for (auto It = PriorityDequeues.CreateConstIterator(); It; ++It)
    {
        int32 Priority = It.Key();
        int32 Count = It.Value();
        double QueueTime = PriorityQueueTimes[Priority];
        
        // Update dequeue count
        PriorityStats[Priority].DequeueCount += Count;
        
        // Update average queue time for this priority
        int32 TotalDequeuesForPriority = PriorityStats[Priority].DequeueCount;
        if (TotalDequeuesForPriority > Count)
        {
            double ExistingAvg = PriorityStats[Priority].AverageQueueTimeMs;
            double ExistingTotal = ExistingAvg * (TotalDequeuesForPriority - Count);
            PriorityStats[Priority].AverageQueueTimeMs = (ExistingTotal + QueueTime) / TotalDequeuesForPriority;
        }
        else
        {
            PriorityStats[Priority].AverageQueueTimeMs = QueueTime / Count;
        }
    }
    
    // Update batching statistics
    Stats.BatchesProcessed++;
    Stats.OperationsPacked += DequeuedItems;
    
    // Update average queue time
    if (Stats.BatchesProcessed > 1)
    {
        double AverageThisBatch = TotalQueueTime / DequeuedItems;
        Stats.AverageQueueTimeMs = ((Stats.AverageQueueTimeMs * (Stats.BatchesProcessed - 1)) + AverageThisBatch) / Stats.BatchesProcessed;
    }
    else
    {
        Stats.AverageQueueTimeMs = TotalQueueTime / DequeuedItems;
    }
    
    // Calculate and update dequeue wait time
    double WaitTime = (FPlatformTime::Seconds() - StartTime) * 1000.0;
    Stats.DequeueWaitTimeMs += WaitTime;
    
    return EQueueOperationResult::Success;
}

void FPriorityTaskQueue::Close(bool bDrainQueue)
{
    FScopeLock Lock(&QueueLock);
    
    // Mark as closed
    bIsClosed = true;
    
    // Clear queue if not draining
    if (!bDrainQueue)
    {
        for (TArray<FQueuedOperation>& Queue : PriorityQueues)
        {
            Queue.Empty();
        }
    }
    
    // Signal any waiting threads
    QueueEvent.Trigger();
}

bool FPriorityTaskQueue::IsClosed() const
{
    return bIsClosed;
}

int32 FPriorityTaskQueue::GetCount() const
{
    FScopeLock Lock(&QueueLock);
    return GetTotalItemCount_NoLock();
}

int32 FPriorityTaskQueue::GetCountForPriority(int32 Priority) const
{
    FScopeLock Lock(&QueueLock);
    
    if (Priority >= 0 && Priority < PriorityQueues.Num())
    {
        return PriorityQueues[Priority].Num();
    }
    
    return 0;
}

bool FPriorityTaskQueue::IsEmpty() const
{
    FScopeLock Lock(&QueueLock);
    return GetTotalItemCount_NoLock() == 0;
}

bool FPriorityTaskQueue::IsFull() const
{
    if (MaxQueueSize <= 0)
    {
        return false; // No capacity limit
    }
    
    FScopeLock Lock(&QueueLock);
    return GetTotalItemCount_NoLock() >= MaxQueueSize;
}

FQueueStats FPriorityTaskQueue::GetStats() const
{
    // Return a copy of the statistics
    return Stats;
}

TArray<FPriorityQueueStats> FPriorityTaskQueue::GetPriorityStats() const
{
    FScopeLock Lock(&QueueLock);
    return PriorityStats;
}

void FPriorityTaskQueue::ResetStats()
{
    FScopeLock Lock(&QueueLock);
    
    // Reset statistics
    Stats.EnqueueCount = 0;
    Stats.DequeueCount = 0;
    Stats.EnqueueWaitTimeMs = 0.0;
    Stats.DequeueWaitTimeMs = 0.0;
    Stats.PeakQueueSize = GetTotalItemCount_NoLock(); // Current size
    Stats.FailedEnqueueCount = 0;
    Stats.FailedDequeueCount = 0;
    Stats.AverageWaitTimeMs = 0.0;
    Stats.AverageQueueTimeMs = 0.0;
    Stats.BatchesProcessed = 0;
    Stats.OperationsPacked = 0;
    
    // Reset priority-specific stats
    for (FPriorityQueueStats& PriStats : PriorityStats)
    {
        PriStats.EnqueueCount = 0;
        PriStats.DequeueCount = 0;
        PriStats.AverageQueueTimeMs = 0.0;
        PriStats.StarvedTimeMs = 0.0;
        // Keep LastDequeueTime to track starvation
    }
    
    // Reset last statistics reset time
    LastStatsResetTime = FPlatformTime::Seconds();
}

bool FPriorityTaskQueue::WaitForItems(uint32 TimeoutMs)
{
    // Check if queue already has items or is closed
    {
        FScopeLock Lock(&QueueLock);
        int32 TotalItems = GetTotalItemCount_NoLock();
        if (TotalItems > 0 || bIsClosed)
        {
            return TotalItems > 0;
        }
    }
    
    // Wait for signal with timeout
    return QueueEvent.Wait(TimeoutMs);
}

int32 FPriorityTaskQueue::GetTotalItemCount_NoLock() const
{
    int32 TotalItems = 0;
    
    for (const TArray<FQueuedOperation>& Queue : PriorityQueues)
    {
        TotalItems += Queue.Num();
    }
    
    return TotalItems;
}

void FPriorityTaskQueue::ApplyAgingIfNeeded()
{
    // Get current time
    double CurrentTime = FPlatformTime::Seconds();
    
    // Check if it's time to apply aging
    double TimeSinceLastCheck = (CurrentTime - StarvationPrevention.LastCheckTime) * 1000.0;
    if (TimeSinceLastCheck < StarvationPrevention.AgingIntervalMs)
    {
        return;
    }
    
    // Update last check time
    StarvationPrevention.LastCheckTime = CurrentTime;
    
    // Check for starvation in each priority level
    for (int32 Priority = 0; Priority < PriorityQueues.Num(); ++Priority)
    {
        TArray<FQueuedOperation>& Queue = PriorityQueues[Priority];
        
        if (Queue.Num() == 0)
        {
            continue;
        }
        
        // Update starvation time for this priority
        double LastDeqTime = PriorityStats[Priority].LastDequeueTime;
        
        if (LastDeqTime > 0.0)
        {
            double StarvationTime = (CurrentTime - LastDeqTime) * 1000.0;
            PriorityStats[Priority].StarvedTimeMs = StarvationTime;
            
            // Apply aging if starvation time exceeds threshold
            if (StarvationTime > StarvationPrevention.AgingIntervalMs)
            {
                // Calculate aging factor based on starvation time
                float AgingAmount = FMath::Min(
                    StarvationPrevention.AgingFactor * (StarvationTime / StarvationPrevention.AgingIntervalMs),
                    1.0f
                );
                
                // Apply aging to all items in this queue
                for (FQueuedOperation& Op : Queue)
                {
                    // Calculate priority difference
                    int32 PriorityDiff = Op.OriginalPriority - Op.CurrentPriority;
                    
                    if (PriorityDiff > 0)
                    {
                        // Apply aging - move towards original priority
                        int32 BoostAmount = FMath::CeilToInt(PriorityDiff * AgingAmount);
                        Op.CurrentPriority = FMath::Min(Op.CurrentPriority + BoostAmount, Op.OriginalPriority);
                    }
                }
            }
            
            // If extreme starvation, temporarily boost priority
            if (StarvationTime > StarvationPrevention.MaxStarvationTimeMs)
            {
                for (FQueuedOperation& Op : Queue)
                {
                    // Temporarily boost priority higher than original
                    Op.CurrentPriority = FMath::Max(0, Op.CurrentPriority - 1);
                }
            }
        }
    }
}