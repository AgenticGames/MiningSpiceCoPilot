// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "2_MemoryManagement/Public/Interfaces/IPoolAllocator.h"
#include "2_MemoryManagement/Public/Interfaces/IMemoryManager.h"

/**
 * Specialized allocator for SVO octree nodes
 * Provides Z-order curve mapping for cache-coherent spatial data allocation
 */
class MININGSPICECOPILOT_API FSVOAllocator : public IPoolAllocator
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
    FSVOAllocator(const FName& InPoolName, uint32 InBlockSize, uint32 InBlockCount, 
        EMemoryAccessPattern InAccessPattern = EMemoryAccessPattern::OctreeTraversal, 
        bool InAllowGrowth = true);

    /** Destructor */
    virtual ~FSVOAllocator();

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
     * Sets the Z-order mapping function for optimizing spatial data locality
     * @param NewMappingFunction New mapping function to use
     */
    void SetZOrderMappingFunction(uint32 (*NewMappingFunction)(uint32 x, uint32 y, uint32 z));

    /**
     * Default Z-order curve mapping function
     * Interleaves bits from x, y, z coordinates to create a cache-friendly index
     * @param x X-coordinate
     * @param y Y-coordinate
     * @param z Z-coordinate 
     * @return Z-order curve index
     */
    static uint32 DefaultZOrderMapping(uint32 x, uint32 y, uint32 z);

    /**
     * High-performance Z-order curve mapping using lookup tables
     * @param x X-coordinate
     * @param y Y-coordinate
     * @param z Z-coordinate
     * @return Z-order curve index
     */
    static uint32 LookupTableZOrderMapping(uint32 x, uint32 y, uint32 z);

    /**
     * Resets the pool to its initial empty state
     * @return True if the pool was successfully reset
     */
    bool Reset();

private:
    /**
     * Allocates memory for the pool
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

    /** Structure containing metadata for a single block in the pool */
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
        
        /** Constructor */
        FBlockMetadata()
            : bAllocated(false)
            , AllocationTag(NAME_None)
            , RequestingObject(nullptr)
            , AllocationTime(0.0)
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
    
    /** Z-order curve mapping function for spatial data */
    uint32 (*ZOrderMappingFunction)(uint32 x, uint32 y, uint32 z);
    
    /** Whether the stats need to be recalculated */
    mutable bool bStatsDirty;
    
    /** Cached pool statistics */
    mutable FPoolStats CachedStats;
};