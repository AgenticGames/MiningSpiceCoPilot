// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IPoolAllocator.h"
#include "Interfaces/IMemoryManager.h"
#include "Math/Vector.h"
#include "Containers/RingBuffer.h"
#include "CompressionUtility.h" // Include for EMaterialCompressionLevel

/**
 * Enum defining supported SIMD instruction sets for memory layout optimization
 */
enum class ESIMDInstructionSet : uint8
{
    None = 0,    // No SIMD support
    SSE = 1,     // SSE instructions (128-bit)
    AVX = 2,     // AVX instructions (256-bit)
    AVX2 = 3,    // AVX2 instructions (256-bit with enhanced integer)
    Neon = 4     // ARM Neon instructions (128-bit)
};

/**
 * Specialized allocator for narrow-band distance field data
 * Provides optimized memory management for multi-channel distance field operations
 * with cache-coherent layout and SIMD optimization for SVO+SDF hybrid mining system
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
    virtual bool Reset() override;
    virtual bool MoveNextFragmentedAllocation(void*& OutOldPtr, void*& OutNewPtr, uint64& OutSize) override;
    virtual bool UpdateTypeVersion(const FTypeVersionMigrationInfo& MigrationInfo) override;
    virtual void SetAlignmentRequirement(uint32 Alignment) override;
    virtual void SetMemoryUsageHint(EPoolMemoryUsage UsageHint) override;
    virtual void SetNumaNode(int32 NodeId) override;
    //~ End IPoolAllocator Interface

    /**
     * Sets the precision tier for this allocator
     * Affects memory layout and representation of distance field values
     * @param NewTier Precision tier to use (Hot=high, Warm=medium, Cold=low, Archive=compressed)
     */
    void SetPrecisionTier(EMemoryTier NewTier);

    /**
     * Gets the current precision tier
     * @return Current precision tier
     */
    EMemoryTier GetPrecisionTier() const;

    /**
     * Sets the number of material channels per element
     * @param NewChannelCount Number of material channels
     */
    void SetChannelCount(uint32 NewChannelCount);

    /**
     * Gets the current number of channels per element
     * @return Number of material channels
     */
    uint32 GetChannelCount() const;

    /**
     * Sets the current mining direction for prefetching optimization
     * @param Direction Current mining direction vector
     */
    void SetMiningDirection(const FVector& Direction);

    /**
     * Gets the current mining direction
     * @return Current mining direction vector
     */
    FVector GetMiningDirection() const;

    /**
     * Sets the position for a block allocation to enable spatial optimization
     * @param Ptr Pointer to allocated block
     * @param Position 3D position for this block in world space
     * @return True if position was successfully set
     */
    bool SetBlockPosition(void* Ptr, const FVector& Position);

    /**
     * Sets the distance from surface for a block allocation (for narrow band optimization)
     * @param Ptr Pointer to allocated block
     * @param Distance Distance from the material surface (0 = directly on surface)
     * @return True if distance was successfully set
     */
    bool SetDistanceFromSurface(void* Ptr, float Distance);

    /**
     * Packs blocks to optimize for spatial locality based on position data
     * More expensive than standard defragmentation but improves cache coherence
     * @param MaxTimeMs Maximum time to spend packing in milliseconds
     * @return True if packing was performed
     */
    bool PackBlocksByPosition(float MaxTimeMs = 10.0f);

    /**
     * Optimizes memory layout based on distance from surface for narrow band efficiency
     * @param MaxTimeMs Maximum time to spend reorganizing in milliseconds
     * @return True if optimization was performed
     */
    bool OptimizeNarrowBand(float MaxTimeMs = 10.0f);

    /**
     * Allocates memory for material channels
     * @param MaterialTypeId ID of the material type
     * @param InChannelCount Number of channels to allocate
     * @param Tier Memory tier for allocation
     * @param CompressionLevel Compression level to use
     * @return Channel ID on success, -1 on failure
     */
    int32 AllocateChannelMemory(uint32 MaterialTypeId, uint32 InChannelCount, EMemoryTier Tier, EMaterialCompressionLevel CompressionLevel);
    
    /**
     * Configures SIMD-optimized memory layout for material field operations
     * @param MaterialTypeId ID of the material type
     * @param FieldAlignment Alignment for SIMD operations (16/32/64 bytes)
     * @param bEnableVectorization Whether to enable vectorized access patterns
     * @param SIMDOperationType Type of SIMD instructions to optimize for (SSE, AVX, AVX2, Neon)
     * @return True if SIMD layout was successfully configured
     */
    bool ConfigureSIMDLayout(uint32 MaterialTypeId, uint32 FieldAlignment, bool bEnableVectorization, ESIMDInstructionSet SIMDOperationType);

    /**
     * Sets up shared channels between related materials
     * @param ChildTypeId Child material type ID
     * @param ParentTypeId Parent material type ID
     * @param ChildChannelId Child material channel ID
     * @param ParentChannelId Parent material channel ID
     * @return True if channels were successfully shared
     */
    bool SetupSharedChannels(uint32 ChildTypeId, uint32 ParentTypeId, int32 ChildChannelId, int32 ParentChannelId);

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
    void UpdateStats() const;

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
    
    /**
     * Prefetches blocks that are likely to be accessed based on mining direction
     * and recent access patterns
     */
    void PrefetchLikelyBlocks();
    
    /**
     * Maps 3D position to Z-order curve index for better cache coherence
     * @param Position 3D position to convert
     * @param GridSize Size of the grid cell for normalization
     * @return Z-order curve index
     */
    uint64 PositionToZOrder(const FVector& Position, float GridSize = 4.0f) const;
    
    /** Structure containing metadata for tracking memory block allocations */
    struct FBlockMetadata
    {
        /** Whether this block is currently allocated */
        bool bAllocated;
        
        /** Allocation tag for tracking */
        FName AllocationTag;
        
        /** Object that requested this allocation */
        TWeakObjectPtr<const UObject> RequestingObject;
        
        /** Position this block is associated with (for spatial prefetching) */
        FVector Position;
        
        /** Time when this block was allocated */
        double AllocationTime;

        /** Distance from the surface (0 = directly on surface) */
        float DistanceFromSurface;
        
        /** Constructor */
        FBlockMetadata()
            : bAllocated(false)
            , AllocationTag(NAME_None)
            , RequestingObject(nullptr)
            , Position(FVector::ZeroVector)
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
    
    /** Precision tier for this narrow band */
    EMemoryTier PrecisionTier;
    
    /** Number of material channels per element */
    uint32 ChannelCount;
    
    /** Whether the stats need to be recalculated */
    mutable bool bStatsDirty;
    
    /** Cached pool statistics */
    mutable FPoolStats CachedStats;
    
    /** Recent block access patterns for prefetching */
    TRingBuffer<uint32> RecentAccessPattern;

    /** Last known mining direction for prefetching */
    FVector LastMiningDirection;

    /** Grid size for Z-order mapping */
    float ZOrderGridSize;

    /** Prefetch distance in blocks */
    uint32 PrefetchDistance;

    /** Counter for tracking consecutive allocations */
    uint64 AllocationCounter;
    
    /** SIMD instruction set in use for memory layout optimization */
    ESIMDInstructionSet SIMDInstructions;
    
    /** Map of material type IDs to their configured field alignments */
    TMap<uint32, uint32> MaterialFieldAlignments;
    
    /** Map of material types to vectorization flags */
    TMap<uint32, bool> MaterialVectorizationEnabled;
    
    /** Structure for SIMD field layout configuration */
    struct FSIMDFieldLayout
    {
        /** Material type ID */
        uint32 MaterialTypeId;
        
        /** Field alignment in bytes */
        uint32 FieldAlignment;
        
        /** Whether vectorization is enabled */
        bool bVectorizationEnabled;
        
        /** SIMD instruction set to use */
        ESIMDInstructionSet InstructionSet;
        
        FSIMDFieldLayout()
            : MaterialTypeId(0)
            , FieldAlignment(16)
            , bVectorizationEnabled(false)
            , InstructionSet(ESIMDInstructionSet::None)
        {
        }
    };
    
    /** Map of material type IDs to their SIMD field layouts */
    TMap<uint32, FSIMDFieldLayout> SIMDFieldLayouts;
};