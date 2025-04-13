// NarrowBandAllocator.cpp
// Specialized memory allocator for narrow-band SDF

#include "25_SvoSdfVolume/Public/NarrowBandAllocator.h"
#include "2_MemoryManagement/Public/IMemoryManager.h"
#include "1_Core/Public/ServiceLocator.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformMath.h"
#include "Math/Box.h"
#include "Math/Vector.h"
#include "Containers/StaticArray.h"

FNarrowBandAllocator::FNarrowBandAllocator()
    : BlockSize(0)
    , MaxMaterials(0)
    , MemoryTelemetry(nullptr)
    , ScratchBufferSize(0)
    , TotalAllocatedBytes(0)
    , TotalUsedBytes(0)
    , bEnableBlockReuse(true)
{
    // Initialize internal state
}

FNarrowBandAllocator::~FNarrowBandAllocator()
{
    // Clean up memory
    ReleaseAllMemory();
}

void FNarrowBandAllocator::Initialize(uint32 InBlockSize, uint8 InMaxMaterials)
{
    BlockSize = FMath::Max(InBlockSize, 1024u); // Minimum 1KB block size
    MaxMaterials = FMath::Min(InMaxMaterials, (uint8)255); // Maximum 255 materials
    
    // Initialize the block pools for each material
    for (uint8 MaterialIndex = 0; MaterialIndex < MaxMaterials; ++MaterialIndex)
    {
        if (!MaterialBlockPools.Contains(MaterialIndex))
        {
            FMaterialBlockPool NewPool;
            NewPool.FreeBlocks.Empty();
            NewPool.UsedBlocks.Empty();
            NewPool.TotalBlocks = 0;
            NewPool.Priority = 0;
            MaterialBlockPools.Add(MaterialIndex, NewPool);
        }
    }
    
    // Create scratch buffer for temporary operations
    ScratchBufferSize = BlockSize * 4; // 4 blocks worth of scratch space
    ScratchBuffer.SetNum(ScratchBufferSize);
    
    // Set up memory tracking
    InitializeMemoryTracking();
}

void FNarrowBandAllocator::SetMemoryTelemetry(FMemoryTelemetry* InMemoryTelemetry)
{
    MemoryTelemetry = InMemoryTelemetry;
}

void* FNarrowBandAllocator::Allocate(uint32 Size, uint8 MaterialIndex, uint8 Priority)
{
    if (MaterialIndex >= MaxMaterials)
    {
        UE_LOG(LogTemp, Error, TEXT("NarrowBandAllocator: Invalid material index %d (max: %d)"), 
               MaterialIndex, MaxMaterials - 1);
        return nullptr;
    }
    
    // Calculate number of blocks needed
    uint32 NumBlocks = (Size + BlockSize - 1) / BlockSize;
    
    // Check if we have a free block to reuse
    void* Result = nullptr;
    bool bBlockReused = false;
    
    if (bEnableBlockReuse && NumBlocks == 1)
    {
        Result = AllocateFromFreeBlocks(MaterialIndex, Priority);
        bBlockReused = (Result != nullptr);
    }
    
    // If no free block available, allocate a new one
    if (!Result)
    {
        Result = AllocateNewBlocks(NumBlocks, MaterialIndex, Priority);
    }
    
    if (Result)
    {
        // Update memory tracking
        TotalUsedBytes += (NumBlocks * BlockSize);
        
        if (MemoryTelemetry)
        {
            // Track memory allocation
            if (bBlockReused)
            {
                MemoryTelemetry->TrackMemoryReused(MaterialIndex, NumBlocks * BlockSize);
            }
            else
            {
                MemoryTelemetry->TrackMemoryAllocated(MaterialIndex, NumBlocks * BlockSize);
            }
        }
    }
    
    return Result;
}

void FNarrowBandAllocator::Free(void* Ptr, uint32 Size, uint8 MaterialIndex)
{
    if (!Ptr || MaterialIndex >= MaxMaterials)
    {
        return;
    }
    
    // Find the block in the used blocks
    FMaterialBlockPool& Pool = MaterialBlockPools[MaterialIndex];
    FMemoryBlockInfo* BlockInfo = nullptr;
    
    for (auto& Block : Pool.UsedBlocks)
    {
        if (Block.StartAddress == Ptr)
        {
            BlockInfo = &Block;
            break;
        }
    }
    
    if (!BlockInfo)
    {
        UE_LOG(LogTemp, Warning, TEXT("NarrowBandAllocator: Attempt to free unallocated block"));
        return;
    }
    
    // Calculate number of blocks
    uint32 NumBlocks = (Size + BlockSize - 1) / BlockSize;
    
    // Update memory tracking
    TotalUsedBytes -= (NumBlocks * BlockSize);
    
    if (MemoryTelemetry)
    {
        MemoryTelemetry->TrackMemoryFreed(MaterialIndex, NumBlocks * BlockSize);
    }
    
    // Check if we should keep the block for reuse
    if (bEnableBlockReuse && NumBlocks == 1)
    {
        // Move to free blocks
        Pool.FreeBlocks.Add(*BlockInfo);
        Pool.UsedBlocks.RemoveSwap(*BlockInfo);
    }
    else
    {
        // Release the memory
        FMemory::Free(BlockInfo->StartAddress);
        Pool.UsedBlocks.RemoveSwap(*BlockInfo);
        Pool.TotalBlocks -= NumBlocks;
        
        TotalAllocatedBytes -= (NumBlocks * BlockSize);
    }
}

void FNarrowBandAllocator::SetMaterialPriority(uint8 MaterialIndex, uint8 Priority)
{
    if (MaterialIndex < MaxMaterials && MaterialBlockPools.Contains(MaterialIndex))
    {
        MaterialBlockPools[MaterialIndex].Priority = Priority;
    }
}

void FNarrowBandAllocator::PrioritizeRegion(const FBox& Region, uint8 Priority)
{
    // Implementation would track which blocks are in the region and adjust their priorities
    // This would involve spatial tracking of memory blocks, which is not implemented in this simplified version
}

void FNarrowBandAllocator::CompactMemory()
{
    // For each material pool
    for (auto& Pair : MaterialBlockPools)
    {
        FMaterialBlockPool& Pool = Pair.Value;
        
        // If free blocks exceed used blocks by a certain threshold, start releasing them
        if (Pool.FreeBlocks.Num() > Pool.UsedBlocks.Num() * 2 && Pool.FreeBlocks.Num() > 10)
        {
            // Sort free blocks by address for better contiguity after compaction
            Pool.FreeBlocks.Sort([](const FMemoryBlockInfo& A, const FMemoryBlockInfo& B) {
                return A.StartAddress < B.StartAddress;
            });
            
            // Determine how many blocks to keep (at least a few for future allocations)
            int32 BlocksToKeep = FMath::Max(5, Pool.UsedBlocks.Num() / 2);
            int32 BlocksToRemove = Pool.FreeBlocks.Num() - BlocksToKeep;
            
            if (BlocksToRemove > 0)
            {
                // Release the excess blocks
                for (int32 i = 0; i < BlocksToRemove; ++i)
                {
                    FMemory::Free(Pool.FreeBlocks[i].StartAddress);
                    Pool.TotalBlocks--;
                    TotalAllocatedBytes -= BlockSize;
                }
                
                // Keep the remaining blocks
                Pool.FreeBlocks.RemoveAt(0, BlocksToRemove);
            }
        }
    }
}

void FNarrowBandAllocator::ReleaseUnusedMemory()
{
    // For each material pool
    for (auto& Pair : MaterialBlockPools)
    {
        FMaterialBlockPool& Pool = Pair.Value;
        
        // Release all free blocks
        for (auto& Block : Pool.FreeBlocks)
        {
            FMemory::Free(Block.StartAddress);
            Pool.TotalBlocks--;
            TotalAllocatedBytes -= BlockSize;
        }
        
        Pool.FreeBlocks.Empty();
    }
}

void FNarrowBandAllocator::ReleaseAllMemory()
{
    // For each material pool
    for (auto& Pair : MaterialBlockPools)
    {
        FMaterialBlockPool& Pool = Pair.Value;
        
        // Free all blocks (both used and free)
        for (auto& Block : Pool.UsedBlocks)
        {
            FMemory::Free(Block.StartAddress);
        }
        
        for (auto& Block : Pool.FreeBlocks)
        {
            FMemory::Free(Block.StartAddress);
        }
        
        Pool.UsedBlocks.Empty();
        Pool.FreeBlocks.Empty();
        Pool.TotalBlocks = 0;
    }
    
    // Reset tracking
    TotalAllocatedBytes = 0;
    TotalUsedBytes = 0;
}

bool FNarrowBandAllocator::IsAddressInNarrowBand(const void* Ptr) const
{
    // Check if this address is managed by us
    for (const auto& Pair : MaterialBlockPools)
    {
        const FMaterialBlockPool& Pool = Pair.Value;
        
        // Check used blocks
        for (const auto& Block : Pool.UsedBlocks)
        {
            if (Ptr >= Block.StartAddress && 
                Ptr < static_cast<uint8*>(Block.StartAddress) + Block.SizeInBytes)
            {
                return true;
            }
        }
        
        // Check free blocks
        for (const auto& Block : Pool.FreeBlocks)
        {
            if (Ptr >= Block.StartAddress && 
                Ptr < static_cast<uint8*>(Block.StartAddress) + Block.SizeInBytes)
            {
                return true;
            }
        }
    }
    
    return false;
}

void* FNarrowBandAllocator::GetScratchBuffer(uint32 RequiredSize)
{
    // Ensure scratch buffer is large enough
    if (RequiredSize > ScratchBufferSize)
    {
        // Reallocate with some extra room for future growth
        ScratchBufferSize = FMath::Max(RequiredSize, ScratchBufferSize * 2);
        ScratchBuffer.SetNum(ScratchBufferSize);
    }
    
    return ScratchBuffer.GetData();
}

uint64 FNarrowBandAllocator::GetTotalAllocatedBytes() const
{
    return TotalAllocatedBytes;
}

uint64 FNarrowBandAllocator::GetTotalUsedBytes() const
{
    return TotalUsedBytes;
}

uint64 FNarrowBandAllocator::GetTotalFreeBytes() const
{
    return TotalAllocatedBytes - TotalUsedBytes;
}

uint32 FNarrowBandAllocator::GetTotalBlockCount() const
{
    uint32 TotalBlocks = 0;
    
    for (const auto& Pair : MaterialBlockPools)
    {
        TotalBlocks += Pair.Value.TotalBlocks;
    }
    
    return TotalBlocks;
}

FNarrowBandMemoryStats FNarrowBandAllocator::GetMemoryStats() const
{
    FNarrowBandMemoryStats Stats;
    
    Stats.TotalAllocatedBytes = TotalAllocatedBytes;
    Stats.TotalUsedBytes = TotalUsedBytes;
    Stats.TotalFreeBytes = GetTotalFreeBytes();
    Stats.BlockSize = BlockSize;
    Stats.MaxMaterials = MaxMaterials;
    Stats.TotalBlocks = GetTotalBlockCount();
    
    // Calculate material-specific stats
    for (const auto& Pair : MaterialBlockPools)
    {
        uint8 MaterialIndex = Pair.Key;
        const FMaterialBlockPool& Pool = Pair.Value;
        
        FMaterialMemoryStats MaterialStats;
        MaterialStats.MaterialIndex = MaterialIndex;
        MaterialStats.UsedBlockCount = Pool.UsedBlocks.Num();
        MaterialStats.FreeBlockCount = Pool.FreeBlocks.Num();
        MaterialStats.TotalBlockCount = Pool.TotalBlocks;
        MaterialStats.Priority = Pool.Priority;
        MaterialStats.AllocatedBytes = Pool.TotalBlocks * BlockSize;
        MaterialStats.UsedBytes = Pool.UsedBlocks.Num() * BlockSize;
        MaterialStats.FreeBytes = Pool.FreeBlocks.Num() * BlockSize;
        
        Stats.MaterialStats.Add(MaterialStats);
    }
    
    return Stats;
}

void FNarrowBandAllocator::EnableBlockReuse(bool bEnable)
{
    bEnableBlockReuse = bEnable;
    
    // If disabling block reuse, release all free blocks
    if (!bEnableBlockReuse)
    {
        ReleaseUnusedMemory();
    }
}

// Private implementation methods

void* FNarrowBandAllocator::AllocateFromFreeBlocks(uint8 MaterialIndex, uint8 Priority)
{
    if (!MaterialBlockPools.Contains(MaterialIndex))
    {
        return nullptr;
    }
    
    FMaterialBlockPool& Pool = MaterialBlockPools[MaterialIndex];
    
    if (Pool.FreeBlocks.Num() == 0)
    {
        return nullptr;
    }
    
    // Get a block from the free list
    FMemoryBlockInfo BlockInfo = Pool.FreeBlocks.Pop();
    
    // Update the block info with new priority
    BlockInfo.Priority = Priority;
    
    // Move to used blocks
    Pool.UsedBlocks.Add(BlockInfo);
    
    return BlockInfo.StartAddress;
}

void* FNarrowBandAllocator::AllocateNewBlocks(uint32 NumBlocks, uint8 MaterialIndex, uint8 Priority)
{
    if (!MaterialBlockPools.Contains(MaterialIndex))
    {
        return nullptr;
    }
    
    // Allocate memory with appropriate alignment
    void* NewMemory = FMemory::Malloc(NumBlocks * BlockSize, 16); // 16-byte alignment for SIMD
    
    if (!NewMemory)
    {
        UE_LOG(LogTemp, Error, TEXT("NarrowBandAllocator: Failed to allocate %d blocks for material %d"),
               NumBlocks, MaterialIndex);
        return nullptr;
    }
    
    // Create block info
    FMemoryBlockInfo BlockInfo;
    BlockInfo.StartAddress = NewMemory;
    BlockInfo.SizeInBytes = NumBlocks * BlockSize;
    BlockInfo.NumBlocks = NumBlocks;
    BlockInfo.Priority = Priority;
    BlockInfo.MaterialIndex = MaterialIndex;
    BlockInfo.Timestamp = FPlatformTime::Seconds();
    
    // Add to used blocks
    FMaterialBlockPool& Pool = MaterialBlockPools[MaterialIndex];
    Pool.UsedBlocks.Add(BlockInfo);
    Pool.TotalBlocks += NumBlocks;
    
    // Update tracking
    TotalAllocatedBytes += (NumBlocks * BlockSize);
    
    return NewMemory;
}

void FNarrowBandAllocator::InitializeMemoryTracking()
{
    // Register with memory manager if we have one
    auto* MemoryManager = IServiceLocator::Get().ResolveService<IMemoryManager>();
    if (MemoryManager)
    {
        MemoryManager->RegisterMemoryAllocator("NarrowBandAllocator", this);
    }
}