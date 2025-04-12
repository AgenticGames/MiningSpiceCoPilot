// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IThreadSafeQueue.h"
#include "HAL/ThreadSafeCounter.h"
#include "Containers/LockFreeList.h"

/**
 * Thread-safe queue for mining operations
 * Provides efficient concurrent queue operations with atomic access
 * and wait-free implementation for high-performance scenarios
 */
class MININGSPICECOPILOT_API FThreadSafeOperationQueue : public IThreadSafeQueue
{
public:
    /** Constructor */
    FThreadSafeOperationQueue();
    
    /** Destructor */
    virtual ~FThreadSafeOperationQueue();

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

private:
    /** Internal node structure for the queue */
    struct FQueueNode
    {
        /** Item pointer */
        void* Item;
        
        /** Next node in the queue */
        FQueueNode* Next;
        
        /** Constructor */
        FQueueNode() : Item(nullptr), Next(nullptr) {}
        
        /** Constructor with item */
        explicit FQueueNode(void* InItem) : Item(InItem), Next(nullptr) {}
    };

    /** Whether the queue has been initialized */
    bool bIsInitialized;
    
    /** Whether the queue has been closed */
    FThreadSafeCounter bClosed;
    
    /** Maximum capacity (0 for unlimited) */
    int32 Capacity;
    
    /** Current size */
    FThreadSafeCounter Size;
    
    /** Head node (for dequeue) */
    FQueueNode* Head;
    
    /** Tail node (for enqueue) */
    FQueueNode* Tail;
    
    /** Lock for enqueue operations */
    mutable FCriticalSection EnqueueLock;
    
    /** Lock for dequeue operations */
    mutable FCriticalSection DequeueLock;
    
    /** Pre-allocated node pool for lock-free operation */
    TLockFreeFixedSizeAllocator<FQueueNode> NodePool;
    
    /** Free list for recycling nodes */
    TLockFreeClassAllocator<FQueueNode> FreeList;
    
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
    
    /** Allocates a new node from the pool */
    FQueueNode* AllocateNode(void* Item = nullptr);
    
    /** Frees a node back to the pool */
    void FreeNode(FQueueNode* Node);
    
    /** Waits until the queue is not full or timeout */
    bool WaitNotFull(uint32 TimeoutMs, double& OutWaitTimeMs);
    
    /** Waits until the queue is not empty or timeout */
    bool WaitNotEmpty(uint32 TimeoutMs, double& OutWaitTimeMs);

    /** Singleton instance */
    static FThreadSafeOperationQueue* Instance;
};