// Copyright Epic Games, Inc. All Rights Reserved.

#include "ThreadSafeOperationQueue.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"

FThreadSafeOperationQueue::FThreadSafeOperationQueue(int32 MaxSize)
    : MaxQueueSize(FMath::Max(1, MaxSize))
    , bIsClosed(false)
{
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
    
    // Initialize last statistics reset time
    LastStatsResetTime = FPlatformTime::Seconds();
}

FThreadSafeOperationQueue::~FThreadSafeOperationQueue()
{
    // Close the queue if not already closed
    if (!bIsClosed)
    {
        Close();
    }
}

EQueueOperationResult FThreadSafeOperationQueue::Enqueue(void* Item, uint32 TimeoutMs)
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
    
    // Track enqueue start time
    double StartTime = FPlatformTime::Seconds();
    bool bTimeoutSpecified = (TimeoutMs > 0);
    
    // Acquire lock for enqueueing
    FScopeLock Lock(&QueueLock);
    
    // Check if queue is full
    const bool bHasCapacityLimit = (MaxQueueSize > 0);
    
    if (bHasCapacityLimit && Queue.Num() >= MaxQueueSize)
    {
        // Wait for space if timeout specified
        if (bTimeoutSpecified)
        {
            double EndTime = StartTime + (TimeoutMs / 1000.0);
            
            while (Queue.Num() >= MaxQueueSize && FPlatformTime::Seconds() < EndTime)
            {
                // Release lock temporarily
                QueueLock.Unlock();
                
                // Sleep briefly
                FPlatformProcess::Sleep(0.001f);
                
                // Reacquire lock
                QueueLock.Lock();
                
                // Check if queue closed while waiting
                if (bIsClosed)
                {
                    Stats.FailedEnqueueCount++;
                    return EQueueOperationResult::Closed;
                }
            }
            
            // Check if we're still full after timeout
            if (Queue.Num() >= MaxQueueSize)
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
    
    // Add item to queue
    FQueuedOperation QueuedOp;
    QueuedOp.Item = Item;
    QueuedOp.EnqueueTime = FPlatformTime::Seconds();
    Queue.Add(QueuedOp);
    
    // Update statistics
    Stats.EnqueueCount++;
    Stats.PeakQueueSize = FMath::Max(Stats.PeakQueueSize, Queue.Num());
    
    // Calculate and update enqueue wait time
    double WaitTime = (QueuedOp.EnqueueTime - StartTime) * 1000.0;
    Stats.EnqueueWaitTimeMs += WaitTime;
    
    // Signal waiting threads
    QueueEvent.Trigger();
    
    return EQueueOperationResult::Success;
}

EQueueOperationResult FThreadSafeOperationQueue::Dequeue(void*& OutItem, uint32 TimeoutMs)
{
    // Initialize output parameter
    OutItem = nullptr;
    
    // Track dequeue start time
    double StartTime = FPlatformTime::Seconds();
    bool bTimeoutSpecified = (TimeoutMs > 0);
    
    // Acquire lock for dequeueing
    FScopeLock Lock(&QueueLock);
    
    // Check if queue is empty
    if (Queue.Num() == 0)
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
            
            while (Queue.Num() == 0 && !bIsClosed && FPlatformTime::Seconds() < EndTime)
            {
                // Release lock temporarily
                QueueLock.Unlock();
                
                // Wait for signal with timeout
                QueueEvent.Wait(1); // Wait with 1ms timeout
                
                // Reacquire lock
                QueueLock.Lock();
            }
            
            // Check if we're still empty after timeout
            if (Queue.Num() == 0)
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
    
    // Get the first item
    FQueuedOperation QueuedOp = Queue[0];
    Queue.RemoveAt(0);
    
    // Set output parameter
    OutItem = QueuedOp.Item;
    
    // Update statistics
    Stats.DequeueCount++;
    
    // Calculate queue time
    double QueueTime = (FPlatformTime::Seconds() - QueuedOp.EnqueueTime) * 1000.0;
    
    // Update queue time statistics
    if (Stats.DequeueCount > 1)
    {
        Stats.AverageQueueTimeMs = ((Stats.AverageQueueTimeMs * (Stats.DequeueCount - 1)) + QueueTime) / Stats.DequeueCount;
    }
    else
    {
        Stats.AverageQueueTimeMs = QueueTime;
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

EQueueOperationResult FThreadSafeOperationQueue::DequeueAll(TArray<void*>& OutItems, uint32 TimeoutMs)
{
    // Initialize output array
    OutItems.Empty();
    
    // Track dequeue start time
    double StartTime = FPlatformTime::Seconds();
    bool bTimeoutSpecified = (TimeoutMs > 0);
    
    // Acquire lock for dequeueing
    FScopeLock Lock(&QueueLock);
    
    // Check if queue is empty
    if (Queue.Num() == 0)
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
            
            while (Queue.Num() == 0 && !bIsClosed && FPlatformTime::Seconds() < EndTime)
            {
                // Release lock temporarily
                QueueLock.Unlock();
                
                // Wait for signal with timeout
                QueueEvent.Wait(1); // Wait with 1ms timeout
                
                // Reacquire lock
                QueueLock.Lock();
            }
            
            // Check if we're still empty after timeout
            if (Queue.Num() == 0)
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
    
    // Reserve space for all items
    OutItems.Reserve(Queue.Num());
    
    // Double average queue time for all items
    double TotalQueueTime = 0.0;
    double CurrentTime = FPlatformTime::Seconds();
    
    // Get all items
    for (const FQueuedOperation& QueuedOp : Queue)
    {
        // Add item to output array
        OutItems.Add(QueuedOp.Item);
        
        // Calculate queue time for statistics
        TotalQueueTime += (CurrentTime - QueuedOp.EnqueueTime) * 1000.0;
    }
    
    // Update dequeue count
    Stats.DequeueCount += Queue.Num();
    
    // Update batching statistics
    Stats.BatchesProcessed++;
    Stats.OperationsPacked += Queue.Num();
    
    // Update average queue time
    if (Stats.BatchesProcessed > 1)
    {
        double AverageThisBatch = TotalQueueTime / Queue.Num();
        Stats.AverageQueueTimeMs = ((Stats.AverageQueueTimeMs * (Stats.BatchesProcessed - 1)) + AverageThisBatch) / Stats.BatchesProcessed;
    }
    else
    {
        Stats.AverageQueueTimeMs = TotalQueueTime / Queue.Num();
    }
    
    // Clear the queue
    Queue.Empty();
    
    // Calculate and update dequeue wait time
    double WaitTime = (FPlatformTime::Seconds() - StartTime) * 1000.0;
    Stats.DequeueWaitTimeMs += WaitTime;
    
    return EQueueOperationResult::Success;
}

EQueueOperationResult FThreadSafeOperationQueue::DequeueBatch(TArray<void*>& OutItems, int32 MaxItems, uint32 TimeoutMs)
{
    // Initialize output array
    OutItems.Empty();
    
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
    
    // Check if queue is empty
    if (Queue.Num() == 0)
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
            
            while (Queue.Num() == 0 && !bIsClosed && FPlatformTime::Seconds() < EndTime)
            {
                // Release lock temporarily
                QueueLock.Unlock();
                
                // Wait for signal with timeout
                QueueEvent.Wait(1); // Wait with 1ms timeout
                
                // Reacquire lock
                QueueLock.Lock();
            }
            
            // Check if we're still empty after timeout
            if (Queue.Num() == 0)
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
    
    // Limit to available items or max batch size
    int32 ItemsToCopy = FMath::Min(Queue.Num(), MaxItems);
    
    // Reserve space for items
    OutItems.Reserve(ItemsToCopy);
    
    // Track queue times for statistics
    double TotalQueueTime = 0.0;
    double CurrentTime = FPlatformTime::Seconds();
    
    // Get items
    for (int32 i = 0; i < ItemsToCopy; ++i)
    {
        const FQueuedOperation& QueuedOp = Queue[i];
        
        // Add item to output array
        OutItems.Add(QueuedOp.Item);
        
        // Calculate queue time for statistics
        TotalQueueTime += (CurrentTime - QueuedOp.EnqueueTime) * 1000.0;
    }
    
    // Remove items from queue
    Queue.RemoveAt(0, ItemsToCopy);
    
    // Update dequeue count
    Stats.DequeueCount += ItemsToCopy;
    
    // Update batching statistics
    Stats.BatchesProcessed++;
    Stats.OperationsPacked += ItemsToCopy;
    
    // Update average queue time
    if (Stats.BatchesProcessed > 1)
    {
        double AverageThisBatch = TotalQueueTime / ItemsToCopy;
        Stats.AverageQueueTimeMs = ((Stats.AverageQueueTimeMs * (Stats.BatchesProcessed - 1)) + AverageThisBatch) / Stats.BatchesProcessed;
    }
    else
    {
        Stats.AverageQueueTimeMs = TotalQueueTime / ItemsToCopy;
    }
    
    // Calculate and update dequeue wait time
    double WaitTime = (FPlatformTime::Seconds() - StartTime) * 1000.0;
    Stats.DequeueWaitTimeMs += WaitTime;
    
    return EQueueOperationResult::Success;
}

void FThreadSafeOperationQueue::Close(bool bDrainQueue)
{
    FScopeLock Lock(&QueueLock);
    
    // Mark as closed
    bIsClosed = true;
    
    // Clear queue if not draining
    if (!bDrainQueue)
    {
        Queue.Empty();
    }
    
    // Signal any waiting threads
    QueueEvent.Trigger();
}

bool FThreadSafeOperationQueue::IsClosed() const
{
    return bIsClosed;
}

int32 FThreadSafeOperationQueue::GetCount() const
{
    FScopeLock Lock(&QueueLock);
    return Queue.Num();
}

bool FThreadSafeOperationQueue::IsEmpty() const
{
    FScopeLock Lock(&QueueLock);
    return Queue.Num() == 0;
}

bool FThreadSafeOperationQueue::IsFull() const
{
    if (MaxQueueSize <= 0)
    {
        return false; // No capacity limit
    }
    
    FScopeLock Lock(&QueueLock);
    return Queue.Num() >= MaxQueueSize;
}

FQueueStats FThreadSafeOperationQueue::GetStats() const
{
    // Return a copy of the statistics
    return Stats;
}

void FThreadSafeOperationQueue::ResetStats()
{
    FScopeLock Lock(&QueueLock);
    
    // Reset statistics
    Stats.EnqueueCount = 0;
    Stats.DequeueCount = 0;
    Stats.EnqueueWaitTimeMs = 0.0;
    Stats.DequeueWaitTimeMs = 0.0;
    Stats.PeakQueueSize = Queue.Num(); // Current size
    Stats.FailedEnqueueCount = 0;
    Stats.FailedDequeueCount = 0;
    Stats.AverageWaitTimeMs = 0.0;
    Stats.AverageQueueTimeMs = 0.0;
    Stats.BatchesProcessed = 0;
    Stats.OperationsPacked = 0;
    
    // Reset last statistics reset time
    LastStatsResetTime = FPlatformTime::Seconds();
}

bool FThreadSafeOperationQueue::WaitForItems(uint32 TimeoutMs)
{
    // Check if queue already has items or is closed
    {
        FScopeLock Lock(&QueueLock);
        if (Queue.Num() > 0 || bIsClosed)
        {
            return Queue.Num() > 0;
        }
    }
    
    // Wait for signal with timeout
    return QueueEvent.Wait(TimeoutMs);
}