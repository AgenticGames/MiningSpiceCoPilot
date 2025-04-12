// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConfigProvider.h"
#include "FileConfigProvider.generated.h"

/**
 * File-based configuration provider
 * Loads and saves configuration from/to files in JSON format.
 * Supports automatic loading on initialization and saving on shutdown.
 */
UCLASS()
class MININGSPICECOPILOT_API UFileConfigProvider : public UConfigProvider
{
    GENERATED_BODY()

public:
    /** Constructor */
    UFileConfigProvider();
    
    /** Destructor */
    virtual ~UFileConfigProvider();

    //~ Begin IConfigProvider Interface
    virtual FConfigOperationResult Load() override;
    virtual FConfigOperationResult Save() override;
    //~ End IConfigProvider Interface
    
    /**
     * Sets the file path for this provider
     * @param InFilePath Path to the configuration file
     * @param bAutoLoad Whether to automatically load the file
     */
    void SetFilePath(const FString& InFilePath, bool bAutoLoad = false);
    
    /**
     * Gets the file path for this provider
     * @return Path to the configuration file
     */
    FString GetFilePath() const;
    
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
    
    /**
     * Sets the JSON indentation level for saved files
     * @param InIndentLevel Number of spaces for indentation (0 for compact format)
     */
    void SetIndentLevel(int32 InIndentLevel);
    
    /**
     * Gets the JSON indentation level for saved files
     * @return Indentation level
     */
    int32 GetIndentLevel() const;

private:
    /** Path to the configuration file */
    FString FilePath;
    
    /** Whether to automatically save on shutdown */
    bool bAutoSave;
    
    /** JSON indentation level for saved files */
    int32 IndentLevel;
    
    /** Whether the file exists */
    bool bFileExists;
};