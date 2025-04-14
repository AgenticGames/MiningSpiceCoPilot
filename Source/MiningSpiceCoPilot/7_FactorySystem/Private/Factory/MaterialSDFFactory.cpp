// MaterialSDFFactory.cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factory/MaterialSDFFactory.h"
#include "Interfaces/IComponentPoolManager.h"
#include "Interfaces/IFactoryMetrics.h"

namespace
{
    // Singleton instance
    UMaterialSDFFactory* GlobalFactoryInstance = nullptr;

    // Factory name
    const FName FactoryName = "MaterialSDFFactory";
}

UMaterialSDFFactory::UMaterialSDFFactory()
    : bIsInitialized(false)
    , FactoryName(::FactoryName)
{
    // Store singleton reference
    if (!GlobalFactoryInstance)
    {
        GlobalFactoryInstance = this;
    }
}

bool UMaterialSDFFactory::Initialize()
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
        UE_LOG(LogTemp, Error, TEXT("MaterialSDFFactory: Failed to cast IComponentPoolManager to UObject"));
        return false;
    }

    // Initialize metrics tracking
    IFactoryMetrics::Get().TrackOperation(
        FactoryName,
        nullptr,
        EFactoryOperationType::Initialize, 
        0.0f);

    bIsInitialized = true;
    UE_LOG(LogTemp, Display, TEXT("MaterialSDFFactory initialized"));
    return true;
}

void UMaterialSDFFactory::Shutdown()
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
    
    // Clear material configs
    MaterialConfigs.Empty();
    
    bIsInitialized = false;
    UE_LOG(LogTemp, Display, TEXT("MaterialSDFFactory shut down"));
}

bool UMaterialSDFFactory::IsInitialized() const
{
    return bIsInitialized;
}

FName UMaterialSDFFactory::GetFactoryName() const
{
    return FactoryName;
}

bool UMaterialSDFFactory::SupportsType(UClass* ComponentType) const
{
    if (!ComponentType)
    {
        return false;
    }

    return SupportedTypes.Contains(ComponentType);
}

UObject* UMaterialSDFFactory::CreateComponent(UClass* ComponentType, const TMap<FName, FString>& Parameters)
{
    if (!bIsInitialized || !ComponentType)
    {
        UE_LOG(LogTemp, Warning, TEXT("MaterialSDFFactory: Cannot create component - factory not initialized or invalid component type"));
        return nullptr;
    }

    if (!SupportedTypes.Contains(ComponentType))
    {
        UE_LOG(LogTemp, Warning, TEXT("MaterialSDFFactory: Component type not supported: %s"), *ComponentType->GetName());
        return nullptr;
    }

    // Start timing for metrics
    int32 MetricHandle = IFactoryMetrics::Get().BeginOperation(
        FactoryName, 
        ComponentType, 
        EFactoryOperationType::Create);

    // Parse material type parameter
    uint32 MaterialType = 0;
    if (Parameters.Contains(FName("MaterialType")))
    {
        MaterialType = FCString::Atoi(*Parameters.FindRef(FName("MaterialType")));
    }
    
    // Parse operation parameter
    EMiningCsgOperation Operation = EMiningCsgOperation::Union;
    if (Parameters.Contains(FName("Operation")))
    {
        FString OperationStr = Parameters.FindRef(FName("Operation"));
        if (OperationStr.Equals("Union", ESearchCase::IgnoreCase))
        {
            Operation = EMiningCsgOperation::Union;
        }
        else if (OperationStr.Equals("Subtraction", ESearchCase::IgnoreCase))
        {
            Operation = EMiningCsgOperation::Subtraction;
        }
        else if (OperationStr.Equals("Intersection", ESearchCase::IgnoreCase))
        {
            Operation = EMiningCsgOperation::Intersection;
        }
        else if (OperationStr.Equals("SmoothUnion", ESearchCase::IgnoreCase))
        {
            Operation = EMiningCsgOperation::SmoothUnion;
        }
        else if (OperationStr.Equals("SmoothSubtract", ESearchCase::IgnoreCase))
        {
            Operation = EMiningCsgOperation::SmoothSubtract;
        }
        else if (OperationStr.Equals("Replace", ESearchCase::IgnoreCase))
        {
            Operation = EMiningCsgOperation::Replace;
        }
    }
    
    // Parse resolution parameter
    FIntVector Resolution(32, 32, 32); // Default
    if (Parameters.Contains(FName("Resolution")))
    {
        FString ResolutionStr = Parameters.FindRef(FName("Resolution"));
        FIntVector::FParseStream(ResolutionStr) >> Resolution;
    }
    
    // Parse blend mode parameter
    EMaterialBlendMode BlendMode = EMaterialBlendMode::Smooth;
    if (Parameters.Contains(FName("BlendMode")))
    {
        FString BlendModeStr = Parameters.FindRef(FName("BlendMode"));
        if (BlendModeStr.Equals("Hard", ESearchCase::IgnoreCase))
        {
            BlendMode = EMaterialBlendMode::Hard;
        }
        else if (BlendModeStr.Equals("Smooth", ESearchCase::IgnoreCase))
        {
            BlendMode = EMaterialBlendMode::Smooth;
        }
        else if (BlendModeStr.Equals("Fractional", ESearchCase::IgnoreCase))
        {
            BlendMode = EMaterialBlendMode::Fractional;
        }
        else if (BlendModeStr.Equals("Layered", ESearchCase::IgnoreCase))
        {
            BlendMode = EMaterialBlendMode::Layered;
        }
    }

    // Try to find appropriate pool
    FName PoolName = GetMaterialPoolName(MaterialType, Operation);
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
        // Configure the material SDF
        ConfigureMaterialProperties(Result, MaterialType, Operation, BlendMode);
        
        // Apply remaining parameters
        for (const auto& Param : Parameters)
        {
            // Skip already processed parameters
            if (Param.Key == FName("MaterialType") || 
                Param.Key == FName("Operation") || 
                Param.Key == FName("Resolution") ||
                Param.Key == FName("BlendMode"))
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

TArray<UClass*> UMaterialSDFFactory::GetSupportedTypes() const
{
    TArray<UClass*> Types;
    for (UClass* SupportedType : SupportedTypes)
    {
        Types.Add(SupportedType);
    }
    return Types;
}

bool UMaterialSDFFactory::RegisterArchetype(UClass* ComponentType, UObject* Archetype)
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
    
    UE_LOG(LogTemp, Display, TEXT("MaterialSDFFactory: Registered archetype for %s"), *ComponentType->GetName());
    return true;
}

bool UMaterialSDFFactory::HasPool(UClass* ComponentType) const
{
    if (!bIsInitialized || !ComponentType || !PoolManager)
    {
        return false;
    }

    return PoolManager->HasPoolForType(ComponentType);
}

bool UMaterialSDFFactory::CreatePool(UClass* ComponentType, int32 InitialSize, int32 MaxSize, bool bEnablePooling)
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
        UE_LOG(LogTemp, Display, TEXT("MaterialSDFFactory: Created pool for %s (Initial: %d, Max: %d)"), 
            *ComponentType->GetName(), InitialSize, MaxSize);
    }
    
    return bSuccess;
}

bool UMaterialSDFFactory::ReturnToPool(UObject* Component)
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

int32 UMaterialSDFFactory::FlushPool(UClass* ComponentType)
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
    
    UE_LOG(LogTemp, Display, TEXT("MaterialSDFFactory: Flushed pool for %s (%d instances removed)"), 
        *ComponentType->GetName(), Removed);
        
    return Removed;
}

bool UMaterialSDFFactory::GetPoolStats(UClass* ComponentType, int32& OutAvailable, int32& OutTotal) const
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

UObject* UMaterialSDFFactory::CreateMaterialSDF(uint32 MaterialType, EMiningCsgOperation Operation, const FIntVector& Resolution, EMaterialBlendMode BlendMode)
{
    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Warning, TEXT("MaterialSDFFactory: Cannot create material SDF - factory not initialized"));
        return nullptr;
    }

    // Get the class to instantiate for material SDFs
    UClass* SDFClass = nullptr;
    for (UClass* Class : SupportedTypes)
    {
        if (Class->GetName().Contains(TEXT("MaterialSDF")) || 
            Class->GetName().Contains(TEXT("MaterialField")))
        {
            SDFClass = Class;
            break;
        }
    }
    
    if (!SDFClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("MaterialSDFFactory: No suitable class found for material SDF"));
        return nullptr;
    }

    // Start timing for metrics
    int32 MetricHandle = IFactoryMetrics::Get().BeginOperation(
        FactoryName, 
        SDFClass, 
        EFactoryOperationType::Create);

    // Try to find appropriate pool
    FName PoolName = GetMaterialPoolName(MaterialType, Operation);
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
            Result = NewObject<UObject>(GetTransientPackage(), SDFClass);
            bCacheMiss = true;
        }
    }
    else
    {
        // Create new instance without pooling
        Result = NewObject<UObject>(GetTransientPackage(), SDFClass);
    }

    if (Result)
    {
        // Configure the material SDF
        ConfigureMaterialProperties(Result, MaterialType, Operation, BlendMode);
    }

    // End metrics timing
    IFactoryMetrics::Get().EndOperation(MetricHandle, Result != nullptr, bCacheMiss);
    
    return Result;
}

UObject* UMaterialSDFFactory::CreateMultiMaterialSDF(const TArray<uint32>& MaterialTypes, const FIntVector& Resolution, EMaterialBlendMode BlendMode)
{
    if (!bIsInitialized || MaterialTypes.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("MaterialSDFFactory: Cannot create multi-material SDF - factory not initialized or no materials specified"));
        return nullptr;
    }

    // Get the class to instantiate for multi-material SDFs
    UClass* MultiSDFClass = nullptr;
    for (UClass* Class : SupportedTypes)
    {
        if (Class->GetName().Contains(TEXT("MultiMaterial")) || 
            Class->GetName().Contains(TEXT("MaterialComposite")))
        {
            MultiSDFClass = Class;
            break;
        }
    }
    
    if (!MultiSDFClass)
    {
        // Fall back to regular material SDF class
        for (UClass* Class : SupportedTypes)
        {
            if (Class->GetName().Contains(TEXT("MaterialSDF")) || 
                Class->GetName().Contains(TEXT("MaterialField")))
            {
                MultiSDFClass = Class;
                break;
            }
        }
    }
    
    if (!MultiSDFClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("MaterialSDFFactory: No suitable class found for multi-material SDF"));
        return nullptr;
    }

    // Start timing for metrics
    int32 MetricHandle = IFactoryMetrics::Get().BeginOperation(
        FactoryName, 
        MultiSDFClass, 
        EFactoryOperationType::Create);

    // Create new instance (multi-material instances are generally not pooled)
    UObject* Result = NewObject<UObject>(GetTransientPackage(), MultiSDFClass);

    if (Result)
    {
        // Configure base properties
        ConfigureBlending(Result, MaterialTypes, BlendMode);
    }

    // End metrics timing
    IFactoryMetrics::Get().EndOperation(MetricHandle, Result != nullptr, true);
    
    return Result;
}

void UMaterialSDFFactory::SetMaterialPropertyProvider(TScriptInterface<IMaterialPropertyProvider> InPropertyProvider)
{
    MaterialPropertyProvider = InPropertyProvider;
}

UMaterialSDFFactory* UMaterialSDFFactory::Get()
{
    if (!GlobalFactoryInstance)
    {
        // Create instance if needed
        GlobalFactoryInstance = NewObject<UMaterialSDFFactory>();
        GlobalFactoryInstance->AddToRoot(); // Prevent garbage collection
        GlobalFactoryInstance->Initialize();
    }
    
    return GlobalFactoryInstance;
}

bool UMaterialSDFFactory::ConfigureMaterialProperties(UObject* Component, uint32 MaterialType, EMiningCsgOperation Operation, EMaterialBlendMode BlendMode)
{
    if (!Component)
    {
        return false;
    }

    // Set material type property
    FProperty* MaterialTypeProperty = Component->GetClass()->FindPropertyByName(FName("MaterialType"));
    if (MaterialTypeProperty)
    {
        FString MaterialTypeStr = FString::FromInt(MaterialType);
        MaterialTypeProperty->ImportText_Direct(*MaterialTypeStr, MaterialTypeProperty->ContainerPtrToValuePtr<void>(Component), Component, PPF_None);
    }
    
    // Set operation property
    FProperty* OperationProperty = Component->GetClass()->FindPropertyByName(FName("Operation"));
    if (OperationProperty)
    {
        FString OperationStr = FString::FromInt((int32)Operation);
        OperationProperty->ImportText_Direct(*OperationStr, OperationProperty->ContainerPtrToValuePtr<void>(Component), Component, PPF_None);
    }
    
    // Set blend mode property
    FProperty* BlendModeProperty = Component->GetClass()->FindPropertyByName(FName("BlendMode"));
    if (BlendModeProperty)
    {
        FString BlendModeStr = FString::FromInt((int32)BlendMode);
        BlendModeProperty->ImportText_Direct(*BlendModeStr, BlendModeProperty->ContainerPtrToValuePtr<void>(Component), Component, PPF_None);
    }
    
    // Apply material configuration if available
    if (MaterialConfigs.Contains(MaterialType))
    {
        const FMaterialSDFConfig& Config = MaterialConfigs[MaterialType];
        
        // Set blend radius
        FProperty* BlendRadiusProperty = Component->GetClass()->FindPropertyByName(FName("BlendRadius"));
        if (BlendRadiusProperty)
        {
            FString BlendRadiusStr = FString::SanitizeFloat(Config.DefaultBlendRadius);
            BlendRadiusProperty->ImportText_Direct(*BlendRadiusStr, BlendRadiusProperty->ContainerPtrToValuePtr<void>(Component), Component, PPF_None);
        }
    }
    
    // Call initialize method if available
    UFunction* InitFunc = Component->FindFunction(FName("Initialize"));
    if (InitFunc)
    {
        Component->ProcessEvent(InitFunc, nullptr);
    }
    
    return true;
}

bool UMaterialSDFFactory::ConfigureBlending(UObject* Component, const TArray<uint32>& MaterialTypes, EMaterialBlendMode BlendMode)
{
    if (!Component)
    {
        return false;
    }

    // Set material types array property
    FProperty* MaterialTypesProperty = Component->GetClass()->FindPropertyByName(FName("MaterialTypes"));
    if (MaterialTypesProperty)
    {
        FString MaterialTypesStr = TEXT("(");
        for (int32 i = 0; i < MaterialTypes.Num(); ++i)
        {
            if (i > 0)
            {
                MaterialTypesStr += TEXT(",");
            }
            MaterialTypesStr += FString::FromInt(MaterialTypes[i]);
        }
        MaterialTypesStr += TEXT(")");
        
        MaterialTypesProperty->ImportText_Direct(*MaterialTypesStr, MaterialTypesProperty->ContainerPtrToValuePtr<void>(Component), Component, PPF_None);
    }
    
    // Set blend mode property
    FProperty* BlendModeProperty = Component->GetClass()->FindPropertyByName(FName("BlendMode"));
    if (BlendModeProperty)
    {
        FString BlendModeStr = FString::FromInt((int32)BlendMode);
        BlendModeProperty->ImportText_Direct(*BlendModeStr, BlendModeProperty->ContainerPtrToValuePtr<void>(Component), Component, PPF_None);
    }
    
    // Set material count property
    FProperty* MaterialCountProperty = Component->GetClass()->FindPropertyByName(FName("MaterialCount"));
    if (MaterialCountProperty)
    {
        FString CountStr = FString::FromInt(MaterialTypes.Num());
        MaterialCountProperty->ImportText_Direct(*CountStr, MaterialCountProperty->ContainerPtrToValuePtr<void>(Component), Component, PPF_None);
    }
    
    // Call initialize method if available
    UFunction* InitFunc = Component->FindFunction(FName("Initialize"));
    if (InitFunc)
    {
        Component->ProcessEvent(InitFunc, nullptr);
    }
    
    return true;
}

FName UMaterialSDFFactory::GetMaterialPoolName(uint32 MaterialType, EMiningCsgOperation Operation)
{
    // Generate a consistent pool name based on the parameters
    return FName(*FString::Printf(TEXT("MaterialSDF_%d_Op%d"), MaterialType, (int32)Operation));
}