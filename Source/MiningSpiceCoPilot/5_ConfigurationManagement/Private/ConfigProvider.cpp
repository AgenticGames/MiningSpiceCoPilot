// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConfigProvider.h"

UConfigProvider::UConfigProvider()
    : bInitialized(false)
{
    // Set default provider info
    ProviderInfo.ProviderId = FGuid::NewGuid();
    ProviderInfo.Name = TEXT("Base Config Provider");
    ProviderInfo.Description = TEXT("Base configuration provider implementation");
    ProviderInfo.Type = EConfigProviderType::Memory;
    ProviderInfo.Priority = EConfigSourcePriority::Default;
    ProviderInfo.bIsReadOnly = false;
    ProviderInfo.bSupportsHierarchy = true;
}

UConfigProvider::~UConfigProvider()
{
    Shutdown();
}

bool UConfigProvider::Initialize()
{
    FScopeLock Lock(&CriticalSection);
    
    if (bInitialized)
    {
        return true;
    }
    
    // Load initial values
    FConfigOperationResult LoadResult = Load();
    
    bInitialized = true;
    return LoadResult.bSuccess;
}

void UConfigProvider::Shutdown()
{
    FScopeLock Lock(&CriticalSection);
    
    if (!bInitialized)
    {
        return;
    }
    
    // Save values if needed
    if (!ProviderInfo.bIsReadOnly)
    {
        Save();
    }
    
    // Clear values and cache
    ConfigValues.Empty();
    KeyInfoCache.Empty();
    
    bInitialized = false;
}

bool UConfigProvider::IsInitialized() const
{
    return bInitialized;
}

FConfigProviderInfo UConfigProvider::GetProviderInfo() const
{
    return ProviderInfo;
}

bool UConfigProvider::GetValue(const FString& Key, FConfigValue& OutValue) const
{
    FScopeLock Lock(&CriticalSection);
    
    // Normalize key
    FString NormalizedKey = NormalizeKey(Key);
    
    if (ConfigValues.Contains(NormalizedKey))
    {
        OutValue = ConfigValues[NormalizedKey];
        return true;
    }
    
    return false;
}

FConfigOperationResult UConfigProvider::SetValue(const FString& Key, const FConfigValue& Value)
{
    FScopeLock Lock(&CriticalSection);
    
    // Check if provider is read-only
    if (ProviderInfo.bIsReadOnly)
    {
        return FConfigOperationResult(TEXT("Provider is read-only"));
    }
    
    // Normalize key
    FString NormalizedKey = NormalizeKey(Key);
    
    // Update or add the value
    ConfigValues.Add(NormalizedKey, Value);
    
    // Update key info cache
    if (KeyInfoCache.Contains(NormalizedKey))
    {
        KeyInfoCache.Remove(NormalizedKey);
    }
    
    FConfigOperationResult Result;
    Result.bSuccess = true;
    Result.AffectedKeyCount = 1;
    return Result;
}

FConfigOperationResult UConfigProvider::RemoveValue(const FString& Key)
{
    FScopeLock Lock(&CriticalSection);
    
    // Check if provider is read-only
    if (ProviderInfo.bIsReadOnly)
    {
        return FConfigOperationResult(TEXT("Provider is read-only"));
    }
    
    // Normalize key
    FString NormalizedKey = NormalizeKey(Key);
    
    // Remove the value if it exists
    bool bRemoved = ConfigValues.Remove(NormalizedKey) > 0;
    
    // Update key info cache
    if (KeyInfoCache.Contains(NormalizedKey))
    {
        KeyInfoCache.Remove(NormalizedKey);
    }
    
    FConfigOperationResult Result;
    Result.bSuccess = bRemoved;
    Result.AffectedKeyCount = bRemoved ? 1 : 0;
    if (!bRemoved)
    {
        Result.ErrorMessage = TEXT("Key not found");
    }
    
    return Result;
}

bool UConfigProvider::HasKey(const FString& Key) const
{
    FScopeLock Lock(&CriticalSection);
    
    // Normalize key
    FString NormalizedKey = NormalizeKey(Key);
    
    return ConfigValues.Contains(NormalizedKey);
}

TArray<FString> UConfigProvider::GetAllKeys() const
{
    FScopeLock Lock(&CriticalSection);
    
    TArray<FString> Result;
    ConfigValues.GetKeys(Result);
    return Result;
}

TArray<FString> UConfigProvider::GetKeysInSection(const FString& Section, bool bRecursive) const
{
    FScopeLock Lock(&CriticalSection);
    
    TArray<FString> Result;
    
    // Normalize section key
    FString NormalizedSection = NormalizeKey(Section);
    if (!NormalizedSection.IsEmpty() && !NormalizedSection.EndsWith(TEXT(".")))
    {
        NormalizedSection += TEXT(".");
    }
    
    // Find all keys in the section
    for (const auto& Pair : ConfigValues)
    {
        const FString& Key = Pair.Key;
        
        if (Key.StartsWith(NormalizedSection))
        {
            // For non-recursive, check that this is a direct child
            if (!bRecursive && !IsDirectChild(NormalizedSection, Key))
            {
                continue;
            }
            
            Result.Add(Key);
        }
    }
    
    return Result;
}

TArray<FString> UConfigProvider::GetSubsections(const FString& Section, bool bRecursive) const
{
    FScopeLock Lock(&CriticalSection);
    
    TSet<FString> UniqueSubsections;
    
    // Normalize section key
    FString NormalizedSection = NormalizeKey(Section);
    if (!NormalizedSection.IsEmpty() && !NormalizedSection.EndsWith(TEXT(".")))
    {
        NormalizedSection += TEXT(".");
    }
    
    // Find all subsections in the section
    for (const auto& Pair : ConfigValues)
    {
        const FString& Key = Pair.Key;
        
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
    
    return UniqueSubsections.Array();
}

FConfigOperationResult UConfigProvider::Load()
{
    // Base implementation doesn't load from anywhere, just succeeds
    FConfigOperationResult Result;
    Result.bSuccess = true;
    return Result;
}

FConfigOperationResult UConfigProvider::Save()
{
    // Base implementation doesn't save anywhere, just succeeds
    FConfigOperationResult Result;
    Result.bSuccess = true;
    return Result;
}

FConfigOperationResult UConfigProvider::Reset()
{
    FScopeLock Lock(&CriticalSection);
    
    // Check if provider is read-only
    if (ProviderInfo.bIsReadOnly)
    {
        return FConfigOperationResult(TEXT("Provider is read-only"));
    }
    
    // Clear all values
    int32 KeyCount = ConfigValues.Num();
    ConfigValues.Empty();
    KeyInfoCache.Empty();
    
    FConfigOperationResult Result;
    Result.bSuccess = true;
    Result.AffectedKeyCount = KeyCount;
    return Result;
}

TSharedPtr<FConfigKeyInfo> UConfigProvider::GetKeyInfo(const FString& Key) const
{
    FScopeLock Lock(&CriticalSection);
    
    // Normalize key
    FString NormalizedKey = NormalizeKey(Key);
    
    // Check cache first
    if (KeyInfoCache.Contains(NormalizedKey))
    {
        return KeyInfoCache[NormalizedKey];
    }
    
    // Create key info if the key exists
    if (ConfigValues.Contains(NormalizedKey))
    {
        TSharedPtr<FConfigKeyInfo> KeyInfo = MakeShareable(new FConfigKeyInfo());
        
        KeyInfo->Key = NormalizedKey;
        KeyInfo->Type = EConfigValueType::String; // Default, should be set properly based on value
        KeyInfo->bIsReadOnly = ProviderInfo.bIsReadOnly;
        KeyInfo->LastModified = FDateTime::Now();
        
        // Cache key info
        KeyInfoCache.Add(NormalizedKey, KeyInfo);
        
        return KeyInfo;
    }
    
    return nullptr;
}

TArray<TSharedPtr<FConfigKeyInfo>> UConfigProvider::GetAllKeyInfo() const
{
    FScopeLock Lock(&CriticalSection);
    
    TArray<TSharedPtr<FConfigKeyInfo>> Result;
    
    // Get info for all keys
    for (const auto& Pair : ConfigValues)
    {
        TSharedPtr<FConfigKeyInfo> KeyInfo = GetKeyInfo(Pair.Key);
        if (KeyInfo.IsValid())
        {
            Result.Add(KeyInfo);
        }
    }
    
    return Result;
}

void UConfigProvider::SetProviderInfo(const FConfigProviderInfo& InProviderInfo)
{
    FScopeLock Lock(&CriticalSection);
    ProviderInfo = InProviderInfo;
}

FString UConfigProvider::NormalizeKey(const FString& Key) const
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

void UConfigProvider::ParseKey(const FString& Key, FString& OutSection, FString& OutName) const
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

bool UConfigProvider::IsDirectChild(const FString& SectionKey, const FString& Key) const
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

FString UConfigProvider::GetParentSection(const FString& SectionKey) const
{
    int32 LastDotIndex;
    if (SectionKey.FindLastChar('.', LastDotIndex))
    {
        return SectionKey.Left(LastDotIndex);
    }
    return TEXT("");
}