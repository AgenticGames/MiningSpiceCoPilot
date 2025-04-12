// MaterialSDFManager.h
// Multi-channel SDF management for material-specific distance fields

#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"
#include "Math/SIMDFloat.h"
#include "HAL/ThreadSafeCounter.h"

// Forward declarations
class FNarrowBandAllocator;

/**
 * Multi-channel SDF management for material-specific distance fields
 * Handles material boundaries with independent distance functions
 * Supports Boolean operations, gradient calculation, and narrow-band precision management
 */
class MININGSPICECOPILOT_API FMaterialSDFManager
{
public:
    FMaterialSDFManager();
    ~FMaterialSDFManager();

    // Initialization
    void Initialize(uint32 MaterialCount, float NarrowBandThickness);
    void SetNarrowBandAllocator(TSharedPtr<FNarrowBandAllocator> InAllocator);

    // Material channel operations
    void AddMaterialChannel(uint8 MaterialIndex);
    void RemoveMaterialChannel(uint8 MaterialIndex);
    bool HasMaterialChannel(uint8 MaterialIndex) const;
    uint32 GetMaterialChannelCount() const;
    TArray<uint8> GetActiveMaterialIndices() const;

    // SDF manipulation
    void SetDistanceValue(const FVector& WorldPosition, uint8 MaterialIndex, float Value);
    float GetDistanceValue(const FVector& WorldPosition, uint8 MaterialIndex) const;
    void ModifyDistanceField(const FVector& Center, float Radius, uint8 MaterialIndex, float Value, bool Additive);

    // Field operations
    void UnionOperation(const FVector& Center, float Radius, uint8 MaterialIndex, float Strength);
    void SubtractionOperation(const FVector& Center, float Radius, uint8 MaterialIndex, float Strength);
    void IntersectionOperation(const FVector& Center, float Radius, uint8 MaterialIndex, float Strength);
    void SmoothUnionOperation(const FVector& Center, float Radius, uint8 MaterialIndex, float Strength, float Smoothness);
    
    // Material blending
    void BlendMaterials(const FVector& Center, float Radius, uint8 SourceMaterial, uint8 TargetMaterial, float BlendFactor);
    
    // Gradient and normal calculation
    FVector CalculateGradient(const FVector& WorldPosition, uint8 MaterialIndex) const;
    FVector CalculateNormal(const FVector& WorldPosition, uint8 MaterialIndex) const;
    
    // Field analysis
    void FindMaterialBoundary(const FVector& Start, const FVector& Direction, float MaxDistance, 
                             uint8 MaterialIndex, FVector& OutHitPoint) const;
    
    // Memory management
    void OptimizeMemoryUsage();
    uint64 GetMemoryUsage() const;
    void CompressInactiveRegions();
    
    // Network synchronization
    void SerializeFieldState(FArchive& Ar, const TArray<uint8>& MaterialIndices);
    void SerializeFieldDelta(FArchive& Ar, uint64 BaseVersion, const TArray<uint8>& MaterialIndices);
    bool ValidateFieldOperation(const FVector& Position, float Radius, uint8 MaterialIndex) const;
    TArray<uint8> GenerateFieldDelta(uint64 BaseVersion, uint64 CurrentVersion, const TArray<uint8>& MaterialIndices) const;
    void ApplyFieldDelta(const TArray<uint8>& DeltaData, const TArray<uint8>& MaterialIndices);
    uint64 GetCurrentFieldVersion() const;
    
private:
    // Internal data structures
    struct FMaterialChannel;
    struct FFieldRegion;
    
    // Implementation details
    TMap<uint8, TUniquePtr<FMaterialChannel>> MaterialChannels;
    TSharedPtr<FNarrowBandAllocator> NarrowBand;
    float NarrowBandThickness;
    uint32 MaxMaterialCount;
    FThreadSafeCounter FieldVersionCounter;
    
    // Helper methods
    bool IsInNarrowBand(float Distance) const;
    void PropagateDistanceChanges(const FVector& Center, float Radius, uint8 MaterialIndex);
    void UpdateAdjacentMaterials(const FVector& Position, float Radius, const TArray<uint8>& MaterialIndices);
    void PriorityPropagation(const FVector& Center, float Radius, uint8 MaterialIndex);
};