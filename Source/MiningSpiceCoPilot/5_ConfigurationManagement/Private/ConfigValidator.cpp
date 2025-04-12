// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConfigValidator.h"
#include "ConfigManager.h"

// Initialize static instance
UConfigValidator* UConfigValidator::SingletonInstance = nullptr;

UConfigValidator::UConfigValidator()
    : bInitialized(false)
{
    // Create singleton instance if not already created
    if (!SingletonInstance)
    {
        SingletonInstance = this;
    }
}

UConfigValidator::~UConfigValidator()
{
    Shutdown();
    
    // Clear singleton instance if this is the singleton
    if (SingletonInstance == this)
    {
        SingletonInstance = nullptr;
    }
}

bool UConfigValidator::Initialize()
{
    FScopeLock Lock(&CriticalSection);
    
    if (bInitialized)
    {
        return true;
    }
    
    bInitialized = true;
    return true;
}

void UConfigValidator::Shutdown()
{
    FScopeLock Lock(&CriticalSection);
    
    if (!bInitialized)
    {
        return;
    }
    
    // Clear validation rules and custom functions
    ValidationRules.Empty();
    CustomValidationFunctions.Empty();
    
    bInitialized = false;
}

bool UConfigValidator::IsInitialized() const
{
    return bInitialized;
}

FConfigValidationDetail UConfigValidator::ValidateValue(const FString& Key, const FMiningConfigValue& Value, bool bAutoCorrect)
{
    FScopeLock Lock(&CriticalSection);
    
    // Normalize key
    FString NormalizedKey = NormalizeKey(Key);
    
    // Check for custom validation function
    if (CustomValidationFunctions.Contains(NormalizedKey))
    {
        return CustomValidationFunctions[NormalizedKey](NormalizedKey, Value, bAutoCorrect);
    }
    
    // Check for validation rule
    if (ValidationRules.Contains(NormalizedKey))
    {
        const FConfigMetadata& Metadata = ValidationRules[NormalizedKey];
        
        // Validate based on value type
        switch (Value.Type)
        {
        case EConfigValueType::Boolean:
            // Boolean values don't have constraints, just check if required and has a value
            if (Metadata.DefaultValue.Type == EConfigValueType::Boolean)
            {
                return FConfigValidationDetail(); // Valid
            }
            break;
            
        case EConfigValueType::Integer:
        case EConfigValueType::Float:
            return ValidateNumericValue(NormalizedKey, Value, Metadata, bAutoCorrect);
            
        case EConfigValueType::String:
            return ValidateStringValue(NormalizedKey, Value, Metadata, bAutoCorrect);
            
        case EConfigValueType::Vector:
            // Vector validation could check if within a range or has valid components
            // For simplicity, we'll just check if it's not zero when required
            if (Metadata.DefaultValue.Type == EConfigValueType::Vector && Value.VectorValue.IsZero())
            {
                if (bAutoCorrect)
                {
                    FConfigValidationDetail Result(NormalizedKey, TEXT("Vector cannot be zero"), Metadata.DefaultValue, EValidationSeverity::Error);
                    Result.bAutoCorrected = true;
                    return Result;
                }
                else
                {
                    return FConfigValidationDetail(NormalizedKey, TEXT("Vector cannot be zero"), EValidationSeverity::Error);
                }
            }
            break;
            
        case EConfigValueType::Rotator:
            // Rotator validation could check for valid rotation range
            // For simplicity, we'll just say all rotators are valid
            break;
            
        case EConfigValueType::Transform:
            // Transform validation could check for scale not being zero or negative
            if (Metadata.DefaultValue.Type == EConfigValueType::Transform && 
                (Value.TransformValue.GetScale3D().X <= 0.0f || 
                 Value.TransformValue.GetScale3D().Y <= 0.0f || 
                 Value.TransformValue.GetScale3D().Z <= 0.0f))
            {
                if (bAutoCorrect)
                {
                    FConfigValidationDetail Result(NormalizedKey, TEXT("Transform scale must be positive"), Metadata.DefaultValue, EValidationSeverity::Error);
                    Result.bAutoCorrected = true;
                    return Result;
                }
                else
                {
                    return FConfigValidationDetail(NormalizedKey, TEXT("Transform scale must be positive"), EValidationSeverity::Error);
                }
            }
            break;
            
        case EConfigValueType::Color:
            // Color validation could check for valid ranges of components
            // For simplicity, we'll just say all colors are valid
            break;
            
        case EConfigValueType::JsonObject:
            // JSON object validation could check for required fields or structure
            // For simplicity, we'll just check if it's valid when required
            if (Metadata.DefaultValue.Type == EConfigValueType::JsonObject && !Value.JsonValue.IsValid())
            {
                if (bAutoCorrect)
                {
                    FConfigValidationDetail Result(NormalizedKey, TEXT("JSON object is not valid"), Metadata.DefaultValue, EValidationSeverity::Error);
                    Result.bAutoCorrected = true;
                    return Result;
                }
                else
                {
                    return FConfigValidationDetail(NormalizedKey, TEXT("JSON object is not valid"), EValidationSeverity::Error);
                }
            }
            break;
        }
    }
    
    // No validation rule found, or validation passed
    return FConfigValidationDetail();
}

bool UConfigValidator::RegisterValidationRule(const FString& Key, const FMiningConfigValue& MinValue, const FMiningConfigValue& MaxValue, bool bRequired, const FMiningConfigValue* DefaultValue)
{
    FScopeLock Lock(&CriticalSection);
    
    // Normalize key
    FString NormalizedKey = NormalizeKey(Key);
    
    // Create metadata
    FConfigMetadata Metadata;
    
    // Set min and max values
    Metadata.MinValue = MinValue;
    Metadata.MaxValue = MaxValue;
    
    // Set default value if provided
    if (DefaultValue != nullptr)
    {
        Metadata.DefaultValue = *DefaultValue;
    }
    else
    {
        // Set a type-appropriate default value based on the minimum value type
        switch (MinValue.Type)
        {
        case EConfigValueType::Boolean:
            Metadata.DefaultValue = FMiningConfigValue(false);
            break;
            
        case EConfigValueType::Integer:
            Metadata.DefaultValue = FMiningConfigValue(MinValue.IntValue);
            break;
            
        case EConfigValueType::Float:
            Metadata.DefaultValue = FMiningConfigValue(MinValue.FloatValue);
            break;
            
        case EConfigValueType::String:
            Metadata.DefaultValue = FMiningConfigValue(TEXT(""));
            break;
            
        case EConfigValueType::Vector:
            Metadata.DefaultValue = FMiningConfigValue();
            Metadata.DefaultValue.Type = EConfigValueType::Vector;
            Metadata.DefaultValue.VectorValue = FVector::ZeroVector;
            break;
            
        case EConfigValueType::Rotator:
            Metadata.DefaultValue = FMiningConfigValue();
            Metadata.DefaultValue.Type = EConfigValueType::Rotator;
            Metadata.DefaultValue.RotatorValue = FRotator::ZeroRotator;
            break;
            
        case EConfigValueType::Transform:
            Metadata.DefaultValue = FMiningConfigValue();
            Metadata.DefaultValue.Type = EConfigValueType::Transform;
            Metadata.DefaultValue.TransformValue = FTransform::Identity;
            break;
            
        case EConfigValueType::Color:
            Metadata.DefaultValue = FMiningConfigValue();
            Metadata.DefaultValue.Type = EConfigValueType::Color;
            Metadata.DefaultValue.ColorValue = FLinearColor::White;
            break;
            
        case EConfigValueType::JsonObject:
            Metadata.DefaultValue = FMiningConfigValue();
            Metadata.DefaultValue.Type = EConfigValueType::JsonObject;
            Metadata.DefaultValue.JsonValue = MakeShareable(new FJsonObject());
            break;
        }
    }
    
    // Add or update validation rule
    ValidationRules.Add(NormalizedKey, Metadata);
    
    // Remove any custom validation function for this key
    if (CustomValidationFunctions.Contains(NormalizedKey))
    {
        CustomValidationFunctions.Remove(NormalizedKey);
    }
    
    return true;
}

bool UConfigValidator::RegisterStringValidationRule(const FString& Key, const TArray<FString>& AllowedValues, bool bRequired, const FString* DefaultValue)
{
    FScopeLock Lock(&CriticalSection);
    
    // Normalize key
    FString NormalizedKey = NormalizeKey(Key);
    
    // Create metadata
    FConfigMetadata Metadata;
    
    // Store allowed values in the description
    FString AllowedValuesStr = TEXT("Allowed Values: ");
    for (int32 i = 0; i < AllowedValues.Num(); ++i)
    {
        if (i > 0)
        {
            AllowedValuesStr += TEXT(", ");
        }
        AllowedValuesStr += AllowedValues[i];
    }
    Metadata.Description = AllowedValuesStr;
    
    // Store allowed values in JSON value
    TSharedPtr<FJsonObject> JsonAllowedValues = MakeShareable(new FJsonObject());
    for (int32 i = 0; i < AllowedValues.Num(); ++i)
    {
        JsonAllowedValues->SetStringField(FString::Printf(TEXT("%d"), i), AllowedValues[i]);
    }
    
    // Set min and max values (using JSON to store allowed values)
    Metadata.MinValue = FMiningConfigValue();
    Metadata.MinValue.Type = EConfigValueType::JsonObject;
    Metadata.MinValue.JsonValue = JsonAllowedValues;
    
    Metadata.MaxValue = FMiningConfigValue();
    Metadata.MaxValue.Type = EConfigValueType::JsonObject;
    Metadata.MaxValue.JsonValue = JsonAllowedValues;
    
    // Set default value if provided
    if (DefaultValue != nullptr)
    {
        Metadata.DefaultValue = FMiningConfigValue(*DefaultValue);
    }
    else if (AllowedValues.Num() > 0)
    {
        // Use first allowed value as default
        Metadata.DefaultValue = FMiningConfigValue(AllowedValues[0]);
    }
    else
    {
        // Empty default
        Metadata.DefaultValue = FMiningConfigValue(TEXT(""));
    }
    
    // Add or update validation rule
    ValidationRules.Add(NormalizedKey, Metadata);
    
    // Remove any custom validation function for this key
    if (CustomValidationFunctions.Contains(NormalizedKey))
    {
        CustomValidationFunctions.Remove(NormalizedKey);
    }
    
    return true;
}

bool UConfigValidator::RegisterCustomValidationRule(const FString& Key, TFunction<FConfigValidationDetail(const FString&, const FMiningConfigValue&, bool)> ValidationFunc)
{
    FScopeLock Lock(&CriticalSection);
    
    // Normalize key
    FString NormalizedKey = NormalizeKey(Key);
    
    // Add or update custom validation function
    CustomValidationFunctions.Add(NormalizedKey, ValidationFunc);
    
    // Remove any standard validation rule for this key
    if (ValidationRules.Contains(NormalizedKey))
    {
        ValidationRules.Remove(NormalizedKey);
    }
    
    return true;
}

FConfigValidationSummary UConfigValidator::ValidateAll(IConfigManager* ConfigManager, bool bAutoCorrect)
{
    FScopeLock Lock(&CriticalSection);
    
    FConfigValidationSummary Summary;
    
    if (!ConfigManager)
    {
        UE_LOG(LogTemp, Warning, TEXT("ConfigValidator: Cannot validate, ConfigManager is null"));
        return Summary;
    }
    
    // Get all keys with validation rules
    TArray<FString> RuleKeys;
    ValidationRules.GetKeys(RuleKeys);
    
    // Validate each key
    for (const FString& Key : RuleKeys)
    {
        FMiningConfigValue Value;
        if (ConfigManager->GetValue(Key, Value))
        {
            FConfigValidationDetail Result = ValidateValue(Key, Value, bAutoCorrect);
            Summary.AddResult(Result);
            
            // Apply correction if auto-correct is enabled and a correction was made
            if (bAutoCorrect && !Result.bIsValid && Result.bAutoCorrected)
            {
                ConfigManager->SetValue(Key, Result.SuggestedValue, EConfigSourcePriority::Default);
            }
        }
    }
    
    // Get all keys with custom validation functions
    TArray<FString> CustomKeys;
    CustomValidationFunctions.GetKeys(CustomKeys);
    
    // Validate each key with custom functions
    for (const FString& Key : CustomKeys)
    {
        FMiningConfigValue Value;
        if (ConfigManager->GetValue(Key, Value))
        {
            FConfigValidationDetail Result = ValidateValue(Key, Value, bAutoCorrect);
            Summary.AddResult(Result);
            
            // Apply correction if auto-correct is enabled and a correction was made
            if (bAutoCorrect && !Result.bIsValid && Result.bAutoCorrected)
            {
                ConfigManager->SetValue(Key, Result.SuggestedValue, EConfigSourcePriority::Default);
            }
        }
    }
    
    return Summary;
}

FConfigValidationSummary UConfigValidator::ValidateSection(IConfigManager* ConfigManager, const FString& SectionKey, bool bRecursive, bool bAutoCorrect)
{
    FScopeLock Lock(&CriticalSection);
    
    FConfigValidationSummary Summary;
    
    if (!ConfigManager)
    {
        UE_LOG(LogTemp, Warning, TEXT("ConfigValidator: Cannot validate, ConfigManager is null"));
        return Summary;
    }
    
    // Normalize section key
    FString NormalizedSectionKey = NormalizeKey(SectionKey);
    if (!NormalizedSectionKey.IsEmpty() && !NormalizedSectionKey.EndsWith(TEXT(".")))
    {
        NormalizedSectionKey += TEXT(".");
    }
    
    // Get all keys in the section
    TArray<FString> SectionKeys = ConfigManager->GetKeysInSection(NormalizedSectionKey, bRecursive);
    
    // Validate each key in the section
    for (const FString& Key : SectionKeys)
    {
        // Only validate if we have a rule or custom function for this key
        if (ValidationRules.Contains(Key) || CustomValidationFunctions.Contains(Key))
        {
            FMiningConfigValue Value;
            if (ConfigManager->GetValue(Key, Value))
            {
                FConfigValidationDetail Result = ValidateValue(Key, Value, bAutoCorrect);
                Summary.AddResult(Result);
                
                // Apply correction if auto-correct is enabled and a correction was made
                if (bAutoCorrect && !Result.bIsValid && Result.bAutoCorrected)
                {
                    ConfigManager->SetValue(Key, Result.SuggestedValue, EConfigSourcePriority::Default);
                }
            }
        }
    }
    
    return Summary;
}

TMap<FString, FConfigMetadata> UConfigValidator::GetAllValidationRules() const
{
    FScopeLock Lock(&CriticalSection);
    return ValidationRules;
}

const FConfigMetadata* UConfigValidator::GetValidationRule(const FString& Key) const
{
    FScopeLock Lock(&CriticalSection);
    
    // Normalize key
    FString NormalizedKey = NormalizeKey(Key);
    
    // Check if rule exists
    if (ValidationRules.Contains(NormalizedKey))
    {
        return &ValidationRules[NormalizedKey];
    }
    
    return nullptr;
}

bool UConfigValidator::RemoveValidationRule(const FString& Key)
{
    FScopeLock Lock(&CriticalSection);
    
    // Normalize key
    FString NormalizedKey = NormalizeKey(Key);
    
    // Remove rule if exists
    bool bRemovedRule = ValidationRules.Remove(NormalizedKey) > 0;
    
    // Remove custom function if exists
    bool bRemovedFunc = CustomValidationFunctions.Remove(NormalizedKey) > 0;
    
    return bRemovedRule || bRemovedFunc;
}

IConfigValidator& UConfigValidator::Get()
{
    if (!SingletonInstance)
    {
        SingletonInstance = NewObject<UConfigValidator>();
        SingletonInstance->AddToRoot(); // Prevent garbage collection
    }
    
    return *SingletonInstance;
}

FConfigValidationDetail UConfigValidator::ValidateNumericValue(const FString& Key, const FMiningConfigValue& Value, const FConfigMetadata& Metadata, bool bAutoCorrect)
{
    // Check if integer value is within range
    if (Value.Type == EConfigValueType::Integer)
    {
        // Check minimum value
        if (Metadata.MinValue.Type == EConfigValueType::Integer && Value.IntValue < Metadata.MinValue.IntValue)
        {
            if (bAutoCorrect)
            {
                FConfigValidationDetail Result(Key, FString::Printf(TEXT("Integer value %lld is less than minimum %lld"), Value.IntValue, Metadata.MinValue.IntValue), Metadata.DefaultValue, EValidationSeverity::Error);
                Result.bAutoCorrected = true;
                return Result;
            }
            else
            {
                return FConfigValidationDetail(Key, FString::Printf(TEXT("Integer value %lld is less than minimum %lld"), Value.IntValue, Metadata.MinValue.IntValue), EValidationSeverity::Error);
            }
        }
        
        // Check maximum value
        if (Metadata.MaxValue.Type == EConfigValueType::Integer && Value.IntValue > Metadata.MaxValue.IntValue)
        {
            if (bAutoCorrect)
            {
                FConfigValidationDetail Result(Key, FString::Printf(TEXT("Integer value %lld is greater than maximum %lld"), Value.IntValue, Metadata.MaxValue.IntValue), Metadata.DefaultValue, EValidationSeverity::Error);
                Result.bAutoCorrected = true;
                return Result;
            }
            else
            {
                return FConfigValidationDetail(Key, FString::Printf(TEXT("Integer value %lld is greater than maximum %lld"), Value.IntValue, Metadata.MaxValue.IntValue), EValidationSeverity::Error);
            }
        }
    }
    // Check if float value is within range
    else if (Value.Type == EConfigValueType::Float)
    {
        // Check minimum value
        if (Metadata.MinValue.Type == EConfigValueType::Float && Value.FloatValue < Metadata.MinValue.FloatValue)
        {
            if (bAutoCorrect)
            {
                FConfigValidationDetail Result(Key, FString::Printf(TEXT("Float value %f is less than minimum %f"), Value.FloatValue, Metadata.MinValue.FloatValue), Metadata.DefaultValue, EValidationSeverity::Error);
                Result.bAutoCorrected = true;
                return Result;
            }
            else
            {
                return FConfigValidationDetail(Key, FString::Printf(TEXT("Float value %f is less than minimum %f"), Value.FloatValue, Metadata.MinValue.FloatValue), EValidationSeverity::Error);
            }
        }
        
        // Check maximum value
        if (Metadata.MaxValue.Type == EConfigValueType::Float && Value.FloatValue > Metadata.MaxValue.FloatValue)
        {
            if (bAutoCorrect)
            {
                FConfigValidationDetail Result(Key, FString::Printf(TEXT("Float value %f is greater than maximum %f"), Value.FloatValue, Metadata.MaxValue.FloatValue), Metadata.DefaultValue, EValidationSeverity::Error);
                Result.bAutoCorrected = true;
                return Result;
            }
            else
            {
                return FConfigValidationDetail(Key, FString::Printf(TEXT("Float value %f is greater than maximum %f"), Value.FloatValue, Metadata.MaxValue.FloatValue), EValidationSeverity::Error);
            }
        }
    }
    
    // Validation passed
    return FConfigValidationDetail();
}

FConfigValidationDetail UConfigValidator::ValidateStringValue(const FString& Key, const FMiningConfigValue& Value, const FConfigMetadata& Metadata, bool bAutoCorrect)
{
    // Check if this is an enum-like string with allowed values
    if (Metadata.MinValue.Type == EConfigValueType::JsonObject && Metadata.MinValue.JsonValue.IsValid())
    {
        // Extract allowed values from JSON
        TArray<FString> AllowedValues;
        TArray<FString> FieldNames;
        Metadata.MinValue.JsonValue->Values.GetKeys(FieldNames);
        
        for (const FString& FieldName : FieldNames)
        {
            const TSharedPtr<FJsonValue>* JsonValue = Metadata.MinValue.JsonValue->Values.Find(FieldName);
            if (JsonValue != nullptr && (*JsonValue)->Type == EJson::String)
            {
                AllowedValues.Add((*JsonValue)->AsString());
            }
        }
        
        // Check if string is in allowed values
        if (!AllowedValues.Contains(Value.StringValue))
        {
            FString AllowedValuesStr = TEXT("Allowed Values: ");
            for (int32 i = 0; i < AllowedValues.Num(); ++i)
            {
                if (i > 0)
                {
                    AllowedValuesStr += TEXT(", ");
                }
                AllowedValuesStr += AllowedValues[i];
            }
            
            if (bAutoCorrect)
            {
                FConfigValidationDetail Result(Key, FString::Printf(TEXT("String value '%s' is not in allowed values list. %s"), *Value.StringValue, *AllowedValuesStr), Metadata.DefaultValue, EValidationSeverity::Error);
                Result.bAutoCorrected = true;
                return Result;
            }
            else
            {
                return FConfigValidationDetail(Key, FString::Printf(TEXT("String value '%s' is not in allowed values list. %s"), *Value.StringValue, *AllowedValuesStr), EValidationSeverity::Error);
            }
        }
    }
    
    // Validation passed
    return FConfigValidationDetail();
}

FString UConfigValidator::NormalizeKey(const FString& Key) const
{
    // Remove leading and trailing whitespace
    FString NormalizedKey = Key.TrimStartAndEnd();
    
    // Ensure no double dots
    while (NormalizedKey.Contains(TEXT("..")))
    {
        NormalizedKey = NormalizedKey.Replace(TEXT(".."), TEXT("."));
    }
    
    // Remove leading and trailing dots
    NormalizedKey = NormalizedKey.TrimStartAndEndInline(TEXT("."));
    
    return NormalizedKey;
}