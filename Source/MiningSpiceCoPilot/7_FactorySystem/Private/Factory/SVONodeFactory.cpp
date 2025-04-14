// SVONodeFactory.cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factory/SVONodeFactory.h"
#include "Interfaces/IComponentPoolManager.h"
#include "Interfaces/IFactoryMetrics.h"

namespace
{
    // Singleton instance
    USVONodeFactory* GlobalFactoryInstance = nullptr;

    // Factory name
    const FName FactoryName = "SVONodeFactory";
}

USVONodeFactory::USVONodeFactory()
    : bIsInitialized(false)
    , FactoryName(::FactoryName)
{
    // Store singleton reference
    if (!GlobalFactoryInstance)
    {
        GlobalFactoryInstance = this;
    }
}

bool USVONodeFactory::Initialize()
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
        UE_LOG(LogTemp, Error, TEXT("SVONodeFactory: Failed to cast IComponentPoolManager to UObject"));
        return false;
    }

    // Initialize metrics tracking
    IFactoryMetrics::Get().TrackOperation(
        FactoryName,
        nullptr,
        EFactoryOperationType::Initialize, 
        0.0f);

    // Set default pool sizes
    NodePoolSizes.Add(FName("InternalNode"), 1024);
    NodePoolSizes.Add(FName("LeafNode"), 2048);
    NodePoolSizes.Add(FName("EmptyNode"), 512);

    bIsInitialized = true;
    UE_LOG(LogTemp, Display, TEXT("SVONodeFactory initialized"));
    return true;
}

void USVONodeFactory::Shutdown()
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
    UE_LOG(LogTemp, Display, TEXT("SVONodeFactory shut down"));
}

bool USVONodeFactory::IsInitialized() const
{
    return bIsInitialized;
}

FName USVONodeFactory::GetFactoryName() const
{
    return FactoryName;
}

bool USVONodeFactory::SupportsType(UClass* ComponentType) const
{
    if (!ComponentType)
    {
        return false;
    }

    return SupportedTypes.Contains(ComponentType);
}

UObject* USVONodeFactory::CreateComponent(UClass* ComponentType, const TMap<FName, FString>& Parameters)
{
    if (!bIsInitialized || !ComponentType)
    {
        UE_LOG(LogTemp, Warning, TEXT("SVONodeFactory: Cannot create component - factory not initialized or invalid component type"));
        return nullptr;
    }

    if (!SupportedTypes.Contains(ComponentType))
    {
        UE_LOG(LogTemp, Warning, TEXT("SVONodeFactory: Component type not supported: %s"), *ComponentType->GetName());
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
        // Parse parameters
        FVector Location = FVector::ZeroVector;
        uint8 LOD = 0;
        uint32 MaterialTypeId = 0;
        
        if (Parameters.Contains(FName("Location")))
        {
            FString LocationStr = Parameters.FindRef(FName("Location"));
            Location.InitFromString(LocationStr);
        }
        
        if (Parameters.Contains(FName("LOD")))
        {
            LOD = FCString::Atoi(*Parameters.FindRef(FName("LOD")));
        }
        
        if (Parameters.Contains(FName("MaterialTypeId")))
        {
            MaterialTypeId = FCString::Atoi(*Parameters.FindRef(FName("MaterialTypeId")));
        }
        
        // Configure the node
        ConfigureNode(Result, Location, LOD, MaterialTypeId);
        
        // Apply remaining parameters
        for (const auto& Param : Parameters)
        {
            // Skip already processed parameters
            if (Param.Key == FName("Location") || 
                Param.Key == FName("LOD") || 
                Param.Key == FName("MaterialTypeId"))
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

    // Update metrics
    if (Result)
    {
        FName NodeTypeName;
        
        if (Result->GetClass()->GetName().Contains(TEXT("Internal")))
        {
            NodeTypeName = FName("InternalNode");
        }
        else if (Result->GetClass()->GetName().Contains(TEXT("Leaf")))
        {
            NodeTypeName = FName("LeafNode");
        }
        else
        {
            NodeTypeName = FName("EmptyNode");
        }
        
        if (!NodeMetrics.Contains(NodeTypeName))
        {
            NodeMetrics.Add(NodeTypeName, FNodeMetrics());
        }
        
        FNodeMetrics& Metrics = NodeMetrics[NodeTypeName];
        Metrics.TotalCreated++;
        Metrics.ActiveCount++;
        
        if (bCacheMiss)
        {
            Metrics.CacheMisses++;
        }
        else
        {
            Metrics.CacheHits++;
        }
    }

    // End metrics timing
    IFactoryMetrics::Get().EndOperation(MetricHandle, Result != nullptr, bCacheMiss);

    return Result;
}

TArray<UClass*> USVONodeFactory::GetSupportedTypes() const
{
    TArray<UClass*> Types;
    for (UClass* SupportedType : SupportedTypes)
    {
        Types.Add(SupportedType);
    }
    return Types;
}

bool USVONodeFactory::RegisterArchetype(UClass* ComponentType, UObject* Archetype)
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
    
    UE_LOG(LogTemp, Display, TEXT("SVONodeFactory: Registered archetype for %s"), *ComponentType->GetName());
    return true;
}

bool USVONodeFactory::HasPool(UClass* ComponentType) const
{
    if (!bIsInitialized || !ComponentType || !PoolManager)
    {
        return false;
    }

    return PoolManager->HasPoolForType(ComponentType);
}

bool USVONodeFactory::CreatePool(UClass* ComponentType, int32 InitialSize, int32 MaxSize, bool bEnablePooling)
{
    if (!bIsInitialized || !ComponentType || !PoolManager)
    {
        return false;
    }

    // Determine node type for custom pool sizing
    FName NodeTypeName;
    if (ComponentType->GetName().Contains(TEXT("Internal")))
    {
        NodeTypeName = FName("InternalNode");
    }
    else if (ComponentType->GetName().Contains(TEXT("Leaf")))
    {
        NodeTypeName = FName("LeafNode");
    }
    else
    {
        NodeTypeName = FName("EmptyNode");
    }
    
    // Use default size if specified size is too small
    int32 DefaultSize = NodePoolSizes.FindRef(NodeTypeName);
    if (InitialSize < DefaultSize / 4)
    {
        InitialSize = DefaultSize / 4;
    }
    
    if (MaxSize < DefaultSize)
    {
        MaxSize = DefaultSize;
    }

    // Configure pool
    FComponentPoolConfig PoolConfig;
    PoolConfig.PoolName = FName(*FString::Printf(TEXT("%s_Pool"), *ComponentType->GetName()));
    PoolConfig.ComponentType = ComponentType;
    PoolConfig.InitialSize = InitialSize;
    PoolConfig.MaxSize = MaxSize;
    PoolConfig.bPreallocate = true;
    PoolConfig.Template = Archetypes.FindRef(ComponentType);
    PoolConfig.AllocationStrategy = EPoolAllocationStrategy::FirstAvailable; // Fastest for high-frequency nodes
    
    // Create pool
    bool bSuccess = PoolManager->CreatePool(PoolConfig);
    
    if (bSuccess)
    {
        UE_LOG(LogTemp, Display, TEXT("SVONodeFactory: Created pool for %s (Initial: %d, Max: %d)"), 
            *ComponentType->GetName(), InitialSize, MaxSize);
    }
    
    return bSuccess;
}

bool USVONodeFactory::ReturnToPool(UObject* Component)
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
    
    // Update metrics
    if (bSuccess)
    {
        FName NodeTypeName;
        
        if (Component->GetClass()->GetName().Contains(TEXT("Internal")))
        {
            NodeTypeName = FName("InternalNode");
        }
        else if (Component->GetClass()->GetName().Contains(TEXT("Leaf")))
        {
            NodeTypeName = FName("LeafNode");
        }
        else
        {
            NodeTypeName = FName("EmptyNode");
        }
        
        if (NodeMetrics.Contains(NodeTypeName))
        {
            NodeMetrics[NodeTypeName].ActiveCount = FMath::Max(0, NodeMetrics[NodeTypeName].ActiveCount - 1);
        }
    }
    
    // End metrics timing
    IFactoryMetrics::Get().EndOperation(MetricHandle, bSuccess, false);
    
    return bSuccess;
}

int32 USVONodeFactory::FlushPool(UClass* ComponentType)
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
    
    UE_LOG(LogTemp, Display, TEXT("SVONodeFactory: Flushed pool for %s (%d instances removed)"), 
        *ComponentType->GetName(), Removed);
        
    return Removed;
}

bool USVONodeFactory::GetPoolStats(UClass* ComponentType, int32& OutAvailable, int32& OutTotal) const
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

UObject* USVONodeFactory::CreateSVONode(ENodeType NodeType, const FVector& Location, uint8 LOD, uint32 MaterialTypeId)
{
    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Warning, TEXT("SVONodeFactory: Cannot create node - factory not initialized"));
        return nullptr;
    }

    // Get the class to instantiate based on node type
    UClass* NodeClass = nullptr;
    FName NodeTypeName;
    
    switch (NodeType)
    {
    case ENodeType::Internal:
        {
            // Replace FilterByPredicate with manual iteration
            TArray<UClass*> Classes;
            SupportedTypes.GetKeys(Classes);
            for (UClass* Class : Classes)
            {
                if (Class->GetName().Contains(TEXT("Internal")))
                {
                    NodeClass = Class;
                    break;
                }
            }
            NodeTypeName = FName("InternalNode");
        }
        break;
        
    case ENodeType::Leaf:
        {
            // Replace FilterByPredicate with manual iteration
            TArray<UClass*> Classes;
            SupportedTypes.GetKeys(Classes);
            for (UClass* Class : Classes)
            {
                if (Class->GetName().Contains(TEXT("Leaf")))
                {
                    NodeClass = Class;
                    break;
                }
            }
            NodeTypeName = FName("LeafNode");
        }
        break;
        
    case ENodeType::Empty:
        {
            // Replace FilterByPredicate with manual iteration
            TArray<UClass*> Classes;
            SupportedTypes.GetKeys(Classes);
            for (UClass* Class : Classes)
            {
                if (Class->GetName().Contains(TEXT("Empty")))
                {
                    NodeClass = Class;
                    break;
                }
            }
            NodeTypeName = FName("EmptyNode");
        }
        break;
    }
    
    if (!NodeClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("SVONodeFactory: No class found for node type %d"), (int32)NodeType);
        return nullptr;
    }

    // Start timing for metrics
    int32 MetricHandle = IFactoryMetrics::Get().BeginOperation(
        FactoryName, 
        NodeClass, 
        EFactoryOperationType::Create);

    // Check if we should use pooling
    UObject* Result = nullptr;
    bool bCacheMiss = false;
    
    if (HasPool(NodeClass))
    {
        // Try to get from pool
        Result = PoolManager->AllocateComponentByType(NodeClass);
        
        if (!Result)
        {
            // Create new instance if pool is empty
            Result = NewObject<UObject>(GetTransientPackage(), NodeClass);
            bCacheMiss = true;
        }
    }
    else
    {
        // Create new instance without pooling
        Result = NewObject<UObject>(GetTransientPackage(), NodeClass);
    }

    if (Result)
    {
        // Configure the node
        ConfigureNode(Result, Location, LOD, MaterialTypeId);
    }

    // Update metrics
    if (Result)
    {
        if (!NodeMetrics.Contains(NodeTypeName))
        {
            NodeMetrics.Add(NodeTypeName, FNodeMetrics());
        }
        
        FNodeMetrics& Metrics = NodeMetrics[NodeTypeName];
        Metrics.TotalCreated++;
        Metrics.ActiveCount++;
        
        if (bCacheMiss)
        {
            Metrics.CacheMisses++;
        }
        else
        {
            Metrics.CacheHits++;
        }
    }

    // End metrics timing
    IFactoryMetrics::Get().EndOperation(MetricHandle, Result != nullptr, bCacheMiss);
    
    return Result;
}

TArray<UObject*> USVONodeFactory::CreateSVONodeBatch(ENodeType NodeType, int32 Count, uint8 LOD)
{
    TArray<UObject*> Results;
    
    if (!bIsInitialized || Count <= 0)
    {
        return Results;
    }

    // Get the class to instantiate based on node type
    UClass* NodeClass = nullptr;
    FName NodeTypeName;
    
    switch (NodeType)
    {
    case ENodeType::Internal:
        {
            // Replace FilterByPredicate with manual iteration
            TArray<UClass*> Classes;
            SupportedTypes.GetKeys(Classes);
            for (UClass* Class : Classes)
            {
                if (Class->GetName().Contains(TEXT("Internal")))
                {
                    NodeClass = Class;
                    break;
                }
            }
            NodeTypeName = FName("InternalNode");
        }
        break;
        
    case ENodeType::Leaf:
        {
            // Replace FilterByPredicate with manual iteration
            TArray<UClass*> Classes;
            SupportedTypes.GetKeys(Classes);
            for (UClass* Class : Classes)
            {
                if (Class->GetName().Contains(TEXT("Leaf")))
                {
                    NodeClass = Class;
                    break;
                }
            }
            NodeTypeName = FName("LeafNode");
        }
        break;
        
    case ENodeType::Empty:
        {
            // Replace FilterByPredicate with manual iteration
            TArray<UClass*> Classes;
            SupportedTypes.GetKeys(Classes);
            for (UClass* Class : Classes)
            {
                if (Class->GetName().Contains(TEXT("Empty")))
                {
                    NodeClass = Class;
                    break;
                }
            }
            NodeTypeName = FName("EmptyNode");
        }
        break;
    }
    
    if (!NodeClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("SVONodeFactory: No class found for node type %d"), (int32)NodeType);
        return Results;
    }

    // Start timing for batch creation
    int32 MetricHandle = IFactoryMetrics::Get().BeginOperation(
        FactoryName, 
        NodeClass, 
        EFactoryOperationType::Create);

    // Optimize memory for batch creation
    OptimizeMemoryLayout(NodeType, Count);
    
    Results.Reserve(Count);
    int32 CacheMissCount = 0;
    
    // Create nodes in batch
    for (int32 i = 0; i < Count; ++i)
    {
        UObject* Node = nullptr;
        bool bCacheMiss = false;
        
        if (HasPool(NodeClass))
        {
            // Try to get from pool
            Node = PoolManager->AllocateComponentByType(NodeClass);
            
            if (!Node)
            {
                // Create new instance if pool is empty
                Node = NewObject<UObject>(GetTransientPackage(), NodeClass);
                bCacheMiss = true;
                CacheMissCount++;
            }
        }
        else
        {
            // Create new instance without pooling
            Node = NewObject<UObject>(GetTransientPackage(), NodeClass);
        }
        
        if (Node)
        {
            // Minimal configuration for batch mode - caller will configure further
            ConfigureNode(Node, FVector::ZeroVector, LOD, 0);
            Results.Add(Node);
        }
    }

    // Update metrics
    if (!NodeMetrics.Contains(NodeTypeName))
    {
        NodeMetrics.Add(NodeTypeName, FNodeMetrics());
    }
    
    FNodeMetrics& Metrics = NodeMetrics[NodeTypeName];
    Metrics.TotalCreated += Results.Num();
    Metrics.ActiveCount += Results.Num();
    Metrics.CacheMisses += CacheMissCount;
    Metrics.CacheHits += (Results.Num() - CacheMissCount);

    // End metrics timing
    IFactoryMetrics::Get().EndOperation(MetricHandle, true, CacheMissCount > 0);

    return Results;
}

USVONodeFactory* USVONodeFactory::Get()
{
    if (!GlobalFactoryInstance)
    {
        // Create instance if needed
        GlobalFactoryInstance = NewObject<USVONodeFactory>();
        GlobalFactoryInstance->AddToRoot(); // Prevent garbage collection
        GlobalFactoryInstance->Initialize();
    }
    
    return GlobalFactoryInstance;
}

bool USVONodeFactory::ConfigureNode(UObject* Node, const FVector& Location, uint8 LOD, uint32 MaterialTypeId)
{
    if (!Node)
    {
        return false;
    }

    // Set location property
    FProperty* LocationProperty = Node->GetClass()->FindPropertyByName(FName("Location"));
    if (LocationProperty)
    {
        LocationProperty->ImportText_Direct(*Location.ToString(), LocationProperty->ContainerPtrToValuePtr<void>(Node), Node, PPF_None);
    }
    
    // Set LOD property
    FProperty* LODProperty = Node->GetClass()->FindPropertyByName(FName("LOD"));
    if (LODProperty)
    {
        FString LODStr = FString::FromInt(LOD);
        LODProperty->ImportText_Direct(*LODStr, LODProperty->ContainerPtrToValuePtr<void>(Node), Node, PPF_None);
    }
    
    // Set material type property for leaf nodes
    if (Node->GetClass()->GetName().Contains(TEXT("Leaf")))
    {
        FProperty* MaterialProperty = Node->GetClass()->FindPropertyByName(FName("MaterialTypeId"));
        if (MaterialProperty)
        {
            FString MaterialStr = FString::FromInt(MaterialTypeId);
            MaterialProperty->ImportText_Direct(*MaterialStr, MaterialProperty->ContainerPtrToValuePtr<void>(Node), Node, PPF_None);
        }
    }
    
    return true;
}

void USVONodeFactory::OptimizeMemoryLayout(ENodeType NodeType, int32 Count)
{
    // Pre-allocate memory to avoid fragmentation during batch creation
    // This method would implement specific memory layout strategies
    // for different node types to improve cache coherence
    
    // For now, just make sure the pool is large enough
    UClass* NodeClass = nullptr;
    FName NodeTypeName;
    
    switch (NodeType)
    {
    case ENodeType::Internal:
        {
            // Replace FilterByPredicate with manual iteration
            TArray<UClass*> Classes;
            SupportedTypes.GetKeys(Classes);
            for (UClass* Class : Classes)
            {
                if (Class->GetName().Contains(TEXT("Internal")))
                {
                    NodeClass = Class;
                    break;
                }
            }
            NodeTypeName = FName("InternalNode");
        }
        break;
        
    case ENodeType::Leaf:
        {
            // Replace FilterByPredicate with manual iteration
            TArray<UClass*> Classes;
            SupportedTypes.GetKeys(Classes);
            for (UClass* Class : Classes)
            {
                if (Class->GetName().Contains(TEXT("Leaf")))
                {
                    NodeClass = Class;
                    break;
                }
            }
            NodeTypeName = FName("LeafNode");
        }
        break;
        
    case ENodeType::Empty:
        {
            // Replace FilterByPredicate with manual iteration
            TArray<UClass*> Classes;
            SupportedTypes.GetKeys(Classes);
            for (UClass* Class : Classes)
            {
                if (Class->GetName().Contains(TEXT("Empty")))
                {
                    NodeClass = Class;
                    break;
                }
            }
            NodeTypeName = FName("EmptyNode");
        }
        break;
    }
    
    if (NodeClass && PoolManager)
    {
        FComponentPoolStats Stats;
        FName PoolName = FName(*FString::Printf(TEXT("%s_Pool"), *NodeClass->GetName()));
        
        if (PoolManager->GetPoolStats(PoolName, Stats))
        {
            // If available nodes are less than half of requested count, grow the pool
            if (Stats.AvailableCount < Count / 2)
            {
                int32 NeededGrowth = Count - Stats.AvailableCount;
                if (NeededGrowth > 0)
                {
                    PoolManager->GrowPool(PoolName, NeededGrowth);
                }
            }
        }
    }
}