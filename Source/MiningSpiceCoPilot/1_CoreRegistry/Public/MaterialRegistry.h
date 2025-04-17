// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IRegistry.h"
#include "Engine/EngineTypes.h"
#include "UObject/SoftObjectPtr.h"
#include "HAL/ThreadSafeBool.h"
#include "Containers/Map.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundBase.h"
#include "Misc/EnumClassFlags.h"
#include "HAL/CriticalSection.h"
#include "Misc/Optional.h"
#include "Misc/ScopeRWLock.h"
#include "Interfaces/IServiceLocator.h"
#include "TypeVersionMigrationInfo.h"
#include "Interfaces/IMemoryManager.h"
#include "Interfaces/IPoolAllocator.h"

// Forward declarations
class UMaterialInstanceDynamic;

/**
 * Enumeration of material priorities for conflict resolution
 */
UENUM(BlueprintType)
enum class EMaterialPriority : uint8
{
    Low = 0,
    Normal = 1,
    High = 2,
    Critical = 3
};

/**
 * Enumeration of material capabilities
 * Used for hardware feature detection and runtime compatibility checks
 */
enum class EMaterialCapabilities : uint32
{
    None = 0,
    SupportsBlending = 1 << 0,
    SupportsProcGen = 1 << 1,
    SupportsNoise = 1 << 2,
    SupportsGeometricModifiers = 1 << 3,
    SupportsPatterns = 1 << 4,
    SupportsFracturing = 1 << 5,
    SupportsHotReload = 1 << 6,
    SupportsVersionedSerialization = 1 << 7,
    SupportsIncrementalUpdates = 1 << 8,
    SupportsMultiThreading = 1 << 9,
    SupportsGPUCompute = 1 << 10,
    // SIMD capabilities
    SupportsSSE = 1 << 11,
    SupportsAVX = 1 << 12,
    SupportsAVX2 = 1 << 13,
    SupportsNeon = 1 << 14,
    // Advanced features
    SupportsDynamicRehierarchization = 1 << 15,
    SupportsAdaptiveCompression = 1 << 16,
    SupportsRuntimeMaterialReplacement = 1 << 17,
    SupportsLiveEditing = 1 << 18
};
ENUM_CLASS_FLAGS(EMaterialCapabilities);

/**
 * Base structure for material property data
 */
struct MININGSPICECOPILOT_API FMaterialPropertyBase
{
    /** Property type name */
    FName PropertyName;
    
    /** Property display name for UI */
    FText DisplayName;
    
    /** Property description for UI */
    FText Description;
    
    /** Property category for organization */
    FName Category;
    
    /** Whether this property is exposed to blueprints */
    bool bExposeToBlueprintGraph;
    
    /** Whether this property is serialized */
    bool bSerializable;
    
    /** Whether this property supports hot reload */
    bool bSupportsHotReload;
    
    /** Whether this property is inherited by child materials */
    bool bInheritable;
    
    /** Default constructor */
    FMaterialPropertyBase()
        : PropertyName(NAME_None)
        , DisplayName(FText::GetEmpty())
        , Description(FText::GetEmpty())
        , Category(NAME_None)
        , bExposeToBlueprintGraph(false)
        , bSerializable(true)
        , bSupportsHotReload(true)
        , bInheritable(true)
    {
    }
    
    /** Virtual destructor for polymorphism */
    virtual ~FMaterialPropertyBase() {}
    
    /** Gets the property type name */
    virtual FName GetTypeName() const { return NAME_None; }
    
    /** Creates a clone of this property */
    virtual TSharedPtr<FMaterialPropertyBase> Clone() const = 0;
    
    /** Gets the property value as a string */
    virtual FString GetValueAsString() const = 0;
    
    /** Sets the property value from a string */
    virtual bool SetValueFromString(const FString& InValue) = 0;
};

/**
 * Templated material property for different value types
 */
template<typename ValueType>
struct FMaterialProperty : public FMaterialPropertyBase
{
    /** Property value */
    ValueType Value;
    
    /** Default property value */
    ValueType DefaultValue;
    
    /** Constructor */
    FMaterialProperty()
        : FMaterialPropertyBase()
        , Value()
        , DefaultValue()
    {
    }
    
    /** Constructor with name and value */
    FMaterialProperty(const FName& InName, const ValueType& InValue)
        : FMaterialPropertyBase()
        , Value(InValue)
        , DefaultValue(InValue)
    {
        PropertyName = InName;
    }
    
    /** Gets the property type name */
    virtual FName GetTypeName() const override
    {
        if (std::is_same<ValueType, float>::value)
            return FName(TEXT("Float"));
        else if (std::is_same<ValueType, bool>::value)
            return FName(TEXT("Bool"));
        else if (std::is_same<ValueType, int32>::value)
            return FName(TEXT("Int"));
        else if (std::is_same<ValueType, FString>::value)
            return FName(TEXT("String"));
        else if (std::is_same<ValueType, FName>::value)
            return FName(TEXT("Name"));
        else if (std::is_same<ValueType, FVector>::value)
            return FName(TEXT("Vector"));
        else if (std::is_same<ValueType, FColor>::value)
            return FName(TEXT("Color"));
        else if (std::is_same<ValueType, FSoftObjectPath>::value)
            return FName(TEXT("Asset"));
        else
            return FName(TEXT("Custom"));
    }
    
    /** Creates a clone of this property */
    virtual TSharedPtr<FMaterialPropertyBase> Clone() const override
    {
        TSharedPtr<FMaterialProperty<ValueType>> Clone = MakeShared<FMaterialProperty<ValueType>>();
        Clone->PropertyName = PropertyName;
        Clone->DisplayName = DisplayName;
        Clone->Description = Description;
        Clone->Category = Category;
        Clone->bExposeToBlueprintGraph = bExposeToBlueprintGraph;
        Clone->bSerializable = bSerializable;
        Clone->bSupportsHotReload = bSupportsHotReload;
        Clone->bInheritable = bInheritable;
        Clone->Value = Value;
        Clone->DefaultValue = DefaultValue;
        return Clone;
    }
    
    /** Gets the property value as a string */
    virtual FString GetValueAsString() const override
    {
        if constexpr (std::is_same<ValueType, float>::value || std::is_same<ValueType, double>::value)
            return FString::Printf(TEXT("%f"), static_cast<double>(Value));
        else if constexpr (std::is_same<ValueType, bool>::value)
            return Value ? TEXT("true") : TEXT("false");
        else if constexpr (std::is_same<ValueType, int32>::value || std::is_same<ValueType, uint32>::value)
            return FString::Printf(TEXT("%d"), static_cast<int32>(Value));
        else if constexpr (std::is_same<ValueType, FString>::value)
            return Value;
        else if constexpr (std::is_same<ValueType, FName>::value)
            return Value.ToString();
        else if constexpr (std::is_same<ValueType, FVector>::value)
            return FString::Printf(TEXT("(%f,%f,%f)"), Value.X, Value.Y, Value.Z);
        else if constexpr (std::is_same<ValueType, FColor>::value)
            return FString::Printf(TEXT("(%d,%d,%d,%d)"), Value.R, Value.G, Value.B, Value.A);
        else if constexpr (std::is_same<ValueType, FSoftObjectPath>::value)
            return Value.ToString();
        else
            return TEXT("Unsupported Type");
    }
    
    /** Sets the property value from a string */
    virtual bool SetValueFromString(const FString& InValue) override
    {
        if constexpr (std::is_same<ValueType, float>::value)
        {
            Value = FCString::Atof(*InValue);
            return true;
        }
        else if constexpr (std::is_same<ValueType, bool>::value)
        {
            Value = InValue.ToBool();
            return true;
        }
        else if constexpr (std::is_same<ValueType, int32>::value)
        {
            Value = FCString::Atoi(*InValue);
            return true;
        }
        else if constexpr (std::is_same<ValueType, FString>::value)
        {
            Value = InValue;
            return true;
        }
        else if constexpr (std::is_same<ValueType, FName>::value)
        {
            Value = FName(*InValue);
            return true;
        }
        else if constexpr (std::is_same<ValueType, FSoftObjectPath>::value)
        {
            Value = FSoftObjectPath(InValue);
            return true;
        }
        else
        {
            // For complex types like FVector, proper parsing would be needed
            return false;
        }
    }
};

/**
 * Structure containing information about a material type
 * Optimized for memory alignment and SIMD operations
 */
struct MININGSPICECOPILOT_API FMaterialTypeInfo
{
    /** Unique ID for this material type */
    uint32 TypeId;
    
    /** Name of this material type */
    FName TypeName;
    
    /** Version of this material type's schema */
    uint32 SchemaVersion;
    
    /** Priority of this material type */
    EMaterialPriority Priority;
    
    /** Parent material type ID (0 for base types) */
    uint32 ParentTypeId;
    
    /** Display name for UI */
    FText DisplayName;
    
    /** Description for UI */
    FText Description;
    
    /** Category for organization */
    FName Category;
    
    /** Capabilities bit flags (using EMaterialCapabilities) */
    uint32 CapabilitiesFlags;
    
    /** Capabilities of this material type (stored as an enum for easier interface access) */
    EMaterialCapabilities Capabilities;
    
    /** Whether this type can be instantiated */
    bool bCanBeInstantiated;
    
    /** Whether this type is an abstract base for others */
    bool bIsAbstract;
    
    /** Whether this type supports visual material properties */
    bool bHasVisualProperties;
    
    /** Whether this type is a composite */
    bool bIsComposite;
    
    /** Whether this type is a blend */
    bool bIsBlend;
    
    /** Associated material instance (if applicable) */
    TSoftObjectPtr<UMaterialInstanceDynamic> MaterialInstance;
    
    /** Associated sound (if applicable) */
    TSoftObjectPtr<USoundBase> ImpactSound;
    
    /** Channel ID allocated for this material type (-1 if not allocated) */
    int32 ChannelId;
    
    /** Number of channels allocated for this material type */
    int32 ChannelCount;
    
    /** Resource value multiplier for this material */
    float ResourceValueMultiplier;
    
    /** Base mining resistance (higher = harder to mine) */
    float BaseMiningResistance;
    
    /** Sound amplification factor for mining sound effects */
    float SoundAmplificationFactor;
    
    /** Particle emission multiplier for mining effects */
    float ParticleEmissionMultiplier;
    
    /** Whether this material is mineable */
    bool bIsMineable;
    
    /** Whether this material is a resource */
    bool bIsResource;
    
    /** Whether this material can fracture */
    bool bCanFracture;
    
    /** Visualization material for the editor */
    TSoftObjectPtr<UMaterialInterface> VisualizationMaterial;
    
    /** Base mining sound */
    TSoftObjectPtr<USoundBase> MiningSound;
    
    /** 
     * Hot reload identifier - used to preserve references when reloading material types
     * This remains constant across reloads, even if TypeId changes
     */
    FGuid HotReloadId;
    
    /** Cached dynamic material instance for visualization */
    mutable TWeakObjectPtr<UMaterialInstanceDynamic> CachedVisualizationInstance;
    
    /** Constructor initializing with default values */
    FMaterialTypeInfo()
        : TypeId(0)
        , SchemaVersion(1)
        , Priority(EMaterialPriority::Normal)
        , ParentTypeId(0)
        , DisplayName(FText::GetEmpty())
        , Description(FText::GetEmpty())
        , Category(NAME_None)
        , CapabilitiesFlags(0)
        , Capabilities(EMaterialCapabilities::None)
        , bCanBeInstantiated(true)
        , bIsAbstract(false)
        , bHasVisualProperties(true)
        , bIsComposite(false)
        , bIsBlend(false)
        , ChannelId(-1)
        , ChannelCount(0)
        , ResourceValueMultiplier(1.0f)
        , BaseMiningResistance(1.0f)
        , SoundAmplificationFactor(1.0f)
        , ParticleEmissionMultiplier(1.0f)
        , bIsMineable(true)
        , bIsResource(false)
        , bCanFracture(true)
        , HotReloadId(FGuid::NewGuid())
    {
    }
    
    /** Helper method to check if this material type has a specific capability */
    bool HasCapability(EMaterialCapabilities InCapability) const
    {
        return (CapabilitiesFlags & static_cast<uint32>(InCapability)) != 0;
    }
    
    /** Helper method to add a capability to this material type */
    void AddCapability(EMaterialCapabilities InCapability)
    {
        CapabilitiesFlags |= static_cast<uint32>(InCapability);
    }
    
    /** Helper method to remove a capability from this material type */
    void RemoveCapability(EMaterialCapabilities InCapability)
    {
        CapabilitiesFlags &= ~static_cast<uint32>(InCapability);
    }
    
    /** Creates a Blueprint wrapper for this material type */
    void CreateBlueprintWrapper() const;
    
    /** 
     * Updates material type from a newer schema version
     * @param CurrentSchemaVersion The current schema version to upgrade to
     * @return True if migration was successful
     */
    bool MigrateToCurrentVersion(uint32 CurrentSchemaVersion);
};

/**
 * Structure containing information about a material relationship
 */
struct MININGSPICECOPILOT_API FMaterialRelationship
{
    /** Unique ID for this relationship */
    uint32 RelationshipId;
    
    /** Source material type ID */
    uint32 SourceTypeId;
    
    /** Target material type ID */
    uint32 TargetTypeId;
    
    /** Compatibility score (0-1, where 1 = fully compatible) */
    float CompatibilityScore;
    
    /** Whether these materials can blend at interfaces */
    bool bCanBlend;
    
    /** Blend sharpness (0 = smooth blend, 1 = sharp interface) */
    float BlendSharpness;
    
    /** Interaction type between materials */
    FName InteractionType;
    
    /** Transition effect between materials (if any) */
    TSoftObjectPtr<UObject> TransitionEffect;
    
    /** Interaction priority for conflict resolution */
    int32 InteractionPriority;
    
    /** Schema version this relationship was created with */
    uint32 SchemaVersion;
    
    /** Constructor initializing with default values */
    FMaterialRelationship()
        : RelationshipId(0)
        , SourceTypeId(0)
        , TargetTypeId(0)
        , CompatibilityScore(0.0f)
        , bCanBlend(false)
        , BlendSharpness(0.5f)
        , InteractionType(NAME_None)
        , InteractionPriority(0)
        , SchemaVersion(1)
    {
    }
    
    /** 
     * Updates relationship from a newer schema version
     * @param CurrentSchemaVersion The current schema version to upgrade to
     * @return True if migration was successful
     */
    bool MigrateToCurrentVersion(uint32 CurrentSchemaVersion);
    
    /**
     * Check if relationship is valid
     * @return True if the relationship has valid source and target types
     */
    bool IsValid() const
    {
        return SourceTypeId != 0 && TargetTypeId != 0;
    }
};

/**
 * Registry for material types in the mining system
 * Handles material registration, properties, and relationships between materials
 * Optimized for thread-safety and performance in concurrent environments
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
    virtual bool SetTypeVersion(uint32 TypeId, uint32 NewVersion, bool bMigrateInstanceData = true) override;
    virtual uint32 GetTypeVersion(uint32 TypeId) const override;
    //~ End IRegistry Interface
    
    /**
     * Registers a new material type with the registry
     * @param InTypeName Name of the material type
     * @param InPriority Priority for conflict resolution
     * @param InParentTypeName Optional name of parent material type for inheritance
     * @return Unique ID for the registered type, or 0 if registration failed
     */
    uint32 RegisterMaterialType(
        const FName& InTypeName,
        EMaterialPriority InPriority = EMaterialPriority::Normal,
        const FName& InParentTypeName = NAME_None);
    
    /**
     * Registers a relationship between two material types
     * @param InSourceTypeName Name of the source material type
     * @param InTargetTypeName Name of the target material type
     * @param InCompatibilityScore Compatibility score (0-1)
     * @param bInCanBlend Whether these materials can blend at interfaces
     * @return Unique ID for the registered relationship, or 0 if registration failed
     */
    uint32 RegisterMaterialRelationship(
        const FName& InSourceTypeName,
        const FName& InTargetTypeName,
        float InCompatibilityScore = 0.5f,
        bool bInCanBlend = false);
    
    /**
     * Allocates a channel ID for a material type
     * @param InTypeId Material type ID
     * @return Allocated channel ID, or -1 if allocation failed
     */
    int32 AllocateMaterialChannel(uint32 InTypeId);
    
    /**
     * Gets information about a registered material type
     * Uses optimistic read locking for high-performance in read-heavy scenarios
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
     * Gets information about a material relationship
     * @param InRelationshipId Unique ID of the relationship
     * @return Pointer to relationship info, or nullptr if not found
     */
    const FMaterialRelationship* GetMaterialRelationship(uint32 InRelationshipId) const;
    
    /**
     * Gets all registered material types
     * @return Array of material type info structures
     */
    TArray<FMaterialTypeInfo> GetAllMaterialTypes() const;
    
    /**
     * Gets all material types derived from a parent type
     * @param InParentTypeId Parent material type ID
     * @return Array of derived material type info structures
     */
    TArray<FMaterialTypeInfo> GetDerivedMaterialTypes(uint32 InParentTypeId) const;
    
    /**
     * Gets all relationships for a material type
     * @param InTypeId Material type ID
     * @return Array of relationship info structures
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
     * Checks if a material type is derived from another
     * @param InDerivedTypeId Derived material type ID
     * @param InBaseTypeId Base material type ID
     * @return True if derived type is derived from base type
     */
    bool IsMaterialDerivedFrom(uint32 InDerivedTypeId, uint32 InBaseTypeId) const;
    
    /**
     * Updates a material property
     * @param InTypeId Material type ID
     * @param InPropertyName Property name
     * @param InValue Property value as string
     * @return True if the update was successful
     */
    bool UpdateMaterialProperty(uint32 InTypeId, const FName& InPropertyName, const FString& InValue);
    
    /**
     * Registers a custom property for a material type
     * @param InTypeId Material type ID
     * @param InProperty Shared pointer to the property to register
     * @return True if registration was successful
     */
    bool RegisterMaterialProperty(uint32 InTypeId, TSharedPtr<FMaterialPropertyBase> InProperty);
    
    /**
     * Gets a custom property for a material type
     * @param InTypeId Material type ID
     * @param InPropertyName Name of the property
     * @return Shared pointer to the property, or nullptr if not found
     */
    TSharedPtr<FMaterialPropertyBase> GetMaterialProperty(uint32 InTypeId, const FName& InPropertyName) const;
    
    /**
     * Gets all custom properties for a material type
     * @param InTypeId Material type ID
     * @return Map of property names to property pointers
     */
    TMap<FName, TSharedPtr<FMaterialPropertyBase>> GetAllMaterialProperties(uint32 InTypeId) const;
    
    /**
     * Inherits properties from a parent material type
     * Enhanced with better property inheritance and conflict resolution
     * @param InChildTypeId Child material type ID
     * @param InParentTypeId Parent material type ID
     * @param bOverrideExisting Whether to override existing properties
     * @return True if inheritance was successful
     */
    bool InheritPropertiesFromParent(uint32 InChildTypeId, uint32 InParentTypeId, bool bOverrideExisting = false);
    
    /**
     * Gets a material type's capability flags
     * @param InTypeId Material type ID
     * @return Capability flags for the material type
     */
    EMaterialCapabilities GetMaterialCapabilities(uint32 InTypeId) const;
    
    /**
     * Adds a capability to a material type
     * @param InTypeId Material type ID
     * @param InCapability Capability to add
     * @return True if the capability was added
     */
    bool AddMaterialCapability(uint32 InTypeId, EMaterialCapabilities InCapability);
    
    /**
     * Removes a capability from a material type
     * @param InTypeId Material type ID
     * @param InCapability Capability to remove
     * @return True if the capability was removed
     */
    bool RemoveMaterialCapability(uint32 InTypeId, EMaterialCapabilities InCapability);
    
    /**
     * Creates a deep copy of a material type with a new name
     * @param InSourceTypeId Source material type ID
     * @param InNewTypeName Name for the new material type
     * @param bInheritRelationships Whether to inherit relationships
     * @return Unique ID for the cloned type, or 0 if cloning failed
     */
    uint32 CloneMaterialType(uint32 InSourceTypeId, const FName& InNewTypeName, bool bInheritRelationships = true);
    
    /**
     * Handle hot reload of material types
     * This ensures references remain valid when reloading the registry
     * @return True if hot reload was successful
     */
    bool HandleHotReload();
    
    /**
     * Updates all material types to the current schema version
     * @return True if all migrations were successful
     */
    bool MigrateAllTypes();
    
    /**
     * Creates a blueprint-friendly representation of all material types
     * This prepares material data for blueprint access
     */
    void CreateBlueprintWrappers();
    
    /**
     * Detects available SIMD capabilities on the current hardware
     * Updates material type capabilities based on what's available
     * @return EMaterialCapabilities flags representing available hardware features
     */
    EMaterialCapabilities DetectHardwareCapabilities();
    
    /**
     * Gets all material types in a specific category
     * Useful for UI organization and filtering
     * @param InCategory The category to filter by
     * @return Array of material types in the specified category
     */
    TArray<FMaterialTypeInfo> GetMaterialTypesByCategory(const FName& InCategory) const;
    
    /**
     * Sets the category for a material type
     * @param InTypeId Material type ID
     * @param InCategory Category name
     * @return True if the category was set successfully
     */
    bool SetMaterialCategory(uint32 InTypeId, const FName& InCategory);
    
    /**
     * Sets up SIMD-optimized memory layout for material fields
     * Integrates with NarrowBandAllocator to configure optimized field access
     * @param InTypeId Material type ID to configure
     * @param bEnableVectorization Whether to enable vectorized operations
     * @return True if setup was successful
     */
    bool SetupMaterialFields(uint32 InTypeId, bool bEnableVectorization = true);
    
    /**
     * Creates a visualization of the material type hierarchy
     * Useful for debugging material relationships
     * @param OutVisualizationData Output string containing visualization data
     * @return True if visualization was generated successfully
     */
    bool CreateTypeHierarchyVisualization(FString& OutVisualizationData) const;
    
    /** Gets the singleton instance of the material registry */
    static FMaterialRegistry& Get();
    
private:
    /** Generates a unique type ID for new material type registrations */
    uint32 GenerateUniqueTypeId();
    
    /** Generates a unique relationship ID for new material relationship registrations */
    uint32 GenerateUniqueRelationshipId();
    
    /** Gets a mutable pointer to a material type info */
    TSharedRef<FMaterialTypeInfo>* GetMutableMaterialTypeInfo(uint32 InTypeId);
    
    /**
     * Allocates channel memory for material type using NarrowBandAllocator
     * Integrates with memory management system to optimize material storage
     * @param TypeInfo Material type information
     */
    void AllocateChannelMemory(const TSharedRef<FMaterialTypeInfo>& TypeInfo);
    
    /**
     * Sets up memory sharing between related material types
     * Optimizes memory usage by sharing channels between parent and child materials
     * @param TypeId Material type ID to set up sharing for
     */
    void SetupMemorySharingForDerivedMaterials(uint32 TypeId);
    
    /** Ensures that a property map exists for a material type */
    void EnsurePropertyMap(uint32 InTypeId);
    
    /**
     * Helper function for creating type hierarchy visualization
     * @param InTypeId Type ID to visualize
     * @param InDepth Current depth in hierarchy
     * @param OutVisualizationData String to append visualization data to
     */
    void VisualizeTypeHierarchy(uint32 InTypeId, int32 InDepth, FString& OutVisualizationData) const;
    
    /** Map of registered material types by ID */
    TMap<uint32, TSharedRef<FMaterialTypeInfo>> MaterialTypeMap;
    
    /** Map of registered material types by name for fast lookup */
    TMap<FName, uint32> MaterialTypeNameMap;
    
    /** Map of registered material types by hot reload ID for reference preservation */
    TMap<FGuid, uint32> MaterialTypeHotReloadMap;
    
    /** Map of registered material relationships by ID */
    TMap<uint32, TSharedRef<FMaterialRelationship>> RelationshipMap;
    
    /** Multimap of relationship IDs by source material type ID */
    TMultiMap<uint32, uint32> RelationshipsBySourceMap;
    
    /** Multimap of relationship IDs by target material type ID */
    TMultiMap<uint32, uint32> RelationshipsByTargetMap;
    
    /** Map of material properties by material type ID and property name */
    TMap<uint32, TMap<FName, TSharedPtr<FMaterialPropertyBase>>> MaterialPropertyMap;
    
    /** Map of material types by category for efficient filtering */
    TMultiMap<FName, uint32> MaterialTypesByCategoryMap;
    
    /** Counter for generating unique type IDs */
    FThreadSafeCounter NextTypeId;
    
    /** Counter for generating unique relationship IDs */
    FThreadSafeCounter NextRelationshipId;
    
    /** Counter for allocating channel IDs */
    FThreadSafeCounter NextChannelId;
    
    /** Thread-safe flag indicating if the registry has been initialized */
    FThreadSafeBool bIsInitialized;
    
    /** Schema version of this registry */
    uint32 SchemaVersion;
    
    /** Lock for thread-safe access to the registry maps */
    mutable FRWLock RegistryLock;
    
    /** Critical section for initialization and shutdown */
    FCriticalSection InitializationLock;
    
    /** Cached hardware capabilities */
    EMaterialCapabilities HardwareCapabilities;
    
    /** Singleton instance of the registry */
    static FMaterialRegistry* Singleton;
    
    /** Thread-safe initialization flag for the singleton */
    static FThreadSafeBool bSingletonInitialized;
};