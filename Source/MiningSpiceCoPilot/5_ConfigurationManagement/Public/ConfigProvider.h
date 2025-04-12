// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IConfigProvider.h"
#include "HAL/CriticalSection.h"
#include "ConfigProvider.generated.h"

/**
 * Base ConfigProvider implementation
 * Provides the foundation for various configuration providers in the SVO+SDF mining system.
 * Handles configuration storage, access, and basic operations.
 */
UCLASS()
class MININGSPICECOPILOT_API UConfigProvider : public UObject, public IConfigProvider
{
    GENERATED_BODY()

public:
    /** Constructor */
    UConfigProvider();
    
    /** Destructor */
    virtual ~UConfigProvider();
    
    //~ Begin IConfigProvider Interface
    virtual bool Initialize() override;
    virtual void Shutdown() override;
    virtual bool IsInitialized() const override;
    
    virtual FConfigProviderInfo GetProviderInfo() const override;
    
    virtual bool GetValue(const FString& Key, FConfigValue& OutValue) const override;
    virtual FConfigOperationResult SetValue(const FString& Key, const FConfigValue& Value) override;
    virtual FConfigOperationResult RemoveValue(const FString& Key) override;
    
    virtual bool HasKey(const FString& Key) const override;
    virtual TArray<FString> GetAllKeys() const override;
    virtual TArray<FString> GetKeysInSection(const FString& Section, bool bRecursive = false) const override;
    virtual TArray<FString> GetSubsections(const FString& Section, bool bRecursive = false) const override;
    
    virtual FConfigOperationResult Load() override;
    virtual FConfigOperationResult Save() override;
    virtual FConfigOperationResult Reset() override;
    
    virtual TSharedPtr<FConfigKeyInfo> GetKeyInfo(const FString& Key) const override;
    virtual TArray<TSharedPtr<FConfigKeyInfo>> GetAllKeyInfo() const override;
    //~ End IConfigProvider Interface

    /** Sets the provider info */
    void SetProviderInfo(const FConfigProviderInfo& InProviderInfo);

protected:
    /** Normalize key format */
    virtual FString NormalizeKey(const FString& Key) const;
    
    /** Parse key into section and name */
    virtual void ParseKey(const FString& Key, FString& OutSection, FString& OutName) const;
    
    /** Check if a key is a direct child of a section */
    virtual bool IsDirectChild(const FString& SectionKey, const FString& Key) const;
    
    /** Get parent section key */
    virtual FString GetParentSection(const FString& SectionKey) const;

protected:
    /** Flag indicating if the provider has been initialized */
    bool bInitialized;
    
    /** Provider information */
    FConfigProviderInfo ProviderInfo;
    
    /** Configuration values */
    TMap<FString, FConfigValue> ConfigValues;
    
    /** Key information cache */
    mutable TMap<FString, TSharedPtr<FConfigKeyInfo>> KeyInfoCache;
    
    /** Critical section for thread safety */
    mutable FCriticalSection CriticalSection;
};