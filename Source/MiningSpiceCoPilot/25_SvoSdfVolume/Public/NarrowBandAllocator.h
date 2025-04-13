// NarrowBandAllocator.h
// Specialized memory allocator for narrow-band SDF field data around material interfaces

#pragma once

#include "CoreMinimal.h"
#include "Math/Box.h"
#include "Containers/Map.h"
#include "Containers/Array.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/ThreadSafeCounter.h"

// Forward declarations
class FMemoryTelemetry;

/**
 * Specialized memory allocator for narrow-band SDF field data around material interfaces
 * Provides efficient memory management with block-based allocation to reduce fragmentation
 * and support adaptive precision around material boundaries
 */
class MININGSPICECOPILOT_API FNarrowBandAllocator
{
public:
    FNarrowBandAllocator();
    ~FNarrowBandAllocator();

    /**
     * Configurable allocation block size and priority levels
     * Each block is divided into chunks for different precision levels
     */
    struct FAllocatorConfig
    {
        uint32 BlockSize;         // Size of memory blocks in bytes
        uint32 MinChunkSize;      // Minimum allocation chunk size
        uint32 MaxChunkSize;      // Maximum allocation chunk size
        uint32 PriorityLevels;    // Number of priority levels
        uint32 MaxBlocks;         // Maximum number of blocks to allocate

        // Constructor with default values
        FAllocatorConfig()
            : BlockSize(1024 * 1024)     // 1MB blocks
            , MinChunkSize(64)           // 64-byte minimum chunks
            , MaxChunkSize(4096)         // 4KB maximum chunks
            , PriorityLevels(8)          // 8 priority levels
            , MaxBlocks(64)              // 64 blocks maximum
        {}
    };

    // Block management
    void Initialize(uint32 BlockSizeInBytes = 1048576, uint32 MaxBlockCount = 64);
    void SetMemoryTelemetry(FMemoryTelemetry* InTelemetry);
    void SetAllocatorConfig(const FAllocatorConfig& Config);

    // Allocation and deallocation
    void* Allocate(uint32 SizeInBytes, uint8 PriorityLevel = 4);
    void* AllocateWithAlignment(uint32 SizeInBytes, uint32 Alignment, uint8 PriorityLevel = 4);
    void Free(void* Ptr);
    void* Reallocate(void* Ptr, uint32 NewSizeInBytes);

    // Region-based management
    void PrioritizeRegion(const FBox& Region, uint8 Priority);
    void DeprioritizeRegion(const FBox& Region);
    uint8 GetRegionPriority(const FBox& Region) const;

    // Memory optimization
    void CompactMemory();
    void ReleaseUnusedMemory();
    void DefragmentMemory();
    void OptimizeMemoryLayout();

    // Statistics and monitoring
    float GetFragmentationRatio() const;
    uint64 GetTotalAllocatedMemory() const;
    uint64 GetTotalUsedMemory() const;
    uint32 GetBlockCount() const;
    uint32 GetTotalAllocationCount() const;
    uint32 GetFreeChunkCount() const;
    
    // Debugging helpers
    void DumpAllocationMap() const;
    void ValidateMemoryBlocks() const;
    bool ContainsAddress(const void* Ptr) const;

private:
    // Memory block structure
    struct FMemoryBlock
    {
        uint8* Data;
        uint32 Size;
        uint32 Used;
        FThreadSafeBool InUse;

        FMemoryBlock()
            : Data(nullptr)
            , Size(0)
            , Used(0)
            , InUse(false)
        {}
    };

    // Memory chunk structure (subdivision of blocks)
    struct FMemoryChunk
    {
        uint32 BlockIndex;
        uint32 Offset;
        uint32 Size;
        uint8 Priority;
        bool IsAllocated;

        FMemoryChunk()
            : BlockIndex(UINT32_MAX)
            , Offset(0)
            , Size(0)
            , Priority(0)
            , IsAllocated(false)
        {}
    };

    // Region priority tracking
    struct FRegionPriority
    {
        FBox Region;
        uint8 Priority;
        double Timestamp;

        FRegionPriority()
            : Region(ForceInit)
            , Priority(0)
            , Timestamp(0)
        {}

        FRegionPriority(const FBox& InRegion, uint8 InPriority)
            : Region(InRegion)
            , Priority(InPriority)
            , Timestamp(FPlatformTime::Seconds())
        {}
    };

    // Internal state
    TArray<FMemoryBlock> Blocks;
    TMap<void*, FMemoryChunk> AllocatedChunks;
    TArray<FMemoryChunk> FreeChunks;
    TArray<FRegionPriority> PriorityRegions;
    FAllocatorConfig Config;
    FThreadSafeCounter TotalAllocations;
    FMemoryTelemetry* MemoryTelemetry;

    // Internal helpers
    FMemoryBlock* AllocateNewBlock();
    void ReleaseBlock(uint32 BlockIndex);
    void SplitChunk(uint32 ChunkIndex, uint32 SizeNeeded);
    void MergeAdjacentChunks();
    uint8 GetPriorityForLocation(const FBox& LocationBox) const;
    bool IsChunkInRegion(const FMemoryChunk& Chunk, const FBox& Region) const;
    int32 FindSuitableChunk(uint32 SizeInBytes, uint32 Alignment, uint8 Priority) const;
    
    // Memory tracking
    void TrackAllocation(uint32 Size);
    void TrackDeallocation(uint32 Size);
    
    // Thread safety
    FCriticalSection AllocationLock;
};