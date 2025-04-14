#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "5_IConfigManager.generated.h"

// Forward declarations
class IConfigProvider;
class IConfigValidator;
class IConfigEventListener;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class U5_ConfigManager : public UInterface
{
    GENERATED_BODY()
};

class MININGSPICECOPILOT_API I5_ConfigManager
{
    GENERATED_BODY()
    
public:
    // Core lifecycle methods
    virtual bool Initialize() = 0;
    virtual bool Shutdown() = 0;
    virtual bool IsInitialized() const = 0;
    
    // Provider management
    virtual bool RegisterProvider(TScriptInterface<IConfigProvider> Provider) = 0;
    virtual bool UnregisterProvider(TScriptInterface<IConfigProvider> Provider) = 0;
    virtual TArray<TScriptInterface<IConfigProvider>> GetProviders() const = 0;
    virtual TScriptInterface<IConfigProvider> GetProviderById(const FName& ProviderId) const = 0;
    
    // Value management
    virtual FVariant GetValue(const FString& Key, const FVariant& DefaultValue = FVariant()) const = 0;
    virtual FVariant GetValueFromProvider(const FString& Key, const FName& ProviderId, const FVariant& DefaultValue = FVariant()) const = 0;
    virtual bool SetValue(const FString& Key, const FVariant& Value) = 0;
    virtual bool SetValueInProvider(const FString& Key, const FVariant& Value, const FName& ProviderId) = 0;
    virtual bool RemoveValue(const FString& Key) = 0;
    
    // Key management
    virtual bool HasKey(const FString& Key) const = 0;
    virtual bool HasKeyInProvider(const FString& Key, const FName& ProviderId) const = 0;
    virtual TArray<FString> GetAllKeys() const = 0;
    virtual TArray<FString> GetKeysInSection(const FString& Section) const = 0;
    virtual TArray<FString> GetSubsections(const FString& ParentSection = TEXT("")) const = 0;
    
    // File operations
    virtual bool SaveAll() = 0;
    virtual bool LoadAll() = 0;
    virtual bool ResetAll() = 0;
    
    // Validators
    virtual void SetValidator(TScriptInterface<IConfigValidator> Validator) = 0;
    virtual TScriptInterface<IConfigValidator> GetValidator() const = 0;
    virtual bool ValidateAll() = 0;
    virtual bool ValidateSection(const FString& Section) const = 0;
    
    // Event management
    virtual void AddEventListener(TScriptInterface<IConfigEventListener> Listener) = 0;
    virtual void RemoveEventListener(TScriptInterface<IConfigEventListener> Listener) = 0;
    virtual void NotifyEventListeners(const FString& Key, const FVariant& OldValue, const FVariant& NewValue) = 0;
};


