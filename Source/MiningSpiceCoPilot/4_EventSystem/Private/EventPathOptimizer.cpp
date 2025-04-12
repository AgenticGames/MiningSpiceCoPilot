// Copyright Epic Games, Inc. All Rights Reserved.

#include "EventPathOptimizer.h"
#include "HAL/PlatformTime.h"
#include "Algo/Reverse.h"

UEventPathOptimizer* UEventPathOptimizer::Get()
{
    static UEventPathOptimizer* Singleton = nullptr;
    if (!Singleton)
    {
        Singleton = NewObject<UEventPathOptimizer>();
        Singleton->Initialize();
    }
    return Singleton;
}

bool UEventPathOptimizer::Initialize()
{
    if (bIsInitialized)
    {
        return true;
    }
    
    bIsInitialized = true;
    LastRebuildTimeSeconds = FPlatformTime::Seconds();
    
    return true;
}

void UEventPathOptimizer::Shutdown()
{
    if (!bIsInitialized)
    {
        return;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    // Clear collections
    RegionConnections.Empty();
    PathCache.Empty();
    RegionPairToPathId.Empty();
    SubscriberCache.Empty();
    RegionClusters.Empty();
    
    bIsInitialized = false;
}

bool UEventPathOptimizer::IsInitialized() const
{
    return bIsInitialized;
}

bool UEventPathOptimizer::AddRegionConnection(int32 SourceRegionId, int32 TargetRegionId, float Cost)
{
    if (!bIsInitialized || SourceRegionId == INDEX_NONE || TargetRegionId == INDEX_NONE || Cost <= 0.0f)
    {
        return false;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    // Add bidirectional connection
    if (!RegionConnections.Contains(SourceRegionId))
    {
        RegionConnections.Add(SourceRegionId, TMap<int32, float>());
    }
    
    if (!RegionConnections.Contains(TargetRegionId))
    {
        RegionConnections.Add(TargetRegionId, TMap<int32, float>());
    }
    
    // Set connection costs
    RegionConnections[SourceRegionId].Add(TargetRegionId, Cost);
    RegionConnections[TargetRegionId].Add(SourceRegionId, Cost);
    
    // Invalidate cached paths that involve these regions
    TArray<TPair<int32, int32>> InvalidateKeys;
    for (const auto& Entry : RegionPairToPathId)
    {
        if ((Entry.Key.Key == SourceRegionId || Entry.Key.Value == SourceRegionId) ||
            (Entry.Key.Key == TargetRegionId || Entry.Key.Value == TargetRegionId))
        {
            FGuid PathId = Entry.Value;
            PathCache.Remove(PathId);
            InvalidateKeys.Add(Entry.Key);
        }
    }
    
    for (const auto& Key : InvalidateKeys)
    {
        RegionPairToPathId.Remove(Key);
    }
    
    return true;
}

bool UEventPathOptimizer::RemoveRegionConnection(int32 SourceRegionId, int32 TargetRegionId)
{
    if (!bIsInitialized || SourceRegionId == INDEX_NONE || TargetRegionId == INDEX_NONE)
    {
        return false;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    bool bRemoved = false;
    
    // Remove bidirectional connection
    if (RegionConnections.Contains(SourceRegionId))
    {
        bRemoved |= RegionConnections[SourceRegionId].Remove(TargetRegionId) > 0;
    }
    
    if (RegionConnections.Contains(TargetRegionId))
    {
        bRemoved |= RegionConnections[TargetRegionId].Remove(SourceRegionId) > 0;
    }
    
    // Invalidate cached paths that involve these regions
    TArray<TPair<int32, int32>> InvalidateKeys;
    for (const auto& Entry : RegionPairToPathId)
    {
        if ((Entry.Key.Key == SourceRegionId || Entry.Key.Value == SourceRegionId) ||
            (Entry.Key.Key == TargetRegionId || Entry.Key.Value == TargetRegionId))
        {
            FGuid PathId = Entry.Value;
            PathCache.Remove(PathId);
            InvalidateKeys.Add(Entry.Key);
        }
    }
    
    for (const auto& Key : InvalidateKeys)
    {
        RegionPairToPathId.Remove(Key);
    }
    
    return bRemoved;
}

bool UEventPathOptimizer::UpdateConnectionCost(int32 SourceRegionId, int32 TargetRegionId, float NewCost)
{
    if (!bIsInitialized || SourceRegionId == INDEX_NONE || TargetRegionId == INDEX_NONE || NewCost <= 0.0f)
    {
        return false;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    bool bUpdated = false;
    
    // Update bidirectional connection costs
    if (RegionConnections.Contains(SourceRegionId) && RegionConnections[SourceRegionId].Contains(TargetRegionId))
    {
        RegionConnections[SourceRegionId][TargetRegionId] = NewCost;
        bUpdated = true;
    }
    
    if (RegionConnections.Contains(TargetRegionId) && RegionConnections[TargetRegionId].Contains(SourceRegionId))
    {
        RegionConnections[TargetRegionId][SourceRegionId] = NewCost;
        bUpdated = true;
    }
    
    if (bUpdated)
    {
        // Invalidate cached paths that involve these regions
        TArray<TPair<int32, int32>> InvalidateKeys;
        for (const auto& Entry : RegionPairToPathId)
        {
            if ((Entry.Key.Key == SourceRegionId || Entry.Key.Value == SourceRegionId) ||
                (Entry.Key.Key == TargetRegionId || Entry.Key.Value == TargetRegionId))
            {
                FGuid PathId = Entry.Value;
                PathCache.Remove(PathId);
                InvalidateKeys.Add(Entry.Key);
            }
        }
        
        for (const auto& Key : InvalidateKeys)
        {
            RegionPairToPathId.Remove(Key);
        }
    }
    
    return bUpdated;
}

const FEventDeliveryPath* UEventPathOptimizer::GetOptimalPath(int32 SourceRegionId, int32 TargetRegionId)
{
    if (!bIsInitialized || SourceRegionId == INDEX_NONE || TargetRegionId == INDEX_NONE)
    {
        return nullptr;
    }
    
    // Special case for same region (direct delivery)
    if (SourceRegionId == TargetRegionId)
    {
        FEventDeliveryPath DirectPath;
        DirectPath.SourceRegionId = SourceRegionId;
        DirectPath.TargetRegionId = TargetRegionId;
        DirectPath.bIsDirect = true;
        DirectPath.DeliveryCost = 0.0f;
        DirectPath.LastUsedTimeSeconds = FPlatformTime::Seconds();
        
        FScopeLock Lock(&CriticalSection);
        
        TPair<int32, int32> RegionPair(SourceRegionId, TargetRegionId);
        if (!RegionPairToPathId.Contains(RegionPair))
        {
            RegionPairToPathId.Add(RegionPair, DirectPath.PathId);
            PathCache.Add(DirectPath.PathId, DirectPath);
        }
        else
        {
            FGuid PathId = RegionPairToPathId[RegionPair];
            if (PathCache.Contains(PathId))
            {
                PathCache[PathId].LastUsedTimeSeconds = FPlatformTime::Seconds();
                return &PathCache[PathId];
            }
            else
            {
                RegionPairToPathId[RegionPair] = DirectPath.PathId;
                PathCache.Add(DirectPath.PathId, DirectPath);
            }
        }
        
        return &PathCache[DirectPath.PathId];
    }
    
    FScopeLock Lock(&CriticalSection);
    
    // Check if we have a cached path
    TPair<int32, int32> RegionPair(SourceRegionId, TargetRegionId);
    if (RegionPairToPathId.Contains(RegionPair))
    {
        FGuid PathId = RegionPairToPathId[RegionPair];
        if (PathCache.Contains(PathId))
        {
            FEventDeliveryPath& Path = PathCache[PathId];
            
            // Check if the path is still valid or needs updating
            if (!ShouldUpdatePath(Path))
            {
                Path.LastUsedTimeSeconds = FPlatformTime::Seconds();
                return &Path;
            }
        }
    }
    
    // Calculate a new optimal path
    FEventDeliveryPath OptimalPath = CalculateOptimalPath(SourceRegionId, TargetRegionId);
    
    // Cache the new path
    RegionPairToPathId.Add(RegionPair, OptimalPath.PathId);
    PathCache.Add(OptimalPath.PathId, OptimalPath);
    
    return &PathCache[OptimalPath.PathId];
}

void UEventPathOptimizer::RegisterSubscriber(IEventSubscriber* Subscriber)
{
    if (!bIsInitialized || !Subscriber || !Subscriber->IsInitialized())
    {
        return;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    TWeakInterfacePtr<IEventSubscriber> WeakSubscriber(Subscriber);
    
    if (!SubscriberCache.Contains(WeakSubscriber))
    {
        SubscriberCache.Add(WeakSubscriber, FCachedSubscriberInfo());
        UpdateSubscriberCache(Subscriber);
    }
}

void UEventPathOptimizer::UnregisterSubscriber(IEventSubscriber* Subscriber)
{
    if (!bIsInitialized || !Subscriber)
    {
        return;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    TWeakInterfacePtr<IEventSubscriber> WeakSubscriber(Subscriber);
    
    if (SubscriberCache.Contains(WeakSubscriber))
    {
        SubscriberCache.Remove(WeakSubscriber);
        
        // Rebuild clusters since a subscriber was removed
        BuildSubscriberClusters();
    }
}

void UEventPathOptimizer::UpdateSubscriberCache(IEventSubscriber* Subscriber)
{
    if (!bIsInitialized || !Subscriber || !Subscriber->IsInitialized())
    {
        return;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    TWeakInterfacePtr<IEventSubscriber> WeakSubscriber(Subscriber);
    
    if (!SubscriberCache.Contains(WeakSubscriber))
    {
        SubscriberCache.Add(WeakSubscriber, FCachedSubscriberInfo());
    }
    
    FCachedSubscriberInfo& CachedInfo = SubscriberCache[WeakSubscriber];
    CachedInfo.Subscriber = WeakSubscriber;
    CachedInfo.LastUpdateTimeSeconds = FPlatformTime::Seconds();
    
    // Populate the cached subscriber info using subscription information
    TArray<FSubscriptionInfo> Subscriptions = Subscriber->GetAllSubscriptions();
    
    // Clear existing data
    CachedInfo.RegionIds.Empty();
    CachedInfo.ZoneIds.Empty();
    CachedInfo.EventTypes.Empty();
    CachedInfo.Scopes.Empty();
    
    // Extract information from subscriptions
    for (const FSubscriptionInfo& SubInfo : Subscriptions)
    {
        CachedInfo.EventTypes.Add(SubInfo.EventType);
        
        if (SubInfo.Options.RegionIdFilter != INDEX_NONE)
        {
            CachedInfo.RegionIds.AddUnique(SubInfo.Options.RegionIdFilter);
            
            if (SubInfo.Options.ZoneIdFilter != INDEX_NONE)
            {
                CachedInfo.ZoneIds.AddUnique(TPair<int32, int32>(SubInfo.Options.RegionIdFilter, SubInfo.Options.ZoneIdFilter));
            }
        }
        
        for (const EEventScope& Scope : SubInfo.Options.Scopes)
        {
            CachedInfo.Scopes.AddUnique(Scope);
        }
        
        // Use the highest handler priority
        CachedInfo.Priority = FMath::Max(CachedInfo.Priority, SubInfo.Options.HandlerPriority);
    }
    
    // Rebuild clusters since a subscriber was updated
    BuildSubscriberClusters();
}

TMap<int32, FSubscriberCluster> UEventPathOptimizer::GetSubscriberClusters(const FName& EventType) const
{
    if (!bIsInitialized)
    {
        return TMap<int32, FSubscriberCluster>();
    }
    
    FScopeLock Lock(&CriticalSection);
    
    // Create a filtered map based on the event type
    TMap<int32, FSubscriberCluster> FilteredClusters;
    
    for (const auto& ClusterPair : RegionClusters)
    {
        const FSubscriberCluster& Cluster = ClusterPair.Value;
        
        if (Cluster.EventSubscriberMap.Contains(EventType) && 
            Cluster.EventSubscriberMap[EventType].Num() > 0)
        {
            FilteredClusters.Add(ClusterPair.Key, ClusterPair.Value);
        }
    }
    
    return FilteredClusters;
}

void UEventPathOptimizer::NotifyPathUsed(const FGuid& PathId, float DeliveryTimeMs)
{
    if (!bIsInitialized || !PathId.IsValid() || DeliveryTimeMs <= 0.0f)
    {
        return;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    if (PathCache.Contains(PathId))
    {
        FEventDeliveryPath& Path = PathCache[PathId];
        Path.LastUsedTimeSeconds = FPlatformTime::Seconds();
        
        // Adaptive cost update based on delivery time
        // This is a simplified approach - a real implementation would use more sophisticated
        // feedback mechanisms to adjust path costs
        if (!Path.bIsDirect && Path.IntermediateRegions.Num() > 0)
        {
            // Update costs for connections involved in this path
            int32 PrevRegion = Path.SourceRegionId;
            
            for (int32 NextRegion : Path.IntermediateRegions)
            {
                if (RegionConnections.Contains(PrevRegion) && 
                    RegionConnections[PrevRegion].Contains(NextRegion))
                {
                    // Adjust cost based on delivery time 
                    // (this is a simple implementation - can be improved with more complex algorithms)
                    float CurrentCost = RegionConnections[PrevRegion][NextRegion];
                    float ExpectedCost = Path.DeliveryCost / (Path.IntermediateRegions.Num() + 1);
                    float ActualCost = DeliveryTimeMs / (Path.IntermediateRegions.Num() + 1);
                    
                    // Blend the costs with a damping factor
                    float NewCost = CurrentCost * 0.8f + (ActualCost / ExpectedCost) * CurrentCost * 0.2f;
                    
                    // Update bidirectional costs
                    RegionConnections[PrevRegion][NextRegion] = NewCost;
                    RegionConnections[NextRegion][PrevRegion] = NewCost;
                }
                
                PrevRegion = NextRegion;
            }
            
            // Final connection to target
            if (RegionConnections.Contains(PrevRegion) && 
                RegionConnections[PrevRegion].Contains(Path.TargetRegionId))
            {
                float CurrentCost = RegionConnections[PrevRegion][Path.TargetRegionId];
                float ExpectedCost = Path.DeliveryCost / (Path.IntermediateRegions.Num() + 1);
                float ActualCost = DeliveryTimeMs / (Path.IntermediateRegions.Num() + 1);
                
                float NewCost = CurrentCost * 0.8f + (ActualCost / ExpectedCost) * CurrentCost * 0.2f;
                
                RegionConnections[PrevRegion][Path.TargetRegionId] = NewCost;
                RegionConnections[Path.TargetRegionId][PrevRegion] = NewCost;
            }
        }
    }
}

void UEventPathOptimizer::RebuildPaths()
{
    if (!bIsInitialized)
    {
        return;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    // Clear cached paths
    PathCache.Empty();
    RegionPairToPathId.Empty();
    
    LastRebuildTimeSeconds = FPlatformTime::Seconds();
    
    // Rebuild clusters
    BuildSubscriberClusters();
}

FEventDeliveryPath UEventPathOptimizer::CalculateOptimalPath(int32 SourceRegionId, int32 TargetRegionId) const
{
    FEventDeliveryPath Result;
    Result.SourceRegionId = SourceRegionId;
    Result.TargetRegionId = TargetRegionId;
    Result.LastUsedTimeSeconds = FPlatformTime::Seconds();
    
    // Check if we have connections
    if (!RegionConnections.Contains(SourceRegionId) || !RegionConnections.Contains(TargetRegionId))
    {
        // No connections, just use direct path
        Result.bIsDirect = true;
        Result.DeliveryCost = FLT_MAX; // Very high cost
        return Result;
    }
    
    // Check direct connection
    if (RegionConnections[SourceRegionId].Contains(TargetRegionId))
    {
        Result.bIsDirect = true;
        Result.DeliveryCost = RegionConnections[SourceRegionId][TargetRegionId];
        return Result;
    }
    
    // Implement Dijkstra's algorithm to find shortest path
    TMap<int32, float> Distance;
    TMap<int32, int32> Previous;
    TSet<int32> Unvisited;
    
    // Initialize with all regions
    for (const auto& RegionPair : RegionConnections)
    {
        int32 RegionId = RegionPair.Key;
        Distance.Add(RegionId, FLT_MAX);
        Previous.Add(RegionId, INDEX_NONE);
        Unvisited.Add(RegionId);
    }
    
    // Set source distance to 0
    Distance[SourceRegionId] = 0.0f;
    
    while (Unvisited.Num() > 0)
    {
        // Find region with minimum distance
        int32 CurrentRegion = INDEX_NONE;
        float MinDistance = FLT_MAX;
        
        for (int32 RegionId : Unvisited)
        {
            if (Distance[RegionId] < MinDistance)
            {
                MinDistance = Distance[RegionId];
                CurrentRegion = RegionId;
            }
        }
        
        // If we couldn't find a region or we've found the target, we're done
        if (CurrentRegion == INDEX_NONE || CurrentRegion == TargetRegionId || MinDistance == FLT_MAX)
        {
            break;
        }
        
        // Remove from unvisited
        Unvisited.Remove(CurrentRegion);
        
        // Update neighbors
        for (const auto& Neighbor : RegionConnections[CurrentRegion])
        {
            int32 NeighborId = Neighbor.Key;
            float Cost = Neighbor.Value;
            
            if (Unvisited.Contains(NeighborId))
            {
                float NewDistance = Distance[CurrentRegion] + Cost;
                if (NewDistance < Distance[NeighborId])
                {
                    Distance[NeighborId] = NewDistance;
                    Previous[NeighborId] = CurrentRegion;
                }
            }
        }
    }
    
    // Check if we found a path
    if (Distance[TargetRegionId] == FLT_MAX)
    {
        // No path found
        Result.bIsDirect = true;
        Result.DeliveryCost = FLT_MAX; // Very high cost
        return Result;
    }
    
    // Reconstruct path
    int32 Current = TargetRegionId;
    TArray<int32> Path;
    
    while (Current != SourceRegionId)
    {
        Path.Add(Current);
        Current = Previous[Current];
        
        // Safety check for cycles
        if (Current == INDEX_NONE)
        {
            // Something went wrong
            Result.bIsDirect = true;
            Result.DeliveryCost = FLT_MAX;
            return Result;
        }
    }
    
    // Reverse path since we built it backwards
    Algo::Reverse(Path);
    
    // Remove the target from intermediates
    if (Path.Num() > 0 && Path.Last() == TargetRegionId)
    {
        Path.RemoveAt(Path.Num() - 1);
    }
    
    Result.bIsDirect = Path.Num() == 0;
    Result.IntermediateRegions = Path;
    Result.DeliveryCost = Distance[TargetRegionId];
    
    return Result;
}

void UEventPathOptimizer::BuildSubscriberClusters()
{
    FScopeLock Lock(&CriticalSection);
    
    // Clear existing clusters
    RegionClusters.Empty();
    
    // Group subscribers by region
    for (const auto& CacheEntry : SubscriberCache)
    {
        const FCachedSubscriberInfo& SubInfo = CacheEntry.Value;
        TWeakInterfacePtr<IEventSubscriber> Subscriber = CacheEntry.Key;
        
        // Skip invalid subscribers
        if (!Subscriber.IsValid())
        {
            continue;
        }
        
        // Add to global cluster for global events
        if (SubInfo.Scopes.Contains(EEventScope::Global))
        {
            const int32 GlobalRegionId = -2; // Special ID for global cluster
            
            if (!RegionClusters.Contains(GlobalRegionId))
            {
                FSubscriberCluster GlobalCluster;
                GlobalCluster.RegionId = GlobalRegionId;
                RegionClusters.Add(GlobalRegionId, GlobalCluster);
            }
            
            RegionClusters[GlobalRegionId].Subscribers.AddUnique(Subscriber);
            
            // Add to event map for each event type
            for (const FName& EventType : SubInfo.EventTypes)
            {
                if (!RegionClusters[GlobalRegionId].EventSubscriberMap.Contains(EventType))
                {
                    RegionClusters[GlobalRegionId].EventSubscriberMap.Add(EventType, TArray<TWeakInterfacePtr<IEventSubscriber>>());
                }
                
                RegionClusters[GlobalRegionId].EventSubscriberMap[EventType].AddUnique(Subscriber);
            }
        }
        
        // Add to specific region clusters
        for (int32 RegionId : SubInfo.RegionIds)
        {
            if (!RegionClusters.Contains(RegionId))
            {
                FSubscriberCluster Cluster;
                Cluster.RegionId = RegionId;
                RegionClusters.Add(RegionId, Cluster);
            }
            
            RegionClusters[RegionId].Subscribers.AddUnique(Subscriber);
            
            // Add to event map for each event type
            for (const FName& EventType : SubInfo.EventTypes)
            {
                if (!RegionClusters[RegionId].EventSubscriberMap.Contains(EventType))
                {
                    RegionClusters[RegionId].EventSubscriberMap.Add(EventType, TArray<TWeakInterfacePtr<IEventSubscriber>>());
                }
                
                RegionClusters[RegionId].EventSubscriberMap[EventType].AddUnique(Subscriber);
            }
        }
    }
}

bool UEventPathOptimizer::ShouldUpdatePath(const FEventDeliveryPath& Path) const
{
    double CurrentTime = FPlatformTime::Seconds();
    
    // Update if more than 30 seconds since last rebuild or 2 minutes since last use
    return (CurrentTime - LastRebuildTimeSeconds > 30.0) || 
           (CurrentTime - Path.LastUsedTimeSeconds > 120.0);
}

void UEventPathOptimizer::CleanupStalePaths()
{
    FScopeLock Lock(&CriticalSection);
    
    double CurrentTime = FPlatformTime::Seconds();
    TArray<FGuid> StalePaths;
    
    // Find paths not used for a long time
    for (const auto& PathEntry : PathCache)
    {
        if (CurrentTime - PathEntry.Value.LastUsedTimeSeconds > 300.0) // 5 minutes
        {
            StalePaths.Add(PathEntry.Key);
        }
    }
    
    // Remove stale paths
    for (const FGuid& PathId : StalePaths)
    {
        if (PathCache.Contains(PathId))
        {
            const FEventDeliveryPath& Path = PathCache[PathId];
            
            // Remove mapping
            TPair<int32, int32> RegionPair(Path.SourceRegionId, Path.TargetRegionId);
            if (RegionPairToPathId.Contains(RegionPair) && RegionPairToPathId[RegionPair] == PathId)
            {
                RegionPairToPathId.Remove(RegionPair);
            }
            
            // Remove path
            PathCache.Remove(PathId);
        }
    }
}