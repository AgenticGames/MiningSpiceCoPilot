#pragma once

#include "CoreMinimal.h"
#include "RHICommandList.h"
#include "RHIResources.h"

/**
 * Manages zero-copy resources for efficient CPU/GPU data sharing
 * Provides memory pinning and resource transitions for SDF fields
 */
class MININGSPICECOPILOT_API FZeroCopyResourceManager
{
public:
    FZeroCopyResourceManager();
    ~FZeroCopyResourceManager();
    
    // Initialization
    bool Initialize();
    
    // Memory pinning for zero-copy access
    void* PinMemory(void* CPUAddress, SIZE_T Size, uint32& OutBufferIndex);
    
    // Get GPU buffer for pinned memory
    FRHIGPUBufferReadback* GetGPUBuffer(uint32 BufferIndex);
    
    // Release pinned memory
    void ReleaseMemory(uint32 BufferIndex);
    
    // Resource state transitions
    void TransitionResource(FRHIResource* Resource, ERHIAccess NewAccess, ERHIPipeline Pipeline);
    
    // Memory usage tracking
    uint64 GetTotalAllocatedMemory() const;
    
    // Buffer creation and management
    FRHIBuffer* CreateBuffer(SIZE_T Size, EBufferUsageFlags Usage, FRHIResourceCreateInfo& CreateInfo);
    FRHIUnorderedAccessView* CreateUAV(FRHIBuffer* Buffer, EPixelFormat Format = PF_Unknown);
    FRHIShaderResourceView* CreateSRV(FRHIBuffer* Buffer, EPixelFormat Format = PF_Unknown);
    
private:
    // Structure to track pinned memory
    struct FPinnedBuffer
    {
        void* CPUAddress;
        SIZE_T Size;
        FRHIGPUBufferReadback* GPUBuffer;
        double LastUsedTime;
        uint32 UsageCount;
    };
    
    // Member variables
    TMap<uint32, FPinnedBuffer> PinnedBuffers;
    uint32 NextBufferIndex;
    FCriticalSection ResourceLock;
    uint64 TotalAllocatedBytes;
    
    // Resource state tracking
    TMap<FRHIResource*, ERHIAccess> ResourceAccessMap;
    TMap<FRHIResource*, ERHIPipeline> ResourcePipelineMap;
    
    // Helper methods
    FString GetBufferName(uint32 Index) const;
    void CleanupUnusedResources();
};