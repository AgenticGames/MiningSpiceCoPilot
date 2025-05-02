#include "../Public/ZeroCopyResourceManager.h"
#include "../Public/GPUDispatcherLogging.h"

#include "RenderCore.h"
#include "RHIResources.h"
#include "RHIDefinitions.h"
#include "RHI.h"
#include "RHIUtilities.h"
#include "RHIStaticStates.h"
#include "PipelineStateCache.h"
#include "RenderGraphResources.h" // For FBufferUnorderedAccessViewDesc
#include "Shader.h" // For SupportsComputeShaders

FZeroCopyResourceManager::FZeroCopyResourceManager()
    : NextBufferIndex(0)
    , TotalAllocatedBytes(0)
{
}

FZeroCopyResourceManager::~FZeroCopyResourceManager()
{
    // Clean up resources
    for (auto& BufferPair : PinnedBuffers)
    {
        FPinnedBuffer& Buffer = BufferPair.Value;
        
        // Free GPU buffer
        if (Buffer.GPUBuffer)
        {
            delete Buffer.GPUBuffer;
            Buffer.GPUBuffer = nullptr;
        }
    }
    
    PinnedBuffers.Empty();
}

bool FZeroCopyResourceManager::Initialize()
{
    // Check if RHI supports compute shaders (needed for shared resources)
    bool bSupportsSharedMemory = IsFeatureLevelSupported(GMaxRHIShaderPlatform, ERHIFeatureLevel::SM5);
    
    if (!bSupportsSharedMemory)
    {
        GPU_DISPATCHER_LOG_WARNING("RHI does not support compute shaders, zero-copy buffers will be emulated");
    }
    
    // Initialize the RHI if not done already
    if (!GDynamicRHI)
    {
        GPU_DISPATCHER_LOG_WARNING("DynamicRHI not initialized, some operations may fail");
    }
    
    GPU_DISPATCHER_LOG_DEBUG("ZeroCopyResourceManager initialized");
    return true;
}

void* FZeroCopyResourceManager::PinMemory(void* CPUAddress, SIZE_T Size, uint32& OutBufferIndex)
{
    // Create a mutable reference to ResourceLock before using it with FScopeLock
    FCriticalSection& MutableLock = const_cast<FCriticalSection&>(ResourceLock);
    FScopeLock Lock(&MutableLock);
    
    // Generate a unique buffer index
    OutBufferIndex = NextBufferIndex++;
    
    // Create a pin for this memory range
    FPinnedBuffer& Buffer = PinnedBuffers.Add(OutBufferIndex);
    Buffer.CPUAddress = CPUAddress;
    Buffer.Size = Size;
    Buffer.LastUsedTime = FPlatformTime::Seconds();
    Buffer.UsageCount = 1;
    
    // Create a GPU buffer for this memory
    Buffer.GPUBuffer = new FRHIGPUBufferReadback(TEXT("ZeroCopyBuffer"));
    
    // Track allocated memory
    TotalAllocatedBytes += Size;
    
    GPU_DISPATCHER_LOG_VERBOSE("Pinned memory at %p, size %llu, buffer index %u",
        CPUAddress, Size, OutBufferIndex);
    
    // Return the CPU address (may be different if memory had to be copied)
    return CPUAddress;
}

FRHIGPUBufferReadback* FZeroCopyResourceManager::GetGPUBuffer(uint32 BufferIndex)
{
    // Create a mutable reference to ResourceLock before using it with FScopeLock
    FCriticalSection& MutableLock = const_cast<FCriticalSection&>(ResourceLock);
    FScopeLock Lock(&MutableLock);
    
    // Find the buffer
    FPinnedBuffer* Buffer = PinnedBuffers.Find(BufferIndex);
    if (!Buffer)
    {
        GPU_DISPATCHER_LOG_WARNING("Buffer index %u not found", BufferIndex);
        return nullptr;
    }
    
    // Update usage stats
    Buffer->LastUsedTime = FPlatformTime::Seconds();
    Buffer->UsageCount++;
    
    return Buffer->GPUBuffer;
}

void FZeroCopyResourceManager::ReleaseMemory(uint32 BufferIndex)
{
    // Create a mutable reference to ResourceLock before using it with FScopeLock
    FCriticalSection& MutableLock = const_cast<FCriticalSection&>(ResourceLock);
    FScopeLock Lock(&MutableLock);
    
    // Find the buffer
    FPinnedBuffer* Buffer = PinnedBuffers.Find(BufferIndex);
    if (!Buffer)
    {
        GPU_DISPATCHER_LOG_WARNING("Buffer index %u not found for release", BufferIndex);
        return;
    }
    
    // Track allocated memory
    TotalAllocatedBytes -= Buffer->Size;
    
    // Clean up GPU buffer
    if (Buffer->GPUBuffer)
    {
        delete Buffer->GPUBuffer;
        Buffer->GPUBuffer = nullptr;
    }
    
    // Remove from map
    PinnedBuffers.Remove(BufferIndex);
    
    GPU_DISPATCHER_LOG_VERBOSE("Released memory buffer %u", BufferIndex);
    
    // Clean up old resources
    CleanupUnusedResources();
}

void FZeroCopyResourceManager::TransitionResource(FRHIResource* Resource, ERHIAccess NewAccess, ERHIPipeline Pipeline)
{
    // Create a mutable reference to ResourceLock before using it with FScopeLock
    FCriticalSection& MutableLock = const_cast<FCriticalSection&>(ResourceLock);
    FScopeLock Lock(&MutableLock);
    
    // Check if resource exists
    if (!Resource)
    {
        return;
    }
    
    // Get current access and pipeline
    ERHIAccess CurrentAccess = ResourceAccessMap.FindRef(Resource);
    ERHIPipeline CurrentPipeline = ResourcePipelineMap.FindRef(Resource);
    
    // Skip if no transition needed
    if (CurrentAccess == NewAccess && CurrentPipeline == Pipeline)
    {
        return;
    }
    
    // Update resource state
    ResourceAccessMap.Add(Resource, NewAccess);
    ResourcePipelineMap.Add(Resource, Pipeline);
    
    // In a real implementation, we would enqueue a resource transition
    // For simplicity, we'll just log the transition
    GPU_DISPATCHER_LOG_VERBOSE("Transitioned resource %p: Access %d -> %d, Pipeline %d -> %d",
        Resource, (int32)CurrentAccess, (int32)NewAccess, (int32)CurrentPipeline, (int32)Pipeline);
}

uint64 FZeroCopyResourceManager::GetTotalAllocatedMemory() const
{
    return TotalAllocatedBytes;
}

FRHIBuffer* FZeroCopyResourceManager::CreateBuffer(SIZE_T Size, EBufferUsageFlags Usage, FRHIResourceCreateInfo& CreateInfo)
{
    // This is just a wrapper around RHI buffer creation
    // In a real implementation, it would handle shared memory setup
    
    // Cast to uint32 to avoid operator overload ambiguity
    uint32 AdditionalUsageFlags = static_cast<uint32>(BUF_Shared | BUF_ShaderResource | BUF_StructuredBuffer);
    uint32 UsageFlags = static_cast<uint32>(Usage) | AdditionalUsageFlags;
    
    // Updated buffer creation for UE5.5
    FRHIResourceCreateInfo ResourceCreateInfo = CreateInfo;
    
    // Set buffer usage flags (combine with provided flags)
    uint32 FinalUsageFlags = static_cast<uint32>(Usage) | AdditionalUsageFlags;

    // Create buffer using UE5.5 compatible API
    FRHIBufferCreateInfo BufferCreateInfo;
    BufferCreateInfo.Size = Size;
    BufferCreateInfo.Usage = static_cast<ERHIBufferUsageFlags>(FinalUsageFlags);
    BufferCreateInfo.AccessFlags = ERHIAccess::SRVMask;
    BufferCreateInfo.ClearValue = FRHIClearValue();
    
    FBufferRHIRef BufferRef = RHICreateBuffer(BufferCreateInfo);
    
    // Track allocated memory
    TotalAllocatedBytes += Size;
    
    // Return raw pointer (ownership transferred to caller)
    return BufferRef.GetReference();
}

FRHIUnorderedAccessView* FZeroCopyResourceManager::CreateUAV(FRHIBuffer* Buffer, EPixelFormat Format)
{
    if (!Buffer)
    {
        return nullptr;
    }
    
    // Create UAV with updated API for UE5.5
    // Create UAV descriptor specifying format and buffer
    FRHIUnorderedAccessViewDesc UAVDesc;
    UAVDesc.Buffer.Buffer = Buffer;
    UAVDesc.Buffer.Format = Format;
    
    FUnorderedAccessViewRHIRef UAVRef = RHICreateUnorderedAccessView(UAVDesc);
    
    // Return raw pointer (ownership transferred to caller)
    return UAVRef.GetReference();
}

FRHIShaderResourceView* FZeroCopyResourceManager::CreateSRV(FRHIBuffer* Buffer, EPixelFormat Format)
{
    if (!Buffer)
    {
        return nullptr;
    }
    
    // Create SRV with updated API for UE5.5
    // Create SRV descriptor specifying format and buffer
    FRHIShaderResourceViewDesc SRVDesc;
    SRVDesc.Buffer.Buffer = Buffer;
    SRVDesc.Buffer.Format = Format;
    
    FShaderResourceViewRHIRef SRVRef = RHICreateShaderResourceView(SRVDesc);
    
    // Return raw pointer (ownership transferred to caller)
    return SRVRef.GetReference();
}

FString FZeroCopyResourceManager::GetBufferName(uint32 Index) const
{
    return FString::Printf(TEXT("ZeroCopyBuffer_%u"), Index);
}

void FZeroCopyResourceManager::CleanupUnusedResources()
{
    // Clean up resources that haven't been used in a while
    double CurrentTime = FPlatformTime::Seconds();
    constexpr double UnusedThreshold = 60.0; // 60 seconds
    
    TArray<uint32> BuffersToRemove;
    
    for (auto& BufferPair : PinnedBuffers)
    {
        uint32 BufferIndex = BufferPair.Key;
        FPinnedBuffer& Buffer = BufferPair.Value;
        
        // Check if unused for a while
        if (CurrentTime - Buffer.LastUsedTime > UnusedThreshold)
        {
            // Clean up GPU buffer
            if (Buffer.GPUBuffer)
            {
                delete Buffer.GPUBuffer;
                Buffer.GPUBuffer = nullptr;
            }
            
            // Track allocated memory
            TotalAllocatedBytes -= Buffer.Size;
            
            // Add to removal list
            BuffersToRemove.Add(BufferIndex);
        }
    }
    
    // Remove unused buffers
    for (uint32 BufferIndex : BuffersToRemove)
    {
        PinnedBuffers.Remove(BufferIndex);
    }
    
    if (BuffersToRemove.Num() > 0)
    {
        GPU_DISPATCHER_LOG_VERBOSE("Cleaned up %d unused buffers", BuffersToRemove.Num());
    }
}