// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Interfaces/IBufferProvider.h"
#include "RHI.h"
#include "RHIResources.h"
#include "RHICommandList.h"
#include "RHIDefinitions.h"
#include "RenderResource.h"
#include "RHIUtilities.h"
#include "RenderCore.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/ThreadSafeCounter.h"
#include "HAL/ThreadSafeCounter64.h"
#include "Misc/ScopeLock.h"
#include "Async/AsyncWork.h"
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
 * Memory access pattern tracker for mining operations
 * Helps optimize prefetching and memory layout based on observed patterns
 */
struct FMemoryAccessPattern
{
    /** Access type (sequential, random, strided) */
    enum class EPatternType : uint8
    {
        Sequential,  // Sequential access pattern (ideal)
        Strided,     // Regular stride pattern (common in SDF field operations)
        Random,      // Random access pattern (worst case)
        Unknown      // Not enough data to determine pattern
    };

    /** Create and initialize a new pattern tracker */
    FMemoryAccessPattern();

    /** Record a memory access at the given offset */
    void RecordAccess(uint64 Offset, uint64 Size);

    /** Get the detected access pattern type */
    EPatternType GetPatternType() const;

    /** Get suggested prefetch size based on pattern */
    uint64 GetSuggestedPrefetchSize() const;

    /** Reset pattern detection */
    void Reset();

private:
    /** Maximum number of accesses to track */
    static const int32 MaxAccessesToTrack = 16;

    /** Recent access offsets */
    TArray<uint64> RecentAccesses;

    /** Recent access sizes */
    TArray<uint64> RecentSizes;

    /** Detected pattern type */
    EPatternType PatternType;

    /** Average access size */
    uint64 AverageAccessSize;

    /** Last analyzed access count */
    int32 LastAnalyzedCount;

    /** Analyze current pattern from recorded accesses */
    void AnalyzePattern();
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
     * @param InUsageHint Usage hint for optimizing memory layout
     * @param InGPUWritable Whether the GPU can write to this buffer
     */
    FZeroCopyBuffer(const FName& InName, uint64 InSizeInBytes, 
                   EBufferUsage InUsageHint = EBufferUsage::General,
                   bool InGPUWritable = false);

    /** Destructor */
    virtual ~FZeroCopyBuffer();

    //~ Begin IBufferProvider Interface
    virtual bool Initialize() override;
    virtual void Shutdown() override;
    virtual bool IsInitialized() const override;
    virtual FName GetBufferName() const override { return Name; }
    virtual uint64 GetSizeInBytes() const override { return SizeInBytes; }
    virtual void* Map(EBufferAccessMode AccessMode = EBufferAccessMode::ReadWrite) override;
    virtual bool Unmap() override;
    virtual bool IsMapped() const override;
    virtual bool Resize(uint64 NewSizeInBytes, bool bPreserveContent = true) override;
    virtual void SetUsageHint(EBufferUsage UsageHint) override;
    virtual EBufferUsage GetUsageHint() const override;
    virtual bool SupportsZeroCopy() const override { return true; }
    virtual bool IsGPUWritable() const override { return bGPUWritable; }
    virtual uint64 GetVersionNumber() const override;
    virtual void* GetGPUResource() const override;
    virtual void AddRef() override;
    virtual uint32 Release() override;
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
    TRefCountPtr<FRHIBuffer> GetRHIBuffer() const;

    /**
     * Gets shader resource view for the buffer
     * @return Shader resource view
     */
    TRefCountPtr<FRHIShaderResourceView> GetShaderResourceView() const;

    /**
     * Gets unordered access view for the buffer (if GPU writable)
     * @return Unordered access view
     */
    TRefCountPtr<FRHIUnorderedAccessView> GetUnorderedAccessView() const;

    /**
     * Optimizes buffer access based on mining operations
     * @param bEnablePrefetching Whether to enable prefetching
     */
    void OptimizeForMiningOperations(bool bEnablePrefetching = true);

    /**
     * Sets buffer state for active mining operations
     * @param bActiveMining Whether buffer is being used for active mining
     */
    void SetActiveMiningState(bool bActiveMining);

    /**
     * Waits for pending GPU operations to complete
     * @param bFlushCommands Whether to flush the command buffer
     */
    void WaitForGPU(bool bFlushCommands = true);

    /**
     * Creates a partial view of this buffer 
     * @param OffsetInBytes Offset from start of buffer
     * @param ViewSize Size of the view
     * @return New buffer view object, or nullptr if failed
     */
    FZeroCopyBuffer* CreateBufferView(uint64 OffsetInBytes, uint64 ViewSize);

private:
    /**
     * Creates GPU resources on the render thread
     */
    void CreateGPUBuffer_RenderThread();

    /**
     * Releases GPU resources on the render thread
     */
    void ReleaseGPUBuffer_RenderThread();

    /**
     * Optimizes buffer layout based on usage hint
     */
    void OptimizeLayoutForUsage();

    /**
     * Updates version number after modification
     */
    void IncrementVersion();

    /**
     * Records memory access for pattern detection
     * @param Offset Offset accessed
     * @param Size Size of the access 
     */
    void RecordMemoryAccess(uint64 Offset, uint64 Size);

    /** Buffer name */
    FName Name;

    /** Size of the buffer in bytes */
    uint64 SizeInBytes;

    /** Raw CPU memory buffer */
    void* RawData;

    /** Pointer to mapped memory (when mapped) */
    void* MappedData;

    /** GPU resource buffer */
    TRefCountPtr<FRHIBuffer> ResourceBuffer;

    /** Shader resource view (for reading in shaders) */
    TRefCountPtr<FRHIShaderResourceView> ShaderResourceView;

    /** Unordered access view (for writing in shaders) */
    TRefCountPtr<FRHIUnorderedAccessView> UnorderedAccessView;

    /** Whether the buffer is writable by the GPU */
    bool bGPUWritable;

    /** Whether the buffer is initialized */
    bool bInitialized;

    /** Current buffer access mode */
    EBufferAccessMode CurrentAccessMode;

    /** Usage hint for optimizing layout */
    EBufferUsage CurrentUsageHint;

    /** Reference count of this buffer */
    FThreadSafeCounter ReferenceCount;

    /** Version counter incremented on modifications */
    FThreadSafeCounter64 VersionCounter;

    /** Map count for telemetry */
    FThreadSafeCounter64 MapCount;

    /** Unmap count for telemetry */
    FThreadSafeCounter64 UnmapCount;

    /** Synchronization for thread safety */
    mutable FCriticalSection CriticalSection;

    /** Memory access pattern tracker */
    FMemoryAccessPattern AccessPattern;

    /** Whether buffer is being used for active mining */
    FThreadSafeBool bActiveMining;

    /** Whether prefetching is enabled */
    bool bPrefetchingEnabled;

    /** Cached buffer stats for performance */
    mutable FBufferStats CachedStats;

    /** Last time stats were updated */
    mutable double LastStatsUpdateTime;
};

/**
 * Factory function for creating optimized zero-copy buffers
 * @param Name Buffer name for tracking
 * @param SizeInBytes Size of buffer in bytes
 * @param UsageHint Usage pattern hint
 * @param bGPUWritable Whether GPU can write to buffer
 * @return New buffer instance or nullptr if creation failed
 */
MININGSPICECOPILOT_API FZeroCopyBuffer* CreateZeroCopyBuffer(
    const FName& Name, 
    uint64 SizeInBytes, 
    EBufferUsage UsageHint = EBufferUsage::General,
    bool bGPUWritable = false);
