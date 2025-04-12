// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Math/Box.h"
#include "Containers/Map.h"

/**
 * Spatial hashing system for efficient querying of objects in 3D space
 * Uses box regions for spatial partitioning and efficient collision detection
 */
class MININGSPICECOPILOT_API FBoxHash
{
public:
    FBoxHash();
    ~FBoxHash();
    
    /**
     * Initialize the box hash with the given cell size
     * @param InWorldBounds Overall bounds of the world space
     * @param InCellSize Size of each cell in the hash grid
     */
    void Initialize(const FBox& InWorldBounds, float InCellSize);
    
    /**
     * Insert an object into the spatial hash
     * @param ObjectId Unique identifier for the object
     * @param ObjectBounds Bounds of the object in world space
     */
    void InsertObject(uint32 ObjectId, const FBox& ObjectBounds);
    
    /**
     * Update an existing object's position in the hash
     * @param ObjectId Unique identifier for the object
     * @param NewObjectBounds New bounds of the object in world space
     */
    void UpdateObject(uint32 ObjectId, const FBox& NewObjectBounds);
    
    /**
     * Remove an object from the spatial hash
     * @param ObjectId Unique identifier for the object
     * @return True if the object was successfully removed
     */
    bool RemoveObject(uint32 ObjectId);
    
    /**
     * Query objects that potentially intersect with a given box
     * @param QueryBox Box to query in world space
     * @param OutObjectIds Array to fill with potentially intersecting object IDs
     */
    void QueryObjects(const FBox& QueryBox, TArray<uint32>& OutObjectIds) const;
    
    /**
     * Query objects that potentially intersect with a given sphere
     * @param SphereCenter Center of the sphere in world space
     * @param SphereRadius Radius of the sphere
     * @param OutObjectIds Array to fill with potentially intersecting object IDs
     */
    void QueryObjects(const FVector& SphereCenter, float SphereRadius, TArray<uint32>& OutObjectIds) const;
    
    /**
     * Clear all objects from the hash
     */
    void Clear();
    
private:
    // Internal implementation details
    struct FCellCoord
    {
        int32 X;
        int32 Y;
        int32 Z;
        
        bool operator==(const FCellCoord& Other) const
        {
            return X == Other.X && Y == Other.Y && Z == Other.Z;
        }
        
        friend uint32 GetTypeHash(const FCellCoord& Coord)
        {
            return HashCombine(HashCombine(GetTypeHash(Coord.X), GetTypeHash(Coord.Y)), GetTypeHash(Coord.Z));
        }
    };
    
    // Convert world position to cell coordinate
    FCellCoord WorldToCell(const FVector& WorldPos) const;
    
    // Get cells that a box overlaps
    void GetOverlappingCells(const FBox& Box, TArray<FCellCoord>& OutCells) const;
    
    // The world bounds the hash covers
    FBox WorldBounds;
    
    // Size of each cell in the hash grid
    float CellSize;
    
    // Mapping from cell coordinates to object IDs
    TMap<FCellCoord, TSet<uint32>> CellToObjects;
    
    // Mapping from object IDs to occupied cells
    TMap<uint32, TSet<FCellCoord>> ObjectToCells;
    
    // Mapping from object IDs to their bounds
    TMap<uint32, FBox> ObjectBounds;
};