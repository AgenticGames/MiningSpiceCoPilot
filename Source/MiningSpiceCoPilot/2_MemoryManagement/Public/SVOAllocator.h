// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IPoolAllocator.h"
#include "Interfaces/IMemoryManager.h"

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
    virtual bool Reset() override;
    virtual bool MoveNextFragmentedAllocation(void*& OutOldPtr, void*& OutNewPtr, uint64& OutSize) override;
    virtual bool UpdateTypeVersion(const FTypeVersionMigrationInfo& MigrationInfo) override;
    virtual void SetAlignmentRequirement(uint32 Alignment) override;
    virtual void SetMemoryUsageHint(EPoolMemoryUsage UsageHint) override;
    virtual void SetNumaNode(int32 NodeId) override;
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
     * Configures memory layout optimization for a specific node type
     * @param TypeId ID of the node type to configure
     * @param bUseZOrderCurve Whether to use Z-order curve for spatial indexing
     * @param bEnablePrefetching Whether to enable memory prefetching
     * @param AccessPattern Expected access pattern for this type
     * @return True if configuration was successful
     */
    bool ConfigureTypeLayout(uint32 TypeId, bool bUseZOrderCurve, bool bEnablePrefetching, EMemoryAccessPattern AccessPattern);

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
    void UpdateStats() const;

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
    
    /** Structure for node type memory layout configuration */
    struct FNodeTypeLayout
    {
        /** Type ID */
        uint32 TypeId;
        
        /** Whether to use Z-order curve for spatial indexing */
        bool bUseZOrderCurve;
        
        /** Whether to enable memory prefetching */
        bool bEnablePrefetching;
        
        /** Expected access pattern for this type */
        EMemoryAccessPattern AccessPattern;
        
        FNodeTypeLayout()
            : TypeId(0)
            , bUseZOrderCurve(true)
            , bEnablePrefetching(true)
            , AccessPattern(EMemoryAccessPattern::OctreeTraversal)
        {
        }
    };
    
    /** Map of node types to their layout configurations */
    TMap<uint32, FNodeTypeLayout> NodeTypeLayouts;
    
    /** Recently accessed node indices for prefetching optimization */
    TArray<uint32> RecentlyAccessedNodes;
    
    /** Maximum number of recent nodes to track */
    static constexpr uint32 MaxRecentNodeCount = 64;
};