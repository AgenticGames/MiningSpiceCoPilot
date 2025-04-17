// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeCounter.h"
#include "Misc/ScopeLock.h"
#include "HAL/PlatformAffinity.h"

/**
 * Parallel execution mode for the executor
 */
enum class EParallelExecutionMode : uint8
{
    /** Automatically determine the best execution approach */
    Automatic,
    
    /** Force parallel execution */
    ForceParallel,
    
    /** Force sequential execution */
    ForceSequential,
    
    /** SIMD-optimized parallel execution for SDF operations */
    SIMDOptimized,
    
    /** Cache-aware execution with data locality optimization */
    CacheOptimized,
    
    /** Adaptive execution based on system load */
    Adaptive
};

/**
 * Work chunk for distributing work across cores
 */
struct FWorkChunk
{
    /** Start index for this chunk */
    int32 StartIndex;
    
    /** End index for this chunk (inclusive) */
    int32 EndIndex;
    
    /** Thread that will process this chunk */
    int32 ThreadIndex;
    
    /** Whether this chunk has been completed */
    bool bCompleted;
    
    /** Constructor */
    FWorkChunk()
        : StartIndex(0)
        , EndIndex(0)
        , ThreadIndex(-1)
        , bCompleted(false)
    {
    }
};

/**
 * Parallel execution completion event
 */
class MININGSPICECOPILOT_API FParallelCompletionEvent
{
public:
    /** Constructor */
    FParallelCompletionEvent();
    
    /** Destructor */
    ~FParallelCompletionEvent();
    
    /** Signals that a chunk has been completed */
    void SignalCompletion();
    
    /** Waits for all chunks to be completed */
    void Wait();
    
    /** Sets the number of chunks to wait for */
    void SetChunkCount(int32 Count);
    
    /** Gets the number of chunks */
    int32 GetChunkCount() const;
    
    /** Gets the number of completed chunks */
    int32 GetCompletedChunks() const;
    
    /** Resets the event for reuse */
    void Reset();

private:
    /** Number of work chunks */
    int32 ChunkCount;
    
    /** Number of chunks completed */
    FThreadSafeCounter CompletedChunks;
    
    /** Synchronization event */
    FEvent* CompletionEvent;
};

/**
 * Parallel execution context containing work item and range information
 */
struct FParallelContext
{
    /** The function to execute for each item */
    TFunction<void(int32)> WorkItemFunction;
    
    /** The function to execute for a range of items */
    TFunction<void(int32, int32)> WorkRangeFunction;
    
    /** Parallel completion event */
    FParallelCompletionEvent* CompletionEvent;
    
    /** Work chunks */
    TArray<FWorkChunk> Chunks;
    
    /** Execution mode */
    EParallelExecutionMode ExecutionMode;
    
    /** Total number of items to process */
    int32 ItemCount;
    
    /** Granularity (items per chunk) */
    int32 Granularity;
    
    /** Number of threads to use */
    int32 NumThreads;
    
    /** Whether the operation was cancelled */
    FThreadSafeCounter bCancelled;
    
    /** Whether to use work stealing */
    bool bUseWorkStealing;
    
    /** Whether to use thread affinity */
    bool bUseThreadAffinity;
    
    /** Constructor */
    FParallelContext()
        : CompletionEvent(nullptr)
        , ExecutionMode(EParallelExecutionMode::Automatic)
        , ItemCount(0)
        , Granularity(0)
        , NumThreads(0)
        , bUseWorkStealing(true)
        , bUseThreadAffinity(false)
    {
    }
    
    /** Delete copy assignment operator since FThreadSafeCounter is not copyable */
    FParallelContext& operator=(const FParallelContext&) = delete;
};

/**
 * Parallel executor for efficient execution of similar mining operations
 * Provides work distribution across available cores with NUMA awareness,
 * SIMD optimization, and load balancing for mining operations.
 */
class MININGSPICECOPILOT_API FParallelExecutor
{
public:
    /** Constructor */
    FParallelExecutor();
    
    /** Destructor */
    ~FParallelExecutor();
    
    /**
     * Executes a function for each item in a range in parallel
     * @param ItemCount Total number of items to process
     * @param Function Function to execute for each item
     * @param ExecutionMode How to execute the operation
     * @param Granularity Number of items per work unit (0 for automatic)
     * @return True if all items were processed successfully
     */
    bool ParallelFor(int32 ItemCount, TFunction<void(int32)> Function, 
        EParallelExecutionMode ExecutionMode = EParallelExecutionMode::Automatic, 
        int32 Granularity = 0);
    
    /**
     * Executes a function for ranges of items in parallel
     * @param ItemCount Total number of items to process
     * @param Function Function to execute for each range
     * @param ExecutionMode How to execute the operation
     * @param Granularity Number of items per work unit (0 for automatic)
     * @return True if all items were processed successfully
     */
    bool ParallelForRange(int32 ItemCount, TFunction<void(int32, int32)> Function,
        EParallelExecutionMode ExecutionMode = EParallelExecutionMode::Automatic,
        int32 Granularity = 0);
    
    /**
     * Executes a SIMD-optimized function for processing distance fields
     * @param VoxelCount Number of voxels to process
     * @param Function Function to execute with SIMD optimization
     * @param ExecutionMode How to execute the operation
     * @return True if all items were processed successfully
     */
    bool ParallelForSDF(int32 VoxelCount, TFunction<void(int32, int32)> Function,
        EParallelExecutionMode ExecutionMode = EParallelExecutionMode::SIMDOptimized);
    
    /**
     * Executes a function for multiple zones in parallel with data locality
     * @param Zones Array of zone IDs to process
     * @param Function Function to execute for each zone
     * @param ExecutionMode How to execute the operation
     * @return True if all zones were processed successfully
     */
    bool ParallelZones(const TArray<int32>& Zones, TFunction<void(int32)> Function,
        EParallelExecutionMode ExecutionMode = EParallelExecutionMode::CacheOptimized);
    
    /**
     * Cancels the current parallel execution
     */
    void Cancel();
    
    /**
     * Waits for the current parallel execution to complete
     * @param TimeoutMs Maximum time to wait in milliseconds (0 for indefinite)
     * @return True if the execution completed, false if it timed out
     */
    bool Wait(uint32 TimeoutMs = 0);
    
    /**
     * Gets the recommended number of threads for parallel execution
     * @return Number of threads to use
     */
    int32 GetRecommendedThreadCount() const;
    
    /**
     * Sets whether to use work stealing for load balancing
     * @param bEnable Whether to enable work stealing
     */
    void SetWorkStealing(bool bEnable);
    
    /**
     * Sets whether to use thread affinity for cache coherence
     * @param bEnable Whether to enable thread affinity
     */
    void SetThreadAffinity(bool bEnable);
    
    /**
     * Sets the thread count for parallel operations
     * @param Count Number of threads to use (0 for automatic)
     */
    void SetThreadCount(int32 Count);
    
    /**
     * Gets the singleton instance
     * @return Reference to the parallel executor
     */
    static FParallelExecutor& Get();

private:
    /** Current execution context */
    FParallelContext CurrentContext;
    
    /** Lock for context access */
    FCriticalSection ContextLock;
    
    /** Whether the executor is currently executing */
    FThreadSafeCounter bIsExecuting;
    
    /** Thread count for parallel operations (0 for automatic) */
    int32 ThreadCount;
    
    /** Determines the optimal granularity for a workload */
    int32 DetermineOptimalGranularity(int32 ItemCount, EParallelExecutionMode ExecutionMode) const;
    
    /** Creates work chunks based on the execution parameters */
    void CreateWorkChunks(FParallelContext& Context);
    
    /** Worker thread function for processing a chunk */
    static void ProcessChunk(FWorkChunk& Chunk, FParallelContext& Context);
    
    /** Attempts to steal work from other threads */
    static bool StealWork(FParallelContext& Context, int32 ThreadIndex, FWorkChunk& OutChunk);
    
    /** Distributes a workload across the thread pool */
    bool DistributeWork(FParallelContext& Context);
    
    /** Executes the workload sequentially */
    bool ExecuteSequential(FParallelContext& Context);
    
    /** SIMD-optimized execution for distance fields */
    bool ExecuteSIMD(FParallelContext& Context);
    
    /** SSE-optimized execution */
    bool ExecuteSIMD_SSE(FParallelContext& Context);
    
    /** SSE4-optimized execution */
    bool ExecuteSIMD_SSE4(FParallelContext& Context);
    
    /** AVX-optimized execution */
    bool ExecuteSIMD_AVX(FParallelContext& Context);
    
    /** AVX2-optimized execution */
    bool ExecuteSIMD_AVX2(FParallelContext& Context);
    
    /** Cache-optimized execution with data locality */
    bool ExecuteCacheOptimized(FParallelContext& Context);
    
    /** Determines if the workload should be executed in parallel */
    bool ShouldExecuteInParallel(int32 ItemCount) const;
    
    /** Gets the SIMD processing width based on CPU features */
    int32 GetSIMDProcessingWidth() const;

    /** Singleton instance */
    static FParallelExecutor* Instance;
};