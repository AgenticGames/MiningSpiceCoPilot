// ZOrderCurve.h
// Cache-coherent Z-order curve implementation for spatial locality in memory

#pragma once

#include "CoreMinimal.h"
#include "Math/Vector.h"
#include "Math/IntVector.h"

/**
 * Z-order curve implementation for cache-coherent memory access patterns
 * Maps multi-dimensional coordinates to 1D space while preserving spatial locality
 * Used by the SVO system for optimal memory layout and traversal
 */
class MININGSPICECOPILOT_API FZOrderCurve
{
public:
    // Morton code generation (Z-order curve mapping)
    static uint64 MortonEncode(uint32 x, uint32 y, uint32 z);
    static uint64 MortonEncode(const FIntVector& Coords);
    
    // Morton code decoding (reverse mapping)
    static void MortonDecode(uint64 MortonCode, uint32& X, uint32& Y, uint32& Z);
    static FIntVector MortonDecode(uint64 MortonCode);
    
    // Neighborhood operations
    static uint64 GetNeighbor(uint64 MortonCode, int32 DeltaX, int32 DeltaY, int32 DeltaZ);
    static TArray<uint64> GetNeighbors(uint64 MortonCode, uint32 Radius = 1);
    
    // Z-order curve navigation
    static uint64 GetParent(uint64 MortonCode, uint32 Level);
    static uint64 GetChild(uint64 MortonCode, uint32 ChildIndex, uint32 Level);
    static TArray<uint64> GetChildren(uint64 MortonCode, uint32 Level);
    
    // Level operations
    static uint32 GetLevel(uint64 MortonCode);
    static uint64 SetLevel(uint64 MortonCode, uint32 Level);
    
    // Z-order curve properties
    static uint32 GetMaxCoordinate(uint32 Level);
    static bool IsAncestor(uint64 Ancestor, uint64 Descendant);
    static uint32 GetCommonAncestorLevel(uint64 CodeA, uint64 CodeB);
    
    // Spatial query operations
    static bool IsInBox(uint64 MortonCode, const FIntVector& Min, const FIntVector& Max);
    static TArray<uint64> GetCodesInBox(const FIntVector& Min, const FIntVector& Max, uint32 Level);
    
    // Bit manipulation utilities
    static uint32 SeparateBits3D(uint32 Value);
    static uint32 CompactBits3D(uint32 Value);
    
    // Helper methods for memory layout optimization
    static uint32 ReverseBits(uint32 Value);
    static uint64 ReverseBits64(uint64 Value);
    static uint32 InterleaveBits(uint32 ValueX, uint32 ValueY, uint32 ValueZ);
    
private:
    // Helper methods for bit operations
    static uint32 ExpandBits1(uint32 Value);
    static uint32 ExpandBits2(uint32 Value);
    static uint32 CompactBits1(uint32 Value);
    static uint32 CompactBits2(uint32 Value);
    
    // Constants for bit operations
    static const uint32 MORTON_X_MASK = 0x49249249;
    static const uint32 MORTON_Y_MASK = 0x92492492;
    static const uint32 MORTON_Z_MASK = 0x24924924;
};

/**
 * Helper class for node data memory layout based on Z-order curve
 * Provides memory addressing tools for cache-coherent access patterns
 */
class MININGSPICECOPILOT_API FNodeMemoryLayout
{
public:
    // Constructor
    FNodeMemoryLayout(uint32 InNodeSize = 64, uint32 InCacheLineSize = 64);
    
    // Configure memory layout
    void SetNodeSize(uint32 NewNodeSize);
    void SetCacheLineSize(uint32 NewCacheLineSize);
    
    // Memory mapping operations
    uint32 GetNodeAddress(uint64 MortonCode) const;
    uint32 GetNodeAddress(uint32 X, uint32 Y, uint32 Z, uint32 Level) const;
    
    // Optimized memory layouts
    void OptimizeForTraversal();
    void OptimizeForFieldEvaluation();
    void OptimizeForMaterialOperations();
    
    // Access pattern analysis
    void RecordAccess(uint64 MortonCode);
    void AnalyzeAccessPatterns();
    void ReportCacheMissEstimate();
    
    // Memory statistics
    uint32 GetNodeSize() const { return NodeSize; }
    uint32 GetCacheLineSize() const { return CacheLineSize; }
    float GetEstimatedCacheHitRate() const;
    
private:
    // Memory layout configuration
    uint32 NodeSize;
    uint32 CacheLineSize;
    
    // Memory addressing strategy
    enum class EAddressingMode
    {
        ZOrderCurve,
        HilbertCurve,
        ZOrderWithCacheLines,
        LevelByLevel
    };
    
    // Current memory layout strategy
    EAddressingMode AddressingMode;
    
    // Access pattern tracking
    TMap<uint64, uint32> AccessFrequency;
    TMap<uint32, uint32> CacheLineAccess;
    
    // Internal helpers
    uint32 ApplyAddressingMode(uint64 MortonCode) const;
    uint32 GetCacheLineIndex(uint32 Address) const;
};