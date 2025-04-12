// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "2_MemoryManagement/Public/Interfaces/IPoolAllocator.h"
#include "2_MemoryManagement/Public/Interfaces/IMemoryManager.h"
#include "Math/SIMDFloat.h"

/**
 * Specialized allocator for narrow-band distance field data
 * Optimized for SDF operations with precision tier-based memory management
 */
class MININGSPICECOPILOT_API FNarrowBandAllocator : public IPoolAllocator
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
    FNarrowBandAllocator(
        const FName& InPoolName,
        uint32 InBlockSize, 
        uint32 InBlockCount,
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
    //~ End IPoolAllocator Interface

    /**
     * Sets the precision tier for the narrow band
     * @param InTier Memory tier for precision control
     */
    void SetPrecisionTier(EMemoryTier InTier);

    /**
     * Gets the current precision tier
     * @return Current precision tier
     */
    EMemoryTier GetPrecisionTier() const;

    /**
     * Sets the number of material channels
     * @param InChannelCount Number of material channels
     * @return True if successful
     */
    bool SetMaterialChannelCount(uint32 InChannelCount);

    /**
     * Gets the number of material channels
     * @return Number of material channels
     */
    uint32 GetMaterialChannelCount() const;

    /**
     * Allocates a block within the narrow band at a specific position
     * @param GridX X position in the distance field grid
     * @param GridY Y position in the distance field grid
     * @param GridZ Z position in the distance field grid
     * @param RequestingObject Optional object for tracking
     * @param AllocationTag Optional tag for tracking
     * @return Pointer to the allocated memory or nullptr if allocation failed
     */
    void* AllocateAtPosition(int32 GridX, int32 GridY, int32 GridZ,
        const UObject* RequestingObject = nullptr, FName AllocationTag = NAME_None);

    /**
     * Preallocates memory for a narrow band around a surface
     * @param CenterX Center X position in the grid
     * @param CenterY Center Y position in the grid
     * @param CenterZ Center Z position in the grid
     * @param BandWidth Width of the narrow band in grid cells
     * @return Number of blocks preallocated
     */
    uint32 PreallocateNarrowBand(int32 CenterX, int32 CenterY, int32 CenterZ, int32 BandWidth);

    /**
     * Gets the best SIMD alignment for the current configuration
     * @return Optimal alignment for SIMD operations
     */
    uint32 GetSIMDAlignment() const;

    /**
     * Checks if hardware supports optimal SIMD instructions for this allocator
     * @return True if optimal SIMD instructions are supported
     */
    bool HasOptimalSIMDSupport() const;

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
    
    /** Current precision tier */
    EMemoryTier PrecisionTier;
    
    /** Number of material channels */
    uint32 MaterialChannelCount;
    
    /** Alignment for SIMD operations */
    uint32 SIMDAlignment;
    
    /** Block metadata */
    struct FBlockMetadata
    {
        FName AllocationTag;
        TWeakObjectPtr<const UObject> RequestingObject;
        int32 GridX;
        int32 GridY;
        int32 GridZ;
        bool bAllocated;
        double AllocationTime;
        uint32 MaterialMask; // Bit mask of active materials in this block
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
    
    /** Calculate optimal SIMD alignment based on precision tier and material count */
    void UpdateSIMDAlignment();
    
    /** Calculate element size based on precision tier */
    uint32 CalculateElementSize() const;
};
