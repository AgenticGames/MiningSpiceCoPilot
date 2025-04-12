// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "1_CoreRegistry/Public/Interfaces/IRegistry.h"
#include "Containers/Map.h"
#include "Templates/SharedPointer.h"
#include "HAL/ThreadSafeBool.h"
#include "Misc/SpinLock.h"
#include "UObject/SoftObjectPtr.h"

// Forward declarations
class UMaterialInterface;
class USoundBase;

/**
 * Material priority levels for interaction conflicts
 */
enum class EMaterialPriority : uint8
{
    /** Low priority materials that yield to others */
    Low,
    
    /** Standard priority for most materials */
    Normal,
    
    /** High priority materials that override others */
    High,
    
    /** Critical materials that always take precedence */
    Critical
};

/**
 * Structure containing metadata for a material type
 */
struct MININGSPICECOPILOT_API FMaterialTypeInfo
{
    /** Unique ID for this material type */
    uint32 TypeId;
    
    /** Name of this material type */
    FName TypeName;
    
    /** Parent material type ID, 0 if none */
    uint32 ParentTypeId;
    
    /** Priority level for this material */
    EMaterialPriority Priority;
    
    /** Resource value multiplier */
    float ResourceValueMultiplier;
    
    /** Base resistance to mining operations */
    float BaseMiningResistance;
    
    /** Base sound amplification factor */
    float SoundAmplificationFactor;
    
    /** Base particle emission rate multiplier */
    float ParticleEmissionMultiplier;
    
    /** Whether this material is mineable */
    bool bIsMineable;
    
    /** Whether this material is valuable as a resource */
    bool bIsResource;
    
    /** Whether this material can fracture */
    bool bCanFracture;
    
    /** Visualization material for the editor */
    TSoftObjectPtr<UMaterialInterface> VisualizationMaterial;
    
    /** Base mining sound */
    TSoftObjectPtr<USoundBase> MiningSound;
    
    /** Channel ID for this material in multi-channel SDF fields */
    int32 ChannelId;
};

/**
 * Structure defining a relationship between two material types
 */
struct MININGSPICECOPILOT_API FMaterialRelationship
{
    /** Unique ID for this relationship */
    uint32 RelationshipId;
    
    /** Source material type ID */
    uint32 SourceTypeId;
    
    /** Target material type ID */
    uint32 TargetTypeId;
    
    /** Relationship compatibility score (0-1) */
    float CompatibilityScore;
    
    /** Whether materials can blend at boundaries */
    bool bCanBlend;
    
    /** Boundary sharpness when blending (0-1) */
    float BlendSharpness;
};

/**
 * Registry for material types in the mining system
 * Handles material type registration, properties, and relationships
 */
class MININGSPICECOPILOT_API FMaterialRegistry : public IRegistry
{
public:
    /** Default constructor */
    FMaterialRegistry();
    
    /** Destructor */
    virtual ~FMaterialRegistry();
    
    //~ Begin IRegistry Interface
    virtual bool Initialize() override;
    virtual void Shutdown() override;
    virtual bool IsInitialized() const override;
    virtual FName GetRegistryName() const override;
    virtual uint32 GetSchemaVersion() const override;
    virtual bool Validate(TArray<FString>& OutErrors) const override;
    virtual void Clear() override;
    //~ End IRegistry Interface
    
    /**
     * Registers a new material type with the registry
     * @param InTypeName Name of the material type
     * @param InPriority Priority level for this material
     * @param InParentTypeName Optional parent material type name
     * @return Unique ID for the registered type, or 0 if registration failed
     */
    uint32 RegisterMaterialType(
        const FName& InTypeName,
        EMaterialPriority InPriority = EMaterialPriority::Normal,
        const FName& InParentTypeName = NAME_None);
    
    /**
     * Registers a relationship between two material types
     * @param InSourceTypeName Source material type name
     * @param InTargetTypeName Target material type name
     * @param InCompatibilityScore Compatibility score between the materials (0-1)
     * @param bInCanBlend Whether the materials can blend at boundaries
     * @return Unique ID for the registered relationship, or 0 if registration failed
     */
    uint32 RegisterMaterialRelationship(
        const FName& InSourceTypeName,
        const FName& InTargetTypeName,
        float InCompatibilityScore = 0.5f,
        bool bInCanBlend = false);
    
    /**
     * Allocates a channel ID for a material type in multi-channel SDF fields
     * @param InTypeId Material type ID
     * @return Allocated channel ID, or -1 if allocation failed
     */
    int32 AllocateMaterialChannel(uint32 InTypeId);
    
    /**
     * Gets information about a registered material type
     * @param InTypeId Unique ID of the material type
     * @return Pointer to material type info, or nullptr if not found
     */
    const FMaterialTypeInfo* GetMaterialTypeInfo(uint32 InTypeId) const;
    
    /**
     * Gets information about a registered material type by name
     * @param InTypeName Name of the material type
     * @return Pointer to material type info, or nullptr if not found
     */
    const FMaterialTypeInfo* GetMaterialTypeInfoByName(const FName& InTypeName) const;
    
    /**
     * Gets information about a registered material relationship
     * @param InRelationshipId Unique ID of the relationship
     * @return Pointer to material relationship, or nullptr if not found
     */
    const FMaterialRelationship* GetMaterialRelationship(uint32 InRelationshipId) const;
    
    /**
     * Gets all registered material types
     * @return Array of all material type infos
     */
    TArray<FMaterialTypeInfo> GetAllMaterialTypes() const;
    
    /**
     * Gets all material types that inherit from a parent type
     * @param InParentTypeId Parent type ID
     * @return Array of derived material type infos
     */
    TArray<FMaterialTypeInfo> GetDerivedMaterialTypes(uint32 InParentTypeId) const;
    
    /**
     * Gets all relationships for a material type
     * @param InTypeId Material type ID
     * @return Array of material relationships
     */
    TArray<FMaterialRelationship> GetMaterialRelationships(uint32 InTypeId) const;
    
    /**
     * Checks if a material type is registered
     * @param InTypeId Unique ID of the material type
     * @return True if the type is registered
     */
    bool IsMaterialTypeRegistered(uint32 InTypeId) const;
    
    /**
     * Checks if a material type is registered by name
     * @param InTypeName Name of the material type
     * @return True if the type is registered
     */
    bool IsMaterialTypeRegistered(const FName& InTypeName) const;
    
    /**
     * Checks if a material inherits from another (directly or indirectly)
     * @param InDerivedTypeId The potential derived type ID
     * @param InBaseTypeId The potential base type ID
     * @return True if InDerivedTypeId is derived from InBaseTypeId
     */
    bool IsMaterialDerivedFrom(uint32 InDerivedTypeId, uint32 InBaseTypeId) const;
    
    /**
     * Updates a material type's properties
     * @param InTypeId Material type ID
     * @param InPropertyName Property name
     * @param InValue Property value
     * @return True if update was successful
     */
    bool UpdateMaterialProperty(uint32 InTypeId, const FName& InPropertyName, const FString& InValue);
    
    /** Gets the singleton instance of the material registry */
    static FMaterialRegistry& Get();
    
private:
    /** Generates a unique type ID for new material type registrations */
    uint32 GenerateUniqueTypeId();
    
    /** Generates a unique relationship ID for new relationship registrations */
    uint32 GenerateUniqueRelationshipId();
    
    /** Map of registered material types by ID */
    TMap<uint32, TSharedRef<FMaterialTypeInfo>> MaterialTypeMap;
    
    /** Map of registered material types by name for fast lookup */
    TMap<FName, uint32> MaterialTypeNameMap;
    
    /** Map of registered material relationships by ID */
    TMap<uint32, TSharedRef<FMaterialRelationship>> RelationshipMap;
    
    /** Map of material relationships by source type ID */
    TMultiMap<uint32, uint32> RelationshipsBySourceMap;
    
    /** Map of material relationships by target type ID */
    TMultiMap<uint32, uint32> RelationshipsByTargetMap;
    
    /** Counter for generating unique type IDs */
    uint32 NextTypeId;
    
    /** Counter for generating unique relationship IDs */
    uint32 NextRelationshipId;
    
    /** Counter for allocating material channel IDs */
    int32 NextChannelId;
    
    /** Thread-safe flag indicating if the registry has been initialized */
    FThreadSafeBool bIsInitialized;
    
    /** Schema version of this registry */
    uint32 SchemaVersion;
    
    /** Lock for thread-safe access to the registry maps */
    mutable FSpinLock RegistryLock;
    
    /** Singleton instance of the registry */
    static FMaterialRegistry* Singleton;
    
    /** Thread-safe initialization flag for the singleton */
    static FThreadSafeBool bSingletonInitialized;
};