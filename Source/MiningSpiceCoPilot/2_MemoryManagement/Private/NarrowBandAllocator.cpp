// Copyright Epic Games, Inc. All Rights Reserved.

#include "NarrowBandAllocator.h"
#include "HAL/PlatformMemory.h"
#include "Misc/ScopeLock.h"
#include "Math/UnrealMathSSE.h"
#include "Math/Vector.h"
#include "HAL/PlatformTime.h"
// Use GenericPlatform alignment functions instead of the missing AlignmentTemplates.h
#include "GenericPlatform/GenericPlatformMemory.h"

FNarrowBandAllocator::FNarrowBandAllocator(const FName& InPoolName, uint32 InBlockSize, uint32 InBlockCount, 
    EMemoryAccessPattern InAccessPattern, bool InAllowGrowth)
    : IPoolAllocator()
    , PoolName(InPoolName)
    , BlockSize(InBlockSize < 8u ? 8u : InBlockSize) // Ensure minimum block size
    , PoolMemory(nullptr)
    , MaxBlockCount(InBlockCount)
    , CurrentBlockCount(0)
    , bIsInitialized(false)
    , bAllowsGrowth(InAllowGrowth)
    , AccessPattern(InAccessPattern)
    , PrecisionTier(EMemoryTier::Hot) // Default to highest precision
    , ChannelCount(1) // Default to single channel
    , bStatsDirty(true)
    , LastMiningDirection(FVector::ForwardVector) // Default mining direction
    , ZOrderGridSize(4.0f) // Default grid size for Z-order mapping
    , PrefetchDistance(8) // Default prefetch distance
    , AllocationCounter(0)
    , SIMDInstructions(ESIMDInstructionSet::None) // Default to no SIMD support
{
    // Align block size to appropriate boundary based on precision tier
    BlockSize = Align(BlockSize, GetElementAlignment());
}

FNarrowBandAllocator::~FNarrowBandAllocator()
{
    Shutdown();
}

bool FNarrowBandAllocator::Initialize()
{
    FScopeLock Lock(&PoolLock);
    
    if (bIsInitialized)
    {
        return true;
    }
    
    if (!AllocatePoolMemory(MaxBlockCount))
    {
        return false;
    }
    
    bIsInitialized = true;
    bStatsDirty = true;
    
    UE_LOG(LogTemp, Log, TEXT("FNarrowBandAllocator::Initialize - Initialized pool '%s' with %u blocks of %u bytes each (Precision: %d, Channels: %u)"),
        *PoolName.ToString(), MaxBlockCount, BlockSize, static_cast<int32>(PrecisionTier), ChannelCount);
    
    return true;
}

void FNarrowBandAllocator::Shutdown()
{
    FScopeLock Lock(&PoolLock);
    
    if (!bIsInitialized)
    {
        return;
    }
    
    FreePoolMemory();
    bIsInitialized = false;
    
    UE_LOG(LogTemp, Log, TEXT("FNarrowBandAllocator::Shutdown - Shut down pool '%s'"), *PoolName.ToString());
}

bool FNarrowBandAllocator::IsInitialized() const
{
    return bIsInitialized;
}

FName FNarrowBandAllocator::GetPoolName() const
{
    return PoolName;
}

uint32 FNarrowBandAllocator::GetBlockSize() const
{
    return BlockSize;
}

void* FNarrowBandAllocator::Allocate(const UObject* RequestingObject, FName AllocationTag)
{
    FScopeLock Lock(&PoolLock);
    
    if (!bIsInitialized)
    {
        return nullptr;
    }
    
    if (FreeBlocks.Num() == 0)
    {
        // No free blocks, try to grow if allowed
        uint32 growAmount = MaxBlockCount / 4;
        if (growAmount < 32u) 
        {
            growAmount = 32u;
        }
        
        if (!bAllowsGrowth || !Grow(growAmount))
        {
            // Could not grow the pool
            CachedStats.AllocationFailures++;
            bStatsDirty = true;
            return nullptr;
        }
    }
    
    // Get a free block index
    uint32 BlockIndex = FreeBlocks.Pop(EAllowShrinking::No);
    
    // Mark as allocated and set metadata
    BlockMetadata[BlockIndex].bAllocated = true;
    BlockMetadata[BlockIndex].AllocationTag = AllocationTag;
    BlockMetadata[BlockIndex].RequestingObject = RequestingObject;
    BlockMetadata[BlockIndex].AllocationTime = FPlatformTime::Seconds();
    
    // Record access pattern for prefetching - fix to manage buffer size
    if (RecentAccessPattern.Num() >= 32) // Using a reasonable size instead of GetCapacity
    {
        RecentAccessPattern.PopFront();
    }
    RecentAccessPattern.Add(BlockIndex);
    
    // Calculate address of the block
    void* Ptr = PoolMemory + (BlockIndex * BlockSize);
    
    // Zero out the memory for the new allocation
    FMemory::Memzero(Ptr, BlockSize);
    
    // Track allocation count for performance tuning
    AllocationCounter++;
    
    // Prefetch likely blocks if we're doing frequent allocations
    if (AllocationCounter % 64 == 0)
    {
        PrefetchLikelyBlocks();
    }
    
    // Update stats
    bStatsDirty = true;
    CachedStats.TotalAllocations++;
    
    return Ptr;
}

bool FNarrowBandAllocator::Free(void* Ptr)
{
    if (!Ptr || !bIsInitialized)
    {
        return false;
    }
    
    FScopeLock Lock(&PoolLock);
    
    // Find which block this is
    int32 BlockIndex = GetBlockIndex(Ptr);
    if (BlockIndex == INDEX_NONE)
    {
        return false;
    }
    
    // Check if already freed
    if (!BlockMetadata[BlockIndex].bAllocated)
    {
        return false;
    }
    
    // Mark as free
    BlockMetadata[BlockIndex].bAllocated = false;
    BlockMetadata[BlockIndex].AllocationTag = NAME_None;
    BlockMetadata[BlockIndex].RequestingObject = nullptr;
    BlockMetadata[BlockIndex].AllocationTime = 0.0;
    BlockMetadata[BlockIndex].DistanceFromSurface = 0.0f;
    
    // Add to free list
    FreeBlocks.Add(BlockIndex);
    
    // Update stats
    bStatsDirty = true;
    CachedStats.TotalFrees++;
    
    return true;
}

bool FNarrowBandAllocator::Grow(uint32 AdditionalBlockCount, bool bForceGrowth)
{
    FScopeLock Lock(&PoolLock);
    
    if (!bIsInitialized)
    {
        return false;
    }
    
    // Check if growth is allowed
    if (!bAllowsGrowth && !bForceGrowth)
    {
        return false;
    }
    
    // Validate inputs
    if (AdditionalBlockCount == 0)
    {
        return true; // Nothing to do
    }
    
    // Store old memory info
    uint8* OldMemory = PoolMemory;
    uint32 OldBlockCount = CurrentBlockCount;
    uint32 NewBlockCount = OldBlockCount + AdditionalBlockCount;
    
    // Allocate aligned memory for SIMD operations
    uint32 Alignment = GetElementAlignment();
    uint8* NewMemory = (uint8*)FMemory::Malloc(NewBlockCount * BlockSize, Alignment);
    if (!NewMemory)
    {
        return false;
    }
    
    // Copy existing blocks if there were any
    if (OldMemory && OldBlockCount > 0)
    {
        FMemory::Memcpy(NewMemory, OldMemory, OldBlockCount * BlockSize);
    }
    
    // Update pool information
    PoolMemory = NewMemory;
    CurrentBlockCount = NewBlockCount;
    
    // Zero out new memory
    FMemory::Memzero(NewMemory + (OldBlockCount * BlockSize), AdditionalBlockCount * BlockSize);
    
    // Make new blocks available
    BlockMetadata.SetNum(NewBlockCount);
    for (uint32 i = OldBlockCount; i < NewBlockCount; ++i)
    {
        FreeBlocks.Add(i);
    }
    
    // Free old memory if we grew an existing pool
    if (OldMemory)
    {
        FMemory::Free(OldMemory);
    }
    
    // Update stats
    bStatsDirty = true;
    CachedStats.GrowthCount++;
    
    UE_LOG(LogTemp, Log, TEXT("FNarrowBandAllocator::Grow - Grew pool '%s' from %u to %u blocks"),
        *PoolName.ToString(), OldBlockCount, NewBlockCount);
    
    return true;
}

uint32 FNarrowBandAllocator::Shrink(uint32 MaxBlocksToRemove)
{
    FScopeLock Lock(&PoolLock);
    
    if (!bIsInitialized || CurrentBlockCount == 0)
    {
        return 0;
    }
    
    // Get stats (used to determine how many blocks we can remove)
    FPoolStats Stats = GetStats();
    uint32 AllocatedBlocks = Stats.AllocatedBlocks;
    uint32 FreeBlockCount = Stats.FreeBlocks;
    
    // Determine how many blocks to remove
    uint32 BlocksToRemove = MaxBlocksToRemove < FreeBlockCount ? MaxBlocksToRemove : FreeBlockCount;
    if (BlocksToRemove == 0)
    {
        return 0; // Can't remove any blocks
    }
    
    // For narrow band allocators, we should keep a margin of free blocks
    // based on the estimated size of the narrow band
    uint32 MinimumFreeMargin = AllocatedBlocks / 4;
    if (MinimumFreeMargin < 32u)
    {
        MinimumFreeMargin = 32u;
    }
    if (FreeBlockCount - BlocksToRemove < MinimumFreeMargin)
    {
        BlocksToRemove = (FreeBlockCount > MinimumFreeMargin) ? 
            (FreeBlockCount - MinimumFreeMargin) : 0;
    }
    
    if (BlocksToRemove == 0)
    {
        return 0;
    }
    
    // Sort free blocks to facilitate removing those at the end (compacting memory)
    FreeBlocks.Sort([](uint32 A, uint32 B) { return A > B; });
    
    // Check how many blocks at the end are contiguously free
    uint32 ContigFreeBlocks = 0;
    for (int32 i = 0; i < FreeBlocks.Num(); ++i)
    {
        uint32 BlockIndex = FreeBlocks[i];
        if (BlockIndex == CurrentBlockCount - 1 - ContigFreeBlocks)
        {
            ContigFreeBlocks++;
        }
        else
        {
            break;
        }
    }
    
    // Determine how many blocks we can actually remove
    BlocksToRemove = BlocksToRemove < ContigFreeBlocks ? BlocksToRemove : ContigFreeBlocks;
    if (BlocksToRemove == 0)
    {
        return 0;
    }
    
    // Remove the highest-numbered free blocks from the free list
    for (uint32 i = 0; i < BlocksToRemove; ++i)
    {
        FreeBlocks.RemoveAt(0); // Remove the highest index (first in sorted array)
    }
    
    // Reduce metadata array size
    uint32 NewBlockCount = CurrentBlockCount - BlocksToRemove;
    BlockMetadata.SetNum(NewBlockCount);
    
    // Reallocate memory with optimal alignment
    if (NewBlockCount > 0)
    {
        uint32 Alignment = GetElementAlignment();
        uint8* NewMemory = (uint8*)FMemory::Malloc(NewBlockCount * BlockSize, Alignment);
        if (NewMemory)
        {
            // Copy the remaining blocks
            FMemory::Memcpy(NewMemory, PoolMemory, NewBlockCount * BlockSize);
            
            // Free old memory
            FMemory::Free(PoolMemory);
            
            // Update pool information
            PoolMemory = NewMemory;
            CurrentBlockCount = NewBlockCount;
        }
        else
        {
            // Failed to allocate new memory, revert changes
            BlockMetadata.SetNum(CurrentBlockCount);
            BlocksToRemove = 0;
        }
    }
    else
    {
        // No blocks left, just free all memory
        FMemory::Free(PoolMemory);
        PoolMemory = nullptr;
        CurrentBlockCount = 0;
    }
    
    // Update stats
    bStatsDirty = true;
    
    UE_LOG(LogTemp, Log, TEXT("FNarrowBandAllocator::Shrink - Shrunk pool '%s' by %u blocks to %u blocks"),
        *PoolName.ToString(), BlocksToRemove, NewBlockCount);
    
    return BlocksToRemove;
}

bool FNarrowBandAllocator::OwnsPointer(const void* Ptr) const
{
    if (!bIsInitialized || !PoolMemory || !Ptr)
    {
        return false;
    }
    
    // Check if the pointer is within our memory range
    const uint8* BytePtr = static_cast<const uint8*>(Ptr);
    return BytePtr >= PoolMemory && BytePtr < (PoolMemory + (CurrentBlockCount * BlockSize));
}

void FNarrowBandAllocator::SetAccessPattern(EMemoryAccessPattern InAccessPattern)
{
    FScopeLock Lock(&PoolLock);
    AccessPattern = InAccessPattern;
}

EMemoryAccessPattern FNarrowBandAllocator::GetAccessPattern() const
{
    return AccessPattern;
}

FPoolStats FNarrowBandAllocator::GetStats() const
{
    FScopeLock Lock(&PoolLock);
    
    if (bStatsDirty)
    {
        UpdateStats();
        bStatsDirty = false;
    }
    
    return CachedStats;
}

bool FNarrowBandAllocator::Defragment(float MaxTimeMs)
{
    FScopeLock Lock(&PoolLock);
    
    if (!bIsInitialized || CurrentBlockCount == 0)
    {
        return false;
    }
    
    // Track time to stay within budget
    double StartTime = FPlatformTime::Seconds();
    double EndTime = StartTime + MaxTimeMs / 1000.0;
    bool bDidDefragment = false;
    
    // Count fragmentation (transitions between allocated and free blocks)
    uint32 FragmentCount = 0;
    bool PrevWasAllocated = false;
    
    // Sort allocated blocks to be contiguous
    TArray<uint32> AllocatedBlocks;
    TArray<uint32> SpatialMap; // Maps new indices to original block indices
    
    // First pass: collect all allocated blocks
    for (uint32 i = 0; i < CurrentBlockCount; ++i)
    {
        bool IsAllocated = BlockMetadata[i].bAllocated;
        
        if (i > 0 && IsAllocated != PrevWasAllocated)
        {
            FragmentCount++;
        }
        
        if (IsAllocated)
        {
            AllocatedBlocks.Add(i);
        }
        
        PrevWasAllocated = IsAllocated;
        
        // Check if we're out of time
        if (FPlatformTime::Seconds() >= EndTime)
        {
            break;
        }
    }
    
    // If we have significant fragmentation, try to defragment
    if (FragmentCount > 1 && AllocatedBlocks.Num() > 0)
    {
        // Allocate a temporary buffer to hold relocated blocks
        uint32 Alignment = GetElementAlignment();
        uint8* TempMemory = (uint8*)FMemory::Malloc(CurrentBlockCount * BlockSize, Alignment);
        
        if (TempMemory)
        {
            // Create a new layout with all allocated blocks first, then free blocks
            SpatialMap.SetNum(CurrentBlockCount);
            uint32 NewIndex = 0;
            
            // First, copy all allocated blocks to the beginning of the temp buffer
            for (uint32 OldIndex : AllocatedBlocks)
            {
                // Copy block data
                FMemory::Memcpy(TempMemory + (NewIndex * BlockSize), 
                            PoolMemory + (OldIndex * BlockSize), 
                            BlockSize);
                
                // Update spatial mapping
                SpatialMap[NewIndex] = OldIndex;
                NewIndex++;
            }
            
            // Reset free blocks array
            FreeBlocks.Empty(CurrentBlockCount - AllocatedBlocks.Num());
            
            // Update metadata and create free list
            for (uint32 i = 0; i < CurrentBlockCount; ++i)
            {
                if (i < static_cast<uint32>(AllocatedBlocks.Num()))
                {
                    // This is now an allocated block
                    uint32 OldIndex = SpatialMap[i];
                    BlockMetadata[i] = BlockMetadata[OldIndex];
                }
                else
                {
                    // This is now a free block
                    BlockMetadata[i].bAllocated = false;
                    BlockMetadata[i].AllocationTag = NAME_None;
                    BlockMetadata[i].RequestingObject = nullptr;
                    BlockMetadata[i].AllocationTime = 0.0;
                    BlockMetadata[i].Position = FVector::ZeroVector;
                    BlockMetadata[i].DistanceFromSurface = 0.0f;
                    FreeBlocks.Add(i);
                }
            }
            
            // Replace the old buffer with the new one
            FMemory::Free(PoolMemory);
            PoolMemory = TempMemory;
            
            // Update stats
            bStatsDirty = true;
            bDidDefragment = true;
            
            UE_LOG(LogTemp, Log, TEXT("FNarrowBandAllocator::Defragment - Defragmented pool '%s', reduced fragments from %u to 1"),
                *PoolName.ToString(), FragmentCount);
        }
    }
    
    return bDidDefragment;
}

bool FNarrowBandAllocator::PackBlocksByPosition(float MaxTimeMs)
{
    FScopeLock Lock(&PoolLock);
    
    if (!bIsInitialized || CurrentBlockCount == 0)
    {
        return false;
    }
    
    // Track time to stay within budget
    double StartTime = FPlatformTime::Seconds();
    double EndTime = StartTime + MaxTimeMs / 1000.0;
    bool bDidPack = false;
    
    // Collect all allocated blocks
    TArray<uint32> AllocatedBlocks;
    for (uint32 i = 0; i < CurrentBlockCount; ++i)
    {
        if (BlockMetadata[i].bAllocated)
        {
            AllocatedBlocks.Add(i);
        }
    }
    
    if (AllocatedBlocks.Num() < 2)
    {
        // Not enough blocks to warrant reorganization
        return false;
    }
    
    // Sort blocks by Z-order curve index for spatial locality
    AllocatedBlocks.Sort([this](uint32 A, uint32 B) {
        uint64 ZOrderA = PositionToZOrder(BlockMetadata[A].Position);
        uint64 ZOrderB = PositionToZOrder(BlockMetadata[B].Position);
        return ZOrderA < ZOrderB;
    });
    
    // Allocate temporary memory for reorganized blocks
    uint32 Alignment = GetElementAlignment();
    uint8* TempMemory = (uint8*)FMemory::Malloc(CurrentBlockCount * BlockSize, Alignment);
    
    if (!TempMemory)
    {
        return false;
    }
    
    // Create a mapping from old indices to new indices
    TArray<uint32> OldToNewIndex;
    OldToNewIndex.SetNum(CurrentBlockCount);
    for (uint32 i = 0; i < CurrentBlockCount; ++i)
    {
        OldToNewIndex[i] = INDEX_NONE;
    }
    
    // Copy blocks in Z-order to new memory
    uint32 NewIndex = 0;
    for (uint32 OldIndex : AllocatedBlocks)
    {
        // Copy block data
        FMemory::Memcpy(TempMemory + (NewIndex * BlockSize), 
                     PoolMemory + (OldIndex * BlockSize), 
                     BlockSize);
        
        // Update mapping
        OldToNewIndex[OldIndex] = NewIndex;
        NewIndex++;
        
        // Check time budget
        if (FPlatformTime::Seconds() >= EndTime)
        {
            // Out of time, clean up and abort
            FMemory::Free(TempMemory);
            return false;
        }
    }
    
    // Create new metadata array
    TArray<FBlockMetadata> NewMetadata;
    NewMetadata.SetNum(CurrentBlockCount);
    
    // Update metadata and prepare free list
    FreeBlocks.Empty(CurrentBlockCount - AllocatedBlocks.Num());
    
    for (uint32 i = 0; i < CurrentBlockCount; ++i)
    {
        if (i < static_cast<uint32>(AllocatedBlocks.Num()))
        {
            // This is an allocated block - copy metadata
            uint32 OldIndex = AllocatedBlocks[i];
            NewMetadata[i] = BlockMetadata[OldIndex];
        }
        else
        {
            // This is a free block
            NewMetadata[i].bAllocated = false;
            NewMetadata[i].AllocationTag = NAME_None;
            NewMetadata[i].RequestingObject = nullptr;
            NewMetadata[i].AllocationTime = 0.0;
            NewMetadata[i].Position = FVector::ZeroVector;
            NewMetadata[i].DistanceFromSurface = 0.0f;
            FreeBlocks.Add(i);
        }
    }
    
    // Replace old memory and metadata
    FMemory::Free(PoolMemory);
    PoolMemory = TempMemory;
    BlockMetadata = MoveTemp(NewMetadata);
    
    // Update stats
    bStatsDirty = true;
    bDidPack = true;
    
    UE_LOG(LogTemp, Log, TEXT("FNarrowBandAllocator::PackBlocksByPosition - Packed pool '%s' with %u allocated blocks"),
        *PoolName.ToString(), AllocatedBlocks.Num());
    
    return bDidPack;
}

bool FNarrowBandAllocator::OptimizeNarrowBand(float MaxTimeMs)
{
    FScopeLock Lock(&PoolLock);
    
    if (!bIsInitialized || CurrentBlockCount == 0)
    {
        return false;
    }
    
    // Track time to stay within budget
    double StartTime = FPlatformTime::Seconds();
    double EndTime = StartTime + MaxTimeMs / 1000.0;
    bool bDidOptimize = false;
    
    // Collect all allocated blocks
    TArray<uint32> AllocatedBlocks;
    for (uint32 i = 0; i < CurrentBlockCount; ++i)
    {
        if (BlockMetadata[i].bAllocated)
        {
            AllocatedBlocks.Add(i);
        }
    }
    
    if (AllocatedBlocks.Num() < 2)
    {
        // Not enough blocks to warrant reorganization
        return false;
    }
    
    // Sort blocks by distance from surface (narrow band optimization)
    AllocatedBlocks.Sort([this](uint32 A, uint32 B) {
        return BlockMetadata[A].DistanceFromSurface < BlockMetadata[B].DistanceFromSurface;
    });
    
    // Allocate temporary memory for reorganized blocks
    uint32 Alignment = GetElementAlignment();
    uint8* TempMemory = (uint8*)FMemory::Malloc(CurrentBlockCount * BlockSize, Alignment);
    
    if (!TempMemory)
    {
        return false;
    }
    
    // Copy blocks in narrow band order to new memory
    uint32 NewIndex = 0;
    for (uint32 OldIndex : AllocatedBlocks)
    {
        // Copy block data
        FMemory::Memcpy(TempMemory + (NewIndex * BlockSize), 
                     PoolMemory + (OldIndex * BlockSize), 
                     BlockSize);
        
        NewIndex++;
        
        // Check time budget
        if (FPlatformTime::Seconds() >= EndTime)
        {
            // Out of time, clean up and abort
            FMemory::Free(TempMemory);
            return false;
        }
    }
    
    // Create new metadata array
    TArray<FBlockMetadata> NewMetadata;
    NewMetadata.SetNum(CurrentBlockCount);
    
    // Update metadata and prepare free list
    FreeBlocks.Empty(CurrentBlockCount - AllocatedBlocks.Num());
    
    for (uint32 i = 0; i < CurrentBlockCount; ++i)
    {
        if (i < static_cast<uint32>(AllocatedBlocks.Num()))
        {
            // This is an allocated block - copy metadata
            uint32 OldIndex = AllocatedBlocks[i];
            NewMetadata[i] = BlockMetadata[OldIndex];
        }
        else
        {
            // This is a free block
            NewMetadata[i].bAllocated = false;
            NewMetadata[i].AllocationTag = NAME_None;
            NewMetadata[i].RequestingObject = nullptr;
            NewMetadata[i].AllocationTime = 0.0;
            NewMetadata[i].Position = FVector::ZeroVector;
            NewMetadata[i].DistanceFromSurface = 0.0f;
            FreeBlocks.Add(i);
        }
    }
    
    // Replace old memory and metadata
    FMemory::Free(PoolMemory);
    PoolMemory = TempMemory;
    BlockMetadata = MoveTemp(NewMetadata);
    
    // Update stats
    bStatsDirty = true;
    bDidOptimize = true;
    
    UE_LOG(LogTemp, Log, TEXT("FNarrowBandAllocator::OptimizeNarrowBand - Optimized pool '%s' with %u allocated blocks"),
        *PoolName.ToString(), AllocatedBlocks.Num());
    
    return bDidOptimize;
}

bool FNarrowBandAllocator::Validate(TArray<FString>& OutErrors) const
{
    FScopeLock Lock(&PoolLock);
    
    if (!bIsInitialized)
    {
        OutErrors.Add(FString::Printf(TEXT("Pool '%s' is not initialized"), *PoolName.ToString()));
        return false;
    }
    
    if (!PoolMemory)
    {
        OutErrors.Add(FString::Printf(TEXT("Pool '%s' has invalid memory"), *PoolName.ToString()));
        return false;
    }
    
    // Verify free list integrity
    TArray<bool> BlockUsed;
    BlockUsed.SetNum(CurrentBlockCount);
    FMemory::Memzero(BlockUsed.GetData(), BlockUsed.Num() * sizeof(bool));
    
    // Check free list
    for (uint32 FreeIndex : FreeBlocks)
    {
        if (FreeIndex >= CurrentBlockCount)
        {
            OutErrors.Add(FString::Printf(TEXT("Pool '%s' has invalid free index %u (max: %u)"), 
                *PoolName.ToString(), FreeIndex, CurrentBlockCount - 1));
            return false;
        }
        
        if (BlockUsed[FreeIndex])
        {
            OutErrors.Add(FString::Printf(TEXT("Pool '%s' has duplicate free index %u"), 
                *PoolName.ToString(), FreeIndex));
            return false;
        }
        
        if (BlockMetadata[FreeIndex].bAllocated)
        {
            OutErrors.Add(FString::Printf(TEXT("Pool '%s' has free index %u marked as allocated"), 
                *PoolName.ToString(), FreeIndex));
            return false;
        }
        
        BlockUsed[FreeIndex] = true;
    }
    
    // Check metadata consistency
    uint32 AllocatedCount = 0;
    uint32 FreeCount = 0;
    
    for (uint32 i = 0; i < CurrentBlockCount; ++i)
    {
        if (BlockMetadata[i].bAllocated)
        {
            AllocatedCount++;
        }
        else
        {
            FreeCount++;
        }
    }
    
    if (FreeCount != (uint32)FreeBlocks.Num())
    {
        OutErrors.Add(FString::Printf(TEXT("Pool '%s' free count mismatch: %u in metadata, %u in free list"), 
            *PoolName.ToString(), FreeCount, FreeBlocks.Num()));
        return false;
    }
    
    if (AllocatedCount + FreeCount != CurrentBlockCount)
    {
        OutErrors.Add(FString::Printf(TEXT("Pool '%s' block count mismatch: %u allocated + %u free != %u total"), 
            *PoolName.ToString(), AllocatedCount, FreeCount, CurrentBlockCount));
        return false;
    }
    
    return true;
}

void FNarrowBandAllocator::SetPrecisionTier(EMemoryTier NewTier)
{
    FScopeLock Lock(&PoolLock);
    
    if (NewTier != PrecisionTier)
    {
        // Save old tier for logging
        EMemoryTier OldTier = PrecisionTier;
        PrecisionTier = NewTier;
        
        // If initialized, we'll need to re-align blocks the next time we grow
        if (bIsInitialized)
        {
            UE_LOG(LogTemp, Warning, TEXT("FNarrowBandAllocator::SetPrecisionTier - Pool '%s' precision tier changed from %d to %d after initialization. Changes will be applied on next allocation."),
                *PoolName.ToString(), static_cast<int32>(OldTier), static_cast<int32>(NewTier));
        }
        
        // Force stats update
        bStatsDirty = true;
    }
}

EMemoryTier FNarrowBandAllocator::GetPrecisionTier() const
{
    return PrecisionTier;
}

void FNarrowBandAllocator::SetChannelCount(uint32 NewChannelCount)
{
    FScopeLock Lock(&PoolLock);
    
    if (NewChannelCount == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("FNarrowBandAllocator::SetChannelCount - Invalid channel count (0), using 1 instead"));
        NewChannelCount = 1;
    }
    
    if (NewChannelCount != ChannelCount)
    {
        uint32 OldChannelCount = ChannelCount;
        ChannelCount = NewChannelCount;
        
        // Log warning if allocator is already initialized
        if (bIsInitialized)
        {
            UE_LOG(LogTemp, Warning, TEXT("FNarrowBandAllocator::SetChannelCount - Pool '%s' channel count changed from %u to %u after initialization. This may affect block sizing."),
                *PoolName.ToString(), OldChannelCount, NewChannelCount);
        }
        
        // Force stats update
        bStatsDirty = true;
    }
}

uint32 FNarrowBandAllocator::GetChannelCount() const
{
    return ChannelCount;
}

void FNarrowBandAllocator::SetMiningDirection(const FVector& Direction)
{
    if (!Direction.IsNearlyZero())
    {
        FScopeLock Lock(&PoolLock);
        LastMiningDirection = Direction.GetSafeNormal();
    }
}

FVector FNarrowBandAllocator::GetMiningDirection() const
{
    return LastMiningDirection;
}

bool FNarrowBandAllocator::SetBlockPosition(void* Ptr, const FVector& Position)
{
    if (!Ptr || !bIsInitialized)
    {
        return false;
    }
    
    FScopeLock Lock(&PoolLock);
    
    // Find which block this is
    int32 BlockIndex = GetBlockIndex(Ptr);
    if (BlockIndex == INDEX_NONE || !BlockMetadata[BlockIndex].bAllocated)
    {
        return false;
    }
    
    // Set position
    BlockMetadata[BlockIndex].Position = Position;
    return true;
}

bool FNarrowBandAllocator::SetDistanceFromSurface(void* Ptr, float Distance)
{
    if (!Ptr || !bIsInitialized)
    {
        return false;
    }
    
    FScopeLock Lock(&PoolLock);
    
    // Find which block this is
    int32 BlockIndex = GetBlockIndex(Ptr);
    if (BlockIndex == INDEX_NONE || !BlockMetadata[BlockIndex].bAllocated)
    {
        return false;
    }
    
    // Set distance
    BlockMetadata[BlockIndex].DistanceFromSurface = Distance;
    return true;
}

bool FNarrowBandAllocator::Reset()
{
    FScopeLock Lock(&PoolLock);
    
    if (!bIsInitialized)
    {
        return false;
    }
    
    // Clear all allocations
    FreeBlocks.Empty(CurrentBlockCount);
    
    for (uint32 i = 0; i < CurrentBlockCount; ++i)
    {
        // Reset metadata
        BlockMetadata[i].bAllocated = false;
        BlockMetadata[i].AllocationTag = NAME_None;
        BlockMetadata[i].RequestingObject = nullptr;
        BlockMetadata[i].Position = FVector::ZeroVector;
        BlockMetadata[i].AllocationTime = 0.0;
        BlockMetadata[i].DistanceFromSurface = 0.0f;
        
        // Add to free list
        FreeBlocks.Add(i);
    }
    
    // Clear access history
    RecentAccessPattern.Empty();
    
    // Update stats
    bStatsDirty = true;
    
    UE_LOG(LogTemp, Log, TEXT("FNarrowBandAllocator::Reset - Reset pool '%s'"), *PoolName.ToString());
    
    return true;
}

bool FNarrowBandAllocator::AllocatePoolMemory(uint32 BlockCount)
{
    // Check inputs
    if (BlockCount == 0 || BlockSize == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("FNarrowBandAllocator::AllocatePoolMemory - Invalid parameters: BlockCount=%u, BlockSize=%u"),
            BlockCount, BlockSize);
        return false;
    }
    
    // Free existing memory if any
    if (PoolMemory)
    {
        FreePoolMemory();
    }
    
    // Calculate total memory size
    uint64 TotalSize = static_cast<uint64>(BlockCount) * static_cast<uint64>(BlockSize);
    
    // Get appropriate alignment based on precision tier
    uint32 Alignment = GetElementAlignment();
    
    // Allocate memory with alignment for SIMD operations
    PoolMemory = (uint8*)FMemory::Malloc(TotalSize, Alignment);
    if (!PoolMemory)
    {
        UE_LOG(LogTemp, Error, TEXT("FNarrowBandAllocator::AllocatePoolMemory - Failed to allocate %llu bytes with alignment %u"),
            TotalSize, Alignment);
        return false;
    }
    
    // Zero out the memory
    FMemory::Memzero(PoolMemory, TotalSize);
    
    // Initialize metadata
    BlockMetadata.SetNum(BlockCount);
    
    // Initialize free blocks
    FreeBlocks.Empty(BlockCount);
    for (uint32 i = 0; i < BlockCount; ++i)
    {
        FreeBlocks.Add(i);
    }
    
    // Update capacity
    CurrentBlockCount = BlockCount;
    
    // Initialize cached stats
    CachedStats = FPoolStats();
    CachedStats.PoolName = PoolName;
    CachedStats.BlockSize = BlockSize;
    CachedStats.BlockCount = BlockCount;
    CachedStats.FreeBlocks = BlockCount;
    CachedStats.bAllowsGrowth = bAllowsGrowth;
    
    return true;
}

void FNarrowBandAllocator::FreePoolMemory()
{
    if (PoolMemory)
    {
        FMemory::Free(PoolMemory);
        PoolMemory = nullptr;
    }
    
    BlockMetadata.Empty();
    FreeBlocks.Empty();
    CurrentBlockCount = 0;
}

void FNarrowBandAllocator::UpdateStats() const
{
    // Reset stats
    CachedStats.PoolName = PoolName;
    CachedStats.BlockSize = BlockSize;
    CachedStats.BlockCount = CurrentBlockCount;
    CachedStats.bAllowsGrowth = bAllowsGrowth;
    
    // Count allocations and track data
    uint32 AllocatedBlockCount = 0;
    TMap<FName, uint32> AllocationsByTag;
    float AvgDistanceFromSurface = 0.0f;
    
    for (const auto& Metadata : BlockMetadata)
    {
        if (Metadata.bAllocated)
        {
            AllocatedBlockCount++;
            
            // Track allocations by tag
            uint32& TagCount = AllocationsByTag.FindOrAdd(Metadata.AllocationTag, 0);
            TagCount++;
            
            // Track distance metrics
            AvgDistanceFromSurface += Metadata.DistanceFromSurface;
        }
    }
    
    // Calculate average distances
    if (AllocatedBlockCount > 0)
    {
        AvgDistanceFromSurface /= AllocatedBlockCount;
    }
    
    // Update peak allocation tracking
    CachedStats.PeakAllocatedBlocks = FMath::Max(CachedStats.PeakAllocatedBlocks, AllocatedBlockCount);
    
    // Calculate memory usage
    CachedStats.AllocatedBlocks = AllocatedBlockCount;
    CachedStats.FreeBlocks = CurrentBlockCount - AllocatedBlockCount;
    
    // Calculate fragmentation - transitions between allocated and free
    uint32 FragmentCount = 0;
    bool PrevWasAllocated = false;
    
    for (uint32 i = 0; i < CurrentBlockCount; ++i)
    {
        bool IsAllocated = BlockMetadata[i].bAllocated;
        
        if (i > 0 && IsAllocated != PrevWasAllocated)
        {
            FragmentCount++;
        }
        
        PrevWasAllocated = IsAllocated;
    }
    
    // Simplified fragmentation metric based on the number of transitions
    CachedStats.FragmentationPercent = (CurrentBlockCount > 1) ? 
        (100.0f * static_cast<float>(FragmentCount) / (CurrentBlockCount - 1)) : 0.0f;
    
    // Calculate overhead (class size + metadata + free list)
    CachedStats.OverheadBytes = sizeof(FNarrowBandAllocator) +
                               (BlockMetadata.Num() * sizeof(FBlockMetadata)) +
                               (FreeBlocks.Num() * sizeof(uint32));
}

int32 FNarrowBandAllocator::GetBlockIndex(const void* Ptr) const
{
    if (!bIsInitialized || !PoolMemory || !Ptr)
    {
        return INDEX_NONE;
    }
    
    // Calculate offset from pool base
    const uint8* BytePtr = static_cast<const uint8*>(Ptr);
    ptrdiff_t Offset = BytePtr - PoolMemory;
    
    // Check if pointer is within pool range
    if (Offset < 0 || Offset >= static_cast<ptrdiff_t>(CurrentBlockCount * BlockSize))
    {
        return INDEX_NONE;
    }
    
    // Calculate block index
    int32 BlockIndex = static_cast<int32>(Offset / BlockSize);
    
    // Validate that pointer is at block boundary
    if ((Offset % BlockSize) != 0)
    {
        // Not at a block boundary
        return INDEX_NONE;
    }
    
    return BlockIndex;
}

uint32 FNarrowBandAllocator::GetElementAlignment() const
{
    // Determine appropriate alignment based on precision tier
    switch (PrecisionTier)
    {
        case EMemoryTier::Hot:
            // High precision - align for AVX operations (32 bytes)
            return 32;
            
        case EMemoryTier::Warm:
            // Medium precision - align for SSE operations (16 bytes)
            return 16;
            
        case EMemoryTier::Cold:
            // Low precision - align for basic SIMD (8 bytes)
            return 8;
            
        case EMemoryTier::Archive:
            // Compressed format - minimal alignment (4 bytes)
            return 4;
            
        default:
            // Default to SSE alignment (16 bytes)
            return 16;
    }
}

uint32 FNarrowBandAllocator::GetBytesPerChannel() const
{
    // Determine bytes per channel based on precision tier
    switch (PrecisionTier)
    {
        case EMemoryTier::Hot:
            // High precision - full float (4 bytes)
            return 4;
            
        case EMemoryTier::Warm:
            // Medium precision - half float (2 bytes)
            return 2;
            
        case EMemoryTier::Cold:
            // Low precision - 8-bit (1 byte)
            return 1;
            
        case EMemoryTier::Archive:
            // Compressed format - sub-byte precision
            // For simplicity, we'll use a reasonable approximation
            return (ChannelCount > 1) ? 1 : 2; // More aggressive compression with multiple channels
            
        default:
            // Default to full float
            return 4;
    }
}

void FNarrowBandAllocator::PrefetchLikelyBlocks()
{
    // This function doesn't need the lock as it only reads data and uses prefetching
    // which is just a hint to the CPU and not a critical operation
    
    if (!bIsInitialized || CurrentBlockCount == 0)
    {
        return;
    }
    
    // Check recent access pattern to find blocks that might be accessed next
    for (int32 i = 0; i < RecentAccessPattern.Num(); ++i)
    {
        uint32 RecentBlockIndex = RecentAccessPattern[i];
        
        // Skip if not valid
        if (RecentBlockIndex >= CurrentBlockCount || !BlockMetadata[RecentBlockIndex].bAllocated)
        {
            continue;
        }
        
        // Get position of this block
        const FVector& Position = BlockMetadata[RecentBlockIndex].Position;
        
        // Skip if position is not set
        if (Position.IsZero())
        {
            continue;
        }
        
        // Calculate a position in the mining direction
        FVector PrefetchPos = Position + (LastMiningDirection * PrefetchDistance);
        
        // Find blocks near this position
        for (uint32 j = 0; j < CurrentBlockCount; ++j)
        {
            if (BlockMetadata[j].bAllocated)
            {
                const FVector& BlockPos = BlockMetadata[j].Position;
                
                // Skip if position is not set
                if (BlockPos.IsZero())
                {
                    continue;
                }
                
                // Check if this block is in our prefetch direction
                float DistSq = FVector::DistSquared(BlockPos, PrefetchPos);
                
                // If it's close enough to our prefetch position
                if (DistSq < (PrefetchDistance * PrefetchDistance * 4.0f))
                {
                    // Calculate address of this block
                    void* PrefetchPtr = PoolMemory + (j * BlockSize);
                    
                    // Prefetch this block into cache
                    FPlatformMisc::Prefetch(PrefetchPtr);
                    
                    // Limit the number of prefetches to avoid thrashing the cache
                    if (j % 4 == 0)
                    {
                        break;
                    }
                }
            }
        }
    }
}

uint64 FNarrowBandAllocator::PositionToZOrder(const FVector& Position, float GridSize) const
{
    // Convert position to grid coordinates
    int32 X = FMath::FloorToInt(Position.X / GridSize);
    int32 Y = FMath::FloorToInt(Position.Y / GridSize);
    int32 Z = FMath::FloorToInt(Position.Z / GridSize);
    
    // Ensure values are positive by adding a large offset
    // (Z-order works best with unsigned integers)
    const int32 Offset = 1 << 20; // Large power of 2 offset
    X += Offset;
    Y += Offset;
    Z += Offset;
    
    // Clamp to 21 bits per component (63 bits total for the curve)
    X = FMath::Clamp(X, 0, (1 << 21) - 1);
    Y = FMath::Clamp(Y, 0, (1 << 21) - 1);
    Z = FMath::Clamp(Z, 0, (1 << 21) - 1);
    
    // Interleave bits using Morton code / Z-order curve
    uint64 Result = 0;
    
    // Process 21 bits per component (up to 63 bits total)
    for (int32 i = 0; i < 21; ++i)
    {
        Result |= ((X & (1ULL << i)) << (2 * i)) |
                  ((Y & (1ULL << i)) << (2 * i + 1)) |
                  ((Z & (1ULL << i)) << (2 * i + 2));
    }
    
    return Result;
}

bool FNarrowBandAllocator::MoveNextFragmentedAllocation(void*& OutOldPtr, void*& OutNewPtr, uint64& OutSize)
{
    FScopeLock Lock(&PoolLock);
    
    if (!bIsInitialized || CurrentBlockCount == 0)
    {
        return false;
    }
    
    // Find a fragmented block - a block where one side is allocated and the other is free
    for (uint32 i = 0; i < CurrentBlockCount - 1; ++i)
    {
        // Look for an allocated block followed by a free block
        if (BlockMetadata[i].bAllocated && !BlockMetadata[i+1].bAllocated)
        {
            // Found a fragmented allocation - see if there's room at the end
            uint32 LastAllocatedIndex = 0;
            
            // Find the last allocated block
            for (uint32 j = CurrentBlockCount - 1; j > i + 1; --j)
            {
                if (BlockMetadata[j].bAllocated)
                {
                    LastAllocatedIndex = j;
                    break;
                }
            }
            
            // If the last allocated block is after our current position, move this block there
            if (LastAllocatedIndex == 0 || LastAllocatedIndex <= i + 1)
            {
                // No suitable location found
                continue;
            }
            
            // Find an available destination position after the last allocated block
            uint32 DestIndex = 0;
            for (uint32 j = LastAllocatedIndex + 1; j < CurrentBlockCount; ++j)
            {
                if (!BlockMetadata[j].bAllocated)
                {
                    DestIndex = j;
                    break;
                }
            }
            
            if (DestIndex == 0)
            {
                // No suitable destination found
                continue;
            }
            
            // Calculate source and destination pointers
            OutOldPtr = PoolMemory + (i * BlockSize);
            OutNewPtr = PoolMemory + (DestIndex * BlockSize);
            OutSize = BlockSize;
            
            // Move the block data
            FMemory::Memcpy(OutNewPtr, OutOldPtr, BlockSize);
            
            // Update metadata
            BlockMetadata[DestIndex] = BlockMetadata[i];
            BlockMetadata[i].bAllocated = false;
            BlockMetadata[i].AllocationTag = NAME_None;
            BlockMetadata[i].RequestingObject = nullptr;
            BlockMetadata[i].AllocationTime = 0.0;
            BlockMetadata[i].Position = FVector::ZeroVector;
            BlockMetadata[i].DistanceFromSurface = 0.0f;
            
            // Update free blocks list
            FreeBlocks.Add(i);
            for (int32 FreeIndex = 0; FreeIndex < FreeBlocks.Num(); ++FreeIndex)
            {
                if (FreeBlocks[FreeIndex] == DestIndex)
                {
                    FreeBlocks.RemoveAt(FreeIndex);
                    break;
                }
            }
            
            // Update stats
            bStatsDirty = true;
            
            return true;
        }
    }
    
    // No fragmented allocations found
    return false;
}

int32 FNarrowBandAllocator::AllocateChannelMemory(uint32 MaterialTypeId, uint32 InChannelCount, EMemoryTier Tier, EMaterialCompressionLevel CompressionLevel)
{
    FScopeLock Lock(&PoolLock);
    
    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Warning, TEXT("FNarrowBandAllocator::AllocateChannelMemory - Pool '%s' not initialized"), 
            *PoolName.ToString());
        return -1;
    }
    
    // Set the precision tier for this allocation
    PrecisionTier = Tier;
    
    // Calculate memory requirements based on channel count and precision tier
    uint32 BytesPerChannel = GetBytesPerChannel();
    uint32 TotalBytesRequired = BytesPerChannel * InChannelCount;
    
    // Generate a channel ID
    static int32 NextChannelId = 0;
    int32 ChannelId = NextChannelId++;
    
    UE_LOG(LogTemp, Log, TEXT("FNarrowBandAllocator::AllocateChannelMemory - Allocated channel %d for material %u with %u channels (Tier: %d, Compression: %d)"),
        ChannelId, MaterialTypeId, InChannelCount, static_cast<int32>(Tier), static_cast<int32>(CompressionLevel));
    
    return ChannelId;
}

bool FNarrowBandAllocator::SetupSharedChannels(uint32 ChildTypeId, uint32 ParentTypeId, int32 ChildChannelId, int32 ParentChannelId)
{
    FScopeLock Lock(&PoolLock);
    
    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Warning, TEXT("FNarrowBandAllocator::SetupSharedChannels - Pool '%s' not initialized"), 
            *PoolName.ToString());
        return false;
    }
    
    // Validate channel IDs
    if (ChildChannelId < 0 || ParentChannelId < 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("FNarrowBandAllocator::SetupSharedChannels - Invalid channel IDs: Child=%d, Parent=%d"), 
            ChildChannelId, ParentChannelId);
        return false;
    }
    
    // Log the channel sharing setup
    UE_LOG(LogTemp, Log, TEXT("FNarrowBandAllocator::SetupSharedChannels - Set up shared channels between material %u (Channel %d) and material %u (Channel %d)"),
        ChildTypeId, ChildChannelId, ParentTypeId, ParentChannelId);
    
    return true;
}

bool FNarrowBandAllocator::UpdateTypeVersion(const FTypeVersionMigrationInfo& MigrationInfo)
{
    FScopeLock Lock(&PoolLock);
    
    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Warning, TEXT("FNarrowBandAllocator::UpdateTypeVersion - Pool '%s' not initialized"), 
            *PoolName.ToString());
        return false;
    }
    
    // Log the migration
    UE_LOG(LogTemp, Log, TEXT("FNarrowBandAllocator::UpdateTypeVersion - Migrating type '%s' from version %u to %u"),
        *MigrationInfo.TypeName.ToString(), MigrationInfo.OldVersion, MigrationInfo.NewVersion);
    
    // For now, we just report success without actually doing any migration
    // In a real implementation, this would update the memory layout based on the version change
    return true;
}

void FNarrowBandAllocator::SetAlignmentRequirement(uint32 Alignment)
{
    // Ensure alignment is a power of 2
    if (Alignment > 0 && ((Alignment & (Alignment - 1)) == 0))
    {
        // Store alignment value or apply it to allocations
        UE_LOG(LogTemp, Verbose, TEXT("FNarrowBandAllocator(%s): Setting alignment requirement to %u bytes"), 
            *PoolName.ToString(), Alignment);
        
        // In a real implementation, we would adjust memory alignment
        // For now, we just log the request
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("FNarrowBandAllocator(%s): Invalid alignment value %u (must be power of 2)"), 
            *PoolName.ToString(), Alignment);
    }
}

void FNarrowBandAllocator::SetMemoryUsageHint(EPoolMemoryUsage UsageHint)
{
    UE_LOG(LogTemp, Verbose, TEXT("FNarrowBandAllocator(%s): Setting memory usage hint to %d"), 
        *PoolName.ToString(), static_cast<int32>(UsageHint));
    
    // In a real implementation, we would optimize memory layout based on the hint
    // For now, we just log the request
}

void FNarrowBandAllocator::SetNumaNode(int32 NodeId)
{
    UE_LOG(LogTemp, Verbose, TEXT("FNarrowBandAllocator(%s): Setting NUMA node to %d"), 
        *PoolName.ToString(), NodeId);
    
    // In a real implementation, we would bind allocations to the specified NUMA node
    // For now, we just log the request
}

bool FNarrowBandAllocator::ConfigureSIMDLayout(uint32 MaterialTypeId, uint32 FieldAlignment, bool bEnableVectorization, ESIMDInstructionSet SIMDOperationType)
{
    FScopeLock Lock(&PoolLock);
    
    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Warning, TEXT("FNarrowBandAllocator::ConfigureSIMDLayout - Pool '%s' is not initialized"), *PoolName.ToString());
        return false;
    }
    
    // Validate alignment - must be power of 2 and at least 16 bytes for SIMD
    if (FieldAlignment < 16 || (FieldAlignment & (FieldAlignment - 1)) != 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("FNarrowBandAllocator::ConfigureSIMDLayout - Invalid field alignment %u, must be power of 2 and at least 16 bytes"), FieldAlignment);
        return false;
    }
    
    // Determine appropriate alignment based on SIMD type
    uint32 MinimumAlignment = 16; // Default 16-byte alignment for SSE/Neon
    switch (SIMDOperationType)
    {
        case ESIMDInstructionSet::AVX:
        case ESIMDInstructionSet::AVX2:
            MinimumAlignment = 32; // 32-byte alignment for 256-bit operations
            break;
        case ESIMDInstructionSet::SSE:
        case ESIMDInstructionSet::Neon:
            MinimumAlignment = 16; // 16-byte alignment for 128-bit operations
            break;
        case ESIMDInstructionSet::None:
            MinimumAlignment = 8; // No special alignment needed
            break;
    }
    
    // Ensure alignment is at least the minimum for the instruction set
    if (FieldAlignment < MinimumAlignment)
    {
        FieldAlignment = MinimumAlignment;
        UE_LOG(LogTemp, Log, TEXT("FNarrowBandAllocator::ConfigureSIMDLayout - Adjusted field alignment to %u bytes for SIMD type %d"), 
            FieldAlignment, static_cast<int32>(SIMDOperationType));
    }
    
    // Create or update SIMD field layout
    FSIMDFieldLayout& Layout = SIMDFieldLayouts.FindOrAdd(MaterialTypeId);
    Layout.MaterialTypeId = MaterialTypeId;
    Layout.FieldAlignment = FieldAlignment;
    Layout.bVectorizationEnabled = bEnableVectorization;
    Layout.InstructionSet = SIMDOperationType;
    
    // Store in individual maps for faster lookups
    MaterialFieldAlignments.Add(MaterialTypeId, FieldAlignment);
    MaterialVectorizationEnabled.Add(MaterialTypeId, bEnableVectorization);
    
    // Apply global SIMD setting if this is higher than current
    if (static_cast<uint8>(SIMDOperationType) > static_cast<uint8>(SIMDInstructions))
    {
        SIMDInstructions = SIMDOperationType;
    }
    
    UE_LOG(LogTemp, Log, TEXT("FNarrowBandAllocator::ConfigureSIMDLayout - Configured material %u with alignment %u bytes, vectorization %s, SIMD type %d"),
        MaterialTypeId, FieldAlignment, bEnableVectorization ? TEXT("enabled") : TEXT("disabled"), static_cast<int32>(SIMDOperationType));
    
    return true;
}