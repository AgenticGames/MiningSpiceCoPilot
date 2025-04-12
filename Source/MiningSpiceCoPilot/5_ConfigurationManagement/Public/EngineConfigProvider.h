// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConfigProvider.h"
#include "EngineConfigProvider.generated.h"

/**
 * Engine configuration provider
 * Loads and saves configuration from/to Unreal Engine config files.
 * Supports hierarchical configuration access through engine sections.
 */
UCLASS()
class MININGSPICECOPILOT_API UEngineConfigProvider : public UConfigProvider
{
    GENERATED_BODY()

public:
    /** Constructor */
    UEngineConfigProvider();
    
    /** Destructor */
    virtual ~UEngineConfigProvider();

    //~ Begin IConfigProvider Interface
    virtual FConfigOperationResult Load() override;
    virtual FConfigOperationResult Save() override;
    //~ End IConfigProvider Interface
    
    /**
     * Sets the config section name for this provider
     * @param InConfigSectionName Name of the config section to use
     * @param bAutoLoad Whether to automatically load the section
     */
    void SetConfigSectionName(const FString& InConfigSectionName, bool bAutoLoad = false);
    
    /**
     * Gets the config section name for this provider
     * @return Name of the config section
     */
    FString GetConfigSectionName() const;
    
    /**
     * Sets the config file name for this provider
     * @param InConfigFileName Name of the config file to use (e.g., "Game", "Engine", etc.)
     * @param bAutoLoad Whether to automatically load the section
     */
    void SetConfigFileName(const FString& InConfigFileName, bool bAutoLoad = false);
    
    /**
     * Gets the config file name for this provider
     * @return Name of the config file
     */
    FString GetConfigFileName() const;
    
    /**
     * Sets whether to automatically save on shutdown
     * @param bInAutoSave Whether to automatically save on shutdown
     */
    void SetAutoSave(bool bInAutoSave);
    
    /**
     * Gets whether to automatically save on shutdown
     * @return Whether auto-save is enabled
     */
    bool GetAutoSave() const;

private:
    /** Config section name (e.g., "/Script/MiningSpiceCoPilot.SVOSDFSettings") */
    FString ConfigSectionName;
    
    /** Config file name (e.g., "Game", "Engine", etc.) */
    FString ConfigFileName;
    
    /** Whether to automatically save on shutdown */
    bool bAutoSave;
};