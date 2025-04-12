// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConfigManager.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// Initialize static instance
UConfigManager* UConfigManager::SingletonInstance = nullptr;

UConfigManager::UConfigManager()
    : bInitialized(false)
{
    // Create singleton instance if not already created
    if (!SingletonInstance)
    {
        SingletonInstance = this;
    }
}

UConfigManager::~UConfigManager()
{
    Shutdown();
    
    // Clear singleton instance if this is the singleton
    if (SingletonInstance == this)
    {
        SingletonInstance = nullptr;
    }
}

bool UConfigManager::Initialize()
{
    FScopeLock Lock(&CriticalSection);
    
    if (bInitialized)
    {
        return true;
    }
    
    // Initialize all providers
    for (TSharedPtr<IConfigProvider> Provider : Providers)
    {
        if (Provider.IsValid())
        {
            Provider->Initialize();
        }
    }
    
    // Initialize validator if available
    if (Validator.IsValid())
    {
        Validator->Initialize();
    }
    
    bInitialized = true;
    return true;
}

void UConfigManager::Shutdown()
{
    FScopeLock Lock(&CriticalSection);
    
    if (!bInitialized)
    {
        return;
    }
    
    // Clear value cache
    ValueCache.Empty();
    
    // Clear change callbacks
    ChangeCallbacks.Empty();
    
    // Shutdown all providers
    for (TSharedPtr<IConfigProvider> Provider : Providers)
    {
        if (Provider.IsValid())
        {
            Provider->Shutdown();
        }
    }
    
    // Clear providers
    Providers.Empty();
    
    // Shutdown validator if available
    if (Validator.IsValid())
    {
        Validator->Shutdown();
        Validator.Reset();
    }
    
    bInitialized = false;
}

bool UConfigManager::IsInitialized() const
{
    return bInitialized;
}

bool UConfigManager::LoadFromFile(const FString& FilePath, EConfigSourcePriority Priority)
{
    if (!bInitialized)
    {
        return false;
    }
    
    // Read file content
    FString FileContent;
    if (!FFileHelper::LoadFileToString(FileContent, *FilePath))
    {
        UE_LOG(LogTemp, Warning, TEXT("ConfigManager: Failed to load file %s"), *FilePath);
        return false;
    }
    
    // Parse JSON
    TSharedPtr<FJsonObject> JsonConfig;
    TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(FileContent);
    if (!FJsonSerializer::Deserialize(JsonReader, JsonConfig) || !JsonConfig.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("ConfigManager: Failed to parse JSON from file %s"), *FilePath);
        return false;
    }
    
    // Process all keys in the JSON object
    TArray<TPair<FString, TSharedPtr<FJsonValue>>> KeyValuePairs;
    FlattenJsonObject(JsonConfig, TEXT(""), KeyValuePairs);
    
    int32 SuccessCount = 0;
    int32 TotalCount = 0;
    
    // Set values for each key
    for (const auto& KeyValuePair : KeyValuePairs)
    {
        FMiningConfigValue ConfigValue;
        
        TotalCount++;
        
        // Convert JSON value to FMiningConfigValue
        if (JsonValueToConfigValue(KeyValuePair.Value, ConfigValue))
        {
            // Set source priority
            ConfigValue.SourcePriority = Priority;
            
            // Set the value
            if (SetValue(KeyValuePair.Key, ConfigValue, Priority, EConfigPropagationMode::DirectOnly))
            {
                SuccessCount++;
            }
        }
    }
    
    UE_LOG(LogTemp, Log, TEXT("ConfigManager: Loaded %d/%d values from %s"), SuccessCount, TotalCount, *FilePath);
    return SuccessCount > 0;
}

bool UConfigManager::SaveToFile(const FString& FilePath, bool bOnlyModified, EConfigSourcePriority Priority)
{
    if (!bInitialized)
    {
        return false;
    }
    
    TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject());
    
    // Collect all keys that should be saved
    TArray<FString> AllKeys;
    for (const auto& Provider : Providers)
    {
        if (Provider.IsValid())
        {
            AllKeys.Append(Provider->GetAllKeys());
        }
    }
    
    // Deduplicate keys
    TSet<FString> UniqueKeys;
    for (const FString& Key : AllKeys)
    {
        UniqueKeys.Add(Key);
    }
    
    int32 SavedCount = 0;
    
    // Process each unique key
    for (const FString& Key : UniqueKeys)
    {
        FMiningConfigValue Value;
        if (GetValue(Key, Value))
        {
            // Skip if not at or above the specified priority
            if (Value.SourcePriority < Priority)
            {
                continue;
            }
            
            // Skip if we're only saving modified values and this one isn't modified
            if (bOnlyModified && !Value.bIsOverridden)
            {
                continue;
            }
            
            // Add to JSON object
            AddValueToJsonObject(RootObject, Key, Value);
            SavedCount++;
        }
    }
    
    // Serialize JSON to string
    FString OutputString;
    TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&OutputString);
    if (FJsonSerializer::Serialize(RootObject.ToSharedRef(), JsonWriter))
    {
        // Write to file
        if (FFileHelper::SaveStringToFile(OutputString, *FilePath))
        {
            UE_LOG(LogTemp, Log, TEXT("ConfigManager: Saved %d values to %s"), SavedCount, *FilePath);
            return true;
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("ConfigManager: Failed to write to file %s"), *FilePath);
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("ConfigManager: Failed to serialize JSON"));
    }
    
    return false;
}

bool UConfigManager::GetValue(const FString& Key, FMiningConfigValue& OutValue) const
{
    if (!bInitialized)
    {
        return false;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    // Normalize key
    FString NormalizedKey = NormalizeKey(Key);
    
    // Check cache first
    if (ValueCache.Contains(NormalizedKey))
    {
        OutValue = ValueCache[NormalizedKey];
        return true;
    }
    
    // Not in cache, query providers
    FMiningConfigValue HighestPriorityValue;
    bool bFoundValue = false;
    
    // Check all providers in reverse order (higher priority providers added last)
    for (int32 i = Providers.Num() - 1; i >= 0; --i)
    {
        if (!Providers[i].IsValid())
        {
            continue;
        }
        
        FConfigValue ProviderValue;
        if (Providers[i]->GetValue(NormalizedKey, ProviderValue))
        {
            // Convert FConfigValue to FMiningConfigValue
            FMiningConfigValue CurrentValue;
            ConfigValueToMiningConfigValue(ProviderValue, CurrentValue);
            
            // Update if this is the first value found or has higher priority
            if (!bFoundValue || CurrentValue.SourcePriority > HighestPriorityValue.SourcePriority)
            {
                HighestPriorityValue = CurrentValue;
                bFoundValue = true;
            }
        }
    }
    
    // Update cache if value was found
    if (bFoundValue)
    {
        ValueCache.Add(NormalizedKey, HighestPriorityValue);
        OutValue = HighestPriorityValue;
    }
    
    return bFoundValue;
}

bool UConfigManager::GetBool(const FString& Key, bool DefaultValue) const
{
    FMiningConfigValue Value;
    if (GetValue(Key, Value) && Value.Type == EConfigValueType::Boolean)
    {
        return Value.BoolValue;
    }
    return DefaultValue;
}

int64 UConfigManager::GetInt(const FString& Key, int64 DefaultValue) const
{
    FMiningConfigValue Value;
    if (GetValue(Key, Value) && Value.Type == EConfigValueType::Integer)
    {
        return Value.IntValue;
    }
    return DefaultValue;
}

double UConfigManager::GetFloat(const FString& Key, double DefaultValue) const
{
    FMiningConfigValue Value;
    if (GetValue(Key, Value) && Value.Type == EConfigValueType::Float)
    {
        return Value.FloatValue;
    }
    return DefaultValue;
}

FString UConfigManager::GetString(const FString& Key, const FString& DefaultValue) const
{
    FMiningConfigValue Value;
    if (GetValue(Key, Value) && Value.Type == EConfigValueType::String)
    {
        return Value.StringValue;
    }
    return DefaultValue;
}

FVector UConfigManager::GetVector(const FString& Key, const FVector& DefaultValue) const
{
    FMiningConfigValue Value;
    if (GetValue(Key, Value) && Value.Type == EConfigValueType::Vector)
    {
        return Value.VectorValue;
    }
    return DefaultValue;
}

FLinearColor UConfigManager::GetColor(const FString& Key, const FLinearColor& DefaultValue) const
{
    FMiningConfigValue Value;
    if (GetValue(Key, Value) && Value.Type == EConfigValueType::Color)
    {
        return Value.ColorValue;
    }
    return DefaultValue;
}

TSharedPtr<FJsonObject> UConfigManager::GetJson(const FString& Key) const
{
    FMiningConfigValue Value;
    if (GetValue(Key, Value) && Value.Type == EConfigValueType::JsonObject)
    {
        return Value.JsonValue;
    }
    return nullptr;
}

bool UConfigManager::SetValue(const FString& Key, const FMiningConfigValue& Value, EConfigSourcePriority Priority, EConfigPropagationMode PropagationMode)
{
    if (!bInitialized)
    {
        return false;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    // Normalize key
    FString NormalizedKey = NormalizeKey(Key);
    
    // Check if the value is read-only
    FMiningConfigValue ExistingValue;
    if (GetValue(NormalizedKey, ExistingValue) && ExistingValue.bIsReadOnly && Priority < EConfigSourcePriority::Debug)
    {
        // Can't modify read-only values except with debug priority
        UE_LOG(LogTemp, Warning, TEXT("ConfigManager: Cannot modify read-only value %s"), *NormalizedKey);
        return false;
    }
    
    // Create modified value
    FMiningConfigValue ModifiedValue = Value;
    ModifiedValue.SourcePriority = Priority;
    ModifiedValue.bIsOverridden = true;
    ModifiedValue.LastUpdated = FDateTime::Now();
    
    // Validate value if validator is available
    if (Validator.IsValid())
    {
        FConfigValidationDetail ValidationResult = Validator->ValidateValue(NormalizedKey, ModifiedValue, true);
        if (!ValidationResult.bIsValid && ValidationResult.Severity >= EValidationSeverity::Error)
        {
            // Failed validation
            UE_LOG(LogTemp, Warning, TEXT("ConfigManager: Value for %s failed validation: %s"), *NormalizedKey, *ValidationResult.Message);
            
            if (ValidationResult.bAutoCorrected)
            {
                // Use auto-corrected value
                ModifiedValue = ValidationResult.SuggestedValue;
                UE_LOG(LogTemp, Warning, TEXT("ConfigManager: Value for %s auto-corrected"), *NormalizedKey);
            }
            else
            {
                // Cannot use invalid value without auto-correction
                return false;
            }
        }
    }
    
    // Update cache
    ValueCache.Add(NormalizedKey, ModifiedValue);
    
    // Find provider with the specified priority
    TSharedPtr<IConfigProvider> TargetProvider = nullptr;
    for (TSharedPtr<IConfigProvider> Provider : Providers)
    {
        if (Provider.IsValid() && Provider->GetProviderInfo().Priority == Priority)
        {
            TargetProvider = Provider;
            break;
        }
    }
    
    // If no provider with the exact priority was found, use the highest priority provider
    if (!TargetProvider.IsValid() && Providers.Num() > 0)
    {
        TargetProvider = Providers.Last();
    }
    
    // Set value in provider
    bool bSuccess = false;
    if (TargetProvider.IsValid())
    {
        // Convert FMiningConfigValue to FConfigValue
        FConfigValue ProviderValue;
        MiningConfigValueToConfigValue(ModifiedValue, ProviderValue);
        
        // Set in provider
        FConfigOperationResult Result = TargetProvider->SetValue(NormalizedKey, ProviderValue);
        bSuccess = Result.bSuccess;
        
        if (!bSuccess)
        {
            UE_LOG(LogTemp, Warning, TEXT("ConfigManager: Failed to set value for %s: %s"), *NormalizedKey, *Result.ErrorMessage);
        }
    }
    
    // Notify subscribers of the change
    if (bSuccess)
    {
        NotifyValueChanged(NormalizedKey, ModifiedValue, PropagationMode);
    }
    
    return bSuccess;
}

bool UConfigManager::SetBool(const FString& Key, bool Value, EConfigSourcePriority Priority, EConfigPropagationMode PropagationMode)
{
    return SetValue(Key, FMiningConfigValue(Value, Priority), Priority, PropagationMode);
}

bool UConfigManager::SetInt(const FString& Key, int64 Value, EConfigSourcePriority Priority, EConfigPropagationMode PropagationMode)
{
    return SetValue(Key, FMiningConfigValue(Value, Priority), Priority, PropagationMode);
}

bool UConfigManager::SetFloat(const FString& Key, double Value, EConfigSourcePriority Priority, EConfigPropagationMode PropagationMode)
{
    return SetValue(Key, FMiningConfigValue(Value, Priority), Priority, PropagationMode);
}

bool UConfigManager::SetString(const FString& Key, const FString& Value, EConfigSourcePriority Priority, EConfigPropagationMode PropagationMode)
{
    return SetValue(Key, FMiningConfigValue(Value, Priority), Priority, PropagationMode);
}

bool UConfigManager::SetVector(const FString& Key, const FVector& Value, EConfigSourcePriority Priority, EConfigPropagationMode PropagationMode)
{
    FMiningConfigValue ConfigValue;
    ConfigValue.Type = EConfigValueType::Vector;
    ConfigValue.VectorValue = Value;
    ConfigValue.SourcePriority = Priority;
    return SetValue(Key, ConfigValue, Priority, PropagationMode);
}

bool UConfigManager::SetColor(const FString& Key, const FLinearColor& Value, EConfigSourcePriority Priority, EConfigPropagationMode PropagationMode)
{
    FMiningConfigValue ConfigValue;
    ConfigValue.Type = EConfigValueType::Color;
    ConfigValue.ColorValue = Value;
    ConfigValue.SourcePriority = Priority;
    return SetValue(Key, ConfigValue, Priority, PropagationMode);
}

bool UConfigManager::SetJson(const FString& Key, TSharedPtr<FJsonObject> Value, EConfigSourcePriority Priority, EConfigPropagationMode PropagationMode)
{
    if (!Value.IsValid())
    {
        return false;
    }
    
    FMiningConfigValue ConfigValue;
    ConfigValue.Type = EConfigValueType::JsonObject;
    ConfigValue.JsonValue = Value;
    ConfigValue.SourcePriority = Priority;
    return SetValue(Key, ConfigValue, Priority, PropagationMode);
}

bool UConfigManager::RemoveValue(const FString& Key, EConfigSourcePriority Priority, EConfigPropagationMode PropagationMode)
{
    if (!bInitialized)
    {
        return false;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    // Normalize key
    FString NormalizedKey = NormalizeKey(Key);
    
    // Check if the value is read-only
    FMiningConfigValue ExistingValue;
    if (GetValue(NormalizedKey, ExistingValue))
    {
        if (ExistingValue.bIsReadOnly && Priority < EConfigSourcePriority::Debug)
        {
            // Can't remove read-only values except with debug priority
            UE_LOG(LogTemp, Warning, TEXT("ConfigManager: Cannot remove read-only value %s"), *NormalizedKey);
            return false;
        }
        
        // Remove from cache
        ValueCache.Remove(NormalizedKey);
        
        // Find providers with priority less than or equal to the specified priority
        bool bRemovedAny = false;
        for (TSharedPtr<IConfigProvider> Provider : Providers)
        {
            if (Provider.IsValid() && Provider->GetProviderInfo().Priority <= Priority)
            {
                FConfigOperationResult Result = Provider->RemoveValue(NormalizedKey);
                if (Result.bSuccess)
                {
                    bRemovedAny = true;
                }
            }
        }
        
        // Notify subscribers of the change if any value was removed
        if (bRemovedAny)
        {
            // Get the new effective value (if any)
            FMiningConfigValue NewValue;
            bool bHasNewValue = GetValue(NormalizedKey, NewValue);
            
            // If there's a new effective value, notify with that
            // If there's no new value, notify with an empty default value
            NotifyValueChanged(NormalizedKey, bHasNewValue ? NewValue : FMiningConfigValue(), PropagationMode);
        }
        
        return bRemovedAny;
    }
    
    return false;
}

bool UConfigManager::HasKey(const FString& Key) const
{
    if (!bInitialized)
    {
        return false;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    // Normalize key
    FString NormalizedKey = NormalizeKey(Key);
    
    // Check cache first
    if (ValueCache.Contains(NormalizedKey))
    {
        return true;
    }
    
    // Check all providers
    for (const TSharedPtr<IConfigProvider>& Provider : Providers)
    {
        if (Provider.IsValid() && Provider->HasKey(NormalizedKey))
        {
            return true;
        }
    }
    
    return false;
}

EConfigSourcePriority UConfigManager::GetValuePriority(const FString& Key) const
{
    FMiningConfigValue Value;
    if (GetValue(Key, Value))
    {
        return Value.SourcePriority;
    }
    return EConfigSourcePriority::Default;
}

bool UConfigManager::GetMetadata(const FString& Key, FConfigMetadata& OutMetadata) const
{
    FScopeLock Lock(&CriticalSection);
    
    // Normalize key
    FString NormalizedKey = NormalizeKey(Key);
    
    if (ConfigMetadata.Contains(NormalizedKey))
    {
        OutMetadata = ConfigMetadata[NormalizedKey];
        return true;
    }
    
    // If no metadata is defined, check if the validator has a rule for this key
    if (Validator.IsValid())
    {
        const FConfigMetadata* ValidatorMetadata = Validator->GetValidationRule(NormalizedKey);
        if (ValidatorMetadata)
        {
            OutMetadata = *ValidatorMetadata;
            return true;
        }
    }
    
    return false;
}

bool UConfigManager::SetMetadata(const FString& Key, const FConfigMetadata& Metadata)
{
    FScopeLock Lock(&CriticalSection);
    
    // Normalize key
    FString NormalizedKey = NormalizeKey(Key);
    
    // Store metadata
    ConfigMetadata.Add(NormalizedKey, Metadata);
    
    return true;
}

TArray<FString> UConfigManager::GetKeysInSection(const FString& Section, bool bRecursive) const
{
    if (!bInitialized)
    {
        return TArray<FString>();
    }
    
    FScopeLock Lock(&CriticalSection);
    
    // Normalize section key
    FString NormalizedSection = NormalizeKey(Section);
    if (!NormalizedSection.IsEmpty() && !NormalizedSection.EndsWith(TEXT(".")))
    {
        NormalizedSection += TEXT(".");
    }
    
    // Collect keys from all providers
    TSet<FString> UniqueKeys;
    
    // First check cache
    for (const auto& KeyValuePair : ValueCache)
    {
        const FString& Key = KeyValuePair.Key;
        
        if (Key.StartsWith(NormalizedSection))
        {
            // For non-recursive, check that this is a direct child
            if (!bRecursive && !IsDirectChild(NormalizedSection, Key))
            {
                continue;
            }
            
            UniqueKeys.Add(Key);
        }
    }
    
    // Then check providers
    for (const TSharedPtr<IConfigProvider>& Provider : Providers)
    {
        if (Provider.IsValid())
        {
            TArray<FString> ProviderKeys = Provider->GetKeysInSection(NormalizedSection, bRecursive);
            for (const FString& Key : ProviderKeys)
            {
                UniqueKeys.Add(Key);
            }
        }
    }
    
    return UniqueKeys.Array();
}

TArray<FString> UConfigManager::GetSubsections(const FString& Section, bool bRecursive) const
{
    if (!bInitialized)
    {
        return TArray<FString>();
    }
    
    FScopeLock Lock(&CriticalSection);
    
    // Normalize section key
    FString NormalizedSection = NormalizeKey(Section);
    if (!NormalizedSection.IsEmpty() && !NormalizedSection.EndsWith(TEXT(".")))
    {
        NormalizedSection += TEXT(".");
    }
    
    // Collect subsections from all providers
    TSet<FString> UniqueSubsections;
    
    // First check cache
    for (const auto& KeyValuePair : ValueCache)
    {
        const FString& Key = KeyValuePair.Key;
        
        if (Key.StartsWith(NormalizedSection))
        {
            // Extract subsection
            FString SubsectionPath = Key.Mid(NormalizedSection.Len());
            int32 DotIndex;
            if (SubsectionPath.FindChar('.', DotIndex))
            {
                // Extract direct subsection or full path based on recursive flag
                FString Subsection;
                if (bRecursive)
                {
                    Subsection = SubsectionPath.Left(DotIndex);
                }
                else
                {
                    Subsection = NormalizedSection + SubsectionPath.Left(DotIndex);
                }
                
                if (!Subsection.IsEmpty())
                {
                    UniqueSubsections.Add(Subsection);
                }
            }
        }
    }
    
    // Then check providers
    for (const TSharedPtr<IConfigProvider>& Provider : Providers)
    {
        if (Provider.IsValid())
        {
            TArray<FString> ProviderSubsections = Provider->GetSubsections(NormalizedSection, bRecursive);
            for (const FString& Subsection : ProviderSubsections)
            {
                UniqueSubsections.Add(Subsection);
            }
        }
    }
    
    return UniqueSubsections.Array();
}

FDelegateHandle UConfigManager::RegisterChangeCallback(const FString& Key, const FConfigValueChangedDelegate& Callback)
{
    FScopeLock Lock(&CriticalSection);
    
    // Normalize key
    FString NormalizedKey = NormalizeKey(Key);
    
    // Create callback map for this key if it doesn't exist
    if (!ChangeCallbacks.Contains(NormalizedKey))
    {
        ChangeCallbacks.Add(NormalizedKey, TMap<FDelegateHandle, FConfigValueChangedDelegate>());
    }
    
    // Generate a unique handle
    FDelegateHandle Handle = FDelegateHandle(FDelegateHandle::EGenerateNewHandle);
    
    // Add callback to map
    ChangeCallbacks[NormalizedKey].Add(Handle, Callback);
    
    return Handle;
}

bool UConfigManager::UnregisterChangeCallback(const FString& Key, FDelegateHandle Handle)
{
    FScopeLock Lock(&CriticalSection);
    
    // Normalize key
    FString NormalizedKey = NormalizeKey(Key);
    
    // Check if the key exists in the callback map
    if (!ChangeCallbacks.Contains(NormalizedKey))
    {
        return false;
    }
    
    // Check if the handle exists for this key
    if (!ChangeCallbacks[NormalizedKey].Contains(Handle))
    {
        return false;
    }
    
    // Remove the callback
    ChangeCallbacks[NormalizedKey].Remove(Handle);
    
    // Remove the key if there are no more callbacks
    if (ChangeCallbacks[NormalizedKey].Num() == 0)
    {
        ChangeCallbacks.Remove(NormalizedKey);
    }
    
    return true;
}

IConfigManager& UConfigManager::Get()
{
    if (!SingletonInstance)
    {
        SingletonInstance = NewObject<UConfigManager>();
        SingletonInstance->AddToRoot(); // Prevent garbage collection
    }
    
    return *SingletonInstance;
}

bool UConfigManager::RegisterProvider(TSharedPtr<IConfigProvider> Provider)
{
    if (!Provider.IsValid())
    {
        return false;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    // Check if a provider with the same ID is already registered
    for (const TSharedPtr<IConfigProvider>& ExistingProvider : Providers)
    {
        if (ExistingProvider.IsValid() && 
            ExistingProvider->GetProviderInfo().ProviderId == Provider->GetProviderInfo().ProviderId)
        {
            return false;
        }
    }
    
    // Add the provider
    Providers.Add(Provider);
    
    // Sort providers by priority
    Providers.Sort([](const TSharedPtr<IConfigProvider>& A, const TSharedPtr<IConfigProvider>& B) {
        return A->GetProviderInfo().Priority < B->GetProviderInfo().Priority;
    });
    
    // Initialize the provider if manager is already initialized
    if (bInitialized && !Provider->IsInitialized())
    {
        Provider->Initialize();
    }
    
    // Clear cache to force reloading values from providers
    ValueCache.Empty();
    
    return true;
}

bool UConfigManager::UnregisterProvider(const FGuid& ProviderId)
{
    FScopeLock Lock(&CriticalSection);
    
    // Find the provider with the specified ID
    for (int32 i = 0; i < Providers.Num(); ++i)
    {
        if (Providers[i].IsValid() && Providers[i]->GetProviderInfo().ProviderId == ProviderId)
        {
            // Shutdown provider if initialized
            if (Providers[i]->IsInitialized())
            {
                Providers[i]->Shutdown();
            }
            
            // Remove provider
            Providers.RemoveAt(i);
            
            // Clear cache to force reloading values from providers
            ValueCache.Empty();
            
            return true;
        }
    }
    
    return false;
}

TArray<TSharedPtr<IConfigProvider>> UConfigManager::GetAllProviders() const
{
    FScopeLock Lock(&CriticalSection);
    return Providers;
}

void UConfigManager::SetValidator(TSharedPtr<IConfigValidator> InValidator)
{
    FScopeLock Lock(&CriticalSection);
    
    // Shutdown existing validator if initialized
    if (Validator.IsValid() && Validator->IsInitialized())
    {
        Validator->Shutdown();
    }
    
    Validator = InValidator;
    
    // Initialize new validator if manager is already initialized
    if (bInitialized && Validator.IsValid() && !Validator->IsInitialized())
    {
        Validator->Initialize();
    }
}

TSharedPtr<IConfigValidator> UConfigManager::GetValidator() const
{
    FScopeLock Lock(&CriticalSection);
    return Validator;
}

FConfigValidationSummary UConfigManager::ValidateAll(bool bAutoCorrect)
{
    FScopeLock Lock(&CriticalSection);
    
    FConfigValidationSummary Summary;
    
    // Check if validator is available
    if (!Validator.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("ConfigManager: No validator available"));
        return Summary;
    }
    
    // Validate all configuration values
    Summary = Validator->ValidateAll(this, bAutoCorrect);
    
    // Clear cache if auto-correction was enabled to force reloading corrected values
    if (bAutoCorrect)
    {
        ValueCache.Empty();
    }
    
    return Summary;
}

FConfigValidationSummary UConfigManager::ValidateSection(const FString& SectionKey, bool bRecursive, bool bAutoCorrect)
{
    FScopeLock Lock(&CriticalSection);
    
    FConfigValidationSummary Summary;
    
    // Check if validator is available
    if (!Validator.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("ConfigManager: No validator available"));
        return Summary;
    }
    
    // Validate configuration values in section
    Summary = Validator->ValidateSection(this, SectionKey, bRecursive, bAutoCorrect);
    
    // Clear cache if auto-correction was enabled to force reloading corrected values
    if (bAutoCorrect)
    {
        ValueCache.Empty();
    }
    
    return Summary;
}

void UConfigManager::NotifyValueChanged(const FString& Key, const FMiningConfigValue& NewValue, EConfigPropagationMode PropagationMode)
{
    // Get all keys to notify based on propagation mode
    TArray<FString> NotificationKeys = GetNotificationKeys(Key, PropagationMode);
    
    // Notify all relevant callbacks
    for (const FString& NotificationKey : NotificationKeys)
    {
        if (ChangeCallbacks.Contains(NotificationKey))
        {
            TMap<FDelegateHandle, FConfigValueChangedDelegate>& Callbacks = ChangeCallbacks[NotificationKey];
            for (const auto& Callback : Callbacks)
            {
                if (Callback.Value.IsBound())
                {
                    Callback.Value.Execute(Key, NewValue);
                }
            }
        }
    }
}

TArray<FString> UConfigManager::GetNotificationKeys(const FString& Key, EConfigPropagationMode PropagationMode) const
{
    TArray<FString> Result;
    
    // Always include direct key
    Result.Add(Key);
    
    switch (PropagationMode)
    {
    case EConfigPropagationMode::DirectOnly:
        // Already added the direct key
        break;
        
    case EConfigPropagationMode::UpTree:
        {
            // Add parent sections
            FString Section = GetParentSection(Key);
            while (!Section.IsEmpty())
            {
                Result.Add(Section);
                Section = GetParentSection(Section);
            }
        }
        break;
        
    case EConfigPropagationMode::DownTree:
        {
            // Add child keys
            FString KeyWithDot = Key;
            if (!KeyWithDot.EndsWith(TEXT(".")))
            {
                KeyWithDot += TEXT(".");
            }
            
            for (const auto& Callback : ChangeCallbacks)
            {
                if (Callback.Key.StartsWith(KeyWithDot))
                {
                    Result.Add(Callback.Key);
                }
            }
        }
        break;
        
    case EConfigPropagationMode::FullTree:
        {
            // Add both parents and children
            // First add parents
            FString Section = GetParentSection(Key);
            while (!Section.IsEmpty())
            {
                Result.Add(Section);
                Section = GetParentSection(Section);
            }
            
            // Then add children
            FString KeyWithDot = Key;
            if (!KeyWithDot.EndsWith(TEXT(".")))
            {
                KeyWithDot += TEXT(".");
            }
            
            for (const auto& Callback : ChangeCallbacks)
            {
                if (Callback.Key.StartsWith(KeyWithDot))
                {
                    Result.Add(Callback.Key);
                }
            }
        }
        break;
    }
    
    return Result;
}

void UConfigManager::ParseKey(const FString& Key, FString& OutSection, FString& OutName) const
{
    int32 LastDotIndex;
    if (Key.FindLastChar('.', LastDotIndex))
    {
        OutSection = Key.Left(LastDotIndex);
        OutName = Key.Mid(LastDotIndex + 1);
    }
    else
    {
        OutSection = TEXT("");
        OutName = Key;
    }
}

FString UConfigManager::GetParentSection(const FString& SectionKey) const
{
    int32 LastDotIndex;
    if (SectionKey.FindLastChar('.', LastDotIndex))
    {
        return SectionKey.Left(LastDotIndex);
    }
    return TEXT("");
}

bool UConfigManager::IsDirectChild(const FString& SectionKey, const FString& Key) const
{
    // Ensure section key ends with a dot
    FString SectionWithDot = SectionKey;
    if (!SectionWithDot.IsEmpty() && !SectionWithDot.EndsWith(TEXT(".")))
    {
        SectionWithDot += TEXT(".");
    }
    
    // Key must start with the section
    if (!Key.StartsWith(SectionWithDot))
    {
        return false;
    }
    
    // Remove section prefix
    FString Remainder = Key.Mid(SectionWithDot.Len());
    
    // Direct child should not have any dots in the remainder
    return !Remainder.Contains(TEXT("."));
}

FString UConfigManager::NormalizeKey(const FString& Key) const
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

void UConfigManager::FlattenJsonObject(const TSharedPtr<FJsonObject>& JsonObject, const FString& KeyPrefix, TArray<TPair<FString, TSharedPtr<FJsonValue>>>& OutKeyValuePairs)
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

bool UConfigManager::JsonValueToConfigValue(const TSharedPtr<FJsonValue>& JsonValue, FMiningConfigValue& OutValue)
{
    if (!JsonValue.IsValid())
    {
        return false;
    }
    
    switch (JsonValue->Type)
    {
    case EJson::Boolean:
        OutValue = FMiningConfigValue(JsonValue->AsBool());
        return true;
        
    case EJson::Number:
        {
            double NumValue = JsonValue->AsNumber();
            int64 IntValue = static_cast<int64>(NumValue);
            
            // Check if the number is an integer (by comparing with its integer cast)
            if (FMath::IsNearlyEqual(NumValue, static_cast<double>(IntValue)))
            {
                OutValue = FMiningConfigValue(IntValue);
            }
            else
            {
                OutValue = FMiningConfigValue(NumValue);
            }
            return true;
        }
        
    case EJson::String:
        OutValue = FMiningConfigValue(JsonValue->AsString());
        return true;
        
    case EJson::Object:
        OutValue.Type = EConfigValueType::JsonObject;
        OutValue.JsonValue = JsonValue->AsObject();
        return true;
        
    case EJson::Null:
        // Handle null as empty string
        OutValue = FMiningConfigValue(TEXT(""));
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
        
    default:
        return false;
    }
}

void UConfigManager::AddValueToJsonObject(TSharedPtr<FJsonObject>& JsonObject, const FString& Key, const FMiningConfigValue& Value)
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

void UConfigManager::ConfigValueToMiningConfigValue(const FConfigValue& From, FMiningConfigValue& To)
{
    // Implementation required
}

void UConfigManager::MiningConfigValueToConfigValue(const FMiningConfigValue& From, FConfigValue& To)
{
    // Implementation required
}