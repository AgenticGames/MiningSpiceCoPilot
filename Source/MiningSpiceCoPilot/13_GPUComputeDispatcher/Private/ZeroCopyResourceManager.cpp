#include "../Public/ZeroCopyResourceManager.h"
#include "../Public/GPUDispatcherLogging.h"
#include "SimulatedGPUBuffer.h"

// Simplified implementation - no RHI dependencies
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
    // Simplified initialization with no RHI checks
    GPU_DISPATCHER_LOG_DEBUG("ZeroCopyResourceManager initialized with simplified implementation");
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
    
    // Create a simulated GPU buffer
    Buffer.GPUBuffer = new FSimulatedGPUReadback(*GetBufferName(OutBufferIndex));
    
    // Track allocated memory
    TotalAllocatedBytes += Size;
    
    GPU_DISPATCHER_LOG_VERBOSE("Pinned memory at %p, size %llu, buffer index %u",
        CPUAddress, Size, OutBufferIndex);
    
    // Return the CPU address (may be different if memory had to be copied)
    return CPUAddress;
}

FSimulatedGPUReadback* FZeroCopyResourceManager::GetGPUBuffer(uint32 BufferIndex)
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

void FZeroCopyResourceManager::TransitionResource(FSimplifiedResource* Resource, ESimplifiedAccess NewAccess, ESimplifiedPipeline Pipeline)
{
    // Simplified implementation with no actual RHI transitions
    // Just log the intended transition
    GPU_DISPATCHER_LOG_VERBOSE("Resource transition requested (simplified implementation)");
}

uint64 FZeroCopyResourceManager::GetTotalAllocatedMemory() const
{
    return TotalAllocatedBytes;
}

FSimulatedGPUBuffer* FZeroCopyResourceManager::CreateBuffer(SIZE_T Size, uint32 UsageFlags)
{
    // Simplified implementation using our own buffer class
    GPU_DISPATCHER_LOG_DEBUG("CreateBuffer called with size %llu (simplified implementation)", Size);
    
    // Create a new simulated GPU buffer
    FSimulatedGPUBuffer* Buffer = new FSimulatedGPUBuffer(
        Size, 
        UsageFlags, 
        FString::Printf(TEXT("Buffer_%llu"), FMath::Rand()));
    
    // Track allocated memory
    TotalAllocatedBytes += Size;
    
    return Buffer;
}

void* FZeroCopyResourceManager::CreateUAV(FSimulatedGPUBuffer* Buffer, uint32 Format)
{
    // Simplified implementation - logging only
    if (Buffer)
    {
        GPU_DISPATCHER_LOG_DEBUG("CreateUAV called for buffer %s (simplified implementation)", 
            *Buffer->GetName());
    }
    else
    {
        GPU_DISPATCHER_LOG_DEBUG("CreateUAV called with null buffer (simplified implementation)");
    }
    
    // Return buffer's CPU address as a proxy for a UAV
    return Buffer ? Buffer->GetCPUAddress() : nullptr;
}

void* FZeroCopyResourceManager::CreateSRV(FSimulatedGPUBuffer* Buffer, uint32 Format)
{
    // Simplified implementation - logging only
    if (Buffer)
    {
        GPU_DISPATCHER_LOG_DEBUG("CreateSRV called for buffer %s (simplified implementation)", 
            *Buffer->GetName());
    }
    else
    {
        GPU_DISPATCHER_LOG_DEBUG("CreateSRV called with null buffer (simplified implementation)");
    }
    
    // Return buffer's CPU address as a proxy for an SRV
    return Buffer ? Buffer->GetCPUAddress() : nullptr;
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