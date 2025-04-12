// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IConfigValidator.h"
#include "HAL/CriticalSection.h"
#include "ConfigValidator.generated.h"

/**
 * ConfigValidator implementation
 * Provides validation for configuration values based on rules and constraints.
 * Supports range checking, required values, and custom validation functions.
 */
UCLASS()
class MININGSPICECOPILOT_API UConfigValidator : public UObject, public IConfigValidator
{
    GENERATED_BODY()

public:
    /** Constructor */
    UConfigValidator();
    
    /** Destructor */
    virtual ~UConfigValidator();
    
    //~ Begin IConfigValidator Interface
    virtual bool Initialize() override;
    virtual void Shutdown() override;
    virtual bool IsInitialized() const override;
    
    virtual FConfigValidationDetail ValidateValue(const FString& Key, const FMiningConfigValue& Value, bool bAutoCorrect = false) override;
    
    virtual bool RegisterValidationRule(const FString& Key, const FMiningConfigValue& MinValue, const FMiningConfigValue& MaxValue, bool bRequired = false, const FMiningConfigValue* DefaultValue = nullptr) override;
    virtual bool RegisterStringValidationRule(const FString& Key, const TArray<FString>& AllowedValues, bool bRequired = false, const FString* DefaultValue = nullptr) override;
    virtual bool RegisterCustomValidationRule(const FString& Key, TFunction<FConfigValidationDetail(const FString&, const FMiningConfigValue&, bool)> ValidationFunc) override;
    
    virtual FConfigValidationSummary ValidateAll(class IConfigManager* ConfigManager, bool bAutoCorrect = false) override;
    virtual FConfigValidationSummary ValidateSection(class IConfigManager* ConfigManager, const FString& SectionKey, bool bRecursive = true, bool bAutoCorrect = false) override;
    
    virtual TMap<FString, FConfigMetadata> GetAllValidationRules() const override;
    virtual const FConfigMetadata* GetValidationRule(const FString& Key) const override;
    virtual bool RemoveValidationRule(const FString& Key) override;
    
    static IConfigValidator& Get() override;
    //~ End IConfigValidator Interface

private:
    /** Validate a numeric value against min/max constraints */
    FConfigValidationDetail ValidateNumericValue(const FString& Key, const FMiningConfigValue& Value, const FConfigMetadata& Metadata, bool bAutoCorrect);
    
    /** Validate a string value against allowed values */
    FConfigValidationDetail ValidateStringValue(const FString& Key, const FMiningConfigValue& Value, const FConfigMetadata& Metadata, bool bAutoCorrect);
    
    /** Normalize key format */
    FString NormalizeKey(const FString& Key) const;

private:
    /** Flag indicating if the validator has been initialized */
    bool bInitialized;
    
    /** Validation rules by key */
    TMap<FString, FConfigMetadata> ValidationRules;
    
    /** Custom validation functions by key */
    TMap<FString, TFunction<FConfigValidationDetail(const FString&, const FMiningConfigValue&, bool)>> CustomValidationFunctions;
    
    /** Critical section for thread safety */
    mutable FCriticalSection CriticalSection;
    
    /** Static instance for singleton access */
    static UConfigValidator* SingletonInstance;
};