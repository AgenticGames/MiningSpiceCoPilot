// ZoneFactory.cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factory/ZoneFactory.h"
#include "Interfaces/IComponentPoolManager.h"
#include "Interfaces/IFactoryMetrics.h"

namespace
{
    // Singleton instance
    UZoneFactory* GlobalFactoryInstance = nullptr;

    // Factory name
    const FName FactoryName = "ZoneFactory";
}

UZoneFactory::UZoneFactory()
    : bIsInitialized(false)
    , FactoryName(::FactoryName)
{
    // Store singleton reference
    if (!GlobalFactoryInstance)
    {
        GlobalFactoryInstance = this;
    }
}

bool UZoneFactory::Initialize()
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
    UE_LOG(LogTemp, Display, TEXT("ZoneFactory initialized"));
    return true;
}

void UZoneFactory::Shutdown()
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
    UE_LOG(LogTemp, Display, TEXT("ZoneFactory shut down"));
}

bool UZoneFactory::IsInitialized() const
{
    return bIsInitialized;
}

FName UZoneFactory::GetFactoryName() const
{
    return FactoryName;
}

bool UZoneFactory::SupportsType(UClass* ComponentType) const
{
    if (!ComponentType)
    {
        return false;
    }

    return SupportedTypes.Contains(ComponentType);
}

UObject* UZoneFactory::CreateComponent(UClass* ComponentType, const TMap<FName, FString>& Parameters)
{
    if (!bIsInitialized || !ComponentType)
    {
        UE_LOG(LogTemp, Warning, TEXT("ZoneFactory: Cannot create component - factory not initialized or invalid component type"));
        return nullptr;
    }

    if (!SupportedTypes.Contains(ComponentType))
    {
        UE_LOG(LogTemp, Warning, TEXT("ZoneFactory: Component type not supported: %s"), *ComponentType->GetName());
        return nullptr;
    }

    // Start timing for metrics
    int32 MetricHandle = IFactoryMetrics::Get().BeginOperation(
        FactoryName, 
        ComponentType, 
        EFactoryOperationType::Create);

    // Parse bounds parameter
    FBox ZoneBounds(FVector::ZeroVector, FVector(100.0f, 100.0f, 100.0f)); // Default
    if (Parameters.Contains(FName("Bounds")))
    {
        FString BoundsStr = Parameters.FindRef(FName("Bounds"));
        FBox::FParseStream(BoundsStr) >> ZoneBounds;
    }
    
    // Parse zone type parameter
    EZoneType ZoneType = EZoneType::Standard;
    if (Parameters.Contains(FName("ZoneType")))
    {
        FString ZoneTypeStr = Parameters.FindRef(FName("ZoneType"));
        if (ZoneTypeStr.Equals("Standard", ESearchCase::IgnoreCase))
        {
            ZoneType = EZoneType::Standard;
        }
        else if (ZoneTypeStr.Equals("HighResolution", ESearchCase::IgnoreCase))
        {
            ZoneType = EZoneType::HighResolution;
        }
        else if (ZoneTypeStr.Equals("LowResolution", ESearchCase::IgnoreCase))
        {
            ZoneType = EZoneType::LowResolution;
        }
        else if (ZoneTypeStr.Equals("Transition", ESearchCase::IgnoreCase))
        {
            ZoneType = EZoneType::Transition;
        }
        else if (ZoneTypeStr.Equals("Buffer", ESearchCase::IgnoreCase))
        {
            ZoneType = EZoneType::Buffer;
        }
    }
    
    // Parse resolution parameter
    int32 Resolution = 32; // Default
    if (Parameters.Contains(FName("Resolution")))
    {
        Resolution = FCString::Atoi(*Parameters.FindRef(FName("Resolution")));
    }
    
    // Parse concurrency parameter
    EZoneConcurrencyMode ConcurrencyMode = EZoneConcurrencyMode::Optimistic;
    if (Parameters.Contains(FName("ConcurrencyMode")))
    {
        FString ConcurrencyModeStr = Parameters.FindRef(FName("ConcurrencyMode"));
        if (ConcurrencyModeStr.Equals("Exclusive", ESearchCase::IgnoreCase))
        {
            ConcurrencyMode = EZoneConcurrencyMode::Exclusive;
        }
        else if (ConcurrencyModeStr.Equals("Optimistic", ESearchCase::IgnoreCase))
        {
            ConcurrencyMode = EZoneConcurrencyMode::Optimistic;
        }
        else if (ConcurrencyModeStr.Equals("ReadOnly", ESearchCase::IgnoreCase))
        {
            ConcurrencyMode = EZoneConcurrencyMode::ReadOnly;
        }
    }

    // Try to find appropriate pool based on zone type
    FName PoolName = GetZonePoolName(ZoneType);
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
        // Configure the zone
        ConfigureZone(Result, ZoneBounds, ZoneType, Resolution, ConcurrencyMode);
        
        // Apply remaining parameters
        for (const auto& Param : Parameters)
        {
            // Skip already processed parameters
            if (Param.Key == FName("Bounds") || 
                Param.Key == FName("ZoneType") || 
                Param.Key == FName("Resolution") ||
                Param.Key == FName("ConcurrencyMode"))
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

TArray<UClass*> UZoneFactory::GetSupportedTypes() const
{
    TArray<UClass*> Types;
    SupportedTypes.GetKeys(Types);
    return Types;
}

bool UZoneFactory::RegisterArchetype(UClass* ComponentType, UObject* Archetype)
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
    
    UE_LOG(LogTemp, Display, TEXT("ZoneFactory: Registered archetype for %s"), *ComponentType->GetName());
    return true;
}

bool UZoneFactory::HasPool(UClass* ComponentType) const
{
    if (!bIsInitialized || !ComponentType || !PoolManager)
    {
        return false;
    }

    return PoolManager->HasPoolForType(ComponentType);
}

bool UZoneFactory::CreatePool(UClass* ComponentType, int32 InitialSize, int32 MaxSize, bool bEnablePooling)
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
        UE_LOG(LogTemp, Display, TEXT("ZoneFactory: Created pool for %s (Initial: %d, Max: %d)"), 
            *ComponentType->GetName(), InitialSize, MaxSize);
    }
    
    return bSuccess;
}

bool UZoneFactory::ReturnToPool(UObject* Component)
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

int32 UZoneFactory::FlushPool(UClass* ComponentType)
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
    
    UE_LOG(LogTemp, Display, TEXT("ZoneFactory: Flushed pool for %s (%d instances removed)"), 
        *ComponentType->GetName(), Removed);
        
    return Removed;
}

bool UZoneFactory::GetPoolStats(UClass* ComponentType, int32& OutAvailable, int32& OutTotal) const
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

UObject* UZoneFactory::CreateZone(const FBox& Bounds, EZoneType ZoneType, int32 Resolution, EZoneConcurrencyMode ConcurrencyMode)
{
    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Warning, TEXT("ZoneFactory: Cannot create zone - factory not initialized"));
        return nullptr;
    }

    // Get the class to instantiate for zones
    UClass* ZoneClass = nullptr;
    for (UClass* Class : SupportedTypes)
    {
        if (Class->GetName().Contains(TEXT("Zone")))
        {
            ZoneClass = Class;
            break;
        }
    }
    
    if (!ZoneClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("ZoneFactory: No suitable class found for zone"));
        return nullptr;
    }

    // Start timing for metrics
    int32 MetricHandle = IFactoryMetrics::Get().BeginOperation(
        FactoryName, 
        ZoneClass, 
        EFactoryOperationType::Create);

    // Try to find appropriate pool
    FName PoolName = GetZonePoolName(ZoneType);
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
            Result = NewObject<UObject>(GetTransientPackage(), ZoneClass);
            bCacheMiss = true;
        }
    }
    else
    {
        // Create new instance without pooling
        Result = NewObject<UObject>(GetTransientPackage(), ZoneClass);
    }

    if (Result)
    {
        // Configure the zone
        ConfigureZone(Result, Bounds, ZoneType, Resolution, ConcurrencyMode);
    }

    // End metrics timing
    IFactoryMetrics::Get().EndOperation(MetricHandle, Result != nullptr, bCacheMiss);
    
    return Result;
}

TArray<UObject*> UZoneFactory::CreateZoneGrid(const FBox& TotalBounds, int32 DivisionsX, int32 DivisionsY, int32 DivisionsZ, EZoneType ZoneType)
{
    TArray<UObject*> Results;
    
    if (!bIsInitialized || DivisionsX <= 0 || DivisionsY <= 0 || DivisionsZ <= 0)
    {
        return Results;
    }

    // Calculate zone size
    FVector TotalSize = TotalBounds.Max - TotalBounds.Min;
    FVector ZoneSize = FVector(
        TotalSize.X / DivisionsX,
        TotalSize.Y / DivisionsY,
        TotalSize.Z / DivisionsZ
    );
    
    // Calculate default resolution based on zone size
    int32 Resolution = FMath::Max(16, FMath::RoundToInt(FMath::Max3(ZoneSize.X, ZoneSize.Y, ZoneSize.Z) / 4.0f));
    
    // Pre-allocate array for results
    int32 TotalZones = DivisionsX * DivisionsY * DivisionsZ;
    Results.Reserve(TotalZones);
    
    // Create all zones
    for (int32 x = 0; x < DivisionsX; ++x)
    {
        for (int32 y = 0; y < DivisionsY; ++y)
        {
            for (int32 z = 0; z < DivisionsZ; ++z)
            {
                // Calculate this zone's bounds
                FVector MinPoint = TotalBounds.Min + FVector(
                    x * ZoneSize.X,
                    y * ZoneSize.Y,
                    z * ZoneSize.Z
                );
                
                FVector MaxPoint = MinPoint + ZoneSize;
                
                FBox ZoneBounds(MinPoint, MaxPoint);
                
                // Determine zone type - use transition zones at boundaries
                EZoneType ThisZoneType = ZoneType;
                if (x == 0 || x == DivisionsX - 1 || 
                    y == 0 || y == DivisionsY - 1 || 
                    z == 0 || z == DivisionsZ - 1)
                {
                    ThisZoneType = EZoneType::Transition;
                }
                
                // Create the zone
                UObject* Zone = CreateZone(
                    ZoneBounds, 
                    ThisZoneType, 
                    Resolution, 
                    EZoneConcurrencyMode::Optimistic
                );
                
                if (Zone)
                {
                    // Configure grid coordinates
                    ConfigureZoneGridCoordinates(Zone, x, y, z);
                    
                    Results.Add(Zone);
                }
            }
        }
    }
    
    // Link neighboring zones if supported
    if (Results.Num() > 0 && SupportedLinkingBetweenZones(Results[0]))
    {
        for (int32 x = 0; x < DivisionsX; ++x)
        {
            for (int32 y = 0; y < DivisionsY; ++y)
            {
                for (int32 z = 0; z < DivisionsZ; ++z)
                {
                    int32 Index = (x * DivisionsY * DivisionsZ) + (y * DivisionsZ) + z;
                    if (Index < Results.Num())
                    {
                        LinkZoneToNeighbors(
                            Results[Index], 
                            Results, 
                            x, y, z, 
                            DivisionsX, DivisionsY, DivisionsZ
                        );
                    }
                }
            }
        }
    }
    
    return Results;
}

UZoneFactory* UZoneFactory::Get()
{
    if (!GlobalFactoryInstance)
    {
        // Create instance if needed
        GlobalFactoryInstance = NewObject<UZoneFactory>();
        GlobalFactoryInstance->AddToRoot(); // Prevent garbage collection
        GlobalFactoryInstance->Initialize();
    }
    
    return GlobalFactoryInstance;
}

bool UZoneFactory::ConfigureZone(UObject* Zone, const FBox& Bounds, EZoneType ZoneType, int32 Resolution, EZoneConcurrencyMode ConcurrencyMode)
{
    if (!Zone)
    {
        return false;
    }

    // Set bounds property
    FProperty* BoundsProperty = Zone->GetClass()->FindPropertyByName(FName("Bounds"));
    if (BoundsProperty)
    {
        BoundsProperty->ImportText_Direct(*Bounds.ToString(), BoundsProperty->ContainerPtrToValuePtr<void>(Zone), Zone, PPF_None);
    }
    
    // Set zone type property
    FProperty* ZoneTypeProperty = Zone->GetClass()->FindPropertyByName(FName("ZoneType"));
    if (ZoneTypeProperty)
    {
        FString ZoneTypeStr = FString::FromInt((int32)ZoneType);
        ZoneTypeProperty->ImportText_Direct(*ZoneTypeStr, ZoneTypeProperty->ContainerPtrToValuePtr<void>(Zone), Zone, PPF_None);
    }
    
    // Set resolution property
    FProperty* ResolutionProperty = Zone->GetClass()->FindPropertyByName(FName("Resolution"));
    if (ResolutionProperty)
    {
        FString ResolutionStr = FString::FromInt(Resolution);
        ResolutionProperty->ImportText_Direct(*ResolutionStr, ResolutionProperty->ContainerPtrToValuePtr<void>(Zone), Zone, PPF_None);
    }
    
    // Set concurrency mode property
    FProperty* ConcurrencyProperty = Zone->GetClass()->FindPropertyByName(FName("ConcurrencyMode"));
    if (ConcurrencyProperty)
    {
        FString ConcurrencyStr = FString::FromInt((int32)ConcurrencyMode);
        ConcurrencyProperty->ImportText_Direct(*ConcurrencyStr, ConcurrencyProperty->ContainerPtrToValuePtr<void>(Zone), Zone, PPF_None);
    }
    
    // Set unique ID for the zone
    FProperty* IdProperty = Zone->GetClass()->FindPropertyByName(FName("ZoneId"));
    if (IdProperty)
    {
        FGuid NewGuid = FGuid::NewGuid();
        IdProperty->ImportText_Direct(*NewGuid.ToString(), IdProperty->ContainerPtrToValuePtr<void>(Zone), Zone, PPF_None);
    }
    
    // Call initialize method if available
    UFunction* InitFunc = Zone->FindFunction(FName("Initialize"));
    if (InitFunc)
    {
        Zone->ProcessEvent(InitFunc, nullptr);
    }
    
    return true;
}

bool UZoneFactory::ConfigureZoneGridCoordinates(UObject* Zone, int32 X, int32 Y, int32 Z)
{
    if (!Zone)
    {
        return false;
    }

    // Set grid coordinates property
    FProperty* GridCoordProperty = Zone->GetClass()->FindPropertyByName(FName("GridCoordinates"));
    if (GridCoordProperty)
    {
        FIntVector GridCoord(X, Y, Z);
        GridCoordProperty->ImportText_Direct(*GridCoord.ToString(), GridCoordProperty->ContainerPtrToValuePtr<void>(Zone), Zone, PPF_None);
    }
    
    return true;
}

bool UZoneFactory::SupportedLinkingBetweenZones(UObject* Zone)
{
    if (!Zone)
    {
        return false;
    }
    
    // Check if the zone class has neighbor properties
    bool HasNeighborProperties = 
        Zone->GetClass()->FindPropertyByName(FName("NeighborXPos")) != nullptr ||
        Zone->GetClass()->FindPropertyByName(FName("Neighbors")) != nullptr;
        
    return HasNeighborProperties;
}

bool UZoneFactory::LinkZoneToNeighbors(
    UObject* Zone, 
    const TArray<UObject*>& AllZones, 
    int32 X, int32 Y, int32 Z,
    int32 DivisionsX, int32 DivisionsY, int32 DivisionsZ)
{
    if (!Zone)
    {
        return false;
    }
    
    // Check if the zone has an array property for neighbors
    FProperty* NeighborsArrayProperty = Zone->GetClass()->FindPropertyByName(FName("Neighbors"));
    if (NeighborsArrayProperty)
    {
        // Link using array property
        TArray<UObject*> Neighbors;
        
        // Add up to 6 neighbors (positive and negative directions in X, Y, and Z)
        // X+
        if (X < DivisionsX - 1)
        {
            int32 Index = ((X + 1) * DivisionsY * DivisionsZ) + (Y * DivisionsZ) + Z;
            if (Index < AllZones.Num())
            {
                Neighbors.Add(AllZones[Index]);
            }
        }
        
        // X-
        if (X > 0)
        {
            int32 Index = ((X - 1) * DivisionsY * DivisionsZ) + (Y * DivisionsZ) + Z;
            if (Index < AllZones.Num())
            {
                Neighbors.Add(AllZones[Index]);
            }
        }
        
        // Y+
        if (Y < DivisionsY - 1)
        {
            int32 Index = (X * DivisionsY * DivisionsZ) + ((Y + 1) * DivisionsZ) + Z;
            if (Index < AllZones.Num())
            {
                Neighbors.Add(AllZones[Index]);
            }
        }
        
        // Y-
        if (Y > 0)
        {
            int32 Index = (X * DivisionsY * DivisionsZ) + ((Y - 1) * DivisionsZ) + Z;
            if (Index < AllZones.Num())
            {
                Neighbors.Add(AllZones[Index]);
            }
        }
        
        // Z+
        if (Z < DivisionsZ - 1)
        {
            int32 Index = (X * DivisionsY * DivisionsZ) + (Y * DivisionsZ) + (Z + 1);
            if (Index < AllZones.Num())
            {
                Neighbors.Add(AllZones[Index]);
            }
        }
        
        // Z-
        if (Z > 0)
        {
            int32 Index = (X * DivisionsY * DivisionsZ) + (Y * DivisionsZ) + (Z - 1);
            if (Index < AllZones.Num())
            {
                Neighbors.Add(AllZones[Index]);
            }
        }
        
        // Set neighbors array
        if (Neighbors.Num() > 0)
        {
            // Construct a string representation of the array
            FString NeighborsStr = TEXT("(");
            for (int32 i = 0; i < Neighbors.Num(); ++i)
            {
                if (i > 0)
                {
                    NeighborsStr += TEXT(",");
                }
                NeighborsStr += Neighbors[i]->GetPathName();
            }
            NeighborsStr += TEXT(")");
            
            // Import the array
            NeighborsArrayProperty->ImportText_Direct(*NeighborsStr, NeighborsArrayProperty->ContainerPtrToValuePtr<void>(Zone), Zone, PPF_None);
        }
        
        return true;
    }
    else
    {
        // Check for individual neighbor properties
        bool HasAnyNeighbor = false;
        
        // X+
        if (X < DivisionsX - 1)
        {
            int32 Index = ((X + 1) * DivisionsY * DivisionsZ) + (Y * DivisionsZ) + Z;
            if (Index < AllZones.Num())
            {
                FProperty* NeighborProperty = Zone->GetClass()->FindPropertyByName(FName("NeighborXPos"));
                if (NeighborProperty)
                {
                    NeighborProperty->ImportText_Direct(*AllZones[Index]->GetPathName(), NeighborProperty->ContainerPtrToValuePtr<void>(Zone), Zone, PPF_None);
                    HasAnyNeighbor = true;
                }
            }
        }
        
        // X-
        if (X > 0)
        {
            int32 Index = ((X - 1) * DivisionsY * DivisionsZ) + (Y * DivisionsZ) + Z;
            if (Index < AllZones.Num())
            {
                FProperty* NeighborProperty = Zone->GetClass()->FindPropertyByName(FName("NeighborXNeg"));
                if (NeighborProperty)
                {
                    NeighborProperty->ImportText_Direct(*AllZones[Index]->GetPathName(), NeighborProperty->ContainerPtrToValuePtr<void>(Zone), Zone, PPF_None);
                    HasAnyNeighbor = true;
                }
            }
        }
        
        // Y+
        if (Y < DivisionsY - 1)
        {
            int32 Index = (X * DivisionsY * DivisionsZ) + ((Y + 1) * DivisionsZ) + Z;
            if (Index < AllZones.Num())
            {
                FProperty* NeighborProperty = Zone->GetClass()->FindPropertyByName(FName("NeighborYPos"));
                if (NeighborProperty)
                {
                    NeighborProperty->ImportText_Direct(*AllZones[Index]->GetPathName(), NeighborProperty->ContainerPtrToValuePtr<void>(Zone), Zone, PPF_None);
                    HasAnyNeighbor = true;
                }
            }
        }
        
        // Y-
        if (Y > 0)
        {
            int32 Index = (X * DivisionsY * DivisionsZ) + ((Y - 1) * DivisionsZ) + Z;
            if (Index < AllZones.Num())
            {
                FProperty* NeighborProperty = Zone->GetClass()->FindPropertyByName(FName("NeighborYNeg"));
                if (NeighborProperty)
                {
                    NeighborProperty->ImportText_Direct(*AllZones[Index]->GetPathName(), NeighborProperty->ContainerPtrToValuePtr<void>(Zone), Zone, PPF_None);
                    HasAnyNeighbor = true;
                }
            }
        }
        
        // Z+
        if (Z < DivisionsZ - 1)
        {
            int32 Index = (X * DivisionsY * DivisionsZ) + (Y * DivisionsZ) + (Z + 1);
            if (Index < AllZones.Num())
            {
                FProperty* NeighborProperty = Zone->GetClass()->FindPropertyByName(FName("NeighborZPos"));
                if (NeighborProperty)
                {
                    NeighborProperty->ImportText_Direct(*AllZones[Index]->GetPathName(), NeighborProperty->ContainerPtrToValuePtr<void>(Zone), Zone, PPF_None);
                    HasAnyNeighbor = true;
                }
            }
        }
        
        // Z-
        if (Z > 0)
        {
            int32 Index = (X * DivisionsY * DivisionsZ) + (Y * DivisionsZ) + (Z - 1);
            if (Index < AllZones.Num())
            {
                FProperty* NeighborProperty = Zone->GetClass()->FindPropertyByName(FName("NeighborZNeg"));
                if (NeighborProperty)
                {
                    NeighborProperty->ImportText_Direct(*AllZones[Index]->GetPathName(), NeighborProperty->ContainerPtrToValuePtr<void>(Zone), Zone, PPF_None);
                    HasAnyNeighbor = true;
                }
            }
        }
        
        return HasAnyNeighbor;
    }
}

FName UZoneFactory::GetZonePoolName(EZoneType ZoneType)
{
    // Generate a consistent pool name based on the zone type
    return FName(*FString::Printf(TEXT("Zone_%d"), (int32)ZoneType));
}