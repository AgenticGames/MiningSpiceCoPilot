// Copyright Epic Games, Inc. All Rights Reserved.

#include "SVOAllocator.h"
#include "HAL/PlatformMath.h"
#include "Misc/ScopeLock.h"
#include "Math/UnrealMathSSE.h"

// Lookup tables for Z-order curve calculations
namespace 
{
    // Pre-computed lookup tables for 10-bit values (up to 1024^3 grid)
    uint32 SplitBy3[1024];
    
    // Initialize lookup tables
    bool InitializeZOrderTables()
    {
        static bool bInitialized = false;
        
        if (!bInitialized)
        {
            // Fill split table for fast Z-order curve calculations
            for (uint32 i = 0; i < 1024; ++i)
            {
                uint32 x = i;
                x = (x | (x << 16)) & 0x030000FF;
                x = (x | (x << 8)) & 0x0300F00F;
                x = (x | (x << 4)) & 0x030C30C3;
                x = (x | (x << 2)) & 0x09249249;
                SplitBy3[i] = x;
            }
            
            bInitialized = true;
        }
        
        return true;
    }
    
    // Static initializer to ensure tables are initialized
    static bool TablesInitialized = InitializeZOrderTables();
}

FSVOAllocator::FSVOAllocator(const FName& InPoolName, uint32 InBlockSize, uint32 InBlockCount, 
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
    , ZOrderMappingFunction(&FSVOAllocator::DefaultZOrderMapping)
    , bStatsDirty(true)
{
    // Align block size to 16 bytes for SIMD operations
    BlockSize = Align(BlockSize, 16);
}

FSVOAllocator::~FSVOAllocator()
{
    Shutdown();
}

bool FSVOAllocator::Initialize()
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
    
    return true;
}

void FSVOAllocator::Shutdown()
{
    FScopeLock Lock(&PoolLock);
    
    if (!bIsInitialized)
    {
        return;
    }
    
    FreePoolMemory();
    bIsInitialized = false;
}

bool FSVOAllocator::IsInitialized() const
{
    return bIsInitialized;
}

FName FSVOAllocator::GetPoolName() const
{
    return PoolName;
}

uint32 FSVOAllocator::GetBlockSize() const
{
    return BlockSize;
}

void* FSVOAllocator::Allocate(const UObject* RequestingObject, FName AllocationTag)
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
    
    // Calculate address of the block
    void* Ptr = PoolMemory + (BlockIndex * BlockSize);
    
    // Update stats
    bStatsDirty = true;
    CachedStats.TotalAllocations++;
    
    return Ptr;
}

bool FSVOAllocator::Free(void* Ptr)
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
    
    // Add to free list
    FreeBlocks.Add(BlockIndex);
    
    // Update stats
    bStatsDirty = true;
    CachedStats.TotalFrees++;
    
    return true;
}

bool FSVOAllocator::Grow(uint32 AdditionalBlockCount, bool bForceGrowth)
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
    
    // Allocate new memory
    uint8* NewMemory = (uint8*)FMemory::Malloc(NewBlockCount * BlockSize, 16);
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
    
    return true;
}

uint32 FSVOAllocator::Shrink(uint32 MaxBlocksToRemove)
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
    
    // Don't shrink below minimum capacity
    uint32 MinCapacity = AllocatedBlocks * 2;
    if (MinCapacity < 64u)
    {
        MinCapacity = 64u;
    }
    if (CurrentBlockCount - BlocksToRemove < MinCapacity)
    {
        BlocksToRemove = CurrentBlockCount > MinCapacity ? CurrentBlockCount - MinCapacity : 0;
    }
    
    if (BlocksToRemove == 0)
    {
        return 0;
    }
    
    // To properly shrink, we would need to relocate allocated blocks to create
    // a contiguous free area at the end of the pool. This would require tracking
    // all pointers to allocated blocks and updating them.
    
    // For this implementation, we simply verify that all blocks at the end are free,
    // and if so, we can easily truncate the pool.
    
    // Sort free blocks by index (descending) to check if the highest-numbered blocks are free
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
    
    // Reallocate memory if we have any blocks left
    if (NewBlockCount > 0)
    {
        uint8* NewMemory = (uint8*)FMemory::Malloc(NewBlockCount * BlockSize, 16);
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
    
    return BlocksToRemove;
}

bool FSVOAllocator::OwnsPointer(const void* Ptr) const
{
    if (!bIsInitialized || !PoolMemory || !Ptr)
    {
        return false;
    }
    
    // Check if the pointer is within our memory range
    const uint8* BytePtr = static_cast<const uint8*>(Ptr);
    return BytePtr >= PoolMemory && BytePtr < (PoolMemory + (CurrentBlockCount * BlockSize));
}

void FSVOAllocator::SetAccessPattern(EMemoryAccessPattern InAccessPattern)
{
    FScopeLock Lock(&PoolLock);
    AccessPattern = InAccessPattern;
}

EMemoryAccessPattern FSVOAllocator::GetAccessPattern() const
{
    return AccessPattern;
}

FPoolStats FSVOAllocator::GetStats() const
{
    FScopeLock Lock(&PoolLock);
    
    if (bStatsDirty)
    {
        UpdateStats();
        bStatsDirty = false;
    }
    
    // Update stats if needed
    if (bStatsDirty)
    {
        UpdateStats();
    }
    
    // Update peaks
    if (CachedStats.AllocatedBlocks > CachedStats.PeakAllocatedBlocks)
    {
        CachedStats.PeakAllocatedBlocks = CachedStats.AllocatedBlocks;
    }
    
    return CachedStats;
}

bool FSVOAllocator::Defragment(float MaxTimeMs)
{
    FScopeLock Lock(&PoolLock);
    
    if (!bIsInitialized)
    {
        return false;
    }
    
    // Track time to stay within budget
    double StartTime = FPlatformTime::Seconds();
    double EndTime = StartTime + MaxTimeMs / 1000.0;
    bool bDidDefragment = false;

    // Calculate current fragmentation
    uint32 FragmentCount = 0;
    uint32 FragmentedBlocks = 0;
    
    // Simple approach: count transitions between allocated and free blocks
    bool PrevWasAllocated = false;
    
    for (uint32 i = 0; i < CurrentBlockCount; ++i)
    {
        bool IsAllocated = BlockMetadata[i].bAllocated;
        
        if (i > 0 && IsAllocated != PrevWasAllocated)
        {
            FragmentCount++;
        }
        
        PrevWasAllocated = IsAllocated;
        
        // Check if we're out of time
        if (FPlatformTime::Seconds() >= EndTime)
        {
            break;
        }
    }
    
    // Calculate fragmentation percentage for logging
    float FragmentationPercentage = (CurrentBlockCount > 0) ? 
        (100.0f * static_cast<float>(FragmentCount) / CurrentBlockCount) : 0.0f;
    
    // For a real implementation, we would actually move blocks around to reduce fragmentation
    // This would require tracking all pointers to allocated blocks and updating them
    
    // In this simplified version, we'll just log the fragmentation
    if (FragmentCount > 0)
    {
        UE_LOG(LogTemp, Verbose, TEXT("FSVOAllocator::Defragment - Pool '%s' has %u fragments (%.1f%%)"),
            *PoolName.ToString(), FragmentCount, FragmentationPercentage);
            
        // Set dirty flag to recalculate stats
        bStatsDirty = true;
        bDidDefragment = true;
    }
    
    return bDidDefragment;
}

bool FSVOAllocator::Validate(TArray<FString>& OutErrors) const
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
    FMemory::Memset(BlockUsed.GetData(), 0, BlockUsed.Num() * sizeof(bool));
    
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

void FSVOAllocator::SetZOrderMappingFunction(uint32 (*NewMappingFunction)(uint32, uint32, uint32))
{
    FScopeLock Lock(&PoolLock);
    
    if (NewMappingFunction)
    {
        ZOrderMappingFunction = NewMappingFunction;
    }
    else
    {
        ZOrderMappingFunction = &FSVOAllocator::DefaultZOrderMapping;
    }
}

uint32 FSVOAllocator::DefaultZOrderMapping(uint32 x, uint32 y, uint32 z)
{
    // Simple 10-bit interleaving for 3D Z-order curve
    // Split and interleave bits of x, y, and z to create a cache-coherent spatial index
    // This code assumes we have a maximum of 10 bits per coordinate (1024x1024x1024 space)
    
    // Mask to 10 bits per value
    x &= 0x3FF;
    y &= 0x3FF;
    z &= 0x3FF;
    
    // Separate bits by 3 positions using bit manipulation
    x = (x | (x << 16)) & 0x030000FF;
    x = (x | (x << 8)) & 0x0300F00F;
    x = (x | (x << 4)) & 0x030C30C3;
    x = (x | (x << 2)) & 0x09249249;
    
    y = (y | (y << 16)) & 0x030000FF;
    y = (y | (y << 8)) & 0x0300F00F;
    y = (y | (y << 4)) & 0x030C30C3;
    y = (y | (y << 2)) & 0x09249249;
    
    z = (z | (z << 16)) & 0x030000FF;
    z = (z | (z << 8)) & 0x0300F00F;
    z = (z | (z << 4)) & 0x030C30C3;
    z = (z | (z << 2)) & 0x09249249;
    
    // Interleave the bits
    return x | (y << 1) | (z << 2);
}

uint32 FSVOAllocator::LookupTableZOrderMapping(uint32 x, uint32 y, uint32 z)
{
    // Ensure lookup tables are initialized
    if (!InitializeZOrderTables())
    {
        return DefaultZOrderMapping(x, y, z);
    }
    
    // Mask to 10 bits per coordinate
    x &= 0x3FF;
    y &= 0x3FF;
    z &= 0x3FF;
    
    // Use pre-computed lookups for bit splitting
    return SplitBy3[x] | (SplitBy3[y] << 1) | (SplitBy3[z] << 2);
}

bool FSVOAllocator::Reset()
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
        
        // Add to free list
        FreeBlocks.Add(i);
    }
    
    // Update stats
    bStatsDirty = true;
    
    return true;
}

bool FSVOAllocator::AllocatePoolMemory(uint32 BlockCount)
{
    // Check inputs
    if (BlockCount == 0 || BlockSize == 0)
    {
        return false;
    }
    
    // Free existing memory if any
    if (PoolMemory)
    {
        FreePoolMemory();
    }
    
    // Calculate total memory size
    uint64 TotalSize = static_cast<uint64>(BlockCount) * static_cast<uint64>(BlockSize);
    
    // Allocate memory aligned to 16 bytes for SIMD operations
    PoolMemory = (uint8*)FMemory::Malloc(TotalSize, 16);
    if (!PoolMemory)
    {
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

void FSVOAllocator::FreePoolMemory()
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

void FSVOAllocator::UpdateStats() const
{
    // Acquire lock for thread safety
    FScopeLock Lock(&PoolLock);
    
    // Only update if dirty
    if (!bStatsDirty)
    {
        return;
    }
    
    // Reset stats
    CachedStats.PoolName = PoolName;
    CachedStats.BlockSize = BlockSize;
    CachedStats.BlockCount = CurrentBlockCount;
    CachedStats.bAllowsGrowth = bAllowsGrowth;
    
    // Count allocations
    uint32 AllocatedBlockCount = 0;
    for (const auto& Metadata : BlockMetadata)
    {
        if (Metadata.bAllocated)
        {
            AllocatedBlockCount++;
        }
    }
    
    // Update peak allocation tracking
    CachedStats.PeakAllocatedBlocks = FMath::Max(CachedStats.PeakAllocatedBlocks, AllocatedBlockCount);
    
    // Calculate memory usage
    CachedStats.AllocatedBlocks = AllocatedBlockCount;
    CachedStats.FreeBlocks = CurrentBlockCount - AllocatedBlockCount;
    
    // Calculate fragmentation - simple transitions between allocated and free
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
    
    // Calculate overhead
    CachedStats.OverheadBytes = sizeof(FSVOAllocator) +                              // Class instance
                                (BlockMetadata.Num() * sizeof(FBlockMetadata)) +     // Metadata
                                (FreeBlocks.Num() * sizeof(uint32));                 // Free list
    
    // Mark stats as clean
    bStatsDirty = false;
}

int32 FSVOAllocator::GetBlockIndex(const void* Ptr) const
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

bool FSVOAllocator::MoveNextFragmentedAllocation(void*& OutOldPtr, void*& OutNewPtr, uint64& OutAllocationSize)
{
    FScopeLock Lock(&PoolLock);
    
    if (!bIsInitialized || !PoolMemory)
    {
        return false;
    }
    
    // Simple defragmentation approach: scan for non-contiguous allocated blocks
    // and move them to lower memory addresses to reduce fragmentation
    
    // Start from the last block and find an allocated block
    static uint32 LastCheckedIndex = 0;
    uint32 BlockIndex = LastCheckedIndex;
    bool bFoundAllocated = false;
    
    // Find the next allocated block starting from the last checked index
    for (uint32 i = 0; i < CurrentBlockCount; ++i)
    {
        BlockIndex = (LastCheckedIndex + i) % CurrentBlockCount;
        
        // Check if this block is allocated
        if (BlockMetadata[BlockIndex].bAllocated)
        {
            // Find the first free block with a lower index
            uint32 TargetIndex = BlockIndex;
            for (uint32 j = 0; j < BlockIndex; ++j)
            {
                if (!BlockMetadata[j].bAllocated)
                {
                    TargetIndex = j;
                    bFoundAllocated = true;
                    break;
                }
            }
            
            // If we found a free block with a lower index, we can move this allocation
            if (bFoundAllocated && TargetIndex < BlockIndex)
            {
                // Get the pointers
                OutOldPtr = PoolMemory + (BlockIndex * BlockSize);
                OutNewPtr = PoolMemory + (TargetIndex * BlockSize);
                OutAllocationSize = BlockSize;
                
                // Update our last checked index for the next call
                LastCheckedIndex = (BlockIndex + 1) % CurrentBlockCount;
                
                // Move the memory
                FMemory::Memcpy(OutNewPtr, OutOldPtr, BlockSize);
                
                // Update metadata
                BlockMetadata[TargetIndex] = BlockMetadata[BlockIndex];
                BlockMetadata[BlockIndex].bAllocated = false;
                BlockMetadata[BlockIndex].AllocationTag = NAME_None;
                BlockMetadata[BlockIndex].RequestingObject = nullptr;
                BlockMetadata[BlockIndex].AllocationTime = 0.0;
                
                // Update free block list
                FreeBlocks.Remove(TargetIndex);
                FreeBlocks.Add(BlockIndex);
                
                // Stats are now dirty
                bStatsDirty = true;
                
                return true;
            }
        }
    }
    
    // Reset the last checked index if we've gone through all blocks
    LastCheckedIndex = 0;
    
    // No fragmented allocations found that can be moved
    return false;
}

bool FSVOAllocator::UpdateTypeVersion(const FTypeVersionMigrationInfo& MigrationInfo)
{
    FScopeLock Lock(&PoolLock);
    
    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Warning, TEXT("FSVOAllocator::UpdateTypeVersion - Pool '%s' not initialized"), 
            *PoolName.ToString());
        return false;
    }
    
    // Log the migration
    UE_LOG(LogTemp, Log, TEXT("FSVOAllocator::UpdateTypeVersion - Migrating type '%s' from version %u to %u"),
        *MigrationInfo.TypeName.ToString(), MigrationInfo.OldVersion, MigrationInfo.NewVersion);
    
    // For now, we just report success without actually doing any migration
    // In a real implementation, this would update the memory layout based on the version change
    return true;
}

void FSVOAllocator::SetAlignmentRequirement(uint32 Alignment)
{
    // Ensure alignment is a power of 2
    if (Alignment > 0 && ((Alignment & (Alignment - 1)) == 0))
    {
        // Store alignment value or apply it to allocations
        UE_LOG(LogTemp, Verbose, TEXT("FSVOAllocator(%s): Setting alignment requirement to %u bytes"), 
            *PoolName.ToString(), Alignment);
        
        // In a real implementation, we would adjust memory alignment
        // For now, we just log the request
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("FSVOAllocator(%s): Invalid alignment value %u (must be power of 2)"), 
            *PoolName.ToString(), Alignment);
    }
}

void FSVOAllocator::SetMemoryUsageHint(EPoolMemoryUsage UsageHint)
{
    UE_LOG(LogTemp, Verbose, TEXT("FSVOAllocator(%s): Setting memory usage hint to %d"), 
        *PoolName.ToString(), static_cast<int32>(UsageHint));
    
    // In a real implementation, we would optimize memory layout based on the hint
    // For now, we just log the request
}

void FSVOAllocator::SetNumaNode(int32 NodeId)
{
    UE_LOG(LogTemp, Verbose, TEXT("FSVOAllocator(%s): Setting NUMA node to %d"), 
        *PoolName.ToString(), NodeId);
    
    // In a real implementation, we would bind allocations to the specified NUMA node
    // For now, we just log the request
}

bool FSVOAllocator::ConfigureTypeLayout(uint32 TypeId, bool bUseZOrderCurve, bool bEnablePrefetching, EMemoryAccessPattern InAccessPattern)
{
    FScopeLock Lock(&PoolLock);
    
    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Warning, TEXT("FSVOAllocator::ConfigureTypeLayout - Pool '%s' is not initialized"), *PoolName.ToString());
        return false;
    }
    
    // Create or update node type layout
    FNodeTypeLayout& Layout = NodeTypeLayouts.FindOrAdd(TypeId);
    Layout.TypeId = TypeId;
    Layout.bUseZOrderCurve = bUseZOrderCurve;
    Layout.bEnablePrefetching = bEnablePrefetching;
    Layout.AccessPattern = InAccessPattern;
    
    // If this is the first type configured, set the default Z-order mapping function based on configuration
    if (NodeTypeLayouts.Num() == 1 && bUseZOrderCurve)
    {
        // Use lookup table version for better performance
        SetZOrderMappingFunction(&FSVOAllocator::LookupTableZOrderMapping);
    }
    
    UE_LOG(LogTemp, Log, TEXT("FSVOAllocator::ConfigureTypeLayout - Configured node type %u with Z-order curve %s, prefetching %s, access pattern %d"),
        TypeId, 
        bUseZOrderCurve ? TEXT("enabled") : TEXT("disabled"),
        bEnablePrefetching ? TEXT("enabled") : TEXT("disabled"),
        static_cast<int32>(InAccessPattern));
    
    return true;
}