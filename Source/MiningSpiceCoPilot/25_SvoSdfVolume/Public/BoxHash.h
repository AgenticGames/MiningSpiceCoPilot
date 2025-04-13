// BoxHash.h
// Spatial hashing system for efficient region-based lookups and collision detection

#pragma once

#include "CoreMinimal.h"
#include "Math/Vector.h"
#include "Math/Box.h"
#include "Containers/Map.h"
#include "Containers/Set.h"

/**
 * Spatial hashing system for efficient region-based lookups and collision detection
 * Provides O(1) lookup for regions and efficiently finds overlapping regions
 * Used by the NetworkVolumeCoordinator for detecting region conflicts
 * and by other components for spatial queries
 */
class MININGSPICECOPILOT_API FBoxHash
{
public:
    // Constructor
    FBoxHash(float InCellSize = 100.0f);
    
    // Cell size configuration
    void SetCellSize(float NewCellSize);
    float GetCellSize() const { return CellSize; }
    
    // Region registration operations
    void RegisterBox(const FBox& Box, uint64 Id);
    void UpdateBox(const FBox& OldBox, const FBox& NewBox, uint64 Id);
    void UnregisterBox(const FBox& Box, uint64 Id);
    void UnregisterBoxById(uint64 Id);
    
    // Query operations
    bool IsBoxRegistered(uint64 Id) const;
    TArray<uint64> GetOverlappingBoxes(const FBox& QueryBox) const;
    TArray<uint64> GetBoxesInRegion(const FBox& Region) const;
    bool HasOverlap(const FBox& QueryBox) const;
    
    // Cell management
    void OptimizeCellSize();
    void Clear();
    
    // Statistics and debug
    uint32 GetBoxCount() const;
    uint32 GetCellCount() const;
    float GetAverageBoxesPerCell() const;
    TArray<FBox> GetAllBoxes() const;
    
private:
    // Cell addressing
    struct FCellCoord
    {
        int32 X;
        int32 Y;
        int32 Z;
        
        // Constructor
        FCellCoord(int32 InX = 0, int32 InY = 0, int32 InZ = 0)
            : X(InX), Y(InY), Z(InZ)
        {}
        
        // Equality operators for container compatibility
        bool operator==(const FCellCoord& Other) const
        {
            return X == Other.X && Y == Other.Y && Z == Other.Z;
        }
        
        // Hash function for containers
        friend uint32 GetTypeHash(const FCellCoord& Coord)
        {
            return HashCombine(HashCombine(GetTypeHash(Coord.X), GetTypeHash(Coord.Y)), GetTypeHash(Coord.Z));
        }
    };
    
    // Convert world position to cell coordinates
    FCellCoord WorldToCell(const FVector& WorldPos) const;
    
    // Get all cell coordinates that a box overlaps
    TArray<FCellCoord> GetOverlappedCells(const FBox& Box) const;
    
    // Cell resolution
    float CellSize;
    
    // Box storage
    TMap<uint64, FBox> BoxById;
    
    // Spatial hash
    TMap<FCellCoord, TSet<uint64>> CellToBoxes;
    
    // Reverse mapping for fast removal
    TMap<uint64, TSet<FCellCoord>> BoxToCells;
    
    // Internal helpers
    void AddBoxToCell(const FCellCoord& Cell, uint64 Id);
    void RemoveBoxFromCell(const FCellCoord& Cell, uint64 Id);
};

/**
 * Helper functions for BoxHash operations
 */
namespace BoxHashHelpers
{
    // Check if two boxes overlap
    inline bool BoxesOverlap(const FBox& BoxA, const FBox& BoxB)
    {
        return BoxA.Intersect(BoxB);
    }
    
    // Calculate optimal cell size for a set of boxes
    inline float CalculateOptimalCellSize(const TArray<FBox>& Boxes)
    {
        if (Boxes.Num() == 0)
        {
            return 100.0f; // Default cell size
        }
        
        // Find average box size
        float TotalSize = 0.0f;
        for (const FBox& Box : Boxes)
        {
            FVector Size = Box.GetSize();
            TotalSize += (Size.X + Size.Y + Size.Z) / 3.0f;
        }
        
        // Average box size is a reasonable cell size
        float AverageSize = TotalSize / Boxes.Num();
        
        // Clamp to reasonable range
        return FMath::Clamp(AverageSize, 10.0f, 1000.0f);
    }
    
    // Calculate density parameter for selecting cell size
    inline float CalculateSpatialDensity(const TArray<FBox>& Boxes, const FBox& TotalBounds)
    {
        if (Boxes.Num() <= 1 || TotalBounds.IsValid == false)
        {
            return 1.0f;
        }
        
        // Calculate total volume
        FVector BoundsSize = TotalBounds.GetSize();
        float TotalVolume = BoundsSize.X * BoundsSize.Y * BoundsSize.Z;
        
        // Calculate sum of box volumes
        float BoxVolume = 0.0f;
        for (const FBox& Box : Boxes)
        {
            FVector Size = Box.GetSize();
            BoxVolume += Size.X * Size.Y * Size.Z;
        }
        
        // Density is ratio of occupied to total volume (clamped to reasonable range)
        return FMath::Clamp(BoxVolume / TotalVolume, 0.01f, 1.0f);
    }
}