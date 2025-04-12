// Copyright Epic Games, Inc. All Rights Reserved.

#include "2_MemoryManagement/Public/ZeroCopyBuffer.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMemory.h"
#include "RHI.h"
#include "RHIResources.h"
#include "Engine/Engine.h"

FZeroCopyBuffer::FZeroCopyBuffer(const FName& InName, uint64 InSizeInBytes, bool bInGPUWritable)
    : Name(InName)
    , SizeInBytes(InSizeInBytes)
    , bGPUWritable(bInGPUWritable)
    , bInitialized(false)
    , RawData(nullptr)
    , MappedData(nullptr)
    , ResourceBuffer(nullptr)
    , LastAccessTime(0)
    , AccessCount(0)
{
    Stats.BufferSize = SizeInBytes;
    Stats.Name = Name;
    Stats.Type = TEXT("ZeroCopy");
    Stats.AccessCount = 0;
    Stats.LastAccessTime = 0;
}

FZeroCopyBuffer::~FZeroCopyBuffer()
{
    Shutdown();
}

bool FZeroCopyBuffer::Initialize()
{
    if (bInitialized)
    {
        UE_LOG(LogTemp, Warning, TEXT("ZeroCopyBuffer: Buffer '%s' already initialized"), *Name.ToString());
        return true;
    }

    if (SizeInBytes == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("ZeroCopyBuffer: Cannot initialize buffer '%s' with zero size"), *Name.ToString());
        return false;
    }

    // Ensure size is aligned to system page size
    const uint64 PageSize = FPlatformMemory::GetConstants().PageSize;
    SizeInBytes = ((SizeInBytes + PageSize - 1) / PageSize) * PageSize;

    // Allocate the CPU accessible memory
    RawData = FPlatformMemory::BinnedAllocFromOS(SizeInBytes);
    if (!RawData)
    {
        UE_LOG(LogTemp, Error, TEXT("ZeroCopyBuffer: Failed to allocate memory for buffer '%s' (%llu bytes)"), 
            *Name.ToString(), SizeInBytes);
        return false;
    }

    // Initialize memory to zero
    FMemory::Memzero(RawData, SizeInBytes);

    // Create GPU resource if we're running with a valid RHI
    if (IsRunningRHIInSeparateThread())
    {
        ENQUEUE_RENDER_COMMAND(CreateZeroCopyBuffer)(
            [this](FRHICommandListImmediate& RHICmdList)
            {
                CreateGPUBuffer_RenderThread();
            }
        );
        // Wait for render thread to complete
        FlushRenderingCommands();
    }
    else
    {
        CreateGPUBuffer_RenderThread();
    }

    bInitialized = true;
    UE_LOG(LogTemp, Log, TEXT("ZeroCopyBuffer: Initialized buffer '%s' (%llu bytes)"), *Name.ToString(), SizeInBytes);

    return true;
}

void FZeroCopyBuffer::CreateGPUBuffer_RenderThread()
{
    if (!GRHISupportsBufferLocks)
    {
        UE_LOG(LogTemp, Warning, TEXT("ZeroCopyBuffer: RHI doesn't support buffer locks, zero-copy may not be optimal"));
    }

    // Create GPU resource
    FRHIResourceCreateInfo CreateInfo(TEXT("ZeroCopyBuffer"));
    
    EBufferUsageFlags BufferUsage = BUF_Dynamic | BUF_ShaderResource;
    if (bGPUWritable)
    {
        BufferUsage |= BUF_UnorderedAccess;
    }
    
    ResourceBuffer = RHICreateStructuredBuffer(
        4, // Stride - minimum size
        SizeInBytes,
        BufferUsage,
        CreateInfo);

    if (!ResourceBuffer)
    {
        UE_LOG(LogTemp, Error, TEXT("ZeroCopyBuffer: Failed to create GPU resource for buffer '%s'"), *Name.ToString());
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("ZeroCopyBuffer: Created GPU resource for buffer '%s'"), *Name.ToString());
    }
}

void FZeroCopyBuffer::Shutdown()
{
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
                    ResourceBuffer.SafeRelease();
                }
            );
            // Wait for render thread to complete
            FlushRenderingCommands();
        }
        else
        {
            ResourceBuffer.SafeRelease();
        }
    }

    // Free CPU resource
    if (RawData)
    {
        FPlatformMemory::BinnedFreeToOS(RawData, SizeInBytes);
        RawData = nullptr;
    }

    bInitialized = false;
    UE_LOG(LogTemp, Log, TEXT("ZeroCopyBuffer: Shutdown buffer '%s'"), *Name.ToString());
}

bool FZeroCopyBuffer::IsInitialized() const
{
    return bInitialized;
}

void* FZeroCopyBuffer::GetRawBuffer() const
{
    return RawData;
}

uint64 FZeroCopyBuffer::GetBufferSize() const
{
    return SizeInBytes;
}

void* FZeroCopyBuffer::MapBuffer(EBufferAccessFlags AccessFlags)
{
    if (!bInitialized || !RawData)
    {
        UE_LOG(LogTemp, Error, TEXT("ZeroCopyBuffer: Cannot map uninitialized buffer '%s'"), *Name.ToString());
        return nullptr;
    }

    if (MappedData)
    {
        UE_LOG(LogTemp, Warning, TEXT("ZeroCopyBuffer: Buffer '%s' already mapped, returning existing mapping"), *Name.ToString());
        return MappedData;
    }

    // For CPU-only access, just use the raw pointer
    if (AccessFlags == EBufferAccessFlags::CPU_Read || AccessFlags == EBufferAccessFlags::CPU_Write)
    {
        MappedData = RawData;
        CurrentAccessFlags = AccessFlags;

        // Update stats
        Stats.AccessCount++;
        Stats.LastAccessTime = FPlatformTime::Seconds();
        Stats.CurrentMappingType = AccessFlags == EBufferAccessFlags::CPU_Read ? TEXT("CPU Read") : TEXT("CPU Write");

        return MappedData;
    }

    // For GPU-shared access, we need to properly synchronize with the GPU
    if (ResourceBuffer)
    {
        bool bNeedGPUSync = (AccessFlags == EBufferAccessFlags::CPU_GPU_Read || 
                          AccessFlags == EBufferAccessFlags::CPU_GPU_Write);
        
        if (bNeedGPUSync)
        {
            // Synchronize with GPU - in a real implementation, this would properly fence and wait
            // But for simplicity, we'll just flush rendering commands
            FlushRenderingCommands();
            
            if (AccessFlags == EBufferAccessFlags::CPU_GPU_Read)
            {
                // If reading from GPU, might need to copy from GPU to CPU
                // This is a simplified approach - real implementation would handle this on the render thread
            }
        }
    }

    MappedData = RawData;
    CurrentAccessFlags = AccessFlags;

    // Update stats
    Stats.AccessCount++;
    Stats.LastAccessTime = FPlatformTime::Seconds();
    if (AccessFlags == EBufferAccessFlags::CPU_GPU_Read)
        Stats.CurrentMappingType = TEXT("CPU-GPU Read");
    else if (AccessFlags == EBufferAccessFlags::CPU_GPU_Write)
        Stats.CurrentMappingType = TEXT("CPU-GPU Write");

    return MappedData;
}

bool FZeroCopyBuffer::UnmapBuffer()
{
    if (!bInitialized || !MappedData)
    {
        return false;
    }

    // If this was GPU-shared access, we need to properly synchronize with the GPU
    if (ResourceBuffer && 
        (CurrentAccessFlags == EBufferAccessFlags::CPU_GPU_Read || 
         CurrentAccessFlags == EBufferAccessFlags::CPU_GPU_Write))
    {
        bool bNeedToUpdateGPU = (CurrentAccessFlags == EBufferAccessFlags::CPU_GPU_Write);
        
        if (bNeedToUpdateGPU)
        {
            // Update GPU buffer from CPU memory
            // This is a simplified approach - real implementation would handle this on the render thread
            if (IsRunningRHIInSeparateThread())
            {
                ENQUEUE_RENDER_COMMAND(UpdateZeroCopyBuffer)(
                    [this](FRHICommandListImmediate& RHICmdList)
                    {
                        // In an actual implementation, this would copy from CPU memory to GPU
                        // or make sure they are properly synchronized
                    }
                );
            }
            else
            {
                // Direct update if not on separate thread
            }
        }
        
        // Flush commands to ensure synchronization
        FlushRenderingCommands();
    }

    // Clear the mapping
    MappedData = nullptr;
    CurrentAccessFlags = EBufferAccessFlags::None;
    
    // Update stats
    Stats.CurrentMappingType = TEXT("None");
    
    return true;
}

bool FZeroCopyBuffer::ResizeBuffer(uint64 NewSizeInBytes)
{
    if (!bInitialized)
    {
        UE_LOG(LogTemp, Error, TEXT("ZeroCopyBuffer: Cannot resize uninitialized buffer '%s'"), *Name.ToString());
        return false;
    }

    if (NewSizeInBytes == SizeInBytes)
    {
        // No change needed
        return true;
    }

    // Ensure buffer is unmapped
    if (MappedData)
    {
        UnmapBuffer();
    }

    // Ensure size is aligned to system page size
    const uint64 PageSize = FPlatformMemory::GetConstants().PageSize;
    NewSizeInBytes = ((NewSizeInBytes + PageSize - 1) / PageSize) * PageSize;

    // Allocate new memory
    void* NewData = FPlatformMemory::BinnedAllocFromOS(NewSizeInBytes);
    if (!NewData)
    {
        UE_LOG(LogTemp, Error, TEXT("ZeroCopyBuffer: Failed to allocate memory for resize of buffer '%s' (%llu bytes)"), 
            *Name.ToString(), NewSizeInBytes);
        return false;
    }

    // Initialize memory to zero (important for new segments)
    FMemory::Memzero(NewData, NewSizeInBytes);

    // Copy old data to new buffer
    uint64 CopySize = FMath::Min(SizeInBytes, NewSizeInBytes);
    FMemory::Memcpy(NewData, RawData, CopySize);

    // Free old data
    FPlatformMemory::BinnedFreeToOS(RawData, SizeInBytes);
    RawData = NewData;

    // Update size
    uint64 OldSize = SizeInBytes;
    SizeInBytes = NewSizeInBytes;
    Stats.BufferSize = SizeInBytes;

    // If we have a GPU resource, need to recreate it
    if (ResourceBuffer)
    {
        if (IsRunningRHIInSeparateThread())
        {
            ENQUEUE_RENDER_COMMAND(RecreateZeroCopyBuffer)(
                [this](FRHICommandListImmediate& RHICmdList)
                {
                    ResourceBuffer.SafeRelease();
                    CreateGPUBuffer_RenderThread();
                }
            );
            // Wait for render thread to complete
            FlushRenderingCommands();
        }
        else
        {
            ResourceBuffer.SafeRelease();
            CreateGPUBuffer_RenderThread();
        }
    }

    UE_LOG(LogTemp, Log, TEXT("ZeroCopyBuffer: Resized buffer '%s' from %llu to %llu bytes"), 
        *Name.ToString(), OldSize, SizeInBytes);

    return true;
}

void* FZeroCopyBuffer::GetGPUBuffer() const
{
    return ResourceBuffer.GetReference();
}

FBufferStats FZeroCopyBuffer::GetStats() const
{
    FBufferStats CurrentStats = Stats;
    
    // Update any dynamic stats
    CurrentStats.CurrentMappingType = MappedData ? Stats.CurrentMappingType : TEXT("None");
    CurrentStats.IsMapped = (MappedData != nullptr);
    
    return CurrentStats;
}