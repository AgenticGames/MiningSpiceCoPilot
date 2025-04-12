// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConfigProvider.h"
#include "MemoryConfigProvider.generated.h"

/**
 * Memory-based configuration provider
 * Stores configuration in memory only, without persistent storage.
 * Useful for runtime-adjustable settings and temporary configurations.
 */
UCLASS()
class MININGSPICECOPILOT_API UMemoryConfigProvider : public UConfigProvider
{
    GENERATED_BODY()

public:
    /** Constructor */
    UMemoryConfigProvider();
    
    /** Destructor */
    virtual ~UMemoryConfigProvider();

    //~ Begin IConfigProvider Interface
    virtual FConfigOperationResult Load() override;
    virtual FConfigOperationResult Save() override;
    virtual FConfigOperationResult Reset() override;
    //~ End IConfigProvider Interface
    
    /**
     * Set default values from a JSON string
     * @param JsonString JSON string containing default configuration values
     * @return Whether the default values were set successfully
     */
    bool SetDefaultValuesFromJson(const FString& JsonString);
    
    /**
     * Set default values from a JSON object
     * @param JsonObject JSON object containing default configuration values
     * @return Whether the default values were set successfully
     */
    bool SetDefaultValuesFromJsonObject(const TSharedPtr<FJsonObject>& JsonObject);
    
    /**
     * Export current values to a JSON string
     * @param bPrettyPrint Whether to format the JSON with indentation
     * @return JSON string containing current configuration values
     */
    FString ExportToJsonString(bool bPrettyPrint = false) const;
};