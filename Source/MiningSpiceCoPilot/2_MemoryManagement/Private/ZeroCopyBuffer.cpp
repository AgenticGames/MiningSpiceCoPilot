// Copyright Epic Games, Inc. All Rights Reserved.

#include "2_MemoryManagement/Public/ZeroCopyBuffer.h"
#include "RHI.h"
#include "RenderingThread.h"
#include "HAL/PlatformMemory.h"
#include "Misc/ScopeLock.h"
#include "Containers/ResourceArray.h"

FZeroCopyBuffer::FZeroCopyBuffer(const FName& InName, uint64 InSizeInBytes, bool InGPUWritable)
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
{
}

FZeroCopyBuffer::~FZeroCopyBuffer()
{
    if (bInitialized)
    {
        Shutdown();
    }
}

bool FZeroCopyBuffer::Initialize()
{
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

    // Allocate CPU memory aligned to platform cache line size for optimal performance
    const uint32 Alignment = FPlatformMemory::GetConstants().CacheLineSize;
    RawData = FPlatformMemory::BinnedAllocFromOS(SizeInBytes, Alignment);
    
    if (!RawData)
    {
        UE_LOG(LogTemp, Error, TEXT("ZeroCopyBuffer: Failed to allocate %llu bytes for buffer '%s'"), 
            SizeInBytes, *Name.ToString());
        return false;
    }

    // Zero out the memory
    FMemory::Memzero(RawData, SizeInBytes);

    // Create GPU resource if RHI is available
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
    else if (IsRHIInitialized())
    {
        // RHI is available but not on a separate thread
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
        
        // Create Shader Resource View
        ShaderResourceView = RHICreateShaderResourceView(ResourceBuffer);
        
        // Create Unordered Access View if writable
        if (bGPUWritable)
        {
            UnorderedAccessView = RHICreateUnorderedAccessView(ResourceBuffer);
        }
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

void* FZeroCopyBuffer::MapBuffer(EBufferAccessMode AccessMode)
{
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

    MappedData = RawData;
    return MappedData;
}

void FZeroCopyBuffer::UnmapBuffer()
{
    if (!bInitialized || !MappedData)
    {
        return;
    }

    // If we have a GPU resource and we potentially modified the buffer, sync to GPU
    if (ResourceBuffer && 
        (CurrentAccessMode == EBufferAccessMode::WriteOnly || CurrentAccessMode == EBufferAccessMode::ReadWrite))
    {
        SyncToGPU();
    }

    MappedData = nullptr;
}

bool FZeroCopyBuffer::IsBufferMapped() const
{
    return MappedData != nullptr;
}

void FZeroCopyBuffer::SyncToGPU()
{
    if (!bInitialized || !ResourceBuffer || !RawData)
    {
        return;
    }

    // Update GPU resource with CPU memory content
    if (IsRunningRHIInSeparateThread())
    {
        void* SourceData = RawData;
        uint64 BufferSize = SizeInBytes;
        FStructuredBufferRHIRef Buffer = ResourceBuffer;

        ENQUEUE_RENDER_COMMAND(UpdateZeroCopyBuffer)(
            [SourceData, BufferSize, Buffer](FRHICommandListImmediate& RHICmdList)
            {
                void* GPUData = RHILockStructuredBuffer(
                    Buffer, 
                    0, 
                    BufferSize, 
                    RLM_WriteOnly);
                
                if (GPUData)
                {
                    FMemory::Memcpy(GPUData, SourceData, BufferSize);
                    RHIUnlockStructuredBuffer(Buffer);
                }
            }
        );
    }
    else if (IsRHIInitialized())
    {
        void* GPUData = RHILockStructuredBuffer(
            ResourceBuffer, 
            0, 
            SizeInBytes, 
            RLM_WriteOnly);
        
        if (GPUData)
        {
            FMemory::Memcpy(GPUData, RawData, SizeInBytes);
            RHIUnlockStructuredBuffer(ResourceBuffer);
        }
    }
}

void FZeroCopyBuffer::SyncFromGPU()
{
    if (!bInitialized || !ResourceBuffer || !RawData)
    {
        return;
    }

    // Update CPU memory with GPU resource content
    if (IsRunningRHIInSeparateThread())
    {
        void* DestData = RawData;
        uint64 BufferSize = SizeInBytes;
        FStructuredBufferRHIRef Buffer = ResourceBuffer;

        ENQUEUE_RENDER_COMMAND(ReadZeroCopyBuffer)(
            [DestData, BufferSize, Buffer](FRHICommandListImmediate& RHICmdList)
            {
                void* GPUData = RHILockStructuredBuffer(
                    Buffer, 
                    0, 
                    BufferSize, 
                    RLM_ReadOnly);
                
                if (GPUData)
                {
                    FMemory::Memcpy(DestData, GPUData, BufferSize);
                    RHIUnlockStructuredBuffer(Buffer);
                }
            }
        );
        // Wait for completion
        FlushRenderingCommands();
    }
    else if (IsRHIInitialized())
    {
        void* GPUData = RHILockStructuredBuffer(
            ResourceBuffer, 
            0, 
            SizeInBytes, 
            RLM_ReadOnly);
        
        if (GPUData)
        {
            FMemory::Memcpy(RawData, GPUData, SizeInBytes);
            RHIUnlockStructuredBuffer(ResourceBuffer);
        }
    }
}

bool FZeroCopyBuffer::IsGPUBufferValid() const
{
    return ResourceBuffer.IsValid();
}

FStructuredBufferRHIRef FZeroCopyBuffer::GetRHIBuffer() const
{
    return ResourceBuffer;
}

FShaderResourceViewRHIRef FZeroCopyBuffer::GetShaderResourceView() const
{
    return ShaderResourceView;
}

FUnorderedAccessViewRHIRef FZeroCopyBuffer::GetUnorderedAccessView() const
{
    if (!bGPUWritable)
    {
        UE_LOG(LogTemp, Warning, TEXT("ZeroCopyBuffer: Buffer '%s' is not GPU writable, UAV not available"), *Name.ToString());
    }
    
    return UnorderedAccessView;
}