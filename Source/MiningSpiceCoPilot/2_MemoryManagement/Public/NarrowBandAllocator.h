// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "2_MemoryManagement/Public/Interfaces/IPoolAllocator.h"
#include "2_MemoryManagement/Public/Interfaces/IMemoryManager.h"
#include "Math/SIMDFloat.h"

/**
 * Specialized allocator for narrow-band distance field data
 * Provides optimized memory management for multi-channel distance field operations
 */
class MININGSPICECOPILOT_API FNarrowBandAllocator : public IPoolAllocator
{
public:
    /**
     * Constructor
     * @param InPoolName Name of this memory pool
     * @param InBlockSize Size of each memory block in bytes
     * @param InBlockCount Initial number of blocks to allocate
     * @param InAccessPattern Memory access pattern to optimize for
     * @param InAllowGrowth Whether the pool can grow beyond initial size
     */
    FNarrowBandAllocator(const FName& InPoolName, uint32 InBlockSize, uint32 InBlockCount, 
        EMemoryAccessPattern InAccessPattern = EMemoryAccessPattern::SDFOperation, 
        bool InAllowGrowth = true);

    /** Destructor */
    virtual ~FNarrowBandAllocator();

    //~ Begin IPoolAllocator Interface
    virtual bool Initialize() override;
    virtual void Shutdown() override;
    virtual bool IsInitialized() const override;
    virtual FName GetPoolName() const override;
    virtual uint32 GetBlockSize() const override;
    virtual void* Allocate(const UObject* RequestingObject = nullptr, FName AllocationTag = NAME_None) override;
    virtual bool Free(void* Ptr) override;
    virtual bool Grow(uint32 AdditionalBlockCount, bool bForceGrowth = false) override;
    virtual uint32 Shrink(uint32 MaxBlocksToRemove = UINT32_MAX) override;
    virtual bool OwnsPointer(const void* Ptr) const override;
    virtual void SetAccessPattern(EMemoryAccessPattern AccessPattern) override;
    virtual EMemoryAccessPattern GetAccessPattern() const override;
    virtual FPoolStats GetStats() const override;
    virtual bool Defragment(float MaxTimeMs = 5.0f) override;
    virtual bool Validate(TArray<FString>& OutErrors) const override;
    //~ End IPoolAllocator Interface

    /**
     * Sets the precision tier for memory allocation
     * @param Tier Memory precision tier (Hot=high, Warm=medium, Cold=low, Archive=compressed)
     */
    void SetPrecisionTier(EMemoryTier Tier);

    /**
     * Gets the current precision tier
     * @return Current memory precision tier
     */
    EMemoryTier GetPrecisionTier() const;

    /**
     * Sets the number of material channels for each element
     * @param NumChannels Number of material channels
     */
    void SetChannelCount(uint32 NumChannels);

    /**
     * Gets the current number of channels per element
     * @return Number of material channels
     */
    uint32 GetChannelCount() const;

    /**
     * Resets the narrow band allocator to its initial empty state
     * @return True if the allocator was successfully reset
     */
    bool Reset();

private:
    /**
     * Allocates memory for the pool with appropriate alignment for SIMD operations
     * @param BlockCount Number of blocks to allocate
     * @return True if allocation was successful
     */
    bool AllocatePoolMemory(uint32 BlockCount);

    /**
     * Frees all memory allocated by this pool
     */
    void FreePoolMemory();

    /**
     * Calculates memory metrics for this pool
     */
    void UpdateStats();

    /**
     * Gets a block index from a pointer
     * @param Ptr Pointer to convert to block index
     * @return Block index or INDEX_NONE if invalid
     */
    int32 GetBlockIndex(const void* Ptr) const;

    /**
     * Gets the optimal element alignment based on current precision tier
     * @return Alignment in bytes
     */
    uint32 GetElementAlignment() const;

    /**
     * Gets the bytes per channel based on current precision tier
     * @return Bytes per channel
     */
    uint32 GetBytesPerChannel() const;
    
    /** Structure containing metadata for tracking memory block allocations */
    struct FBlockMetadata
    {
        /** Whether this block is currently allocated */
        bool bAllocated;
        
        /** Allocation tag for tracking */
        FName AllocationTag;
        
        /** Object that requested this allocation */
        TWeakObjectPtr<const UObject> RequestingObject;
        
        /** Time when this block was allocated */
        double AllocationTime;

        /** Distance from the surface (0 = directly on surface) */
        float DistanceFromSurface;
        
        /** Constructor */
        FBlockMetadata()
            : bAllocated(false)
            , AllocationTag(NAME_None)
            , RequestingObject(nullptr)
            , AllocationTime(0.0)
            , DistanceFromSurface(0.0f)
        {
        }
    };

    /** Name of this pool */
    FName PoolName;
    
    /** Size of each block in bytes */
    uint32 BlockSize;
    
    /** Base address of the pool memory */
    uint8* PoolMemory;
    
    /** Maximum number of blocks this pool can hold (initial capacity) */
    uint32 MaxBlockCount;
    
    /** Current number of blocks in the pool */
    uint32 CurrentBlockCount;
    
    /** Array of free block indices */
    TArray<uint32> FreeBlocks;
    
    /** Array of metadata for each block */
    TArray<FBlockMetadata> BlockMetadata;
    
    /** Lock for thread safety */
    mutable FCriticalSection PoolLock;
    
    /** Whether this pool has been initialized */
    bool bIsInitialized;
    
    /** Whether this pool allows growth beyond initial capacity */
    bool bAllowsGrowth;
    
    /** Memory access pattern for this pool */
    EMemoryAccessPattern AccessPattern;
    
    /** Precision tier for memory allocation */
    EMemoryTier PrecisionTier;
    
    /** Number of material channels per element */
    uint32 ChannelCount;
    
    /** Whether the stats need to be recalculated */
    mutable bool bStatsDirty;
    
    /** Cached pool statistics */
    mutable FPoolStats CachedStats;
};
