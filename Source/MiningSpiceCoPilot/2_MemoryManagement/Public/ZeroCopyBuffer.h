// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "2_MemoryManagement/Public/Interfaces/IBufferProvider.h"
#include "RHIDefinitions.h"
#include "RHIResources.h"

/**
 * Zero-copy buffer implementation for GPU/CPU data sharing
 * Provides efficient memory access for both CPU and GPU with minimal synchronization overhead
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
    virtual FName GetName() const override;
    virtual uint64 GetBufferSize() const override;
    virtual void* GetRawBuffer() const override;
    virtual void* MapBuffer(EBufferAccessMode AccessMode = EBufferAccessMode::ReadWrite) override;
    virtual void UnmapBuffer() override;
    virtual bool IsBufferMapped() const override;
    virtual void SyncToGPU() override;
    virtual void SyncFromGPU() override;
    virtual bool IsGPUBufferValid() const override;
    //~ End IBufferProvider Interface

    /**
     * Gets the RHI structured buffer resource
     * @return RHI buffer resource
     */
    FStructuredBufferRHIRef GetRHIBuffer() const;

    /**
     * Gets an SRV for this buffer
     * @return Shader Resource View for this buffer
     */
    FShaderResourceViewRHIRef GetShaderResourceView() const;

    /**
     * Gets a UAV for this buffer if GPU writable
     * @return Unordered Access View for this buffer
     */
    FUnorderedAccessViewRHIRef GetUnorderedAccessView() const;

private:
    /**
     * Creates the GPU resource on the render thread
     */
    void CreateGPUBuffer_RenderThread();

    /**
     * Releases the GPU resource on the render thread
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

    /** RHI structured buffer resource */
    FStructuredBufferRHIRef ResourceBuffer;

    /** Shader Resource View for the buffer */
    FShaderResourceViewRHIRef ShaderResourceView;

    /** Unordered Access View for the buffer (if GPU writable) */
    FUnorderedAccessViewRHIRef UnorderedAccessView;

    /** Whether the buffer is writable by the GPU */
    bool bGPUWritable;

    /** Whether the buffer has been initialized */
    bool bInitialized;

    /** Current buffer access mode */
    EBufferAccessMode CurrentAccessMode;
};
