// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IBufferProvider.generated.h"

/**
 * Buffer access modes for determining memory visibility
 */
enum class EBufferAccessMode : uint8
{
    /** Read-only access to the buffer */
    ReadOnly,
    
    /** Write-only access to the buffer */
    WriteOnly,
    
    /** Read-write access to the buffer */
    ReadWrite
};

/**
 * Buffer usage hints for optimizing memory layout and access patterns
 */
enum class EBufferUsage : uint8
{
    /** General purpose buffer with balanced characteristics */
    General,
    
    /** Buffer optimized for SDF field data storage */
    SDFField,
    
    /** Buffer optimized for SVO octree node storage */
    SVONodes,
    
    /** Buffer optimized for material channel data */
    MaterialChannels,
    
    /** Buffer optimized for vertex data */
    VertexData,
    
    /** Buffer optimized for index data */
    IndexData
};

/**
 * Structure containing information about a buffer's current state
 */
struct MININGSPICECOPILOT_API FBufferStats
{
    /** Name of the buffer */
    FName BufferName;
    
    /** Size of the buffer in bytes */
    uint64 SizeInBytes;
    
    /** Number of active references to the buffer */
    uint32 ReferenceCount;
    
    /** Whether the buffer is currently mapped for CPU access */
    bool bIsMapped;
    
    /** Whether the buffer allows zero-copy access */
    bool bIsZeroCopy;
    
    /** Whether the buffer can be written to from GPU */
    bool bIsGPUWritable;
    
    /** Current version number of the buffer */
    uint64 VersionNumber;
    
    /** Number of map operations performed */
    uint64 MapCount;
    
    /** Number of unmap operations performed */
    uint64 UnmapCount;
    
    /** Last access mode used for mapping */
    EBufferAccessMode LastAccessMode;
    
    /** Usage hint for the buffer */
    EBufferUsage UsageHint;
};

/**
 * Base interface for buffer providers in the SVO+SDF mining architecture
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UBufferProvider : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for buffer providers in the SVO+SDF mining architecture
 * Provides buffer sharing between CPU and GPU components with efficient synchronization
 */
class MININGSPICECOPILOT_API IBufferProvider
{
    GENERATED_BODY()

public:
    /**
     * Initializes the buffer provider and prepares it for use
     * @return True if initialization was successful
     */
    virtual bool Initialize() = 0;

    /**
     * Shuts down the buffer provider and cleans up resources
     */
    virtual void Shutdown() = 0;
    
    /**
     * Checks if the buffer provider has been initialized
     * @return True if initialized, false otherwise
     */
    virtual bool IsInitialized() const = 0;
    
    /**
     * Gets the name of this buffer
     * @return The buffer name
     */
    virtual FName GetBufferName() const = 0;
    
    /**
     * Gets the size of this buffer
     * @return Buffer size in bytes
     */
    virtual uint64 GetSizeInBytes() const = 0;
    
    /**
     * Maps the buffer for CPU access
     * @param AccessMode Desired access mode for the mapping
     * @return Pointer to the mapped memory or nullptr if mapping failed
     */
    virtual void* Map(EBufferAccessMode AccessMode = EBufferAccessMode::ReadWrite) = 0;
    
    /**
     * Unmaps the buffer, making changes visible to the GPU if applicable
     * @return True if the buffer was successfully unmapped
     */
    virtual bool Unmap() = 0;
    
    /**
     * Checks if the buffer is currently mapped
     * @return True if the buffer is mapped
     */
    virtual bool IsMapped() const = 0;
    
    /**
     * Resizes the buffer, preserving content when possible
     * @param NewSizeInBytes New size in bytes
     * @param bPreserveContent Whether to preserve existing content
     * @return True if the buffer was successfully resized
     */
    virtual bool Resize(uint64 NewSizeInBytes, bool bPreserveContent = true) = 0;
    
    /**
     * Sets the usage hint for this buffer to optimize access patterns
     * @param UsageHint The new usage hint
     */
    virtual void SetUsageHint(EBufferUsage UsageHint) = 0;
    
    /**
     * Gets the current usage hint
     * @return The current usage hint
     */
    virtual EBufferUsage GetUsageHint() const = 0;
    
    /**
     * Checks if this buffer supports zero-copy access
     * @return True if zero-copy is supported
     */
    virtual bool SupportsZeroCopy() const = 0;
    
    /**
     * Checks if this buffer can be written to from the GPU
     * @return True if GPU writeable
     */
    virtual bool IsGPUWritable() const = 0;
    
    /**
     * Gets the current version number of the buffer
     * Version is incremented after each modification
     * @return Current version number
     */
    virtual uint64 GetVersionNumber() const = 0;
    
    /**
     * Gets the underlying GPU resource for rendering or compute operations
     * @return Pointer to the GPU resource or nullptr if not available
     */
    virtual void* GetGPUResource() const = 0;
    
    /**
     * Adds a reference to this buffer
     */
    virtual void AddRef() = 0;
    
    /**
     * Releases a reference to this buffer
     * @return Reference count after release
     */
    virtual uint32 Release() = 0;
    
    /**
     * Gets current statistics for this buffer
     * @return Structure containing buffer statistics
     */
    virtual FBufferStats GetStats() const = 0;
    
    /**
     * Validates the buffer's internal state for debugging
     * @param OutErrors Collection of errors found during validation
     * @return True if valid, false if errors were found
     */
    virtual bool Validate(TArray<FString>& OutErrors) const = 0;
};