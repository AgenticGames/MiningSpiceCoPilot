// ZOrderCurve.cpp
// Implementation of Z-order curve mapping for cache-coherent memory layout

#include "25_SvoSdfVolume/Public/ZOrderCurve.h"
#include "Math/IntVector.h"
#include "Math/Vector.h"
#include "Math/Box.h"
#include "HAL/PlatformMath.h"

namespace ZOrderCurveInternal
{
    // Helper functions for interleaving bits
    inline uint32 SplitBy3(uint32 x)
    {
        // Split bits to create gaps: 0b00010010 -> 0b000001000000010000
        x = (x | (x << 16)) & 0x030000FF;
        x = (x | (x << 8))  & 0x0300F00F;
        x = (x | (x << 4))  & 0x030C30C3;
        x = (x | (x << 2))  & 0x09249249;
        return x;
    }
    
    inline uint32 GetThirdBits(uint32 n)
    {
        // Extract every third bit: 0b010010010 -> 0b000000111
        return (n & 0x1) | ((n & 0x8) >> 2) | ((n & 0x40) >> 4) | ((n & 0x200) >> 6) | ((n & 0x1000) >> 8) |
               ((n & 0x8000) >> 10) | ((n & 0x40000) >> 12) | ((n & 0x200000) >> 14) | ((n & 0x1000000) >> 16) |
               ((n & 0x8000000) >> 18) | ((n & 0x40000000) >> 20);
    }
    
    inline uint32 DecodeMorton3X(uint32 code)
    {
        return GetThirdBits(code);
    }
    
    inline uint32 DecodeMorton3Y(uint32 code)
    {
        return GetThirdBits(code >> 1);
    }
    
    inline uint32 DecodeMorton3Z(uint32 code)
    {
        return GetThirdBits(code >> 2);
    }
}

uint32 ZOrderCurve::MortonEncode(uint32 x, uint32 y)
{
    // Interleave the bits of x and y to create a Z-order value
    return (ZOrderCurveInternal::SplitBy3(y) << 1) | ZOrderCurveInternal::SplitBy3(x);
}

uint32 ZOrderCurve::MortonEncode(uint32 x, uint32 y, uint32 z)
{
    // Interleave the bits of x, y, and z to create a Z-order value
    return (ZOrderCurveInternal::SplitBy3(z) << 2) | (ZOrderCurveInternal::SplitBy3(y) << 1) | ZOrderCurveInternal::SplitBy3(x);
}

void ZOrderCurve::MortonDecode(uint32 code, uint32& outX, uint32& outY)
{
    // Deinterleave the bits to extract X and Y components
    outX = code & 0x55555555;
    outX = (outX ^ (outX >> 1)) & 0x33333333;
    outX = (outX ^ (outX >> 2)) & 0x0F0F0F0F;
    outX = (outX ^ (outX >> 4)) & 0x00FF00FF;
    outX = (outX ^ (outX >> 8)) & 0x0000FFFF;
    
    outY = (code >> 1) & 0x55555555;
    outY = (outY ^ (outY >> 1)) & 0x33333333;
    outY = (outY ^ (outY >> 2)) & 0x0F0F0F0F;
    outY = (outY ^ (outY >> 4)) & 0x00FF00FF;
    outY = (outY ^ (outY >> 8)) & 0x0000FFFF;
}

void ZOrderCurve::MortonDecode(uint32 code, uint32& outX, uint32& outY, uint32& outZ)
{
    // Use specialized function for 3D deinterleaving
    outX = ZOrderCurveInternal::DecodeMorton3X(code);
    outY = ZOrderCurveInternal::DecodeMorton3Y(code);
    outZ = ZOrderCurveInternal::DecodeMorton3Z(code);
}

FIntVector ZOrderCurve::MortonDecode3D(uint32 code)
{
    uint32 x, y, z;
    MortonDecode(code, x, y, z);
    return FIntVector(x, y, z);
}

void ZOrderCurve::GenerateCurvePoints(int32 Level, TArray<FVector>& OutPoints)
{
    // Calculate number of points for this level
    int32 PointCount = 1 << (2 * Level); // 4^level points for 2D curve
    OutPoints.Empty(PointCount);
    
    // Scaling factor to normalize points to [0,1] range
    float Scale = 1.0f / (1 << Level);
    
    // Generate points
    for (uint32 i = 0; i < (uint32)PointCount; ++i)
    {
        uint32 x, y;
        MortonDecode(i, x, y);
        OutPoints.Add(FVector(x * Scale, y * Scale, 0.0f));
    }
}

void ZOrderCurve::GenerateCurvePoints3D(int32 Level, TArray<FVector>& OutPoints)
{
    // Calculate number of points for this level
    int32 PointCount = 1 << (3 * Level); // 8^level points for 3D curve
    OutPoints.Empty(PointCount);
    
    // Scaling factor to normalize points to [0,1] range
    float Scale = 1.0f / (1 << Level);
    
    // Generate points
    for (uint32 i = 0; i < (uint32)PointCount; ++i)
    {
        uint32 x, y, z;
        MortonDecode(i, x, y, z);
        OutPoints.Add(FVector(x * Scale, y * Scale, z * Scale));
    }
}

uint32 ZOrderCurve::MortonCodeFromCoordinates(const FVector& Position, const FBox& BoundingBox)
{
    // Get the box extent and min corner
    FVector Min = BoundingBox.Min;
    FVector Extent = BoundingBox.GetExtent() * 2.0f; // Full extent (not half-extent)
    
    // Normalize position to [0,1] range
    FVector NormalizedPos = (Position - Min) / Extent;
    
    // Clamp to [0,1] range
    NormalizedPos.X = FMath::Clamp(NormalizedPos.X, 0.0f, 1.0f);
    NormalizedPos.Y = FMath::Clamp(NormalizedPos.Y, 0.0f, 1.0f);
    NormalizedPos.Z = FMath::Clamp(NormalizedPos.Z, 0.0f, 1.0f);
    
    // Scale to integer range (10 bits per coordinate = 0-1023 range)
    const uint32 MaxCoord = 1024;
    uint32 X = (uint32)(NormalizedPos.X * (float)MaxCoord);
    uint32 Y = (uint32)(NormalizedPos.Y * (float)MaxCoord);
    uint32 Z = (uint32)(NormalizedPos.Z * (float)MaxCoord);
    
    // Clamp again to ensure we don't exceed maxCoord
    X = FMath::Min(X, MaxCoord - 1);
    Y = FMath::Min(Y, MaxCoord - 1);
    Z = FMath::Min(Z, MaxCoord - 1);
    
    // Compute Morton code
    return MortonEncode(X, Y, Z);
}

FVector ZOrderCurve::PositionFromMortonCode(uint32 MortonCode, const FBox& BoundingBox)
{
    // Decode the Morton code
    uint32 X, Y, Z;
    MortonDecode(MortonCode, X, Y, Z);
    
    // Normalize to [0,1] range (assuming 10 bits per coordinate)
    const float MaxCoord = 1024.0f;
    float NormX = (float)X / MaxCoord;
    float NormY = (float)Y / MaxCoord;
    float NormZ = (float)Z / MaxCoord;
    
    // Convert to actual position
    FVector Min = BoundingBox.Min;
    FVector Extent = BoundingBox.GetExtent() * 2.0f;
    
    return FVector(
        Min.X + NormX * Extent.X,
        Min.Y + NormY * Extent.Y,
        Min.Z + NormZ * Extent.Z
    );
}

void ZOrderCurve::SortVectorsByZOrder(TArray<FVector>& Vectors, const FBox& BoundingBox)
{
    // Create a map of Morton codes to vector indices
    TMap<uint32, int32> MortonMap;
    MortonMap.Reserve(Vectors.Num());
    
    // Compute Morton codes for each vector
    for (int32 i = 0; i < Vectors.Num(); ++i)
    {
        uint32 MortonCode = MortonCodeFromCoordinates(Vectors[i], BoundingBox);
        MortonMap.Add(MortonCode, i);
    }
    
    // Sort the Morton codes
    TArray<uint32> SortedCodes;
    MortonMap.GetKeys(SortedCodes);
    SortedCodes.Sort();
    
    // Create a sorted array of vectors
    TArray<FVector> SortedVectors;
    SortedVectors.Reserve(Vectors.Num());
    
    // Add vectors in Z-order
    for (uint32 Code : SortedCodes)
    {
        int32 Index = MortonMap[Code];
        SortedVectors.Add(Vectors[Index]);
    }
    
    // Replace the original array
    Vectors = MoveTemp(SortedVectors);
}

uint32 ZOrderCurve::ChildOffsetFromIndex(uint32 ChildIndex)
{
    // For octree: In Z-order, the child index directly maps to the interleaved coordinate bits
    // Children ordered [---,--Z,-Y-,-YZ,X--,X-Z,XY-,XYZ]
    return ChildIndex;
}

void ZOrderCurve::GetNeighborIndices(uint32 Index, TArray<uint32>& OutNeighbors)
{
    // Decode the morton code into 3D coordinates
    uint32 X, Y, Z;
    MortonDecode(Index, X, Y, Z);
    
    // Generate the 26 neighbors (considering boundary conditions)
    OutNeighbors.Empty(26);
    
    for (int32 dz = -1; dz <= 1; ++dz)
    {
        for (int32 dy = -1; dy <= 1; ++dy)
        {
            for (int32 dx = -1; dx <= 1; ++dx)
            {
                // Skip the center (original) cell
                if (dx == 0 && dy == 0 && dz == 0)
                {
                    continue;
                }
                
                // Calculate neighbor coordinates
                int32 nx = (int32)X + dx;
                int32 ny = (int32)Y + dy;
                int32 nz = (int32)Z + dz;
                
                // Check if valid (assuming 10-bit coordinates, max value 1023)
                if (nx >= 0 && nx < 1024 && ny >= 0 && ny < 1024 && nz >= 0 && nz < 1024)
                {
                    uint32 NeighborCode = MortonEncode((uint32)nx, (uint32)ny, (uint32)nz);
                    OutNeighbors.Add(NeighborCode);
                }
            }
        }
    }
}

uint32 ZOrderCurve::GetParentIndex(uint32 ChildIndex)
{
    // Isolate every 3rd bit (remove the lowest level from the code)
    uint32 X, Y, Z;
    MortonDecode(ChildIndex, X, Y, Z);
    
    // Shift right by 1 bit (equivalent to dividing by 2)
    X >>= 1;
    Y >>= 1;
    Z >>= 1;
    
    // Re-encode with the new coordinates
    return MortonEncode(X, Y, Z);
}

void ZOrderCurve::GetChildrenIndices(uint32 ParentIndex, TArray<uint32>& OutChildrenIndices)
{
    // Decode the parent morton code
    uint32 PX, PY, PZ;
    MortonDecode(ParentIndex, PX, PY, PZ);
    
    // Generate 8 children (multiply coordinates by 2 and add 0 or 1)
    OutChildrenIndices.Empty(8);
    PX <<= 1;
    PY <<= 1;
    PZ <<= 1;
    
    for (uint32 i = 0; i < 8; ++i)
    {
        uint32 CX = PX + ((i & 4) ? 1 : 0);
        uint32 CY = PY + ((i & 2) ? 1 : 0);
        uint32 CZ = PZ + ((i & 1) ? 1 : 0);
        
        OutChildrenIndices.Add(MortonEncode(CX, CY, CZ));
    }
}

bool ZOrderCurve::GetOctreeChildPosition(FVector ParentPosition, float ParentSize, uint32 ChildIndex, FVector& OutChildPosition)
{
    if (ChildIndex >= 8)
    {
        return false; // Invalid child index
    }
    
    // Child offset direction based on the child index bits
    float HalfSize = ParentSize * 0.5f;
    
    // Using the bit pattern of the child index to determine position offsets
    // XYZ bits where X = bit 2, Y = bit 1, Z = bit 0
    float OffsetX = (ChildIndex & 4) ? HalfSize : -HalfSize;
    float OffsetY = (ChildIndex & 2) ? HalfSize : -HalfSize;
    float OffsetZ = (ChildIndex & 1) ? HalfSize : -HalfSize;
    
    // Child position is parent position + offset
    OutChildPosition = ParentPosition + FVector(OffsetX, OffsetY, OffsetZ) * 0.5f;
    
    return true;
}

TArray<FIntVector> ZOrderCurve::DecomposeMortonCode(uint32 Code, int32 Levels)
{
    TArray<FIntVector> Result;
    Result.SetNum(Levels);
    
    // For each level, extract the bits corresponding to that level
    for (int32 Level = 0; Level < Levels; ++Level)
    {
        // Extract the bits for the current level
        uint32 LevelMask = 0x7 << (Level * 3); // 3 bits per level (x,y,z)
        uint32 LevelBits = (Code & LevelMask) >> (Level * 3);
        
        // Convert to coordinates
        int32 x = (LevelBits & 4) ? 1 : 0;
        int32 y = (LevelBits & 2) ? 1 : 0;
        int32 z = (LevelBits & 1) ? 1 : 0;
        
        Result[Level] = FIntVector(x, y, z);
    }
    
    return Result;
}