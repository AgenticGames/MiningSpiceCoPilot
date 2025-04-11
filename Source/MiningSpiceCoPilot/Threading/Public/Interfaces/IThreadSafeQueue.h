// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IThreadSafeQueue.generated.h"

/**
 * Queue operation results
 */
enum class EQueueResult : uint8
{
    /** Operation was successful */
    Success,
    
    /** Queue is full */
    QueueFull,
    
    /** Queue is empty */
    QueueEmpty,
    
    /** Operation timed out */
    Timeout,
    
    /** Queue is closed and cannot accept or provide items */
    QueueClosed,
    
    /** Operation failed for another reason */
    Error
};

/**
 * Queue statistics structure
 */
struct MININGSPICECOPILOT_API FQueueStats
{
    /** Current number of items in the queue */
    int32 CurrentSize;
    
    /** Maximum capacity of the queue */
    int32 Capacity;
    
    /** Total number of items enqueued since creation or last reset */
    uint64 TotalEnqueued;
    
    /** Total number of items dequeued since creation or last reset */
    uint64 TotalDequeued;
    
    /** Number of enqueue operations that returned QueueFull */
    uint64 EnqueueFailures;
    
    /** Number of dequeue operations that returned QueueEmpty */
    uint64 DequeueFailures;
    
    /** Number of timed out operations */
    uint64 TimeoutCount;
    
    /** Peak queue size since creation or last reset */
    int32 PeakSize;
    
    /** Average wait time for enqueue operations in milliseconds */
    double AverageEnqueueWaitTimeMs;
    
    /** Average wait time for dequeue operations in milliseconds */
    double AverageDequeueWaitTimeMs;
    
    /** Whether the queue is currently closed */
    bool bIsClosed;
    
    /** Default constructor initializes all values to zero */
    FQueueStats()
        : CurrentSize(0)
        , Capacity(0)
        , TotalEnqueued(0)
        , TotalDequeued(0)
        , EnqueueFailures(0)
        , DequeueFailures(0)
        , TimeoutCount(0)
        , PeakSize(0)
        , AverageEnqueueWaitTimeMs(0.0)
        , AverageDequeueWaitTimeMs(0.0)
        , bIsClosed(false)
    {
    }
};

/**
 * Base interface for thread-safe queues in the SVO+SDF mining architecture
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UThreadSafeQueue : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for thread-safe queues in the SVO+SDF mining architecture
 * Provides efficient concurrent queue operations for mining system components
 */
class MININGSPICECOPILOT_API IThreadSafeQueue
{
    GENERATED_BODY()

public:
    /**
     * Initializes the queue with the specified capacity
     * @param InCapacity Maximum number of items the queue can hold (0 for unlimited)
     * @return True if initialization was successful
     */
    virtual bool Initialize(int32 InCapacity = 0) = 0;
    
    /**
     * Shuts down the queue and cleans up resources
     */
    virtual void Shutdown() = 0;
    
    /**
     * Checks if the queue has been initialized
     * @return True if initialized, false otherwise
     */
    virtual bool IsInitialized() const = 0;
    
    /**
     * Attempts to enqueue an item
     * @param Item Item to enqueue
     * @return Result of the operation
     */
    virtual EQueueResult Enqueue(void* Item) = 0;
    
    /**
     * Attempts to enqueue an item with a timeout
     * @param Item Item to enqueue
     * @param TimeoutMs Maximum time to wait in milliseconds (0 for no wait)
     * @return Result of the operation
     */
    virtual EQueueResult EnqueueWithTimeout(void* Item, uint32 TimeoutMs) = 0;
    
    /**
     * Attempts to dequeue an item
     * @param OutItem Receives the dequeued item
     * @return Result of the operation
     */
    virtual EQueueResult Dequeue(void*& OutItem) = 0;
    
    /**
     * Attempts to dequeue an item with a timeout
     * @param OutItem Receives the dequeued item
     * @param TimeoutMs Maximum time to wait in milliseconds (0 for no wait)
     * @return Result of the operation
     */
    virtual EQueueResult DequeueWithTimeout(void*& OutItem, uint32 TimeoutMs) = 0;
    
    /**
     * Attempts to peek at the next item without removing it
     * @param OutItem Receives the item at the front of the queue
     * @return Result of the operation
     */
    virtual EQueueResult Peek(void*& OutItem) const = 0;
    
    /**
     * Checks if the queue is empty
     * @return True if the queue is empty, false otherwise
     */
    virtual bool IsEmpty() const = 0;
    
    /**
     * Checks if the queue is full
     * @return True if the queue is full, false otherwise
     */
    virtual bool IsFull() const = 0;
    
    /**
     * Gets the current size of the queue
     * @return Number of items in the queue
     */
    virtual int32 GetSize() const = 0;
    
    /**
     * Gets the maximum capacity of the queue
     * @return Maximum number of items the queue can hold (0 for unlimited)
     */
    virtual int32 GetCapacity() const = 0;
    
    /**
     * Clears all items from the queue
     */
    virtual void Clear() = 0;
    
    /**
     * Gets statistics for this queue
     * @return Queue statistics
     */
    virtual FQueueStats GetStats() const = 0;
    
    /**
     * Resets the statistics for this queue
     */
    virtual void ResetStats() = 0;
    
    /**
     * Closes the queue, preventing further enqueue operations
     * Dequeue operations can still drain the queue
     */
    virtual void Close() = 0;
    
    /**
     * Checks if the queue is closed
     * @return True if the queue is closed, false otherwise
     */
    virtual bool IsClosed() const = 0;
    
    /**
     * Sets the capacity of the queue
     * @param NewCapacity New maximum capacity (0 for unlimited)
     * @return True if the capacity was changed, false if items would be lost
     */
    virtual bool SetCapacity(int32 NewCapacity) = 0;
    
    /**
     * Attempts to enqueue multiple items
     * @param Items Array of items to enqueue
     * @param Count Number of items to enqueue
     * @return Number of items successfully enqueued
     */
    virtual int32 EnqueueBatch(void** Items, int32 Count) = 0;
    
    /**
     * Attempts to dequeue multiple items
     * @param OutItems Array to receive dequeued items
     * @param MaxCount Maximum number of items to dequeue
     * @return Number of items successfully dequeued
     */
    virtual int32 DequeueBatch(void** OutItems, int32 MaxCount) = 0;
    
    /**
     * Gets the singleton instance
     * @return Reference to the thread-safe queue
     */
    static IThreadSafeQueue& Get();
};

/**
 * Templated wrapper for the thread-safe queue to provide type safety
 */
template<typename T>
class TThreadSafeQueue
{
public:
    TThreadSafeQueue(IThreadSafeQueue* InQueue)
        : Queue(InQueue)
    {
    }
    
    bool Initialize(int32 InCapacity = 0)
    {
        return Queue->Initialize(InCapacity);
    }
    
    void Shutdown()
    {
        Queue->Shutdown();
    }
    
    bool IsInitialized() const
    {
        return Queue->IsInitialized();
    }
    
    EQueueResult Enqueue(const T& Item)
    {
        return Queue->Enqueue((void*)&Item);
    }
    
    EQueueResult EnqueueWithTimeout(const T& Item, uint32 TimeoutMs)
    {
        return Queue->EnqueueWithTimeout((void*)&Item, TimeoutMs);
    }
    
    EQueueResult Dequeue(T& OutItem)
    {
        void* Item = nullptr;
        EQueueResult Result = Queue->Dequeue(Item);
        if (Result == EQueueResult::Success)
        {
            OutItem = *static_cast<T*>(Item);
        }
        return Result;
    }
    
    EQueueResult DequeueWithTimeout(T& OutItem, uint32 TimeoutMs)
    {
        void* Item = nullptr;
        EQueueResult Result = Queue->DequeueWithTimeout(Item, TimeoutMs);
        if (Result == EQueueResult::Success)
        {
            OutItem = *static_cast<T*>(Item);
        }
        return Result;
    }
    
    EQueueResult Peek(T& OutItem) const
    {
        void* Item = nullptr;
        EQueueResult Result = Queue->Peek(Item);
        if (Result == EQueueResult::Success)
        {
            OutItem = *static_cast<T*>(Item);
        }
        return Result;
    }
    
    bool IsEmpty() const
    {
        return Queue->IsEmpty();
    }
    
    bool IsFull() const
    {
        return Queue->IsFull();
    }
    
    int32 GetSize() const
    {
        return Queue->GetSize();
    }
    
    int32 GetCapacity() const
    {
        return Queue->GetCapacity();
    }
    
    void Clear()
    {
        Queue->Clear();
    }
    
    FQueueStats GetStats() const
    {
        return Queue->GetStats();
    }
    
    void ResetStats()
    {
        Queue->ResetStats();
    }
    
    void Close()
    {
        Queue->Close();
    }
    
    bool IsClosed() const
    {
        return Queue->IsClosed();
    }
    
    bool SetCapacity(int32 NewCapacity)
    {
        return Queue->SetCapacity(NewCapacity);
    }
    
    int32 EnqueueBatch(const T* Items, int32 Count)
    {
        return Queue->EnqueueBatch((void**)Items, Count);
    }
    
    int32 DequeueBatch(T* OutItems, int32 MaxCount)
    {
        return Queue->DequeueBatch((void**)OutItems, MaxCount);
    }

private:
    IThreadSafeQueue* Queue;
};
