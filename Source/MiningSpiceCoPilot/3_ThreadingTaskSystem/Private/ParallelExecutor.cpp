// Copyright Epic Games, Inc. All Rights Reserved.

#include "3_ThreadingTaskSystem/Public/ParallelExecutor.h"
#include "HAL/PlatformProcess.h"
#include "HAL/RunnableThread.h"
#include "HAL/ThreadManager.h"
#include "Async/TaskGraphInterfaces.h"
#include "HAL/ThreadHeartBeat.h"

// Initialize static instance to nullptr
FParallelExecutor* FParallelExecutor::Instance = nullptr;

//----------------------------------------------------------------------
// FParallelCompletionEvent Implementation
//----------------------------------------------------------------------

FParallelCompletionEvent::FParallelCompletionEvent()
    : ChunkCount(0)
{
    CompletionEvent = FPlatformProcess::GetSynchEventFromPool(false);
}

FParallelCompletionEvent::~FParallelCompletionEvent()
{
    FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);
    CompletionEvent = nullptr;
}

void FParallelCompletionEvent::SignalCompletion()
{
    int32 NewCount = CompletedChunks.Increment();
    
    if (NewCount == ChunkCount)
    {
        CompletionEvent->Trigger();
    }
}

void FParallelCompletionEvent::Wait()
{
    if (CompletedChunks.GetValue() < ChunkCount)
    {
        CompletionEvent->Wait();
    }
}

void FParallelCompletionEvent::SetChunkCount(int32 Count)
{
    ChunkCount = Count;
    
    // If we're setting the count to 0, trigger the event immediately
    // This handles the case of an empty parallel operation
    if (Count == 0)
    {
        CompletionEvent->Trigger();
    }
}

int32 FParallelCompletionEvent::GetChunkCount() const
{
    return ChunkCount;
}

int32 FParallelCompletionEvent::GetCompletedChunks() const
{
    return CompletedChunks.GetValue();
}

void FParallelCompletionEvent::Reset()
{
    CompletedChunks.Reset();
    CompletionEvent->Reset();
    ChunkCount = 0;
}

//----------------------------------------------------------------------
// FParallelExecutor Implementation
//----------------------------------------------------------------------

FParallelExecutor::FParallelExecutor()
    : ThreadCount(0)
{
    // Set the singleton instance
    check(Instance == nullptr);
    Instance = this;
}

FParallelExecutor::~FParallelExecutor()
{
    // Make sure any running execution is cancelled
    Cancel();

    // Wait for completion
    Wait();

    // Clear the singleton instance if it's us
    if (Instance == this)
    {
        Instance = nullptr;
    }
}

FParallelExecutor& FParallelExecutor::Get()
{
    check(Instance != nullptr);
    return *Instance;
}

bool FParallelExecutor::ParallelFor(int32 ItemCount, TFunction<void(int32)> Function, 
    EParallelExecutionMode ExecutionMode, int32 Granularity)
{
    // Validate parameters
    if (ItemCount <= 0 || !Function)
    {
        return false;
    }

    // Wait if there's an already executing workload
    while (bIsExecuting.GetValue() > 0)
    {
        FPlatformProcess::Sleep(0.001f);
    }

    // Set the executing flag
    bIsExecuting.Set(1);

    // Set up the context
    FScopeLock Lock(&ContextLock);
    
    CurrentContext = FParallelContext();
    CurrentContext.WorkItemFunction = Function;
    CurrentContext.ExecutionMode = ExecutionMode;
    CurrentContext.ItemCount = ItemCount;
    CurrentContext.Granularity = Granularity > 0 ? Granularity : DetermineOptimalGranularity(ItemCount, ExecutionMode);
    CurrentContext.NumThreads = ThreadCount > 0 ? ThreadCount : GetRecommendedThreadCount();
    
    // Create a completion event
    FParallelCompletionEvent CompletionEvent;
    CurrentContext.CompletionEvent = &CompletionEvent;

    // Execute based on the mode
    bool bResult = false;
    
    if (ExecutionMode == EParallelExecutionMode::ForceSequential || 
        (ExecutionMode == EParallelExecutionMode::Automatic && !ShouldExecuteInParallel(ItemCount)))
    {
        bResult = ExecuteSequential(CurrentContext);
    }
    else if (ExecutionMode == EParallelExecutionMode::SIMDOptimized)
    {
        // Convert item function to range function for SIMD optimization
        CurrentContext.WorkRangeFunction = [&Function](int32 StartIndex, int32 EndIndex)
        {
            for (int32 Index = StartIndex; Index <= EndIndex; ++Index)
            {
                Function(Index);
            }
        };
        
        bResult = ExecuteSIMD(CurrentContext);
    }
    else if (ExecutionMode == EParallelExecutionMode::CacheOptimized)
    {
        // Convert item function to range function for cache optimization
        CurrentContext.WorkRangeFunction = [&Function](int32 StartIndex, int32 EndIndex)
        {
            for (int32 Index = StartIndex; Index <= EndIndex; ++Index)
            {
                Function(Index);
            }
        };
        
        bResult = ExecuteCacheOptimized(CurrentContext);
    }
    else
    {
        // Standard parallel execution
        CreateWorkChunks(CurrentContext);
        bResult = DistributeWork(CurrentContext);
        
        // Wait for completion
        CompletionEvent.Wait();
    }

    // Clear the executing flag
    bIsExecuting.Set(0);

    return bResult && (CompletionEvent.GetCompletedChunks() == CompletionEvent.GetChunkCount());
}

bool FParallelExecutor::ParallelForRange(int32 ItemCount, TFunction<void(int32, int32)> Function,
    EParallelExecutionMode ExecutionMode, int32 Granularity)
{
    // Validate parameters
    if (ItemCount <= 0 || !Function)
    {
        return false;
    }

    // Wait if there's an already executing workload
    while (bIsExecuting.GetValue() > 0)
    {
        FPlatformProcess::Sleep(0.001f);
    }

    // Set the executing flag
    bIsExecuting.Set(1);

    // Set up the context
    FScopeLock Lock(&ContextLock);
    
    CurrentContext = FParallelContext();
    CurrentContext.WorkRangeFunction = Function;
    CurrentContext.ExecutionMode = ExecutionMode;
    CurrentContext.ItemCount = ItemCount;
    CurrentContext.Granularity = Granularity > 0 ? Granularity : DetermineOptimalGranularity(ItemCount, ExecutionMode);
    CurrentContext.NumThreads = ThreadCount > 0 ? ThreadCount : GetRecommendedThreadCount();
    
    // Create a completion event
    FParallelCompletionEvent CompletionEvent;
    CurrentContext.CompletionEvent = &CompletionEvent;

    // Execute based on the mode
    bool bResult = false;
    
    if (ExecutionMode == EParallelExecutionMode::ForceSequential || 
        (ExecutionMode == EParallelExecutionMode::Automatic && !ShouldExecuteInParallel(ItemCount)))
    {
        bResult = ExecuteSequential(CurrentContext);
    }
    else if (ExecutionMode == EParallelExecutionMode::SIMDOptimized)
    {
        bResult = ExecuteSIMD(CurrentContext);
    }
    else if (ExecutionMode == EParallelExecutionMode::CacheOptimized)
    {
        bResult = ExecuteCacheOptimized(CurrentContext);
    }
    else
    {
        // Standard parallel execution
        CreateWorkChunks(CurrentContext);
        bResult = DistributeWork(CurrentContext);
        
        // Wait for completion
        CompletionEvent.Wait();
    }

    // Clear the executing flag
    bIsExecuting.Set(0);

    return bResult && (CompletionEvent.GetCompletedChunks() == CompletionEvent.GetChunkCount());
}

bool FParallelExecutor::ParallelForSDF(int32 VoxelCount, TFunction<void(int32, int32)> Function,
    EParallelExecutionMode ExecutionMode)
{
    // SDF operations are always executed using SIMD optimization
    return ParallelForRange(VoxelCount, Function, EParallelExecutionMode::SIMDOptimized);
}

bool FParallelExecutor::ParallelZones(const TArray<int32>& Zones, TFunction<void(int32)> Function,
    EParallelExecutionMode ExecutionMode)
{
    // Validate parameters
    if (Zones.Num() <= 0 || !Function)
    {
        return false;
    }

    // Zone-based execution uses a special cache-optimized approach
    // Convert the zone IDs to indices for our standard parallel loop
    return ParallelFor(Zones.Num(), [&Zones, &Function](int32 Index)
    {
        Function(Zones[Index]);
    }, ExecutionMode);
}

void FParallelExecutor::Cancel()
{
    FScopeLock Lock(&ContextLock);
    
    if (bIsExecuting.GetValue() > 0)
    {
        CurrentContext.bCancelled.Set(1);
    }
}

bool FParallelExecutor::Wait(uint32 TimeoutMs)
{
    if (bIsExecuting.GetValue() == 0)
    {
        return true;
    }

    FScopeLock Lock(&ContextLock);
    
    if (CurrentContext.CompletionEvent)
    {
        if (TimeoutMs > 0)
        {
            return CurrentContext.CompletionEvent->Wait(TimeoutMs * 1000); // Convert ms to microseconds
        }
        else
        {
            CurrentContext.CompletionEvent->Wait();
            return true;
        }
    }
    
    return bIsExecuting.GetValue() == 0;
}

int32 FParallelExecutor::GetRecommendedThreadCount() const
{
    // Get the number of cores available
    int32 NumCores = FPlatformMisc::NumberOfCores();
    
    // For best performance, use one thread per core, but never use more than 16 threads
    // This avoids oversaturation of the thread pool for systems with many cores
    int32 RecommendedCount = FMath::Min(NumCores, 16);
    
    // Always use at least 2 threads
    return FMath::Max(RecommendedCount, 2);
}

void FParallelExecutor::SetWorkStealing(bool bEnable)
{
    FScopeLock Lock(&ContextLock);
    CurrentContext.bUseWorkStealing = bEnable;
}

void FParallelExecutor::SetThreadAffinity(bool bEnable)
{
    FScopeLock Lock(&ContextLock);
    CurrentContext.bUseThreadAffinity = bEnable;
}

void FParallelExecutor::SetThreadCount(int32 Count)
{
    ThreadCount = Count;
}

int32 FParallelExecutor::DetermineOptimalGranularity(int32 ItemCount, EParallelExecutionMode ExecutionMode) const
{
    // The optimal granularity depends on the execution mode and item count
    
    if (ExecutionMode == EParallelExecutionMode::SIMDOptimized)
    {
        // For SIMD operations, align to typical vector size (4, 8, or 16 elements)
        return 16;
    }
    else if (ExecutionMode == EParallelExecutionMode::CacheOptimized)
    {
        // For cache-optimized operations, use larger chunks to improve locality
        return 64;
    }
    else
    {
        // For standard parallel operations, balance overhead vs. parallelism
        int32 NumThreads = ThreadCount > 0 ? ThreadCount : GetRecommendedThreadCount();
        
        // Aim for 4-8 chunks per thread for good load balancing
        int32 TargetChunkCount = NumThreads * 6;
        int32 Granularity = FMath::Max(ItemCount / TargetChunkCount, 1);
        
        // Avoid too small chunks that would have too much overhead
        return FMath::Max(Granularity, 16);
    }
}

void FParallelExecutor::CreateWorkChunks(FParallelContext& Context)
{
    Context.Chunks.Empty();
    
    int32 ItemsPerChunk = Context.Granularity;
    int32 NumChunks = FMath::DivideAndRoundUp(Context.ItemCount, ItemsPerChunk);
    
    // Create the chunks
    for (int32 ChunkIndex = 0; ChunkIndex < NumChunks; ++ChunkIndex)
    {
        FWorkChunk Chunk;
        Chunk.StartIndex = ChunkIndex * ItemsPerChunk;
        Chunk.EndIndex = FMath::Min(Chunk.StartIndex + ItemsPerChunk - 1, Context.ItemCount - 1);
        
        // Distribute chunks among threads in a round-robin fashion for initial assignment
        Chunk.ThreadIndex = ChunkIndex % Context.NumThreads;
        
        Context.Chunks.Add(Chunk);
    }
    
    // Set the chunk count in the completion event
    if (Context.CompletionEvent)
    {
        Context.CompletionEvent->SetChunkCount(Context.Chunks.Num());
    }
}

void FParallelExecutor::ProcessChunk(FWorkChunk& Chunk, FParallelContext& Context)
{
    // Check if we've been cancelled
    if (Context.bCancelled.GetValue() > 0)
    {
        return;
    }
    
    // Process the chunk
    if (Context.WorkItemFunction)
    {
        // Process each item individually
        for (int32 Index = Chunk.StartIndex; Index <= Chunk.EndIndex; ++Index)
        {
            // Check for cancellation periodically
            if ((Index % 64) == 0 && Context.bCancelled.GetValue() > 0)
            {
                break;
            }
            
            Context.WorkItemFunction(Index);
        }
    }
    else if (Context.WorkRangeFunction)
    {
        // Process the entire range at once
        Context.WorkRangeFunction(Chunk.StartIndex, Chunk.EndIndex);
    }
    
    // Mark the chunk as completed
    Chunk.bCompleted = true;
    
    // Signal completion
    if (Context.CompletionEvent)
    {
        Context.CompletionEvent->SignalCompletion();
    }
}

bool FParallelExecutor::StealWork(FParallelContext& Context, int32 ThreadIndex, FWorkChunk& OutChunk)
{
    if (!Context.bUseWorkStealing)
    {
        return false;
    }
    
    // Try to find a chunk assigned to another thread that hasn't been started yet
    for (int32 ChunkIndex = 0; ChunkIndex < Context.Chunks.Num(); ++ChunkIndex)
    {
        FWorkChunk& Chunk = Context.Chunks[ChunkIndex];
        
        if (Chunk.ThreadIndex != ThreadIndex && !Chunk.bCompleted)
        {
            // Try to "steal" this chunk by atomically changing its thread index
            int32 OriginalThreadIndex = FPlatformAtomics::InterlockedCompareExchange(&Chunk.ThreadIndex, ThreadIndex, Chunk.ThreadIndex);
            
            if (OriginalThreadIndex != ThreadIndex)
            {
                // Successfully stole the chunk
                OutChunk = Chunk;
                return true;
            }
        }
    }
    
    return false;
}

bool FParallelExecutor::DistributeWork(FParallelContext& Context)
{
    // Create task graph tasks for each thread
    for (int32 ThreadIndex = 0; ThreadIndex < Context.NumThreads; ++ThreadIndex)
    {
        FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady([this, ThreadIndex, &Context]()
        {
            // Set thread affinity if enabled
            if (Context.bUseThreadAffinity)
            {
                FPlatformAffinity::SetThreadAffinity(1ULL << ThreadIndex);
            }
            
            // Process all chunks assigned to this thread
            for (FWorkChunk& Chunk : Context.Chunks)
            {
                if (Chunk.ThreadIndex == ThreadIndex && !Chunk.bCompleted)
                {
                    ProcessChunk(Chunk, Context);
                    
                    // Check for cancellation after each chunk
                    if (Context.bCancelled.GetValue() > 0)
                    {
                        break;
                    }
                }
            }
            
            // Try to steal work from other threads if work stealing is enabled
            if (Context.bUseWorkStealing && Context.bCancelled.GetValue() == 0)
            {
                FWorkChunk StolenChunk;
                while (StealWork(Context, ThreadIndex, StolenChunk))
                {
                    ProcessChunk(StolenChunk, Context);
                    
                    // Check for cancellation after each stolen chunk
                    if (Context.bCancelled.GetValue() > 0)
                    {
                        break;
                    }
                }
            }
        },
        TStatId(),
        nullptr,
        ENamedThreads::AnyThread);
    }
    
    return true;
}

bool FParallelExecutor::ExecuteSequential(FParallelContext& Context)
{
    // For sequential execution, we create a single work chunk and process it
    FWorkChunk Chunk;
    Chunk.StartIndex = 0;
    Chunk.EndIndex = Context.ItemCount - 1;
    Chunk.ThreadIndex = 0;
    
    if (Context.CompletionEvent)
    {
        Context.CompletionEvent->SetChunkCount(1);
    }
    
    ProcessChunk(Chunk, Context);
    
    return !Context.bCancelled.GetValue();
}

bool FParallelExecutor::ExecuteSIMD(FParallelContext& Context)
{
    // For SIMD execution, we create chunks aligned to SIMD vector sizes
    // and distribute them for processing
    int32 SIMDGranularity = 16; // Align to AVX-512 vector size (16 floats)
    Context.Granularity = SIMDGranularity;
    
    CreateWorkChunks(Context);
    return DistributeWork(Context);
}

bool FParallelExecutor::ExecuteCacheOptimized(FParallelContext& Context)
{
    // For cache-optimized execution, we organize the workload to maximize cache locality
    int32 CacheLineSize = FPlatformMisc::GetCacheLineSize();
    int32 ElementsPerCacheLine = FMath::Max(CacheLineSize / sizeof(int32), 1);
    
    // Round granularity up to be a multiple of elements per cache line
    Context.Granularity = FMath::DivideAndRoundUp(Context.Granularity, ElementsPerCacheLine) * ElementsPerCacheLine;
    
    CreateWorkChunks(Context);
    return DistributeWork(Context);
}

bool FParallelExecutor::ShouldExecuteInParallel(int32 ItemCount) const
{
    // For very small workloads, sequential execution might be faster due to thread creation overhead
    int32 MinItemsForParallel = 128;
    
    return ItemCount >= MinItemsForParallel;
}