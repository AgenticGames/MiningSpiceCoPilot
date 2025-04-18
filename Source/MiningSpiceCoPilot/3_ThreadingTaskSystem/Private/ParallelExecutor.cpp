// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParallelExecutor.h"
#include "Async/TaskGraphInterfaces.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformAffinity.h"
#include "HAL/RunnableThread.h"
#include "HAL/ThreadSafeCounter.h"
#include "Misc/ScopeLock.h"
#include "Misc/SpinLock.h"
#include "HAL/ThreadHeartBeat.h"
#include "Misc/CoreMisc.h"
#include "Math/UnrealMathSSE.h"
#include "Stats/Stats.h"
#include "ProfilingDebugging/CsvProfiler.h"

DECLARE_CYCLE_STAT(TEXT("ParallelExecutor_ParallelFor"), STAT_ParallelExecutor_ParallelFor, STATGROUP_Threading);
DECLARE_CYCLE_STAT(TEXT("ParallelExecutor_WorkerThread"), STAT_ParallelExecutor_WorkerThread, STATGROUP_Threading);
DECLARE_CYCLE_STAT(TEXT("ParallelExecutor_WorkStealing"), STAT_ParallelExecutor_WorkStealing, STATGROUP_Threading);
DECLARE_CYCLE_STAT(TEXT("ParallelExecutor_SIMD"), STAT_ParallelExecutor_SIMD, STATGROUP_Threading);

CSV_DECLARE_CATEGORY_MODULE_EXTERN(MININGSPICECOPILOT_API, Threading);

// Constants for optimal performance
static constexpr int32 CACHE_LINE_SIZE = 64; // Cache line size for optimal performance
static const int32 MIN_ITEMS_PER_THREAD = 64;
static const int32 DEFAULT_GRANULARITY = 1024;
static const int32 WORK_STEALING_ATTEMPTS = 10;

// Define SIMD detection macros if not defined
#ifndef UE_SIMD_SSE
#define UE_SIMD_SSE 0
#endif

#ifndef UE_SIMD_SSE4
#define UE_SIMD_SSE4 0
#endif

#ifndef UE_SIMD_AVX
#define UE_SIMD_AVX 0
#endif

#ifndef UE_SIMD_AVX2
#define UE_SIMD_AVX2 0
#endif

// Static singleton instance
FParallelExecutor* FParallelExecutor::Instance = nullptr;

// Using proper alignment macros for different compilers
#if defined(_MSC_VER)
    #define PARALLEL_ALIGNMENT(Type) __declspec(align(CACHE_LINE_SIZE)) Type
#else
    #define PARALLEL_ALIGNMENT(Type) Type __attribute__((aligned(CACHE_LINE_SIZE)))
#endif

// AlignedStruct declaration
struct AlignedStruct { char dummy; };
typedef PARALLEL_ALIGNMENT(AlignedStruct) AlignedStructType;

//------------------------------------------------------------------------------
// FParallelCompletionEvent Implementation
//------------------------------------------------------------------------------

FParallelCompletionEvent::FParallelCompletionEvent()
    : ChunkCount(0)
    , CompletionEvent(nullptr)
{
    CompletedChunks.Set(0);
    CompletionEvent = FPlatformProcess::GetSynchEventFromPool(false);
}

FParallelCompletionEvent::~FParallelCompletionEvent()
{
    if (CompletionEvent)
    {
        FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);
        CompletionEvent = nullptr;
    }
}

void FParallelCompletionEvent::SignalCompletion()
{
    int32 Completed = CompletedChunks.Increment();
    
    // If all chunks are completed, signal the event
    if (Completed >= ChunkCount && CompletionEvent)
    {
        CompletionEvent->Trigger();
    }
}

void FParallelCompletionEvent::Wait()
{
    if (ChunkCount == 0 || CompletedChunks.GetValue() >= ChunkCount)
    {
        return;
    }
    
    if (CompletionEvent)
    {
        CompletionEvent->Wait();
    }
}

void FParallelCompletionEvent::SetChunkCount(int32 Count)
{
    ChunkCount = Count;
    
    // If count is 0, immediately signal completion
    if (ChunkCount == 0 && CompletionEvent)
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
    if (CompletionEvent)
    {
        CompletionEvent->Reset();
    }
    
    CompletedChunks.Set(0);
}

//------------------------------------------------------------------------------
// FParallelExecutor Implementation
//------------------------------------------------------------------------------

FParallelExecutor::FParallelExecutor()
    : ThreadCount(0)
{
    bIsExecuting.Set(0);
}

FParallelExecutor::~FParallelExecutor()
{
    // Wait for any ongoing execution to complete
    while (bIsExecuting.GetValue() > 0)
    {
        FPlatformProcess::SleepNoStats(0.001f);
    }
}

bool FParallelExecutor::ParallelFor(int32 ItemCount, TFunction<void(int32)> Function, 
    EParallelExecutionMode ExecutionMode, int32 Granularity)
{
    SCOPE_CYCLE_COUNTER(STAT_ParallelExecutor_ParallelFor);
    
    if (ItemCount <= 0 || !Function)
    {
        return false;
    }
    
    // Acquire lock to ensure only one parallel operation runs at a time
    FScopeLock Lock(&ContextLock);
    
    if (bIsExecuting.GetValue() > 0)
    {
        return false;
    }
    
    bIsExecuting.Increment();
    
    // Create a new context for this operation
    FParallelContext Context;
    Context.WorkItemFunction = Function;
    Context.ItemCount = ItemCount;
    Context.ExecutionMode = ExecutionMode;
    Context.NumThreads = ThreadCount > 0 ? ThreadCount : GetRecommendedThreadCount();
    Context.bUseWorkStealing = CurrentContext.bUseWorkStealing;
    Context.bUseThreadAffinity = CurrentContext.bUseThreadAffinity;
    
    // Determine granularity if not specified
    Context.Granularity = Granularity > 0 ? Granularity : DetermineOptimalGranularity(ItemCount, ExecutionMode);
    
    // Create completion event
    FParallelCompletionEvent CompletionEvent;
    Context.CompletionEvent = &CompletionEvent;
    
    // Create work chunks
    CreateWorkChunks(Context);
    CompletionEvent.SetChunkCount(Context.Chunks.Num());
    
    // Manually copy fields instead of using assignment operator
    CurrentContext.WorkItemFunction = Context.WorkItemFunction;
    CurrentContext.WorkRangeFunction = Context.WorkRangeFunction;
    CurrentContext.CompletionEvent = Context.CompletionEvent;
    CurrentContext.Chunks = Context.Chunks;
    CurrentContext.ExecutionMode = Context.ExecutionMode;
    CurrentContext.ItemCount = Context.ItemCount;
    CurrentContext.Granularity = Context.Granularity;
    CurrentContext.NumThreads = Context.NumThreads;
    CurrentContext.bCancelled.Set(0);
    CurrentContext.bUseWorkStealing = Context.bUseWorkStealing;
    CurrentContext.bUseThreadAffinity = Context.bUseThreadAffinity;
    
    bool bSuccess = false;
    
    // Choose execution strategy based on execution mode
    switch (ExecutionMode)
    {
        case EParallelExecutionMode::ForceSequential:
            bSuccess = ExecuteSequential(CurrentContext);
            break;
            
        case EParallelExecutionMode::SIMDOptimized:
            bSuccess = ExecuteSIMD(CurrentContext);
            break;
            
        case EParallelExecutionMode::CacheOptimized:
            bSuccess = ExecuteCacheOptimized(CurrentContext);
            break;
            
        case EParallelExecutionMode::ForceParallel:
            bSuccess = DistributeWork(CurrentContext);
            break;
            
        case EParallelExecutionMode::Automatic:
        case EParallelExecutionMode::Adaptive:
        default:
            // Decide whether to execute in parallel based on workload
            if (ShouldExecuteInParallel(ItemCount))
            {
                bSuccess = DistributeWork(CurrentContext);
            }
            else
            {
                bSuccess = ExecuteSequential(CurrentContext);
            }
            break;
    }
    
    // Wait for completion
    CurrentContext.CompletionEvent->Wait();
    
    bIsExecuting.Decrement();
    return bSuccess;
}

bool FParallelExecutor::ParallelForRange(int32 ItemCount, TFunction<void(int32, int32)> Function, 
    EParallelExecutionMode ExecutionMode, int32 Granularity)
{
    if (ItemCount <= 0 || !Function)
    {
        return false;
    }
    
    // Acquire lock to ensure only one parallel operation runs at a time
    FScopeLock Lock(&ContextLock);
    
    if (bIsExecuting.GetValue() > 0)
    {
        return false;
    }
    
    bIsExecuting.Increment();
    
    // Create a new context for this operation
    FParallelContext Context;
    Context.WorkRangeFunction = Function;
    Context.ItemCount = ItemCount;
    Context.ExecutionMode = ExecutionMode;
    Context.NumThreads = ThreadCount > 0 ? ThreadCount : GetRecommendedThreadCount();
    Context.bUseWorkStealing = CurrentContext.bUseWorkStealing;
    Context.bUseThreadAffinity = CurrentContext.bUseThreadAffinity;
    
    // Determine granularity if not specified
    Context.Granularity = Granularity > 0 ? Granularity : DetermineOptimalGranularity(ItemCount, ExecutionMode);
    
    // Create completion event
    FParallelCompletionEvent CompletionEvent;
    Context.CompletionEvent = &CompletionEvent;
    
    // Create work chunks
    CreateWorkChunks(Context);
    CompletionEvent.SetChunkCount(Context.Chunks.Num());
    
    // Manually copy fields instead of using assignment operator
    CurrentContext.WorkItemFunction = Context.WorkItemFunction;
    CurrentContext.WorkRangeFunction = Context.WorkRangeFunction;
    CurrentContext.CompletionEvent = Context.CompletionEvent;
    CurrentContext.Chunks = Context.Chunks;
    CurrentContext.ExecutionMode = Context.ExecutionMode;
    CurrentContext.ItemCount = Context.ItemCount;
    CurrentContext.Granularity = Context.Granularity;
    CurrentContext.NumThreads = Context.NumThreads;
    CurrentContext.bCancelled.Set(0);
    CurrentContext.bUseWorkStealing = Context.bUseWorkStealing;
    CurrentContext.bUseThreadAffinity = Context.bUseThreadAffinity;
    
    bool bSuccess = false;
    
    // Choose execution strategy based on execution mode
    switch (ExecutionMode)
    {
        case EParallelExecutionMode::ForceSequential:
            bSuccess = ExecuteSequential(CurrentContext);
            break;
            
        case EParallelExecutionMode::SIMDOptimized:
            bSuccess = ExecuteSIMD(CurrentContext);
            break;
            
        case EParallelExecutionMode::CacheOptimized:
            bSuccess = ExecuteCacheOptimized(CurrentContext);
            break;
            
        case EParallelExecutionMode::ForceParallel:
            bSuccess = DistributeWork(CurrentContext);
            break;
            
        case EParallelExecutionMode::Automatic:
        case EParallelExecutionMode::Adaptive:
        default:
            // Decide whether to execute in parallel based on workload
            if (ShouldExecuteInParallel(ItemCount))
            {
                bSuccess = DistributeWork(CurrentContext);
            }
            else
            {
                bSuccess = ExecuteSequential(CurrentContext);
            }
            break;
    }
    
    // Wait for all work to complete
    if (bSuccess)
    {
        CompletionEvent.Wait();
    }
    
    bIsExecuting.Decrement();
    return bSuccess;
}

bool FParallelExecutor::ParallelForSDF(int32 VoxelCount, TFunction<void(int32, int32)> Function, 
    EParallelExecutionMode ExecutionMode)
{
    if (VoxelCount <= 0 || !Function)
    {
        return false;
    }
    
    // Acquire lock to ensure only one parallel operation runs at a time
    FScopeLock Lock(&ContextLock);
    
    if (bIsExecuting.GetValue() > 0)
    {
        return false;
    }
    
    bIsExecuting.Increment();
    
    // Create a new context for this operation
    FParallelContext Context;
    Context.WorkRangeFunction = Function;
    Context.ItemCount = VoxelCount;
    Context.ExecutionMode = ExecutionMode;
    Context.NumThreads = ThreadCount > 0 ? ThreadCount : GetRecommendedThreadCount();
    Context.bUseWorkStealing = CurrentContext.bUseWorkStealing;
    Context.bUseThreadAffinity = CurrentContext.bUseThreadAffinity;
    
    // Determine optimal granularity for SIMD operations
    int32 SimdWidth = GetSIMDProcessingWidth();
    Context.Granularity = FMath::Max(DEFAULT_GRANULARITY, SimdWidth * 16);
    
    // Create completion event
    FParallelCompletionEvent CompletionEvent;
    Context.CompletionEvent = &CompletionEvent;
    
    // Create work chunks aligned to SIMD boundaries
    CreateWorkChunks(Context);
    CompletionEvent.SetChunkCount(Context.Chunks.Num());
    
    // Manually copy fields instead of using assignment operator
    CurrentContext.WorkItemFunction = Context.WorkItemFunction;
    CurrentContext.WorkRangeFunction = Context.WorkRangeFunction;
    CurrentContext.CompletionEvent = Context.CompletionEvent;
    CurrentContext.Chunks = Context.Chunks;
    CurrentContext.ExecutionMode = Context.ExecutionMode;
    CurrentContext.ItemCount = Context.ItemCount;
    CurrentContext.Granularity = Context.Granularity;
    CurrentContext.NumThreads = Context.NumThreads;
    CurrentContext.bCancelled.Set(0);
    CurrentContext.bUseWorkStealing = Context.bUseWorkStealing;
    CurrentContext.bUseThreadAffinity = Context.bUseThreadAffinity;
    
    // Execute using SIMD optimization
    bool bSuccess = ExecuteSIMD(CurrentContext);
    
    // Wait for completion
    CurrentContext.CompletionEvent->Wait();
    
    bIsExecuting.Decrement();
    return bSuccess;
}

bool FParallelExecutor::ParallelZones(const TArray<int32>& Zones, TFunction<void(int32)> Function, 
    EParallelExecutionMode ExecutionMode)
{
    if (Zones.Num() <= 0 || !Function)
    {
        return false;
    }
    
    // Acquire lock to ensure only one parallel operation runs at a time
    FScopeLock Lock(&ContextLock);
    
    if (bIsExecuting.GetValue() > 0)
    {
        return false;
    }
    
    bIsExecuting.Increment();
    
    // Create a new context instead of using assignment
    FParallelContext Context;
    Context.WorkItemFunction = Function;
    Context.ItemCount = Zones.Num();
    Context.ExecutionMode = ExecutionMode;
    Context.NumThreads = ThreadCount > 0 ? ThreadCount : GetRecommendedThreadCount();
    Context.bUseWorkStealing = CurrentContext.bUseWorkStealing;
    Context.bUseThreadAffinity = CurrentContext.bUseThreadAffinity;
    
    // Determine granularity - zones are typically processed individually
    Context.Granularity = FMath::Max(1, DetermineOptimalGranularity(Zones.Num(), ExecutionMode) / 4);
    
    // Create completion event
    FParallelCompletionEvent CompletionEvent;
    Context.CompletionEvent = &CompletionEvent;
    
    // Create work chunks
    CreateWorkChunks(Context);
    CompletionEvent.SetChunkCount(Context.Chunks.Num());
    
    // Manually copy fields to CurrentContext instead of assignment
    CurrentContext.WorkItemFunction = Context.WorkItemFunction;
    CurrentContext.WorkRangeFunction = Context.WorkRangeFunction;
    CurrentContext.CompletionEvent = Context.CompletionEvent;
    CurrentContext.Chunks = Context.Chunks;
    CurrentContext.ExecutionMode = Context.ExecutionMode;
    CurrentContext.ItemCount = Context.ItemCount;
    CurrentContext.Granularity = Context.Granularity;
    CurrentContext.NumThreads = Context.NumThreads;
    CurrentContext.bCancelled.Set(0);
    CurrentContext.bUseWorkStealing = Context.bUseWorkStealing;
    CurrentContext.bUseThreadAffinity = Context.bUseThreadAffinity;
    
    bool bSuccess = false;
    
    // For zone-based processing, we want cache-optimized execution by default
    if (ExecutionMode == EParallelExecutionMode::Automatic)
    {
        bSuccess = ExecuteCacheOptimized(CurrentContext);
    }
    else
    {
        // Choose execution strategy based on execution mode
        switch (ExecutionMode)
        {
            case EParallelExecutionMode::ForceSequential:
                bSuccess = ExecuteSequential(CurrentContext);
                break;
                
            case EParallelExecutionMode::CacheOptimized:
                bSuccess = ExecuteCacheOptimized(CurrentContext);
                break;
                
            case EParallelExecutionMode::ForceParallel:
            case EParallelExecutionMode::Adaptive:
            default:
                bSuccess = DistributeWork(CurrentContext);
                break;
        }
    }
    
    // Wait for all work to complete
    if (bSuccess)
    {
        CompletionEvent.Wait();
    }
    
    bIsExecuting.Decrement();
    
    // Create a wrapper for zone IDs
    TFunction<void(int32)> ZoneFunction = [&Zones, &Function](int32 Index)
    {
        if (Zones.IsValidIndex(Index))
        {
            Function(Zones[Index]);
        }
    };
    
    return ParallelFor(Zones.Num(), ZoneFunction, ExecutionMode);
}

void FParallelExecutor::Cancel()
{
    // Mark operation as cancelled
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
    
    if (CurrentContext.CompletionEvent)
    {
        double StartTime = FPlatformTime::Seconds();
        
        // If no timeout specified, wait indefinitely
        if (TimeoutMs == 0)
        {
            CurrentContext.CompletionEvent->Wait();
            return true;
        }
        
        // Wait with timeout
        double EndTime = StartTime + TimeoutMs / 1000.0;
        while (FPlatformTime::Seconds() < EndTime)
        {
            if (CurrentContext.CompletionEvent->GetCompletedChunks() >= CurrentContext.CompletionEvent->GetChunkCount())
            {
                return true;
            }
            
            FPlatformProcess::SleepNoStats(0.001f);
        }
        
        return false;
    }
    
    return false;
}

int32 FParallelExecutor::GetRecommendedThreadCount() const
{
    int32 NumCores = FPlatformMisc::NumberOfCores();
    int32 NumWorkerThreads = FMath::Max(1, NumCores - 1); // Leave one core for the game thread
    
    // On machines with many cores, use a percentage to avoid oversubscription
    if (NumCores > 16)
    {
        NumWorkerThreads = FMath::Max(1, (int32)(NumCores * 0.75f));
    }
    
    return NumWorkerThreads;
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

FParallelExecutor& FParallelExecutor::Get()
{
    if (Instance == nullptr)
    {
        Instance = new FParallelExecutor();
    }
    
    return *Instance;
}

int32 FParallelExecutor::DetermineOptimalGranularity(int32 ItemCount, EParallelExecutionMode ExecutionMode) const
{
    // Default granularity
    int32 Granularity = DEFAULT_GRANULARITY;
    
    const int32 NumThreads = ThreadCount > 0 ? ThreadCount : GetRecommendedThreadCount();
    
    switch (ExecutionMode)
    {
        case EParallelExecutionMode::SIMDOptimized:
        {
            // For SIMD, align granularity with SIMD processing width
            int32 SIMDWidth = GetSIMDProcessingWidth();
            Granularity = FMath::Max(DEFAULT_GRANULARITY, SIMDWidth * 64);
            break;
        }
        
        case EParallelExecutionMode::CacheOptimized:
        {
            // For cache-optimized execution, use smaller chunks that fit in L2 cache
            const int32 EstimatedItemSize = 64; // Assume average item size
            const int32 L2CacheSize = 256 * 1024; // Assume 256KB L2 cache
            Granularity = FMath::Max(64, L2CacheSize / EstimatedItemSize);
            break;
        }
        
        case EParallelExecutionMode::Adaptive:
        {
            // For adaptive execution, scale granularity with item count and thread count
            Granularity = FMath::Max(64, ItemCount / (NumThreads * 4));
            break;
        }
        
        default:
            break;
    }
    
    // Ensure reasonable limits
    Granularity = FMath::Clamp(Granularity, MIN_ITEMS_PER_THREAD, DEFAULT_GRANULARITY * 4);
    
    // Ensure we don't create too many tiny chunks
    const int32 MaxChunks = NumThreads * 8;
    const int32 MinGranularity = FMath::Max(1, ItemCount / MaxChunks);
    Granularity = FMath::Max(Granularity, MinGranularity);
    
    return Granularity;
}

void FParallelExecutor::CreateWorkChunks(FParallelContext& Context)
{
    const int32 NumThreads = Context.NumThreads;
    const int32 ItemCount = Context.ItemCount;
    const int32 Granularity = Context.Granularity;
    
    // Determine number of chunks
    int32 NumChunks = FMath::Max(1, (ItemCount + Granularity - 1) / Granularity);
    
    // Limit chunks to a reasonable multiple of thread count
    const int32 MaxChunks = NumThreads * 8;
    NumChunks = FMath::Min(NumChunks, MaxChunks);
    
    // Create chunks
    Context.Chunks.Empty(NumChunks);
    
    // Distribute items evenly across chunks
    const int32 ItemsPerChunk = FMath::Max(1, ItemCount / NumChunks);
    const int32 ExtraItems = ItemCount % NumChunks;
    
    int32 StartIndex = 0;
    for (int32 ChunkIndex = 0; ChunkIndex < NumChunks; ++ChunkIndex)
    {
        // Add one extra item to the first 'ExtraItems' chunks
        int32 ChunkSize = ItemsPerChunk + (ChunkIndex < ExtraItems ? 1 : 0);
        
        FWorkChunk Chunk;
        Chunk.StartIndex = StartIndex;
        Chunk.EndIndex = StartIndex + ChunkSize - 1;
        Chunk.ThreadIndex = ChunkIndex % NumThreads;
        Chunk.bCompleted = false;
        
        Context.Chunks.Add(Chunk);
        
        StartIndex += ChunkSize;
    }
}

void FParallelExecutor::ProcessChunk(FWorkChunk& Chunk, FParallelContext& Context)
{
    SCOPE_CYCLE_COUNTER(STAT_ParallelExecutor_WorkerThread);
    
    // Skip if cancelled
    if (Context.bCancelled.GetValue() != 0)
    {
        Chunk.bCompleted = true;
        Context.CompletionEvent->SignalCompletion();
        return;
    }
    
    // Process each item in the chunk
    if (Context.WorkRangeFunction)
    {
        // Process the entire range at once
        Context.WorkRangeFunction(Chunk.StartIndex, Chunk.EndIndex);
    }
    else if (Context.WorkItemFunction)
    {
        // Process each item individually
        for (int32 ItemIndex = Chunk.StartIndex; ItemIndex <= Chunk.EndIndex; ++ItemIndex)
        {
            Context.WorkItemFunction(ItemIndex);
            
            // Check for cancellation
            if (Context.bCancelled.GetValue() != 0)
            {
                break;
            }
        }
    }
    
    // Mark as completed
    Chunk.bCompleted = true;
    
    // Signal completion
    if (Context.CompletionEvent)
    {
        Context.CompletionEvent->SignalCompletion();
    }
}

bool FParallelExecutor::StealWork(FParallelContext& Context, int32 ThreadIndex, FWorkChunk& OutChunk)
{
    SCOPE_CYCLE_COUNTER(STAT_ParallelExecutor_WorkStealing);
    
    if (!Context.bUseWorkStealing)
    {
        return false;
    }
    
    // Try to find uncompleted chunks assigned to other threads
    for (int32 ChunkIndex = 0; ChunkIndex < Context.Chunks.Num(); ++ChunkIndex)
    {
        FWorkChunk& Chunk = Context.Chunks[ChunkIndex];
        
        // Skip completed chunks or chunks assigned to this thread
        if (Chunk.bCompleted || Chunk.ThreadIndex == ThreadIndex)
        {
            continue;
        }
        
        // Try to steal the chunk
        if (FPlatformAtomics::InterlockedCompareExchange((int32*)&Chunk.ThreadIndex, ThreadIndex, Chunk.ThreadIndex) == Chunk.ThreadIndex)
        {
            OutChunk = Chunk;
            return true;
        }
    }
    
    return false;
}

bool FParallelExecutor::DistributeWork(FParallelContext& Context)
{
    const int32 NumThreads = Context.NumThreads;
    
    // Use TaskGraph for work distribution
    FGraphEventArray CompletionEvents;
    CompletionEvents.Reserve(Context.Chunks.Num());
    
    // Create tasks for each chunk
    for (int32 ChunkIndex = 0; ChunkIndex < Context.Chunks.Num(); ++ChunkIndex)
    {
        FWorkChunk& Chunk = Context.Chunks[ChunkIndex];
        
        // Create a task to process this chunk
        FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady(
            [this, ChunkIndex]()
            {
                FWorkChunk& ChunkToProcess = CurrentContext.Chunks[ChunkIndex];
                ProcessChunk(ChunkToProcess, CurrentContext);
                
                // If work stealing is enabled, try to steal and process more chunks
                if (CurrentContext.bUseWorkStealing)
                {
                    FWorkChunk StolenChunk;
                    int32 Attempts = 0;
                    
                    while (StealWork(CurrentContext, ChunkToProcess.ThreadIndex, StolenChunk) && Attempts < WORK_STEALING_ATTEMPTS)
                    {
                        ProcessChunk(StolenChunk, CurrentContext);
                        Attempts++;
                    }
                }
            },
            TStatId(),
            nullptr,
            ENamedThreads::AnyThread
        );
        
        CompletionEvents.Add(Task);
    }
    
    return true;
}

bool FParallelExecutor::ExecuteSequential(FParallelContext& Context)
{
    // Process all chunks sequentially
    for (FWorkChunk& Chunk : Context.Chunks)
    {
        ProcessChunk(Chunk, Context);
        
        // Stop if cancelled
        if (Context.bCancelled.GetValue() != 0)
        {
            break;
        }
    }
    
    return true;
}

bool FParallelExecutor::ExecuteSIMD(FParallelContext& Context)
{
    SCOPE_CYCLE_COUNTER(STAT_ParallelExecutor_SIMD);
    
    // Choose appropriate SIMD implementation based on CPU features
#if UE_SIMD_SSE4
    return ExecuteSIMD_SSE4(Context);
#elif UE_SIMD_SSE
    return ExecuteSIMD_SSE(Context);
#elif UE_SIMD_AVX2
    return ExecuteSIMD_AVX2(Context);
#elif UE_SIMD_AVX
    return ExecuteSIMD_AVX(Context);
#else
    // Fall back to normal distribution if no SIMD support
    return DistributeWork(Context);
#endif
}

bool FParallelExecutor::ExecuteSIMD_SSE(FParallelContext& Context)
{
    // SSE processes 4 floats at a time
    const int32 SIMDWidth = 4;
    
    // Adjust granularity to be a multiple of SIMD width
    Context.Granularity = ((Context.Granularity + SIMDWidth - 1) / SIMDWidth) * SIMDWidth;
    
    // Recreate chunks with aligned boundaries
    CreateWorkChunks(Context);
    
    // Use standard distribution with adjusted chunks
    return DistributeWork(Context);
}

bool FParallelExecutor::ExecuteSIMD_SSE4(FParallelContext& Context)
{
    // SSE4 processes 4 floats at a time
    const int32 SIMDWidth = 4;
    
    // Adjust granularity to be a multiple of SIMD width
    Context.Granularity = ((Context.Granularity + SIMDWidth - 1) / SIMDWidth) * SIMDWidth;
    
    // Recreate chunks with aligned boundaries
    CreateWorkChunks(Context);
    
    // Use standard distribution with adjusted chunks
    return DistributeWork(Context);
}

bool FParallelExecutor::ExecuteSIMD_AVX(FParallelContext& Context)
{
    // AVX processes 8 floats at a time
    const int32 SIMDWidth = 8;
    
    // Adjust granularity to be a multiple of SIMD width
    Context.Granularity = ((Context.Granularity + SIMDWidth - 1) / SIMDWidth) * SIMDWidth;
    
    // Recreate chunks with aligned boundaries
    CreateWorkChunks(Context);
    
    // Use standard distribution with adjusted chunks
    return DistributeWork(Context);
}

bool FParallelExecutor::ExecuteSIMD_AVX2(FParallelContext& Context)
{
    // AVX2 processes 8 floats at a time
    const int32 SIMDWidth = 8;
    
    // Adjust granularity to be a multiple of SIMD width
    Context.Granularity = ((Context.Granularity + SIMDWidth - 1) / SIMDWidth) * SIMDWidth;
    
    // Recreate chunks with aligned boundaries
    CreateWorkChunks(Context);
    
    // Use standard distribution with adjusted chunks
    return DistributeWork(Context);
}

bool FParallelExecutor::ExecuteCacheOptimized(FParallelContext& Context)
{
    // For cache-optimized execution, we sort chunks by memory locality 
    // In this simplified implementation, we just adjust granularity and thread affinity
    
    // Modify context for cache-optimized execution
    Context.bUseThreadAffinity = true;
    
    // Adjust granularity for better cache utilization
    const int32 EstimatedItemSize = 64; // Assume average item size in bytes
    const int32 L2CacheSize = 256 * 1024; // Assume 256KB L2 cache
    Context.Granularity = FMath::Max(64, L2CacheSize / EstimatedItemSize);
    
    // Recreate chunks with cache-friendly sizes
    CreateWorkChunks(Context);
    
    // Use standard distribution with adjusted settings
    return DistributeWork(Context);
}

bool FParallelExecutor::ShouldExecuteInParallel(int32 ItemCount) const
{
    // Only parallelize if we have enough work
    if (ItemCount < MIN_ITEMS_PER_THREAD)
    {
        return false;
    }
    
    const int32 NumThreads = ThreadCount > 0 ? ThreadCount : GetRecommendedThreadCount();
    
    // Check if we have enough items to justify parallelization
    // Rule of thumb: at least 64 items per thread
    if (ItemCount < NumThreads * MIN_ITEMS_PER_THREAD)
    {
        return false;
    }
    
    return true;
}

int32 FParallelExecutor::GetSIMDProcessingWidth() const
{
#if UE_SIMD_AVX2 || UE_SIMD_AVX
    return 8; // 8 floats per SIMD register
#elif UE_SIMD_SSE4 || UE_SIMD_SSE
    return 4; // 4 floats per SIMD register
#else
    return 1; // No SIMD
#endif
}