// DistanceFieldFactory.cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factory/DistanceFieldFactory.h"
#include "Interfaces/IComponentPoolManager.h"
#include "Interfaces/IFactoryMetrics.h"

namespace
{
    // Singleton instance
    UDistanceFieldFactory* GlobalFactoryInstance = nullptr;

    // Factory name
    const FName FactoryName = "DistanceFieldFactory";
}

UDistanceFieldFactory::UDistanceFieldFactory()
    : bIsInitialized(false)
    , FactoryName(::FactoryName)
{
    // Store singleton reference
    if (!GlobalFactoryInstance)
    {
        GlobalFactoryInstance = this;
    }
}

bool UDistanceFieldFactory::Initialize()
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
        UE_LOG(LogTemp, Error, TEXT("DistanceFieldFactory: Failed to cast IComponentPoolManager to UObject"));
        return false;
    }

    // Initialize metrics tracking
    IFactoryMetrics::Get().TrackOperation(
        FactoryName,
        nullptr,
        EFactoryOperationType::Initialize, 
        0.0f);

    bIsInitialized = true;
    UE_LOG(LogTemp, Display, TEXT("DistanceFieldFactory initialized"));
    return true;
}

void UDistanceFieldFactory::Shutdown()
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
    UE_LOG(LogTemp, Display, TEXT("DistanceFieldFactory shut down"));
}

bool UDistanceFieldFactory::IsInitialized() const
{
    return bIsInitialized;
}

FName UDistanceFieldFactory::GetFactoryName() const
{
    return FactoryName;
}

bool UDistanceFieldFactory::SupportsType(UClass* ComponentType) const
{
    if (!ComponentType)
    {
        return false;
    }

    return SupportedTypes.Contains(ComponentType);
}

UObject* UDistanceFieldFactory::CreateComponent(UClass* ComponentType, const TMap<FName, FString>& Parameters)
{
    if (!bIsInitialized || !ComponentType)
    {
        UE_LOG(LogTemp, Warning, TEXT("DistanceFieldFactory: Cannot create component - factory not initialized or invalid component type"));
        return nullptr;
    }

    if (!SupportedTypes.Contains(ComponentType))
    {
        UE_LOG(LogTemp, Warning, TEXT("DistanceFieldFactory: Component type not supported: %s"), *ComponentType->GetName());
        return nullptr;
    }

    // Start timing for metrics
    int32 MetricHandle = IFactoryMetrics::Get().BeginOperation(
        FactoryName, 
        ComponentType, 
        EFactoryOperationType::Create);

    // Parse resolution parameter
    FIntVector Resolution(32, 32, 32); // Default
    if (Parameters.Contains(FName("Resolution")))
    {
        FString ResolutionStr = Parameters.FindRef(FName("Resolution"));
        // Parse the string into an FIntVector
        TArray<FString> Components;
        ResolutionStr.ParseIntoArray(Components, TEXT(","), true);
        if (Components.Num() >= 3)
        {
            Resolution.X = FCString::Atoi(*Components[0]);
            Resolution.Y = FCString::Atoi(*Components[1]);
            Resolution.Z = FCString::Atoi(*Components[2]);
        }
    }
    
    // Parse materials parameter
    int32 MaterialChannels = 1; // Default
    if (Parameters.Contains(FName("MaterialChannels")))
    {
        MaterialChannels = FCString::Atoi(*Parameters.FindRef(FName("MaterialChannels")));
    }
    
    // Parse narrow band parameter
    float NarrowBandWidth = 4.0f; // Default
    if (Parameters.Contains(FName("NarrowBandWidth")))
    {
        NarrowBandWidth = FCString::Atof(*Parameters.FindRef(FName("NarrowBandWidth")));
    }
    
    // Parse precision parameter
    EFieldPrecision Precision = EFieldPrecision::Medium; // Default
    if (Parameters.Contains(FName("Precision")))
    {
        FString PrecisionStr = Parameters.FindRef(FName("Precision"));
        if (PrecisionStr.Equals("Low", ESearchCase::IgnoreCase))
        {
            Precision = EFieldPrecision::Low;
        }
        else if (PrecisionStr.Equals("Medium", ESearchCase::IgnoreCase))
        {
            Precision = EFieldPrecision::Medium;
        }
        else if (PrecisionStr.Equals("High", ESearchCase::IgnoreCase))
        {
            Precision = EFieldPrecision::High;
        }
        else if (PrecisionStr.Equals("Double", ESearchCase::IgnoreCase))
        {
            Precision = EFieldPrecision::Double;
        }
    }

    // Try to find appropriate pool
    FName PoolName = GetFieldPoolName(Resolution, MaterialChannels, Precision);
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
        // Configure the field
        ConfigureField(Result, Resolution, MaterialChannels, NarrowBandWidth, Precision);
        
        // Apply remaining parameters
        for (const auto& Param : Parameters)
        {
            // Skip already processed parameters
            if (Param.Key == FName("Resolution") || 
                Param.Key == FName("MaterialChannels") || 
                Param.Key == FName("NarrowBandWidth") ||
                Param.Key == FName("Precision"))
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

TArray<UClass*> UDistanceFieldFactory::GetSupportedTypes() const
{
    TArray<UClass*> Types;
    for (UClass* SupportedType : SupportedTypes)
    {
        Types.Add(SupportedType);
    }
    return Types;
}

bool UDistanceFieldFactory::RegisterArchetype(UClass* ComponentType, UObject* Archetype)
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
    
    UE_LOG(LogTemp, Display, TEXT("DistanceFieldFactory: Registered archetype for %s"), *ComponentType->GetName());
    return true;
}

bool UDistanceFieldFactory::HasPool(UClass* ComponentType) const
{
    if (!bIsInitialized || !ComponentType || !PoolManager)
    {
        return false;
    }

    return PoolManager->HasPoolForType(ComponentType);
}

bool UDistanceFieldFactory::CreatePool(UClass* ComponentType, int32 InitialSize, int32 MaxSize, bool bEnablePooling)
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
        UE_LOG(LogTemp, Display, TEXT("DistanceFieldFactory: Created pool for %s (Initial: %d, Max: %d)"), 
            *ComponentType->GetName(), InitialSize, MaxSize);
    }
    
    return bSuccess;
}

bool UDistanceFieldFactory::ReturnToPool(UObject* Component)
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

int32 UDistanceFieldFactory::FlushPool(UClass* ComponentType)
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
    
    UE_LOG(LogTemp, Display, TEXT("DistanceFieldFactory: Flushed pool for %s (%d instances removed)"), 
        *ComponentType->GetName(), Removed);
        
    return Removed;
}

bool UDistanceFieldFactory::GetPoolStats(UClass* ComponentType, int32& OutAvailable, int32& OutTotal) const
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

UObject* UDistanceFieldFactory::CreateDistanceField(const FIntVector& Resolution, int32 MaterialChannels, float NarrowBandWidth, EFieldPrecision Precision)
{
    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Warning, TEXT("DistanceFieldFactory: Cannot create distance field - factory not initialized"));
        return nullptr;
    }

    // Get the class to instantiate for distance fields
    UClass* FieldClass = nullptr;
    for (UClass* Class : SupportedTypes)
    {
        if (Class->GetName().Contains(TEXT("DistanceField")) || 
            Class->GetName().Contains(TEXT("SDF")))
        {
            FieldClass = Class;
            break;
        }
    }
    
    if (!FieldClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("DistanceFieldFactory: No suitable class found for distance field"));
        return nullptr;
    }

    // Start timing for metrics
    int32 MetricHandle = IFactoryMetrics::Get().BeginOperation(
        FactoryName, 
        FieldClass, 
        EFactoryOperationType::Create);

    // Try to find appropriate pool
    FName PoolName = GetFieldPoolName(Resolution, MaterialChannels, Precision);
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
            Result = NewObject<UObject>(GetTransientPackage(), FieldClass);
            bCacheMiss = true;
        }
    }
    else
    {
        // Create new instance without pooling
        Result = NewObject<UObject>(GetTransientPackage(), FieldClass);
    }

    if (Result)
    {
        // Configure the field
        ConfigureField(Result, Resolution, MaterialChannels, NarrowBandWidth, Precision);
    }

    // End metrics timing
    IFactoryMetrics::Get().EndOperation(MetricHandle, Result != nullptr, bCacheMiss);
    
    return Result;
}

UObject* UDistanceFieldFactory::CreateDistanceFieldFromMesh(UStaticMesh* Mesh, const FIntVector& Resolution, int32 MaterialIndex, EFieldPrecision Precision)
{
    if (!bIsInitialized || !Mesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("DistanceFieldFactory: Cannot create distance field from mesh - factory not initialized or invalid mesh"));
        return nullptr;
    }

    // Create the base distance field
    UObject* Field = CreateDistanceField(Resolution, 1, 4.0f, Precision);
    
    if (!Field)
    {
        return nullptr;
    }

    // Set the source mesh property
    FProperty* MeshProperty = Field->GetClass()->FindPropertyByName(FName("SourceMesh"));
    if (MeshProperty)
    {
        MeshProperty->ImportText_Direct(*Mesh->GetPathName(), MeshProperty->ContainerPtrToValuePtr<void>(Field), Field, PPF_None);
    }
    
    // Set material index property
    FProperty* MaterialIndexProperty = Field->GetClass()->FindPropertyByName(FName("MaterialIndex"));
    if (MaterialIndexProperty)
    {
        FString MaterialIndexStr = FString::FromInt(MaterialIndex);
        MaterialIndexProperty->ImportText_Direct(*MaterialIndexStr, MaterialIndexProperty->ContainerPtrToValuePtr<void>(Field), Field, PPF_None);
    }
    
    // Call generate field method if available
    UFunction* GenerateFunc = Field->FindFunction(FName("GenerateFromMesh"));
    if (GenerateFunc)
    {
        Field->ProcessEvent(GenerateFunc, nullptr);
    }
    
    return Field;
}

UDistanceFieldFactory* UDistanceFieldFactory::Get()
{
    if (!GlobalFactoryInstance)
    {
        // Create instance if needed
        GlobalFactoryInstance = NewObject<UDistanceFieldFactory>();
        GlobalFactoryInstance->AddToRoot(); // Prevent garbage collection
        GlobalFactoryInstance->Initialize();
    }
    
    return GlobalFactoryInstance;
}

bool UDistanceFieldFactory::ConfigureField(
    UObject* Field, 
    const FIntVector& Resolution, 
    int32 MaterialChannels, 
    float NarrowBandWidth,
    EFieldPrecision Precision)
{
    if (!Field)
    {
        return false;
    }

    // Set resolution property
    FProperty* ResolutionProperty = Field->GetClass()->FindPropertyByName(FName("Resolution"));
    if (ResolutionProperty)
    {
        ResolutionProperty->ImportText_Direct(*Resolution.ToString(), ResolutionProperty->ContainerPtrToValuePtr<void>(Field), Field, PPF_None);
    }
    
    // Set material channels property
    FProperty* MaterialChannelsProperty = Field->GetClass()->FindPropertyByName(FName("MaterialChannels"));
    if (MaterialChannelsProperty)
    {
        FString MaterialChannelsStr = FString::FromInt(MaterialChannels);
        MaterialChannelsProperty->ImportText_Direct(*MaterialChannelsStr, MaterialChannelsProperty->ContainerPtrToValuePtr<void>(Field), Field, PPF_None);
    }
    
    // Set narrow band width property
    FProperty* NarrowBandProperty = Field->GetClass()->FindPropertyByName(FName("NarrowBandWidth"));
    if (NarrowBandProperty)
    {
        FString NarrowBandStr = FString::SanitizeFloat(NarrowBandWidth);
        NarrowBandProperty->ImportText_Direct(*NarrowBandStr, NarrowBandProperty->ContainerPtrToValuePtr<void>(Field), Field, PPF_None);
    }
    
    // Set precision property
    FProperty* PrecisionProperty = Field->GetClass()->FindPropertyByName(FName("Precision"));
    if (PrecisionProperty)
    {
        FString PrecisionStr = FString::FromInt((int32)Precision);
        PrecisionProperty->ImportText_Direct(*PrecisionStr, PrecisionProperty->ContainerPtrToValuePtr<void>(Field), Field, PPF_None);
    }
    
    // Calculate and allocate memory
    int64 MemorySize = CalculateOptimalMemoryAllocation(Resolution, MaterialChannels, NarrowBandWidth, Precision);
    
    // Allocate memory if there's a function for it
    UFunction* AllocateFunc = Field->FindFunction(FName("AllocateMemory"));
    if (AllocateFunc)
    {
        struct { int64 Size; } Params;
        Params.Size = MemorySize;
        Field->ProcessEvent(AllocateFunc, &Params);
    }
    
    return true;
}

int64 UDistanceFieldFactory::CalculateOptimalMemoryAllocation(
    const FIntVector& Resolution, 
    int32 MaterialChannels, 
    float NarrowBandWidth,
    EFieldPrecision Precision)
{
    // Calculate optimal memory allocation based on resolution, material channels, and precision
    // For narrow-band optimization, we only need to allocate memory for voxels near the surface
    
    // Calculate total number of voxels
    int64 TotalVoxels = (int64)Resolution.X * (int64)Resolution.Y * (int64)Resolution.Z;
    
    // Calculate approximate narrow-band voxel count
    // Assume a surface-to-volume ratio based on a simple sphere
    double SurfaceToVolumeRatio = 3.0 / (Resolution.GetMin() / 2.0);
    
    // Narrow band is approximately the surface plus a band of width NarrowBandWidth
    double NarrowBandRatio = SurfaceToVolumeRatio * NarrowBandWidth;
    
    // Ensure the ratio is reasonable (not less than 5% or more than 50% of total volume)
    NarrowBandRatio = FMath::Clamp(NarrowBandRatio, 0.05, 0.5);
    
    // Calculate the number of voxels in the narrow band
    int64 NarrowBandVoxels = (int64)(TotalVoxels * NarrowBandRatio);
    
    // Calculate bytes per voxel based on precision
    int64 BytesPerVoxel = 0;
    switch (Precision)
    {
    case EFieldPrecision::Low:
        BytesPerVoxel = 1; // 8-bit
        break;
    case EFieldPrecision::Medium:
        BytesPerVoxel = 2; // 16-bit
        break;
    case EFieldPrecision::High:
        BytesPerVoxel = 4; // 32-bit
        break;
    case EFieldPrecision::Double:
        BytesPerVoxel = 8; // 64-bit
        break;
    }
    
    // Multiply by number of channels
    BytesPerVoxel *= MaterialChannels;
    
    // Add overhead for spatial indexing and gradient information
    // Gradients add 3 values per voxel for directions
    int64 GradientSize = NarrowBandVoxels * 3 * (BytesPerVoxel / MaterialChannels);
    
    // Spatial indexing overhead
    int64 IndexSize = NarrowBandVoxels * 4; // Typically 4 bytes per voxel for indexing
    
    // Total memory size
    int64 TotalMemory = (NarrowBandVoxels * BytesPerVoxel) + GradientSize + IndexSize;
    
    // Add buffer for safety (20%)
    TotalMemory = (TotalMemory * 120) / 100;
    
    // Store config for reference
    FFieldPoolConfig Config;
    Config.PoolName = GetFieldPoolName(Resolution, MaterialChannels, Precision);
    Config.Resolution = Resolution;
    Config.MaterialChannels = MaterialChannels;
    Config.NarrowBandWidth = NarrowBandWidth;
    Config.Precision = Precision;
    Config.MemoryPerField = TotalMemory;
    
    // Store in map for future reference
    FieldPoolConfigs.Add(Resolution, Config);
    
    return TotalMemory;
}

FName UDistanceFieldFactory::GetFieldPoolName(
    const FIntVector& Resolution, 
    int32 MaterialChannels, 
    EFieldPrecision Precision)
{
    // Generate a consistent pool name based on the parameters
    return FName(*FString::Printf(TEXT("DF_%d_%d_%d_M%d_P%d"), 
        Resolution.X, Resolution.Y, Resolution.Z, 
        MaterialChannels, 
        (int32)Precision));
}