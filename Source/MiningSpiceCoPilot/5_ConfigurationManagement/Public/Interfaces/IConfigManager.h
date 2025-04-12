// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IConfigManager.generated.h"

/**
 * Config value types
 */
enum class EConfigValueType : uint8
{
    /** Boolean value type */
    Boolean,
    
    /** Integer value type */
    Integer,
    
    /** Float value type */
    Float,
    
    /** String value type */
    String,
    
    /** Vector value type */
    Vector,
    
    /** Rotator value type */
    Rotator,
    
    /** Transform value type */
    Transform,
    
    /** Color value type */
    Color,
    
    /** JSON object value type */
    JsonObject
};

/**
 * Config source priority levels
 */
enum class EConfigSourcePriority : uint8
{
    /** Default/fallback values */
    Default = 0,
    
    /** Values loaded from system configuration */
    System = 10,
    
    /** Values loaded from game configuration */
    Game = 20,
    
    /** Values loaded from user configuration */
    User = 30,
    
    /** Values set from command line */
    CommandLine = 40,
    
    /** Values set at runtime through code */
    Runtime = 50,
    
    /** Debug override values */
    Debug = 100
};

/**
 * Config value change propagation mode
 */
enum class EConfigPropagationMode : uint8
{
    /** Notify only direct subscribers to this key */
    DirectOnly,
    
    /** Notify subscribers to this key and parent sections */
    UpTree,
    
    /** Notify subscribers to this key and child keys */
    DownTree,
    
    /** Notify all related subscribers */
    FullTree
};

// Forward declaration
DECLARE_DELEGATE_TwoParams(FConfigValueChangedDelegate, const FString& /* ConfigKey */, const struct FMiningConfigValue& /* NewValue */);

/**
 * Configuration value structure
 */
struct MININGSPICECOPILOT_API FMiningConfigValue
{
    /** Value type */
    EConfigValueType Type;
    
    /** Boolean value (if Type == Boolean) */
    bool BoolValue;
    
    /** Integer value (if Type == Integer) */
    int64 IntValue;
    
    /** Float value (if Type == Float) */
    double FloatValue;
    
    /** String value (if Type == String) */
    FString StringValue;
    
    /** Vector value (if Type == Vector) */
    FVector VectorValue;
    
    /** Rotator value (if Type == Rotator) */
    FRotator RotatorValue;
    
    /** Transform value (if Type == Transform) */
    FTransform TransformValue;
    
    /** Color value (if Type == Color) */
    FLinearColor ColorValue;
    
    /** JSON value (if Type == JsonObject) */
    TSharedPtr<FJsonObject> JsonValue;
    
    /** Source priority of this value */
    EConfigSourcePriority SourcePriority;
    
    /** Whether this value is overridden */
    bool bIsOverridden;
    
    /** Whether this value is read-only */
    bool bIsReadOnly;
    
    /** Timestamp when the value was last updated */
    FDateTime LastUpdated;
    
    /** Default constructor */
    FMiningConfigValue()
        : Type(EConfigValueType::String)
        , BoolValue(false)
        , IntValue(0)
        , FloatValue(0.0)
        , VectorValue(FVector::ZeroVector)
        , RotatorValue(FRotator::ZeroRotator)
        , TransformValue(FTransform::Identity)
        , ColorValue(FLinearColor::White)
        , SourcePriority(EConfigSourcePriority::Default)
        , bIsOverridden(false)
        , bIsReadOnly(false)
        , LastUpdated(FDateTime::Now())
    {
    }
    
    /** Boolean constructor */
    FMiningConfigValue(bool InValue, EConfigSourcePriority InSourcePriority = EConfigSourcePriority::Default)
        : Type(EConfigValueType::Boolean)
        , BoolValue(InValue)
        , IntValue(InValue ? 1 : 0)
        , FloatValue(InValue ? 1.0 : 0.0)
        , VectorValue(FVector::ZeroVector)
        , RotatorValue(FRotator::ZeroRotator)
        , TransformValue(FTransform::Identity)
        , ColorValue(FLinearColor::White)
        , SourcePriority(InSourcePriority)
        , bIsOverridden(false)
        , bIsReadOnly(false)
        , LastUpdated(FDateTime::Now())
    {
    }
    
    /** Integer constructor */
    FMiningConfigValue(int64 InValue, EConfigSourcePriority InSourcePriority = EConfigSourcePriority::Default)
        : Type(EConfigValueType::Integer)
        , BoolValue(InValue != 0)
        , IntValue(InValue)
        , FloatValue(static_cast<double>(InValue))
        , VectorValue(FVector::ZeroVector)
        , RotatorValue(FRotator::ZeroRotator)
        , TransformValue(FTransform::Identity)
        , ColorValue(FLinearColor::White)
        , SourcePriority(InSourcePriority)
        , bIsOverridden(false)
        , bIsReadOnly(false)
        , LastUpdated(FDateTime::Now())
    {
    }
    
    /** Float constructor */
    FMiningConfigValue(double InValue, EConfigSourcePriority InSourcePriority = EConfigSourcePriority::Default)
        : Type(EConfigValueType::Float)
        , BoolValue(InValue != 0.0)
        , IntValue(static_cast<int64>(InValue))
        , FloatValue(InValue)
        , VectorValue(FVector::ZeroVector)
        , RotatorValue(FRotator::ZeroRotator)
        , TransformValue(FTransform::Identity)
        , ColorValue(FLinearColor::White)
        , SourcePriority(InSourcePriority)
        , bIsOverridden(false)
        , bIsReadOnly(false)
        , LastUpdated(FDateTime::Now())
    {
    }
    
    /** String constructor */
    FMiningConfigValue(const FString& InValue, EConfigSourcePriority InSourcePriority = EConfigSourcePriority::Default)
        : Type(EConfigValueType::String)
        , BoolValue(InValue.ToBool())
        , IntValue(FCString::Atoi64(*InValue))
        , FloatValue(FCString::Atod(*InValue))
        , StringValue(InValue)
        , VectorValue(FVector::ZeroVector)
        , RotatorValue(FRotator::ZeroRotator)
        , TransformValue(FTransform::Identity)
        , ColorValue(FLinearColor::White)
        , SourcePriority(InSourcePriority)
        , bIsOverridden(false)
        , bIsReadOnly(false)
        , LastUpdated(FDateTime::Now())
    {
    }
    
    /** Conversion to string */
    FString ToString() const
    {
        switch (Type)
        {
        case EConfigValueType::Boolean:
            return BoolValue ? TEXT("true") : TEXT("false");
        case EConfigValueType::Integer:
            return FString::Printf(TEXT("%lld"), IntValue);
        case EConfigValueType::Float:
            return FString::SanitizeFloat(FloatValue);
        case EConfigValueType::String:
            return StringValue;
        case EConfigValueType::Vector:
            return VectorValue.ToString();
        case EConfigValueType::Rotator:
            return RotatorValue.ToString();
        case EConfigValueType::Transform:
            return TransformValue.ToString();
        case EConfigValueType::Color:
            return ColorValue.ToString();
        case EConfigValueType::JsonObject:
            {
                FString OutString;
                TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutString);
                FJsonSerializer::Serialize(JsonValue.ToSharedRef(), Writer);
                return OutString;
            }
        default:
            return TEXT("");
        }
    }
};

/**
 * Configuration metadata structure
 */
struct MININGSPICECOPILOT_API FConfigMetadata
{
    /** Default value */
    FMiningConfigValue DefaultValue;
    
    /** Minimum value for numeric types */
    FMiningConfigValue MinValue;
    
    /** Maximum value for numeric types */
    FMiningConfigValue MaxValue;
    
    /** Description of this configuration option */
    FString Description;
    
    /** Category for UI organization */
    FString Category;
    
    /** Whether this option is deprecated */
    bool bIsDeprecated;
    
    /** Replacement key if deprecated */
    FString ReplacementKey;
    
    /** Default constructor */
    FConfigMetadata()
        : bIsDeprecated(false)
    {
    }
};

/**
 * Base interface for configuration managers in the SVO+SDF mining architecture
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UConfigManager : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for configuration management in the SVO+SDF mining architecture
 * Provides access to hierarchical configuration with priority-based overrides
 */
class MININGSPICECOPILOT_API IConfigManager
{
    GENERATED_BODY()

public:
    /**
     * Initializes the configuration manager
     * @return True if initialization was successful
     */
    virtual bool Initialize() = 0;
    
    /**
     * Shuts down the configuration manager and cleans up resources
     */
    virtual void Shutdown() = 0;
    
    /**
     * Checks if the configuration manager has been initialized
     * @return True if initialized, false otherwise
     */
    virtual bool IsInitialized() const = 0;
    
    /**
     * Loads configuration from a file
     * @param FilePath Path to the configuration file
     * @param Priority Priority level for loaded values
     * @return True if loading was successful
     */
    virtual bool LoadFromFile(const FString& FilePath, EConfigSourcePriority Priority = EConfigSourcePriority::Game) = 0;
    
    /**
     * Saves configuration to a file
     * @param FilePath Path to the configuration file
     * @param bOnlyModified Whether to save only modified values
     * @param Priority Priority level filter (only values with this priority or higher will be saved)
     * @return True if saving was successful
     */
    virtual bool SaveToFile(const FString& FilePath, bool bOnlyModified = true, EConfigSourcePriority Priority = EConfigSourcePriority::User) = 0;
    
    /**
     * Gets a configuration value as a raw FMiningConfigValue
     * @param Key Configuration key (section.subsection.name format)
     * @param OutValue Receives the config value
     * @return True if the key was found
     */
    virtual bool GetValue(const FString& Key, FMiningConfigValue& OutValue) const = 0;
    
    /**
     * Gets a boolean configuration value
     * @param Key Configuration key
     * @param DefaultValue Default value if key is not found
     * @return The boolean value
     */
    virtual bool GetBool(const FString& Key, bool DefaultValue = false) const = 0;
    
    /**
     * Gets an integer configuration value
     * @param Key Configuration key
     * @param DefaultValue Default value if key is not found
     * @return The integer value
     */
    virtual int64 GetInt(const FString& Key, int64 DefaultValue = 0) const = 0;
    
    /**
     * Gets a float configuration value
     * @param Key Configuration key
     * @param DefaultValue Default value if key is not found
     * @return The float value
     */
    virtual double GetFloat(const FString& Key, double DefaultValue = 0.0) const = 0;
    
    /**
     * Gets a string configuration value
     * @param Key Configuration key
     * @param DefaultValue Default value if key is not found
     * @return The string value
     */
    virtual FString GetString(const FString& Key, const FString& DefaultValue = TEXT("")) const = 0;
    
    /**
     * Gets a vector configuration value
     * @param Key Configuration key
     * @param DefaultValue Default value if key is not found
     * @return The vector value
     */
    virtual FVector GetVector(const FString& Key, const FVector& DefaultValue = FVector::ZeroVector) const = 0;
    
    /**
     * Gets a color configuration value
     * @param Key Configuration key
     * @param DefaultValue Default value if key is not found
     * @return The color value
     */
    virtual FLinearColor GetColor(const FString& Key, const FLinearColor& DefaultValue = FLinearColor::White) const = 0;
    
    /**
     * Gets a JSON configuration value
     * @param Key Configuration key
     * @return The JSON value or null if not found
     */
    virtual TSharedPtr<FJsonObject> GetJson(const FString& Key) const = 0;
    
    /**
     * Sets a configuration value
     * @param Key Configuration key
     * @param Value Value to set
     * @param Priority Priority level for the new value
     * @param PropagationMode How to propagate change notifications
     * @return True if the value was set successfully
     */
    virtual bool SetValue(const FString& Key, const FMiningConfigValue& Value, EConfigSourcePriority Priority = EConfigSourcePriority::Runtime, EConfigPropagationMode PropagationMode = EConfigPropagationMode::DirectOnly) = 0;
    
    /**
     * Sets a boolean configuration value
     * @param Key Configuration key
     * @param Value Value to set
     * @param Priority Priority level for the new value
     * @param PropagationMode How to propagate change notifications
     * @return True if the value was set successfully
     */
    virtual bool SetBool(const FString& Key, bool Value, EConfigSourcePriority Priority = EConfigSourcePriority::Runtime, EConfigPropagationMode PropagationMode = EConfigPropagationMode::DirectOnly) = 0;
    
    /**
     * Sets an integer configuration value
     * @param Key Configuration key
     * @param Value Value to set
     * @param Priority Priority level for the new value
     * @param PropagationMode How to propagate change notifications
     * @return True if the value was set successfully
     */
    virtual bool SetInt(const FString& Key, int64 Value, EConfigSourcePriority Priority = EConfigSourcePriority::Runtime, EConfigPropagationMode PropagationMode = EConfigPropagationMode::DirectOnly) = 0;
    
    /**
     * Sets a float configuration value
     * @param Key Configuration key
     * @param Value Value to set
     * @param Priority Priority level for the new value
     * @param PropagationMode How to propagate change notifications
     * @return True if the value was set successfully
     */
    virtual bool SetFloat(const FString& Key, double Value, EConfigSourcePriority Priority = EConfigSourcePriority::Runtime, EConfigPropagationMode PropagationMode = EConfigPropagationMode::DirectOnly) = 0;
    
    /**
     * Sets a string configuration value
     * @param Key Configuration key
     * @param Value Value to set
     * @param Priority Priority level for the new value
     * @param PropagationMode How to propagate change notifications
     * @return True if the value was set successfully
     */
    virtual bool SetString(const FString& Key, const FString& Value, EConfigSourcePriority Priority = EConfigSourcePriority::Runtime, EConfigPropagationMode PropagationMode = EConfigPropagationMode::DirectOnly) = 0;
    
    /**
     * Sets a vector configuration value
     * @param Key Configuration key
     * @param Value Value to set
     * @param Priority Priority level for the new value
     * @param PropagationMode How to propagate change notifications
     * @return True if the value was set successfully
     */
    virtual bool SetVector(const FString& Key, const FVector& Value, EConfigSourcePriority Priority = EConfigSourcePriority::Runtime, EConfigPropagationMode PropagationMode = EConfigPropagationMode::DirectOnly) = 0;
    
    /**
     * Sets a color configuration value
     * @param Key Configuration key
     * @param Value Value to set
     * @param Priority Priority level for the new value
     * @param PropagationMode How to propagate change notifications
     * @return True if the value was set successfully
     */
    virtual bool SetColor(const FString& Key, const FLinearColor& Value, EConfigSourcePriority Priority = EConfigSourcePriority::Runtime, EConfigPropagationMode PropagationMode = EConfigPropagationMode::DirectOnly) = 0;
    
    /**
     * Sets a JSON configuration value
     * @param Key Configuration key
     * @param Value Value to set
     * @param Priority Priority level for the new value
     * @param PropagationMode How to propagate change notifications
     * @return True if the value was set successfully
     */
    virtual bool SetJson(const FString& Key, TSharedPtr<FJsonObject> Value, EConfigSourcePriority Priority = EConfigSourcePriority::Runtime, EConfigPropagationMode PropagationMode = EConfigPropagationMode::DirectOnly) = 0;
    
    /**
     * Removes a configuration value
     * @param Key Configuration key
     * @param Priority Priority level filter (only removes values with this priority or lower)
     * @param PropagationMode How to propagate change notifications
     * @return True if the value was removed
     */
    virtual bool RemoveValue(const FString& Key, EConfigSourcePriority Priority = EConfigSourcePriority::Runtime, EConfigPropagationMode PropagationMode = EConfigPropagationMode::DirectOnly) = 0;
    
    /**
     * Checks if a configuration key exists
     * @param Key Configuration key
     * @return True if the key exists
     */
    virtual bool HasKey(const FString& Key) const = 0;
    
    /**
     * Gets the source priority of a configuration value
     * @param Key Configuration key
     * @return Source priority of the value
     */
    virtual EConfigSourcePriority GetValuePriority(const FString& Key) const = 0;
    
    /**
     * Gets metadata for a configuration key
     * @param Key Configuration key
     * @param OutMetadata Receives the metadata
     * @return True if metadata was found
     */
    virtual bool GetMetadata(const FString& Key, FConfigMetadata& OutMetadata) const = 0;
    
    /**
     * Sets metadata for a configuration key
     * @param Key Configuration key
     * @param Metadata Metadata to set
     * @return True if metadata was set successfully
     */
    virtual bool SetMetadata(const FString& Key, const FConfigMetadata& Metadata) = 0;
    
    /**
     * Gets keys in a section
     * @param Section Section key
     * @param bRecursive Whether to recursively get keys from subsections
     * @return Array of keys in the section
     */
    virtual TArray<FString> GetKeysInSection(const FString& Section, bool bRecursive = false) const = 0;
    
    /**
     * Gets subsections in a section
     * @param Section Section key
     * @param bRecursive Whether to recursively get subsections
     * @return Array of subsection names
     */
    virtual TArray<FString> GetSubsections(const FString& Section, bool bRecursive = false) const = 0;
    
    /**
     * Registers a callback for configuration value changes
     * @param Key Configuration key to monitor
     * @param Callback Delegate to call when the value changes
     * @return Handle to the registered callback for unregistration
     */
    virtual FDelegateHandle RegisterChangeCallback(const FString& Key, const FConfigValueChangedDelegate& Callback) = 0;
    
    /**
     * Unregisters a callback for configuration value changes
     * @param Key Configuration key
     * @param Handle Handle to the registered callback
     * @return True if the callback was unregistered
     */
    virtual bool UnregisterChangeCallback(const FString& Key, FDelegateHandle Handle) = 0;
    
    /**
     * Gets the singleton instance
     * @return Reference to the configuration manager
     */
    static IConfigManager& Get();
};
