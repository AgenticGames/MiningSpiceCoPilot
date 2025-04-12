// TransactionContextFactory.cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factory/TransactionContextFactory.h"
#include "Interfaces/IComponentPoolManager.h"
#include "Interfaces/IFactoryMetrics.h"

namespace
{
    // Singleton instance
    UTransactionContextFactory* GlobalFactoryInstance = nullptr;

    // Factory name
    const FName FactoryName = "TransactionContextFactory";
}

UTransactionContextFactory::UTransactionContextFactory()
    : bIsInitialized(false)
    , FactoryName(::FactoryName)
{
    // Store singleton reference
    if (!GlobalFactoryInstance)
    {
        GlobalFactoryInstance = this;
    }
}

bool UTransactionContextFactory::Initialize()
{
    if (bIsInitialized)
    {
        return true;
    }

    // Get component pool manager
    IComponentPoolManager& PoolManagerRef = IComponentPoolManager::Get();
    PoolManager = TScriptInterface<IComponentPoolManager>(&PoolManagerRef);

    // Initialize metrics tracking
    IFactoryMetrics::Get().TrackOperation(
        FactoryName,
        nullptr,
        EFactoryOperationType::Initialize, 
        0.0f);

    bIsInitialized = true;
    UE_LOG(LogTemp, Display, TEXT("TransactionContextFactory initialized"));
    return true;
}

void UTransactionContextFactory::Shutdown()
{
    if (!bIsInitialized)
    {
        return;
    }

    // Track operation for metrics
    IFactoryMetrics::Get().TrackOperation(
        FactoryName,
        nullptr,
        EFactoryOperationType::Shutdown, 
        0.0f);

    // Clear archetypes
    Archetypes.Empty();
    
    // Clear supported types
    SupportedTypes.Empty();
    
    bIsInitialized = false;
    UE_LOG(LogTemp, Display, TEXT("TransactionContextFactory shut down"));
}

bool UTransactionContextFactory::IsInitialized() const
{
    return bIsInitialized;
}

FName UTransactionContextFactory::GetFactoryName() const
{
    return FactoryName;
}

bool UTransactionContextFactory::SupportsType(UClass* ComponentType) const
{
    if (!ComponentType)
    {
        return false;
    }

    return SupportedTypes.Contains(ComponentType);
}

UObject* UTransactionContextFactory::CreateComponent(UClass* ComponentType, const TMap<FName, FString>& Parameters)
{
    if (!bIsInitialized || !ComponentType)
    {
        UE_LOG(LogTemp, Warning, TEXT("TransactionContextFactory: Cannot create component - factory not initialized or invalid component type"));
        return nullptr;
    }

    if (!SupportedTypes.Contains(ComponentType))
    {
        UE_LOG(LogTemp, Warning, TEXT("TransactionContextFactory: Component type not supported: %s"), *ComponentType->GetName());
        return nullptr;
    }

    // Start timing for metrics
    int32 MetricHandle = IFactoryMetrics::Get().BeginOperation(
        FactoryName, 
        ComponentType, 
        EFactoryOperationType::Create);

    // Parse transaction type parameter
    ETransactionType TransactionType = ETransactionType::Read;
    if (Parameters.Contains(FName("TransactionType")))
    {
        FString TransactionTypeStr = Parameters.FindRef(FName("TransactionType"));
        if (TransactionTypeStr.Equals("Read", ESearchCase::IgnoreCase))
        {
            TransactionType = ETransactionType::Read;
        }
        else if (TransactionTypeStr.Equals("Write", ESearchCase::IgnoreCase))
        {
            TransactionType = ETransactionType::Write;
        }
        else if (TransactionTypeStr.Equals("ReadWrite", ESearchCase::IgnoreCase))
        {
            TransactionType = ETransactionType::ReadWrite;
        }
    }
    
    // Parse priority parameter
    ETransactionPriority Priority = ETransactionPriority::Normal;
    if (Parameters.Contains(FName("Priority")))
    {
        FString PriorityStr = Parameters.FindRef(FName("Priority"));
        if (PriorityStr.Equals("Low", ESearchCase::IgnoreCase))
        {
            Priority = ETransactionPriority::Low;
        }
        else if (PriorityStr.Equals("Normal", ESearchCase::IgnoreCase))
        {
            Priority = ETransactionPriority::Normal;
        }
        else if (PriorityStr.Equals("High", ESearchCase::IgnoreCase))
        {
            Priority = ETransactionPriority::High;
        }
        else if (PriorityStr.Equals("Critical", ESearchCase::IgnoreCase))
        {
            Priority = ETransactionPriority::Critical;
        }
    }
    
    // Parse isolation level parameter
    ETransactionIsolation IsolationLevel = ETransactionIsolation::ReadCommitted;
    if (Parameters.Contains(FName("IsolationLevel")))
    {
        FString IsolationStr = Parameters.FindRef(FName("IsolationLevel"));
        if (IsolationStr.Equals("ReadUncommitted", ESearchCase::IgnoreCase))
        {
            IsolationLevel = ETransactionIsolation::ReadUncommitted;
        }
        else if (IsolationStr.Equals("ReadCommitted", ESearchCase::IgnoreCase))
        {
            IsolationLevel = ETransactionIsolation::ReadCommitted;
        }
        else if (IsolationStr.Equals("RepeatableRead", ESearchCase::IgnoreCase))
        {
            IsolationLevel = ETransactionIsolation::RepeatableRead;
        }
        else if (IsolationStr.Equals("Serializable", ESearchCase::IgnoreCase))
        {
            IsolationLevel = ETransactionIsolation::Serializable;
        }
    }

    // Parse timeout parameter
    float TimeoutSeconds = 5.0f; // Default timeout
    if (Parameters.Contains(FName("TimeoutSeconds")))
    {
        TimeoutSeconds = FCString::Atof(*Parameters.FindRef(FName("TimeoutSeconds")));
    }

    // Try to find appropriate pool based on transaction type and priority
    FName PoolName = GetTransactionPoolName(TransactionType, Priority);
    UObject* Result = nullptr;
    bool bCacheMiss = false;
    
    // Check if we have a pool for this configuration
    if (!PoolName.IsNone() && PoolManager->HasPool(PoolName))
    {
        // Try to get from pool
        Result = PoolManager->AllocateComponent(PoolName);
        
        if (!Result)
        {
            // Create new instance if pool is empty
            Result = NewObject<UObject>(GetTransientPackage(), ComponentType);
            bCacheMiss = true;
        }
    }
    else
    {
        // Create new instance without pooling
        Result = NewObject<UObject>(GetTransientPackage(), ComponentType);
    }

    if (Result)
    {
        // Configure the transaction context
        ConfigureTransaction(Result, TransactionType, Priority, IsolationLevel, TimeoutSeconds);
        
        // Apply remaining parameters
        for (const auto& Param : Parameters)
        {
            // Skip already processed parameters
            if (Param.Key == FName("TransactionType") || 
                Param.Key == FName("Priority") || 
                Param.Key == FName("IsolationLevel") ||
                Param.Key == FName("TimeoutSeconds"))
            {
                continue;
            }
            
            FProperty* Property = Result->GetClass()->FindPropertyByName(Param.Key);
            if (Property)
            {
                // Set property from string representation
                Property->ImportText_Direct(*Param.Value, Property->ContainerPtrToValuePtr<void>(Result), Result, PPF_None);
            }
        }
    }

    // End metrics timing
    IFactoryMetrics::Get().EndOperation(MetricHandle, Result != nullptr, bCacheMiss);

    return Result;
}

TArray<UClass*> UTransactionContextFactory::GetSupportedTypes() const
{
    TArray<UClass*> Types;
    SupportedTypes.GetKeys(Types);
    return Types;
}

bool UTransactionContextFactory::RegisterArchetype(UClass* ComponentType, UObject* Archetype)
{
    if (!bIsInitialized || !ComponentType || !Archetype || !Archetype->IsA(ComponentType))
    {
        return false;
    }

    // Register the component type if not already registered
    if (!SupportedTypes.Contains(ComponentType))
    {
        SupportedTypes.Add(ComponentType);
    }

    // Store archetype for future use
    Archetypes.Add(ComponentType, Archetype);
    
    UE_LOG(LogTemp, Display, TEXT("TransactionContextFactory: Registered archetype for %s"), *ComponentType->GetName());
    return true;
}

bool UTransactionContextFactory::HasPool(UClass* ComponentType) const
{
    if (!bIsInitialized || !ComponentType || !PoolManager)
    {
        return false;
    }

    return PoolManager->HasPoolForType(ComponentType);
}

bool UTransactionContextFactory::CreatePool(UClass* ComponentType, int32 InitialSize, int32 MaxSize, bool bEnablePooling)
{
    if (!bIsInitialized || !ComponentType || !PoolManager)
    {
        return false;
    }

    // Configure pool
    FComponentPoolConfig PoolConfig;
    PoolConfig.PoolName = FName(*FString::Printf(TEXT("%s_Pool"), *ComponentType->GetName()));
    PoolConfig.ComponentType = ComponentType;
    PoolConfig.InitialSize = InitialSize;
    PoolConfig.MaxSize = MaxSize;
    PoolConfig.bPreallocate = true;
    PoolConfig.Template = Archetypes.FindRef(ComponentType);
    
    // Create pool
    bool bSuccess = PoolManager->CreatePool(PoolConfig);
    
    if (bSuccess)
    {
        UE_LOG(LogTemp, Display, TEXT("TransactionContextFactory: Created pool for %s (Initial: %d, Max: %d)"), 
            *ComponentType->GetName(), InitialSize, MaxSize);
    }
    
    return bSuccess;
}

bool UTransactionContextFactory::ReturnToPool(UObject* Component)
{
    if (!bIsInitialized || !Component || !PoolManager)
    {
        return false;
    }

    // Start timing for metrics
    int32 MetricHandle = IFactoryMetrics::Get().BeginOperation(
        FactoryName, 
        Component->GetClass(), 
        EFactoryOperationType::Return);
    
    // Return to pool
    bool bSuccess = PoolManager->ReleaseComponent(Component);
    
    // End metrics timing
    IFactoryMetrics::Get().EndOperation(MetricHandle, bSuccess, false);
    
    return bSuccess;
}

int32 UTransactionContextFactory::FlushPool(UClass* ComponentType)
{
    if (!bIsInitialized || !ComponentType || !PoolManager)
    {
        return 0;
    }

    // Get current pool stats
    FComponentPoolStats Stats;
    if (!PoolManager->GetPoolStats(FName(*FString::Printf(TEXT("%s_Pool"), *ComponentType->GetName())), Stats))
    {
        return 0;
    }
    
    int32 AvailableBefore = Stats.AvailableCount;
    
    // Shrink the pool to remove all available instances
    int32 Removed = PoolManager->ShrinkPool(
        FName(*FString::Printf(TEXT("%s_Pool"), *ComponentType->GetName())), 
        AvailableBefore, 
        0.0f);
    
    UE_LOG(LogTemp, Display, TEXT("TransactionContextFactory: Flushed pool for %s (%d instances removed)"), 
        *ComponentType->GetName(), Removed);
        
    return Removed;
}

bool UTransactionContextFactory::GetPoolStats(UClass* ComponentType, int32& OutAvailable, int32& OutTotal) const
{
    if (!bIsInitialized || !ComponentType || !PoolManager)
    {
        OutAvailable = 0;
        OutTotal = 0;
        return false;
    }

    FComponentPoolStats Stats;
    bool bSuccess = PoolManager->GetPoolStats(FName(*FString::Printf(TEXT("%s_Pool"), *ComponentType->GetName())), Stats);
    
    if (bSuccess)
    {
        OutAvailable = Stats.AvailableCount;
        OutTotal = Stats.CurrentSize;
    }
    else
    {
        OutAvailable = 0;
        OutTotal = 0;
    }
    
    return bSuccess;
}

UObject* UTransactionContextFactory::CreateTransactionContext(ETransactionType Type, ETransactionPriority Priority, ETransactionIsolation IsolationLevel, float TimeoutSeconds)
{
    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Warning, TEXT("TransactionContextFactory: Cannot create transaction - factory not initialized"));
        return nullptr;
    }

    // Get the class to instantiate for transactions
    UClass* TransactionClass = nullptr;
    for (UClass* Class : SupportedTypes)
    {
        if (Class->GetName().Contains(TEXT("Transaction")))
        {
            TransactionClass = Class;
            break;
        }
    }
    
    if (!TransactionClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("TransactionContextFactory: No suitable class found for transaction context"));
        return nullptr;
    }

    // Start timing for metrics
    int32 MetricHandle = IFactoryMetrics::Get().BeginOperation(
        FactoryName, 
        TransactionClass, 
        EFactoryOperationType::Create);

    // Try to find appropriate pool
    FName PoolName = GetTransactionPoolName(Type, Priority);
    UObject* Result = nullptr;
    bool bCacheMiss = false;
    
    // Check if we have a pool for this configuration
    if (!PoolName.IsNone() && PoolManager->HasPool(PoolName))
    {
        // Try to get from pool
        Result = PoolManager->AllocateComponent(PoolName);
        
        if (!Result)
        {
            // Create new instance if pool is empty
            Result = NewObject<UObject>(GetTransientPackage(), TransactionClass);
            bCacheMiss = true;
        }
    }
    else
    {
        // Create new instance without pooling
        Result = NewObject<UObject>(GetTransientPackage(), TransactionClass);
    }

    if (Result)
    {
        // Configure the transaction
        ConfigureTransaction(Result, Type, Priority, IsolationLevel, TimeoutSeconds);
    }

    // End metrics timing
    IFactoryMetrics::Get().EndOperation(MetricHandle, Result != nullptr, bCacheMiss);
    
    return Result;
}

UObject* UTransactionContextFactory::BeginBatchTransaction(TArray<UObject*> Zones, ETransactionType Type, ETransactionPriority Priority)
{
    if (!bIsInitialized || Zones.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("TransactionContextFactory: Cannot create batch transaction - invalid parameters"));
        return nullptr;
    }
    
    // Default settings for batch transactions
    ETransactionIsolation IsolationLevel = ETransactionIsolation::ReadCommitted;
    float TimeoutSeconds = 10.0f; // Longer timeout for batch operations
    
    // Create base transaction
    UObject* TransactionContext = CreateTransactionContext(
        Type, 
        Priority, 
        IsolationLevel, 
        TimeoutSeconds);
        
    if (!TransactionContext)
    {
        return nullptr;
    }
    
    // Add all zones to the transaction context
    AddZonesToTransaction(TransactionContext, Zones);
    
    // Initialize the batch transaction
    UFunction* InitBatchFunc = TransactionContext->FindFunction(FName("InitializeBatch"));
    if (InitBatchFunc)
    {
        TransactionContext->ProcessEvent(InitBatchFunc, nullptr);
    }
    
    return TransactionContext;
}

UTransactionContextFactory* UTransactionContextFactory::Get()
{
    if (!GlobalFactoryInstance)
    {
        // Create instance if needed
        GlobalFactoryInstance = NewObject<UTransactionContextFactory>();
        GlobalFactoryInstance->AddToRoot(); // Prevent garbage collection
        GlobalFactoryInstance->Initialize();
    }
    
    return GlobalFactoryInstance;
}

bool UTransactionContextFactory::ConfigureTransaction(UObject* Transaction, ETransactionType Type, ETransactionPriority Priority, ETransactionIsolation IsolationLevel, float TimeoutSeconds)
{
    if (!Transaction)
    {
        return false;
    }
    
    // Set transaction type property
    FProperty* TypeProperty = Transaction->GetClass()->FindPropertyByName(FName("TransactionType"));
    if (TypeProperty)
    {
        FString TypeStr = FString::FromInt((int32)Type);
        TypeProperty->ImportText_Direct(*TypeStr, TypeProperty->ContainerPtrToValuePtr<void>(Transaction), Transaction, PPF_None);
    }
    
    // Set priority property
    FProperty* PriorityProperty = Transaction->GetClass()->FindPropertyByName(FName("Priority"));
    if (PriorityProperty)
    {
        FString PriorityStr = FString::FromInt((int32)Priority);
        PriorityProperty->ImportText_Direct(*PriorityStr, PriorityProperty->ContainerPtrToValuePtr<void>(Transaction), Transaction, PPF_None);
    }
    
    // Set isolation level property
    FProperty* IsolationProperty = Transaction->GetClass()->FindPropertyByName(FName("IsolationLevel"));
    if (IsolationProperty)
    {
        FString IsolationStr = FString::FromInt((int32)IsolationLevel);
        IsolationProperty->ImportText_Direct(*IsolationStr, IsolationProperty->ContainerPtrToValuePtr<void>(Transaction), Transaction, PPF_None);
    }
    
    // Set timeout property
    FProperty* TimeoutProperty = Transaction->GetClass()->FindPropertyByName(FName("TimeoutSeconds"));
    if (TimeoutProperty)
    {
        FString TimeoutStr = FString::SanitizeFloat(TimeoutSeconds);
        TimeoutProperty->ImportText_Direct(*TimeoutStr, TimeoutProperty->ContainerPtrToValuePtr<void>(Transaction), Transaction, PPF_None);
    }
    
    // Set unique ID for the transaction
    FProperty* IdProperty = Transaction->GetClass()->FindPropertyByName(FName("TransactionId"));
    if (IdProperty)
    {
        FGuid NewGuid = FGuid::NewGuid();
        IdProperty->ImportText_Direct(*NewGuid.ToString(), IdProperty->ContainerPtrToValuePtr<void>(Transaction), Transaction, PPF_None);
    }
    
    // Set timestamp property
    FProperty* TimestampProperty = Transaction->GetClass()->FindPropertyByName(FName("Timestamp"));
    if (TimestampProperty)
    {
        FDateTime Now = FDateTime::Now();
        TimestampProperty->ImportText_Direct(*Now.ToString(), TimestampProperty->ContainerPtrToValuePtr<void>(Transaction), Transaction, PPF_None);
    }
    
    // Call initialize method if available
    UFunction* InitFunc = Transaction->FindFunction(FName("Initialize"));
    if (InitFunc)
    {
        Transaction->ProcessEvent(InitFunc, nullptr);
    }
    
    return true;
}

bool UTransactionContextFactory::AddZonesToTransaction(UObject* Transaction, const TArray<UObject*>& Zones)
{
    if (!Transaction || Zones.Num() == 0)
    {
        return false;
    }
    
    // Find zones property (assuming it's an array)
    FProperty* ZonesArrayProperty = Transaction->GetClass()->FindPropertyByName(FName("Zones"));
    if (ZonesArrayProperty && ZonesArrayProperty->IsA<FArrayProperty>())
    {
        // Construct a string representation of the array
        FString ZonesStr = TEXT("(");
        for (int32 i = 0; i < Zones.Num(); ++i)
        {
            if (i > 0)
            {
                ZonesStr += TEXT(",");
            }
            ZonesStr += Zones[i]->GetPathName();
        }
        ZonesStr += TEXT(")");
        
        // Import the array
        ZonesArrayProperty->ImportText_Direct(*ZonesStr, ZonesArrayProperty->ContainerPtrToValuePtr<void>(Transaction), Transaction, PPF_None);
        return true;
    }
    
    // If no array property, look for AddZone function
    UFunction* AddZoneFunc = Transaction->FindFunction(FName("AddZone"));
    if (AddZoneFunc)
    {
        struct FFunctionParams
        {
            UObject* Zone;
        };
        
        FFunctionParams Params;
        
        // Add each zone individually
        for (UObject* Zone : Zones)
        {
            Params.Zone = Zone;
            Transaction->ProcessEvent(AddZoneFunc, &Params);
        }
        
        return true;
    }
    
    return false;
}

FName UTransactionContextFactory::GetTransactionPoolName(ETransactionType Type, ETransactionPriority Priority)
{
    // Generate a consistent pool name based on the transaction type and priority
    return FName(*FString::Printf(TEXT("Transaction_%d_%d"), (int32)Type, (int32)Priority));
}