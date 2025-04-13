// SVONodeFactory.h
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IFactory.h"
#include "SVONodeFactory.generated.h"

class IComponentPoolManager;

/** SVO node types */
UENUM(BlueprintType)
enum class ENodeType : uint8
{
    Internal,        // Internal octree node
    Leaf,            // Leaf node with material
    Empty            // Empty space node
};

/** Node performance metrics */
USTRUCT(BlueprintType)
struct MININGSPICECOPILOT_API FNodeMetrics
{
    GENERATED_BODY()

    // Average creation time in microseconds
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node Metrics")
    float AverageCreationTimeUs = 0.0f;

    // Total nodes created
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node Metrics")
    int64 TotalCreated = 0;

    // Nodes currently active
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node Metrics")
    int32 ActiveCount = 0;

    // Cache hits when requesting from pool
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node Metrics")
    int64 CacheHits = 0;

    // Cache misses when requesting from pool
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node Metrics")
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