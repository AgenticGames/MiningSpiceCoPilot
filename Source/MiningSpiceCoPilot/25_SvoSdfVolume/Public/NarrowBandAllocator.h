// NarrowBandAllocator.h
// Specialized memory allocator for narrow band SDF representation

#pragma once

#include "CoreMinimal.h"
#include "Containers/BinaryHeap.h"
#include "HAL/ThreadSafeCounter.h"
#include "HAL/ThreadSafeBool.h"

/**
 * Specialized memory allocator for narrow band SDF representation
 * Provides efficient memory management for the active region around material interfaces
 * Uses block-based allocation to reduce fragmentation and overhead
 */
class MININGSPICECOPILOT_API FNarrowBandAllocator
{
public:
    FNarrowBandAllocator();
    ~FNarrowBandAllocator();

    // Allocation strategies
    enum class EAllocationStrategy : uint8
    {
        Fixed,      // Fixed block size
        Variable,   // Variable block sizing based on distance
        Adaptive,   // Adaptive sizing based on usage patterns
        Pooled      // Pre-allocated pools of common sizes
    };

    // Initialization
    void Initialize(uint32 InitialCapacity, uint32 BlockSize, EAllocationStrategy Strategy);
    
    // Memory allocation
    void* AllocateBlock(uint32 Size);
    void* AllocateAlignedBlock(uint32 Size, uint32 Alignment);
    void* AllocateInNarrowBand(const FVector& Position, float Distance, uint32 Size);
    void ReleaseBlock(void* Block);
    
    // Block management
    bool ResizeBlock(void* Block, uint32 NewSize);
    bool IsInNarrowBand(const FVector& Position, float Distance) const;
    uint32 GetBlockSize(void* Block) const;
    
    // Narrow band management
    void SetNarrowBandThickness(float Thickness);
    float GetNarrowBandThickness() const;
    void UpdateNarrowBand(const FVector& Center, float Radius);
    void OptimizeNarrowBandCoverage();
    
    // Memory management
    void Defragment();
    void ReleaseUnusedMemory();
    uint64 GetAllocatedMemory() const;
    uint64 GetUsedMemory() const;
    float GetFragmentationRatio() const;
    
    // Thread safety
    void LockAllocator();
    void UnlockAllocator();
    bool TryLockAllocator();

private:
    // Internal data structures
    struct FMemoryBlock;
    struct FMemoryChunk;
    struct FNarrowBandRegion;
    
    // Implementation details
    TArray<FMemoryChunk> MemoryChunks;
    TBinaryHeap<FMemoryBlock*, TLess<FMemoryBlock*>> FreeBlocks;
    TArray<FNarrowBandRegion> ActiveRegions;
    EAllocationStrategy AllocationStrategy;
    uint32 DefaultBlockSize;
    float NarrowBandThickness;
    FCriticalSection AllocationLock;
    FThreadSafeCounter AllocatedBlockCount;
    FThreadSafeCounter AllocatedMemorySize;
    
    // Helper methods
    FMemoryChunk* AllocateNewChunk(uint32 ChunkSize);
    void SplitBlock(FMemoryBlock& Block, uint32 Size);
    void MergeAdjacentBlocks();
    bool IsBlockInNarrowBand(const FMemoryBlock& Block) const;
    void UpdateAllocationStrategy(float UsageRatio);
    uint32 CalculateOptimalBlockSize(uint32 RequestedSize) const;
};