// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IBufferProvider.h"
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
    virtual FName GetBufferName() const override;   // Changed from GetName to GetBufferName
    virtual uint64 GetSizeInBytes() const override; // Changed from GetBufferSize to GetSizeInBytes
    virtual void* GetRawBuffer() const;             // Removed override as this isn't in the interface
    virtual void* Map(EBufferAccessMode AccessMode = EBufferAccessMode::ReadWrite) override;  // Changed from MapBuffer to Map
    virtual bool Unmap() override;                  // Changed from void UnmapBuffer() to bool Unmap()
    virtual bool IsMapped() const override;         // Changed from IsBufferMapped to IsMapped
    virtual bool Resize(uint64 NewSizeInBytes, bool bPreserveContent = true) override;  // Added missing method
    virtual void SetUsageHint(EBufferUsage UsageHint) override;
    virtual EBufferUsage GetUsageHint() const override;
    virtual bool SupportsZeroCopy() const override;
    virtual bool IsGPUWritable() const override;
    virtual uint64 GetVersionNumber() const override;
    virtual void* GetGPUResource() const override;
    virtual void AddRef() override;                 // Changed return type from int32 to void
    virtual uint32 Release() override;              // Changed return type from int32 to uint32
    virtual FBufferStats GetStats() const override;
    virtual bool Validate(TArray<FString>& OutErrors) const override; // Added missing method
    //~ End IBufferProvider Interface

    /**
     * Gets the current reference count
     * @return Number of active references
     */
    int32 GetRefCount() const;

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
