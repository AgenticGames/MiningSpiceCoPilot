// Copyright Epic Games, Inc. All Rights Reserved.

#include "MemoryConfigProvider.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

UMemoryConfigProvider::UMemoryConfigProvider()
    : Super()
{
    // Update provider info
    ProviderInfo.Type = EConfigProviderType::Memory;
    ProviderInfo.Name = TEXT("Memory Config Provider");
    ProviderInfo.Description = TEXT("Stores configuration in memory only, without persistent storage");
}

UMemoryConfigProvider::~UMemoryConfigProvider()
{
    // Nothing special required for cleanup
}

FConfigOperationResult UMemoryConfigProvider::Load()
{
    // Memory provider doesn't load from anywhere, just report success
    FConfigOperationResult Result;
    Result.bSuccess = true;
    Result.AffectedKeyCount = ConfigValues.Num();
    return Result;
}

FConfigOperationResult UMemoryConfigProvider::Save()
{
    // Memory provider doesn't save anywhere, just report success
    FConfigOperationResult Result;
    Result.bSuccess = true;
    Result.AffectedKeyCount = ConfigValues.Num();
    return Result;
}

FConfigOperationResult UMemoryConfigProvider::Reset()
{
    FScopeLock Lock(&CriticalSection);
    
    // Count keys before clearing
    int32 KeyCount = ConfigValues.Num();
    
    // Clear all configuration values
    ConfigValues.Empty();
    
    // Clear key info cache
    KeyInfoCache.Empty();
    
    FConfigOperationResult Result;
    Result.bSuccess = true;
    Result.AffectedKeyCount = KeyCount;
    return Result;
}

bool UMemoryConfigProvider::SetDefaultValuesFromJson(const FString& JsonString)
{
    if (JsonString.IsEmpty())
    {
        return false;
    }
    
    // Parse JSON string
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(JsonReader, JsonObject) || !JsonObject.IsValid())
    {
        return false;
    }
    
    return SetDefaultValuesFromJsonObject(JsonObject);
}

bool UMemoryConfigProvider::SetDefaultValuesFromJsonObject(const TSharedPtr<FJsonObject>& JsonObject)
{
    if (!JsonObject.IsValid())
    {
        return false;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    // Process all keys in the JSON object
    TArray<TPair<FString, TSharedPtr<FJsonValue>>> KeyValuePairs;
    FlattenJsonObject(JsonObject, TEXT(""), KeyValuePairs);
    
    // Set values for each key
    int32 SuccessCount = 0;
    for (const auto& KeyValuePair : KeyValuePairs)
    {
        FConfigValue ConfigValue;
        
        // Convert JSON value to ConfigValue
        if (JsonValueToConfigValue(KeyValuePair.Value, ConfigValue))
        {
            // Add to configuration values
            ConfigValues.Add(KeyValuePair.Key, ConfigValue);
            SuccessCount++;
        }
    }
    
    return SuccessCount > 0;
}

FString UMemoryConfigProvider::ExportToJsonString(bool bPrettyPrint) const
{
    FScopeLock Lock(&CriticalSection);
    
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
    if (bPrettyPrint)
    {
        TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> JsonWriter = 
            TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&OutputString);
        FJsonSerializer::Serialize(RootObject.ToSharedRef(), JsonWriter);
    }
    else
    {
        TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&OutputString);
        FJsonSerializer::Serialize(RootObject.ToSharedRef(), JsonWriter);
    }
    
    return OutputString;
}

void UMemoryConfigProvider::FlattenJsonObject(const TSharedPtr<FJsonObject>& JsonObject, const FString& KeyPrefix, TArray<TPair<FString, TSharedPtr<FJsonValue>>>& OutKeyValuePairs)
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

bool UMemoryConfigProvider::JsonValueToConfigValue(const TSharedPtr<FJsonValue>& JsonValue, FConfigValue& OutValue)
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

void UMemoryConfigProvider::AddValueToJsonObject(TSharedPtr<FJsonObject>& JsonObject, const FString& Key, const FConfigValue& Value) const
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