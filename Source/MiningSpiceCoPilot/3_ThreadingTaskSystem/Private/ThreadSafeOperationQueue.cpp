#include "ThreadSafeOperationQueue.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformAtomics.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"
#include "Math/UnrealMathSSE.h"
#include "Misc/AssertionMacros.h"
#include "Containers/LockFreeList.h"
#include "Containers/LockFreeFixedSizeAllocator.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Stats/Stats.h"

// Performance profiling categories
DECLARE_CYCLE_STAT(TEXT("TSOperationQueue_Enqueue"), STAT_TSOperationQueue_Enqueue, STATGROUP_Threading);
DECLARE_CYCLE_STAT(TEXT("TSOperationQueue_Dequeue"), STAT_TSOperationQueue_Dequeue, STATGROUP_Threading);
DECLARE_CYCLE_STAT(TEXT("TSOperationQueue_EnqueueBatch"), STAT_TSOperationQueue_EnqueueBatch, STATGROUP_Threading);
DECLARE_CYCLE_STAT(TEXT("TSOperationQueue_DequeueBatch"), STAT_TSOperationQueue_DequeueBatch, STATGROUP_Threading);
DECLARE_CYCLE_STAT(TEXT("TSOperationQueue_SIMD"), STAT_TSOperationQueue_SIMD, STATGROUP_Threading);
DECLARE_CYCLE_STAT(TEXT("TSOperationQueue_CacheOptimized"), STAT_TSOperationQueue_CacheOptimized, STATGROUP_Threading);

CSV_DECLARE_CATEGORY_MODULE_EXTERN(MININGSPICECOPILOT_API, Threading);

// Constants unique to this implementation
static const int32 MIN_NODE_POOL_SIZE = 256;
static const int32 CONTENTION_BACKOFF_MIN_US = 1;
static const int32 CONTENTION_BACKOFF_MAX_US = 32;

// Static singleton instance
FThreadSafeOperationQueue* FThreadSafeOperationQueue::Instance = nullptr;

FThreadSafeOperationQueue::FThreadSafeOperationQueue()
    : bIsInitialized(false)
    , Capacity(0)
    , bUseAgeBasedPromotion(false)
    , AgePromotionThresholdMs(1000)
    , bUseSIMDOptimization(true)
    , bUseCacheOptimization(true)
    , bTrackMemoryUsage(false)
    , ProcessorAffinityMask(0)
    , Head(nullptr)
    , Tail(nullptr)
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
    , TotalLatencyMs(0.0)
    , LatencySamples(0)
    , MaxObservedLatencyMs(0.0)
    , TotalMemoryTracked(0)
    , SIMDBatchCount(0)
    , CacheOptimizedCount(0)
    , ContentionCount(0)
    , TotalOperationsCount(0)
    , PerformanceTimestamp(0.0)
{
    // Initialize atomic counter
    Size.Set(0);
    bClosed.Set(0);
    
    // Initialize performance timestamp
    PerformanceTimestamp = FPlatformTime::Seconds();
}

FThreadSafeOperationQueue::~FThreadSafeOperationQueue()
{
    Shutdown();
}

bool FThreadSafeOperationQueue::Initialize(int32 InCapacity)
{
    if (bIsInitialized)
    {
        return true;
    }

    Capacity = InCapacity;
    
    // Initialize node allocator - using constructor instead of calling Init
    const uint32 ElementSize = POOL_ALLOCATOR_ELEMENT_SIZE;
    const uint32 ElementCount = FMath::Max(MIN_NODE_POOL_SIZE, Capacity > 0 ? Capacity * 2 : 4096);
    
    // No need to call Init explicitly, allocator is initialized by constructor
    
    // Setup initial dummy node
    FQueueNode* DummyNode = AllocateNode();
    check(DummyNode);
    DummyNode->Descriptor.Payload = nullptr;
    DummyNode->Next = nullptr;

    // Ensure Head and Tail are properly aligned
    Head = DummyNode;
    Tail = DummyNode;

    bIsInitialized = true;
    
    // Reset performance tracking
    PerformanceTimestamp = FPlatformTime::Seconds();
    
    return true;
}

void FThreadSafeOperationQueue::Shutdown()
{
    if (!bIsInitialized)
    {
        return;
    }

    // Mark queue as closed to prevent new operations
    Close();
    
    // Drain the queue
    void* Item = nullptr;
    while (Dequeue(Item) == EQueueResult::Success)
    {
        // Just remove all items
    }
    
    // Free the remaining dummy node
    if (Head)
    {
        FreeNode(Head);
        Head = nullptr;
        Tail = nullptr;
    }

    bIsInitialized = false;
}

bool FThreadSafeOperationQueue::IsInitialized() const
{
    return bIsInitialized;
}

void FThreadSafeOperationQueue::SetAgeBasedPromotion(bool bEnable, uint32 AgeThresholdMs)
{
    bUseAgeBasedPromotion = bEnable;
    AgePromotionThresholdMs = AgeThresholdMs;
}

void FThreadSafeOperationQueue::SetProcessorAffinity(uint64 AffinityMask)
{
    ProcessorAffinityMask = AffinityMask;
}

void FThreadSafeOperationQueue::SetSIMDOptimization(bool bEnable)
{
    bUseSIMDOptimization = bEnable;
}

void FThreadSafeOperationQueue::SetCacheOptimization(bool bEnable)
{
    bUseCacheOptimization = bEnable;
}

void FThreadSafeOperationQueue::SetMemoryTracking(bool bEnable)
{
    bTrackMemoryUsage = bEnable;
}

FOperationDescriptor FThreadSafeOperationQueue::PrepareOperationDescriptor(void* Item, EOperationType Type, int32 SizeBytes, bool bSIMDCompatible, uint8 CacheLocalityHint)
{
    FOperationDescriptor Descriptor;
    Descriptor.Payload = Item;
    Descriptor.Type = Type;
    Descriptor.EnqueueTime = FPlatformTime::Seconds();
    Descriptor.SizeBytes = SizeBytes;
    Descriptor.OperationId = static_cast<uint64>(FPlatformAtomics::InterlockedIncrement(reinterpret_cast<volatile int64*>(&TotalOperationsCount)));
    Descriptor.bSIMDCompatible = bSIMDCompatible;
    Descriptor.CacheLocalityHint = CacheLocalityHint;
    
    return Descriptor;
}

EQueueResult FThreadSafeOperationQueue::Enqueue(void* Item)
{
    SCOPE_CYCLE_COUNTER(STAT_TSOperationQueue_Enqueue);
    
    if (!bIsInitialized)
    {
        return EQueueResult::Error;
    }

    if (bClosed.GetValue() != 0)
    {
        return EQueueResult::QueueClosed;
    }

    if (Capacity > 0 && Size.GetValue() >= Capacity)
    {
        UpdateEnqueueStats(false);
        return EQueueResult::QueueFull;
    }

    // Allocate a new node
    FQueueNode* NewNode = AllocateNode(Item);
    if (!NewNode)
    {
        return EQueueResult::Error;
    }
    
    // Set timestamp for latency tracking
    NewNode->Descriptor.EnqueueTime = FPlatformTime::Seconds();

    // Use spin lock for reduced contention
    EnqueueLock.Lock();

    // Link the new node at the end of the queue
    Tail->Next = NewNode;
    Tail = NewNode;

    EnqueueLock.Unlock();

    // Update size and stats atomically
    Size.Increment();
    UpdateEnqueueStats(true);

    return EQueueResult::Success;
}

EQueueResult FThreadSafeOperationQueue::EnqueueWithMetadata(void* Item, EOperationType Type, int32 SizeBytes, bool bSIMDCompatible, uint8 CacheLocalityHint)
{
    SCOPE_CYCLE_COUNTER(STAT_TSOperationQueue_Enqueue);
    
    if (!bIsInitialized)
    {
        return EQueueResult::Error;
    }

    if (bClosed.GetValue() != 0)
    {
        return EQueueResult::QueueClosed;
    }

    if (Capacity > 0 && Size.GetValue() >= Capacity)
    {
        UpdateEnqueueStats(false);
        return EQueueResult::QueueFull;
    }

    // Allocate a new node
    FQueueNode* NewNode = AllocateNode(Item);
    if (!NewNode)
    {
        return EQueueResult::Error;
    }
    
    // Fill in the descriptor with metadata
    NewNode->Descriptor = PrepareOperationDescriptor(Item, Type, SizeBytes, bSIMDCompatible, CacheLocalityHint);

    // Use spin lock for reduced contention
    EnqueueLock.Lock();

    // Link the new node at the end of the queue
    Tail->Next = NewNode;
    Tail = NewNode;

    EnqueueLock.Unlock();

    // Update size and stats atomically
    Size.Increment();
    UpdateEnqueueStats(true, 0.0, 1, sizeof(FQueueNode));

    return EQueueResult::Success;
}

EQueueResult FThreadSafeOperationQueue::EnqueueWithTimeout(void* Item, uint32 TimeoutMs)
{
    SCOPE_CYCLE_COUNTER(STAT_TSOperationQueue_Enqueue);
    
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
    if (Capacity > 0 && Size.GetValue() >= Capacity)
    {
        // Wait for space or timeout
        if (!WaitNotFull(TimeoutMs, WaitTimeMs))
        {
            UpdateEnqueueStats(false, WaitTimeMs);
            TimeoutCount++;
            return EQueueResult::Timeout;
        }
    }

    // Allocate a new node
    FQueueNode* NewNode = AllocateNode(Item);
    if (!NewNode)
    {
        return EQueueResult::Error;
    }
    
    // Set timestamp for latency tracking
    NewNode->Descriptor.EnqueueTime = FPlatformTime::Seconds();

    // Try to acquire the lock with exponential backoff
    bool bLockAcquired = false;
    uint32 BackoffTime = CONTENTION_BACKOFF_MIN_US;
    
    while (!bLockAcquired)
    {
        if (EnqueueLock.TryLock())
        {
            bLockAcquired = true;
        }
        else
        {
            // We encountered contention, update stats
            UpdateContentionStats();
            
            // Back off exponentially to reduce contention
            FPlatformProcess::SleepNoStats(BackoffTime / 1000000.0f);
            BackoffTime = FMath::Min<int32>(BackoffTime * 2, CONTENTION_BACKOFF_MAX_US);
        }
    }

    // Link the new node at the end of the queue
    Tail->Next = NewNode;
    Tail = NewNode;

    EnqueueLock.Unlock();

    // Update size and stats atomically
    Size.Increment();
    UpdateEnqueueStats(true, WaitTimeMs);

    return EQueueResult::Success;
}

EQueueResult FThreadSafeOperationQueue::Dequeue(void*& OutItem)
{
    SCOPE_CYCLE_COUNTER(STAT_TSOperationQueue_Dequeue);
    
    if (!bIsInitialized)
    {
        return EQueueResult::Error;
    }

    if (Size.GetValue() == 0)
    {
        UpdateDequeueStats(false);
        return EQueueResult::QueueEmpty;
    }

    // Use spin lock for dequeue operations
    DequeueLock.Lock();

    // If queue is empty, return failure
    if (Head->Next == nullptr)
    {
        DequeueLock.Unlock();
        UpdateDequeueStats(false);
        return EQueueResult::QueueEmpty;
    }

    // Save item pointer
    FQueueNode* OldHead = Head;
    FQueueNode* NewHead = Head->Next;
    
    // Get the item and calculate latency if tracking is enabled
    OutItem = NewHead->Descriptor.Payload;
    double LatencyMs = 0.0;
    
    if (NewHead->Descriptor.EnqueueTime > 0.0)
    {
        double CurrentTime = FPlatformTime::Seconds();
        LatencyMs = (CurrentTime - NewHead->Descriptor.EnqueueTime) * 1000.0; // Convert to ms
    }
    
    Head = NewHead;
    DequeueLock.Unlock();

    // Free the old head (dummy) node
    FreeNode(OldHead);

    // Update size and stats
    Size.Decrement();
    UpdateDequeueStats(true, 0.0, LatencyMs);

    return EQueueResult::Success;
}

EQueueResult FThreadSafeOperationQueue::DequeueWithMetadata(FOperationDescriptor& OutDescriptor)
{
    SCOPE_CYCLE_COUNTER(STAT_TSOperationQueue_Dequeue);
    
    if (!bIsInitialized)
    {
        return EQueueResult::Error;
    }

    if (Size.GetValue() == 0)
    {
        UpdateDequeueStats(false);
        return EQueueResult::QueueEmpty;
    }

    // Use spin lock for dequeue operations
    DequeueLock.Lock();

    // If queue is empty, return failure
    if (Head->Next == nullptr)
    {
        DequeueLock.Unlock();
        UpdateDequeueStats(false);
        return EQueueResult::QueueEmpty;
    }

    // Save item pointer and descriptor
    FQueueNode* OldHead = Head;
    FQueueNode* NewHead = Head->Next;
    
    // Copy the full descriptor
    OutDescriptor = NewHead->Descriptor;
    
    // Calculate latency if timestamp is available
    double LatencyMs = 0.0;
    if (OutDescriptor.EnqueueTime > 0.0)
    {
        double CurrentTime = FPlatformTime::Seconds();
        LatencyMs = (CurrentTime - OutDescriptor.EnqueueTime) * 1000.0; // Convert to ms
    }
    
    Head = NewHead;
    DequeueLock.Unlock();

    // Free the old head (dummy) node
    FreeNode(OldHead);

    // Update size and stats
    Size.Decrement();
    
    // Update memory tracking if enabled
    int32 MemSize = bTrackMemoryUsage ? OutDescriptor.SizeBytes : 0;
    
    // Update stats with extra metadata
    bool bIsSIMD = OutDescriptor.bSIMDCompatible;
    bool bIsCache = (OutDescriptor.CacheLocalityHint > 0);
    UpdateDequeueStats(true, 0.0, LatencyMs, 1, bIsSIMD, bIsCache);

    return EQueueResult::Success;
}

EQueueResult FThreadSafeOperationQueue::DequeueWithTimeout(void*& OutItem, uint32 TimeoutMs)
{
    SCOPE_CYCLE_COUNTER(STAT_TSOperationQueue_Dequeue);
    
    if (!bIsInitialized)
    {
        return EQueueResult::Error;
    }

    // If queue is empty, wait until not empty or timeout
    double WaitTimeMs = 0.0;
    if (Size.GetValue() == 0)
    {
        // Wait for item or timeout
        if (!WaitNotEmpty(TimeoutMs, WaitTimeMs))
        {
            UpdateDequeueStats(false, WaitTimeMs);
            TimeoutCount++;
            return EQueueResult::Timeout;
        }
    }

    // Try to acquire the lock with exponential backoff
    bool bLockAcquired = false;
    uint32 BackoffTime = CONTENTION_BACKOFF_MIN_US;
    
    while (!bLockAcquired)
    {
        if (DequeueLock.TryLock())
        {
            bLockAcquired = true;
        }
        else
        {
            // We encountered contention, update stats
            UpdateContentionStats();
            
            // Back off exponentially to reduce contention
            FPlatformProcess::SleepNoStats(BackoffTime / 1000000.0f);
            BackoffTime = FMath::Min<int32>(BackoffTime * 2, CONTENTION_BACKOFF_MAX_US);
        }
    }

    // If queue is empty, return failure
    if (Head->Next == nullptr)
    {
        DequeueLock.Unlock();
        UpdateDequeueStats(false, WaitTimeMs);
        return EQueueResult::QueueEmpty;
    }

    // Save item pointer
    FQueueNode* OldHead = Head;
    FQueueNode* NewHead = Head->Next;
    
    // Get the item and calculate latency if tracking is enabled
    OutItem = NewHead->Descriptor.Payload;
    double LatencyMs = 0.0;
    
    if (NewHead->Descriptor.EnqueueTime > 0.0)
    {
        double CurrentTime = FPlatformTime::Seconds();
        LatencyMs = (CurrentTime - NewHead->Descriptor.EnqueueTime) * 1000.0; // Convert to ms
    }
    
    Head = NewHead;
    DequeueLock.Unlock();

    // Free the old head (dummy) node
    FreeNode(OldHead);

    // Update size and stats
    Size.Decrement();
    UpdateDequeueStats(true, WaitTimeMs, LatencyMs);

    return EQueueResult::Success;
}

EQueueResult FThreadSafeOperationQueue::Peek(void*& OutItem) const
{
    if (!bIsInitialized)
    {
        return EQueueResult::Error;
    }

    if (Size.GetValue() == 0)
    {
        return EQueueResult::QueueEmpty;
    }

    // Lock to ensure consistent state for peek
    DequeueLock.Lock();

    // If queue is empty, return failure
    if (Head->Next == nullptr)
    {
        DequeueLock.Unlock();
        return EQueueResult::QueueEmpty;
    }

    // Get item from the first real node (after dummy)
    OutItem = Head->Next->Descriptor.Payload;
    DequeueLock.Unlock();
    
    return EQueueResult::Success;
}

bool FThreadSafeOperationQueue::IsEmpty() const
{
    return Size.GetValue() == 0;
}

bool FThreadSafeOperationQueue::IsFull() const
{
    return Capacity > 0 && Size.GetValue() >= Capacity;
}

int32 FThreadSafeOperationQueue::GetSize() const
{
    return Size.GetValue();
}

int32 FThreadSafeOperationQueue::GetCapacity() const
{
    return Capacity;
}

void FThreadSafeOperationQueue::Clear()
{
    if (!bIsInitialized)
    {
        return;
    }

    void* Item = nullptr;
    while (Dequeue(Item) == EQueueResult::Success)
    {
        // Just dequeue and discard
    }
}

FQueueStats FThreadSafeOperationQueue::GetStats() const
{
    FScopeLock Lock(&StatsLock);
    
    FQueueStats Stats;
    Stats.CurrentSize = Size.GetValue();
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

FExtendedQueueStats FThreadSafeOperationQueue::GetExtendedStats() const
{
    FScopeLock Lock(&StatsLock);
    
    // Update performance metrics
    UpdatePerformanceStats();
    
    // Copy base stats
    FQueueStats BaseStats = GetStats();
    FExtendedQueueStats Stats = ExtendedStats;
    
    // Copy basic stats
    Stats.CurrentSize = BaseStats.CurrentSize;
    Stats.Capacity = BaseStats.Capacity;
    Stats.TotalEnqueued = BaseStats.TotalEnqueued;
    Stats.TotalDequeued = BaseStats.TotalDequeued;
    Stats.EnqueueFailures = BaseStats.EnqueueFailures;
    Stats.DequeueFailures = BaseStats.DequeueFailures;
    Stats.TimeoutCount = BaseStats.TimeoutCount;
    Stats.PeakSize = BaseStats.PeakSize;
    Stats.AverageEnqueueWaitTimeMs = BaseStats.AverageEnqueueWaitTimeMs;
    Stats.AverageDequeueWaitTimeMs = BaseStats.AverageDequeueWaitTimeMs;
    Stats.bIsClosed = BaseStats.bIsClosed;
    
    // Calculate extended stats
    Stats.AverageLatencyMs = LatencySamples > 0 ? TotalLatencyMs / LatencySamples : 0.0;
    Stats.MaxLatencyMs = MaxObservedLatencyMs;
    Stats.MemoryUsageBytes = TotalMemoryTracked;
    Stats.SIMDBatchOperations = SIMDBatchCount;
    Stats.CacheOptimizedOperations = CacheOptimizedCount;
    Stats.ContentionRate = TotalOperationsCount > 0 ? (float)ContentionCount / TotalOperationsCount * 100.0f : 0.0f;
    
    return Stats;
}

void FThreadSafeOperationQueue::ResetStats()
{
    FScopeLock Lock(&StatsLock);
    
    TotalEnqueued = 0;
    TotalDequeued = 0;
    EnqueueFailures = 0;
    DequeueFailures = 0;
    TimeoutCount = 0;
    PeakSize = Size.GetValue();
    TotalEnqueueWaitTimeMs = 0.0;
    TotalDequeueWaitTimeMs = 0.0;
    EnqueueWaitCount = 0;
    DequeueWaitCount = 0;
    TotalLatencyMs = 0.0;
    LatencySamples = 0;
    MaxObservedLatencyMs = 0.0;
    TotalMemoryTracked = 0;
    SIMDBatchCount = 0;
    CacheOptimizedCount = 0;
    ContentionCount = 0;
    TotalOperationsCount = 0;
    
    // Reset performance timestamp
    PerformanceTimestamp = FPlatformTime::Seconds();
    
    // Reset extended stats
    ExtendedStats = FExtendedQueueStats();
}

void FThreadSafeOperationQueue::Close()
{
    bClosed.Set(1);
}

bool FThreadSafeOperationQueue::IsClosed() const
{
    return bClosed.GetValue() != 0;
}

bool FThreadSafeOperationQueue::SetCapacity(int32 NewCapacity)
{
    if (NewCapacity < 0)
    {
        return false;
    }
    
    // Can't reduce capacity below current size
    if (NewCapacity > 0 && Size.GetValue() > NewCapacity)
    {
        return false;
    }
    
    Capacity = NewCapacity;
    return true;
}

int32 FThreadSafeOperationQueue::EnqueueBatch(void** Items, int32 Count)
{
    SCOPE_CYCLE_COUNTER(STAT_TSOperationQueue_EnqueueBatch);
    
    if (!bIsInitialized || Count <= 0 || Items == nullptr)
    {
        return 0;
    }
    
    if (bClosed.GetValue() != 0)
    {
        return 0;
    }
    
    // Calculate available capacity
    int32 AvailableSlots = Capacity > 0 ? FMath::Max(0, Capacity - Size.GetValue()) : Count;
    int32 ItemsToEnqueue = FMath::Min(Count, AvailableSlots);
    
    if (ItemsToEnqueue == 0)
    {
        UpdateEnqueueStats(false);
        return 0;
    }
    
    // Try to acquire the lock with exponential backoff
    bool bLockAcquired = false;
    uint32 BackoffTime = CONTENTION_BACKOFF_MIN_US;
    
    while (!bLockAcquired)
    {
        if (EnqueueLock.TryLock())
        {
            bLockAcquired = true;
        }
        else
        {
            // We encountered contention, update stats
            UpdateContentionStats();
            
            // Back off exponentially to reduce contention
            FPlatformProcess::SleepNoStats(BackoffTime / 1000000.0f);
            BackoffTime = FMath::Min<int32>(BackoffTime * 2, CONTENTION_BACKOFF_MAX_US);
        }
    }
    
    // Get current timestamp for latency tracking
    double CurrentTime = FPlatformTime::Seconds();
    
    // SIMD optimized batch creation - process items in SIMD_BATCH_SIZE chunks when possible
    int32 EnqueuedCount = 0;
    int32 MemorySize = 0;
    
    for (int32 BatchStart = 0; BatchStart < ItemsToEnqueue; BatchStart += SIMD_BATCH_SIZE)
    {
        int32 BatchEnd = FMath::Min(BatchStart + SIMD_BATCH_SIZE, ItemsToEnqueue);
        int32 BatchSize = BatchEnd - BatchStart;
        
        if (BatchSize == SIMD_BATCH_SIZE && bUseSIMDOptimization)
        {
            // SIMD batch processing - allocate and prepare nodes in parallel
            TArray<FQueueNode*> BatchNodes;
            BatchNodes.SetNumUninitialized(SIMD_BATCH_SIZE);
            
            for (int32 i = 0; i < SIMD_BATCH_SIZE; ++i)
            {
                // Skip null items
                if (Items[BatchStart + i] == nullptr)
                {
                    continue;
                }
                
                // Allocate node
                FQueueNode* NewNode = AllocateNode(Items[BatchStart + i]);
                if (NewNode)
                {
                    // Set timestamp
                    NewNode->Descriptor.EnqueueTime = CurrentTime;
                    // Set SIMD compatibility flag
                    NewNode->Descriptor.bSIMDCompatible = true;
                    
                    BatchNodes[i] = NewNode;
                    EnqueuedCount++;
                }
            }
            
            // Link all nodes in the batch
            for (int32 i = 0; i < SIMD_BATCH_SIZE - 1; ++i)
            {
                if (BatchNodes[i] && BatchNodes[i + 1])
                {
                    BatchNodes[i]->Next = BatchNodes[i + 1];
                }
            }
            
            // Link batch to queue
            if (BatchNodes[0])
            {
                Tail->Next = BatchNodes[0];
                
                // Find the last valid node in batch to be the new tail
                FQueueNode* LastNode = nullptr;
                for (int32 i = SIMD_BATCH_SIZE - 1; i >= 0; --i)
                {
                    if (BatchNodes[i])
                    {
                        LastNode = BatchNodes[i];
                        break;
                    }
                }
                
                if (LastNode)
                {
                    Tail = LastNode;
                }
            }
        }
        else
        {
            // Regular processing for partial batches or when SIMD is disabled
            for (int32 i = BatchStart; i < BatchEnd; ++i)
            {
                // Skip null items
                if (Items[i] == nullptr)
                {
                    continue;
                }
                
                // Allocate a new node
                FQueueNode* NewNode = AllocateNode(Items[i]);
                if (NewNode)
                {
                    // Set timestamp
                    NewNode->Descriptor.EnqueueTime = CurrentTime;
                    
                    // Link the new node at the end of the queue
                    Tail->Next = NewNode;
                    Tail = NewNode;
                    
                    EnqueuedCount++;
                }
            }
        }
    }
    
    EnqueueLock.Unlock();
    
    // Update size and stats
    if (EnqueuedCount > 0)
    {
        Size.Add(EnqueuedCount);
        
        // Update memory tracking if enabled
        if (bTrackMemoryUsage)
        {
            MemorySize = EnqueuedCount * sizeof(FQueueNode);
        }
        
        UpdateEnqueueStats(true, 0.0, EnqueuedCount, MemorySize);
    }
    
    return EnqueuedCount;
}

int32 FThreadSafeOperationQueue::DequeueBatch(void** OutItems, int32 MaxCount)
{
    SCOPE_CYCLE_COUNTER(STAT_TSOperationQueue_DequeueBatch);
    
    if (!bIsInitialized || MaxCount <= 0 || OutItems == nullptr)
    {
        return 0;
    }
    
    // Try SIMD batch dequeue first if enabled and appropriate
    if (bUseSIMDOptimization && MaxCount >= SIMD_BATCH_SIZE)
    {
        int32 ProcessedCount = 0;
        if (DequeueSIMDBatch(OutItems, MaxCount, ProcessedCount))
        {
            return ProcessedCount;
        }
    }
    
    int32 CurrentSize = Size.GetValue();
    int32 ItemsToDequeue = FMath::Min(MaxCount, CurrentSize);
    
    if (ItemsToDequeue == 0)
    {
        UpdateDequeueStats(false);
        return 0;
    }
    
    // Try to acquire the lock with exponential backoff
    bool bLockAcquired = false;
    uint32 BackoffTime = CONTENTION_BACKOFF_MIN_US;
    
    while (!bLockAcquired)
    {
        if (DequeueLock.TryLock())
        {
            bLockAcquired = true;
        }
        else
        {
            // We encountered contention, update stats
            UpdateContentionStats();
            
            // Back off exponentially to reduce contention
            FPlatformProcess::SleepNoStats(BackoffTime / 1000000.0f);
            BackoffTime = FMath::Min<int32>(BackoffTime * 2, CONTENTION_BACKOFF_MAX_US);
        }
    }
    
    int32 DequeuedCount = 0;
    double TotalLatency = 0.0;
    double CurrentTime = FPlatformTime::Seconds();
    
    // Optimized batch dequeue - process in chunks
    while (DequeuedCount < ItemsToDequeue)
    {
        // Check if we've reached the end of the queue
        if (Head->Next == nullptr)
        {
            break;
        }
        
        // Apply age-based promotion if enabled
        if (bUseAgeBasedPromotion)
        {
            // Check for aged operations that need promotion
            FQueueNode* CurrentNode = Head->Next;
            TArray<FQueueNode*> AgedNodes;
            
            while (CurrentNode != nullptr && AgedNodes.Num() < ItemsToDequeue - DequeuedCount)
            {
                if (CurrentNode->Descriptor.EnqueueTime > 0.0)
                {
                    double Age = (CurrentTime - CurrentNode->Descriptor.EnqueueTime) * 1000.0;
                    if (Age > AgePromotionThresholdMs)
                    {
                        AgedNodes.Add(CurrentNode);
                    }
                }
                
                CurrentNode = CurrentNode->Next;
            }
            
            // Process aged nodes first to prevent starvation
            if (AgedNodes.Num() > 0)
            {
                for (FQueueNode* Node : AgedNodes)
                {
                    // Skip if already processed
                    if (Node == Head)
                    {
                        continue;
                    }
                    
                    // Extract the node from the queue
                    FQueueNode* SearchNode = Head;
                    while (SearchNode->Next != Node && SearchNode->Next != nullptr)
                    {
                        SearchNode = SearchNode->Next;
                    }
                    
                    if (SearchNode->Next == Node)
                    {
                        // Remove from current position
                        SearchNode->Next = Node->Next;
                        
                        // Put at front of queue (after dummy head)
                        Node->Next = Head->Next;
                        Head->Next = Node;
                    }
                }
            }
        }
        
        // Determine batch size for this iteration
        int32 BatchSize = FMath::Min(SIMD_BATCH_SIZE, ItemsToDequeue - DequeuedCount);
        
        for (int32 i = 0; i < BatchSize; ++i)
        {
            // Check if we've reached the end of the queue
            if (Head->Next == nullptr)
            {
                break;
            }
            
            // Save item pointer
            FQueueNode* OldHead = Head;
            FQueueNode* NewHead = Head->Next;
            
            // Get the item
            OutItems[DequeuedCount] = NewHead->Descriptor.Payload;
            
            // Calculate latency if timestamp is available
            if (NewHead->Descriptor.EnqueueTime > 0.0)
            {
                double Latency = (CurrentTime - NewHead->Descriptor.EnqueueTime) * 1000.0;
                TotalLatency += Latency;
            }
            
            // Update head
            Head = NewHead;
            
            // Free the old node
            FreeNode(OldHead);
            
            DequeuedCount++;
        }
    }
    
    // If we've emptied the queue, update tail
    if (Head->Next == nullptr)
    {
        Tail = Head;
    }
    
    DequeueLock.Unlock();
    
    // Update size and stats
    if (DequeuedCount > 0)
    {
        Size.Add(-DequeuedCount);
        
        // Update latency stats
        double AvgLatency = (DequeuedCount > 0) ? TotalLatency / DequeuedCount : 0.0;
        UpdateDequeueStats(true, 0.0, AvgLatency, DequeuedCount);
    }
    
    return DequeuedCount;
}

IThreadSafeQueue& FThreadSafeOperationQueue::Get()
{
    // Lazy singleton initialization
    if (Instance == nullptr)
    {
        Instance = new FThreadSafeOperationQueue();
        Instance->Initialize();
    }
    
    return *Instance;
}

void FThreadSafeOperationQueue::UpdateEnqueueStats(bool bSuccess, double WaitTimeMs, int32 BatchSize, int32 MemorySize)
{
    FScopeLock Lock(&StatsLock);
    
    if (bSuccess)
    {
        TotalEnqueued += BatchSize;
        int32 CurrentSize = Size.GetValue();
        if (CurrentSize > PeakSize)
        {
            PeakSize = CurrentSize;
        }
        
        // Track memory usage if enabled
        if (bTrackMemoryUsage && MemorySize > 0)
        {
            TotalMemoryTracked += MemorySize;
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
    
    // Update operations per second calculation
    TotalOperationsCount += BatchSize;
}

void FThreadSafeOperationQueue::UpdateDequeueStats(bool bSuccess, double WaitTimeMs, double LatencyMs, int32 BatchSize, bool bSIMD, bool bCacheOptimized)
{
    FScopeLock Lock(&StatsLock);
    
    if (bSuccess)
    {
        TotalDequeued += BatchSize;
        
        // Track latency stats if provided
        if (LatencyMs > 0.0)
        {
            TotalLatencyMs += LatencyMs * BatchSize;
            LatencySamples += BatchSize;
            
            if (LatencyMs > MaxObservedLatencyMs)
            {
                MaxObservedLatencyMs = LatencyMs;
            }
        }
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
    
    // Update SIMD and cache optimization stats
    if (bSIMD)
    {
        // Use direct values instead of calling GetValue()
        ExtendedStats.SIMDOperationPercentage = (float)SIMDBatchCount * SIMD_BATCH_SIZE / TotalOperationsCount * 100.0f;
    }
    
    if (bCacheOptimized)
    {
        ExtendedStats.CacheOptimizedOperations = CacheOptimizedCount;
    }
    
    // Update operations per second calculation
    TotalOperationsCount += BatchSize;
}

void FThreadSafeOperationQueue::UpdateContentionStats()
{
    ContentionCount++;
}

void FThreadSafeOperationQueue::UpdatePerformanceStats() const
{
    // Implementation that can be called from const methods
    double CurrentTime = FPlatformTime::Seconds();
    double TimeSinceLastUpdate = CurrentTime - PerformanceTimestamp;
    
    if (TimeSinceLastUpdate > 0.0)
    {
        FScopeLock Lock(&StatsLock);
        
        // Calculate operations per second
        ExtendedStats.OperationsPerSecond = TotalOperationsCount / TimeSinceLastUpdate;
        
        // Calculate memory usage
        ExtendedStats.MemoryUsageBytes = TotalMemoryTracked;
        
        // Calculate average batch size
        double TotalOperations = TotalEnqueued + TotalDequeued;
        uint64 TotalBatches = EnqueueWaitCount + DequeueWaitCount;
        ExtendedStats.AverageBatchSize = TotalBatches > 0 ? TotalOperations / TotalBatches : 1.0;
        
        // Calculate SIMD percentage
        ExtendedStats.SIMDOperationPercentage = TotalOperationsCount > 0 ? 
            (float)SIMDBatchCount * SIMD_BATCH_SIZE / TotalOperationsCount * 100.0f : 0.0f;
        
        // Update latency stats
        ExtendedStats.AverageLatencyMs = LatencySamples > 0 ? 
            TotalLatencyMs / LatencySamples : 0.0;
        ExtendedStats.MaxLatencyMs = MaxObservedLatencyMs;
        
        // Calculate queue full percentage
        ExtendedStats.QueueFullPercentage = (float)EnqueueFailures / (EnqueueFailures + TotalEnqueued) * 100.0f;
        
        // Update contention rate
        ExtendedStats.ContentionRate = TotalOperationsCount > 0 ? 
            (float)ContentionCount / TotalOperationsCount * 100.0f : 0.0f;
    }
}

void FThreadSafeOperationQueue::GroupOperationsByLocality(TMap<uint8, TArray<FOperationDescriptor>>& LocalityGroups, int32 MaxItems)
{
    if (!bIsInitialized || !bUseCacheOptimization)
    {
        return;
    }
    
    FScopeLock Lock(&DequeueLock);
    
    FQueueNode* Current = Head->Next;
    int32 ItemCount = 0;
    
    while (Current != nullptr && ItemCount < MaxItems)
    {
        uint8 LocalityHint = Current->Descriptor.CacheLocalityHint;
        
        if (LocalityHint > 0)
        {
            TArray<FOperationDescriptor>& Group = LocalityGroups.FindOrAdd(LocalityHint);
            Group.Add(Current->Descriptor);
        }
        
        Current = Current->Next;
        ItemCount++;
    }
}

FThreadSafeOperationQueue::FQueueNode* FThreadSafeOperationQueue::AllocateNode(void* Item)
{
    FQueueNode* Node = static_cast<FQueueNode*>(NodePool.Pop());
    if (!Node)
    {
        Node = new(NodeAllocator.Allocate()) FQueueNode();
    }
    
    // Initialize with item
    if (Item)
    {
        Node->Descriptor.Payload = Item;
        Node->Descriptor.EnqueueTime = FPlatformTime::Seconds();
    }
    
    return Node;
}

void FThreadSafeOperationQueue::FreeNode(FQueueNode* Node)
{
    if (Node)
    {
        // Clear the node data to prevent issues with reuse
        Node->Descriptor.Payload = nullptr;
        Node->Descriptor.EnqueueTime = 0.0;
        Node->Next = nullptr;
        
        // Return to pool
        NodePool.Push(Node);
    }
}

bool FThreadSafeOperationQueue::WaitNotFull(uint32 TimeoutMs, double& OutWaitTimeMs)
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
        if (Capacity <= 0 || Size.GetValue() < Capacity)
        {
            OutWaitTimeMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
            return true;
        }
        
        // Small sleep to avoid busy waiting, with processor affinity if specified
        if (ProcessorAffinityMask != 0)
        {
            FPlatformProcess::SetThreadAffinityMask(ProcessorAffinityMask);
        }
        
        FPlatformProcess::SleepNoStats(0.0001f);
    }
    
    OutWaitTimeMs = TimeoutMs;
    return false;
}

bool FThreadSafeOperationQueue::WaitNotEmpty(uint32 TimeoutMs, double& OutWaitTimeMs)
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
        if (Size.GetValue() > 0)
        {
            OutWaitTimeMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
            return true;
        }
        
        // Small sleep to avoid busy waiting, with processor affinity if specified
        if (ProcessorAffinityMask != 0)
        {
            FPlatformProcess::SetThreadAffinityMask(ProcessorAffinityMask);
        }
        
        FPlatformProcess::SleepNoStats(0.0001f);
    }
    
    OutWaitTimeMs = TimeoutMs;
    return false;
}

// Fix for Min template ambiguity issue (around line 335)
template<typename T>
T Min(T A, T B)
{
    return A < B ? A : B;
}

/**
 * Performs SIMD-optimized batch dequeue for compatible operations
 * @param OutItems Array to receive dequeued items
 * @param MaxCount Maximum number of items to dequeue
 * @param OutProcessedCount Receives the number of items processed
 * @return True if SIMD processing was applied
 */
bool FThreadSafeOperationQueue::DequeueSIMDBatch(void** OutItems, int32 MaxCount, int32& OutProcessedCount)
{
    OutProcessedCount = 0;
    
    if (!bIsInitialized || bClosed.GetValue() != 0 || !bUseSIMDOptimization)
    {
        return false;
    }
    
    // Limit batch size to SIMD-friendly size
    int32 BatchSize = FMath::Min(MaxCount, SIMD_BATCH_SIZE);
    
    // Fast path: if the queue is empty, return immediately
    if (IsEmpty())
    {
        return false;
    }
    
    // Use a spin lock for faster acquisition during batch processing
    FScopeLock Lock(&DequeueLock);
    
    FQueueNode* Current = Head;
    int32 ProcessedCount = 0;
    TArray<FQueueNode*> NodesToFree;
    
    // Process nodes in a batch for SIMD optimization
    while (Current != nullptr && Current != Tail && ProcessedCount < BatchSize)
    {
        // Extract the operation descriptor
        FOperationDescriptor& Descriptor = Current->Descriptor;
        
        // Check if the operation is SIMD-compatible
        if (Descriptor.bSIMDCompatible)
        {
            // Extract payload
            OutItems[ProcessedCount++] = Descriptor.Payload;
            
            // Record this node for later cleanup
            NodesToFree.Add(Current);
            
            // Move to next node
            Current = Current->Next;
        }
        else
        {
            // Non-SIMD operation, stop the batch processing
            break;
        }
    }
    
    // Update head to the first unprocessed node
    if (ProcessedCount > 0)
    {
        Head = Current;
        
        // Update stats
        Size.Add(-ProcessedCount);
        double Now = FPlatformTime::Seconds();
        
        for (FQueueNode* Node : NodesToFree)
        {
            double LatencyMs = (Now - Node->Descriptor.EnqueueTime) * 1000.0;
            
            // Free the node
            FreeNode(Node);
        }
        
        // Update dequeue statistics
        UpdateDequeueStats(true, 0.0, 0.0, ProcessedCount, true, false);
        
        // Update the output count
        OutProcessedCount = ProcessedCount;
        
        // This was a SIMD-processed batch
        return true;
    }
    
    // No SIMD-compatible operations were found
    return false;
}