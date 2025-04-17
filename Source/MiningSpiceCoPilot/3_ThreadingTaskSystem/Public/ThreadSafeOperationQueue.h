// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IThreadSafeQueue.h"
#include "HAL/ThreadSafeCounter.h"
#include "Containers/LockFreeList.h"
#include "Containers/LockFreeFixedSizeAllocator.h"
#include "HAL/PlatformAffinity.h"
#include "HAL/PlatformAtomics.h"
#include "Math/UnrealMathSSE.h"
#include "HAL/CriticalSection.h"
#include "ThreadSafetyInterface.h"

// Define the element size for the pool allocator
#define CACHE_LINE_SIZE_VALUE 64
#define POOL_ALLOCATOR_ELEMENT_SIZE 128

// Cache-line size for alignment to prevent false sharing
constexpr int32 CACHE_LINE_PADDING = CACHE_LINE_SIZE_VALUE - sizeof(void*);
constexpr int32 SIMD_BATCH_SIZE = 16; // SIMD-friendly batch processing size

/**
 * Operation type used for specialized processing
 */
enum class EOperationType : uint8
{
    /** Standard operation with no special handling */
    Standard,
    
    /** High-priority operation that should be processed immediately */
    HighPriority,
    
    /** Read-mostly operation that can be optimized for concurrent access */
    ReadMostly,
    
    /** SDF field operation that benefits from SIMD optimization */
    SDFField,
    
    /** Cache-sensitive operation that benefits from memory locality */
    CacheSensitive,
    
    /** Long-running operation that might be preempted */
    LongRunning
};

/**
 * Operation descriptor for enhanced queue functionality
 */
struct MININGSPICECOPILOT_API FOperationDescriptor
{
    /** Operation payload */
    void* Payload;
    
    /** Operation type for specialized processing */
    EOperationType Type;
    
    /** Timestamp when operation was enqueued */
    double EnqueueTime;
    
    /** Size of the operation in bytes (for memory tracking) */
    int32 SizeBytes;
    
    /** Unique identifier for the operation */
    uint64 OperationId;
    
    /** SIMD compatibility flag */
    bool bSIMDCompatible;
    
    /** Cache locality hint (0 = no locality, higher values indicate stronger locality) */
    uint8 CacheLocalityHint;
    
    /** Constructor */
    FOperationDescriptor()
        : Payload(nullptr)
        , Type(EOperationType::Standard)
        , EnqueueTime(0.0)
        , SizeBytes(0)
        , OperationId(0)
        , bSIMDCompatible(false)
        , CacheLocalityHint(0)
    {
    }
    
    /** Constructor with payload */
    explicit FOperationDescriptor(void* InPayload)
        : Payload(InPayload)
        , Type(EOperationType::Standard)
        , EnqueueTime(0.0)
        , SizeBytes(0)
        , OperationId(0)
        , bSIMDCompatible(false)
        , CacheLocalityHint(0)
    {
    }
};

/**
 * Extended queue statistics with detailed performance metrics
 */
struct MININGSPICECOPILOT_API FExtendedQueueStats : public FQueueStats
{
    /** Operations processed per second */
    double OperationsPerSecond;
    
    /** Memory usage in bytes */
    int64 MemoryUsageBytes;
    
    /** Average batch size for batch operations */
    double AverageBatchSize;
    
    /** Percentage of operations that use SIMD optimization */
    float SIMDOperationPercentage;
    
    /** Average operation latency (time from enqueue to dequeue) in milliseconds */
    double AverageLatencyMs;
    
    /** Maximum observed latency in milliseconds */
    double MaxLatencyMs;
    
    /** Percentage of time the queue was full */
    float QueueFullPercentage;
    
    /** Number of successful SIMD batch operations */
    uint64 SIMDBatchOperations;
    
    /** Number of cache-optimized operations */
    uint64 CacheOptimizedOperations;
    
    /** Contention rate (percentage of operations that experienced contention) */
    float ContentionRate;
    
    /** Constructor inherits from base stats */
    FExtendedQueueStats()
        : OperationsPerSecond(0.0)
        , MemoryUsageBytes(0)
        , AverageBatchSize(0.0)
        , SIMDOperationPercentage(0.0f)
        , AverageLatencyMs(0.0)
        , MaxLatencyMs(0.0)
        , QueueFullPercentage(0.0f)
        , SIMDBatchOperations(0)
        , CacheOptimizedOperations(0)
        , ContentionRate(0.0f)
    {
    }
};

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
    
    static IThreadSafeQueue& Get();
    //~ End IThreadSafeQueue Interface
    
    /**
     * Enqueues an operation with additional metadata for specialized processing
     * @param Item Operation payload
     * @param Type Operation type for specialized processing
     * @param SizeBytes Size of the operation in bytes
     * @param bSIMDCompatible Whether this operation is compatible with SIMD processing
     * @param CacheLocalityHint Hint for cache locality (0 = no locality)
     * @return Result of the operation
     */
    EQueueResult EnqueueWithMetadata(
        void* Item, 
        EOperationType Type = EOperationType::Standard,
        int32 SizeBytes = 0,
        bool bSIMDCompatible = false,
        uint8 CacheLocalityHint = 0);
    
    /**
     * Dequeues an operation with its metadata
     * @param OutDescriptor Receives the operation descriptor
     * @return Result of the operation
     */
    EQueueResult DequeueWithMetadata(FOperationDescriptor& OutDescriptor);
    
    /**
     * Performs SIMD-optimized batch dequeue for compatible operations
     * @param OutItems Array to receive dequeued items
     * @param MaxCount Maximum number of items to dequeue
     * @param OutProcessedCount Receives the number of items processed
     * @return True if SIMD processing was applied
     */
    bool DequeueSIMDBatch(void** OutItems, int32 MaxCount, int32& OutProcessedCount);
    
    /**
     * Performs cache-optimized batch dequeue for operations with locality
     * @param OutItems Array to receive dequeued items
     * @param MaxCount Maximum number of items to dequeue
     * @param LocalityHint Desired cache locality hint
     * @param OutProcessedCount Receives the number of items processed
     * @return True if cache optimization was applied
     */
    bool DequeueCacheOptimizedBatch(void** OutItems, int32 MaxCount, uint8 LocalityHint, int32& OutProcessedCount);
    
    /**
     * Gets extended statistics about queue performance
     * @return Extended queue statistics
     */
    FExtendedQueueStats GetExtendedStats() const;
    
    /**
     * Enables or disables age-based promotion to prevent starvation
     * @param bEnable Whether to enable age-based promotion
     * @param AgeThresholdMs Milliseconds before an operation is promoted
     */
    void SetAgeBasedPromotion(bool bEnable, uint32 AgeThresholdMs = 1000);
    
    /**
     * Sets the processor affinity for this queue's operations
     * @param AffinityMask Processor affinity mask
     */
    void SetProcessorAffinity(uint64 AffinityMask);
    
    /**
     * Enables or disables SIMD optimization for batch operations
     * @param bEnable Whether to enable SIMD optimization
     */
    void SetSIMDOptimization(bool bEnable);
    
    /**
     * Enables or disables cache optimization for locality-sensitive operations
     * @param bEnable Whether to enable cache optimization
     */
    void SetCacheOptimization(bool bEnable);
    
    /**
     * Sets memory tracking for queue operations
     * @param bEnable Whether to enable memory tracking
     */
    void SetMemoryTracking(bool bEnable);

    /** Cache-aligned internal node structure for the queue */
    struct alignas(CACHE_LINE_SIZE_VALUE) FQueueNode
    {
        /** Operation descriptor */
        FOperationDescriptor Descriptor;
        
        /** Next node in the queue */
        FQueueNode* Next;
        
        /** Padding to ensure cache line alignment */
        uint8 Padding[CACHE_LINE_PADDING];
        
        /** Constructor */
        FQueueNode() : Next(nullptr) {}
        
        /** Constructor with payload */
        explicit FQueueNode(void* InPayload) : Next(nullptr)
        {
            Descriptor.Payload = InPayload;
        }
    };

    /** Reclaim memory for reuse */
    void ReclaimMemory();
    
    /** Node allocator for efficient memory management */
    TLockFreeFixedSizeAllocator<POOL_ALLOCATOR_ELEMENT_SIZE, PLATFORM_CACHE_LINE_SIZE> NodeAllocator;
    
    /** Node pool for efficient allocation */
    TLockFreePointerListUnordered<FQueueNode, PLATFORM_CACHE_LINE_SIZE> NodePool;

private:
    /** Whether the queue has been initialized */
    bool bIsInitialized;
    
    /** Whether the queue has been closed */
    FThreadSafeCounter bClosed;
    
    /** Maximum capacity (0 for unlimited) */
    int32 Capacity;
    
    /** Current size */
    FThreadSafeCounter Size;
    
    /** Age-based promotion settings */
    bool bUseAgeBasedPromotion;
    uint32 AgePromotionThresholdMs;
    
    /** SIMD optimization settings */
    bool bUseSIMDOptimization;
    
    /** Cache optimization settings */
    bool bUseCacheOptimization;
    
    /** Memory tracking settings */
    bool bTrackMemoryUsage;
    
    /** Processor affinity mask */
    uint64 ProcessorAffinityMask;
    
    /** Head node (for dequeue) - cache line aligned */
    alignas(CACHE_LINE_SIZE_VALUE) FQueueNode* Head;
    
    /** Tail node (for enqueue) - cache line aligned */
    alignas(CACHE_LINE_SIZE_VALUE) FQueueNode* Tail;
    
    /** Memory padding to ensure head and tail are on different cache lines */
    uint8 HeadTailPadding[CACHE_LINE_SIZE_VALUE];
    
    /** Lock for enqueue operations */
    mutable FCriticalSection EnqueueLock;
    
    /** Lock for dequeue operations */
    mutable FCriticalSection DequeueLock;
    
    /** Queue statistics */
    mutable FCriticalSection StatsLock;
    
    /** Extended statistics */
    mutable FExtendedQueueStats ExtendedStats;
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
    double TotalLatencyMs;
    uint64 LatencySamples;
    double MaxObservedLatencyMs;
    int64 TotalMemoryTracked;
    uint64 SIMDBatchCount;
    uint64 CacheOptimizedCount;
    uint64 ContentionCount;
    uint64 TotalOperationsCount;
    double PerformanceTimestamp;
    
    /** Updates statistics after an enqueue operation */
    void UpdateEnqueueStats(bool bSuccess, double WaitTimeMs = 0.0, int32 BatchSize = 1, int32 MemorySize = 0);
    
    /** Updates statistics after a dequeue operation */
    void UpdateDequeueStats(bool bSuccess, double WaitTimeMs = 0.0, double LatencyMs = 0.0, int32 BatchSize = 1, bool bSIMD = false, bool bCacheOptimized = false);
    
    /** Updates contention statistics */
    void UpdateContentionStats();
    
    /** Allocates a new node from the pool */
    FQueueNode* AllocateNode(void* Item = nullptr);
    
    /** Frees a node back to the pool */
    void FreeNode(FQueueNode* Node);
    
    /** Waits until the queue is not full or timeout */
    bool WaitNotFull(uint32 TimeoutMs, double& OutWaitTimeMs);
    
    /** Waits until the queue is not empty or timeout */
    bool WaitNotEmpty(uint32 TimeoutMs, double& OutWaitTimeMs);
    
    /** Prepares a new operation descriptor */
    FOperationDescriptor PrepareOperationDescriptor(void* Item, EOperationType Type, int32 SizeBytes, bool bSIMDCompatible, uint8 CacheLocalityHint);
    
    /** Groups operations by cache locality for optimized processing */
    void GroupOperationsByLocality(TMap<uint8, TArray<FOperationDescriptor>>& LocalityGroups, int32 MaxItems);
    
    /** Updates performance statistics */
    void UpdatePerformanceStats() const;

    /** Singleton instance */
    static FThreadSafeOperationQueue* Instance;
};