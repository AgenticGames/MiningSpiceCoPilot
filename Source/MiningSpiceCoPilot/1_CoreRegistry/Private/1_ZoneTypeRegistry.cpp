// Copyright Epic Games, Inc. All Rights Reserved.

#include "ZoneTypeRegistry.h"

// Initialize static members
FZoneTypeRegistry* FZoneTypeRegistry::Singleton = nullptr;
FThreadSafeBool FZoneTypeRegistry::bSingletonInitialized = false;

FZoneTypeRegistry::FZoneTypeRegistry()
    : NextTypeId(1) // Reserve 0 as invalid/unregistered type ID
    , bIsInitialized(false)
    , SchemaVersion(1) // Initial schema version
{
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
        FRWScopeLock Lock(RegistryLock, SLT_Write);
        
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
    
    // Lock for thread safety
    FRWScopeLock Lock(RegistryLock, SLT_ReadOnly);
    
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
    if (!DefaultZoneGridConfigName.IsNone() && !ZoneGridConfigMap.Contains(DefaultZoneGridConfigName))
    {
        OutErrors.Add(FString::Printf(TEXT("Default zone grid configuration '%s' does not exist"),
            *DefaultZoneGridConfigName.ToString()));
        bIsValid = false;
    }
    
    return bIsValid;
}

void FZoneTypeRegistry::Clear()
{
    if (IsInitialized())
    {
        // Lock for thread safety
        FRWScopeLock Lock(RegistryLock, SLT_Write);
        
        // Clear all registered items
        TransactionTypeMap.Empty();
        TransactionTypeNameMap.Empty();
        ZoneGridConfigMap.Empty();
        
        // Reset counter
        NextTypeId.Set(1);
        
        // Reset default config name
        DefaultZoneGridConfigName = NAME_None;
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
    // Check if registry is initialized
    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot get type version - registry not initialized"));
        return 0;
    }
    
    // Lock for thread safety
    FRWScopeLock Lock(RegistryLock, SLT_ReadOnly);
    
    // Check if the type exists
    if (!TransactionTypeMap.Contains(TypeId))
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot get type version - type ID %u not found"), TypeId);
        return 0;
    }
    
    // Get the type info and return its schema version
    const TSharedRef<FZoneTransactionTypeInfo>& TypeInfo = TransactionTypeMap[TypeId];
    return TypeInfo->SchemaVersion;
}

uint32 FZoneTypeRegistry::RegisterTransactionType(
    const FName& InTypeName,
    ETransactionConcurrency InConcurrencyLevel,
    ERetryStrategy InRetryStrategy)
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FZoneTypeRegistry::RegisterTransactionType failed - registry not initialized"));
        return 0;
    }
    
    if (InTypeName.IsNone())
    {
        UE_LOG(LogTemp, Error, TEXT("FZoneTypeRegistry::RegisterTransactionType failed - invalid type name"));
        return 0;
    }
    
    // Lock for thread safety
    FRWScopeLock Lock(RegistryLock, SLT_Write);
    
    // Check if type name is already registered
    if (TransactionTypeNameMap.Contains(InTypeName))
    {
        UE_LOG(LogTemp, Warning, TEXT("FZoneTypeRegistry::RegisterTransactionType - type '%s' is already registered"),
            *InTypeName.ToString());
        return 0;
    }
    
    // Generate a unique type ID
    uint32 TypeId = GenerateUniqueTypeId();
    
    // Create and populate type info
    TSharedRef<FZoneTransactionTypeInfo> TypeInfo = MakeShared<FZoneTransactionTypeInfo>();
    TypeInfo->TypeId = TypeId;
    TypeInfo->TypeName = InTypeName;
    TypeInfo->ConcurrencyLevel = InConcurrencyLevel;
    TypeInfo->RetryStrategy = InRetryStrategy;
    
    // Set default values
    TypeInfo->MaxRetries = (InRetryStrategy != ERetryStrategy::None) ? 3 : 0;
    TypeInfo->BaseRetryIntervalMs = 10;
    TypeInfo->MaterialChannelId = -1; // No material channel by default
    TypeInfo->Priority = 1; // Default priority
    TypeInfo->bRequiresVersionTracking = (InConcurrencyLevel != ETransactionConcurrency::ReadOnly);
    TypeInfo->bSupportsFastPath = true;
    TypeInfo->FastPathThreshold = 0.2f;
    TypeInfo->bHasReadValidateWritePattern = (InConcurrencyLevel == ETransactionConcurrency::Optimistic);
    
    // Register the type
    TransactionTypeMap.Add(TypeId, TypeInfo);
    TransactionTypeNameMap.Add(InTypeName, TypeId);
    
    UE_LOG(LogTemp, Verbose, TEXT("FZoneTypeRegistry::RegisterTransactionType - registered type '%s' with ID %u"),
        *InTypeName.ToString(), TypeId);
    
    return TypeId;
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
    FRWScopeLock Lock(RegistryLock, SLT_Write);
    
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
    TypeInfo->Priority = 1; // Default priority
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
    FRWScopeLock Lock(RegistryLock, SLT_Write);
    
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
    if (DefaultZoneGridConfigName.IsNone())
    {
        DefaultZoneGridConfigName = InConfigName;
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
    FRWScopeLock Lock(RegistryLock, SLT_ReadOnly);
    
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
    FRWScopeLock Lock(RegistryLock, SLT_ReadOnly);
    
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
    FRWScopeLock Lock(RegistryLock, SLT_ReadOnly);
    
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
    if (!IsInitialized() || DefaultZoneGridConfigName.IsNone())
    {
        return nullptr;
    }
    
    return GetZoneGridConfig(DefaultZoneGridConfigName);
}

bool FZoneTypeRegistry::SetDefaultZoneGridConfig(const FName& InConfigName)
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FZoneTypeRegistry::SetDefaultZoneGridConfig failed - registry not initialized"));
        return false;
    }
    
    // Lock for thread safety
    FRWScopeLock Lock(RegistryLock, SLT_Write);
    
    // Check if config exists
    if (!ZoneGridConfigMap.Contains(InConfigName))
    {
        UE_LOG(LogTemp, Error, TEXT("FZoneTypeRegistry::SetDefaultZoneGridConfig failed - config '%s' not found"),
            *InConfigName.ToString());
        return false;
    }
    
    // Set as default
    DefaultZoneGridConfigName = InConfigName;
    
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
    FRWScopeLock Lock(RegistryLock, SLT_Write);
    
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
        TypeInfoPtr->Get().Priority = FCString::Atoi(*InValue);
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
    FRWScopeLock Lock(RegistryLock, SLT_Write);
    
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
    FRWScopeLock Lock(RegistryLock, SLT_ReadOnly);
    
    return TransactionTypeMap.Contains(InTypeId);
}

bool FZoneTypeRegistry::IsTransactionTypeRegistered(const FName& InTypeName) const
{
    if (!IsInitialized())
    {
        return false;
    }
    
    // Lock for thread safety
    FRWScopeLock Lock(RegistryLock, SLT_ReadOnly);
    
    return TransactionTypeNameMap.Contains(InTypeName);
}

FZoneTypeRegistry& FZoneTypeRegistry::Get()
{
    // Thread-safe singleton initialization
    if (!bSingletonInitialized)
    {
        if (!bSingletonInitialized.AtomicSet(true))
        {
            Singleton = new FZoneTypeRegistry();
            Singleton->Initialize();
        }
    }
    
    check(Singleton != nullptr);
    return *Singleton;
}

uint32 FZoneTypeRegistry::GenerateUniqueTypeId()
{
    // Simply increment and return the next ID
    // This function is called within a locked context, so it's thread-safe
    return NextTypeId.Increment();
}