// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "2_MemoryManagement/Public/Interfaces/IBufferProvider.h"
#include "HAL/ThreadSafeBool.h"
#include "Containers/Queue.h"
#include "RHI.h"
#include "RHIResources.h"

/**
 * Manages buffer sharing between CPU and GPU with reference counting
 * and version tracking for consistent access during mining operations
 */
class MININGSPICECOPILOT_API FSharedBufferManager : public IBufferProvider
{
public:
    /**
     * Constructor
     * @param InBufferName Name of the buffer
     * @param InSizeInBytes Size of the buffer in bytes
     * @param InGPUWritable Whether the GPU can write to the buffer
     */
    FSharedBufferManager(
        const FName& InBufferName,
        uint64 InSizeInBytes,
        bool InGPUWritable = false);
    
    /** Destructor */
    virtual ~FSharedBufferManager();

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
     * Sets the buffer update frequency for optimal RHI resource creation
     * @param InUpdateFrequency How frequently this buffer will be updated
     */
    void SetUpdateFrequency(EBufferUpdateFrequency InUpdateFrequency);

    /**
     * Gets the buffer update frequency
     * @return Current update frequency setting
     */
    EBufferUpdateFrequency GetUpdateFrequency() const;

    /**
     * Sets whether this buffer should use a staging buffer to avoid stalls
     * @param bInUseStagingBuffer Whether to use a staging buffer
     */
    void SetUseStagingBuffer(bool bInUseStagingBuffer);

    /**
     * Updates a specific region of the buffer
     * @param DestOffset Offset into the buffer to start the update
     * @param SrcData Source data to copy from
     * @param DataSize Size of the data to copy
     * @return True if the update was successful
     */
    bool UpdateBufferRegion(uint64 DestOffset, const void* SrcData, uint64 DataSize);

    /**
     * Gets the maximum supported buffer size for this platform
     * @return Maximum buffer size in bytes
     */
    static uint64 GetMaxBufferSize();

    /**
     * Registers a snapshot of the current buffer state
     * @param SnapshotName Name to identify the snapshot
     * @return A version ID for the snapshot, or 0 if failed
     */
    uint32 TakeSnapshot(const FName& SnapshotName);

    /**
     * Retrieves a snapshot by name
     * @param SnapshotName Name of the snapshot to retrieve
     * @return Version ID for the snapshot, or 0 if not found
     */
    uint32 GetSnapshotVersion(const FName& SnapshotName) const;

protected:
    /** Name of the buffer */
    FName BufferName;
    
    /** Size of the buffer in bytes */
    uint64 SizeInBytes;
    
    /** CPU-side buffer data */
    uint8* BufferData;
    
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
    
    /** Staging buffer for GPU uploads */
    FBufferRHIRef StagingBuffer;
    
    /** Whether to use a staging buffer for updates */
    bool bUseStagingBuffer;
    
    /** Buffer update frequency for RHI resource creation */
    EBufferUpdateFrequency UpdateFrequency;
    
    /** Critical section for thread safety */
    mutable FCriticalSection BufferLock;
    
    /** Current version of the buffer */
    uint32 CurrentVersion;
    
    /** Map of named snapshots to version IDs */
    TMap<FName, uint32> VersionSnapshots;
    
    /** Stats for this buffer */
    mutable FBufferStats CachedStats;
    
    /** Whether stats are dirty and need updating */
    mutable bool bStatsDirty;

private:
    /** Creates or updates the GPU resource */
    bool CreateGPUResource();
    
    /** Releases the GPU resource */
    void ReleaseGPUResource();
    
    /** Updates the GPU resource with CPU data */
    bool UpdateGPUResource();
    
    /** Creates a staging buffer for efficient updates */
    bool CreateStagingBuffer();
    
    /** Updates stats information */
    void UpdateStats() const;
    
    /** Calculates the best usage flags for the RHI buffer */
    EBufferUsageFlags GetBufferUsageFlags() const;
};
