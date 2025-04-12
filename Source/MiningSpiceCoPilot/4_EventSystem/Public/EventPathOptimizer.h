// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IEventSubscriber.h"
#include "EventPathOptimizer.generated.h"

/**
 * A path for event delivery that can be optimized
 */
struct MININGSPICECOPILOT_API FEventDeliveryPath
{
    /** Unique ID for this path */
    FGuid PathId;
    
    /** Source region ID */
    int32 SourceRegionId;
    
    /** Target region ID */
    int32 TargetRegionId;
    
    /** Intermediate regions in the path */
    TArray<int32> IntermediateRegions;
    
    /** Delivery cost (latency in ms, bandwidth, etc.) */
    float DeliveryCost;
    
    /** Whether this is a direct path */
    bool bIsDirect;
    
    /** Whether this path is currently active */
    bool bIsActive;
    
    /** Last time this path was used */
    double LastUsedTimeSeconds;
    
    /** Constructor */
    FEventDeliveryPath()
        : SourceRegionId(INDEX_NONE)
        , TargetRegionId(INDEX_NONE)
        , DeliveryCost(0.0f)
        , bIsDirect(true)
        , bIsActive(true)
        , LastUsedTimeSeconds(0.0)
    {
        PathId = FGuid::NewGuid();
    }
};

/**
 * Cached subscriber information for path optimization
 */
struct MININGSPICECOPILOT_API FCachedSubscriberInfo
{
    /** Subscriber reference */
    TWeakInterfacePtr<IEventSubscriber> Subscriber;
    
    /** Region IDs that this subscriber is interested in */
    TArray<int32> RegionIds;
    
    /** Zone IDs that this subscriber is interested in */
    TArray<TPair<int32, int32>> ZoneIds;
    
    /** Event types that this subscriber is interested in */
    TSet<FName> EventTypes;
    
    /** Scopes that this subscriber is interested in */
    TArray<EEventScope> Scopes;
    
    /** Priority of this subscriber */
    int32 Priority;
    
    /** Last time this subscriber info was updated */
    double LastUpdateTimeSeconds;
    
    /** Constructor */
    FCachedSubscriberInfo()
        : Priority(0)
        , LastUpdateTimeSeconds(0.0)
    {
    }
};

/**
 * A cluster of subscribers for optimized delivery
 */
struct MININGSPICECOPILOT_API FSubscriberCluster
{
    /** Unique ID for this cluster */
    FGuid ClusterId;
    
    /** Region ID for this cluster */
    int32 RegionId;
    
    /** Subscribers in this cluster */
    TArray<TWeakInterfacePtr<IEventSubscriber>> Subscribers;
    
    /** Map of event types to interested subscribers */
    TMap<FName, TArray<TWeakInterfacePtr<IEventSubscriber>>> EventSubscriberMap;
    
    /** Constructor */
    FSubscriberCluster()
        : RegionId(INDEX_NONE)
    {
        ClusterId = FGuid::NewGuid();
    }
};

/**
 * Event Path Optimizer
 * Optimizes event delivery paths for efficient routing based on region topology
 */
UCLASS()
class MININGSPICECOPILOT_API UEventPathOptimizer : public UObject
{
    GENERATED_BODY()
    
public:
    /**
     * Initializes the path optimizer
     * @return True if initialization was successful
     */
    bool Initialize();
    
    /**
     * Shuts down the path optimizer
     */
    void Shutdown();
    
    /**
     * Checks if the path optimizer has been initialized
     * @return True if initialized, false otherwise
     */
    bool IsInitialized() const;
    
    /**
     * Adds a region connection to the topology
     * @param SourceRegionId Source region ID
     * @param TargetRegionId Target region ID
     * @param Cost Cost of the connection
     * @return True if connection was added
     */
    bool AddRegionConnection(int32 SourceRegionId, int32 TargetRegionId, float Cost = 1.0f);
    
    /**
     * Removes a region connection from the topology
     * @param SourceRegionId Source region ID
     * @param TargetRegionId Target region ID
     * @return True if connection was removed
     */
    bool RemoveRegionConnection(int32 SourceRegionId, int32 TargetRegionId);
    
    /**
     * Updates the cost of an existing connection
     * @param SourceRegionId Source region ID
     * @param TargetRegionId Target region ID
     * @param NewCost New cost for the connection
     * @return True if cost was updated
     */
    bool UpdateConnectionCost(int32 SourceRegionId, int32 TargetRegionId, float NewCost);
    
    /**
     * Gets the optimal delivery path between regions
     * @param SourceRegionId Source region ID
     * @param TargetRegionId Target region ID
     * @return Optimal delivery path or nullptr if none
     */
    const FEventDeliveryPath* GetOptimalPath(int32 SourceRegionId, int32 TargetRegionId);
    
    /**
     * Registers a subscriber with the optimizer
     * @param Subscriber The subscriber to register
     */
    void RegisterSubscriber(IEventSubscriber* Subscriber);
    
    /**
     * Unregisters a subscriber from the optimizer
     * @param Subscriber The subscriber to unregister
     */
    void UnregisterSubscriber(IEventSubscriber* Subscriber);
    
    /**
     * Updates the cached information for a subscriber
     * @param Subscriber The subscriber to update
     */
    void UpdateSubscriberCache(IEventSubscriber* Subscriber);
    
    /**
     * Gets subscribers clustered by region for optimized delivery
     * @param EventType Type of event to get clusters for
     * @return Map of region IDs to subscriber clusters
     */
    TMap<int32, FSubscriberCluster> GetSubscriberClusters(const FName& EventType) const;
    
    /**
     * Notifies that an event was delivered along a path
     * @param PathId ID of the path
     * @param DeliveryTimeMs Time it took to deliver the event in ms
     */
    void NotifyPathUsed(const FGuid& PathId, float DeliveryTimeMs);
    
    /**
     * Rebuilds all optimized paths
     */
    void RebuildPaths();
    
    /**
     * Gets the singleton instance
     */
    static UEventPathOptimizer* Get();
    
private:
    /** Whether the optimizer has been initialized */
    bool bIsInitialized;
    
    /** Critical section for thread safety */
    mutable FCriticalSection CriticalSection;
    
    /** Map of region IDs to their connections */
    TMap<int32, TMap<int32, float>> RegionConnections;
    
    /** Cache of optimized paths between regions */
    mutable TMap<FGuid, FEventDeliveryPath> PathCache;
    
    /** Map of source-target region pairs to path IDs */
    mutable TMap<TPair<int32, int32>, FGuid> RegionPairToPathId;
    
    /** Cached subscriber information */
    TMap<TWeakInterfacePtr<IEventSubscriber>, FCachedSubscriberInfo> SubscriberCache;
    
    /** Cached subscriber clusters by region */
    mutable TMap<int32, FSubscriberCluster> RegionClusters;
    
    /** Last time paths were rebuilt */
    double LastRebuildTimeSeconds;
    
    /**
     * Calculates the optimal path between regions
     * @param SourceRegionId Source region ID
     * @param TargetRegionId Target region ID
     * @return Optimal delivery path
     */
    FEventDeliveryPath CalculateOptimalPath(int32 SourceRegionId, int32 TargetRegionId) const;
    
    /**
     * Builds subscriber clusters based on current cache
     */
    void BuildSubscriberClusters();
    
    /**
     * Checks if a path needs updating based on age and usage
     * @param Path The path to check
     * @return True if the path should be recalculated
     */
    bool ShouldUpdatePath(const FEventDeliveryPath& Path) const;
    
    /**
     * Cleans up any stale paths
     */
    void CleanupStalePaths();
};