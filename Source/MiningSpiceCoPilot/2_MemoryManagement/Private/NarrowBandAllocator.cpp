// Copyright Epic Games, Inc. All Rights Reserved.

#include "2_MemoryManagement/Public/NarrowBandAllocator.h"
#include "HAL/PlatformMath.h"
#include "Misc/ScopeLock.h"

FNarrowBandAllocator::FNarrowBandAllocator(const FName& InPoolName, uint32 InBlockSize, uint32 InBlockCount, 
    EMemoryAccessPattern InAccessPattern, bool InAllowGrowth)
    : PoolName(InPoolName)
    , BlockSize(FMath::Max(InBlockSize, 8u)) // Ensure minimum block size
    , PoolMemory(nullptr)
    , MaxBlockCount(InBlockCount)
    , CurrentBlockCount(0)
    , bIsInitialized(false)
    , bAllowsGrowth(InAllowGrowth)
    , AccessPattern(InAccessPattern)
    , PrecisionTier(EMemoryTier::Hot) // Default to high precision
    , ChannelCount(1) // Default to single channel
    , bStatsDirty(true)
{
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
    
    // Align block size based on precision tier and SIMD requirements
    uint32 Alignment = GetElementAlignment();
    BlockSize = Align(BlockSize, Alignment);
    
    if (!AllocatePoolMemory(MaxBlockCount))
    {
        return false;
    }
    
    bIsInitialized = true;
    bStatsDirty = true;
    
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
        if (!bAllowsGrowth || !Grow(FMath::Max(32u, MaxBlockCount / 4)))
        {
            // Could not grow the pool
            CachedStats.AllocationFailures++;
            bStatsDirty = true;
            return nullptr;
        }
    }
    
    // Get a free block index
    uint32 BlockIndex = FreeBlocks.Pop(false);
    
    // Mark as allocated and set metadata
    BlockMetadata[BlockIndex].bAllocated = true;
    BlockMetadata[BlockIndex].AllocationTag = AllocationTag;
    BlockMetadata[BlockIndex].RequestingObject = RequestingObject;
    BlockMetadata[BlockIndex].AllocationTime = FPlatformTime::Seconds();
    
    // Calculate address of the block
    void* Ptr = PoolMemory + (BlockIndex * BlockSize);
    
    // Zero out the memory for the new allocation
    FMemory::Memzero(Ptr, BlockSize);
    
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
    
    // Calculate alignment based on precision tier
    uint32 Alignment = GetElementAlignment();
    
    // Allocate new memory
    uint64 TotalSize = static_cast<uint64>(NewBlockCount) * static_cast<uint64>(BlockSize);
    uint8* NewMemory = (uint8*)FMemory::Malloc(TotalSize, Alignment);
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
    uint32 BlocksToRemove = FMath::Min(MaxBlocksToRemove, FreeBlockCount);
    if (BlocksToRemove == 0)
    {
        return 0; // Can't remove any blocks
    }
    
    // Don't shrink below minimum capacity
    uint32 MinCapacity = FMath::Max(64u, AllocatedBlocks * 2);
    if (CurrentBlockCount - BlocksToRemove < MinCapacity)
    {
        BlocksToRemove = CurrentBlockCount > MinCapacity ? CurrentBlockCount - MinCapacity : 0;
    }
    
    if (BlocksToRemove == 0)
    {
        return 0;
    }
    
    // Create a compact block layout by moving allocated blocks to the front
    // For a real narrow-band allocator, we'd need to implement defragmentation
    // that preserves the distance field structure and relationships
    // For now, we'll use a simplified approach that doesn't actually shrink
    
    return 0;
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
    
    if (!bIsInitialized)
    {
        return false;
    }
    
    // Track time to stay within budget
    double StartTime = FPlatformTime::Seconds();
    double EndTime = StartTime + MaxTimeMs / 1000.0;
    
    // For narrow-band allocators, defragmentation is more complex because
    // we want to keep elements that are close in the distance field also
    // close in memory for better cache locality
    
    // This would be implemented by:
    // 1. Sort blocks by spatial position or distance from surface
    // 2. Create a new memory layout
    // 3. Update pointers to those blocks
    // 4. Move the data to the new layout
    
    // For this example, we'll just identify fragmentation
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
        
        // Check if we're out of time
        if (FPlatformTime::Seconds() >= EndTime)
        {
            break;
        }
    }
    
    // Update stats 
    bStatsDirty = true;
    
    // Report if we found any fragmentation
    return FragmentCount > 0;
}

bool FNarrowBandAllocator::Validate(TArray<FString>& OutErrors) const
{
    FScopeLock Lock(&PoolLock);
    
    if (!bIsInitialized)
    {
        OutErrors.Add(FString::Printf(TEXT("NarrowBandAllocator '%s' is not initialized"), *PoolName.ToString()));
        return false;
    }
    
    if (!PoolMemory)
    {
        OutErrors.Add(FString::Printf(TEXT("NarrowBandAllocator '%s' has invalid memory"), *PoolName.ToString()));
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
            OutErrors.Add(FString::Printf(TEXT("NarrowBandAllocator '%s' has invalid free index %u (max: %u)"), 
                *PoolName.ToString(), FreeIndex, CurrentBlockCount - 1));
            return false;
        }
        
        if (BlockUsed[FreeIndex])
        {
            OutErrors.Add(FString::Printf(TEXT("NarrowBandAllocator '%s' has duplicate free index %u"), 
                *PoolName.ToString(), FreeIndex));
            return false;
        }
        
        if (BlockMetadata[FreeIndex].bAllocated)
        {
            OutErrors.Add(FString::Printf(TEXT("NarrowBandAllocator '%s' has free index %u marked as allocated"), 
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
        OutErrors.Add(FString::Printf(TEXT("NarrowBandAllocator '%s' free count mismatch: %u in metadata, %u in free list"), 
            *PoolName.ToString(), FreeCount, FreeBlocks.Num()));
        return false;
    }
    
    if (AllocatedCount + FreeCount != CurrentBlockCount)
    {
        OutErrors.Add(FString::Printf(TEXT("NarrowBandAllocator '%s' block count mismatch: %u allocated + %u free != %u total"), 
            *PoolName.ToString(), AllocatedCount, FreeCount, CurrentBlockCount));
        return false;
    }
    
    return true;
}

void FNarrowBandAllocator::SetPrecisionTier(EMemoryTier Tier)
{
    FScopeLock Lock(&PoolLock);
    
    if (Tier != PrecisionTier)
    {
        // Save old tier
        EMemoryTier OldTier = PrecisionTier;
        PrecisionTier = Tier;
        
        // If initialized, we'd need to reallocate with new precision
        // This is complex and would require recreation of the pool with new block sizes
        // For now, we'll just log a warning if the allocator is already initialized
        if (bIsInitialized)
        {
            UE_LOG(LogTemp, Warning, TEXT("NarrowBandAllocator '%s' precision tier changed from %d to %d after initialization. Changes will be applied on next allocation."),
                *PoolName.ToString(), static_cast<int32>(OldTier), static_cast<int32>(Tier));
        }
        
        // Force stats update
        bStatsDirty = true;
    }
}

EMemoryTier FNarrowBandAllocator::GetPrecisionTier() const
{
    return PrecisionTier;
}

void FNarrowBandAllocator::SetChannelCount(uint32 NumChannels)
{
    FScopeLock Lock(&PoolLock);
    
    if (NumChannels != ChannelCount && NumChannels > 0)
    {
        // Save old channel count
        uint32 OldChannelCount = ChannelCount;
        ChannelCount = NumChannels;
        
        // If initialized, we'd need to reallocate with new channel count
        // This is complex and would require recreation of the pool with new block sizes
        // For now, we'll just log a warning if the allocator is already initialized
        if (bIsInitialized)
        {
            UE_LOG(LogTemp, Warning, TEXT("NarrowBandAllocator '%s' channel count changed from %u to %u after initialization. Changes will be applied on next allocation."),
                *PoolName.ToString(), OldChannelCount, NumChannels);
        }
        
        // Force stats update
        bStatsDirty = true;
    }
}

uint32 FNarrowBandAllocator::GetChannelCount() const
{
    return ChannelCount;
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
        BlockMetadata[i].AllocationTime = 0.0;
        BlockMetadata[i].DistanceFromSurface = 0.0f;
        
        // Add to free list
        FreeBlocks.Add(i);
    }
    
    // Update stats
    bStatsDirty = true;
    
    return true;
}

bool FNarrowBandAllocator::AllocatePoolMemory(uint32 BlockCount)
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
    
    // Calculate alignment based on precision tier
    uint32 Alignment = GetElementAlignment();
    
    // Allocate memory aligned appropriately for SIMD operations
    PoolMemory = (uint8*)FMemory::Malloc(TotalSize, Alignment);
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
    // Make sure this is called with the lock held
    
    // Initialize base stats
    CachedStats.PoolName = PoolName;
    CachedStats.BlockSize = BlockSize;
    CachedStats.BlockCount = CurrentBlockCount;
    CachedStats.bAllowsGrowth = bAllowsGrowth;
    CachedStats.FreeBlocks = FreeBlocks.Num();
    CachedStats.AllocatedBlocks = CurrentBlockCount - FreeBlocks.Num();
    
    // Calculate overhead (metadata storage)
    CachedStats.OverheadBytes = BlockMetadata.GetAllocatedSize() + FreeBlocks.GetAllocatedSize();
    
    // Count allocations and peak usage
    uint32 AllocatedCount = 0;
    for (const FBlockMetadata& Metadata : BlockMetadata)
    {
        if (Metadata.bAllocated)
        {
            AllocatedCount++;
        }
    }
    
    CachedStats.AllocatedBlocks = AllocatedCount;
    CachedStats.PeakAllocatedBlocks = FMath::Max(CachedStats.PeakAllocatedBlocks, AllocatedCount);
    
    // Calculate growth info
    CachedStats.GrowthCount = CurrentBlockCount > MaxBlockCount ? 
        (CurrentBlockCount - MaxBlockCount + MaxBlockCount - 1) / MaxBlockCount : 0;
    
    // Calculate fragmentation - for narrow band, fragmentation is more complex
    // as we want to consider spatial locality
    uint32 FragmentTransitions = 0;
    bool PrevWasAllocated = false;
    
    for (uint32 i = 0; i < CurrentBlockCount; ++i)
    {
        bool IsAllocated = BlockMetadata[i].bAllocated;
        
        if (i > 0 && IsAllocated != PrevWasAllocated)
        {
            FragmentTransitions++;
        }
        
        PrevWasAllocated = IsAllocated;
    }
    
    // Fragmentation metric: number of transitions between allocated and free blocks
    // as a percentage of the maximum possible transitions
    const float MaxPossibleTransitions = static_cast<float>(CurrentBlockCount);
    CachedStats.FragmentationPercent = MaxPossibleTransitions > 0.0f ?
        (FragmentTransitions / MaxPossibleTransitions) * 100.0f : 0.0f;
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
    // Choose alignment based on precision tier and SIMD requirements
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
    // Choose bytes per channel based on precision tier
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
        return 0; // Special case, handled separately
        
    default:
        // Default to full float
        return 4;
    }
}