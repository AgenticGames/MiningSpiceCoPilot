// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "2_MemoryManagement/Public/Interfaces/IBufferProvider.h"
#include "HAL/CriticalSection.h"
#include "HAL/ThreadSafeBool.h"
#include "Containers/StaticArray.h"
#include "RHIDefinitions.h"

/**
 * Shared buffer manager implementation for CPU/GPU data sharing
 * Provides efficient memory sharing between CPU and GPU with version tracking and reference counting
 */
class MININGSPICECOPILOT_API FSharedBufferManager : public IBufferProvider
{
public:
    /**
     * Constructor
     * @param InName Buffer name for tracking and debugging
     * @param InSizeInBytes Size of the buffer in bytes
     * @param InGPUWritable Whether the GPU can write to this buffer
     */
    FSharedBufferManager(const FName& InName, uint64 InSizeInBytes, bool InGPUWritable = false);

    /** Destructor */
    virtual ~FSharedBufferManager();

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
     * Adds a reference to this buffer
     * @return New reference count
     */
    int32 AddRef();

    /**
     * Releases a reference to this buffer
     * @return New reference count, if 0 buffer may be released
     */
    int32 Release();

    /**
     * Gets the current reference count
     * @return Number of active references
     */
    int32 GetRefCount() const;

    /**
     * Gets the version number of this buffer
     * Increments each time the buffer is modified
     * @return Current version number
     */
    uint64 GetVersionNumber() const;

    /**
     * Sets a usage hint for this buffer
     * @param InUsage Usage hint for optimizing buffer access
     */
    void SetUsageHint(EBufferUsage InUsage);

    /**
     * Gets the current usage hint
     * @return Current buffer usage hint
     */
    EBufferUsage GetUsageHint() const;

    /**
     * Gets statistics about this buffer
     * @return Buffer statistics
     */
    FBufferStats GetStats() const;

private:
    /**
     * Creates a new version of the buffer
     * Used when modifications are detected
     */
    void BumpVersion();

    /** Buffer name */
    FName Name;

    /** Size of the buffer in bytes */
    uint64 SizeInBytes;

    /** Raw CPU memory buffer */
    void* RawData;

    /** Pointer to mapped memory (when mapped) */
    void* MappedData;

    /** Current buffer access mode */
    EBufferAccessMode CurrentAccessMode;

    /** Whether the buffer is initialized */
    FThreadSafeBool bInitialized;

    /** Whether the buffer has pending CPU changes */
    FThreadSafeBool bHasPendingCPUChanges;

    /** Whether the buffer has pending GPU changes */
    FThreadSafeBool bHasPendingGPUChanges;

    /** Current buffer usage hint */
    EBufferUsage UsageHint;

    /** Whether the buffer is writable by the GPU */
    bool bGPUWritable;

    /** Current version number of the buffer */
    FThreadSafeCounter64 VersionNumber;

    /** Number of active references to the buffer */
    FThreadSafeCounter RefCount;

    /** Lock for thread-safe buffer access */
    mutable FCriticalSection BufferLock;

    /** Number of map operations performed */
    FThreadSafeCounter64 MapCount;

    /** Number of unmap operations performed */
    FThreadSafeCounter64 UnmapCount;

    /** Number of sync to GPU operations performed */
    FThreadSafeCounter64 SyncToGPUCount;

    /** Number of sync from GPU operations performed */
    FThreadSafeCounter64 SyncFromGPUCount;

    /** Statistics for this buffer */
    mutable FBufferStats CachedStats;
};
