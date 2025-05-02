#pragma once

#include "CoreMinimal.h"
#include "ComputeOperationTypes.h"

// Forward declarations for simplified implementation
class FSimulatedGPUBuffer;
class FSimulatedGPUReadback;

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
    FSimulatedGPUReadback* GetGPUBuffer(uint32 BufferIndex);
    
    // Release pinned memory
    void ReleaseMemory(uint32 BufferIndex);
    
    // Resource state transitions
    void TransitionResource(FSimplifiedResource* Resource, ESimplifiedAccess NewAccess, ESimplifiedPipeline Pipeline);
    
    // Memory usage tracking
    uint64 GetTotalAllocatedMemory() const;
    
    // Buffer creation and management
    FSimulatedGPUBuffer* CreateBuffer(SIZE_T Size, uint32 UsageFlags);
    void* CreateUAV(FSimulatedGPUBuffer* Buffer, uint32 Format = 0);
    void* CreateSRV(FSimulatedGPUBuffer* Buffer, uint32 Format = 0);
    
private:
    // Structure to track pinned memory
    struct FPinnedBuffer
    {
        void* CPUAddress;
        SIZE_T Size;
        FSimulatedGPUReadback* GPUBuffer;
        double LastUsedTime;
        uint32 UsageCount;
    };
    
    // Member variables
    TMap<uint32, FPinnedBuffer> PinnedBuffers;
    uint32 NextBufferIndex;
    FCriticalSection ResourceLock;
    uint64 TotalAllocatedBytes;
    
    // Resource state tracking
    TMap<FSimplifiedResource*, ESimplifiedAccess> ResourceAccessMap;
    TMap<FSimplifiedResource*, ESimplifiedPipeline> ResourcePipelineMap;
    
    // Helper methods
    FString GetBufferName(uint32 Index) const;
    void CleanupUnusedResources();
};