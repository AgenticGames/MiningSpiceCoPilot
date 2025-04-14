﻿// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Interfaces/IBufferProvider.h"
#include "RHIResources.h"
#include "RHI.h"
#include "RHIDefinitions.h"
#include "ZeroCopyBuffer.generated.h"

/**
 * Interface for a zero-copy buffer in the memory management system
 * Provides efficient memory access without unnecessary copies
 */
UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UZeroCopyBuffer : public UInterface
{
    GENERATED_BODY()
};

/**
 * Zero-copy buffer interface
 */
class MININGSPICECOPILOT_API IZeroCopyBuffer
{
    GENERATED_BODY()
public:
    /**
     * Gets a direct read-only pointer to the buffer data
     * @return Const pointer to the buffer memory
     */
    virtual const void* GetReadOnlyData() const = 0;
    
    /**
     * Gets a direct writable pointer to the buffer data
     * @return Pointer to the buffer memory
     */
    virtual void* GetWritableData() = 0;
    
    /**
     * Gets the size of the buffer in bytes
     * @return Size in bytes
     */
    virtual uint64 GetSizeInBytes() const = 0;
    
    /**
     * Gets the alignment of the buffer in bytes
     * @return Alignment in bytes
     */
    virtual uint32 GetAlignment() const = 0;
    
    /**
     * Resizes the buffer to the new size
     * @param NewSizeInBytes - New buffer size
     * @param bPreserveContent - Whether to preserve existing content
     * @return True if resize was successful
     */
    virtual bool Resize(uint64 NewSizeInBytes, bool bPreserveContent = true) = 0;
    
    /**
     * Maps the buffer for GPU access
     * @return True if mapping was successful
     */
    virtual bool MapForGPU() = 0;
    
    /**
     * Unmaps the buffer from GPU access
     */
    virtual void UnmapFromGPU() = 0;
    
    /**
     * Checks if the buffer is currently mapped for GPU access
     * @return True if mapped
     */
    virtual bool IsMappedForGPU() const = 0;
};

/**
 * Concrete implementation of a zero-copy buffer
 * Provides efficient memory sharing between CPU and GPU with minimal copying
 */
class MININGSPICECOPILOT_API FZeroCopyBuffer : public IBufferProvider
{
public:
    /**
     * Constructor
     * @param InName Buffer name for tracking and debugging
     * @param InSizeInBytes Size of the buffer in bytes
     * @param InGPUWritable Whether the GPU can write to this buffer
     */
    FZeroCopyBuffer(const FName& InName, uint64 InSizeInBytes, bool InGPUWritable = false);

    /** Destructor */
    virtual ~FZeroCopyBuffer();

    //~ Begin IBufferProvider Interface
    virtual bool Initialize() override;
    virtual void Shutdown() override;
    virtual bool IsInitialized() const override;
    virtual FName GetBufferName() const { return Name; }
    virtual uint64 GetSizeInBytes() const { return SizeInBytes; }
    virtual void* Map(EBufferAccessMode AccessMode = EBufferAccessMode::ReadWrite) override { return MapBuffer(AccessMode); }
    virtual bool Unmap() override { UnmapBuffer(); return true; }
    virtual bool IsMapped() const override { return IsBufferMapped(); }
    virtual bool Resize(uint64 NewSizeInBytes, bool bPreserveContent = true) override;
    virtual void SetUsageHint(EBufferUsage UsageHint) override { /* Not implemented */ }
    virtual EBufferUsage GetUsageHint() const override { return EBufferUsage::General; }
    virtual bool SupportsZeroCopy() const override { return true; }
    virtual bool IsGPUWritable() const override { return bGPUWritable; }
    virtual uint64 GetVersionNumber() const override { return 0; }
    virtual void* GetGPUResource() const override { return ResourceBuffer.GetReference(); }
    virtual void AddRef() override { /* Not implemented */ }
    virtual uint32 Release() override { return 0; }
    virtual FBufferStats GetStats() const override;
    virtual bool Validate(TArray<FString>& OutErrors) const override;
    //~ End IBufferProvider Interface

    /**
     * Gets the name of this buffer
     * @return Buffer name
     */
    FName GetName() const;

    /**
     * Gets the size of this buffer
     * @return Size in bytes
     */
    uint64 GetBufferSize() const;

    /**
     * Gets the raw CPU memory for this buffer
     * @return Pointer to buffer memory
     */
    void* GetRawBuffer() const;

    /**
     * Maps the buffer for CPU access
     * @param AccessMode How the buffer will be accessed
     * @return Pointer to mapped memory
     */
    void* MapBuffer(EBufferAccessMode AccessMode = EBufferAccessMode::ReadWrite);

    /**
     * Unmaps the buffer from CPU access
     */
    void UnmapBuffer();

    /**
     * Checks if the buffer is currently mapped
     * @return True if mapped
     */
    bool IsBufferMapped() const;

    /**
     * Syncs CPU data to GPU buffer
     */
    void SyncToGPU();

    /**
     * Syncs GPU data to CPU buffer
     */
    void SyncFromGPU();

    /**
     * Checks if the GPU buffer is valid
     * @return True if valid
     */
    bool IsGPUBufferValid() const;

    /**
     * Gets the RHI buffer resource
     * @return Buffer resource
     */
    FRHIStructuredBuffer* GetRHIBuffer() const;

    /**
     * Gets shader resource view for the buffer
     * @return Shader resource view
     */
    FRHIShaderResourceView* GetShaderResourceView() const;

    /**
     * Gets unordered access view for the buffer (if GPU writable)
     * @return Unordered access view
     */
    FRHIUnorderedAccessView* GetUnorderedAccessView() const;

private:
    /**
     * Creates GPU resources on the render thread
     */
    void CreateGPUBuffer_RenderThread();

    /**
     * Releases GPU resources on the render thread
     */
    void ReleaseGPUBuffer_RenderThread();

    /** Buffer name */
    FName Name;

    /** Size of the buffer in bytes */
    uint64 SizeInBytes;

    /** Raw CPU memory buffer */
    void* RawData;

    /** Pointer to mapped memory (when mapped) */
    void* MappedData;

    /** GPU resource buffer */
    FStructuredBufferRHIRef ResourceBuffer;

    /** Shader resource view (for reading in shaders) */
    FShaderResourceViewRHIRef ShaderResourceView;

    /** Unordered access view (for writing in shaders) */
    FUnorderedAccessViewRHIRef UnorderedAccessView;

    /** Whether the buffer is writable by the GPU */
    bool bGPUWritable;

    /** Whether the buffer is initialized */
    bool bInitialized;

    /** Current buffer access mode */
    EBufferAccessMode CurrentAccessMode;
};
