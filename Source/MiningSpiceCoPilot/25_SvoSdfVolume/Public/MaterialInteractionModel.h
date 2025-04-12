// MaterialInteractionModel.h
// Material interaction modeling with Boolean operations and transitions

#pragma once

#include "CoreMinimal.h"
#include "Containers/Array.h"
#include "Containers/Map.h"

// Forward declarations
class FMaterialSDFManager;

/**
 * Material interaction modeling with Boolean operations and transitions
 * Handles material priority, blending functions, and interaction constraints
 * Supports physics-based interactions and realistic mining responses
 */
class MININGSPICECOPILOT_API FMaterialInteractionModel
{
public:
    FMaterialInteractionModel();
    ~FMaterialInteractionModel();

    // Interaction rule types
    enum class EInteractionType : uint8
    {
        Replace,      // Complete replacement
        Blend,        // Gradual blending
        Boundary,     // Sharp boundary
        Custom        // Custom interaction function
    };

    // Blend function types
    enum class EBlendFunction : uint8
    {
        Linear,
        Smoothstep,
        Exponential,
        Sinusoidal,
        Custom
    };

    // Material pair definition used as key in the interaction rules
    struct FMaterialPair
    {
        uint8 MaterialA;
        uint8 MaterialB;
        
        // Default constructor
        FMaterialPair() : MaterialA(0), MaterialB(0) {}
        
        // Constructor with materials
        FMaterialPair(uint8 InMaterialA, uint8 InMaterialB) : MaterialA(InMaterialA), MaterialB(InMaterialB) {}
        
        // Equal operator for use with TMap
        bool operator==(const FMaterialPair& Other) const
        {
            return MaterialA == Other.MaterialA && MaterialB == Other.MaterialB;
        }
        
        // For use in TMap as key
        friend uint32 GetTypeHash(const FMaterialPair& Pair)
        {
            return HashCombine(GetTypeHash(Pair.MaterialA), GetTypeHash(Pair.MaterialB));
        }
    };
    
    // Defines how two materials interact when they meet
    struct FInteractionRule
    {
        enum class EInteractionType : uint8
        {
            None,
            Blend,
            Replace,
            Erode,
            Repel
        };
        
        EInteractionType Type;
        float Strength; // 0.0 to 1.0, how strongly materials interact
        float Distance; // Distance threshold for interaction to take effect
        
        // Default constructor
        FInteractionRule() 
            : Type(EInteractionType::None)
            , Strength(0.0f)
            , Distance(0.0f) 
        {}
        
        // Constructor with parameters
        FInteractionRule(EInteractionType InType, float InStrength, float InDistance)
            : Type(InType)
            , Strength(InStrength)
            , Distance(InDistance)
        {}
    };
    
    // Settings for blending two materials
    struct FBlendSettings
    {
        float BlendFactor; // 0.0 to 1.0, how much to blend
        float TransitionWidth; // Width of the transition region
        bool bSmoothTransition; // Whether to use smooth interpolation
        
        // Default constructor
        FBlendSettings()
            : BlendFactor(0.5f)
            , TransitionWidth(1.0f)
            , bSmoothTransition(true)
        {}
        
        // Constructor with parameters
        FBlendSettings(float InBlendFactor, float InTransitionWidth, bool bInSmoothTransition)
            : BlendFactor(InBlendFactor)
            , TransitionWidth(InTransitionWidth)
            , bSmoothTransition(bInSmoothTransition)
        {}
    };

    // Initialization
    void Initialize(FMaterialSDFManager* InMaterialManager);
    
    // Material interaction rules
    void SetInteractionRule(uint8 MaterialA, uint8 MaterialB, EInteractionType Type);
    void SetMaterialPriority(uint8 MaterialIndex, uint8 Priority);
    void SetMaterialCompatibility(uint8 MaterialA, uint8 MaterialB, bool IsCompatible);
    void SetBlendFunction(uint8 MaterialA, uint8 MaterialB, EBlendFunction Function, float Strength);
    
    // Material operations
    void ApplyMaterialOperation(const FVector& Position, float Radius, uint8 MaterialIndex, bool IsAdditive, float Strength);
    void BlendMaterials(const FVector& Position, float Radius, uint8 SourceMaterial, uint8 TargetMaterial, float BlendFactor);
    void ReplaceMaterial(const FVector& Position, float Radius, uint8 SourceMaterial, uint8 TargetMaterial);
    
    // Custom interaction registration
    using FCustomInteractionFunction = TFunction<float(float, float, float)>;
    using FCustomBlendFunction = TFunction<float(float, float)>;
    
    void RegisterCustomInteraction(uint8 MaterialA, uint8 MaterialB, FCustomInteractionFunction Function);
    void RegisterCustomBlendFunction(uint8 MaterialA, uint8 MaterialB, FCustomBlendFunction Function);
    
    // Query methods
    EInteractionType GetInteractionType(uint8 MaterialA, uint8 MaterialB) const;
    bool AreMaterialsCompatible(uint8 MaterialA, uint8 MaterialB) const;
    uint8 GetDominantMaterial(const TArray<TPair<uint8, float>>& Materials) const;
    
    // Network synchronization
    void SerializeInteractionRules(FArchive& Ar);
    TArray<uint8> GenerateRulesDelta(uint64 BaseVersion) const;
    void ApplyRulesDelta(const TArray<uint8>& DeltaData);
    bool ValidateInteraction(uint8 MaterialA, uint8 MaterialB) const;
    uint64 GetCurrentRulesVersion() const;

private:
    // Internal data structures
    FMaterialSDFManager* MaterialManager;
    TMap<FMaterialPair, FInteractionRule> InteractionRules;
    TMap<uint8, uint8> MaterialPriorities;
    TMap<FMaterialPair, bool> MaterialCompatibility;
    TMap<FMaterialPair, FBlendSettings> BlendSettings;
    TMap<FMaterialPair, FCustomInteractionFunction> CustomInteractions;
    TMap<FMaterialPair, FCustomBlendFunction> CustomBlendFunctions;
    uint64 RulesVersion;
    
    // Helper methods
    FMaterialPair MakePair(uint8 MaterialA, uint8 MaterialB) const;
    float ApplyBlendFunction(EBlendFunction Function, float Alpha, float Strength) const;
    void PropagateRuleChanges();
    bool ShouldInteract(uint8 MaterialA, uint8 MaterialB) const;
};