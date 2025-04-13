// MaterialInteractionModel.h
// Material interaction modeling with Boolean operations and transitions

#pragma once

#include "CoreMinimal.h"
#include "Math/Vector.h"
#include "Containers/Map.h"
#include "Containers/Array.h"

// Forward declarations
class USVOHybridVolume;
class FMaterialSDFManager;

/**
 * Material interaction modeling with Boolean operations and transitions
 * Handles material relationships, blending, priority systems and transition handling
 */
class MININGSPICECOPILOT_API FMaterialInteractionModel
{
public:
    // Constructor and destructor
    FMaterialInteractionModel();
    ~FMaterialInteractionModel();

    // Boolean operation types for material interactions
    enum class EBooleanOperation : uint8
    {
        Union,
        Subtraction,
        Intersection,
        SmoothUnion,
        SmoothSubtraction,
        SmoothIntersection,
        Blend,
        Replace
    };
    
    // Blending function types for smooth transitions
    enum class EBlendFunction : uint8
    {
        Linear,
        Smoothstep,
        Exponential,
        Cosine,
        Cubic,
        Custom
    };

    // Material relationship type
    enum class ERelationshipType : uint8
    {
        Compatible,     // Materials can blend with each other
        Incompatible,   // Materials cannot blend and have a sharp boundary
        Dominates,      // One material always replaces the other
        Submits,        // One material is always replaced by the other
        Custom          // Custom relationship with specific handler
    };

    // Structure for defining material relationship rules
    struct FMaterialRelationship
    {
        uint8 MaterialA;
        uint8 MaterialB;
        ERelationshipType Type;
        float TransitionWidth;
        EBlendFunction BlendFunc;
        int32 Priority;
        
        FMaterialRelationship()
            : MaterialA(0)
            , MaterialB(0)
            , Type(ERelationshipType::Compatible)
            , TransitionWidth(1.0f)
            , BlendFunc(EBlendFunction::Linear)
            , Priority(0)
        {}
    };
    
    // Structure for material operation context
    struct FOperationContext
    {
        FVector Position;
        float Radius;
        float Strength;
        uint8 PrimaryMaterial;
        uint8 SecondaryMaterial;
        EBooleanOperation Operation;
        EBlendFunction BlendFunc;
        float BlendFactor;
        
        FOperationContext()
            : Position(FVector::ZeroVector)
            , Radius(1.0f)
            , Strength(1.0f)
            , PrimaryMaterial(0)
            , SecondaryMaterial(0)
            , Operation(EBooleanOperation::Union)
            , BlendFunc(EBlendFunction::Linear)
            , BlendFactor(0.5f)
        {}
    };

    // Initialize with volume reference
    void Initialize(USVOHybridVolume* InVolume, FMaterialSDFManager* InMaterialManager);
    
    // Material relationship management
    void SetMaterialRelationship(uint8 MaterialA, uint8 MaterialB, const FMaterialRelationship& Relationship);
    void SetDefaultRelationship(ERelationshipType Type, float TransitionWidth = 1.0f);
    FMaterialRelationship GetMaterialRelationship(uint8 MaterialA, uint8 MaterialB) const;
    void ClearRelationships();
    
    // Material priority system
    void SetMaterialPriority(uint8 MaterialIndex, int32 Priority);
    int32 GetMaterialPriority(uint8 MaterialIndex) const;
    uint8 GetDominantMaterial(const TArray<uint8>& Materials) const;
    
    // Blending functions
    void RegisterCustomBlendFunction(EBlendFunction BlendType, TFunction<float(float, float, float)> BlendFunc);
    float ApplyBlendFunction(EBlendFunction BlendType, float ValueA, float ValueB, float BlendFactor) const;
    
    // Core SDF operations
    float CombineDistanceFields(float DistanceA, float DistanceB, EBooleanOperation Operation, float Smoothing = 0.0f) const;
    float BlendDistanceFields(float DistanceA, float DistanceB, EBlendFunction BlendFunc, float BlendFactor) const;
    
    // Material operations
    void ApplyMaterialOperation(const FOperationContext& Context);
    void BlendMaterials(const FVector& Position, float Radius, uint8 MaterialA, uint8 MaterialB, float BlendFactor, EBlendFunction BlendFunc = EBlendFunction::Linear);
    void UnionMaterials(const FVector& Position, float Radius, uint8 MaterialA, uint8 MaterialB, float Smoothing = 0.0f);
    void SubtractMaterials(const FVector& Position, float Radius, uint8 MaterialA, uint8 MaterialB, float Smoothing = 0.0f);
    void IntersectMaterials(const FVector& Position, float Radius, uint8 MaterialA, uint8 MaterialB, float Smoothing = 0.0f);
    
    // Boundary management
    float GetBoundaryWidth(uint8 MaterialA, uint8 MaterialB) const;
    void SetBoundaryWidth(uint8 MaterialA, uint8 MaterialB, float Width);
    bool HasSharpBoundary(uint8 MaterialA, uint8 MaterialB) const;
    
    // Material compatibility
    bool AreMaterialsCompatible(uint8 MaterialA, uint8 MaterialB) const;
    TArray<uint8> GetCompatibleMaterials(uint8 MaterialIndex) const;
    
    // Network synchronization
    TArray<uint8> SerializeInteractionData() const;
    bool DeserializeInteractionData(const TArray<uint8>& Data);
    void RegisterNetworkVersion(uint64 VersionId);
    
private:
    // Internal data
    USVOHybridVolume* Volume;
    FMaterialSDFManager* MaterialManager;
    
    // Material interaction data
    TMap<uint64, FMaterialRelationship> MaterialRelationships;
    TMap<uint8, int32> MaterialPriorities;
    TMap<EBlendFunction, TFunction<float(float, float, float)>> CustomBlendFunctions;
    
    FMaterialRelationship DefaultRelationship;
    
    // Network state
    uint64 CurrentNetworkVersion;
    
    // Internal helpers
    uint64 GetMaterialPairKey(uint8 MaterialA, uint8 MaterialB) const;
    TArray<uint8> GetMaterialsAtPosition(const FVector& Position) const;
    float EvaluateFieldAtPosition(const FVector& Position, uint8 MaterialIndex) const;
    
    // SDF operation implementations
    float Union(float DistanceA, float DistanceB) const;
    float Subtraction(float DistanceA, float DistanceB) const;
    float Intersection(float DistanceA, float DistanceB) const;
    float SmoothUnion(float DistanceA, float DistanceB, float Smoothing) const;
    float SmoothSubtraction(float DistanceA, float DistanceB, float Smoothing) const;
    float SmoothIntersection(float DistanceA, float DistanceB, float Smoothing) const;
    
    // Blend function implementations
    float LinearBlend(float ValueA, float ValueB, float BlendFactor) const;
    float SmoothstepBlend(float ValueA, float ValueB, float BlendFactor) const;
    float ExponentialBlend(float ValueA, float ValueB, float BlendFactor) const;
    float CosineBlend(float ValueA, float ValueB, float BlendFactor) const;
    float CubicBlend(float ValueA, float ValueB, float BlendFactor) const;
};