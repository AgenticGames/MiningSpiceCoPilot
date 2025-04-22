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
#include "Templates/SharedPointer.h"
#include "ThreadSafety.h"
#include "../../3_ThreadingTaskSystem/Public/TaskScheduler.h"
#include "../../3_ThreadingTaskSystem/Public/AsyncTaskTypes.h"

// Forward declarations
class UMaterialInstanceDynamic;
struct FMaterialPropertyInfo;
struct FMaterialTypeInfo;
struct FMaterialRelationship;

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
    SupportsLiveEditing = 1 << 18,
    // Extended capabilities for new system
    SupportsSpatialCoherence = 1 << 19,
    SupportsVectorization = 1 << 20,
    SupportsConcurrentAccess = 1 << 21,
    SupportsLowMemoryMode = 1 << 22
};
ENUM_CLASS_FLAGS(EMaterialCapabilities);

/**
 * Information about a material property
 */
struct MININGSPICECOPILOT_API FMaterialPropertyInfo
{
    /** Property ID */
    uint32 PropertyId;
    
    /** Property name */
    FName PropertyName;
    
    /** Display name for UI */
    FText DisplayName;
    
    /** Description for UI */
    FText Description;
    
    /** Property category */
    FName Category;
    
    /** Whether this property is exposed to blueprints */
    bool bExposeToBlueprintGraph;
    
    /** Whether this property is serialized */
    bool bSerializable;
    
    /** Whether this property supports hot reload */
    bool bSupportsHotReload;
    
    /** Whether this property is inherited by child materials */
    bool bInheritable;
    
    /** Parent property ID (for hierarchical properties) */
    uint32 ParentPropertyId;
    
    /** Property schema version */
    uint32 SchemaVersion;
    
    /** Default constructor */
    FMaterialPropertyInfo()
        : PropertyId(0)
        , PropertyName(NAME_None)
        , DisplayName(FText::GetEmpty())
        , Description(FText::GetEmpty())
        , Category(NAME_None)
        , bExposeToBlueprintGraph(false)
        , bSerializable(true)
        , bSupportsHotReload(true)
        , bInheritable(true)
        , ParentPropertyId(0)
        , SchemaVersion(1)
    {
    }
    
    /** Constructor with name */
    FMaterialPropertyInfo(const FName& InName)
        : PropertyId(0)
        , PropertyName(InName)
        , DisplayName(FText::FromName(InName))
        , Description(FText::GetEmpty())
        , Category(NAME_None)
        , bExposeToBlueprintGraph(false)
        , bSerializable(true)
        , bSupportsHotReload(true)
        , bInheritable(true)
        , ParentPropertyId(0)
        , SchemaVersion(1)
    {
    }
    
    /** Returns whether this property info is valid */
    bool IsValid() const
    {
        return !PropertyName.IsNone();
    }
};

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
 * Material type information structure
 */
struct MININGSPICECOPILOT_API FMaterialTypeInfo
{
    /** Unique identifier for this material type */
    uint32 TypeId;
    
    /** Name of this material type */
    FName TypeName;
    
    /** Category of this material type */
    FName Category;
    
    /** Display name for UI */
    FText DisplayName;
    
    /** Description of this material type */
    FText Description;
    
    /** Material properties for this type */
    TMap<FName, FString> Properties;
    
    /** Related material type IDs */
    TArray<uint32> RelatedMaterialTypeIds;
    
    /** Priority for conflict resolution */
    EMaterialPriority Priority;
    
    /** Material capabilities */
    EMaterialCapabilities Capabilities;
    
    /** Parent type ID for inheritance */
    uint32 ParentTypeId;
    
    /** Channel ID for material data */
    int32 ChannelId;
    
    /** Schema version for migration */
    uint32 SchemaVersion;
    
    /** Resource value multiplier */
    float ResourceValueMultiplier;
    
    /** Base mining resistance */
    float BaseMiningResistance;
    
    /** Sound amplification factor */
    float SoundAmplificationFactor;
    
    /** Particle emission multiplier */
    float ParticleEmissionMultiplier;
    
    /** Whether this material is mineable */
    bool bIsMineable;
    
    /** Whether this material is a resource */
    bool bIsResource;
    
    /** Whether this material can fracture */
    bool bCanFracture;
    
    /** Whether this material type is abstract and cannot be instantiated directly */
    bool bIsAbstract;
    
    /** Visualization material for this material type */
    TSoftObjectPtr<UObject> VisualizationMaterial;
    
    /** Mining sound for this material type */
    TSoftObjectPtr<UObject> MiningSound;
    
    /** Hot reload identifier */
    uint32 HotReloadId;
    
    /** Count of channels used by this material */
    int32 ChannelCount;
    
    /** Default constructor */
    FMaterialTypeInfo()
        : TypeId(0)
        , Priority(EMaterialPriority::Normal)
        , Capabilities(EMaterialCapabilities::None)
        , ParentTypeId(0)
        , ChannelId(-1)
        , SchemaVersion(1)
        , ResourceValueMultiplier(1.0f)
        , BaseMiningResistance(1.0f)
        , SoundAmplificationFactor(1.0f)
        , ParticleEmissionMultiplier(1.0f)
        , bIsMineable(false)
        , bIsResource(false)
        , bCanFracture(false)
        , bIsAbstract(false)
        , HotReloadId(0)
        , ChannelCount(0)
    {
    }
    
    /** Constructor with name */
    FMaterialTypeInfo(const FName& InName)
        : TypeId(0)
        , TypeName(InName)
        , Priority(EMaterialPriority::Normal)
        , Capabilities(EMaterialCapabilities::None)
        , ParentTypeId(0)
        , ChannelId(-1)
        , SchemaVersion(1)
        , ResourceValueMultiplier(1.0f)
        , BaseMiningResistance(1.0f)
        , SoundAmplificationFactor(1.0f)
        , ParticleEmissionMultiplier(1.0f)
        , bIsMineable(false)
        , bIsResource(false)
        , bCanFracture(false)
        , bIsAbstract(false)
        , HotReloadId(0)
        , ChannelCount(0)
    {
    }
    
    /** Adds a capability to this material type */
    void AddCapability(EMaterialCapabilities InCapability)
    {
        Capabilities = static_cast<EMaterialCapabilities>(static_cast<uint32>(Capabilities) | static_cast<uint32>(InCapability));
    }
    
    /** Removes a capability from this material type */
    void RemoveCapability(EMaterialCapabilities InCapability)
    {
        Capabilities = static_cast<EMaterialCapabilities>(static_cast<uint32>(Capabilities) & ~static_cast<uint32>(InCapability));
    }
    
    /** Checks if this material type has the specified capability */
    bool HasCapability(EMaterialCapabilities InCapability) const
    {
        return (static_cast<uint32>(Capabilities) & static_cast<uint32>(InCapability)) != 0;
    }
    
    /** Migrates to current schema version */
    bool MigrateToCurrentVersion(uint32 CurrentSchemaVersion)
    {
        if (SchemaVersion >= CurrentSchemaVersion)
        {
            return true;
        }
        
        // Implement migration logic based on schema version differences
        // This is just a stub implementation
        SchemaVersion = CurrentSchemaVersion;
        return true;
    }
    
    /** Creates a blueprint wrapper for this material type */
    bool CreateBlueprintWrapper() const;
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
    
    /** Whether this relationship is applied in both directions */
    bool bBidirectional;
    
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
    
    /** Source material type name */
    FName SourceTypeName;
    
    /** Target material type name */
    FName TargetTypeName;
    
    /** Constructor initializing with default values */
    FMaterialRelationship()
        : RelationshipId(0)
        , SourceTypeId(0)
        , TargetTypeId(0)
        , CompatibilityScore(0.0f)
        , bCanBlend(false)
        , bBidirectional(false)
        , BlendSharpness(0.5f)
        , InteractionType(NAME_None)
        , InteractionPriority(0)
        , SchemaVersion(1)
        , SourceTypeName(NAME_None)
        , TargetTypeName(NAME_None)
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
    /** Constructor */
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
    virtual ERegistryType GetRegistryType() const override;
    virtual ETypeCapabilities GetTypeCapabilities(uint32 TypeId) const override;
    
    /**
     * Gets the extended capabilities of a specific material type
     * @param TypeId The ID of the material type to query
     * @return The extended capabilities of the material type
     */
    virtual ETypeCapabilitiesEx GetTypeCapabilitiesEx(uint32 TypeId) const override;
    
    virtual uint64 ScheduleTypeTask(uint32 TypeId, TFunction<void()> TaskFunc, const struct FTaskConfig& Config) override;
    //~ End IRegistry Interface
    
    // Add missing abstract method implementations
    virtual bool PreInitializeTypes() override;
    virtual bool ParallelInitializeTypes(bool bParallel = true) override;
    virtual bool PostInitializeTypes() override;
    virtual TArray<int32> GetTypeDependencies(uint32 TypeId) const override;
    
    /**
     * Registers a material type
     * @param InTypeInfo Material type information
     * @param InTypeName Material type name
     * @param InPriority Priority for conflict resolution
     * @return Type ID for the registered type
     */
    uint32 RegisterMaterialType(
        const FMaterialTypeInfo& InTypeInfo,
        const FName& InTypeName,
        EMaterialPriority InPriority = EMaterialPriority::Normal
    );
    
    /**
     * Registers a material relationship
     * @param InSourceTypeName Source material type name
     * @param InTargetTypeName Target material type name
     * @param InCompatibilityScore Compatibility score between the materials
     * @return Relationship ID
     */
    uint32 RegisterMaterialRelationship(
        const FName& InSourceTypeName,
        const FName& InTargetTypeName,
        float InCompatibilityScore = 0.5f
    );
    
    /**
     * Allocates a material channel for a type
     * @param InTypeId Material type ID
     * @return Channel ID or INDEX_NONE if allocation failed
     */
    int32 AllocateMaterialChannel(uint32 InTypeId);
    
    /**
     * Gets material type information by ID
     * @param InTypeId Material type ID
     * @return Material type info or nullptr if not found
     */
    const FMaterialTypeInfo* GetMaterialTypeInfo(uint32 InTypeId) const;
    
    /**
     * Gets material type information by name
     * @param InTypeName Material type name
     * @return Material type info or nullptr if not found
     */
    const FMaterialTypeInfo* GetMaterialTypeInfoByName(const FName& InTypeName) const;
    
    /**
     * Gets material relationship information by ID
     * @param InRelationshipId Relationship ID
     * @return Relationship info or nullptr if not found
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
     * Registers a material property
     * @param InTypeId Material type ID
     * @param InProperty Material property to register
     * @return True if registration was successful
     */
    bool RegisterMaterialProperty(uint32 InTypeId, TSharedPtr<FMaterialPropertyBase> InProperty);
    
    /**
     * Gets a material property
     * @param InTypeId Material type ID
     * @param InPropertyName Property name
     * @return Property or nullptr if not found
     */
    TSharedPtr<FMaterialPropertyBase> GetMaterialProperty(uint32 InTypeId, const FName& InPropertyName) const;
    
    /**
     * Gets all properties for a material type
     * @param InTypeId Material type ID
     * @return Map of property names to properties
     */
    TMap<FName, TSharedPtr<FMaterialPropertyBase>> GetAllMaterialProperties(uint32 InTypeId) const;
    
    /**
     * Inherits properties from a parent material type
     * @param InChildTypeId Child material type ID
     * @param InParentTypeId Parent material type ID
     * @param bOverrideExisting Whether to override existing properties
     * @return True if inheritance was successful
     */
    bool InheritPropertiesFromParent(uint32 InChildTypeId, uint32 InParentTypeId, bool bOverrideExisting = false);
    
    /**
     * Gets material capabilities for a type
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
     * Clones a material type
     * @param InSourceTypeId Source material type ID
     * @param InNewTypeName Name for the cloned type
     * @param bInheritRelationships Whether to inherit relationships
     * @return Type ID of the clone
     */
    uint32 CloneMaterialType(uint32 InSourceTypeId, const FName& InNewTypeName, bool bInheritRelationships = true);
    
    /**
     * Handles hot reload of material types
     * @return True if the hot reload was successful
     */
    bool HandleHotReload();
    
    /**
     * Migrates all material types to the current schema version
     * @return True if migration was successful
     */
    bool MigrateAllTypes();
    
    /**
     * Creates Blueprint wrappers for registered material types
     */
    void CreateBlueprintWrappers();
    
    /**
     * Detects hardware capabilities for material operations
     * @return Hardware capability flags
     */
    EMaterialCapabilities DetectHardwareCapabilities();
    
    /**
     * Gets material types by category
     * @param InCategory Category to filter by
     * @return Array of material types in the category
     */
    TArray<FMaterialTypeInfo> GetMaterialTypesByCategory(const FName& InCategory) const;
    
    /**
     * Sets the category for a material type
     * @param InTypeId Material type ID
     * @param InCategory Category name
     * @return True if the category was set
     */
    bool SetMaterialCategory(uint32 InTypeId, const FName& InCategory);
    
    /**
     * Sets up material fields for visualization and editing
     * @param InTypeId Material type ID
     * @param bEnableVectorization Whether to enable SIMD optimization
     * @return True if fields were set up successfully
     */
    bool SetupMaterialFields(uint32 InTypeId, bool bEnableVectorization = true);
    
    /**
     * Creates a visualization of the type hierarchy
     * @param OutVisualizationData Output string with visualization data
     * @return True if visualization was created
     */
    bool CreateTypeHierarchyVisualization(FString& OutVisualizationData) const;
    
    /** Gets the singleton instance */
    static FMaterialRegistry& Get();
    
    /**
     * Begins async type registration from an asset
     * @param SourceAsset Path to the asset to load types from
     * @return Operation ID for tracking
     */
    uint64 BeginAsyncTypeRegistration(const FString& SourceAsset);
    
    /**
     * Begins async batch registration of material types
     * @param TypeInfos Array of material type information
     * @return Operation ID for tracking
     */
    uint64 BeginAsyncMaterialTypeBatchRegistration(const TArray<FMaterialTypeInfo>& TypeInfos);
    
    /**
     * Registers a progress callback for type registration
     * @param OperationId Operation ID from BeginAsyncTypeRegistration
     * @param Callback Progress callback
     * @param UpdateIntervalMs Update interval in milliseconds
     * @return True if registration was successful
     */
    bool RegisterTypeRegistrationProgressCallback(uint64 OperationId, const FAsyncProgressDelegate& Callback, uint32 UpdateIntervalMs = 100);
    
    /**
     * Registers a completion callback for type registration
     * @param OperationId Operation ID from BeginAsyncTypeRegistration
     * @param Callback Completion callback
     * @return True if registration was successful
     */
    bool RegisterTypeRegistrationCompletionCallback(uint64 OperationId, const FTypeRegistrationCompletionDelegate& Callback);
    
    /**
     * Cancels an async type registration
     * @param OperationId Operation ID from BeginAsyncTypeRegistration
     * @param bWaitForCancellation Whether to wait for the cancellation to complete
     * @return True if cancellation was successful
     */
    bool CancelAsyncTypeRegistration(uint64 OperationId, bool bWaitForCancellation = false);
    
    /**
     * Gets material type info optimized for NUMA locality
     * Uses NUMA-local caching for improved performance on multi-socket systems
     * 
     * @param InTypeId Material type ID
     * @return Material type info or nullptr if not found
     */
    const FMaterialTypeInfo* GetMaterialTypeInfoNUMAOptimized(uint32 InTypeId) const;
    
    /**
     * Sets the preferred NUMA domain for a material type
     * Allows optimizing data locality for frequently accessed material types
     * 
     * @param InTypeId Material type ID
     * @param DomainId NUMA domain ID
     * @return True if the domain was set
     */
    bool SetPreferredNUMADomainForType(uint32 InTypeId, uint32 DomainId);
    
    /**
     * Gets the preferred NUMA domain for a material type
     * 
     * @param InTypeId Material type ID
     * @return Preferred NUMA domain ID or MAX_uint32 if not set
     */
    uint32 GetPreferredNUMADomainForType(uint32 InTypeId) const;
    
    /**
     * Prefetches material types to a specific NUMA domain
     * Useful for optimizing access patterns before intensive operations
     * 
     * @param TypeIds Array of type IDs to prefetch
     * @param DomainId Target NUMA domain
     */
    void PrefetchTypesToDomain(const TArray<uint32>& TypeIds, uint32 DomainId);
    
    /**
     * Records access to a material type from a specific thread
     * Used for optimizing NUMA domain assignments
     * 
     * @param InTypeId Material type ID
     * @param ThreadId Thread ID (0 for current thread)
     * @param bIsWrite Whether the access is for writing
     */
    void RecordMaterialTypeAccess(uint32 InTypeId, uint32 ThreadId = 0, bool bIsWrite = false) const;
    
    /**
     * Gets NUMA-specific statistics for material types
     * 
     * @return Map of domain IDs to access statistics
     */
    TMap<uint32, FString> GetNUMAAccessStats() const;
    
    /**
     * Optimizes material type placement across NUMA domains
     * Analyzes access patterns and migrates types to optimal domains
     * 
     * @return Number of types that were migrated
     */
    int32 OptimizeTypeNUMAPlacement();

    /**
     * Registers a material type relationship
     * @param InSourceTypeName Name of the source material type
     * @param InTargetTypeName Name of the target material type
     * @param InCompatibilityScore Compatibility score (0.0-1.0) with higher values indicating better compatibility
     * @param bInCanBlend Whether the materials can blend together
     * @param bInBidirectional Whether the relationship applies in both directions
     * @return Relationship ID, or 0 if registration failed
     */
    uint32 RegisterMaterialRelationship(
        const FName& InSourceTypeName,
        const FName& InTargetTypeName,
        float InCompatibilityScore = 0.5f,
        bool bInCanBlend = true,
        bool bInBidirectional = false);

private:
    /** Generates a unique type ID for new material type registrations */
    uint32 GenerateUniqueTypeId();
    
    /** Generates a unique ID for material relationships */
    uint32 GenerateUniqueRelationshipId();
    
    /** Gets a mutable pointer to a material type info */
    TSharedRef<FMaterialTypeInfo, ESPMode::ThreadSafe>* GetMutableMaterialTypeInfo(uint32 InTypeId);
    
    /** Allocates memory for material channel data */
    void AllocateChannelMemory(const TSharedRef<FMaterialTypeInfo, ESPMode::ThreadSafe>& TypeInfo);
    
    /** Sets up memory sharing between parent and derived materials */
    void SetupMemorySharingForDerivedMaterials(uint32 TypeId);
    
    /** Makes sure property map exists for a material type */
    void EnsurePropertyMap(uint32 InTypeId);
    
    /** Helper method for type hierarchy visualization */
    void VisualizeTypeHierarchy(uint32 InTypeId, int32 InDepth, FString& OutVisualizationData) const;
    
    /** Helper method for material type initialization */
    bool InitializeMaterialType(uint32 TypeId);
    
    /** Allocates property tables for the material type */
    void AllocatePropertyTables(uint32 TypeId);
    
    /** Initializes material properties for a type */
    void InitializeMaterialProperties(uint32 TypeId);
    
    /** Sets up compatibility matrix for material interactions */
    bool SetupCompatibilityMatrix(uint32 TypeId);
    
    /** Generates a combined compatibility matrix for all materials */
    void GenerateCombinedCompatibilityMatrix();
    
    /** Map of material types by ID */
    TMap<uint32, TSharedRef<FMaterialTypeInfo, ESPMode::ThreadSafe>> MaterialTypeMap;
    
    /** Map of material type names to IDs */
    TMap<FName, uint32> MaterialTypeNameMap;
    
    /** Map of relationships by ID */
    TMap<uint32, TSharedRef<FMaterialRelationship, ESPMode::ThreadSafe>> RelationshipMap;
    
    /** Map of relationships by type pair */
    TMap<TPair<uint32, uint32>, uint32> TypePairToRelationshipMap;
    
    /** Map of relationships by source type ID */
    TMap<uint32, TArray<uint32>> RelationshipsBySourceMap;
    
    /** Map of relationships by target type ID */
    TMap<uint32, TArray<uint32>> RelationshipsByTargetMap;
    
    /** Map of property maps by type ID */
    TMap<uint32, TMap<FName, TSharedPtr<FMaterialPropertyBase>>> PropertyMaps;
    
    /** Map of material types by category */
    TMap<FName, TArray<uint32>> MaterialTypesByCategoryMap;
    
    /** Map of type relationships for hierarchical relationships */
    TMap<uint32, TArray<uint32>> TypeRelationships;
    
    /** Hardware capabilities for this system */
    EMaterialCapabilities HardwareCapabilities;
    
    /** Lock for thread safety */
    mutable FCriticalSection RegistryLock;
    
    /** Next available type ID */
    FThreadSafeCounter NextTypeId;
    
    /** Next available relationship ID */
    FThreadSafeCounter NextRelationshipId;
    
    /** Next available channel ID */
    FThreadSafeCounter NextChannelId;
    
    /** Registry name */
    FName RegistryName;
    
    /** Schema version */
    uint32 SchemaVersion;
    
    /** Whether registry is initialized */
    FThreadSafeBool bIsInitialized;
    
    /** Whether types have been initialized */
    bool bTypesInitialized;
    
    /** Whether initialization is in progress */
    bool bInitializationInProgress;
    
    /** Initialization errors */
    TArray<FString> InitializationErrors;
    
    /** Map of channel IDs by type */
    TMap<uint32, int32> TypeToChannelMap;
    
    /** NUMA topology for optimized memory access */
    TMap<uint32, uint32> TypeNUMADomainMap;
    
    /** Lock for NUMA domain operations */
    mutable FCriticalSection NumaDomainLock;
    
    /** Map of type ID to preferred NUMA domain */
    TMap<uint32, uint32> TypeNUMADomainPreferences;
    
    /** Lock for type access tracking */
    mutable FCriticalSection TypeAccessLock;
    
    /** Map of type access counts by domain (TypeID -> Map of DomainID -> AccessCount) */
    mutable TMap<uint32, TMap<uint32, uint32>> TypeAccessByDomain;
    
    /** Lock for initialization operations */
    mutable FCriticalSection InitLock;
    
    /** Singleton instance */
    static FMaterialRegistry* Instance;
    
    /** Whether singleton is initialized */
    static bool bSingletonInitialized;
};