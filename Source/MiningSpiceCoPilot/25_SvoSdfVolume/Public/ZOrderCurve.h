// ZOrderCurve.h
// Z-order curve implementation for cache-coherent memory layout

#pragma once

#include "CoreMinimal.h"
#include "Math/IntVector.h"

/**
 * Z-order curve implementation for cache-coherent memory layout
 * Provides efficient spatial locality for improved cache usage in voxel operations
 * Supports mapping between 3D coordinates and linear memory indices
 */
class MININGSPICECOPILOT_API FZOrderCurve
{
public:
    FZOrderCurve();
    ~FZOrderCurve();

    // Initialization
    void Initialize(const FIntVector& Dimensions);
    
    // Coordinate mapping
    uint64 EncodePosition(const FIntVector& Position) const;
    uint64 EncodePosition(int32 X, int32 Y, int32 Z) const;
    FIntVector DecodePosition(uint64 Index) const;
    
    // Neighbor finding
    uint64 GetNeighborIndex(uint64 Index, int32 DX, int32 DY, int32 DZ) const;
    TArray<uint64> GetNeighborIndices(uint64 Index, int32 Radius) const;
    
    // Traversal
    uint64 GetNextIndex(uint64 CurrentIndex) const;
    uint64 GetPreviousIndex(uint64 CurrentIndex) const;
    TArray<uint64> GetIndicesInBox(const FIntVector& MinCoord, const FIntVector& MaxCoord) const;
    
    // Hierarchy support
    uint64 EncodePositionAtLevel(const FIntVector& Position, uint8 Level) const;
    FIntVector GetParentCellCoord(const FIntVector& Position, uint8 LevelDifference = 1) const;
    uint64 GetParentIndex(uint64 Index, uint8 LevelDifference = 1) const;
    TArray<uint64> GetChildIndices(uint64 ParentIndex, uint8 Level) const;
    
    // Range queries
    TArray<uint64> GetIndicesInRange(uint64 StartIndex, uint64 EndIndex) const;
    bool AreIndicesAdjacent(uint64 IndexA, uint64 IndexB) const;
    
    // Memory and cache optimization
    uint64 GetOptimalChunkSize() const;
    TArray<FIntVector> GetOptimizedTraversalPath(const FBox& Region) const;
    float EstimateCacheEfficiency(uint32 CacheLineSize) const;

private:
    // Internal implementation
    FIntVector Dimensions;
    uint8 MaxBitsPerDimension;
    
    // Helper methods
    uint64 Interleave3(uint32 X, uint32 Y, uint32 Z) const;
    void Deinterleave3(uint64 Index, uint32& OutX, uint32& OutY, uint32& OutZ) const;
    uint64 EncodeMorton3(uint32 X, uint32 Y, uint32 Z) const;
    void DecodeMorton3(uint64 Code, uint32& OutX, uint32& OutY, uint32& OutZ) const;
    bool IsValidPosition(const FIntVector& Position) const;
    uint64 GetLevelMask(uint8 Level) const;
    uint32 ExpandBits(uint32 Value) const;
    uint32 CompactBits(uint32 Value) const;
};