// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "2_MemoryManagement/Public/Interfaces/IPoolAllocator.h"
#include "2_MemoryManagement/Public/Interfaces/IMemoryManager.h"

/**
 * Specialized memory allocator for SVO (Sparse Voxel Octree) nodes
 * Uses Z-order curve indexing for cache-coherent spatial access
 */
class MININGSPICECOPILOT_API FSVOAllocator : public IPoolAllocator
{
public:
    /**
     * Constructor
     * @param InPoolName Name of the pool
     * @param InBlockSize Size of each block in bytes
     * @param InBlockCount Initial number of blocks
     * @param InAccessPattern Access pattern for optimization
     * @param InAllowGrowth Whether the pool can grow
     */
    FSVOAllocator(
        const FName& InPoolName,
        uint32 InBlockSize, 
        uint32 InBlockCount,
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
    virtual bool Reset() override;
    //~ End IPoolAllocator Interface

    /**
     * Gets the Z-order curve mapping function
     * @return Z-order curve mapping function pointer
     */
    typedef uint32 (*FZOrderMappingFunction)(uint32 X, uint32 Y, uint32 Z);
    FORCEINLINE FZOrderMappingFunction GetZOrderMapping() const { return ZOrderMappingFunction; }

    /**
     * Sets the Z-order curve mapping function
     * @param InMappingFunction New Z-order curve mapping function
     */
    void SetZOrderMapping(FZOrderMappingFunction InMappingFunction);

    /**
     * Allocates a block at a specific position in the octree
     * @param X X coordinate in the octree
     * @param Y Y coordinate in the octree
     * @param Z Z coordinate in the octree
     * @param RequestingObject Optional object for tracking
     * @param AllocationTag Optional tag for tracking
     * @return Pointer to the allocated memory or nullptr if allocation failed
     */
    void* AllocateAtPosition(uint32 X, uint32 Y, uint32 Z, 
        const UObject* RequestingObject = nullptr, FName AllocationTag = NAME_None);
        
    /**
     * Gets the position of a block in the octree
     * @param Ptr Pointer to the block
     * @param OutX Output X coordinate
     * @param OutY Output Y coordinate
     * @param OutZ Output Z coordinate
     * @return True if the position was retrieved successfully
     */
    bool GetBlockPosition(const void* Ptr, uint32& OutX, uint32& OutY, uint32& OutZ) const;
    
    /**
     * Preallocation for expected access pattern
     * @param X Center X coordinate
     * @param Y Center Y coordinate
     * @param Z Center Z coordinate
     * @param Radius Radius of the preallocation sphere
     * @return Number of blocks preallocated
     */
    uint32 PreallocateRegion(uint32 X, uint32 Y, uint32 Z, uint32 Radius);

protected:
    /** The name of this pool */
    FName PoolName;
    
    /** Size of each block in bytes */
    uint32 BlockSize;
    
    /** Pointer to the memory blocks */
    uint8* PoolMemory;
    
    /** Array of free block indices */
    TArray<uint32> FreeBlocks;
    
    /** Maximum number of blocks */
    uint32 MaxBlockCount;
    
    /** Current number of blocks */
    uint32 CurrentBlockCount;
    
    /** Whether the pool has been initialized */
    bool bIsInitialized;
    
    /** Whether the pool allows growth */
    bool bAllowsGrowth;
    
    /** Current access pattern */
    EMemoryAccessPattern AccessPattern;
    
    /** Z-order curve mapping function */
    FZOrderMappingFunction ZOrderMappingFunction;
    
    /** Block metadata */
    struct FBlockMetadata
    {
        FName AllocationTag;
        TWeakObjectPtr<const UObject> RequestingObject;
        uint32 X;
        uint32 Y;
        uint32 Z;
        bool bAllocated;
        double AllocationTime;
    };
    
    /** Array of block metadata */
    TArray<FBlockMetadata> BlockMetadata;
    
    /** Protection for thread-safety */
    mutable FCriticalSection PoolLock;
    
    /** Pool statistics */
    mutable FPoolStats CachedStats;

    /** Whether stats are dirty and need recalculation */
    mutable bool bStatsDirty;

private:
    /** Allocate the pool memory */
    bool AllocatePoolMemory(uint32 InBlockCount);
    
    /** Free the pool memory */
    void FreePoolMemory();
    
    /** Calculate pool statistics */
    void UpdateStats() const;

    /** Default Z-order mapping function */
    static uint32 DefaultZOrderMapping(uint32 X, uint32 Y, uint32 Z);
};