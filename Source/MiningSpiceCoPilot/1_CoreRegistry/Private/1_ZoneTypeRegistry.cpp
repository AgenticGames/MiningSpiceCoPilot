// Copyright Epic Games, Inc. All Rights Reserved.

#include "ZoneTypeRegistry.h"
#include "Interfaces/IServiceLocator.h"
#include "CoreServiceLocator.h"
#include "ThreadSafety.h"
#include "TransactionManager.h"
#include "Interfaces/ITransactionManager.h"

// Initialize static members
FZoneTypeRegistry* FZoneTypeRegistry::Instance = nullptr;

FZoneTypeRegistry::FZoneTypeRegistry()
    : SchemaVersion(1) // Initial schema version
    , bIsInitialized(false)
{
    // Initialize NextTypeId with value 1 (Reserve 0 as invalid/unregistered type ID)
    NextTypeId.Set(1);
    // Initialize type version counter
    TypeVersion.Set(1);
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
    bIsInitialized = true;
    
    // Initialize internal maps
    TransactionTypeMap.Empty();
    TransactionTypeNameMap.Empty();
    ZoneGridConfigMap.Empty();
    
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
        ZoneGridConfigMap.Empty();
        
        // Reset state
        bIsInitialized = false;
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
    return SchemaVersion;
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
    for (const auto& ConfigPair : ZoneGridConfigMap)
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
    if (!DefaultConfigName.IsNone() && !ZoneGridConfigMap.Contains(DefaultConfigName))
    {
        OutErrors.Add(FString::Printf(TEXT("Default zone grid configuration '%s' does not exist"),
            *DefaultConfigName.ToString()));
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
        ZoneGridConfigMap.Empty();
        
        // Reset counter
        NextTypeId.Set(1);
        
        // Reset default config name
        DefaultConfigName = NAME_None;
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
    
    // Store old version for logging
    uint32 OldVersion = TypeInfo->SchemaVersion;
    
    // Update type version
    TypeInfo->SchemaVersion = NewVersion;
    
    UE_LOG(LogTemp, Log, TEXT("Updated type '%s' version from %u to %u"),
        *TypeInfo->TypeName.ToString(), OldVersion, NewVersion);
    
    // If migration is not requested, we're done
    if (!bMigrateInstanceData)
    {
        return true;
    }
    
    // Integrate with MemoryPoolManager to update memory state
    IMemoryManager* MemoryManager = IServiceLocator::Get().ResolveService<IMemoryManager>();
    if (!MemoryManager)
    {
        UE_LOG(LogTemp, Warning, TEXT("Memory migration skipped for type '%s' - Memory Manager not available"),
            *TypeInfo->TypeName.ToString());
        return true; // Still return true as the version was updated
    }
    
    // Create memory migration data
    FTypeVersionMigrationInfo MigrationInfo;
    MigrationInfo.TypeId = TypeId;
    MigrationInfo.TypeName = TypeInfo->TypeName;
    MigrationInfo.OldVersion = OldVersion;
    MigrationInfo.NewVersion = NewVersion;
    
    // Find the pool allocator for this type
    IPoolAllocator* PoolAllocator = MemoryManager->GetPoolForType(TypeId);
    if (!PoolAllocator)
    {
        UE_LOG(LogTemp, Warning, TEXT("Memory migration skipped for type '%s' - No pool allocator found"),
            *TypeInfo->TypeName.ToString());
        return true; // Still return true as the version was updated
    }
    
    // Apply the type version update to the pool
    if (!PoolAllocator->UpdateTypeVersion(MigrationInfo))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to update memory pool for type '%s' version %u -> %u"),
            *TypeInfo->TypeName.ToString(), OldVersion, NewVersion);
        return false;
    }
    
    return true;
}

uint32 FZoneTypeRegistry::GetTypeVersion(uint32 TypeId) const
{
    if (!IsInitialized() || !TransactionTypeMap.Contains(TypeId))
    {
        return 0;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Record contention if there's high lock traffic
    FThreadSafety::Get().RecordContention(const_cast<FSpinLock*>(&RegistryLock));
    
    return TransactionTypeMap[TypeId]->SchemaVersion;
}

uint32 FZoneTypeRegistry::RegisterTransactionType(
    const FName& InTypeName,
    ETransactionConcurrency InConcurrencyLevel,
    ERetryStrategy InRetryStrategy)
{
    // Check if registry is initialized
    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot register transaction type - registry not initialized"));
        return 0;
    }
    
    // Check if type already exists (with a read lock first)
    {
        FScopedSpinLock ReadLock(RegistryLock);
        
        // Record contention if there's high lock traffic
        FThreadSafety::Get().RecordContention(const_cast<FSpinLock*>(&RegistryLock));
        
        if (TransactionTypeNameMap.Contains(InTypeName))
        {
            UE_LOG(LogTemp, Warning, TEXT("Transaction type '%s' is already registered with ID %u"),
                *InTypeName.ToString(), TransactionTypeNameMap[InTypeName]);
            return TransactionTypeNameMap[InTypeName];
        }
    }
    
    // Implement optimistic locking for transaction registration
    for (int32 RetryCount = 0; RetryCount < 3; RetryCount++)
    {
        // Read the current type version before attempting the registration
        int32 ExpectedVersion = TypeVersion.GetValue();
        
        // Create the new transaction type info
        TSharedRef<FZoneTransactionTypeInfo> TypeInfo = MakeShared<FZoneTransactionTypeInfo>();
        TypeInfo->TypeName = InTypeName;
        TypeInfo->ConcurrencyLevel = InConcurrencyLevel;
        TypeInfo->RetryStrategy = InRetryStrategy;
        TypeInfo->MaxRetries = (InRetryStrategy == ERetryStrategy::None) ? 0 : 3;
        TypeInfo->BaseRetryIntervalMs = 10;
        TypeInfo->MaterialChannelId = -1; // Default to -1 (not material-specific)
        TypeInfo->Priority = static_cast<ETransactionPriority>(100); // Default priority
        TypeInfo->bRequiresVersionTracking = (InConcurrencyLevel != ETransactionConcurrency::ReadOnly);
        TypeInfo->bSupportsFastPath = (InConcurrencyLevel == ETransactionConcurrency::ReadOnly || 
                                      InConcurrencyLevel == ETransactionConcurrency::Optimistic);
        TypeInfo->FastPathThreshold = 0.2f; // Default threshold (20% conflict rate for fast path)
        TypeInfo->bHasReadValidateWritePattern = (InConcurrencyLevel == ETransactionConcurrency::Optimistic);
        TypeInfo->SchemaVersion = 1; // Initial version
        
        // Add transaction capability metrics for conflict tracking
        TypeInfo->HistoricalConflictRates.Add(0.0f); // Initial conflict rate
        TypeInfo->TotalExecutions = 0; 
        TypeInfo->ConflictCount = 0;
        TypeInfo->bSupportsPartialExecution = (InConcurrencyLevel == ETransactionConcurrency::Optimistic || 
                                              InConcurrencyLevel == ETransactionConcurrency::MaterialChannel);
        TypeInfo->bCanMergeResults = (InConcurrencyLevel != ETransactionConcurrency::Exclusive);
        TypeInfo->Priority = (InConcurrencyLevel == ETransactionConcurrency::ReadOnly) ? static_cast<ETransactionPriority>(50) : 
                            (InConcurrencyLevel == ETransactionConcurrency::Exclusive) ? static_cast<ETransactionPriority>(200) : static_cast<ETransactionPriority>(100);
        
        // Generate a unique type ID (atomic operation)
        uint32 NewTypeId = NextTypeId.Increment();
        TypeInfo->TypeId = NewTypeId;
        
        // Now attempt to commit the changes if the version hasn't changed
        {
            FScopedSpinLock WriteLock(RegistryLock);
            
            // Record contention if there's high lock traffic
            FThreadSafety::Get().RecordContention(const_cast<FSpinLock*>(&RegistryLock));
            
            // Check if another thread registered the same type while we were preparing
            if (TransactionTypeNameMap.Contains(InTypeName))
            {
                UE_LOG(LogTemp, Warning, TEXT("Race condition: Transaction type '%s' was registered by another thread with ID %u"),
                    *InTypeName.ToString(), TransactionTypeNameMap[InTypeName]);
                return TransactionTypeNameMap[InTypeName];
            }
            
            // Use FThreadSafetyHelpers::AtomicCompareExchange to validate and update the version
            int32 ActualVersion;
            if (FThreadSafetyHelpers::AtomicCompareExchange(TypeVersion, ActualVersion, ExpectedVersion + 1, ExpectedVersion))
            {
                // Success! Register the new type
                TransactionTypeMap.Add(NewTypeId, TypeInfo);
                TransactionTypeNameMap.Add(InTypeName, NewTypeId);
                
                // Register the transaction completion callback
                if (!CompletionCallbacks.Contains(NewTypeId))
                {
                    CompletionCallbacks.Add(NewTypeId, FTransactionCompletionDelegate::CreateRaw(this, &FZoneTypeRegistry::OnTransactionCompleted));
                    
                    // Register the callback with the TransactionManager
                    FTransactionManager::Get().RegisterCompletionCallback(NewTypeId, CompletionCallbacks[NewTypeId]);
                }
                
                // Update the FastPathThreshold in FTransactionManager
                FTransactionManager::Get().UpdateFastPathThreshold(NewTypeId, TypeInfo->FastPathThreshold);
                
                UE_LOG(LogTemp, Log, TEXT("Registered transaction type '%s' with ID %u"),
                    *InTypeName.ToString(), NewTypeId);
                
                return NewTypeId;
            }
            else
            {
                // Version changed, need to retry
                UE_LOG(LogTemp, Verbose, TEXT("Optimistic concurrency collision on transaction type registration '%s', retrying..."),
                    *InTypeName.ToString());
                
                // Small backoff before retry
                FPlatformProcess::Sleep(0.001f * FMath::Pow(2.0f, RetryCount));
            }
        }
    }
    
    // If we reach here, we failed after multiple retries
    UE_LOG(LogTemp, Error, TEXT("Failed to register transaction type '%s' after multiple retries"),
        *InTypeName.ToString());
    return 0;
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
    
    // Check if config name is already registered
    if (ZoneGridConfigMap.Contains(InConfigName))
    {
        UE_LOG(LogTemp, Warning, TEXT("FZoneTypeRegistry::RegisterZoneGridConfig - config '%s' is already registered"),
            *InConfigName.ToString());
        return false;
    }
    
    // Create and populate config
    TSharedRef<FZoneGridConfig> Config = MakeShared<FZoneGridConfig>();
    Config->ZoneSize = InZoneSize;
    Config->DefaultConfigName = InConfigName;
    Config->MaxConcurrentTransactions = InMaxConcurrentTransactions;
    Config->bUseMaterialSpecificVersioning = true;
    Config->VersionHistoryLength = 8;
    
    // Register the config
    ZoneGridConfigMap.Add(InConfigName, Config);
    
    // If this is the first config, make it the default
    if (DefaultConfigName.IsNone())
    {
        DefaultConfigName = InConfigName;
        UE_LOG(LogTemp, Verbose, TEXT("FZoneTypeRegistry::RegisterZoneGridConfig - set '%s' as default config"),
            *InConfigName.ToString());
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
    const TSharedRef<FZoneGridConfig>* ConfigPtr = ZoneGridConfigMap.Find(InConfigName);
    if (ConfigPtr)
    {
        return &(ConfigPtr->Get());
    }
    
    return nullptr;
}

const FZoneGridConfig* FZoneTypeRegistry::GetDefaultZoneGridConfig() const
{
    if (!IsInitialized() || DefaultConfigName.IsNone())
    {
        return nullptr;
    }
    
    return GetZoneGridConfig(DefaultConfigName);
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
    if (!ZoneGridConfigMap.Contains(InConfigName))
    {
        UE_LOG(LogTemp, Error, TEXT("FZoneTypeRegistry::SetDefaultZoneGridConfig failed - config '%s' not found"),
            *InConfigName.ToString());
        return false;
    }
    
    // Set as default
    DefaultConfigName = InConfigName;
    
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

FZoneTypeRegistry& FZoneTypeRegistry::Get()
{
    // Use double-checked locking pattern with memory barriers
    if (!Instance)
    {
        // Fix the most vexing parse by creating a named lock object
        FSpinLock TempLock;
        FScopedSpinLock Lock(TempLock);
        if (!Instance)
        {
            Instance = new FZoneTypeRegistry();
            Instance->Initialize();
        }
    }
    
    return *Instance;
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
        UE_LOG(LogTemp, Error, TEXT("FZoneTypeRegistry::UpdateConflictRate failed - registry not initialized"));
        return false;
    }
    
    // Clamp conflict rate to valid range
    float ConflictRate = FMath::Clamp(InNewRate, 0.0f, 1.0f);
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Check if the transaction type is registered
    TSharedRef<FZoneTransactionTypeInfo>* TypeInfoPtr = TransactionTypeMap.Find(InTypeId);
    if (!TypeInfoPtr)
    {
        UE_LOG(LogTemp, Error, TEXT("FZoneTypeRegistry::UpdateConflictRate failed - type ID %u not found"), InTypeId);
        return false;
    }
    
    // Update conflict statistics
    TypeInfoPtr->Get().HistoricalConflictRates.Add(ConflictRate);
    
    // Limit history size to prevent excessive memory usage
    const int32 MaxHistorySize = 100;
    if (TypeInfoPtr->Get().HistoricalConflictRates.Num() > MaxHistorySize)
    {
        TypeInfoPtr->Get().HistoricalConflictRates.RemoveAt(0);
    }
    
    // Increment total executions
    TypeInfoPtr->Get().TotalExecutions++;
    
    // Update conflict count if this was a conflict
    if (ConflictRate > 0.0f)
    {
        TypeInfoPtr->Get().ConflictCount++;
    }
    
    // Calculate new fast path threshold based on moving average of conflict rates
    float AverageConflictRate = 0.0f;
    for (float Rate : TypeInfoPtr->Get().HistoricalConflictRates)
    {
        AverageConflictRate += Rate;
    }
    AverageConflictRate /= TypeInfoPtr->Get().HistoricalConflictRates.Num();
    
    // Update the FastPathThreshold in the registry
    UpdateFastPathThreshold(InTypeId, AverageConflictRate);
    
    // Also update it in the TransactionManager
    FTransactionManager::Get().UpdateFastPathThreshold(InTypeId, TypeInfoPtr->Get().FastPathThreshold);
    
    UE_LOG(LogTemp, Verbose, TEXT("FZoneTypeRegistry::UpdateConflictRate - updated conflict rate for type ID %u to %.3f"),
        InTypeId, ConflictRate);
    
    return true;
}

bool FZoneTypeRegistry::RegisterZoneHierarchy(int32 ParentZoneId, const TArray<int32>& ChildZones)
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FZoneTypeRegistry::RegisterZoneHierarchy failed - registry not initialized"));
        return false;
    }
    
    if (ParentZoneId < 0)
    {
        UE_LOG(LogTemp, Error, TEXT("FZoneTypeRegistry::RegisterZoneHierarchy failed - invalid parent zone ID"));
        return false;
    }
    
    if (ChildZones.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("FZoneTypeRegistry::RegisterZoneHierarchy - no child zones provided for parent %d"), ParentZoneId);
        return false;
    }
    
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Create or update parent-child relationships
    TArray<int32>& Children = ZoneHierarchy.FindOrAdd(ParentZoneId);
    
    // Add all children that aren't already in the list
    for (int32 ChildId : ChildZones)
    {
        if (ChildId >= 0 && ChildId != ParentZoneId)
        {
            // Ensure we don't create cycles in the hierarchy
            if (GetParentZone(ParentZoneId) == ChildId)
            {
                UE_LOG(LogTemp, Error, TEXT("FZoneTypeRegistry::RegisterZoneHierarchy - detected cycle: parent %d is a child of %d"),
                    ParentZoneId, ChildId);
                continue;
            }
            
            if (!Children.Contains(ChildId))
            {
                Children.Add(ChildId);
            }
            
            // Update reverse mapping
            ChildToParentMap.FindOrAdd(ChildId) = ParentZoneId;
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("FZoneTypeRegistry::RegisterZoneHierarchy - invalid child zone ID %d"), ChildId);
        }
    }
    
    UE_LOG(LogTemp, Verbose, TEXT("FZoneTypeRegistry::RegisterZoneHierarchy - registered %d children for parent zone %d"),
        Children.Num(), ParentZoneId);
    
    return true;
}

TArray<int32> FZoneTypeRegistry::GetChildZones(int32 ParentZoneId) const
{
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Look up children for this parent
    const TArray<int32>* Children = ZoneHierarchy.Find(ParentZoneId);
    
    // Return a copy of the array, or empty array if not found
    return Children ? *Children : TArray<int32>();
}

int32 FZoneTypeRegistry::GetParentZone(int32 ChildZoneId) const
{
    // Lock for thread safety
    FScopedSpinLock Lock(RegistryLock);
    
    // Look up parent for this child
    const int32* Parent = ChildToParentMap.Find(ChildZoneId);
    
    // Return parent ID, or INDEX_NONE if not found
    return Parent ? *Parent : INDEX_NONE;
}

void FZoneTypeRegistry::OnTransactionCompleted(uint32 TypeId, const FTransactionStats& Stats)
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FZoneTypeRegistry::OnTransactionCompleted - registry not initialized"));
        return;
    }
    
    // Calculate conflict rate from transaction statistics
    float ConflictRate = 0.0f;
    
    // If there were retries, there were conflicts
    if (Stats.RetryCount > 0)
    {
        // Simple formula: conflict rate increases with retry count
        // 0 retries = 0.0, 1 retry = 0.3, 2 retries = 0.6, 3+ retries = 0.9
        ConflictRate = FMath::Min(Stats.RetryCount * 0.3f, 0.9f);
    }
    
    // For committed transactions with no retries, record a low conflict rate
    // This helps in determining real-world performance
    if (Stats.RetryCount == 0)
    {
        ConflictRate = 0.0f;
    }
    
    // Update conflict statistics for this transaction type
    UpdateConflictRate(TypeId, ConflictRate);
    
    UE_LOG(LogTemp, Verbose, TEXT("FZoneTypeRegistry::OnTransactionCompleted - transaction type %u completed with conflict rate %.2f"),
        TypeId, ConflictRate);
}