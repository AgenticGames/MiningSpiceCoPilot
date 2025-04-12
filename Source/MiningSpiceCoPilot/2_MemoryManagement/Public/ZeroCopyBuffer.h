// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "2_MemoryManagement/Public/Interfaces/IBufferProvider.h"
#include "HAL/ThreadSafeBool.h"
#include "RHI.h"
#include "RHIResources.h"

/**
 * Zero-copy buffer implementation for GPU/CPU sharing
 * Provides efficient shared memory between CPU and GPU with minimal copying
 */
class MININGSPICECOPILOT_API FZeroCopyBuffer : public IBufferProvider
{
public:
    /**
     * Constructor
     * @param InBufferName Name of the buffer
     * @param InSizeInBytes Size of the buffer in bytes
     * @param InGPUWritable Whether the GPU can write to the buffer
     */
    FZeroCopyBuffer(
        const FName& InBufferName,
        uint64 InSizeInBytes,
        bool InGPUWritable = false);
    
    /** Destructor */
    virtual ~FZeroCopyBuffer();

    //~ Begin IBufferProvider Interface
    virtual bool Initialize() override;
    virtual void Shutdown() override;
    virtual bool IsInitialized() const override;
    virtual FName GetBufferName() const override;
    virtual uint64 GetBufferSize() const override;
    virtual void* MapBuffer(EBufferAccessFlags AccessFlags = EBufferAccessFlags::Read, uint32 VersionId = 0) override;
    virtual bool UnmapBuffer() override;
    virtual bool ResizeBuffer(uint64 NewSizeInBytes) override;
    virtual void* GetRawBuffer() const override;
    virtual FRHIBuffer* GetGPUBuffer() const override;
    virtual bool IsGPUAccessible() const override;
    virtual bool IsGPUWritable() const override;
    virtual FBufferStats GetStats() const override;
    virtual uint32 GetCurrentVersion() const override;
    virtual bool Validate(TArray<FString>& OutErrors) const override;
    //~ End IBufferProvider Interface

    /**
     * Checks if zero-copy is supported on this platform
     * @return True if zero-copy is supported
     */
    static bool IsZeroCopySupported();

    /**
     * Gets the specified memory domain for zero-copy allocation
     * @return Memory domain for zero-copy allocation
     */
    EMemoryDomain GetMemoryDomain() const;

    /**
     * Sets the memory domain for zero-copy allocation
     * @param InDomain Memory domain to use
     */
    void SetMemoryDomain(EMemoryDomain InDomain);
    
    /**
     * Synchronizes CPU access to the buffer after GPU operations
     * @param bWaitForGPU Whether to wait for GPU operations to complete
     * @param AccessFlags How the CPU intends to access the memory
     * @return True if synchronization was successful
     */
    bool SynchronizeCPUAccess(bool bWaitForGPU, EBufferAccessFlags AccessFlags);
    
    /**
     * Synchronizes GPU access to the buffer after CPU operations
     * @param AccessFlags How the GPU intends to access the memory
     * @return True if synchronization was successful
     */
    bool SynchronizeGPUAccess(EBufferAccessFlags AccessFlags);
    
    /**
     * Pins the buffer to a specific memory range to prevent paging
     * @param OffsetBytes Offset into the buffer in bytes
     * @param SizeBytes Size of the range to pin in bytes
     * @return True if pinning was successful
     */
    bool PinMemory(uint64 OffsetBytes, uint64 SizeBytes);
    
    /**
     * Unpins memory previously pinned with PinMemory
     */
    void UnpinMemory();
    
    /**
     * Prefetches buffer data into CPU cache
     * @param OffsetBytes Offset into the buffer in bytes
     * @param SizeBytes Size of the range to prefetch in bytes
     * @return True if prefetching was successful
     */
    bool PrefetchToCPUCache(uint64 OffsetBytes, uint64 SizeBytes);

protected:
    /** Name of the buffer */
    FName BufferName;
    
    /** Size of the buffer in bytes */
    uint64 SizeInBytes;
    
    /** Whether the buffer has been initialized */
    FThreadSafeBool bIsInitialized;
    
    /** Whether the buffer is currently mapped */
    FThreadSafeBool bIsMapped;
    
    /** Current mapping access flags */
    EBufferAccessFlags CurrentAccessFlags;
    
    /** Whether the GPU can write to this buffer */
    bool bIsGPUWritable;
    
    /** Whether the buffer is currently accessible by the GPU */
    FThreadSafeBool bIsGPUAccessible;
    
    /** The GPU resource (created on demand) */
    FBufferRHIRef GPUResource;
    
    /** Raw buffer pointer (shared between CPU and GPU) */
    uint8* ZeroCopyMemory;
    
    /** Memory domain for the buffer */
    EMemoryDomain MemoryDomain;
    
    /** Current version of the buffer */
    uint32 CurrentVersion;
    
    /** Critical section for thread safety */
    mutable FCriticalSection BufferLock;
    
    /** Stats for this buffer */
    mutable FBufferStats CachedStats;
    
    /** Whether stats are dirty and need updating */
    mutable bool bStatsDirty;
    
    /** Last fence used for GPU synchronization */
    FRHIGPUFence* LastGPUFence;

private:
    /** Create a zero-copy buffer */
    bool CreateZeroCopyBuffer();
    
    /** Destroy the zero-copy buffer */
    void DestroyZeroCopyBuffer();
    
    /** Update stats information */
    void UpdateStats() const;
    
    /** Get the appropriate RHI buffer creation flags */
    uint32 GetRHIBufferCreateFlags() const;
    
    /** Check if memory is accessible to both CPU and GPU */
    bool ValidateMemoryAccess() const;
    
    /** Apply appropriate memory barriers based on access pattern */
    void ApplyMemoryBarrier(EBufferAccessFlags PreviousAccess, EBufferAccessFlags NewAccess);
};
