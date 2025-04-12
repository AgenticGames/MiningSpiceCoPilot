// Copyright Epic Games, Inc. All Rights Reserved.

#include "ThreadSafeOperationQueue.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"

// Constants for hazard pointer system
const int32 MAX_THREADS = 64;
const int32 MAX_HAZARD_POINTERS_PER_THREAD = 4;

// Hazard pointer system for safe memory reclamation
struct FHazardPointerRecord
{
    const void* Pointer;
    int32 ThreadId;
    int32 SlotIndex;
};

// Per-thread hazard pointer storage
TArray<FHazardPointerRecord> GHazardPointers;
FCriticalSection GHazardPointerLock;

// Thread-local storage for thread ID
static uint32 ThreadLocalIdTLS = FPlatformTLS::AllocTlsSlot();

FThreadSafeOperationQueue::FThreadSafeOperationQueue(int32 InitialCapacity, bool bAllowConcurrentConsumers)
    : bAllowMultipleConsumers(bAllowConcurrentConsumers)
    , CurrentConsumerThreadId(0)
    , bIsClosed(false)
{
    // Initialize with specified capacity
    Operations.Reserve(InitialCapacity);
    RetiredNodes.Reserve(InitialCapacity);
    
    // Initialize statistics
    Stats.EnqueueCount = 0;
    Stats.DequeueCount = 0;
    Stats.PeakQueueSize = 0;
    Stats.EnqueueBlockedCount = 0;
    Stats.DequeueBlockedCount = 0;
    Stats.EnqueueBlockTimeMs = 0.0;
    Stats.DequeueBlockTimeMs = 0.0;
}

FThreadSafeOperationQueue::~FThreadSafeOperationQueue()
{
    // Close the queue first to prevent new operations
    Close();
    
    // Drain any remaining operations
    FQueuedOperation Operation;
    while (Dequeue(Operation, 0))
    {
        // Just remove all operations
    }
    
    // Clean up any retired nodes
    ProcessRetiredNodes(true);
}

int32 FThreadSafeOperationQueue::GetCurrentThreadId()
{
    // Get thread ID from TLS or assign a new one
    int32 ThreadId = (int32)(UPTRINT)FPlatformTLS::GetTlsValue(ThreadLocalIdTLS);
    
    if (ThreadId == 0)
    {
        // Assign a new thread ID (simple incrementing counter is sufficient for our needs)
        static std::atomic<int32> NextThreadId(1);
        ThreadId = NextThreadId.fetch_add(1);
        
        // Store in TLS
        FPlatformTLS::SetTlsValue(ThreadLocalIdTLS, (void*)(UPTRINT)ThreadId);
    }
    
    return ThreadId;
}

void* FThreadSafeOperationQueue::AcquireHazardPointer(const void* Pointer, int32 SlotIndex)
{
    // Validate slot index
    if (SlotIndex < 0 || SlotIndex >= MAX_HAZARD_POINTERS_PER_THREAD)
    {
        return nullptr;
    }
    
    // Get thread ID
    int32 ThreadId = GetCurrentThreadId();
    
    // Register the hazard pointer
    FScopeLock Lock(&GHazardPointerLock);
    
    // Look for existing slot for this thread/index
    for (int32 i = 0; i < GHazardPointers.Num(); ++i)
    {
        FHazardPointerRecord& Record = GHazardPointers[i];
        
        if (Record.ThreadId == ThreadId && Record.SlotIndex == SlotIndex)
        {
            Record.Pointer = Pointer;
            return const_cast<void*>(Pointer);
        }
    }
    
    // Create new hazard pointer record
    FHazardPointerRecord NewRecord;
    NewRecord.Pointer = Pointer;
    NewRecord.ThreadId = ThreadId;
    NewRecord.SlotIndex = SlotIndex;
    GHazardPointers.Add(NewRecord);
    
    return const_cast<void*>(Pointer);
}

void FThreadSafeOperationQueue::ReleaseHazardPointer(int32 SlotIndex)
{
    // Validate slot index
    if (SlotIndex < 0 || SlotIndex >= MAX_HAZARD_POINTERS_PER_THREAD)
    {
        return;
    }
    
    // Get thread ID
    int32 ThreadId = GetCurrentThreadId();
    
    // Clear the hazard pointer
    FScopeLock Lock(&GHazardPointerLock);
    
    for (int32 i = 0; i < GHazardPointers.Num(); ++i)
    {
        FHazardPointerRecord& Record = GHazardPointers[i];
        
        if (Record.ThreadId == ThreadId && Record.SlotIndex == SlotIndex)
        {
            Record.Pointer = nullptr;
            break;
        }
    }
}

bool FThreadSafeOperationQueue::IsPointerHazardous(const void* Pointer)
{
    // Check if any thread has this pointer as a hazard pointer
    FScopeLock Lock(&GHazardPointerLock);
    
    for (const FHazardPointerRecord& Record : GHazardPointers)
    {
        if (Record.Pointer == Pointer)
        {
            return true;
        }
    }
    
    return false;
}

bool FThreadSafeOperationQueue::Enqueue(const FQueuedOperation& Op, uint32 TimeoutMs)
{
    // Check if queue is closed
    if (bIsClosed)
    {
        Stats.EnqueueBlockedCount++;
        return false;
    }
    
    double StartTime = FPlatformTime::Seconds();
    bool bAcquiredLock = false;
    
    // Try to acquire lock with timeout
    if (TimeoutMs == 0)
    {
        // No timeout, try once
        bAcquiredLock = QueueLock.TryLock();
    }
    else
    {
        // With timeout, try repeatedly
        double EndTimeSeconds = StartTime + TimeoutMs / 1000.0;
        
        while (!bAcquiredLock && FPlatformTime::Seconds() < EndTimeSeconds)
        {
            bAcquiredLock = QueueLock.TryLock();
            
            if (!bAcquiredLock)
            {
                // Back off slightly and try again
                FPlatformProcess::Sleep(0.0001f);
            }
        }
    }
    
    if (!bAcquiredLock)
    {
        // Failed to acquire lock within timeout
        Stats.EnqueueBlockedCount++;
        double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
        Stats.EnqueueBlockTimeMs += ElapsedMs;
        return false;
    }
    
    // Lock acquired
    bool bEnqueued = false;
    
    // Check again if queue is closed
    if (!bIsClosed)
    {
        // Add the operation to the queue
        Operations.Add(Op);
        
        // Update statistics
        Stats.EnqueueCount++;
        Stats.PeakQueueSize = FMath::Max(Stats.PeakQueueSize, Operations.Num());
        
        bEnqueued = true;
    }
    else
    {
        Stats.EnqueueBlockedCount++;
    }
    
    // Signal new item available
    QueueEvent.Trigger();
    
    // Release the lock
    QueueLock.Unlock();
    
    // Calculate block time if enqueue failed
    if (!bEnqueued)
    {
        double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
        Stats.EnqueueBlockTimeMs += ElapsedMs;
    }
    
    return bEnqueued;
}

bool FThreadSafeOperationQueue::EnqueueBatch(const TArray<FQueuedOperation>& Ops, uint32 TimeoutMs)
{
    // Check if queue is closed or no operations to enqueue
    if (bIsClosed || Ops.Num() == 0)
    {
        if (bIsClosed)
        {
            Stats.EnqueueBlockedCount++;
        }
        return false;
    }
    
    double StartTime = FPlatformTime::Seconds();
    bool bAcquiredLock = false;
    
    // Try to acquire lock with timeout
    if (TimeoutMs == 0)
    {
        // No timeout, try once
        bAcquiredLock = QueueLock.TryLock();
    }
    else
    {
        // With timeout, try repeatedly
        double EndTimeSeconds = StartTime + TimeoutMs / 1000.0;
        
        while (!bAcquiredLock && FPlatformTime::Seconds() < EndTimeSeconds)
        {
            bAcquiredLock = QueueLock.TryLock();
            
            if (!bAcquiredLock)
            {
                // Back off slightly and try again
                FPlatformProcess::Sleep(0.0001f);
            }
        }
    }
    
    if (!bAcquiredLock)
    {
        // Failed to acquire lock within timeout
        Stats.EnqueueBlockedCount++;
        double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
        Stats.EnqueueBlockTimeMs += ElapsedMs;
        return false;
    }
    
    // Lock acquired
    bool bEnqueued = false;
    
    // Check again if queue is closed
    if (!bIsClosed)
    {
        // Reserve space for the new operations
        const int32 OriginalNum = Operations.Num();
        Operations.Reserve(OriginalNum + Ops.Num());
        
        // Add the operations to the queue
        for (const FQueuedOperation& Op : Ops)
        {
            Operations.Add(Op);
        }
        
        // Update statistics
        Stats.EnqueueCount += Ops.Num();
        Stats.PeakQueueSize = FMath::Max(Stats.PeakQueueSize, Operations.Num());
        
        bEnqueued = true;
    }
    else
    {
        Stats.EnqueueBlockedCount++;
    }
    
    // Signal new items available
    QueueEvent.Trigger();
    
    // Release the lock
    QueueLock.Unlock();
    
    // Calculate block time if enqueue failed
    if (!bEnqueued)
    {
        double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
        Stats.EnqueueBlockTimeMs += ElapsedMs;
    }
    
    return bEnqueued;
}

bool FThreadSafeOperationQueue::Dequeue(FQueuedOperation& OutOp, uint32 TimeoutMs)
{
    // Check if multiple consumers are allowed
    if (!bAllowMultipleConsumers)
    {
        // Get current thread ID
        int32 ThreadId = GetCurrentThreadId();
        
        // Check if another thread is already consuming
        if (CurrentConsumerThreadId != 0 && CurrentConsumerThreadId != ThreadId)
        {
            Stats.DequeueBlockedCount++;
            return false;
        }
    }
    
    double StartTime = FPlatformTime::Seconds();
    bool bGotOperation = false;
    
    // Try to get an operation
    while (!bGotOperation)
    {
        // Check if queue is empty
        bool bIsEmpty = true;
        
        {
            FScopeLock Lock(&QueueLock);
            
            // Check if we have operations
            if (Operations.Num() > 0)
            {
                // Get the next operation
                OutOp = Operations[0];
                Operations.RemoveAt(0, 1, false); // Don't shrink the array
                
                // Update statistics
                Stats.DequeueCount++;
                
                // Set current consumer thread ID
                if (!bAllowMultipleConsumers)
                {
                    CurrentConsumerThreadId = GetCurrentThreadId();
                }
                
                bGotOperation = true;
                bIsEmpty = false;
            }
            else
            {
                // Queue is empty
                bIsEmpty = true;
                
                // If queue is closed and empty, return failure
                if (bIsClosed)
                {
                    Stats.DequeueBlockedCount++;
                    double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
                    Stats.DequeueBlockTimeMs += ElapsedMs;
                    return false;
                }
            }
        }
        
        if (!bGotOperation)
        {
            // If timeout is 0, don't wait
            if (TimeoutMs == 0)
            {
                Stats.DequeueBlockedCount++;
                double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
                Stats.DequeueBlockTimeMs += ElapsedMs;
                return false;
            }
            
            // Check if we've timed out
            double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
            if (ElapsedMs >= TimeoutMs)
            {
                Stats.DequeueBlockedCount++;
                Stats.DequeueBlockTimeMs += ElapsedMs;
                return false;
            }
            
            // Wait for signal with remaining timeout
            uint32 RemainingTimeoutMs = static_cast<uint32>(TimeoutMs - ElapsedMs);
            if (bIsEmpty)
            {
                // Wait for a signal that new items are available
                QueueEvent.Wait(FMath::Min(RemainingTimeoutMs, 1u)); // Wait at most 1ms at a time
            }
        }
    }
    
    // Process any retired nodes periodically
    if (Stats.DequeueCount % 100 == 0)
    {
        ProcessRetiredNodes(false);
    }
    
    return true;
}

bool FThreadSafeOperationQueue::DequeueBatch(TArray<FQueuedOperation>& OutOps, int32 MaxOperations, uint32 TimeoutMs)
{
    // Initialize output array
    OutOps.Reset();
    
    if (MaxOperations <= 0)
    {
        return false;
    }
    
    // Check if multiple consumers are allowed
    if (!bAllowMultipleConsumers)
    {
        // Get current thread ID
        int32 ThreadId = GetCurrentThreadId();
        
        // Check if another thread is already consuming
        if (CurrentConsumerThreadId != 0 && CurrentConsumerThreadId != ThreadId)
        {
            Stats.DequeueBlockedCount++;
            return false;
        }
    }
    
    double StartTime = FPlatformTime::Seconds();
    bool bGotOperations = false;
    
    // Try to get operations
    while (!bGotOperations)
    {
        // Check if queue is empty
        bool bIsEmpty = true;
        
        {
            FScopeLock Lock(&QueueLock);
            
            // Check if we have operations
            int32 AvailableOps = Operations.Num();
            if (AvailableOps > 0)
            {
                // Determine how many operations to dequeue
                int32 OpsToDequeue = FMath::Min(AvailableOps, MaxOperations);
                
                // Reserve space in output array
                OutOps.Reserve(OpsToDequeue);
                
                // Get the operations
                for (int32 i = 0; i < OpsToDequeue; ++i)
                {
                    OutOps.Add(Operations[i]);
                }
                
                // Remove the operations from the queue
                Operations.RemoveAt(0, OpsToDequeue, false); // Don't shrink the array
                
                // Update statistics
                Stats.DequeueCount += OpsToDequeue;
                
                // Set current consumer thread ID
                if (!bAllowMultipleConsumers)
                {
                    CurrentConsumerThreadId = GetCurrentThreadId();
                }
                
                bGotOperations = true;
                bIsEmpty = false;
            }
            else
            {
                // Queue is empty
                bIsEmpty = true;
                
                // If queue is closed and empty, return failure
                if (bIsClosed)
                {
                    Stats.DequeueBlockedCount++;
                    double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
                    Stats.DequeueBlockTimeMs += ElapsedMs;
                    return false;
                }
            }
        }
        
        if (!bGotOperations)
        {
            // If timeout is 0, don't wait
            if (TimeoutMs == 0)
            {
                Stats.DequeueBlockedCount++;
                double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
                Stats.DequeueBlockTimeMs += ElapsedMs;
                return false;
            }
            
            // Check if we've timed out
            double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
            if (ElapsedMs >= TimeoutMs)
            {
                Stats.DequeueBlockedCount++;
                Stats.DequeueBlockTimeMs += ElapsedMs;
                return false;
            }
            
            // Wait for signal with remaining timeout
            uint32 RemainingTimeoutMs = static_cast<uint32>(TimeoutMs - ElapsedMs);
            if (bIsEmpty)
            {
                // Wait for a signal that new items are available
                QueueEvent.Wait(FMath::Min(RemainingTimeoutMs, 10u)); // Wait at most 10ms at a time
            }
        }
    }
    
    // Process any retired nodes periodically
    ProcessRetiredNodes(false);
    
    return (OutOps.Num() > 0);
}

void FThreadSafeOperationQueue::Close()
{
    // Mark queue as closed
    FScopeLock Lock(&QueueLock);
    bIsClosed = true;
    
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
    return Operations.Num();
}

bool FThreadSafeOperationQueue::IsEmpty() const
{
    FScopeLock Lock(&QueueLock);
    return (Operations.Num() == 0);
}

FQueueStats FThreadSafeOperationQueue::GetStats() const
{
    return Stats;
}

void FThreadSafeOperationQueue::ResetConsumer()
{
    // Reset consumer thread ID to allow another thread to consume
    if (!bAllowMultipleConsumers)
    {
        CurrentConsumerThreadId = 0;
    }
}

void FThreadSafeOperationQueue::RetireNode(void* Pointer)
{
    if (Pointer)
    {
        FScopeLock Lock(&RetiredNodesLock);
        RetiredNodes.Add(Pointer);
    }
}

void FThreadSafeOperationQueue::ProcessRetiredNodes(bool bForce)
{
    // Process retired nodes only if we have accumulated enough or force is true
    FScopeLock Lock(&RetiredNodesLock);
    
    if (bForce || RetiredNodes.Num() > 100)
    {
        TArray<void*> NodesToDelete;
        
        // Find nodes that are safe to delete
        for (int32 i = 0; i < RetiredNodes.Num(); ++i)
        {
            void* Node = RetiredNodes[i];
            
            if (!IsPointerHazardous(Node))
            {
                NodesToDelete.Add(Node);
                RetiredNodes.RemoveAtSwap(i);
                --i; // Adjust index for the swap removal
            }
        }
        
        // Delete the safe nodes
        for (void* Node : NodesToDelete)
        {
            FMemory::Free(Node);
        }
    }
}