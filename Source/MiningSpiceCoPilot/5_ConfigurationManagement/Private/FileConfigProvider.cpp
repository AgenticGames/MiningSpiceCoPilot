// Copyright Epic Games, Inc. All Rights Reserved.

#include "FileConfigProvider.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HAL/FileManager.h"

UFileConfigProvider::UFileConfigProvider()
    : Super()
    , bAutoSave(true)
    , IndentLevel(4)
    , bFileExists(false)
{
    // Update provider type
    ProviderInfo.Type = EConfigProviderType::File;
    ProviderInfo.Name = TEXT("File Config Provider");
    ProviderInfo.Description = TEXT("Loads and saves configuration from/to files in JSON format");
}

UFileConfigProvider::~UFileConfigProvider()
{
    // Base class will handle shutdown and auto-save if needed
}

FConfigOperationResult UFileConfigProvider::Load()
{
    FScopeLock Lock(&CriticalSection);
    
    FConfigOperationResult Result;
    
    // Check if file path is set
    if (FilePath.IsEmpty())
    {
        Result.bSuccess = false;
        Result.ErrorMessage = TEXT("No file path set");
        return Result;
    }
    
    // Check if file exists
    bFileExists = IFileManager::Get().FileExists(*FilePath);
    if (!bFileExists)
    {
        Result.bSuccess = false;
        Result.ErrorMessage = FString::Printf(TEXT("File not found: %s"), *FilePath);
        return Result;
    }
    
    // Read file content
    FString FileContent;
    if (!FFileHelper::LoadFileToString(FileContent, *FilePath))
    {
        Result.bSuccess = false;
        Result.ErrorMessage = FString::Printf(TEXT("Failed to read file: %s"), *FilePath);
        return Result;
    }
    
    // Parse JSON
    TSharedPtr<FJsonObject> JsonConfig;
    TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(FileContent);
    if (!FJsonSerializer::Deserialize(JsonReader, JsonConfig) || !JsonConfig.IsValid())
    {
        Result.bSuccess = false;
        Result.ErrorMessage = FString::Printf(TEXT("Failed to parse JSON from file: %s"), *FilePath);
        return Result;
    }
    
    // Clear existing configuration
    ConfigValues.Empty();
    
    // Process all keys in the JSON object
    TArray<TPair<FString, TSharedPtr<FJsonValue>>> KeyValuePairs;
    FlattenJsonObject(JsonConfig, TEXT(""), KeyValuePairs);
    
    // Set values for each key
    int32 SuccessCount = 0;
    for (const auto& KeyValuePair : KeyValuePairs)
    {
        FConfigValue ConfigValue;
        
        // Convert JSON value to FConfigValue
        if (JsonValueToConfigValue(KeyValuePair.Value, ConfigValue))
        {
            // Add to configuration values
            ConfigValues.Add(KeyValuePair.Key, ConfigValue);
            SuccessCount++;
        }
    }
    
    // Update result
    Result.bSuccess = SuccessCount > 0 || KeyValuePairs.Num() == 0; // Empty file is still a success
    Result.AffectedKeyCount = SuccessCount;
    
    if (!Result.bSuccess)
    {
        Result.ErrorMessage = FString::Printf(TEXT("Failed to load any values from file: %s"), *FilePath);
    }
    
    return Result;
}

FConfigOperationResult UFileConfigProvider::Save()
{
    FScopeLock Lock(&CriticalSection);
    
    FConfigOperationResult Result;
    
    // Check if file path is set
    if (FilePath.IsEmpty())
    {
        Result.bSuccess = false;
        Result.ErrorMessage = TEXT("No file path set");
        return Result;
    }
    
    // Check if provider is read-only
    if (ProviderInfo.bIsReadOnly)
    {
        Result.bSuccess = false;
        Result.ErrorMessage = TEXT("Provider is read-only");
        return Result;
    }
    
    // Create root JSON object
    TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject());
    
    // Process all keys
    for (const auto& Pair : ConfigValues)
    {
        const FString& Key = Pair.Key;
        const FConfigValue& Value = Pair.Value;
        
        // Add to JSON object
        AddValueToJsonObject(RootObject, Key, Value);
    }
    
    // Serialize JSON to string
    FString OutputString;
    TSharedRef<TJsonWriter<>> JsonWriter;
    
    if (IndentLevel > 0)
    {
        JsonWriter = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&OutputString, IndentLevel);
    }
    else
    {
        JsonWriter = TJsonWriterFactory<>::Create(&OutputString);
    }
    
    if (FJsonSerializer::Serialize(RootObject.ToSharedRef(), JsonWriter))
    {
        // Create directory if it doesn't exist
        FString Directory = FPaths::GetPath(FilePath);
        if (!Directory.IsEmpty())
        {
            IFileManager::Get().MakeDirectory(*Directory, true);
        }
        
        // Write to file
        if (FFileHelper::SaveStringToFile(OutputString, *FilePath))
        {
            bFileExists = true;
            Result.bSuccess = true;
            Result.AffectedKeyCount = ConfigValues.Num();
        }
        else
        {
            Result.bSuccess = false;
            Result.ErrorMessage = FString::Printf(TEXT("Failed to write to file: %s"), *FilePath);
        }
    }
    else
    {
        Result.bSuccess = false;
        Result.ErrorMessage = TEXT("Failed to serialize configuration to JSON");
    }
    
    return Result;
}

void UFileConfigProvider::SetFilePath(const FString& InFilePath, bool bAutoLoad)
{
    FScopeLock Lock(&CriticalSection);
    
    FilePath = InFilePath;
    
    // Check if the file exists
    bFileExists = IFileManager::Get().FileExists(*FilePath);
    
    // Auto-load if requested and file exists
    if (bAutoLoad && bFileExists)
    {
        Load();
    }
}

FString UFileConfigProvider::GetFilePath() const
{
    FScopeLock Lock(&CriticalSection);
    return FilePath;
}

void UFileConfigProvider::SetAutoSave(bool bInAutoSave)
{
    FScopeLock Lock(&CriticalSection);
    bAutoSave = bInAutoSave;
}

bool UFileConfigProvider::GetAutoSave() const
{
    FScopeLock Lock(&CriticalSection);
    return bAutoSave;
}

void UFileConfigProvider::SetIndentLevel(int32 InIndentLevel)
{
    FScopeLock Lock(&CriticalSection);
    IndentLevel = FMath::Max(0, InIndentLevel);
}

int32 UFileConfigProvider::GetIndentLevel() const
{
    FScopeLock Lock(&CriticalSection);
    return IndentLevel;
}

void UFileConfigProvider::FlattenJsonObject(const TSharedPtr<FJsonObject>& JsonObject, const FString& KeyPrefix, TArray<TPair<FString, TSharedPtr<FJsonValue>>>& OutKeyValuePairs)
{
    if (!JsonObject.IsValid())
    {
        return;
    }
    
    for (const auto& KVP : JsonObject->Values)
    {
        FString FullKey = KeyPrefix.IsEmpty() ? KVP.Key : KeyPrefix + TEXT(".") + KVP.Key;
        
        if (KVP.Value->Type == EJson::Object)
        {
            // Recursively process nested objects
            FlattenJsonObject(KVP.Value->AsObject(), FullKey, OutKeyValuePairs);
        }
        else
        {
            // Add leaf value
            OutKeyValuePairs.Add(TPair<FString, TSharedPtr<FJsonValue>>(FullKey, KVP.Value));
        }
    }
}

bool UFileConfigProvider::JsonValueToConfigValue(const TSharedPtr<FJsonValue>& JsonValue, FConfigValue& OutValue)
{
    if (!JsonValue.IsValid())
    {
        return false;
    }
    
    // Convert based on JSON value type
    switch (JsonValue->Type)
    {
    case EJson::Boolean:
        OutValue.Type = EConfigValueType::Boolean;
        OutValue.BoolValue = JsonValue->AsBool();
        return true;
        
    case EJson::Number:
        {
            double NumValue = JsonValue->AsNumber();
            int64 IntValue = static_cast<int64>(NumValue);
            
            // Check if the number is an integer (by comparing with its integer cast)
            if (FMath::IsNearlyEqual(NumValue, static_cast<double>(IntValue)))
            {
                OutValue.Type = EConfigValueType::Integer;
                OutValue.IntValue = IntValue;
            }
            else
            {
                OutValue.Type = EConfigValueType::Float;
                OutValue.FloatValue = NumValue;
            }
            return true;
        }
        
    case EJson::String:
        OutValue.Type = EConfigValueType::String;
        OutValue.StringValue = JsonValue->AsString();
        return true;
        
    case EJson::Object:
        OutValue.Type = EConfigValueType::JsonObject;
        OutValue.JsonValue = JsonValue->AsObject();
        return true;
        
    case EJson::Array:
        {
            // Convert array to JsonObject
            TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject());
            TArray<TSharedPtr<FJsonValue>> Array = JsonValue->AsArray();
            
            for (int32 i = 0; i < Array.Num(); ++i)
            {
                JsonObject->SetField(FString::Printf(TEXT("%d"), i), Array[i]);
            }
            
            OutValue.Type = EConfigValueType::JsonObject;
            OutValue.JsonValue = JsonObject;
            return true;
        }
        
    case EJson::Null:
        // Handle null as empty string
        OutValue.Type = EConfigValueType::String;
        OutValue.StringValue = TEXT("");
        return true;
        
    default:
        return false;
    }
}

void UFileConfigProvider::AddValueToJsonObject(TSharedPtr<FJsonObject>& JsonObject, const FString& Key, const FConfigValue& Value)
{
    // Split key into components
    TArray<FString> KeyParts;
    Key.ParseIntoArray(KeyParts, TEXT("."));
    
    // Navigate to the correct nested object
    TSharedPtr<FJsonObject> CurrentObject = JsonObject;
    for (int32 i = 0; i < KeyParts.Num() - 1; ++i)
    {
        const FString& Part = KeyParts[i];
        
        // Check if this part already exists
        TSharedPtr<FJsonValue> ExistingValue = CurrentObject->TryGetField(Part);
        TSharedPtr<FJsonObject> NestedObject;
        
        if (ExistingValue.IsValid() && ExistingValue->Type == EJson::Object)
        {
            // Use existing object
            NestedObject = ExistingValue->AsObject();
        }
        else
        {
            // Create new object
            NestedObject = MakeShareable(new FJsonObject());
            CurrentObject->SetObjectField(Part, NestedObject);
        }
        
        CurrentObject = NestedObject;
    }
    
    // Add the value to the final object
    const FString& LastPart = KeyParts.Last();
    
    switch (Value.Type)
    {
    case EConfigValueType::Boolean:
        CurrentObject->SetBoolField(LastPart, Value.BoolValue);
        break;
        
    case EConfigValueType::Integer:
        CurrentObject->SetNumberField(LastPart, static_cast<double>(Value.IntValue));
        break;
        
    case EConfigValueType::Float:
        CurrentObject->SetNumberField(LastPart, Value.FloatValue);
        break;
        
    case EConfigValueType::String:
        CurrentObject->SetStringField(LastPart, Value.StringValue);
        break;
        
    case EConfigValueType::Vector:
        {
            TSharedPtr<FJsonObject> VectorObject = MakeShareable(new FJsonObject());
            VectorObject->SetNumberField(TEXT("X"), Value.VectorValue.X);
            VectorObject->SetNumberField(TEXT("Y"), Value.VectorValue.Y);
            VectorObject->SetNumberField(TEXT("Z"), Value.VectorValue.Z);
            CurrentObject->SetObjectField(LastPart, VectorObject);
        }
        break;
        
    case EConfigValueType::Rotator:
        {
            TSharedPtr<FJsonObject> RotatorObject = MakeShareable(new FJsonObject());
            RotatorObject->SetNumberField(TEXT("Pitch"), Value.RotatorValue.Pitch);
            RotatorObject->SetNumberField(TEXT("Yaw"), Value.RotatorValue.Yaw);
            RotatorObject->SetNumberField(TEXT("Roll"), Value.RotatorValue.Roll);
            CurrentObject->SetObjectField(LastPart, RotatorObject);
        }
        break;
        
    case EConfigValueType::Transform:
        {
            TSharedPtr<FJsonObject> TransformObject = MakeShareable(new FJsonObject());
            
            // Translation
            TSharedPtr<FJsonObject> TranslationObject = MakeShareable(new FJsonObject());
            TranslationObject->SetNumberField(TEXT("X"), Value.TransformValue.GetTranslation().X);
            TranslationObject->SetNumberField(TEXT("Y"), Value.TransformValue.GetTranslation().Y);
            TranslationObject->SetNumberField(TEXT("Z"), Value.TransformValue.GetTranslation().Z);
            TransformObject->SetObjectField(TEXT("Translation"), TranslationObject);
            
            // Rotation
            TSharedPtr<FJsonObject> RotationObject = MakeShareable(new FJsonObject());
            RotationObject->SetNumberField(TEXT("X"), Value.TransformValue.GetRotation().X);
            RotationObject->SetNumberField(TEXT("Y"), Value.TransformValue.GetRotation().Y);
            RotationObject->SetNumberField(TEXT("Z"), Value.TransformValue.GetRotation().Z);
            RotationObject->SetNumberField(TEXT("W"), Value.TransformValue.GetRotation().W);
            TransformObject->SetObjectField(TEXT("Rotation"), RotationObject);
            
            // Scale
            TSharedPtr<FJsonObject> ScaleObject = MakeShareable(new FJsonObject());
            ScaleObject->SetNumberField(TEXT("X"), Value.TransformValue.GetScale3D().X);
            ScaleObject->SetNumberField(TEXT("Y"), Value.TransformValue.GetScale3D().Y);
            ScaleObject->SetNumberField(TEXT("Z"), Value.TransformValue.GetScale3D().Z);
            TransformObject->SetObjectField(TEXT("Scale"), ScaleObject);
            
            CurrentObject->SetObjectField(LastPart, TransformObject);
        }
        break;
        
    case EConfigValueType::Color:
        {
            TSharedPtr<FJsonObject> ColorObject = MakeShareable(new FJsonObject());
            ColorObject->SetNumberField(TEXT("R"), Value.ColorValue.R);
            ColorObject->SetNumberField(TEXT("G"), Value.ColorValue.G);
            ColorObject->SetNumberField(TEXT("B"), Value.ColorValue.B);
            ColorObject->SetNumberField(TEXT("A"), Value.ColorValue.A);
            CurrentObject->SetObjectField(LastPart, ColorObject);
        }
        break;
        
    case EConfigValueType::JsonObject:
        if (Value.JsonValue.IsValid())
        {
            CurrentObject->SetObjectField(LastPart, Value.JsonValue);
        }
        break;
    }
}