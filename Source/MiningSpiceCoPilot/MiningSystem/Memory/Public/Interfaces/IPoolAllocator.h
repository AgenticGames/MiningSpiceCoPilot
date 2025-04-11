// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Interfaces/IMemoryManager.h"
#include "IPoolAllocator.generated.h"

/**
 * Structure containing information about a memory pool's current state
 */
struct MININGSPICECOPILOT_API FPoolStats
{
    /** Name of the pool */
    FName PoolName;
    
    /** Size of each block in bytes */
    uint32 BlockSize;
    
    /** Number of blocks in the pool */
    uint32 BlockCount;
    
    /** Number of allocated blocks */
    uint32 AllocatedBlocks;
    
    /** Number of free blocks */
    uint32 FreeBlocks;
    
    /** Peak number of allocated blocks */
    uint32 PeakAllocatedBlocks;
    
    /** Total number of allocation requests */
    uint64 TotalAllocations;
    
    /** Total number of free operations */
    uint64 TotalFrees;
    
    /** Allocation failures due to out-of-memory */
    uint64 AllocationFailures;
    
    /** Whether the pool allows growth */
    bool bAllowsGrowth;
    
    /** Number of times the pool has grown */
    uint32 GrowthCount;
    
    /** Memory overhead for pool management in bytes */
    uint64 OverheadBytes;
    
    /** Fragmentation percentage (0-100) */
    float FragmentationPercent;
};

/**
 * Base interface for memory pool allocators in the SVO+SDF mining architecture
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UPoolAllocator : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for memory pool allocators in the SVO+SDF mining architecture
 * Provides block-based memory allocation optimized for specific use cases
 */
class MININGSPICECOPILOT_API IPoolAllocator
{
    GENERATED_BODY()

public:
    /**
     * Initializes the pool allocator and prepares it for use
     * @return True if initialization was successful
     */
    virtual bool Initialize() = 0;

    /**
     * Shuts down the pool allocator and cleans up resources
     */
    virtual void Shutdown() = 0;
    
    /**
     * Checks if the pool allocator has been initialized
     * @return True if initialized, false otherwise
     */
    virtual bool IsInitialized() const = 0;
    
    /**
     * Gets the name of this pool
     * @return The pool name
     */
    virtual FName GetPoolName() const = 0;
    
    /**
     * Gets the block size for this pool
     * @return Block size in bytes
     */
    virtual uint32 GetBlockSize() const = 0;
    
    /**
     * Allocates a block from the pool
     * @param RequestingObject Optional object for tracking (can be null)
     * @param AllocationTag Optional tag for tracking allocations
     * @return Pointer to the allocated memory or nullptr if allocation failed
     */
    virtual void* Allocate(const UObject* RequestingObject = nullptr, FName AllocationTag = NAME_None) = 0;
    
    /**
     * Frees a block previously allocated from this pool
     * @param Ptr Pointer to the memory to free
     * @return True if the memory was successfully freed
     */
    virtual bool Free(void* Ptr) = 0;
    
    /**
     * Attempts to grow the pool by adding more blocks
     * @param AdditionalBlockCount Number of blocks to add
     * @param bForceGrowth Whether to force growth even if not normally allowed
     * @return True if the pool was successfully grown
     */
    virtual bool Grow(uint32 AdditionalBlockCount, bool bForceGrowth = false) = 0;
    
    /**
     * Attempts to shrink the pool by removing unused blocks
     * @param MaxBlocksToRemove Maximum number of blocks to remove
     * @return Number of blocks actually removed
     */
    virtual uint32 Shrink(uint32 MaxBlocksToRemove = UINT32_MAX) = 0;
    
    /**
     * Checks if a pointer belongs to this pool
     * @param Ptr Pointer to check
     * @return True if the pointer belongs to this pool
     */
    virtual bool OwnsPointer(const void* Ptr) const = 0;
    
    /**
     * Sets the memory access pattern for optimizing allocation strategies
     * @param AccessPattern The new access pattern
     */
    virtual void SetAccessPattern(EMemoryAccessPattern AccessPattern) = 0;
    
    /**
     * Gets the current memory access pattern
     * @return The current access pattern
     */
    virtual EMemoryAccessPattern GetAccessPattern() const = 0;
    
    /**
     * Gets current statistics for this pool
     * @return Structure containing pool statistics
     */
    virtual FPoolStats GetStats() const = 0;
    
    /**
     * Performs defragmentation on the pool
     * @param MaxTimeMs Maximum time to spend on defragmentation in milliseconds
     * @return True if defragmentation was performed
     */
    virtual bool Defragment(float MaxTimeMs = 5.0f) = 0;
    
    /**
     * Validates the pool's internal state for debugging
     * @param OutErrors Collection of errors found during validation
     * @return True if valid, false if errors were found
     */
    virtual bool Validate(TArray<FString>& OutErrors) const = 0;
    
    /**
     * Clears all allocations and resets the pool to its initial state
     * @return True if the pool was successfully reset
     */
    virtual bool Reset() = 0;
};