// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IConfigManager.h"
#include "HAL/CriticalSection.h"
#include "ConfigManager.generated.h"

class IConfigProvider;
class IConfigValidator;
class UConfigValidator;

/**
 * Central configuration manager for the SVO+SDF mining system
 * Manages multiple configuration providers with priority-based resolution,
 * provides access to configuration values, and handles validation and events.
 */
UCLASS()
class MININGSPICECOPILOT_API UConfigManager : public UObject, public IConfigManager
{
    GENERATED_BODY()

public:
    /** Constructor */
    UConfigManager();
    
    /** Destructor */
    virtual ~UConfigManager();
    
    //~ Begin IConfigManager Interface
    virtual bool Initialize() override;
    virtual void Shutdown() override;
    virtual bool IsInitialized() const override;
    
    virtual bool RegisterProvider(IConfigProvider* Provider, EConfigSourcePriority Priority = EConfigSourcePriority::Default) override;
    virtual bool UnregisterProvider(IConfigProvider* Provider) override;
    virtual TArray<IConfigProvider*> GetProviders() const override;
    virtual IConfigProvider* GetProviderById(const FGuid& ProviderId) const override;
    
    virtual bool GetValue(const FString& Key, FMiningConfigValue& OutValue) const override;
    virtual bool GetValueFromProvider(const FString& Key, IConfigProvider* Provider, FMiningConfigValue& OutValue) const override;
    virtual FConfigOperationResult SetValue(const FString& Key, const FMiningConfigValue& Value, EConfigSourcePriority MinPriority = EConfigSourcePriority::Default) override;
    virtual FConfigOperationResult SetValueInProvider(const FString& Key, const FMiningConfigValue& Value, IConfigProvider* Provider) override;
    virtual FConfigOperationResult RemoveValue(const FString& Key, EConfigSourcePriority MinPriority = EConfigSourcePriority::Default) override;
    
    virtual bool HasKey(const FString& Key) const override;
    virtual bool HasKeyInProvider(const FString& Key, IConfigProvider* Provider) const override;
    virtual TArray<FString> GetAllKeys() const override;
    virtual TArray<FString> GetKeysInSection(const FString& Section, bool bRecursive = false) const override;
    virtual TArray<FString> GetSubsections(const FString& Section, bool bRecursive = false) const override;
    
    virtual FConfigOperationResult SaveAll() override;
    virtual FConfigOperationResult LoadAll() override;
    virtual FConfigOperationResult ResetAll() override;
    
    virtual void SetValidator(IConfigValidator* InValidator) override;
    virtual IConfigValidator* GetValidator() const override;
    virtual FConfigValidationSummary ValidateAll(bool bAutoCorrect = false) override;
    virtual FConfigValidationSummary ValidateSection(const FString& SectionKey, bool bRecursive = true, bool bAutoCorrect = false) override;
    
    virtual void AddEventListener(IConfigEventListener* Listener) override;
    virtual void RemoveEventListener(IConfigEventListener* Listener) override;
    virtual void NotifyEventListeners(const FConfigEvent& Event) override;
    
    /** Gets singleton instance */
    static UConfigManager& Get();
    
private:
    /** Find the highest-priority provider that contains the key */
    IConfigProvider* FindProviderWithKey(const FString& Key) const;
    
    /** Find a provider that can write the key based on priority */
    IConfigProvider* FindWritableProvider(EConfigSourcePriority MinPriority) const;
    
    /** Normalize key format */
    FString NormalizeKey(const FString& Key) const;
    
    /** Notify listeners of a value change */
    void NotifyValueChanged(const FString& Key, const FMiningConfigValue& NewValue, 
                           const FMiningConfigValue* OldValue = nullptr, 
                           IConfigProvider* Provider = nullptr);

private:
    /** Flag indicating if the manager has been initialized */
    bool bInitialized;
    
    /** Configuration providers mapped by priority */
    TMap<EConfigSourcePriority, TArray<IConfigProvider*>> ProvidersByPriority;
    
    /** All registered providers */
    TArray<IConfigProvider*> AllProviders;
    
    /** Configuration validator */
    IConfigValidator* Validator;
    
    /** Event listeners */
    TArray<IConfigEventListener*> EventListeners;
    
    /** Critical section for thread safety */
    mutable FCriticalSection CriticalSection;
    
    /** Singleton instance */
    static UConfigManager* SingletonInstance;
};