// SVONodeFactory.h
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SVONodeFactory.generated.h"

class IComponentPoolManager;

// Define the IMiningFactory interface directly
UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UMiningFactory : public UInterface
{
    GENERATED_BODY()
};

/**
 * Base interface for all component factories in the MiningSpice system
 * Provides common operations for component creation and pooling
 */
class MININGSPICECOPILOT_API IMiningFactory
{
    GENERATED_BODY()

public:
    /** Initialize the factory */
    virtual bool Initialize() = 0;

    /** Shutdown the factory */
    virtual void Shutdown() = 0;

    /** Check if the factory is initialized */
    virtual bool IsInitialized() const = 0;

    /** Get the factory name */
    virtual FName GetFactoryName() const = 0;

    /** Check if the factory can create components of the specified type */
    virtual bool SupportsType(UClass* ComponentType) const = 0;

    /** Create a component of the specified type with optional parameters */
    virtual UObject* CreateComponent(UClass* ComponentType, const TMap<FName, FString>& Parameters = TMap<FName, FString>()) = 0;

    /** Get the component types supported by this factory */
    virtual TArray<UClass*> GetSupportedTypes() const = 0;

    /** Register an archetype for the specified component type */
    virtual bool RegisterArchetype(UClass* ComponentType, UObject* Archetype) = 0;

    /** Check if a memory pool exists for the specified component type */
    virtual bool HasPool(UClass* ComponentType) const = 0;

    /** Create a memory pool for the specified component type */
    virtual bool CreatePool(UClass* ComponentType, int32 InitialSize, int32 MaxSize, bool bEnablePooling = true) = 0;

    /** Return a component to its memory pool */
    virtual bool ReturnToPool(UObject* Component) = 0;

    /** Flush a memory pool for the specified component type */
    virtual int32 FlushPool(UClass* ComponentType) = 0;

    /** Get statistics for a memory pool */
    virtual bool GetPoolStats(UClass* ComponentType, int32& OutAvailable, int32& OutTotal) const = 0;
};

/** SVO node types */
UENUM(BlueprintType)
enum class ENodeType : uint8
{
    Internal,        // Internal octree node
    Leaf,            // Leaf node with material
    Empty            // Empty space node
};

/** Node performance metrics */
USTRUCT()
struct FNodeMetrics
{
    GENERATED_BODY()

    // Average creation time in microseconds
    UPROPERTY()
    float AverageCreationTimeUs = 0.0f;

    // Total nodes created
    UPROPERTY()
    int64 TotalCreated = 0;

    // Nodes currently active
    UPROPERTY()
    int32 ActiveCount = 0;

    // Cache hits when requesting from pool
    UPROPERTY()
    int64 CacheHits = 0;

    // Cache misses when requesting from pool
    UPROPERTY()
    int64 CacheMisses = 0;
};

/**
 * Specialized factory for SVO node creation and optimization
 * Focused on memory-efficient node creation with specialized pooling
 */
UCLASS()
class MININGSPICECOPILOT_API USVONodeFactory : public UObject, public IMiningFactory
{
    GENERATED_BODY()

public:
    USVONodeFactory();

    //~ Begin IMiningFactory Interface
    virtual bool Initialize() override;
    virtual void Shutdown() override;
    virtual bool IsInitialized() const override;
    virtual FName GetFactoryName() const override;
    virtual bool SupportsType(UClass* ComponentType) const override;
    virtual UObject* CreateComponent(UClass* ComponentType, const TMap<FName, FString>& Parameters = TMap<FName, FString>()) override;
    virtual TArray<UClass*> GetSupportedTypes() const override;
    virtual bool RegisterArchetype(UClass* ComponentType, UObject* Archetype) override;
    virtual bool HasPool(UClass* ComponentType) const override;
    virtual bool CreatePool(UClass* ComponentType, int32 InitialSize, int32 MaxSize, bool bEnablePooling = true) override;
    virtual bool ReturnToPool(UObject* Component) override;
    virtual int32 FlushPool(UClass* ComponentType) override;
    virtual bool GetPoolStats(UClass* ComponentType, int32& OutAvailable, int32& OutTotal) const override;
    //~ End IMiningFactory Interface

    /**
     * Create an SVO node component with specialized configuration
     * @param NodeType Type of SVO node to create (internal or leaf)
     * @param Location Spatial location for the node
     * @param LOD Level of detail for the node
     * @param MaterialTypeId Material type ID for the node (for leaf nodes)
     * @return New SVO node instance
     */
    UObject* CreateSVONode(ENodeType NodeType, const FVector& Location, uint8 LOD, uint32 MaterialTypeId = 0);

    /**
     * Create a batch of SVO nodes of the same type
     * @param NodeType Type of SVO node to create
     * @param Count Number of nodes to create
     * @param LOD Level of detail for the nodes
     * @return Array of created nodes
     */
    TArray<UObject*> CreateSVONodeBatch(ENodeType NodeType, int32 Count, uint8 LOD);

    /**
     * Get singleton instance
     * @return Singleton factory instance
     */
    static USVONodeFactory* Get();

protected:
    /** Pool manager reference */
    UPROPERTY()
    TScriptInterface<IComponentPoolManager> PoolManager;

    /** Registered node types */
    UPROPERTY()
    TSet<UClass*> SupportedTypes;

    /** Node archetypes */
    UPROPERTY()
    TMap<UClass*, UObject*> Archetypes;

    /** Whether the factory is initialized */
    UPROPERTY()
    bool bIsInitialized;

    /** Factory name */
    UPROPERTY()
    FName FactoryName;

    /** Memory pool sizes by node type */
    UPROPERTY()
    TMap<FName, int32> NodePoolSizes;

    /** Performance metrics for different node types */
    UPROPERTY()
    TMap<FName, FNodeMetrics> NodeMetrics;

    /**
     * Configure a node with spatial parameters
     * @param Node Node to configure
     * @param Location Spatial location
     * @param LOD Level of detail
     * @param MaterialTypeId Material type ID
     * @return True if configuration was successful
     */
    bool ConfigureNode(UObject* Node, const FVector& Location, uint8 LOD, uint32 MaterialTypeId);

    /**
     * Optimize memory layout for batch node creation
     * @param NodeType Type of node to optimize for
     * @param Count Number of nodes to be created
     */
    void OptimizeMemoryLayout(ENodeType NodeType, int32 Count);
};