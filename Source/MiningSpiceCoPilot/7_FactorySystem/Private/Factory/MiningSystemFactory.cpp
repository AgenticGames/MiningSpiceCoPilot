// MiningSystemFactory.cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factory/MiningSystemFactory.h"
#include "Interfaces/IComponentPoolManager.h"
#include "Factory/ComponentBuilder.h"
#include "Interfaces/IFactoryMetrics.h"

namespace
{
    // Singleton instance
    UMiningSystemFactory* GlobalFactoryInstance = nullptr;

    // Factory name
    const FName FactoryName = "MiningSystemFactory";
}

UMiningSystemFactory::UMiningSystemFactory()
    : bIsInitialized(false)
    , FactoryName(::FactoryName)
{
    // Store singleton reference
    if (!GlobalFactoryInstance)
    {
        GlobalFactoryInstance = this;
    }
}

bool UMiningSystemFactory::Initialize()
{
    if (bIsInitialized)
    {
        return true;
    }

    // Get component pool manager
    IComponentPoolManager* PoolManagerPtr = &IComponentPoolManager::Get();
    UObject* PoolManagerObject = Cast<UObject>(PoolManagerPtr);
    if (PoolManagerObject)
    {
        PoolManager.SetObject(PoolManagerObject);
        PoolManager.SetInterface(PoolManagerPtr);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("MiningSystemFactory: Failed to cast IComponentPoolManager to UObject"));
        return false;
    }

    // Initialize metrics tracking
    IFactoryMetrics::Get().TrackOperation(
        FactoryName,
        nullptr,
        EFactoryOperationType::Initialize, 
        0.0f);

    bIsInitialized = true;
    UE_LOG(LogTemp, Display, TEXT("MiningSystemFactory initialized"));
    return true;
}

void UMiningSystemFactory::Shutdown()
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
    UE_LOG(LogTemp, Display, TEXT("MiningSystemFactory shut down"));
}

bool UMiningSystemFactory::IsInitialized() const
{
    return bIsInitialized;
}

FName UMiningSystemFactory::GetFactoryName() const
{
    return FactoryName;
}

bool UMiningSystemFactory::SupportsType(UClass* ComponentType) const
{
    if (!ComponentType)
    {
        return false;
    }

    return SupportedTypes.Contains(ComponentType);
}

UObject* UMiningSystemFactory::CreateComponent(UClass* ComponentType, const TMap<FName, FString>& Parameters)
{
    if (!bIsInitialized || !ComponentType)
    {
        UE_LOG(LogTemp, Warning, TEXT("MiningSystemFactory: Cannot create component - factory not initialized or invalid component type"));
        return nullptr;
    }

    if (!SupportedTypes.Contains(ComponentType))
    {
        UE_LOG(LogTemp, Warning, TEXT("MiningSystemFactory: Component type not supported: %s"), *ComponentType->GetName());
        return nullptr;
    }

    // Start timing for metrics
    int32 MetricHandle = IFactoryMetrics::Get().BeginOperation(
        FactoryName, 
        ComponentType, 
        EFactoryOperationType::Create);

    // Check if we should use pooling
    UObject* Result = nullptr;
    bool bCacheMiss = false;
    
    if (HasPool(ComponentType))
    {
        // Try to get from pool
        Result = PoolManager->AllocateComponentByType(ComponentType);
        
        if (!Result)
        {
            // Create new instance if pool is empty
            Result = CreateComponentInstance(ComponentType, nullptr, NAME_None);
            bCacheMiss = true;
        }
    }
    else
    {
        // Create new instance without pooling
        Result = CreateComponentInstance(ComponentType, nullptr, NAME_None);
    }

    if (Result)
    {
        // Apply parameters
        ConfigureComponent(Result, Parameters);
    }

    // End metrics timing
    IFactoryMetrics::Get().EndOperation(MetricHandle, Result != nullptr, bCacheMiss);

    return Result;
}

TArray<UClass*> UMiningSystemFactory::GetSupportedTypes() const
{
    TArray<UClass*> Types;
    for (UClass* SupportedType : SupportedTypes)
    {
        Types.Add(SupportedType);
    }
    return Types;
}

bool UMiningSystemFactory::RegisterArchetype(UClass* ComponentType, UObject* Archetype)
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
    
    UE_LOG(LogTemp, Display, TEXT("MiningSystemFactory: Registered archetype for %s"), *ComponentType->GetName());
    return true;
}

bool UMiningSystemFactory::HasPool(UClass* ComponentType) const
{
    if (!bIsInitialized || !ComponentType || !PoolManager)
    {
        return false;
    }

    return PoolManager->HasPoolForType(ComponentType);
}

bool UMiningSystemFactory::CreatePool(UClass* ComponentType, int32 InitialSize, int32 MaxSize, bool bEnablePooling)
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
        UE_LOG(LogTemp, Display, TEXT("MiningSystemFactory: Created pool for %s (Initial: %d, Max: %d)"), 
            *ComponentType->GetName(), InitialSize, MaxSize);
    }
    
    return bSuccess;
}

bool UMiningSystemFactory::ReturnToPool(UObject* Component)
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

int32 UMiningSystemFactory::FlushPool(UClass* ComponentType)
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
    
    UE_LOG(LogTemp, Display, TEXT("MiningSystemFactory: Flushed pool for %s (%d instances removed)"), 
        *ComponentType->GetName(), Removed);
        
    return Removed;
}

bool UMiningSystemFactory::GetPoolStats(UClass* ComponentType, int32& OutAvailable, int32& OutTotal) const
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

bool UMiningSystemFactory::RegisterComponentType(UClass* ComponentType)
{
    if (!bIsInitialized || !ComponentType)
    {
        return false;
    }

    if (SupportedTypes.Contains(ComponentType))
    {
        // Already registered
        return true;
    }

    SupportedTypes.Add(ComponentType);
    UE_LOG(LogTemp, Display, TEXT("MiningSystemFactory: Registered component type: %s"), 
        *ComponentType->GetName());
    
    return true;
}

int32 UMiningSystemFactory::RegisterComponentTypes(const TArray<UClass*>& ComponentTypes)
{
    int32 SuccessCount = 0;
    
    for (UClass* ComponentType : ComponentTypes)
    {
        if (RegisterComponentType(ComponentType))
        {
            SuccessCount++;
        }
    }
    
    return SuccessCount;
}

bool UMiningSystemFactory::UnregisterComponentType(UClass* ComponentType)
{
    if (!bIsInitialized || !ComponentType)
    {
        return false;
    }

    if (!SupportedTypes.Contains(ComponentType))
    {
        // Not registered
        return false;
    }

    // Remove archetype if present
    if (Archetypes.Contains(ComponentType))
    {
        Archetypes.Remove(ComponentType);
    }
    
    // Remove from supported types
    SupportedTypes.Remove(ComponentType);
    
    UE_LOG(LogTemp, Display, TEXT("MiningSystemFactory: Unregistered component type: %s"), 
        *ComponentType->GetName());
    
    return true;
}

TSharedPtr<IComponentBuilder> UMiningSystemFactory::CreateBuilder(UClass* ComponentType, bool UsePooling)
{
    if (!bIsInitialized || !ComponentType)
    {
        return nullptr;
    }

    if (!SupportedTypes.Contains(ComponentType))
    {
        UE_LOG(LogTemp, Warning, TEXT("MiningSystemFactory: Cannot create builder - component type not supported: %s"), 
            *ComponentType->GetName());
        return nullptr;
    }

    // Create builder instance
    TSharedPtr<UComponentBuilder> Builder = UComponentBuilder::CreateBuilder(ComponentType, UsePooling);
    
    if (Builder.IsValid())
    {
        // Set pool manager
        Builder->SetPoolManager(PoolManager);
    }
    
    return Builder;
}

UObject* UMiningSystemFactory::CreateComponentInstance(UClass* ComponentType, UObject* Outer, FName Name)
{
    if (!ComponentType)
    {
        return nullptr;
    }

    // Check for archetype
    UObject* Archetype = Archetypes.FindRef(ComponentType);
    UObject* NewInstance = nullptr;
    
    if (Archetype)
    {
        // Create from archetype
        NewInstance = NewObject<UObject>(
            Outer ? Outer : GetTransientPackage(), 
            ComponentType, 
            Name, 
            RF_NoFlags, 
            Archetype);
    }
    else
    {
        // Create fresh instance
        NewInstance = NewObject<UObject>(
            Outer ? Outer : GetTransientPackage(), 
            ComponentType, 
            Name);
    }
    
    return NewInstance;
}

bool UMiningSystemFactory::ConfigureComponent(UObject* Component, const TMap<FName, FString>& Parameters)
{
    if (!Component)
    {
        return false;
    }

    // Apply each parameter
    for (const auto& Param : Parameters)
    {
        FProperty* Property = Component->GetClass()->FindPropertyByName(Param.Key);
        if (Property)
        {
            // Set property from string representation
            Property->ImportText_Direct(*Param.Value, Property->ContainerPtrToValuePtr<void>(Component), Component, PPF_None);
        }
    }
    
    return true;
}

UMiningSystemFactory* UMiningSystemFactory::Get()
{
    if (!GlobalFactoryInstance)
    {
        // Create instance if needed
        GlobalFactoryInstance = NewObject<UMiningSystemFactory>();
        GlobalFactoryInstance->AddToRoot(); // Prevent garbage collection
        GlobalFactoryInstance->Initialize();
    }
    
    return GlobalFactoryInstance;
}