// Copyright Epic Games, Inc. All Rights Reserved.

#include "../Public/ZoneTypeRegistry.h"
#include "Interfaces/IServiceLocator.h"
#include "CoreServiceLocator.h"
#include "ThreadSafety.h"
#include "TransactionManager.h"
#include "Interfaces/ITransactionManager.h"
#include "MiningSpiceCoPilot/3_ThreadingTaskSystem/Public/TaskHelpers.h"
#include "TypeRegistrationOperation.h"
#include "../../3_ThreadingTaskSystem/Public/AsyncTaskManager.h"
#include "Logging/LogMining.h"

// Initialize static members
FZoneTypeRegistry* FZoneTypeRegistry::Singleton = nullptr;
FThreadSafeBool FZoneTypeRegistry::bSingletonInitialized(false);

FZoneTypeRegistry::FZoneTypeRegistry()
{
    // Initialize members properly
    NextTypeId.Set(1);
    bIsInitialized.AtomicSet(false);
    // Constructor is intentionally minimal
}

FZoneTypeRegistry::~FZoneTypeRegistry()
{
    // Ensure we're properly shut down
    if (IsInitialized())
    {
        Shutdown();
    }
}

bool FZoneTypeRegistry::Initialize()
{
    // Check if already initialized
    if (bIsInitialized)
    {
        return false;
    }
    
    // Set initialized flag
    bIsInitialized.AtomicSet(true);
    
    // Initialize internal maps
    TransactionTypeMap.Empty();
    TransactionTypeNameMap.Empty();
    ZoneConfigMap.Empty();
    ZoneTypeMap.Empty();
    ZoneTypeNameMap.Empty();
    ZoneHierarchy.Empty();
    ChildToParentMap.Empty();
    
    // Reset type ID counter
    NextTypeId.Set(1);
    
    return true;
}

void FZoneTypeRegistry::Shutdown()
{
    if (bIsInitialized)
    {
        // Lock for thread safety
        FScopedSpinLock Lock(RegistryLock);
        
        // Clear all registered items
        TransactionTypeMap.Empty();
        TransactionTypeNameMap.Empty();
        ZoneConfigMap.Empty();
        
        // Reset state
        bIsInitialized.AtomicSet(false);
    }
}

bool FZoneTypeRegistry::IsInitialized() const
{
    return bIsInitialized;
}

FName FZoneTypeRegistry::GetRegistryName() const
{
    return FName(TEXT("ZoneTypeRegistry"));
}

uint32 FZoneTypeRegistry::GetSchemaVersion() const
{
    return 1; // Return schema version directly
}

bool FZoneTypeRegistry::Validate(TArray<FString>& OutErrors) const
{
    if (!IsInitialized())
    {
        OutErrors.Add(TEXT("Zone Type Registry is not initialized"));
        return false;
    }
    
    // Lock for thread safety - Replace FRWScopeLock with FScopedSpinLock
    FScopedSpinLock Lock(RegistryLock);
    
    // Record contention if there's high lock traffic
    FThreadSafety::Get().RecordContention(const_cast<FSpinLock*>(&RegistryLock));
    
    bool bIsValid = true;
    
    // Validate transaction type map integrity
    for (const auto& TypeNamePair : TransactionTypeNameMap)
    {
        const FName& TypeName = TypeNamePair.Key;
        const uint32 TypeId = TypeNamePair.Value;
        
        if (!TransactionTypeMap.Contains(TypeId))
        {
            OutErrors.Add(FString::Printf(TEXT("Zone transaction type name '%s' references non-existent type ID %u"), 
                *TypeName.ToString(), TypeId));
            bIsValid = false;
        }
        else if (TransactionTypeMap[TypeId]->TypeName != TypeName)
        {
            OutErrors.Add(FString::Printf(TEXT("Zone transaction type name mismatch: '%s' references ID %u, but ID maps to name '%s'"),
                *TypeName.ToString(), TypeId, *TransactionTypeMap[TypeId]->TypeName.ToString()));
            bIsValid = false;
        }
    }
    
    // Validate transaction type properties
    for (const auto& TypeInfoPair : TransactionTypeMap)
    {
        const uint32 TypeId = TypeInfoPair.Key;
        const TSharedRef<FZoneTransactionTypeInfo>& TypeInfo = TypeInfoPair.Value;
        
        // Validate material channel transactions
        if (TypeInfo->ConcurrencyLevel == ETransactionConcurrency::MaterialChannel && TypeInfo->MaterialChannelId < 0)
        {
            OutErrors.Add(FString::Printf(TEXT("Material channel transaction '%s' (ID %u) has invalid channel ID %d"),
                *TypeInfo->TypeName.ToString(), TypeId, TypeInfo->MaterialChannelId));
            bIsValid = false;
        }
        
        // Validate retry strategy
        if (TypeInfo->RetryStrategy != ERetryStrategy::None && TypeInfo->MaxRetries == 0)
        {
            OutErrors.Add(FString::Printf(TEXT("Zone transaction '%s' (ID %u) has retry strategy but MaxRetries is 0"),
                *TypeInfo->TypeName.ToString(), TypeId));
            bIsValid = false;
        }
        
        // Validate fast path threshold
        if (TypeInfo->bSupportsFastPath && 
            (TypeInfo->FastPathThreshold < 0.0f || TypeInfo->FastPathThreshold > 1.0f))
        {
            OutErrors.Add(FString::Printf(TEXT("Zone transaction '%s' (ID %u) has invalid fast path threshold %.3f (must be between 0 and 1)"),
                *TypeInfo->TypeName.ToString(), TypeId, TypeInfo->FastPathThreshold));
            bIsValid = false;
        }
    }
    
    // Validate zone grid configurations
    for (const auto& ConfigPair : ZoneConfigMap)
    {
        const FName& ConfigName = ConfigPair.Key;
        const TSharedRef<FZoneGridConfig>& Config = ConfigPair.Value;
        
        // Validate zone size
        if (Config->ZoneSize <= 0.0f)
        {
            OutErrors.Add(FString::Printf(TEXT("Zone grid configuration '%s' has invalid zone size %.3f"),
                *ConfigName.ToString(), Config->ZoneSize));
            bIsValid = false;
        }
        
        // Validate max concurrent transactions
        if (Config->MaxConcurrentTransactions == 0)
        {
            OutErrors.Add(FString::Printf(TEXT("Zone grid configuration '%s' has invalid max concurrent transactions 0"),
                *ConfigName.ToString()));
            bIsValid = false;
        }
    }
    
    // Check default zone grid configuration
    if (!DefaultZoneConfigName.IsNone() && !ZoneConfigMap.Contains(DefaultZoneConfigName))
    {
        OutErrors.Add(FString::Printf(TEXT("Default zone grid configuration '%s' does not exist"),
            *DefaultZoneConfigName.ToString()));
        bIsValid = false;
    }
    
    return bIsValid;
}

void FZoneTypeRegistry::Clear()
{
    if (IsInitialized())
    {
        // Lock for thread safety - Replace FRWScopeLock with FScopedSpinLock
        FScopedSpinLock Lock(RegistryLock);
        
        // Record contention if there's high lock traffic
        FThreadSafety::Get().RecordContention(const_cast<FSpinLock*>(&RegistryLock));
        
        // Clear all registered items
        TransactionTypeMap.Empty();
        TransactionTypeNameMap.Empty();
        ZoneConfigMap.Empty();
        
        // Reset counter
        NextTypeId.Set(1);
        
        // Reset default config name
        DefaultZoneConfigName = NAME_None;
    }
}

bool FZoneTypeRegistry::SetTypeVersion(uint32 TypeId, uint32 NewVersion, bool bMigrateInstanceData)
{
    // Check if registry is initialized
    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot set type version - registry not initialized"));
        return false;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Record contention if there's high lock traffic
    FThreadSafety::Get().RecordContention(const_cast<FSpinLock*>(&RegistryLock));
    
    // Check if type exists
    if (!TransactionTypeMap.Contains(TypeId))
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot set type version - type ID %u not found"), TypeId);
        return false;
    }
    
    // Get mutable type info
    TSharedRef<FZoneTransactionTypeInfo>& TypeInfo = TransactionTypeMap[TypeId];
    
    // If version is the same, nothing to do
    if (TypeInfo->SchemaVersion == NewVersion)
    {
        UE_LOG(LogTemp, Warning, TEXT("Type '%s' is already at version %u"),
            *TypeInfo->TypeName.ToString(), NewVersion);
        return true;
    }
    
    // Record previous version for migration
    uint32 PreviousVersion = TypeInfo->SchemaVersion;
    
    // Update version
    TypeInfo->SchemaVersion = NewVersion;
    
    // Log version change
    UE_LOG(LogTemp, Log, TEXT("Updated type '%s' (ID %u) from version %u to version %u"),
        *TypeInfo->TypeName.ToString(), TypeId, PreviousVersion, NewVersion);
    
    // Handle instance data migration if needed
    if (bMigrateInstanceData)
    {
        // TODO: Implement migration logic
        UE_LOG(LogTemp, Warning, TEXT("Instance data migration not yet implemented"));
    }
    
    return true;
}

uint32 FZoneTypeRegistry::GetTypeVersion(uint32 TypeId) const
{
    // Check if registry is initialized
    if (!bIsInitialized)
    {
        return 0;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Record contention if there's high lock traffic
    FThreadSafety::Get().RecordContention(const_cast<FSpinLock*>(&RegistryLock));
    
    // Check if type exists
    if (!TransactionTypeMap.Contains(TypeId))
    {
        return 0;
    }
    
    // Return version
    return TransactionTypeMap[TypeId]->SchemaVersion;
}

uint32 FZoneTypeRegistry::RegisterTransactionType(
    const FName& InTypeName,
    ETransactionConcurrency InConcurrencyLevel,
    ERetryStrategy InRetryStrategy)
{
    // Check if already registered
    if (IsTransactionTypeRegistered(InTypeName))
    {
        // Return existing ID
        FScopedSpinLock ReadLock(RegistryLock);
        return TransactionTypeNameMap[InTypeName];
    }
    
    // Create new transaction type info
    TSharedRef<FZoneTransactionTypeInfo> TypeInfo = MakeShared<FZoneTransactionTypeInfo>();
    
    // Fill in basic info
    TypeInfo->TypeName = InTypeName;
    TypeInfo->ConcurrencyLevel = InConcurrencyLevel;
    TypeInfo->RetryStrategy = InRetryStrategy;
    
    // Set default values based on concurrency level
    switch (InConcurrencyLevel)
    {
        case ETransactionConcurrency::ReadOnly:
            TypeInfo->bRequiresVersionTracking = false;
            TypeInfo->MaxRetries = 2;
            TypeInfo->BaseRetryIntervalMs = 5;
            TypeInfo->bSupportsFastPath = true;
            TypeInfo->FastPathThreshold = 0.9f;
            break;
            
        case ETransactionConcurrency::Optimistic:
            TypeInfo->bRequiresVersionTracking = true;
            TypeInfo->MaxRetries = 5;
            TypeInfo->BaseRetryIntervalMs = 10;
            TypeInfo->bSupportsFastPath = true;
            TypeInfo->FastPathThreshold = 0.7f;
            break;
            
        case ETransactionConcurrency::Exclusive:
            TypeInfo->bRequiresVersionTracking = true;
            TypeInfo->MaxRetries = 0;
            TypeInfo->BaseRetryIntervalMs = 0;
            TypeInfo->bSupportsFastPath = false;
            TypeInfo->FastPathThreshold = 0.0f;
            break;
            
        case ETransactionConcurrency::MaterialChannel:
            TypeInfo->bRequiresVersionTracking = true;
            TypeInfo->MaxRetries = 3;
            TypeInfo->BaseRetryIntervalMs = 20;
            TypeInfo->bSupportsFastPath = true;
            TypeInfo->FastPathThreshold = 0.5f;
            break;
    }
    
    // Get next available ID
    uint32 NewTypeId = GenerateUniqueTypeId();
    TypeInfo->TypeId = NewTypeId;
    
    // Initialize version info
    TypeInfo->SchemaVersion = 1;  // Start at version 1
    
    // Register in registry
    {
        FScopedSpinLock WriteLock(RegistryLock);
        
        // Add to maps
        TransactionTypeMap.Add(NewTypeId, TypeInfo);
        TransactionTypeNameMap.Add(InTypeName, NewTypeId);
    }
    
    UE_LOG(LogTemp, Log, TEXT("Registered zone transaction type '%s' with ID %u"),
        *InTypeName.ToString(), NewTypeId);
    
    return NewTypeId;
}

uint32 FZoneTypeRegistry::RegisterMaterialTransaction(
    const FName& InTypeName,
    int32 InMaterialChannelId)
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FZoneTypeRegistry::RegisterMaterialTransaction failed - registry not initialized"));
        return 0;
    }
    
    if (InTypeName.IsNone())
    {
        UE_LOG(LogTemp, Error, TEXT("FZoneTypeRegistry::RegisterMaterialTransaction failed - invalid type name"));
        return 0;
    }
    
    if (InMaterialChannelId < 0)
    {
        UE_LOG(LogTemp, Error, TEXT("FZoneTypeRegistry::RegisterMaterialTransaction failed - invalid material channel ID"));
        return 0;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Check if type name is already registered
    if (TransactionTypeNameMap.Contains(InTypeName))
    {
        UE_LOG(LogTemp, Warning, TEXT("FZoneTypeRegistry::RegisterMaterialTransaction - type '%s' is already registered"),
            *InTypeName.ToString());
        return 0;
    }
    
    // Generate a unique type ID
    uint32 TypeId = GenerateUniqueTypeId();
    
    // Create and populate type info
    TSharedRef<FZoneTransactionTypeInfo> TypeInfo = MakeShared<FZoneTransactionTypeInfo>();
    TypeInfo->TypeId = TypeId;
    TypeInfo->TypeName = InTypeName;
    TypeInfo->ConcurrencyLevel = ETransactionConcurrency::MaterialChannel;
    TypeInfo->RetryStrategy = ERetryStrategy::FixedInterval;
    
    // Set default values and material-specific values
    TypeInfo->MaxRetries = 3;
    TypeInfo->BaseRetryIntervalMs = 5; // Shorter interval for material transactions
    TypeInfo->MaterialChannelId = InMaterialChannelId;
    TypeInfo->Priority = static_cast<ETransactionPriority>(1); // Default priority
    TypeInfo->bRequiresVersionTracking = true;
    TypeInfo->bSupportsFastPath = true;
    TypeInfo->FastPathThreshold = 0.1f; // Lower threshold for material transactions
    TypeInfo->bHasReadValidateWritePattern = true;
    
    // Register the type
    TransactionTypeMap.Add(TypeId, TypeInfo);
    TransactionTypeNameMap.Add(InTypeName, TypeId);
    
    UE_LOG(LogTemp, Verbose, TEXT("FZoneTypeRegistry::RegisterMaterialTransaction - registered type '%s' with ID %u for channel %d"),
        *InTypeName.ToString(), TypeId, InMaterialChannelId);
    
    return TypeId;
}

bool FZoneTypeRegistry::RegisterZoneGridConfig(
    const FName& InConfigName,
    float InZoneSize,
    uint32 InMaxConcurrentTransactions)
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FZoneTypeRegistry::RegisterZoneGridConfig failed - registry not initialized"));
        return false;
    }
    
    if (InConfigName.IsNone())
    {
        UE_LOG(LogTemp, Error, TEXT("FZoneTypeRegistry::RegisterZoneGridConfig failed - invalid config name"));
        return false;
    }
    
    if (InZoneSize <= 0.0f)
    {
        UE_LOG(LogTemp, Error, TEXT("FZoneTypeRegistry::RegisterZoneGridConfig failed - invalid zone size"));
        return false;
    }
    
    if (InMaxConcurrentTransactions == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("FZoneTypeRegistry::RegisterZoneGridConfig failed - invalid max concurrent transactions"));
        return false;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Check if a configuration with this name already exists
    if (ZoneConfigMap.Contains(InConfigName))
    {
        UE_LOG(LogTemp, Warning, TEXT("Zone grid configuration '%s' already exists, overwriting"),
            *InConfigName.ToString());
    }
    
    // Create a new configuration object
    TSharedRef<FZoneGridConfig> Config = MakeShared<FZoneGridConfig>();
    Config->ZoneSize = InZoneSize;
    Config->MaxConcurrentTransactions = InMaxConcurrentTransactions;
    Config->DefaultConfigName = InConfigName;
    Config->bUseMaterialSpecificVersioning = false;
    Config->VersionHistoryLength = 10;
    
    // Add to map
    ZoneConfigMap.Add(InConfigName, Config);
    
    // If this is the first configuration, make it the default
    if (DefaultZoneConfigName.IsNone())
    {
        DefaultZoneConfigName = InConfigName;
    }
    
    UE_LOG(LogTemp, Verbose, TEXT("FZoneTypeRegistry::RegisterZoneGridConfig - registered config '%s' with zone size %.2f"),
        *InConfigName.ToString(), InZoneSize);
    
    return true;
}

const FZoneTransactionTypeInfo* FZoneTypeRegistry::GetTransactionTypeInfo(uint32 InTypeId) const
{
    if (!IsInitialized())
    {
        return nullptr;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Look up type by ID
    const TSharedRef<FZoneTransactionTypeInfo>* TypeInfoPtr = TransactionTypeMap.Find(InTypeId);
    if (TypeInfoPtr)
    {
        return &(TypeInfoPtr->Get());
    }
    
    return nullptr;
}

const FZoneTransactionTypeInfo* FZoneTypeRegistry::GetTransactionTypeInfoByName(const FName& InTypeName) const
{
    if (!IsInitialized())
    {
        return nullptr;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Look up type ID by name
    const uint32* TypeIdPtr = TransactionTypeNameMap.Find(InTypeName);
    if (TypeIdPtr)
    {
        // Look up type info by ID
        const TSharedRef<FZoneTransactionTypeInfo>* TypeInfoPtr = TransactionTypeMap.Find(*TypeIdPtr);
        if (TypeInfoPtr)
        {
            return &(TypeInfoPtr->Get());
        }
    }
    
    return nullptr;
}

const FZoneGridConfig* FZoneTypeRegistry::GetZoneGridConfig(const FName& InConfigName) const
{
    if (!IsInitialized())
    {
        return nullptr;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Look up config by name
    const TSharedRef<FZoneGridConfig>* ConfigPtr = ZoneConfigMap.Find(InConfigName);
    if (ConfigPtr)
    {
        return &(ConfigPtr->Get());
    }
    
    return nullptr;
}

const FZoneGridConfig* FZoneTypeRegistry::GetDefaultZoneGridConfig() const
{
    if (!IsInitialized() || DefaultZoneConfigName.IsNone())
    {
        return nullptr;
    }
    
    return GetZoneGridConfig(DefaultZoneConfigName);
}

bool FZoneTypeRegistry::SetDefaultZoneGridConfig(const FName& InConfigName)
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FZoneTypeRegistry::SetDefaultZoneGridConfig failed - registry not initialized"));
        return false;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Check if config exists
    if (!ZoneConfigMap.Contains(InConfigName))
    {
        UE_LOG(LogTemp, Error, TEXT("FZoneTypeRegistry::SetDefaultZoneGridConfig failed - config '%s' not found"),
            *InConfigName.ToString());
        return false;
    }
    
    // Set as default
    DefaultZoneConfigName = InConfigName;
    
    UE_LOG(LogTemp, Verbose, TEXT("FZoneTypeRegistry::SetDefaultZoneGridConfig - set '%s' as default config"),
        *InConfigName.ToString());
    
    return true;
}

bool FZoneTypeRegistry::UpdateTransactionProperty(uint32 InTypeId, const FName& InPropertyName, const FString& InValue)
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FZoneTypeRegistry::UpdateTransactionProperty failed - registry not initialized"));
        return false;
    }
    
    if (InPropertyName.IsNone())
    {
        UE_LOG(LogTemp, Error, TEXT("FZoneTypeRegistry::UpdateTransactionProperty failed - invalid property name"));
        return false;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Check if the transaction type is registered
    TSharedRef<FZoneTransactionTypeInfo>* TypeInfoPtr = TransactionTypeMap.Find(InTypeId);
    if (!TypeInfoPtr)
    {
        UE_LOG(LogTemp, Error, TEXT("FZoneTypeRegistry::UpdateTransactionProperty failed - type ID %u not found"), InTypeId);
        return false;
    }
    
    // Update the property based on its name
    if (InPropertyName == TEXT("MaxRetries"))
    {
        TypeInfoPtr->Get().MaxRetries = FCString::Atoi(*InValue);
        return true;
    }
    else if (InPropertyName == TEXT("BaseRetryIntervalMs"))
    {
        TypeInfoPtr->Get().BaseRetryIntervalMs = FCString::Atoi(*InValue);
        return true;
    }
    else if (InPropertyName == TEXT("Priority"))
    {
        TypeInfoPtr->Get().Priority = static_cast<ETransactionPriority>(FCString::Atoi(*InValue));
        return true;
    }
    else if (InPropertyName == TEXT("RequiresVersionTracking"))
    {
        TypeInfoPtr->Get().bRequiresVersionTracking = InValue.ToBool();
        return true;
    }
    else if (InPropertyName == TEXT("SupportsFastPath"))
    {
        TypeInfoPtr->Get().bSupportsFastPath = InValue.ToBool();
        return true;
    }
    else if (InPropertyName == TEXT("FastPathThreshold"))
    {
        // Clamp to valid range
        TypeInfoPtr->Get().FastPathThreshold = FMath::Clamp(FCString::Atof(*InValue), 0.0f, 1.0f);
        return true;
    }
    else if (InPropertyName == TEXT("HasReadValidateWritePattern"))
    {
        TypeInfoPtr->Get().bHasReadValidateWritePattern = InValue.ToBool();
        return true;
    }
    
    UE_LOG(LogTemp, Warning, TEXT("FZoneTypeRegistry::UpdateTransactionProperty - unknown property '%s'"), *InPropertyName.ToString());
    return false;
}

bool FZoneTypeRegistry::UpdateFastPathThreshold(uint32 InTypeId, float InConflictRate)
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FZoneTypeRegistry::UpdateFastPathThreshold failed - registry not initialized"));
        return false;
    }
    
    // Clamp conflict rate to valid range
    float ConflictRate = FMath::Clamp(InConflictRate, 0.0f, 1.0f);
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Check if the transaction type is registered
    TSharedRef<FZoneTransactionTypeInfo>* TypeInfoPtr = TransactionTypeMap.Find(InTypeId);
    if (!TypeInfoPtr)
    {
        UE_LOG(LogTemp, Error, TEXT("FZoneTypeRegistry::UpdateFastPathThreshold failed - type ID %u not found"), InTypeId);
        return false;
    }
    
    // Skip update if fast path is not supported
    if (!TypeInfoPtr->Get().bSupportsFastPath)
    {
        UE_LOG(LogTemp, Verbose, TEXT("FZoneTypeRegistry::UpdateFastPathThreshold - fast path not supported for type ID %u"), InTypeId);
        return false;
    }
    
    // Update threshold using adaptive algorithm
    // Add small margin to observed conflict rate
    float NewThreshold = ConflictRate + 0.05f;
    
    // Blend with existing threshold (exponential moving average)
    constexpr float BlendFactor = 0.2f;
    float CurrentThreshold = TypeInfoPtr->Get().FastPathThreshold;
    float BlendedThreshold = (CurrentThreshold * (1.0f - BlendFactor)) + (NewThreshold * BlendFactor);
    
    // Clamp to valid range
    TypeInfoPtr->Get().FastPathThreshold = FMath::Clamp(BlendedThreshold, 0.05f, 0.95f);
    
    UE_LOG(LogTemp, Verbose, TEXT("FZoneTypeRegistry::UpdateFastPathThreshold - updated threshold for type ID %u to %.3f"),
        InTypeId, TypeInfoPtr->Get().FastPathThreshold);
    
    return true;
}

bool FZoneTypeRegistry::IsTransactionTypeRegistered(uint32 InTypeId) const
{
    if (!IsInitialized())
    {
        return false;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    return TransactionTypeMap.Contains(InTypeId);
}

bool FZoneTypeRegistry::IsTransactionTypeRegistered(const FName& InTypeName) const
{
    if (!IsInitialized())
    {
        return false;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    return TransactionTypeNameMap.Contains(InTypeName);
}

bool FZoneTypeRegistry::IsZoneTypeRegistered(uint32 InTypeId) const
{
    if (!IsInitialized())
    {
        return false;
    }
    
    FScopedSpinLock Lock(RegistryLock);
    
    return ZoneTypeMap.Contains(InTypeId);
}

FZoneTypeRegistry& FZoneTypeRegistry::Get()
{
    // Use double-checked locking pattern with memory barriers
    if (!Singleton)
    {
        // Fix the most vexing parse by creating a named lock object
        FSpinLock TempLock;
        FScopedSpinLock Lock(TempLock);
        if (!Singleton)
        {
            Singleton = new FZoneTypeRegistry();
            Singleton->Initialize();
        }
    }
    
    return *Singleton;
}

uint32 FZoneTypeRegistry::GenerateUniqueTypeId()
{
    // Use atomic increment to generate unique IDs
    return NextTypeId.Increment();
}

bool FZoneTypeRegistry::UpdateConflictRate(uint32 InTypeId, float InNewRate)
{
    if (!IsInitialized())
    {
        return false;
    }
    
    if (InNewRate < 0.0f || InNewRate > 1.0f)
    {
        // Invalid conflict rate provided
        return false;
    }
    
    FScopedSpinLock Lock(RegistryLock);
    
    if (!TransactionTypeMap.Contains(InTypeId))
    {
        // Type not found
        return false;
    }
    
    TSharedRef<FZoneTransactionTypeInfo>& TypeInfo = TransactionTypeMap[InTypeId];
    
    // Update conflict rate
    TypeInfo->HistoricalConflictRates.Add(InNewRate);
    
    // Keep only the last 20 rates to avoid excessive memory usage
    if (TypeInfo->HistoricalConflictRates.Num() > 20)
    {
        TypeInfo->HistoricalConflictRates.RemoveAt(0);
    }
    
    // Update overall conflict rate average
    if (TypeInfo->TotalExecutions > 0)
    {
        TypeInfo->ConflictCount = FMath::RoundToInt(TypeInfo->TotalExecutions * InNewRate);
    }
    
    // Update fast path threshold if applicable
    if (TypeInfo->bSupportsFastPath)
    {
        return UpdateFastPathThreshold(InTypeId, InNewRate);
    }
    
    return true;
}

void FZoneTypeRegistry::OnTransactionCompleted(uint32 TypeId, const FTransactionStats& Stats)
{
    // Check if registry is initialized
    if (!bIsInitialized)
    {
        return;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Record contention if there's high lock traffic
    FThreadSafety::Get().RecordContention(const_cast<FSpinLock*>(&RegistryLock));
    
    // Check if type exists
    if (!TransactionTypeMap.Contains(TypeId))
    {
        UE_LOG(LogTemp, Warning, TEXT("Received completion stats for unknown transaction type %u"), TypeId);
        return;
    }
    
    // Get mutable type info
    TSharedRef<FZoneTransactionTypeInfo>& TypeInfo = TransactionTypeMap[TypeId];
    
    // Update execution stats
    TypeInfo->TotalExecutions++;
    TypeInfo->ConflictCount += Stats.ConflictCount;
    
    // Update conflict rate history
    float ConflictRate = Stats.ConflictCount > 0 ? 1.0f : 0.0f;
    TypeInfo->HistoricalConflictRates.Add(ConflictRate);
    
    // Maintain a limited history (last 100 executions)
    const int32 MaxHistorySize = 100;
    if (TypeInfo->HistoricalConflictRates.Num() > MaxHistorySize)
    {
        TypeInfo->HistoricalConflictRates.RemoveAt(0, TypeInfo->HistoricalConflictRates.Num() - MaxHistorySize);
    }
    
    // Calculate new average conflict rate
    float TotalRate = 0.0f;
    for (float Rate : TypeInfo->HistoricalConflictRates)
    {
        TotalRate += Rate;
    }
    float AverageRate = TypeInfo->HistoricalConflictRates.Num() > 0 ? 
        TotalRate / TypeInfo->HistoricalConflictRates.Num() : 0.0f;
    
    // Update fast path threshold based on conflict history
    UpdateFastPathThreshold(TypeId, AverageRate);
}

ERegistryType FZoneTypeRegistry::GetRegistryType() const
{
    return ERegistryType::Zone;
}

ETypeCapabilities FZoneTypeRegistry::GetTypeCapabilities(uint32 TypeId) const
{
    // Start with no capabilities
    ETypeCapabilities Capabilities = ETypeCapabilities::None;
    
    // Check if this type is registered
    if (!IsTransactionTypeRegistered(TypeId))
    {
        return Capabilities;
    }
    
    // Get the transaction type info
    const FZoneTransactionTypeInfo* TypeInfo = GetTransactionTypeInfo(TypeId);
    if (!TypeInfo)
    {
        return Capabilities;
    }
    
    // Map zone transaction capabilities to type capabilities
    if (TypeInfo->bSupportsThreading)
    {
        Capabilities = TypeCapabilitiesHelpers::AddBasicCapability(
            Capabilities, ETypeCapabilities::ThreadSafe);
    }
    
    if (TypeInfo->bSupportsPartialProcessing)
    {
        Capabilities = TypeCapabilitiesHelpers::AddBasicCapability(
            Capabilities, ETypeCapabilities::PartialExecution);
    }
    
    if (TypeInfo->bSupportsIncrementalUpdates)
    {
        Capabilities = TypeCapabilitiesHelpers::AddBasicCapability(
            Capabilities, ETypeCapabilities::IncrementalUpdates);
    }
    
    if (TypeInfo->bSupportsResultMerging)
    {
        Capabilities = TypeCapabilitiesHelpers::AddBasicCapability(
            Capabilities, ETypeCapabilities::ResultMerging);
    }
    
    if (TypeInfo->bSupportsAsyncProcessing)
    {
        Capabilities = TypeCapabilitiesHelpers::AddBasicCapability(
            Capabilities, ETypeCapabilities::AsyncOperations);
    }
    
    return Capabilities;
}

ETypeCapabilitiesEx FZoneTypeRegistry::GetTypeCapabilitiesEx(uint32 TypeId) const
{
    // Start with no extended capabilities
    ETypeCapabilitiesEx Capabilities = ETypeCapabilitiesEx::None;
    
    // Check if this type is registered
    if (!IsTransactionTypeRegistered(TypeId))
    {
        return Capabilities;
    }
    
    // Get the transaction type info
    const FZoneTransactionTypeInfo* TypeInfo = GetTransactionTypeInfo(TypeId);
    if (!TypeInfo)
    {
        return Capabilities;
    }
    
    // Map zone transaction capabilities to extended type capabilities
    if (TypeInfo->bLowContention)
    {
        Capabilities = TypeCapabilitiesHelpers::AddAdvancedCapability(
            Capabilities, ETypeCapabilitiesEx::LowContention);
    }
    
    // Add additional mapping for other extended capabilities as needed
    
    return Capabilities;
}

uint64 FZoneTypeRegistry::ScheduleTypeTask(uint32 TypeId, TFunction<void()> TaskFunc, const FTaskConfig& Config)
{
    // Create a type-specific task configuration
    FTaskConfig TypedConfig = Config;
    TypedConfig.SetTypeId(TypeId, ERegistryType::Zone);
    
    // Set optimization flags based on type capabilities
    ETypeCapabilities BasicCapabilities = GetTypeCapabilities(TypeId);
    ETypeCapabilitiesEx ExtendedCapabilities = GetTypeCapabilitiesEx(TypeId);
    EThreadOptimizationFlags OptimizationFlags = FTaskScheduler::MapCapabilitiesToOptimizationFlags(
        BasicCapabilities, ExtendedCapabilities);
    
    TypedConfig.SetOptimizationFlags(OptimizationFlags);
    
    // Schedule the task with the scheduler
    return ScheduleTaskWithScheduler(TaskFunc, TypedConfig);
}

uint64 FZoneTypeRegistry::BeginAsyncTypeRegistration(const FString& SourceAsset)
{
    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot begin async registration - registry not initialized"));
        return 0;
    }
    
    // Create a new type registration operation
    TSharedPtr<FTypeRegistrationOperation> Operation = MakeShared<FTypeRegistrationOperation>();
    
    // Configure operation
    Operation->SourceAsset = SourceAsset;
    Operation->bUsingSourceAsset = true;
    
    // Generate a unique operation ID
    uint64 OperationId = FMath::Rand() * 1000 + FMath::RandHelper(999);
    
    // Add to pending operations
    {
        FScopedSpinLock Lock(PendingOperationsLock);
        PendingOperations.Add(OperationId, Operation);
    }
    
    return OperationId;
}

bool FZoneTypeRegistry::RegisterTypeRegistrationCompletionCallback(uint64 OperationId, const FTypeRegistrationCompletionDelegate& Callback)
{
    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot register completion callback - registry not initialized"));
        return false;
    }
    
    // Get the operation
    TSharedPtr<FTypeRegistrationOperation> Operation = nullptr;
    
    {
        FScopedSpinLock Lock(PendingOperationsLock);
        if (PendingOperations.Contains(OperationId))
        {
            Operation = PendingOperations[OperationId];
        }
    }
    
    if (!Operation.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot register completion callback - operation %llu not found"), OperationId);
        return false;
    }
    
    // Register the completion callback with the operation
    Operation->CompletionCallback = Callback;
    return true;
}

bool FZoneTypeRegistry::CancelAsyncTypeRegistration(uint64 OperationId, bool bWaitForCancellation)
{
    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot cancel operation - registry not initialized"));
        return false;
    }
    
    // Get the operation
    TSharedPtr<FTypeRegistrationOperation> Operation = nullptr;
    
    {
        FScopedSpinLock Lock(PendingOperationsLock);
        if (PendingOperations.Contains(OperationId))
        {
            Operation = PendingOperations[OperationId];
        }
    }
    
    if (!Operation.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot cancel operation - operation %llu not found"), OperationId);
        return false;
    }
    
    // Cancel the operation
    Operation->bCancelled = true;
    
    // Wait for cancellation if requested
    if (bWaitForCancellation)
    {
        // TODO: Implement waiting logic
    }
    
    // Remove from pending operations
    {
        FScopedSpinLock Lock(PendingOperationsLock);
        PendingOperations.Remove(OperationId);
    }
    
    return true;
}

bool FZoneTypeRegistry::PreInitializeTypes()
{
    if (!IsInitialized())
    {
        UE_LOG(LogZoneTypeRegistry, Error, TEXT("Cannot pre-initialize types - registry not initialized"));
        return false;
    }
    
    // Get all registered types
    TArray<FZoneTypeInfo> AllTypes = GetAllZoneTypes();
    
    if (AllTypes.Num() == 0)
    {
        // Nothing to initialize, consider it successful
        return true;
    }
    
    UE_LOG(LogZoneTypeRegistry, Log, TEXT("Pre-initializing %d Zone types"), AllTypes.Num());
    
    // Perform pre-initialization steps for all types
    for (const FZoneTypeInfo& TypeInfo : AllTypes)
    {
        // Pre-initialization logic
    }
    
    return true;
}

bool FZoneTypeRegistry::ParallelInitializeTypes(bool bParallel)
{
    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot initialize types - registry not initialized"));
        return false;
    }
    
    // Get all registered types
    TArray<uint32> AllTypes;
    
    {
        FScopedSpinLock Lock(RegistryLock);
        TransactionTypeMap.GetKeys(AllTypes);
    }
    
    if (AllTypes.Num() == 0)
    {
        // No types to initialize
        return true;
    }
    
    if (!bParallel)
    {
        // Sequential initialization
        for (uint32 TypeId : AllTypes)
        {
            InitializeTransactionType(TypeId);
        }
        return true;
    }
    else
    {
        // Parallel initialization using the available API
        FParallelExecutor Executor;
        
        // Bind initialization function
        Executor.ParallelFor(AllTypes.Num(),
            [this, &AllTypes](int32 Index)
            {
                InitializeTransactionType(AllTypes[Index]);
            });
            
        // The execution will happen when ParallelFor returns
        return true;
    }
}

bool FZoneTypeRegistry::PostInitializeTypes()
{
    if (!IsInitialized())
    {
        UE_LOG(LogZoneTypeRegistry, Error, TEXT("Cannot post-initialize types - registry not initialized"));
        return false;
    }
    
    // Get all registered types
    TArray<FZoneTypeInfo> AllTypes = GetAllZoneTypes();
    
    if (AllTypes.Num() == 0)
    {
        // Nothing to initialize, consider it successful
        return true;
    }
    
    UE_LOG(LogZoneTypeRegistry, Log, TEXT("Post-initializing %d Zone types"), AllTypes.Num());
    
    // Perform post-initialization steps that require all types to be initialized
    
    // Example: Validate type relationships
    TArray<FString> ValidationErrors;
    bool bSuccess = Validate(ValidationErrors);
    
    if (!bSuccess)
    {
        for (const FString& Error : ValidationErrors)
        {
            UE_LOG(LogZoneTypeRegistry, Error, TEXT("Post-initialization validation error: %s"), *Error);
        }
        return false;
    }
    
    return true;
}

TArray<int32> FZoneTypeRegistry::GetTypeDependencies(uint32 TypeId) const
{
    TArray<int32> Dependencies;
    
    if (!IsInitialized() || !IsZoneTypeRegistered(TypeId))
    {
        return Dependencies;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(this->RegistryLock);
    
    // Get the type info
    const TSharedRef<FZoneTypeInfo>* TypeInfoPtr = ZoneTypeMap.Find(TypeId);
    if (!TypeInfoPtr)
    {
        return Dependencies;
    }
    
    const FZoneTypeInfo& TypeInfo = TypeInfoPtr->Get();
    
    // Add dependencies based on hierarchy or composition requirements
    if (TypeInfo.ParentZoneTypeId != 0)
    {
        // Safely convert uint32 to int32, skipping if out of range
        if (TypeInfo.ParentZoneTypeId <= static_cast<uint32>(INT32_MAX))
        {
            Dependencies.Add(static_cast<int32>(TypeInfo.ParentZoneTypeId));
        }
        else
        {
            UE_LOG(LogZoneTypeRegistry, Warning, TEXT("Parent zone type ID %u exceeds INT32_MAX, skipping as dependency"), TypeInfo.ParentZoneTypeId);
        }
    }
    
    // Add dependencies from required material types
    for (uint32 MaterialTypeId : TypeInfo.SupportedMaterialTypes)
    {
        // Safely convert uint32 to int32, skipping if out of range
        if (MaterialTypeId <= static_cast<uint32>(INT32_MAX))
        {
            Dependencies.Add(static_cast<int32>(MaterialTypeId));
        }
        else
        {
            UE_LOG(LogZoneTypeRegistry, Warning, TEXT("Material type ID %u exceeds INT32_MAX, skipping as dependency"), MaterialTypeId);
        }
    }
    
    return Dependencies;
}

TArray<FZoneTypeInfo> FZoneTypeRegistry::GetAllZoneTypes() const
{
    TArray<FZoneTypeInfo> Result;
    
    if (!IsInitialized())
    {
        return Result;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(this->RegistryLock);
    
    // Get all type entries from the map
    for (const auto& Pair : ZoneTypeMap)
    {
        Result.Add(Pair.Value.Get());
    }
    
    return Result;
}

void FZoneTypeRegistry::InitializeTransactionType(uint32 TypeId)
{
    // Check if registry is initialized
    if (!bIsInitialized)
    {
        return;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Check if type exists
    if (!TransactionTypeMap.Contains(TypeId))
    {
        return;
    }
    
    // Get type info
    TSharedRef<FZoneTransactionTypeInfo>& TypeInfo = TransactionTypeMap[TypeId];
    
    // If already initialized, nothing to do
    if (TypeInfo->TotalExecutions > 0)
    {
        return;
    }
    
    // Get transaction manager for type initialization
    ITransactionManager& TransactionManager = FTransactionManager::Get();
    
    // Update transaction manager with type info
    TransactionManager.UpdateFastPathThreshold(TypeId, TypeInfo->FastPathThreshold);
    
    // Initialize conflict history
    if (TypeInfo->HistoricalConflictRates.Num() == 0)
    {
        // Start with assumption of low conflict rate
        TypeInfo->HistoricalConflictRates.Add(0.1f);
    }
    
    UE_LOG(LogTemp, Verbose, TEXT("Initialized transaction type '%s' (ID %u)"),
        *TypeInfo->TypeName.ToString(), TypeId);
}