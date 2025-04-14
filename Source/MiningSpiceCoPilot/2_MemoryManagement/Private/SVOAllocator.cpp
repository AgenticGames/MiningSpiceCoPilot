// Copyright Epic Games, Inc. All Rights Reserved.

#include "SVOAllocator.h"
#include "HAL/PlatformMath.h"
#include "Misc/ScopeLock.h"

FSVOAllocator::FSVOAllocator(const FName& InPoolName, uint32 InBlockSize, uint32 InBlockCount, 
    EMemoryAccessPattern InAccessPattern, bool InAllowGrowth)
    : PoolName(InPoolName)
    , BlockSize(FMath::Max(InBlockSize, 8u)) // Ensure minimum block size
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
        if (!bAllowsGrowth || !Grow(FMath::Max(32u, MaxBlockCount / 4)))
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
    // This is a complex operation and could be optimized further in a real implementation
    
    // Not implementing full defragmentation here as it's a complex operation
    // In a real implementation, we would:
    // 1. Sort blocks so that all allocated blocks are at the front
    // 2. Update pointers to those blocks
    // 3. Create a new smaller memory buffer
    // 4. Copy the allocated blocks to the new buffer
    // 5. Free the old buffer
    
    // For this example, we'll just report that we can't shrink since it would
    // require full pointer tracking
    
    return 0;
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
    
    // Calculate fragmentation (interspersed allocated and free blocks)
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
    
    // For a real implementation, we'd actually move blocks around to reduce fragmentation
    // This would require tracking all pointers to allocated blocks and updating them
    
    // Return true if we did any defragmentation work (in this case, just analysis)
    return FragmentCount > 0;
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
    // Interleave bits of x, y, and z to create a cache-coherent spatial index
    
    // Separate every bit with two zeros
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
    // This would be implemented using pre-computed lookup tables
    // For performance compared to bit manipulation
    // Not implemented here for brevity, would use DefaultZOrderMapping instead
    return DefaultZOrderMapping(x, y, z);
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
    CachedStats.TotalAllocations = 0;
    CachedStats.PeakAllocatedBlocks = 0;
    CachedStats.AllocatedBlocks = 0;
    // Clear previous allocation tracking data if present in your implementation
    // CachedStats.AllocationsByTag.Empty();
    // CachedStats.AllocationsByObject.Empty();
    
    // Count allocations
    uint32 AllocatedBlockCount = 0;
    for (const auto& Metadata : BlockMetadata)
    {
        if (Metadata.bAllocated)
        {
            AllocatedBlockCount++;
            // Track allocations by tag if implemented
            // CachedStats.AllocationsByTag.FindOrAdd(Metadata.AllocationTag)++;
            
            // Track allocations by object if implemented
            // if (Metadata.RequestingObject.IsValid())
            // {
            //     const UObject* Object = Metadata.RequestingObject.Get();
            //     CachedStats.AllocationsByObject.FindOrAdd(Object->GetClass()->GetFName())++;
            // }
        }
    }
    
    // Calculate memory usage
    CachedStats.AllocatedBlocks = AllocatedBlockCount;
    // Update fragmentation calculation
    CachedStats.FreeBlocks = CurrentBlockCount - AllocatedBlockCount;
    CachedStats.FragmentationPercent = CurrentBlockCount > 0 
        ? 100.0f * (static_cast<float>(CachedStats.FreeBlocks) / CurrentBlockCount) 
        : 0.0f;
    
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