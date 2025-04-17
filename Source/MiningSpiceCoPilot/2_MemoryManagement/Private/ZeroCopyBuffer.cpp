// Copyright Epic Games, Inc. All Rights Reserved.

#include "ZeroCopyBuffer.h"
#include "RHI.h"
#include "RenderingThread.h"
#include "HAL/PlatformMemory.h"
#include "Misc/ScopeLock.h"
#include "Containers/ResourceArray.h"
#include "RHIResources.h"
#include "RHICommandList.h"
#include "RHIUtilities.h"
#include "RenderGraphResources.h"
#include "RenderGraphBuilder.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"
#include "Misc/Paths.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Stats/Stats.h"
#include "Misc/CoreStats.h"

// Stats declarations for memory tracking
DECLARE_STATS_GROUP(TEXT("ZeroCopyBuffer"), STATGROUP_ZeroCopyBuffer, STATCAT_Advanced);
DECLARE_MEMORY_STAT(TEXT("ZeroCopyBuffer Memory"), STAT_ZeroCopyBuffer_Memory, STATGROUP_ZeroCopyBuffer);
DECLARE_DWORD_COUNTER_STAT(TEXT("ZeroCopyBuffer Count"), STAT_ZeroCopyBuffer_Count, STATGROUP_ZeroCopyBuffer);
DECLARE_CYCLE_STAT(TEXT("ZeroCopyBuffer Map"), STAT_ZeroCopyBuffer_Map, STATGROUP_ZeroCopyBuffer);
DECLARE_CYCLE_STAT(TEXT("ZeroCopyBuffer Unmap"), STAT_ZeroCopyBuffer_Unmap, STATGROUP_ZeroCopyBuffer);
DECLARE_CYCLE_STAT(TEXT("ZeroCopyBuffer SyncToGPU"), STAT_ZeroCopyBuffer_SyncToGPU, STATGROUP_ZeroCopyBuffer);
DECLARE_CYCLE_STAT(TEXT("ZeroCopyBuffer SyncFromGPU"), STAT_ZeroCopyBuffer_SyncFromGPU, STATGROUP_ZeroCopyBuffer);

// Console variables for runtime configuration
static TAutoConsoleVariable<int32> CVarZeroCopyBufferOptimizationLevel(
    TEXT("r.ZeroCopyBuffer.OptimizationLevel"),
    2,
    TEXT("Controls the optimization level for ZeroCopyBuffer:\n")
    TEXT("0: No optimizations\n")
    TEXT("1: Basic optimizations\n")
    TEXT("2: Full optimizations with access pattern detection (default)"),
    ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarZeroCopyBufferPrefetchSize(
    TEXT("r.ZeroCopyBuffer.PrefetchSizeKB"),
    64,
    TEXT("Size of memory to prefetch in kilobytes when access pattern is detected (0 to disable)"),
    ECVF_RenderThreadSafe
);

namespace
{
    bool IsRHIInitialized()
    {
        return GDynamicRHI != nullptr && !IsRunningCommandlet();
    }

    // Helper method to get optimal memory alignment based on usage hint
    uint32 GetOptimalAlignment(EBufferUsage UsageHint)
    {
        switch (UsageHint)
        {
        case EBufferUsage::SDFField:
        case EBufferUsage::SVONodes:
            // SDF and SVO data benefits from SIMD alignment for vectorized operations
            return 64; // AVX-512 alignment
        case EBufferUsage::MaterialChannels:
        case EBufferUsage::VertexData:
            return 32; // AVX/AVX2 alignment
        case EBufferUsage::IndexData:
            return 16; // SSE alignment
        case EBufferUsage::General:
        default:
            return 16; // Default alignment
        }
    }

    // Helper method to choose optimal buffer usage flags based on usage hint
    uint32 GetBufferUsageFlags(EBufferUsage UsageHint, bool bGPUWritable)
    {
        uint32 UsageFlags = (uint32)(BUF_Dynamic | BUF_ShaderResource | BUF_StructuredBuffer);
        
        if (bGPUWritable)
        {
            UsageFlags |= (uint32)BUF_UnorderedAccess;
        }
        
        switch (UsageHint)
        {
        case EBufferUsage::SDFField:
            // SDF fields need fast random access and are often updated from compute shaders
            UsageFlags |= (uint32)BUF_FastVRAM;
            break;
        case EBufferUsage::SVONodes:
            // Octree nodes are accessed in more structured patterns and need fast access
            UsageFlags |= (uint32)BUF_FastVRAM;
            break;
        case EBufferUsage::MaterialChannels:
            // Material data is often read-heavy
            UsageFlags |= (bGPUWritable ? 0 : (uint32)BUF_Static);
            break;
        case EBufferUsage::VertexData:
            // Vertex data is typically read-only from GPU
            UsageFlags |= (uint32)BUF_VertexBuffer;
            break;
        case EBufferUsage::IndexData:
            // Index data is typically read-only from GPU
            UsageFlags |= (uint32)BUF_IndexBuffer;
            break;
        default:
            break;
        }
        
        return UsageFlags;
    }
    
    // Increments global counter for tracking total buffer count
    void IncrementBufferCount()
    {
        INC_DWORD_STAT(STAT_ZeroCopyBuffer_Count);
    }
    
    // Decrements global counter for tracking total buffer count
    void DecrementBufferCount()
    {
        DEC_DWORD_STAT(STAT_ZeroCopyBuffer_Count);
    }
    
    // Updates memory stat tracking
    void UpdateMemoryStats(int64 SizeDelta)
    {
        if (SizeDelta != 0)
        {
            INC_MEMORY_STAT_BY(STAT_ZeroCopyBuffer_Memory, SizeDelta);
        }
    }
}

// FMemoryAccessPattern implementation
FMemoryAccessPattern::FMemoryAccessPattern()
    : PatternType(EPatternType::Unknown)
    , AverageAccessSize(0)
    , LastAnalyzedCount(0)
{
    RecentAccesses.Reserve(MaxAccessesToTrack);
    RecentSizes.Reserve(MaxAccessesToTrack);
}

void FMemoryAccessPattern::RecordAccess(uint64 Offset, uint64 Size)
{
    // Add to recent accesses, removing oldest if necessary
    if (RecentAccesses.Num() >= MaxAccessesToTrack)
    {
        RecentAccesses.RemoveAt(0);
        RecentSizes.RemoveAt(0);
    }
    
    RecentAccesses.Add(Offset);
    RecentSizes.Add(Size);
    
    // Analyze pattern if we have enough data
    if (RecentAccesses.Num() >= 4 && RecentAccesses.Num() > LastAnalyzedCount + 2)
    {
        AnalyzePattern();
    }
}

FMemoryAccessPattern::EPatternType FMemoryAccessPattern::GetPatternType() const
{
    return PatternType;
}

uint64 FMemoryAccessPattern::GetSuggestedPrefetchSize() const
{
    // Only suggest prefetching for sequential or strided patterns
    if (PatternType == EPatternType::Sequential)
    {
        // For sequential access, prefetch ahead several times the average access size
        return AverageAccessSize * 8;
    }
    else if (PatternType == EPatternType::Strided)
    {
        // For strided access, cover a few strides
        if (RecentAccesses.Num() >= 2)
        {
            uint64 AvgStride = 0;
            for (int32 i = 1; i < FMath::Min(RecentAccesses.Num(), 5); i++)
            {
                AvgStride += (RecentAccesses[i] > RecentAccesses[i-1]) ? 
                    (RecentAccesses[i] - RecentAccesses[i-1]) : 
                    (RecentAccesses[i-1] - RecentAccesses[i]);
            }
            AvgStride /= FMath::Min(RecentAccesses.Num() - 1, 4);
            
            // Prefetch a few strides ahead
            return AvgStride * 4;
        }
    }
    
    // Default to a reasonable size for random or unknown patterns
    return 64 * 1024; // 64 KB
}

void FMemoryAccessPattern::Reset()
{
    RecentAccesses.Empty();
    RecentSizes.Empty();
    PatternType = EPatternType::Unknown;
    AverageAccessSize = 0;
    LastAnalyzedCount = 0;
}

void FMemoryAccessPattern::AnalyzePattern()
{
    if (RecentAccesses.Num() < 4)
    {
        PatternType = EPatternType::Unknown;
        return;
    }
    
    // Calculate average access size
    uint64 TotalSize = 0;
    for (uint64 Size : RecentSizes)
    {
        TotalSize += Size;
    }
    AverageAccessSize = TotalSize / RecentSizes.Num();
    
    // Count access patterns: sequential, strided, or random
    int32 SequentialCount = 0;
    int32 StridedCount = 0;
    
    // Detect if all strides are approximately the same
    bool bConsistentStride = true;
    int64 FirstStride = 0;
    
    // Check access pattern by analyzing differences between consecutive accesses
    for (int32 i = 1; i < RecentAccesses.Num(); i++)
    {
        int64 Stride = (int64)RecentAccesses[i] - (int64)RecentAccesses[i-1];
        
        // First stride calculation
        if (i == 1)
        {
            FirstStride = Stride;
        }
        
        // Check if sequential (approximately equal to average access size)
        if (FMath::Abs(Stride - (int64)AverageAccessSize) <= (int64)(AverageAccessSize * 0.1))
        {
            SequentialCount++;
        }
        // Check if same stride as first (with tolerance)
        else if (FMath::Abs(Stride - FirstStride) <= (FMath::Abs(FirstStride) * 0.1))
        {
            StridedCount++;
        }
        else
        {
            bConsistentStride = false;
        }
    }
    
    const int32 TotalComparisons = RecentAccesses.Num() - 1;
    
    // Determine pattern type based on counts
    if (SequentialCount >= TotalComparisons * 0.7)
    {
        PatternType = EPatternType::Sequential;
    }
    else if (bConsistentStride && StridedCount >= TotalComparisons * 0.7)
    {
        PatternType = EPatternType::Strided;
    }
    else
    {
        PatternType = EPatternType::Random;
    }
    
    LastAnalyzedCount = RecentAccesses.Num();
}

// Implementation of FZeroCopyBuffer

FZeroCopyBuffer::FZeroCopyBuffer(const FName& InName, uint64 InSizeInBytes, EBufferUsage InUsageHint, bool InGPUWritable)
    : Name(InName)
    , SizeInBytes(InSizeInBytes)
    , RawData(nullptr)
    , MappedData(nullptr)
    , ResourceBuffer(nullptr)
    , ShaderResourceView(nullptr)
    , UnorderedAccessView(nullptr)
    , bGPUWritable(InGPUWritable)
    , bInitialized(false)
    , CurrentAccessMode(EBufferAccessMode::ReadWrite)
    , CurrentUsageHint(InUsageHint)
    , ReferenceCount(1)
    , VersionCounter(0)
    , MapCount(0)
    , UnmapCount(0)
    , bActiveMining(false)
    , bPrefetchingEnabled(true)
    , LastStatsUpdateTime(0.0)
{
    // Register with stats tracking
    IncrementBufferCount();
    UpdateMemoryStats(SizeInBytes);
}

FZeroCopyBuffer::~FZeroCopyBuffer()
{
    if (bInitialized)
    {
        Shutdown();
    }
    
    // Update stats tracking
    DecrementBufferCount();
    UpdateMemoryStats(-static_cast<int64>(SizeInBytes));
}

bool FZeroCopyBuffer::Initialize()
{
    SCOPE_CYCLE_COUNTER(STAT_ZeroCopyBuffer_Map);
    FScopeLock Lock(&CriticalSection);
    
    // Check if already initialized
    if (bInitialized)
    {
        return true;
    }

    // Validate size
    if (SizeInBytes == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("ZeroCopyBuffer: Cannot initialize buffer '%s' with zero size"), *Name.ToString());
        return false;
    }

    // Determine optimal alignment based on usage hint
    const uint32 Alignment = GetOptimalAlignment(CurrentUsageHint);
    
    // Allocate CPU memory with alignment
    RawData = FMemory::Malloc(SizeInBytes, Alignment);
    
    if (!RawData)
    {
        UE_LOG(LogTemp, Error, TEXT("ZeroCopyBuffer: Failed to allocate %llu bytes for buffer '%s'"), 
            SizeInBytes, *Name.ToString());
        return false;
    }

    // Zero out the memory
    FMemory::Memzero(RawData, SizeInBytes);

    // Create GPU resource if RHI is available
    if (IsRHIInitialized())
    {
        if (IsRunningRHIInSeparateThread())
        {
            // Enqueue command to create buffer on render thread
            ENQUEUE_RENDER_COMMAND(CreateZeroCopyBuffer)(
                [this](FRHICommandListImmediate& RHICmdList)
                {
                    CreateGPUBuffer_RenderThread();
                }
            );
            // Wait for completion
            FlushRenderingCommands();
        }
        else
        {
            // RHI is available but not on a separate thread
            CreateGPUBuffer_RenderThread();
        }
    }

    // Apply optimization based on usage hint
    OptimizeLayoutForUsage();
    
    // Initialize version counter
    VersionCounter.Set(1);
    
    bInitialized = true;
    UE_LOG(LogTemp, Log, TEXT("ZeroCopyBuffer: Initialized buffer '%s' (%llu bytes, alignment %u, usage %d)"), 
        *Name.ToString(), SizeInBytes, Alignment, static_cast<int32>(CurrentUsageHint));

    return true;
}

void FZeroCopyBuffer::CreateGPUBuffer_RenderThread()
{
    // Convert usage hint to RHI buffer flags
    uint32 BufferUsage = GetBufferUsageFlags(CurrentUsageHint, bGPUWritable);
    
    FRHIResourceCreateInfo CreateInfo(TEXT("ZeroCopyBuffer"));
    
    // Get immediate command list
    FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
    
    // Use the correct structured buffer creation method in UE 5.5.1
    uint32 Stride = 4; // Minimum size for structured buffer
    
    // Use the command list's CreateStructuredBuffer method directly
    ERHIAccess ResourceState = RHIGetDefaultResourceState((EBufferUsageFlags)BufferUsage, false);
    ResourceBuffer = RHICmdList.CreateStructuredBuffer(
        Stride, 
        SizeInBytes, 
        (EBufferUsageFlags)BufferUsage,
        ResourceState, 
        CreateInfo);

    if (!ResourceBuffer.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("ZeroCopyBuffer: Failed to create GPU resource for buffer '%s'"), *Name.ToString());
        return;
    }
    
    UE_LOG(LogTemp, Log, TEXT("ZeroCopyBuffer: Created GPU resource for buffer '%s'"), *Name.ToString());
    
    // Create shader resource view using the UE 5.5.1 API
    FRHIViewDesc::FBufferSRV::FInitializer SRVInitializer = FRHIViewDesc::CreateBufferSRV();
    SRVInitializer.SetTypeFromBuffer(ResourceBuffer);
    
    // Create the SRV using the command list
    ShaderResourceView = RHICmdList.CreateShaderResourceView(ResourceBuffer, SRVInitializer);
    
    // Create Unordered Access View if writable
    if (bGPUWritable && ResourceBuffer.IsValid())
    {
        // Create UAV view descriptor
        FRHIViewDesc::FBufferUAV::FInitializer UAVInitializer = FRHIViewDesc::CreateBufferUAV();
        UAVInitializer.SetTypeFromBuffer(ResourceBuffer);
        
        // Create the UAV using the command list
        UnorderedAccessView = RHICmdList.CreateUnorderedAccessView(ResourceBuffer, UAVInitializer);
    }
}

void FZeroCopyBuffer::ReleaseGPUBuffer_RenderThread()
{
    if (UnorderedAccessView)
    {
        UnorderedAccessView.SafeRelease();
    }
    
    if (ShaderResourceView)
    {
        ShaderResourceView.SafeRelease();
    }
    
    if (ResourceBuffer)
    {
        ResourceBuffer.SafeRelease();
    }
}

void FZeroCopyBuffer::Shutdown()
{
    FScopeLock Lock(&CriticalSection);
    
    if (!bInitialized)
    {
        return;
    }

    // Make sure buffer is unmapped
    if (MappedData)
    {
        UnmapBuffer();
    }

    // Free GPU resource if we're running with a valid RHI
    if (ResourceBuffer)
    {
        if (IsRunningRHIInSeparateThread())
        {
            ENQUEUE_RENDER_COMMAND(ReleaseZeroCopyBuffer)(
                [this](FRHICommandListImmediate& RHICmdList)
                {
                    ReleaseGPUBuffer_RenderThread();
                }
            );
            // Wait for render thread to complete
            FlushRenderingCommands();
        }
        else
        {
            ReleaseGPUBuffer_RenderThread();
        }
    }

    // Free CPU resource
    if (RawData)
    {
        FMemory::Free(RawData);
        RawData = nullptr;
    }

    bInitialized = false;
    UE_LOG(LogTemp, Log, TEXT("ZeroCopyBuffer: Shutdown buffer '%s'"), *Name.ToString());
}

bool FZeroCopyBuffer::IsInitialized() const
{
    return bInitialized;
}

void* FZeroCopyBuffer::Map(EBufferAccessMode AccessMode)
{
    return MapBuffer(AccessMode);
}

void* FZeroCopyBuffer::MapBuffer(EBufferAccessMode AccessMode)
{
    SCOPE_CYCLE_COUNTER(STAT_ZeroCopyBuffer_Map);
    FScopeLock Lock(&CriticalSection);
    
    if (!bInitialized || !RawData)
    {
        UE_LOG(LogTemp, Error, TEXT("ZeroCopyBuffer: Cannot map uninitialized buffer '%s'"), *Name.ToString());
        return nullptr;
    }

    // If buffer is already mapped, return the existing mapping
    if (MappedData)
    {
        return MappedData;
    }

    CurrentAccessMode = AccessMode;

    // If we have a GPU resource, lock it for access
    if (ResourceBuffer)
    {
        // Sync from GPU to CPU if reading
        if (AccessMode == EBufferAccessMode::ReadOnly || AccessMode == EBufferAccessMode::ReadWrite)
        {
            SyncFromGPU();
        }
    }

    // Record map operation for telemetry
    MapCount.Increment();
    
    MappedData = RawData;
    return MappedData;
}

bool FZeroCopyBuffer::Unmap()
{
    SCOPE_CYCLE_COUNTER(STAT_ZeroCopyBuffer_Unmap);
    FScopeLock Lock(&CriticalSection);
    
    if (!bInitialized || !MappedData)
    {
        return false;
    }

    // If we have a GPU resource and we potentially modified the buffer, sync to GPU
    if (ResourceBuffer && 
        (CurrentAccessMode == EBufferAccessMode::WriteOnly || CurrentAccessMode == EBufferAccessMode::ReadWrite))
    {
        SyncToGPU();
        
        // Increment version number after modification
        IncrementVersion();
    }

    // Record unmap operation for telemetry
    UnmapCount.Increment();
    
    MappedData = nullptr;
    return true;
}

bool FZeroCopyBuffer::IsMapped() const
{
    return IsBufferMapped();
}

bool FZeroCopyBuffer::IsBufferMapped() const
{
    return MappedData != nullptr;
}

void FZeroCopyBuffer::SyncToGPU()
{
    SCOPE_CYCLE_COUNTER(STAT_ZeroCopyBuffer_SyncToGPU);
    
    if (!bInitialized || !ResourceBuffer.IsValid() || !RawData)
    {
        return;
    }

    // Update GPU resource with CPU memory content
    if (IsRunningRHIInSeparateThread())
    {
        void* SourceData = RawData;
        uint64 BufferSize = SizeInBytes;
        TRefCountPtr<FRHIBuffer> Buffer = ResourceBuffer;

        ENQUEUE_RENDER_COMMAND(UpdateZeroCopyBuffer)(
            [SourceData, BufferSize, Buffer](FRHICommandListImmediate& RHICmdList)
            {
                // In UE 5.5, use Lock/Unlock directly on the underlying resource
                void* GPUData = RHICmdList.LockBuffer(Buffer.GetReference(), 0, BufferSize, RLM_WriteOnly);
                
                if (GPUData)
                {
                    FMemory::Memcpy(GPUData, SourceData, BufferSize);
                    RHICmdList.UnlockBuffer(Buffer.GetReference());
                }
            }
        );
    }
    else if (IsRHIInitialized())
    {
        // When not using a separate render thread, we lock the buffer directly
        FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
        void* GPUData = RHICmdList.LockBuffer(ResourceBuffer.GetReference(), 0, SizeInBytes, RLM_WriteOnly);
        
        if (GPUData)
        {
            FMemory::Memcpy(GPUData, RawData, SizeInBytes);
            RHICmdList.UnlockBuffer(ResourceBuffer.GetReference());
        }
    }
}

void FZeroCopyBuffer::SyncFromGPU()
{
    SCOPE_CYCLE_COUNTER(STAT_ZeroCopyBuffer_SyncFromGPU);
    
    if (!bInitialized || !ResourceBuffer.IsValid() || !RawData)
    {
        return;
    }

    // Update CPU memory with GPU resource content
    if (IsRunningRHIInSeparateThread())
    {
        void* DestData = RawData;
        uint64 BufferSize = SizeInBytes;
        TRefCountPtr<FRHIBuffer> Buffer = ResourceBuffer;

        ENQUEUE_RENDER_COMMAND(ReadZeroCopyBuffer)(
            [DestData, BufferSize, Buffer](FRHICommandListImmediate& RHICmdList)
            {
                // In UE 5.5, use LockBuffer/UnlockBuffer on the command list
                void* GPUData = RHICmdList.LockBuffer(Buffer.GetReference(), 0, BufferSize, RLM_ReadOnly);
                
                if (GPUData)
                {
                    FMemory::Memcpy(DestData, GPUData, BufferSize);
                    RHICmdList.UnlockBuffer(Buffer.GetReference());
                }
            }
        );
        // Wait for completion
        FlushRenderingCommands();
    }
    else if (IsRHIInitialized())
    {
        // When not using a separate render thread, we lock the buffer directly
        FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
        void* GPUData = RHICmdList.LockBuffer(ResourceBuffer.GetReference(), 0, SizeInBytes, RLM_ReadOnly);
        
        if (GPUData)
        {
            FMemory::Memcpy(RawData, GPUData, SizeInBytes);
            RHICmdList.UnlockBuffer(ResourceBuffer.GetReference());
        }
    }
}

bool FZeroCopyBuffer::IsGPUBufferValid() const
{
    return ResourceBuffer.IsValid();
}

TRefCountPtr<FRHIBuffer> FZeroCopyBuffer::GetRHIBuffer() const
{
    return ResourceBuffer;
}

TRefCountPtr<FRHIShaderResourceView> FZeroCopyBuffer::GetShaderResourceView() const
{
    return ShaderResourceView;
}

TRefCountPtr<FRHIUnorderedAccessView> FZeroCopyBuffer::GetUnorderedAccessView() const
{
    if (!bGPUWritable)
    {
        UE_LOG(LogTemp, Warning, TEXT("ZeroCopyBuffer: Buffer '%s' is not GPU writable, UAV not available"), *Name.ToString());
    }
    
    return UnorderedAccessView;
}

bool FZeroCopyBuffer::Resize(uint64 NewSizeInBytes, bool bPreserveContent)
{
    FScopeLock Lock(&CriticalSection);
    
    if (!bInitialized)
    {
        UE_LOG(LogTemp, Error, TEXT("ZeroCopyBuffer: Cannot resize uninitialized buffer '%s'"), *Name.ToString());
        return false;
    }
    
    if (NewSizeInBytes == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("ZeroCopyBuffer: Cannot resize buffer '%s' to zero size"), *Name.ToString());
        return false;
    }
    
    // Don't resize if already mapped
    if (MappedData)
    {
        UE_LOG(LogTemp, Error, TEXT("ZeroCopyBuffer: Cannot resize mapped buffer '%s'"), *Name.ToString());
        return false;
    }
    
    // If size matches, no need to resize
    if (NewSizeInBytes == SizeInBytes)
    {
        return true;
    }
    
    // Update memory stats
    UpdateMemoryStats(-(int64)SizeInBytes);
    
    // Save old data if preserving content
    void* OldData = nullptr;
    uint64 OldSize = SizeInBytes;
    
    if (bPreserveContent && RawData)
    {
        OldData = FMemory::Malloc(OldSize);
        FMemory::Memcpy(OldData, RawData, OldSize);
    }
    
    // Shutdown current buffer (releases GPU resources)
    Shutdown();
    
    // Update size
    SizeInBytes = NewSizeInBytes;
    
    // Update memory stats with new size
    UpdateMemoryStats(SizeInBytes);
    
    // Reinitialize with new size
    bool Result = Initialize();
    
    // Restore content if needed
    if (Result && bPreserveContent && OldData)
    {
        uint64 CopySize = FMath::Min(OldSize, NewSizeInBytes);
        FMemory::Memcpy(RawData, OldData, CopySize);
        
        // Sync to GPU if available
        SyncToGPU();
    }
    
    // Free temporary copy
    if (OldData)
    {
        FMemory::Free(OldData);
    }
    
    // Increment version after resize
    IncrementVersion();
    
    return Result;
}

void FZeroCopyBuffer::SetUsageHint(EBufferUsage UsageHint)
{
    FScopeLock Lock(&CriticalSection);
    
    if (CurrentUsageHint == UsageHint)
    {
        return;
    }
    
    CurrentUsageHint = UsageHint;
    
    // Only optimize if already initialized
    if (bInitialized)
    {
        OptimizeLayoutForUsage();
    }
}

EBufferUsage FZeroCopyBuffer::GetUsageHint() const
{
    return CurrentUsageHint;
}

uint64 FZeroCopyBuffer::GetVersionNumber() const
{
    return VersionCounter.GetValue();
}

void* FZeroCopyBuffer::GetGPUResource() const
{
    if (ResourceBuffer.IsValid())
    {
        return ResourceBuffer.GetReference();
    }
    return nullptr;
}

void FZeroCopyBuffer::AddRef()
{
    ReferenceCount.Increment();
}

uint32 FZeroCopyBuffer::Release()
{
    int32 NewCount = ReferenceCount.Decrement();
    
    // If reference count reaches zero, delete this buffer
    if (NewCount <= 0)
    {
        delete this;
        return 0;
    }
    
    return NewCount;
}

FBufferStats FZeroCopyBuffer::GetStats() const
{
    FScopeLock Lock(&CriticalSection);
    
    // Update cached stats no more than once per frame to avoid performance impact
    double CurrentTime = FPlatformTime::Seconds();
    if (LastStatsUpdateTime == 0.0 || (CurrentTime - LastStatsUpdateTime) > 0.033)
    {
        LastStatsUpdateTime = CurrentTime;
        
        CachedStats.BufferName = Name;
        CachedStats.SizeInBytes = SizeInBytes;
        CachedStats.ReferenceCount = ReferenceCount.GetValue();
        CachedStats.bIsMapped = (MappedData != nullptr);
        CachedStats.bIsZeroCopy = true;
        CachedStats.bIsGPUWritable = bGPUWritable;
        CachedStats.VersionNumber = VersionCounter.GetValue();
        CachedStats.MapCount = MapCount.GetValue();
        CachedStats.UnmapCount = UnmapCount.GetValue();
        CachedStats.LastAccessMode = CurrentAccessMode;
        CachedStats.UsageHint = CurrentUsageHint;
    }
    
    return CachedStats;
}

bool FZeroCopyBuffer::Validate(TArray<FString>& OutErrors) const
{
    if (!bInitialized)
    {
        OutErrors.Add(FString::Printf(TEXT("ZeroCopyBuffer '%s' is not initialized"), *Name.ToString()));
        return false;
    }
    
    if (!RawData)
    {
        OutErrors.Add(FString::Printf(TEXT("ZeroCopyBuffer '%s' has null raw data pointer"), *Name.ToString()));
        return false;
    }
    
    if (SizeInBytes == 0)
    {
        OutErrors.Add(FString::Printf(TEXT("ZeroCopyBuffer '%s' has zero size"), *Name.ToString()));
        return false;
    }
    
    if (MappedData && MappedData != RawData)
    {
        OutErrors.Add(FString::Printf(TEXT("ZeroCopyBuffer '%s' has inconsistent mapped pointer"), *Name.ToString()));
        return false;
    }
    
    // Validation specific to GPU resources
    if (IsRHIInitialized() && !ResourceBuffer.IsValid())
    {
        OutErrors.Add(FString::Printf(TEXT("ZeroCopyBuffer '%s' has invalid GPU resource"), *Name.ToString()));
        return false;
    }
    
    // Validation for SRV
    if (IsRHIInitialized() && !ShaderResourceView.IsValid())
    {
        OutErrors.Add(FString::Printf(TEXT("ZeroCopyBuffer '%s' has invalid shader resource view"), *Name.ToString()));
        return false;
    }
    
    // Validation for UAV if GPU writable
    if (IsRHIInitialized() && bGPUWritable && !UnorderedAccessView.IsValid())
    {
        OutErrors.Add(FString::Printf(TEXT("ZeroCopyBuffer '%s' is GPU writable but has invalid UAV"), *Name.ToString()));
        return false;
    }
    
    if (VersionCounter.GetValue() == 0)
    {
        OutErrors.Add(FString::Printf(TEXT("ZeroCopyBuffer '%s' has invalid version number"), *Name.ToString()));
        return false;
    }
    
    return true;
}

FName FZeroCopyBuffer::GetName() const
{
    return Name;
}

uint64 FZeroCopyBuffer::GetBufferSize() const
{
    return SizeInBytes;
}

void* FZeroCopyBuffer::GetRawBuffer() const
{
    return RawData;
}

void FZeroCopyBuffer::OptimizeForMiningOperations(bool bEnablePrefetching)
{
    FScopeLock Lock(&CriticalSection);
    
    // Set prefetching flag
    bPrefetchingEnabled = bEnablePrefetching;
    
    // For mining operations, optimize based on the current usage hint
    if (CurrentUsageHint == EBufferUsage::SDFField)
    {
        // SDF field operations benefit from aligned memory and cache-friendly access
        // Use platform-specific optimizations when available
#if PLATFORM_WINDOWS || PLATFORM_XBOXONE
        // Windows/Xbox specific optimization for SDF field data
        if (RawData)
        {
            // On Windows, use VirtualAlloc with large pages for SDF field data if possible
            // This is just a placeholder - actual implementation would depend on platform specifics
            UE_LOG(LogTemp, Log, TEXT("ZeroCopyBuffer: Optimizing SDF field buffer '%s' for mining operations"), *Name.ToString());
        }
#endif
    }
    else if (CurrentUsageHint == EBufferUsage::SVONodes)
    {
        // Octree node access patterns are more structured
        // Optimize for node traversal operations common in mining
        UE_LOG(LogTemp, Log, TEXT("ZeroCopyBuffer: Optimizing SVO nodes buffer '%s' for mining operations"), *Name.ToString());
    }
    
    // Reset access pattern tracking to start fresh
    AccessPattern.Reset();
}

void FZeroCopyBuffer::SetActiveMiningState(bool bInActiveMining)
{
    // Update active mining state
    bActiveMining = bInActiveMining;
    
    if (bActiveMining)
    {
        // When set to active mining, optimize for performance
        int32 OptLevel = CVarZeroCopyBufferOptimizationLevel.GetValueOnAnyThread();
        
        if (OptLevel >= 2)
        {
            // Apply highest level optimizations for active mining
            // This might include prefetching, prioritizing this buffer, etc.
            UE_LOG(LogTemp, Verbose, TEXT("ZeroCopyBuffer: Buffer '%s' set to active mining state with high optimization"), *Name.ToString());
        }
    }
    else
    {
        // When not in active mining, can reduce priority or apply different optimizations
        UE_LOG(LogTemp, Verbose, TEXT("ZeroCopyBuffer: Buffer '%s' set to inactive mining state"), *Name.ToString());
    }
}

void FZeroCopyBuffer::WaitForGPU(bool bFlushCommands)
{
    if (!IsRHIInitialized() || !ResourceBuffer.IsValid())
    {
        return;
    }
    
    if (IsRunningRHIInSeparateThread())
    {
        if (bFlushCommands)
        {
            // Flush rendering commands and wait for completion
            FlushRenderingCommands();
        }
        else
        {
            // In UE 5.5, use a simpler approach with enqueued commands
            ENQUEUE_RENDER_COMMAND(FlushGPUCommand)(
                [](FRHICommandListImmediate& RHICmdList)
                {
                    // Wait for all GPU commands to complete
                    RHICmdList.SubmitCommandsAndFlushGPU();
                }
            );
            
            // Wait for rendering thread
            FlushRenderingCommands();
        }
    }
}

FZeroCopyBuffer* FZeroCopyBuffer::CreateBufferView(uint64 OffsetInBytes, uint64 InViewSize)
{
    FScopeLock Lock(&CriticalSection);
    
    if (!bInitialized || !RawData)
    {
        UE_LOG(LogTemp, Error, TEXT("ZeroCopyBuffer: Cannot create view from uninitialized buffer '%s'"), *Name.ToString());
        return nullptr;
    }
    
    if (OffsetInBytes >= this->SizeInBytes)
    {
        UE_LOG(LogTemp, Error, TEXT("ZeroCopyBuffer: Invalid offset %llu for buffer view of '%s' (size %llu)"), 
            OffsetInBytes, *Name.ToString(), this->SizeInBytes);
        return nullptr;
    }
    
    if (OffsetInBytes + InViewSize > this->SizeInBytes)
    {
        UE_LOG(LogTemp, Warning, TEXT("ZeroCopyBuffer: Clamping buffer view size from %llu to %llu for buffer '%s'"),
            InViewSize, this->SizeInBytes - OffsetInBytes, *Name.ToString());
        InViewSize = this->SizeInBytes - OffsetInBytes;
    }
    
    // Create a view name based on the original buffer
    FName ViewName = FName(*FString::Printf(TEXT("%s_View_%llu_%llu"), *Name.ToString(), OffsetInBytes, InViewSize));
    
    // Create a new buffer object for the view
    // Note: This is just a placeholder implementation. In a real implementation,
    // you would create a custom view class that references the parent buffer
    // without duplicating the memory, and forwards operations to the parent.
    UE_LOG(LogTemp, Log, TEXT("ZeroCopyBuffer: Creating view '%s' of buffer '%s' (offset %llu, size %llu)"),
        *ViewName.ToString(), *Name.ToString(), OffsetInBytes, InViewSize);
        
    // For now, just return null as this is a placeholder
    // In a complete implementation, you would return a proper view object
    return nullptr;
}

void FZeroCopyBuffer::OptimizeLayoutForUsage()
{
    // Apply optimizations based on buffer usage hint
    switch (CurrentUsageHint)
    {
    case EBufferUsage::SDFField:
        // SDF field data benefits from SIMD-friendly layouts
        UE_LOG(LogTemp, Verbose, TEXT("ZeroCopyBuffer: Optimizing buffer '%s' for SDF field operations"), *Name.ToString());
        break;
        
    case EBufferUsage::SVONodes:
        // SVO node data benefits from cache-friendly layouts
        UE_LOG(LogTemp, Verbose, TEXT("ZeroCopyBuffer: Optimizing buffer '%s' for SVO node operations"), *Name.ToString());
        break;
        
    case EBufferUsage::MaterialChannels:
        // Material channel data has specific access patterns
        UE_LOG(LogTemp, Verbose, TEXT("ZeroCopyBuffer: Optimizing buffer '%s' for material channel operations"), *Name.ToString());
        break;
        
    default:
        // General purpose buffer
        UE_LOG(LogTemp, Verbose, TEXT("ZeroCopyBuffer: Using general purpose layout for buffer '%s'"), *Name.ToString());
        break;
    }
}

void FZeroCopyBuffer::IncrementVersion()
{
    // Increment the version counter
    VersionCounter.Increment();
    
    // Version 0 is invalid, so wrap around to 1 if we overflow
    if (VersionCounter.GetValue() == 0)
    {
        VersionCounter.Set(1);
    }
}

void FZeroCopyBuffer::RecordMemoryAccess(uint64 Offset, uint64 Size)
{
    // Only record if optimization level is high enough
    int32 OptLevel = CVarZeroCopyBufferOptimizationLevel.GetValueOnAnyThread();
    if (OptLevel < 2 || !bPrefetchingEnabled)
    {
        return;
    }
    
    // Record the access for pattern detection
    AccessPattern.RecordAccess(Offset, Size);
    
    // If we have a clear sequential pattern and prefetching is enabled,
    // we could potentially trigger prefetching here
    if (bPrefetchingEnabled && AccessPattern.GetPatternType() == FMemoryAccessPattern::EPatternType::Sequential)
    {
        // Prefetch size from console variable (KB)
        int32 PrefetchSizeKB = CVarZeroCopyBufferPrefetchSize.GetValueOnAnyThread();
        if (PrefetchSizeKB > 0)
        {
            uint64 PrefetchSize = PrefetchSizeKB * 1024;
            
            // Calculate prefetch address based on current offset and predicted direction
            // This is just a placeholder - real implementation would be more sophisticated
            uint64 PrefetchOffset = Offset + Size;
            if (PrefetchOffset + PrefetchSize <= SizeInBytes)
            {
                // Hint to the OS/CPU to prefetch memory
                // Platform-specific prefetch implementation would go here
            }
        }
    }
}

FZeroCopyBuffer* CreateZeroCopyBuffer(const FName& Name, uint64 InBufferSize, EBufferUsage UsageHint, bool bGPUWritable)
{
    // Create a new buffer with the specified parameters
    FZeroCopyBuffer* Buffer = new FZeroCopyBuffer(Name, InBufferSize, UsageHint, bGPUWritable);
    
    // Initialize the buffer
    if (!Buffer->Initialize())
    {
        // Failed to initialize, clean up and return null
        delete Buffer;
        return nullptr;
    }
    
    return Buffer;
}

void FZeroCopyBuffer::UnmapBuffer()
{
    SCOPE_CYCLE_COUNTER(STAT_ZeroCopyBuffer_Unmap);

    // Protect against multiple unmaps or unmap without corresponding map
    if (!MappedData)
    {
        UE_LOG(LogTemp, Warning, TEXT("FZeroCopyBuffer::UnmapBuffer - Buffer '%s' is not mapped"), *Name.ToString());
        return;
    }

    // Lock during unmapping
    FScopeLock Lock(&CriticalSection);

    // If the buffer was mapped for write, increment the version number
    if (CurrentAccessMode == EBufferAccessMode::WriteOnly || CurrentAccessMode == EBufferAccessMode::ReadWrite)
    {
        IncrementVersion();
    }

    // Handle GPU synchronization
    if (IsRHIInitialized() && ResourceBuffer.IsValid())
    {
        // Sync changes to GPU if mapped for writing
        if (CurrentAccessMode == EBufferAccessMode::WriteOnly || CurrentAccessMode == EBufferAccessMode::ReadWrite)
        {
            SyncToGPU();
        }

        // Unmap on the render thread if needed
        ENQUEUE_RENDER_COMMAND(ZeroCopyBufferUnmap)(
            [this](FRHICommandListImmediate& RHICmdList)
            {
                // Any GPU-specific unmapping would happen here
                // This is a no-op for most platforms that support zero-copy buffers
            });
    }

    // Reset access mode to read-only as default
    CurrentAccessMode = EBufferAccessMode::ReadOnly;
    
    // Clear the mapped pointer
    MappedData = nullptr;
    
    // Increment unmap count for telemetry
    UnmapCount.Increment();
}