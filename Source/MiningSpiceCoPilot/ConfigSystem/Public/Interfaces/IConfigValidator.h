// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IConfigManager.h"
#include "IConfigValidator.generated.h"

/**
 * Validation severity levels
 */
enum class EValidationSeverity : uint8
{
    /** Information only, no action required */
    Info,
    
    /** Warning, recommended to fix but not mandatory */
    Warning,
    
    /** Error, requires fixing */
    Error,
    
    /** Critical error, must be fixed for system to function */
    Critical
};

/**
 * Configuration validation result
 */
struct MININGSPICECOPILOT_API FConfigValidationResult
{
    /** Whether validation passed */
    bool bIsValid;
    
    /** Validation severity level */
    EValidationSeverity Severity;
    
    /** Validation message */
    FString Message;
    
    /** Configuration key that failed validation */
    FString Key;
    
    /** Suggested valid value (if available) */
    FConfigValue SuggestedValue;
    
    /** Whether auto-correction was applied */
    bool bAutoCorrected;
    
    /** Default constructor for valid result */
    FConfigValidationResult()
        : bIsValid(true)
        , Severity(EValidationSeverity::Info)
        , bAutoCorrected(false)
    {
    }
    
    /** Constructor for validation failure */
    FConfigValidationResult(const FString& InKey, const FString& InMessage, EValidationSeverity InSeverity = EValidationSeverity::Error)
        : bIsValid(false)
        , Severity(InSeverity)
        , Message(InMessage)
        , Key(InKey)
        , bAutoCorrected(false)
    {
    }
    
    /** Constructor with suggested correction */
    FConfigValidationResult(const FString& InKey, const FString& InMessage, const FConfigValue& InSuggestedValue, EValidationSeverity InSeverity = EValidationSeverity::Error)
        : bIsValid(false)
        , Severity(InSeverity)
        , Message(InMessage)
        , Key(InKey)
        , SuggestedValue(InSuggestedValue)
        , bAutoCorrected(false)
    {
    }
};

/**
 * Configuration validation summary
 */
struct MININGSPICECOPILOT_API FConfigValidationSummary
{
    /** Number of configuration keys validated */
    int32 ValidatedCount;
    
    /** Number of keys that passed validation */
    int32 ValidCount;
    
    /** Number of keys that failed validation */
    int32 InvalidCount;
    
    /** Number of keys that had info messages */
    int32 InfoCount;
    
    /** Number of keys that had warnings */
    int32 WarningCount;
    
    /** Number of keys that had errors */
    int32 ErrorCount;
    
    /** Number of keys that had critical errors */
    int32 CriticalCount;
    
    /** Number of keys that were auto-corrected */
    int32 AutoCorrectedCount;
    
    /** Individual validation results */
    TArray<FConfigValidationResult> Results;
    
    /** Default constructor */
    FConfigValidationSummary()
        : ValidatedCount(0)
        , ValidCount(0)
        , InvalidCount(0)
        , InfoCount(0)
        , WarningCount(0)
        , ErrorCount(0)
        , CriticalCount(0)
        , AutoCorrectedCount(0)
    {
    }
    
    /** Add a validation result to the summary */
    void AddResult(const FConfigValidationResult& Result)
    {
        ValidatedCount++;
        if (Result.bIsValid)
        {
            ValidCount++;
        }
        else
        {
            InvalidCount++;
            switch (Result.Severity)
            {
            case EValidationSeverity::Info:
                InfoCount++;
                break;
            case EValidationSeverity::Warning:
                WarningCount++;
                break;
            case EValidationSeverity::Error:
                ErrorCount++;
                break;
            case EValidationSeverity::Critical:
                CriticalCount++;
                break;
            }
            
            if (Result.bAutoCorrected)
            {
                AutoCorrectedCount++;
            }
            
            Results.Add(Result);
        }
    }
    
    /** Check if validation has any issues of the specified severity or higher */
    bool HasIssues(EValidationSeverity MinSeverity = EValidationSeverity::Warning) const
    {
        switch (MinSeverity)
        {
        case EValidationSeverity::Info:
            return InfoCount > 0 || WarningCount > 0 || ErrorCount > 0 || CriticalCount > 0;
        case EValidationSeverity::Warning:
            return WarningCount > 0 || ErrorCount > 0 || CriticalCount > 0;
        case EValidationSeverity::Error:
            return ErrorCount > 0 || CriticalCount > 0;
        case EValidationSeverity::Critical:
            return CriticalCount > 0;
        default:
            return false;
        }
    }
};

/**
 * Base interface for configuration validators in the SVO+SDF mining architecture
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UConfigValidator : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for configuration validation in the SVO+SDF mining architecture
 * Provides validation capabilities for configuration values
 */
class MININGSPICECOPILOT_API IConfigValidator
{
    GENERATED_BODY()

public:
    /**
     * Initializes the configuration validator
     * @return True if initialization was successful
     */
    virtual bool Initialize() = 0;
    
    /**
     * Shuts down the configuration validator and cleans up resources
     */
    virtual void Shutdown() = 0;
    
    /**
     * Checks if the configuration validator has been initialized
     * @return True if initialized, false otherwise
     */
    virtual bool IsInitialized() const = 0;
    
    /**
     * Validates a single configuration value
     * @param Key Configuration key
     * @param Value Configuration value to validate
     * @param bAutoCorrect Whether to automatically correct invalid values
     * @return Validation result
     */
    virtual FConfigValidationResult ValidateValue(const FString& Key, const FConfigValue& Value, bool bAutoCorrect = false) = 0;
    
    /**
     * Registers a validation rule for a configuration key
     * @param Key Configuration key
     * @param MinValue Minimum allowed value (for numeric types)
     * @param MaxValue Maximum allowed value (for numeric types)
     * @param bRequired Whether the key is required
     * @param DefaultValue Default value if auto-correction is needed
     * @return True if the rule was registered successfully
     */
    virtual bool RegisterValidationRule(const FString& Key, const FConfigValue& MinValue, const FConfigValue& MaxValue, bool bRequired = false, const FConfigValue* DefaultValue = nullptr) = 0;
    
    /**
     * Registers a validation rule for a string configuration key
     * @param Key Configuration key
     * @param AllowedValues Array of allowed string values
     * @param bRequired Whether the key is required
     * @param DefaultValue Default value if auto-correction is needed
     * @return True if the rule was registered successfully
     */
    virtual bool RegisterStringValidationRule(const FString& Key, const TArray<FString>& AllowedValues, bool bRequired = false, const FString* DefaultValue = nullptr) = 0;
    
    /**
     * Registers a custom validation function for a configuration key
     * @param Key Configuration key
     * @param ValidationFunc Function to validate the value, should return validation result
     * @return True if the rule was registered successfully
     */
    virtual bool RegisterCustomValidationRule(const FString& Key, TFunction<FConfigValidationResult(const FString&, const FConfigValue&, bool)> ValidationFunc) = 0;
    
    /**
     * Validates all configuration values
     * @param ConfigManager Configuration manager to validate
     * @param bAutoCorrect Whether to automatically correct invalid values
     * @return Validation summary
     */
    virtual FConfigValidationSummary ValidateAll(class IConfigManager* ConfigManager, bool bAutoCorrect = false) = 0;
    
    /**
     * Validates a section of configuration values
     * @param ConfigManager Configuration manager to validate
     * @param SectionKey Section key to validate
     * @param bRecursive Whether to recursively validate subsections
     * @param bAutoCorrect Whether to automatically correct invalid values
     * @return Validation summary
     */
    virtual FConfigValidationSummary ValidateSection(class IConfigManager* ConfigManager, const FString& SectionKey, bool bRecursive = true, bool bAutoCorrect = false) = 0;
    
    /**
     * Gets all validation rules
     * @return Map of configuration keys to validation rules
     */
    virtual TMap<FString, FConfigMetadata> GetAllValidationRules() const = 0;
    
    /**
     * Gets validation rule for a configuration key
     * @param Key Configuration key
     * @return Validation rule or nullptr if no rule exists for the key
     */
    virtual const FConfigMetadata* GetValidationRule(const FString& Key) const = 0;
    
    /**
     * Removes a validation rule
     * @param Key Configuration key
     * @return True if the rule was removed
     */
    virtual bool RemoveValidationRule(const FString& Key) = 0;
    
    /**
     * Gets the singleton instance
     * @return Reference to the configuration validator
     */
    static IConfigValidator& Get();
};
