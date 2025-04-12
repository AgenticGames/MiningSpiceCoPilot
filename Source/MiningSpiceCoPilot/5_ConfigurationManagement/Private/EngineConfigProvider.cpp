// Copyright Epic Games, Inc. All Rights Reserved.

#include "EngineConfigProvider.h"
#include "Misc/ConfigCacheIni.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

UEngineConfigProvider::UEngineConfigProvider()
    : Super()
    , ConfigFileName(TEXT("Game"))
    , bAutoSave(true)
{
    // Update provider info
    ProviderInfo.Type = EConfigProviderType::EngineConfig;
    ProviderInfo.Name = TEXT("Engine Config Provider");
    ProviderInfo.Description = TEXT("Loads and saves configuration from/to Unreal Engine config files");
}

UEngineConfigProvider::~UEngineConfigProvider()
{
    // Auto-save if needed
    if (bInitialized && bAutoSave && !ProviderInfo.bIsReadOnly)
    {
        Save();
    }
}

FConfigOperationResult UEngineConfigProvider::Load()
{
    FScopeLock Lock(&CriticalSection);
    
    FConfigOperationResult Result;
    
    // Check if section name is set
    if (ConfigSectionName.IsEmpty())
    {
        Result.bSuccess = false;
        Result.ErrorMessage = TEXT("No config section name set");
        return Result;
    }
    
    // Clear existing configuration
    ConfigValues.Empty();
    
    // Load properties from engine config
    TArray<FString> Keys;
    GConfig->GetSectionPrivate(*ConfigSectionName, false, true, *ConfigFileName, Keys);
    
    int32 SuccessCount = 0;
    for (const FString& Key : Keys)
    {
        // Get property value
        FString ValueString;
        if (GConfig->GetString(*ConfigSectionName, *Key, ValueString, ConfigFileName))
        {
            // Convert to ConfigValue
            FConfigValue ConfigValue;
            
            // Try to parse as a number first
            if (ValueString.IsNumeric())
            {
                // Check if it's an integer or float
                if (ValueString.Contains(TEXT(".")))
                {
                    // Float value
                    double FloatValue = FCString::Atod(*ValueString);
                    ConfigValue.Type = EConfigValueType::Float;
                    ConfigValue.FloatValue = FloatValue;
                }
                else
                {
                    // Integer value
                    int64 IntValue = FCString::Atoi64(*ValueString);
                    ConfigValue.Type = EConfigValueType::Integer;
                    ConfigValue.IntValue = IntValue;
                }
            }
            // Try to parse as a boolean
            else if (ValueString.Equals(TEXT("true"), ESearchCase::IgnoreCase) || 
                     ValueString.Equals(TEXT("false"), ESearchCase::IgnoreCase))
            {
                bool BoolValue = ValueString.Equals(TEXT("true"), ESearchCase::IgnoreCase);
                ConfigValue.Type = EConfigValueType::Boolean;
                ConfigValue.BoolValue = BoolValue;
            }
            // Try to parse as JSON
            else if ((ValueString.StartsWith(TEXT("{")) && ValueString.EndsWith(TEXT("}"))) || 
                     (ValueString.StartsWith(TEXT("[")) && ValueString.EndsWith(TEXT("]"))))
            {
                TSharedPtr<FJsonObject> JsonValue;
                TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(ValueString);
                if (FJsonSerializer::Deserialize(JsonReader, JsonValue))
                {
                    ConfigValue.Type = EConfigValueType::JsonObject;
                    ConfigValue.JsonValue = JsonValue;
                }
                else
                {
                    // If JSON parsing failed, treat as string
                    ConfigValue.Type = EConfigValueType::String;
                    ConfigValue.StringValue = ValueString;
                }
            }
            // Default to string
            else
            {
                ConfigValue.Type = EConfigValueType::String;
                ConfigValue.StringValue = ValueString;
            }
            
            // Add normalized key to configuration values
            FString NormalizedKey = NormalizeKey(Key);
            ConfigValues.Add(NormalizedKey, ConfigValue);
            SuccessCount++;
        }
    }
    
    // Update result
    Result.bSuccess = SuccessCount > 0 || Keys.Num() == 0; // Empty section is still a success
    Result.AffectedKeyCount = SuccessCount;
    
    if (!Result.bSuccess && Keys.Num() > 0)
    {
        Result.ErrorMessage = FString::Printf(TEXT("Failed to load any values from config section: %s"), *ConfigSectionName);
    }
    
    return Result;
}

FConfigOperationResult UEngineConfigProvider::Save()
{
    FScopeLock Lock(&CriticalSection);
    
    FConfigOperationResult Result;
    
    // Check if section name is set
    if (ConfigSectionName.IsEmpty())
    {
        Result.bSuccess = false;
        Result.ErrorMessage = TEXT("No config section name set");
        return Result;
    }
    
    // Check if provider is read-only
    if (ProviderInfo.bIsReadOnly)
    {
        Result.bSuccess = false;
        Result.ErrorMessage = TEXT("Provider is read-only");
        return Result;
    }
    
    int32 SavedCount = 0;
    
    // Save each configuration value to the engine config
    for (const auto& Pair : ConfigValues)
    {
        const FString& Key = Pair.Key;
        const FConfigValue& Value = Pair.Value;
        
        FString ValueString;
        
        // Convert value to string based on type
        switch (Value.Type)
        {
        case EConfigValueType::Boolean:
            ValueString = Value.BoolValue ? TEXT("true") : TEXT("false");
            break;
            
        case EConfigValueType::Integer:
            ValueString = FString::Printf(TEXT("%lld"), Value.IntValue);
            break;
            
        case EConfigValueType::Float:
            ValueString = FString::Printf(TEXT("%f"), Value.FloatValue);
            break;
            
        case EConfigValueType::String:
            ValueString = Value.StringValue;
            break;
            
        case EConfigValueType::Vector:
            {
                TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject());
                JsonObject->SetNumberField(TEXT("X"), Value.VectorValue.X);
                JsonObject->SetNumberField(TEXT("Y"), Value.VectorValue.Y);
                JsonObject->SetNumberField(TEXT("Z"), Value.VectorValue.Z);
                
                // Serialize to string
                FString JsonString;
                TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&JsonString);
                if (FJsonSerializer::Serialize(JsonObject.ToSharedRef(), JsonWriter))
                {
                    ValueString = JsonString;
                }
                else
                {
                    // Fallback to comma-separated format if JSON serialization fails
                    ValueString = FString::Printf(TEXT("%f,%f,%f"), Value.VectorValue.X, Value.VectorValue.Y, Value.VectorValue.Z);
                }
            }
            break;
            
        case EConfigValueType::Rotator:
            {
                TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject());
                JsonObject->SetNumberField(TEXT("Pitch"), Value.RotatorValue.Pitch);
                JsonObject->SetNumberField(TEXT("Yaw"), Value.RotatorValue.Yaw);
                JsonObject->SetNumberField(TEXT("Roll"), Value.RotatorValue.Roll);
                
                // Serialize to string
                FString JsonString;
                TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&JsonString);
                if (FJsonSerializer::Serialize(JsonObject.ToSharedRef(), JsonWriter))
                {
                    ValueString = JsonString;
                }
                else
                {
                    // Fallback to comma-separated format if JSON serialization fails
                    ValueString = FString::Printf(TEXT("%f,%f,%f"), Value.RotatorValue.Pitch, Value.RotatorValue.Yaw, Value.RotatorValue.Roll);
                }
            }
            break;
            
        case EConfigValueType::Color:
            {
                TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject());
                JsonObject->SetNumberField(TEXT("R"), Value.ColorValue.R);
                JsonObject->SetNumberField(TEXT("G"), Value.ColorValue.G);
                JsonObject->SetNumberField(TEXT("B"), Value.ColorValue.B);
                JsonObject->SetNumberField(TEXT("A"), Value.ColorValue.A);
                
                // Serialize to string
                FString JsonString;
                TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&JsonString);
                if (FJsonSerializer::Serialize(JsonObject.ToSharedRef(), JsonWriter))
                {
                    ValueString = JsonString;
                }
                else
                {
                    // Fallback to comma-separated format if JSON serialization fails
                    ValueString = FString::Printf(TEXT("%f,%f,%f,%f"), Value.ColorValue.R, Value.ColorValue.G, Value.ColorValue.B, Value.ColorValue.A);
                }
            }
            break;
            
        case EConfigValueType::JsonObject:
            {
                if (Value.JsonValue.IsValid())
                {
                    // Serialize to string
                    FString JsonString;
                    TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&JsonString);
                    if (FJsonSerializer::Serialize(Value.JsonValue.ToSharedRef(), JsonWriter))
                    {
                        ValueString = JsonString;
                    }
                    else
                    {
                        // Skip if serialization fails
                        continue;
                    }
                }
                else
                {
                    // Skip if no valid JSON value
                    continue;
                }
            }
            break;
            
        default:
            // Skip unsupported types
            continue;
        }
        
        // Save to engine config
        GConfig->SetString(*ConfigSectionName, *Key, *ValueString, ConfigFileName);
        SavedCount++;
    }
    
    // Save config file
    GConfig->Flush(false, ConfigFileName);
    
    // Update result
    Result.bSuccess = SavedCount > 0 || ConfigValues.Num() == 0; // Empty config is still a success
    Result.AffectedKeyCount = SavedCount;
    
    return Result;
}

void UEngineConfigProvider::SetConfigSectionName(const FString& InConfigSectionName, bool bAutoLoad)
{
    FScopeLock Lock(&CriticalSection);
    
    // Save current section if auto-save is enabled
    if (!ConfigSectionName.IsEmpty() && bInitialized && bAutoSave && !ProviderInfo.bIsReadOnly)
    {
        Save();
    }
    
    // Update section name
    ConfigSectionName = InConfigSectionName;
    
    // Auto-load if requested
    if (bAutoLoad && bInitialized)
    {
        Load();
    }
}

FString UEngineConfigProvider::GetConfigSectionName() const
{
    FScopeLock Lock(&CriticalSection);
    return ConfigSectionName;
}

void UEngineConfigProvider::SetConfigFileName(const FString& InConfigFileName, bool bAutoLoad)
{
    FScopeLock Lock(&CriticalSection);
    
    // Save current config if auto-save is enabled
    if (!ConfigSectionName.IsEmpty() && bInitialized && bAutoSave && !ProviderInfo.bIsReadOnly)
    {
        Save();
    }
    
    // Update config file name
    ConfigFileName = InConfigFileName;
    
    // Auto-load if requested
    if (bAutoLoad && bInitialized && !ConfigSectionName.IsEmpty())
    {
        Load();
    }
}

FString UEngineConfigProvider::GetConfigFileName() const
{
    FScopeLock Lock(&CriticalSection);
    return ConfigFileName;
}

void UEngineConfigProvider::SetAutoSave(bool bInAutoSave)
{
    FScopeLock Lock(&CriticalSection);
    bAutoSave = bInAutoSave;
}

bool UEngineConfigProvider::GetAutoSave() const
{
    FScopeLock Lock(&CriticalSection);
    return bAutoSave;
}