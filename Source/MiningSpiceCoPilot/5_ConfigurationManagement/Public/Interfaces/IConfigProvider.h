// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "../5_IConfigManager.h"
#include "IConfigProvider.generated.h"

/**
 * Configuration provider types
 */
enum class EConfigProviderType : uint8
{
    /** File-based configuration provider */
    File,
    
    /** Memory-based configuration provider */
    Memory,
    
    /** Command line parameter configuration provider */
    CommandLine,
    
    /** Database-based configuration provider */
    Database,
    
    /** Remote configuration provider (server-based) */
    Remote,
    
    /** Registry-based configuration provider */
    Registry,
    
    /** Custom configuration provider */
    Custom
};

/**
 * Configuration operation result
 */
struct MININGSPICECOPILOT_API FConfigOperationResult
{
    /** Whether the operation was successful */
    bool bSuccess;
    
    /** Error message if the operation failed */
    FString ErrorMessage;
    
    /** Number of affected keys */
    int32 AffectedKeyCount;
    
    /** Default constructor for successful result */
    FConfigOperationResult()
        : bSuccess(true)
        , AffectedKeyCount(0)
    {
    }
    
    /** Constructor for failed result */
    FConfigOperationResult(const FString& InErrorMessage)
        : bSuccess(false)
        , ErrorMessage(InErrorMessage)
        , AffectedKeyCount(0)
    {
    }
};

/**
 * Configuration provider information
 */
struct MININGSPICECOPILOT_API FConfigProviderInfo
{
    /** Unique ID for this provider */
    FGuid ProviderId;
    
    /** Provider name */
    FString Name;
    
    /** Provider description */
    FString Description;
    
    /** Provider type */
    EConfigProviderType Type;
    
    /** Source priority for values from this provider */
    EConfigSourcePriority Priority;
    
    /** Whether this provider is read-only */
    bool bIsReadOnly;
    
    /** Whether this provider supports hierarchical keys */
    bool bSupportsHierarchy;
    
    /** Default constructor */
    FConfigProviderInfo()
        : ProviderId(FGuid::NewGuid())
        , Type(EConfigProviderType::Memory)
        , Priority(EConfigSourcePriority::Default)
        , bIsReadOnly(false)
        , bSupportsHierarchy(true)
    {
    }
};

/**
 * Configuration key information
 */
struct MININGSPICECOPILOT_API FConfigKeyInfo
{
    /** Configuration key */
    FString Key;
    
    /** Value type */
    EConfigValueType Type;
    
    /** Whether the key is read-only */
    bool bIsReadOnly;
    
    /** Last modification timestamp */
    FDateTime LastModified;
};

/**
 * Base interface for configuration providers in the SVO+SDF mining architecture
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UConfigProviderInterface : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for configuration providers in the SVO+SDF mining architecture
 * Provides a source of configuration values with specific priority and capabilities
 */
class MININGSPICECOPILOT_API IConfigProviderInterface
{
    GENERATED_BODY()

public:
    /**
     * Initializes the configuration provider
     * @return True if initialization was successful
     */
    virtual bool Initialize() = 0;
    
    /**
     * Shuts down the configuration provider and cleans up resources
     */
    virtual void Shutdown() = 0;
    
    /**
     * Checks if the configuration provider has been initialized
     * @return True if initialized, false otherwise
     */
    virtual bool IsInitialized() const = 0;
    
    /**
     * Gets provider information
     * @return Configuration provider information
     */
    virtual FConfigProviderInfo GetProviderInfo() const = 0;
    
    /**
     * Gets a configuration value
     * @param Key Configuration key
     * @param OutValue Receives the configuration value
     * @return True if the key was found
     */
    virtual bool GetValue(const FString& Key, FConfigValue& OutValue) const = 0;
    
    /**
     * Sets a configuration value
     * @param Key Configuration key
     * @param Value Value to set
     * @return Operation result
     */
    virtual FConfigOperationResult SetValue(const FString& Key, const FConfigValue& Value) = 0;
    
    /**
     * Removes a configuration value
     * @param Key Configuration key
     * @return Operation result
     */
    virtual FConfigOperationResult RemoveValue(const FString& Key) = 0;
    
    /**
     * Checks if a configuration key exists
     * @param Key Configuration key
     * @return True if the key exists
     */
    virtual bool HasKey(const FString& Key) const = 0;
    
    /**
     * Gets all configuration keys
     * @return Array of all configuration keys
     */
    virtual TArray<FString> GetAllKeys() const = 0;
    
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
     * Loads configuration data
     * @return Operation result
     */
    virtual FConfigOperationResult Load() = 0;
    
    /**
     * Saves configuration data
     * @return Operation result
     */
    virtual FConfigOperationResult Save() = 0;
    
    /**
     * Resets configuration data to defaults
     * @return Operation result
     */
    virtual FConfigOperationResult Reset() = 0;
    
    /**
     * Gets detailed information about a configuration key
     * @param Key Configuration key
     * @return Key information or nullptr if the key doesn't exist
     */
    virtual TSharedPtr<FConfigKeyInfo> GetKeyInfo(const FString& Key) const = 0;
    
    /**
     * Gets information about all configuration keys
     * @return Array of key information
     */
    virtual TArray<TSharedPtr<FConfigKeyInfo>> GetAllKeyInfo() const = 0;
};

