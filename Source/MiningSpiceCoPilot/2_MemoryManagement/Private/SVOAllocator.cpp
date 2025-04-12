// Copyright Epic Games, Inc. All Rights Reserved.

#include "2_MemoryManagement/Public/SVOAllocator.h"
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
            // Could not grow or not allowed to grow
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
    
    bStatsDirty = true;
    return Ptr;
}

bool FSVOAllocator::Free(void* Ptr)
{
    if (!Ptr || !bIsInitialized)
    {
        return false;
    }
    
    FScopeLock Lock(&PoolLock);
    
    // Calculate block index from pointer
    uint8* BytePtr = static_cast<uint8*>(Ptr);
    ptrdiff_t Offset = BytePtr - PoolMemory;
    
    if (Offset < 0 || Offset % BlockSize != 0 || Offset >= static_cast<ptrdiff_t>(CurrentBlockCount * BlockSize))
    {
        // Pointer is outside the pool or not aligned with block boundaries
        return false;
    }
    
    uint32 BlockIndex = static_cast<uint32>(Offset / BlockSize);
    
    if (!BlockMetadata[BlockIndex].bAllocated)
    {
        // Block is not allocated, double free
        return false;
    }
    
    // Mark as free
    BlockMetadata[BlockIndex].bAllocated = false;
    BlockMetadata[BlockIndex].AllocationTag = NAME_None;
    BlockMetadata[BlockIndex].RequestingObject = nullptr;
    
    // Add to free list
    FreeBlocks.Add(BlockIndex);
    
    bStatsDirty = true;
    return true;
}

bool FSVOAllocator::Grow(uint32 AdditionalBlockCount, bool bForceGrowth)
{
    FScopeLock Lock(&PoolLock);
    
    if (!bIsInitialized)
    {
        return false;
    }
    
    if (!bAllowsGrowth && !bForceGrowth)
    {
        return false;
    }
    
    // Calculate new block count
    uint32 NewBlockCount = CurrentBlockCount + AdditionalBlockCount;
    
    // Create a new, larger pool
    uint8* NewPoolMemory = static_cast<uint8*>(FMemory::Malloc(NewBlockCount * BlockSize, 16));
    
    if (!NewPoolMemory)
    {
        // Memory allocation failed
        return false;
    }
    
    // Copy existing blocks
    FMemory::Memcpy(NewPoolMemory, PoolMemory, CurrentBlockCount * BlockSize);
    
    // Free old memory
    FMemory::Free(PoolMemory);
    
    // Update metadata array
    uint32 OldBlockCount = BlockMetadata.Num();
    BlockMetadata.SetNum(NewBlockCount);
    
    // Initialize new block metadata
    for (uint32 i = OldBlockCount; i < NewBlockCount; ++i)
    {
        BlockMetadata[i].bAllocated = false;
        BlockMetadata[i].AllocationTag = NAME_None;
        BlockMetadata[i].RequestingObject = nullptr;
        BlockMetadata[i].X = 0;
        BlockMetadata[i].Y = 0;
        BlockMetadata[i].Z = 0;
        
        // Add to free list
        FreeBlocks.Add(i);
    }
    
    // Update pool state
    PoolMemory = NewPoolMemory;
    MaxBlockCount = NewBlockCount;
    CurrentBlockCount = NewBlockCount;
    
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
    
    // Calculate how many blocks we can remove
    uint32 AllocatedBlocks = CurrentBlockCount - FreeBlocks.Num();
    uint32 BlocksToRemove = FMath::Min(MaxBlocksToRemove, FreeBlocks.Num());
    
    if (BlocksToRemove == 0)
    {
        // No blocks can be removed
        return 0;
    }
    
    // We need to compact the memory, which is complex
    // For this implementation, only remove blocks from the end if possible
    
    // Sort free blocks in descending order
    FreeBlocks.Sort([](const uint32& A, const uint32& B) { return A > B; });
    
    // Count consecutive free blocks from the end
    uint32 ConsecutiveFreeBlocks = 0;
    for (uint32 i = 0; i < FreeBlocks.Num(); ++i)
    {
        if (FreeBlocks[i] == CurrentBlockCount - 1 - i)
        {
            ConsecutiveFreeBlocks++;
        }
        else
        {
            break;
        }
    }
    
    // Limit by blocks we're allowed to remove
    BlocksToRemove = FMath::Min(BlocksToRemove, ConsecutiveFreeBlocks);
    
    if (BlocksToRemove == 0)
    {
        // No consecutive free blocks at the end
        return 0;
    }
    
    // Calculate new block count
    uint32 NewBlockCount = CurrentBlockCount - BlocksToRemove;
    
    // Allocate new, smaller pool
    uint8* NewPoolMemory = static_cast<uint8*>(FMemory::Malloc(NewBlockCount * BlockSize, 16));
    
    if (!NewPoolMemory)
    {
        // Memory allocation failed
        return 0;
    }
    
    // Copy used blocks
    FMemory::Memcpy(NewPoolMemory, PoolMemory, NewBlockCount * BlockSize);
    
    // Free old memory
    FMemory::Free(PoolMemory);
    
    // Update free blocks list (remove blocks that are being discarded)
    TArray<uint32> NewFreeBlocks;
    for (uint32 BlockIndex : FreeBlocks)
    {
        if (BlockIndex < NewBlockCount)
        {
            NewFreeBlocks.Add(BlockIndex);
        }
    }
    FreeBlocks = MoveTemp(NewFreeBlocks);
    
    // Update metadata array
    BlockMetadata.SetNum(NewBlockCount);
    
    // Update pool state
    PoolMemory = NewPoolMemory;
    CurrentBlockCount = NewBlockCount;
    
    bStatsDirty = true;
    return BlocksToRemove;
}

bool FSVOAllocator::OwnsPointer(const void* Ptr) const
{
    if (!Ptr || !bIsInitialized)
    {
        return false;
    }
    
    FScopeLock Lock(&PoolLock);
    
    // Calculate block index from pointer
    const uint8* BytePtr = static_cast<const uint8*>(Ptr);
    ptrdiff_t Offset = BytePtr - PoolMemory;
    
    if (Offset < 0 || Offset % BlockSize != 0 || Offset >= static_cast<ptrdiff_t>(CurrentBlockCount * BlockSize))
    {
        // Pointer is outside the pool or not aligned with block boundaries
        return false;
    }
    
    uint32 BlockIndex = static_cast<uint32>(Offset / BlockSize);
    return BlockMetadata[BlockIndex].bAllocated;
}

void FSVOAllocator::SetAccessPattern(EMemoryAccessPattern InAccessPattern)
{
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
    
    // Simple implementation - compacts allocated blocks toward the beginning
    // and free blocks toward the end
    
    double StartTime = FPlatformTime::Seconds();
    double EndTime = StartTime + (MaxTimeMs / 1000.0);
    
    // Find allocated blocks from the end and free blocks from the beginning
    uint32 EndIndex = CurrentBlockCount - 1;
    uint32 StartIndex = 0;
    
    while (StartIndex < EndIndex)
    {
        // Find an allocated block from the end
        while (EndIndex > StartIndex && !BlockMetadata[EndIndex].bAllocated)
        {
            EndIndex--;
        }
        
        // Find a free block from the beginning
        while (StartIndex < EndIndex && BlockMetadata[StartIndex].bAllocated)
        {
            StartIndex++;
        }
        
        // If we can swap, do so
        if (StartIndex < EndIndex && !BlockMetadata[StartIndex].bAllocated && BlockMetadata[EndIndex].bAllocated)
        {
            // Swap block contents
            FMemory::Memcpy(PoolMemory + (StartIndex * BlockSize),
                           PoolMemory + (EndIndex * BlockSize),
                           BlockSize);
            
            // Update metadata
            BlockMetadata[StartIndex] = BlockMetadata[EndIndex];
            BlockMetadata[EndIndex].bAllocated = false;
            BlockMetadata[EndIndex].AllocationTag = NAME_None;
            BlockMetadata[EndIndex].RequestingObject = nullptr;
            
            // Adjust free blocks list
            FreeBlocks.Remove(StartIndex);
            FreeBlocks.Add(EndIndex);
            
            // Move indices
            StartIndex++;
            EndIndex--;
        }
        
        // Check if we've exceeded our time budget
        if (FPlatformTime::Seconds() > EndTime)
        {
            break;
        }
    }
    
    bStatsDirty = true;
    return true;
}

bool FSVOAllocator::Validate(TArray<FString>& OutErrors) const
{
    FScopeLock Lock(&PoolLock);
    
    if (!bIsInitialized)
    {
        OutErrors.Add(FString::Printf(TEXT("Pool '%s' is not initialized"), *PoolName.ToString()));
        return false;
    }
    
    bool bIsValid = true;
    
    // Check allocated counts match
    uint32 CountAllocated = 0;
    for (uint32 i = 0; i < CurrentBlockCount; ++i)
    {
        if (BlockMetadata[i].bAllocated)
        {
            CountAllocated++;
        }
    }
    
    uint32 CountFree = FreeBlocks.Num();
    if (CountAllocated + CountFree != CurrentBlockCount)
    {
        OutErrors.Add(FString::Printf(TEXT("Pool '%s' block count mismatch: Total=%u, Allocated=%u, Free=%u"),
            *PoolName.ToString(), CurrentBlockCount, CountAllocated, CountFree));
        bIsValid = false;
    }
    
    // Check for duplicates in free blocks list
    TSet<uint32> UniqueBlocks;
    for (uint32 i = 0; i < FreeBlocks.Num(); ++i)
    {
        uint32 BlockIndex = FreeBlocks[i];
        
        if (BlockIndex >= CurrentBlockCount)
        {
            OutErrors.Add(FString::Printf(TEXT("Pool '%s' has invalid free block index: %u (max: %u)"),
                *PoolName.ToString(), BlockIndex, CurrentBlockCount - 1));
            bIsValid = false;
            continue;
        }
        
        if (UniqueBlocks.Contains(BlockIndex))
        {
            OutErrors.Add(FString::Printf(TEXT("Pool '%s' has duplicate free block index: %u"),
                *PoolName.ToString(), BlockIndex));
            bIsValid = false;
        }
        
        UniqueBlocks.Add(BlockIndex);
        
        if (BlockMetadata[BlockIndex].bAllocated)
        {
            OutErrors.Add(FString::Printf(TEXT("Pool '%s' has free block index %u marked as allocated"),
                *PoolName.ToString(), BlockIndex));
            bIsValid = false;
        }
    }
    
    return bIsValid;
}

bool FSVOAllocator::Reset()
{
    FScopeLock Lock(&PoolLock);
    
    if (!bIsInitialized)
    {
        return false;
    }
    
    // Clear all blocks and reset metadata
    FreeBlocks.Empty(CurrentBlockCount);
    
    for (uint32 i = 0; i < CurrentBlockCount; ++i)
    {
        BlockMetadata[i].bAllocated = false;
        BlockMetadata[i].AllocationTag = NAME_None;
        BlockMetadata[i].RequestingObject = nullptr;
        BlockMetadata[i].X = 0;
        BlockMetadata[i].Y = 0;
        BlockMetadata[i].Z = 0;
        
        // Add to free list
        FreeBlocks.Add(i);
    }
    
    bStatsDirty = true;
    return true;
}

void FSVOAllocator::SetZOrderMapping(FZOrderMappingFunction InMappingFunction)
{
    if (InMappingFunction)
    {
        ZOrderMappingFunction = InMappingFunction;
    }
    else
    {
        ZOrderMappingFunction = &FSVOAllocator::DefaultZOrderMapping;
    }
}

void* FSVOAllocator::AllocateAtPosition(uint32 X, uint32 Y, uint32 Z, const UObject* RequestingObject, FName AllocationTag)
{
    FScopeLock Lock(&PoolLock);
    
    if (!bIsInitialized)
    {
        return nullptr;
    }
    
    // Allocate a normal block first
    void* Ptr = Allocate(RequestingObject, AllocationTag);
    if (!Ptr)
    {
        return nullptr;
    }
    
    // Set position metadata
    uint8* BytePtr = static_cast<uint8*>(Ptr);
    ptrdiff_t Offset = BytePtr - PoolMemory;
    uint32 BlockIndex = static_cast<uint32>(Offset / BlockSize);
    
    BlockMetadata[BlockIndex].X = X;
    BlockMetadata[BlockIndex].Y = Y;
    BlockMetadata[BlockIndex].Z = Z;
    
    return Ptr;
}

bool FSVOAllocator::GetBlockPosition(const void* Ptr, uint32& OutX, uint32& OutY, uint32& OutZ) const
{
    if (!Ptr || !bIsInitialized)
    {
        return false;
    }
    
    FScopeLock Lock(&PoolLock);
    
    // Calculate block index from pointer
    const uint8* BytePtr = static_cast<const uint8*>(Ptr);
    ptrdiff_t Offset = BytePtr - PoolMemory;
    
    if (Offset < 0 || Offset % BlockSize != 0 || Offset >= static_cast<ptrdiff_t>(CurrentBlockCount * BlockSize))
    {
        // Pointer is outside the pool or not aligned with block boundaries
        return false;
    }
    
    uint32 BlockIndex = static_cast<uint32>(Offset / BlockSize);
    
    if (!BlockMetadata[BlockIndex].bAllocated)
    {
        return false;
    }
    
    OutX = BlockMetadata[BlockIndex].X;
    OutY = BlockMetadata[BlockIndex].Y;
    OutZ = BlockMetadata[BlockIndex].Z;
    
    return true;
}

uint32 FSVOAllocator::PreallocateRegion(uint32 X, uint32 Y, uint32 Z, uint32 Radius)
{
    FScopeLock Lock(&PoolLock);
    
    if (!bIsInitialized)
    {
        return 0;
    }
    
    uint32 BlocksAllocated = 0;
    uint32 RadiusSquared = Radius * Radius;
    
    // Simple implementation: allocate blocks in a cubic region
    for (uint32 OffsetX = 0; OffsetX <= Radius; ++OffsetX)
    {
        for (uint32 OffsetY = 0; OffsetY <= Radius; ++OffsetY)
        {
            for (uint32 OffsetZ = 0; OffsetZ <= Radius; ++OffsetZ)
            {
                // Check if point is within sphere
                if (OffsetX * OffsetX + OffsetY * OffsetY + OffsetZ * OffsetZ <= RadiusSquared)
                {
                    // Allocate positive octant
                    if (AllocateAtPosition(X + OffsetX, Y + OffsetY, Z + OffsetZ, nullptr, NAME_None))
                    {
                        BlocksAllocated++;
                    }
                    
                    // Allocate other octants if not at origin of offset
                    if (OffsetX > 0)
                    {
                        if (AllocateAtPosition(X - OffsetX, Y + OffsetY, Z + OffsetZ, nullptr, NAME_None))
                        {
                            BlocksAllocated++;
                        }
                    }
                    
                    if (OffsetY > 0)
                    {
                        if (AllocateAtPosition(X + OffsetX, Y - OffsetY, Z + OffsetZ, nullptr, NAME_None))
                        {
                            BlocksAllocated++;
                        }
                    }
                    
                    if (OffsetZ > 0)
                    {
                        if (AllocateAtPosition(X + OffsetX, Y + OffsetY, Z - OffsetZ, nullptr, NAME_None))
                        {
                            BlocksAllocated++;
                        }
                    }
                    
                    if (OffsetX > 0 && OffsetY > 0)
                    {
                        if (AllocateAtPosition(X - OffsetX, Y - OffsetY, Z + OffsetZ, nullptr, NAME_None))
                        {
                            BlocksAllocated++;
                        }
                    }
                    
                    if (OffsetX > 0 && OffsetZ > 0)
                    {
                        if (AllocateAtPosition(X - OffsetX, Y + OffsetY, Z - OffsetZ, nullptr, NAME_None))
                        {
                            BlocksAllocated++;
                        }
                    }
                    
                    if (OffsetY > 0 && OffsetZ > 0)
                    {
                        if (AllocateAtPosition(X + OffsetX, Y - OffsetY, Z - OffsetZ, nullptr, NAME_None))
                        {
                            BlocksAllocated++;
                        }
                    }
                    
                    if (OffsetX > 0 && OffsetY > 0 && OffsetZ > 0)
                    {
                        if (AllocateAtPosition(X - OffsetX, Y - OffsetY, Z - OffsetZ, nullptr, NAME_None))
                        {
                            BlocksAllocated++;
                        }
                    }
                }
            }
        }
    }
    
    return BlocksAllocated;
}

bool FSVOAllocator::AllocatePoolMemory(uint32 InBlockCount)
{
    if (InBlockCount == 0)
    {
        return false;
    }
    
    // Allocate memory for the pool
    PoolMemory = static_cast<uint8*>(FMemory::Malloc(InBlockCount * BlockSize, 16));
    
    if (!PoolMemory)
    {
        return false;
    }
    
    // Initialize block metadata
    BlockMetadata.SetNum(InBlockCount);
    FreeBlocks.Empty(InBlockCount);
    
    for (uint32 i = 0; i < InBlockCount; ++i)
    {
        BlockMetadata[i].bAllocated = false;
        BlockMetadata[i].AllocationTag = NAME_None;
        BlockMetadata[i].RequestingObject = nullptr;
        BlockMetadata[i].X = 0;
        BlockMetadata[i].Y = 0;
        BlockMetadata[i].Z = 0;
        
        // Add to free list
        FreeBlocks.Add(i);
    }
    
    CurrentBlockCount = InBlockCount;
    
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
    // Count allocated and free blocks
    uint32 AllocatedBlocks = 0;
    for (uint32 i = 0; i < CurrentBlockCount; ++i)
    {
        if (BlockMetadata[i].bAllocated)
        {
            AllocatedBlocks++;
        }
    }
    
    uint32 FreeBlockCount = FreeBlocks.Num();
    
    // Calculate fragmentation
    float FragmentationPercent = 0.0f;
    if (CurrentBlockCount > 0)
    {
        // Simple fragmentation metric: how consecutive are the allocated blocks?
        uint32 Runs = 0;
        bool bLastAllocated = false;
        
        for (uint32 i = 0; i < CurrentBlockCount; ++i)
        {
            bool bCurrentAllocated = BlockMetadata[i].bAllocated;
            if (bCurrentAllocated != bLastAllocated)
            {
                Runs++;
                bLastAllocated = bCurrentAllocated;
            }
        }
        
        // Perfect case: 2 runs (all allocated blocks together, all free blocks together)
        // Worst case: CurrentBlockCount runs (alternating allocated and free)
        if (Runs > 2 && CurrentBlockCount > 2)
        {
            FragmentationPercent = 100.0f * (static_cast<float>(Runs - 2) / static_cast<float>(CurrentBlockCount - 2));
        }
    }
    
    // Update cached stats
    CachedStats.PoolName = PoolName;
    CachedStats.BlockSize = BlockSize;
    CachedStats.BlockCount = CurrentBlockCount;
    CachedStats.AllocatedBlocks = AllocatedBlocks;
    CachedStats.FreeBlocks = FreeBlockCount;
    CachedStats.PeakAllocatedBlocks = FMath::Max(CachedStats.PeakAllocatedBlocks, AllocatedBlocks);
    CachedStats.bAllowsGrowth = bAllowsGrowth;
    CachedStats.GrowthCount = CurrentBlockCount > MaxBlockCount ? 1 : 0;
    CachedStats.OverheadBytes = sizeof(FSVOAllocator) + BlockMetadata.GetAllocatedSize() + FreeBlocks.GetAllocatedSize();
    CachedStats.FragmentationPercent = FragmentationPercent;
    
    bStatsDirty = false;
}

uint32 FSVOAllocator::DefaultZOrderMapping(uint32 X, uint32 Y, uint32 Z)
{
    // Simple Z-order curve (Morton code)
    // Interleave bits of x, y, z coordinates
    uint32 Result = 0;
    for (uint32 i = 0; i < 10; ++i)  // Support up to 10 bits per coordinate
    {
        Result |= ((X & (1 << i)) << (2 * i)) |
                  ((Y & (1 << i)) << (2 * i + 1)) |
                  ((Z & (1 << i)) << (2 * i + 2));
    }
    return Result;
}