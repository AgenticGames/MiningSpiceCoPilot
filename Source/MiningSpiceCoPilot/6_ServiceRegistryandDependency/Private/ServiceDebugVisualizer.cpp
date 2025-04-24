// Copyright Epic Games, Inc. All Rights Reserved.

#include "ServiceDebugVisualizer.h"
#include "ServiceRegistryPCH.h"
#include "ServiceManager.h"
#include "DependencyResolver.h"
#include "ServiceHealthMonitor.h"
#include "Logging/LogMining.h"
#include "Misc/DateTime.h"
#include "Misc/ScopeLock.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Algo/Reverse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Interfaces/IMemoryAwareService.h"
#include "JsonObjectConverter.h"

// Implementation of the constructors that were moved from the header
FServiceVisualizationNode::FServiceVisualizationNode()
    : InterfaceType(nullptr)
    , State(EServiceState::Uninitialized)
    , HealthStatus(EServiceHealthStatus::Healthy)
    , ZoneID(INDEX_NONE)
    , RegionID(INDEX_NONE)
    , bIsHotspot(false)
{
}

FServiceVisualizationEdge::FServiceVisualizationEdge()
    : DependencyType(FDependencyResolver::EDependencyType::Required)
    , bIsActive(true)
    , bIsBottleneck(false)
    , InteractionCount(0)
    , AverageResponseTimeMs(0.0f)
{
}

FServiceVisualizationResult::FServiceVisualizationResult()
    : CreationTime(FPlatformTime::Seconds())
{
}

// Initialize static singleton instance
FServiceDebugVisualizer* FServiceDebugVisualizer::Instance = nullptr;

FServiceDebugVisualizer::FServiceDebugVisualizer()
    : bIsInitialized(false)
    , ServiceManager(nullptr)
    , DependencyResolver(nullptr)
    , HealthMonitor(nullptr)
    , MaxInteractionHistory(1000)
    , MaxMetricsHistoryEntries(100)
{
    // Set the singleton instance
    check(Instance == nullptr);
    Instance = this;
}

FServiceDebugVisualizer::~FServiceDebugVisualizer()
{
    // Shutdown if still running
    if (bIsInitialized)
    {
        Shutdown();
    }
    
    // Clear the singleton instance if it's us
    if (Instance == this)
    {
        Instance = nullptr;
    }
}

FServiceDebugVisualizer& FServiceDebugVisualizer::Get()
{
    // Create instance if needed
    if (Instance == nullptr)
    {
        Instance = new FServiceDebugVisualizer();
    }
    
    return *Instance;
}

bool FServiceDebugVisualizer::Initialize(FServiceManager& InServiceManager, 
    FDependencyResolver& InDependencyResolver,
    FServiceHealthMonitor* InHealthMonitor)
{
    // Already initialized check
    if (bIsInitialized)
    {
        UE_LOG(LogMiningRegistry, Warning, TEXT("FServiceDebugVisualizer::Initialize - Already initialized"));
        return true;
    }
    
    // Store references
    ServiceManager = &InServiceManager;
    DependencyResolver = &InDependencyResolver;
    HealthMonitor = InHealthMonitor;
    
    // Initialize collections
    RecentInteractions.Empty();
    MetricsHistory.Empty();
    
    bIsInitialized = true;
    
    UE_LOG(LogMiningRegistry, Log, TEXT("FServiceDebugVisualizer::Initialize - Debug visualizer initialized"));
    
    return true;
}

void FServiceDebugVisualizer::Shutdown()
{
    if (!bIsInitialized)
    {
        return;
    }
    
    // Clear collections
    RecentInteractions.Empty();
    MetricsHistory.Empty();
    
    // Clear references
    ServiceManager = nullptr;
    DependencyResolver = nullptr;
    HealthMonitor = nullptr;
    
    bIsInitialized = false;
    
    UE_LOG(LogMiningRegistry, Log, TEXT("FServiceDebugVisualizer::Shutdown - Debug visualizer shutdown"));
}

bool FServiceDebugVisualizer::IsInitialized() const
{
    return bIsInitialized;
}

FServiceVisualizationResult FServiceDebugVisualizer::VisualizeServices(const FServiceVisualizationOptions& Options)
{
    FServiceVisualizationResult Result;
    
    if (!bIsInitialized)
    {
        Result.VisualizationString = TEXT("Debug visualizer is not initialized");
        return Result;
    }
    
    // Get all service keys
    TArray<FName> ServiceKeys;
    ServiceManager->GetAllServiceKeys(ServiceKeys);
    
    // Build visualization
    BuildVisualization(ServiceKeys, Options, Result);
    
    return Result;
}

FServiceVisualizationResult FServiceDebugVisualizer::VisualizeService(const FName& ServiceKey, const FServiceVisualizationOptions& Options)
{
    FServiceVisualizationResult Result;
    
    if (!bIsInitialized)
    {
        Result.VisualizationString = TEXT("Debug visualizer is not initialized");
        return Result;
    }
    
    // Create array with single service
    TArray<FName> ServiceKeys;
    ServiceKeys.Add(ServiceKey);
    
    // Build visualization
    BuildVisualization(ServiceKeys, Options, Result);
    
    return Result;
}

FServiceVisualizationResult FServiceDebugVisualizer::VisualizeServices(const TArray<FName>& ServiceKeys, const FServiceVisualizationOptions& Options)
{
    FServiceVisualizationResult Result;
    
    if (!bIsInitialized)
    {
        Result.VisualizationString = TEXT("Debug visualizer is not initialized");
        return Result;
    }
    
    // Build visualization
    BuildVisualization(ServiceKeys, Options, Result);
    
    return Result;
}

FServiceVisualizationResult FServiceDebugVisualizer::VisualizeZoneServices(int32 ZoneID, const FServiceVisualizationOptions& Options)
{
    FServiceVisualizationResult Result;
    
    if (!bIsInitialized)
    {
        Result.VisualizationString = TEXT("Debug visualizer is not initialized");
        return Result;
    }
    
    // Get all service keys
    TArray<FName> AllServiceKeys;
    ServiceManager->GetAllServiceKeys(AllServiceKeys);
    
    // Filter to zone services
    TArray<FName> ZoneServiceKeys;
    for (const FName& ServiceKey : AllServiceKeys)
    {
        // Rename variable to avoid conflict with class member and use appropriate accessor
        TArray<FServiceInstance> AllServices = ServiceManager->GetAllServices();
        const FServiceInstance* ServiceInstance = nullptr;
        
        // Find the service instance for this key
        for (const FServiceInstance& Service : AllServices)
        {
            FName TestKey = ServiceManager->CreateServiceKey(Service.InterfaceType, Service.ZoneID, Service.RegionID);
            if (TestKey == ServiceKey)
            {
                ServiceInstance = &Service;
                break;
            }
        }
        
        if (ServiceInstance && ServiceInstance->ZoneID == ZoneID)
        {
            ZoneServiceKeys.Add(ServiceKey);
        }
    }
    
    // Build visualization
    BuildVisualization(ZoneServiceKeys, Options, Result);
    
    return Result;
}

FServiceVisualizationResult FServiceDebugVisualizer::VisualizeRegionServices(int32 RegionID, const FServiceVisualizationOptions& Options)
{
    FServiceVisualizationResult Result;
    
    if (!bIsInitialized)
    {
        Result.VisualizationString = TEXT("Debug visualizer is not initialized");
        return Result;
    }
    
    // Get all service keys
    TArray<FName> AllServiceKeys;
    ServiceManager->GetAllServiceKeys(AllServiceKeys);
    
    // Filter to region services
    TArray<FName> RegionServiceKeys;
    for (const FName& ServiceKey : AllServiceKeys)
    {
        // Use accessor approach to avoid private method access
        TArray<FServiceInstance> AllServices = ServiceManager->GetAllServices();
        const FServiceInstance* ServiceInstance = nullptr;
        
        // Find the service instance for this key
        for (const FServiceInstance& Service : AllServices)
        {
            FName TestKey = ServiceManager->CreateServiceKey(Service.InterfaceType, Service.ZoneID, Service.RegionID);
            if (TestKey == ServiceKey)
            {
                ServiceInstance = &Service;
                break;
            }
        }
        
        if (ServiceInstance && ServiceInstance->RegionID == RegionID)
        {
            RegionServiceKeys.Add(ServiceKey);
        }
    }
    
    // Build visualization
    BuildVisualization(RegionServiceKeys, Options, Result);
    
    return Result;
}

FServiceVisualizationResult FServiceDebugVisualizer::VisualizeHotspots(const FServiceVisualizationOptions& Options)
{
    FServiceVisualizationOptions HotspotOptions = Options;
    HotspotOptions.bHighlightHotspots = true;
    HotspotOptions.VisualizationType = EServiceVisualizationType::Performance;
    
    return VisualizeServices(HotspotOptions);
}

FServiceVisualizationResult FServiceDebugVisualizer::VisualizeServiceHealth(const FServiceVisualizationOptions& Options)
{
    FServiceVisualizationOptions HealthOptions = Options;
    HealthOptions.bIncludeHealthStatus = true;
    HealthOptions.VisualizationType = EServiceVisualizationType::Health;
    
    return VisualizeServices(HealthOptions);
}

void FServiceDebugVisualizer::RecordServiceInteraction(const FName& SourceKey, const FName& TargetKey, 
    float DurationMs, bool bSuccess)
{
    if (!bIsInitialized)
    {
        return;
    }
    
    FScopeLock Lock(&VisualizerLock);
    
    // Create interaction record
    FServiceInteraction Interaction;
    Interaction.SourceKey = SourceKey;
    Interaction.TargetKey = TargetKey;
    Interaction.Timestamp = FPlatformTime::Seconds();
    Interaction.DurationMs = DurationMs;
    Interaction.bSuccess = bSuccess;
    
    // Add to recent interactions
    RecentInteractions.Insert(Interaction, 0);
    
    // Trim if needed
    if (RecentInteractions.Num() > MaxInteractionHistory)
    {
        RecentInteractions.RemoveAt(MaxInteractionHistory, RecentInteractions.Num() - MaxInteractionHistory);
    }
}

TArray<FServiceInteraction> FServiceDebugVisualizer::GetRecentInteractions(int32 MaxCount) const
{
    FScopeLock Lock(&VisualizerLock);
    
    TArray<FServiceInteraction> Result;
    
    int32 Count = FMath::Min(MaxCount, RecentInteractions.Num());
    for (int32 i = 0; i < Count; ++i)
    {
        Result.Add(RecentInteractions[i]);
    }
    
    return Result;
}

bool FServiceDebugVisualizer::SaveVisualizationToFile(const FServiceVisualizationResult& Visualization, const FString& FilePath) const
{
    if (Visualization.VisualizationString.IsEmpty())
    {
        UE_LOG(LogMiningRegistry, Warning, TEXT("FServiceDebugVisualizer::SaveVisualizationToFile - Empty visualization"));
        return false;
    }
    
    // Ensure directory exists
    FString Directory = FPaths::GetPath(FilePath);
    if (!Directory.IsEmpty())
    {
        IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
        if (!PlatformFile.DirectoryExists(*Directory))
        {
            PlatformFile.CreateDirectoryTree(*Directory);
        }
    }
    
    // Save visualization to file
    bool bSuccess = FFileHelper::SaveStringToFile(Visualization.VisualizationString, *FilePath);
    
    if (bSuccess)
    {
        UE_LOG(LogMiningRegistry, Log, TEXT("FServiceDebugVisualizer::SaveVisualizationToFile - Saved visualization to %s"), *FilePath);
    }
    else
    {
        UE_LOG(LogMiningRegistry, Error, TEXT("FServiceDebugVisualizer::SaveVisualizationToFile - Failed to save visualization to %s"), *FilePath);
    }
    
    return bSuccess;
}

bool FServiceDebugVisualizer::BuildVisualization(const TArray<FName>& ServiceKeys, 
    const FServiceVisualizationOptions& Options,
    FServiceVisualizationResult& OutResult)
{
    if (!bIsInitialized || !ServiceManager || !DependencyResolver)
    {
        OutResult.VisualizationString = TEXT("Debug visualizer is not properly initialized");
        return false;
    }
    
    // Collect service nodes
    TArray<FServiceVisualizationNode> ServiceNodes;
    if (!CollectServiceNodes(ServiceKeys, Options, ServiceNodes))
    {
        OutResult.VisualizationString = TEXT("Failed to collect service nodes");
        return false;
    }
    
    // Store in result
    OutResult.Services = ServiceNodes;
    
    // Collect dependencies if needed
    if (Options.VisualizationType == EServiceVisualizationType::Dependencies || 
        Options.VisualizationType == EServiceVisualizationType::Interactions)
    {
        TArray<FServiceVisualizationEdge> Dependencies;
        if (!CollectServiceDependencies(ServiceNodes, Options, Dependencies))
        {
            UE_LOG(LogMiningRegistry, Warning, TEXT("FServiceDebugVisualizer::BuildVisualization - Failed to collect dependencies"));
            // Continue anyway, we can still visualize services without dependencies
        }
        
        // Store in result
        OutResult.Dependencies = Dependencies;
    }
    
    // Collect interaction data if needed
    if (Options.VisualizationType == EServiceVisualizationType::Interactions)
    {
        TArray<FServiceInteraction> Interactions;
        if (!CollectInteractionData(ServiceKeys, Options, Interactions))
        {
            UE_LOG(LogMiningRegistry, Warning, TEXT("FServiceDebugVisualizer::BuildVisualization - Failed to collect interaction data"));
            // Continue anyway
        }
        
        // Store in result
        OutResult.Interactions = Interactions;
    }
    
    // Identify hotspots if requested
    if (Options.bHighlightHotspots)
    {
        OutResult.Hotspots = IdentifyHotspots(OutResult.Services, OutResult.Dependencies, Options.HotspotConfig);
    }
    
    // Generate visualization based on format
    switch (Options.Format)
    {
        case EServiceVisualizationFormat::DOT:
            OutResult.VisualizationString = GenerateDOTVisualization(OutResult, Options);
            break;
            
        case EServiceVisualizationFormat::JSON:
            OutResult.VisualizationString = GenerateJSONVisualization(OutResult, Options);
            break;
            
        case EServiceVisualizationFormat::Text:
            OutResult.VisualizationString = GenerateTextVisualization(OutResult, Options);
            break;
            
        default:
            OutResult.VisualizationString = TEXT("Unsupported visualization format");
            return false;
    }
    
    return true;
}

bool FServiceDebugVisualizer::CollectServiceNodes(const TArray<FName>& ServiceKeys,
    const FServiceVisualizationOptions& Options,
    TArray<FServiceVisualizationNode>& OutNodes)
{
    if (!ServiceManager)
    {
        return false;
    }
    
    // Get all services once for efficiency
    TArray<FServiceInstance> AllServices = ServiceManager->GetAllServices();
    
    for (const FName& ServiceKey : ServiceKeys)
    {
        // Get service instance
        const FServiceInstance* ServiceInstance = nullptr;
        
        // Find the service instance for this key
        for (const FServiceInstance& Service : AllServices)
        {
            FName TestKey = ServiceManager->CreateServiceKey(Service.InterfaceType, Service.ZoneID, Service.RegionID);
            if (TestKey == ServiceKey)
            {
                ServiceInstance = &Service;
                break;
            }
        }
        
        if (!ServiceInstance)
        {
            UE_LOG(LogMiningRegistry, Warning, TEXT("FServiceDebugVisualizer::CollectServiceNodes - Service not found: %s"), 
                *ServiceKey.ToString());
            continue;
        }
        
        // Skip inactive services unless specifically requested
        if (!Options.bIncludeInactiveServices && 
            ServiceInstance->State != EServiceState::Active && 
            ServiceInstance->State != EServiceState::Initializing)
        {
            continue;
        }
        
        // Create visualization node
        FServiceVisualizationNode Node;
        Node.ServiceKey = ServiceKey;
        Node.InterfaceType = const_cast<UClass*>(ServiceInstance->InterfaceType);
        Node.State = ServiceInstance->State;
        Node.ZoneID = ServiceInstance->ZoneID;
        Node.RegionID = ServiceInstance->RegionID;
        
        // Copy metrics fields manually instead of using operator=
        Node.Metrics.SuccessfulOperations.Set(ServiceInstance->Metrics.SuccessfulOperations.GetValue());
        Node.Metrics.FailedOperations.Set(ServiceInstance->Metrics.FailedOperations.GetValue());
        Node.Metrics.TotalOperationTimeMs.Set(ServiceInstance->Metrics.TotalOperationTimeMs.GetValue());
        Node.Metrics.MaxOperationTimeMs.Set(ServiceInstance->Metrics.MaxOperationTimeMs.GetValue());
        Node.Metrics.MemoryUsageBytes.Set(ServiceInstance->Metrics.MemoryUsageBytes.GetValue());
        Node.Metrics.ActiveInstances.Set(ServiceInstance->Metrics.ActiveInstances.GetValue());
        Node.Metrics.LastHealthCheckTime = ServiceInstance->Metrics.LastHealthCheckTime;
        Node.Metrics.LastFailureTime = ServiceInstance->Metrics.LastFailureTime;
        Node.Metrics.LastRecoveryTime = ServiceInstance->Metrics.LastRecoveryTime;
        
        // Get health status if available
        if (Options.bIncludeHealthStatus && HealthMonitor)
        {
            Node.HealthStatus = HealthMonitor->GetServiceHealthStatus(ServiceKey);
        }
        
        // Get historical metrics if requested
        if (Options.HistoricalTimeRangeSec > 0.0f)
        {
            Node.HistoricalMetrics = GetMetricsHistory(ServiceKey, Options.HistoricalTimeRangeSec);
        }
        
        // Add to result
        OutNodes.Add(Node);
    }
    
    return true;
}

bool FServiceDebugVisualizer::CollectServiceDependencies(const TArray<FServiceVisualizationNode>& ServiceNodes,
    const FServiceVisualizationOptions& Options,
    TArray<FServiceVisualizationEdge>& OutEdges)
{
    if (!ServiceManager || !DependencyResolver)
    {
        return false;
    }
    
    // Build a map of service keys to indices
    TMap<FName, int32> ServiceKeyToIndex;
    for (int32 i = 0; i < ServiceNodes.Num(); ++i)
    {
        ServiceKeyToIndex.Add(ServiceNodes[i].ServiceKey, i);
    }
    
    // Process each node
    for (int32 i = 0; i < ServiceNodes.Num(); ++i)
    {
        const FServiceVisualizationNode& Node = ServiceNodes[i];
        
        // Get dependency node ID
        uint32 NodeId = DependencyResolver->GetNodeIdByName(Node.ServiceKey);
        if (NodeId == 0)
        {
            // No dependency info for this service
            continue;
        }
        
        // Get dependencies
        TArray<FDependencyResolver::FDependencyEdge> Dependencies = DependencyResolver->GetDependencies(NodeId);
        if (Dependencies.Num() > 0)
        {
            for (const FDependencyResolver::FDependencyEdge& Dependency : Dependencies)
            {
                // Get target node name
                FName TargetName = DependencyResolver->GetNodeNameById(Dependency.TargetId);
                if (TargetName.IsNone())
                {
                    // Invalid target
                    continue;
                }
                
                // Skip if target is not in our visualization
                if (!ServiceKeyToIndex.Contains(TargetName))
                {
                    continue;
                }
                
                // Create edge
                FServiceVisualizationEdge Edge;
                Edge.SourceKey = Node.ServiceKey;
                Edge.TargetKey = TargetName;
                Edge.DependencyType = Dependency.Type;
                Edge.bIsActive = Dependency.bIsActive;
                
                // Add to result
                OutEdges.Add(Edge);
            }
        }
    }
    
    // Count interactions if available
    if (Options.VisualizationType == EServiceVisualizationType::Interactions)
    {
        FScopeLock Lock(&VisualizerLock);
        
        // Build a map of source->target to edge index
        TMap<TPair<FName, FName>, int32> EdgeMap;
        for (int32 i = 0; i < OutEdges.Num(); ++i)
        {
            TPair<FName, FName> Key(OutEdges[i].SourceKey, OutEdges[i].TargetKey);
            EdgeMap.Add(Key, i);
        }
        
        // Count interactions
        for (const FServiceInteraction& Interaction : RecentInteractions)
        {
            TPair<FName, FName> Key(Interaction.SourceKey, Interaction.TargetKey);
            if (EdgeMap.Contains(Key))
            {
                int32 EdgeIndex = EdgeMap[Key];
                OutEdges[EdgeIndex].InteractionCount++;
                
                // Update average response time
                float OldAvg = OutEdges[EdgeIndex].AverageResponseTimeMs;
                int32 OldCount = OutEdges[EdgeIndex].InteractionCount - 1;
                
                if (OldCount <= 0)
                {
                    OutEdges[EdgeIndex].AverageResponseTimeMs = Interaction.DurationMs;
                }
                else
                {
                    OutEdges[EdgeIndex].AverageResponseTimeMs = 
                        (OldAvg * OldCount + Interaction.DurationMs) / OutEdges[EdgeIndex].InteractionCount;
                }
            }
        }
    }
    
    return true;
}

bool FServiceDebugVisualizer::CollectInteractionData(const TArray<FName>& ServiceKeys,
    const FServiceVisualizationOptions& Options,
    TArray<FServiceInteraction>& OutInteractions)
{
    FScopeLock Lock(&VisualizerLock);
    
    // Build a set of service keys for fast lookup
    TSet<FName> ServiceKeySet;
    for (const FName& Key : ServiceKeys)
    {
        ServiceKeySet.Add(Key);
    }
    
    // Filter interactions to those involving these services
    for (const FServiceInteraction& Interaction : RecentInteractions)
    {
        if (ServiceKeySet.Contains(Interaction.SourceKey) || 
            ServiceKeySet.Contains(Interaction.TargetKey))
        {
            OutInteractions.Add(Interaction);
        }
    }
    
    return true;
}

FString FServiceDebugVisualizer::GetStateColor(EServiceState State) const
{
    switch (State)
    {
        case EServiceState::Active:
            return TEXT("#32CD32"); // Lime Green
            
        case EServiceState::Initializing:
            return TEXT("#00BFFF"); // Deep Sky Blue
            
        case EServiceState::Failing:
            return TEXT("#FF4500"); // Orange Red
            
        case EServiceState::ShuttingDown:
            return TEXT("#DAA520"); // Goldenrod
            
        case EServiceState::Destroyed:
            return TEXT("#A9A9A9"); // Dark Gray
            
        case EServiceState::Uninitialized:
        default:
            return TEXT("#808080"); // Gray
    }
}

FString FServiceDebugVisualizer::GetHealthStatusColor(EServiceHealthStatus Status) const
{
    switch (Status)
    {
        case EServiceHealthStatus::Healthy:
            return TEXT("#32CD32"); // Lime Green
            
        case EServiceHealthStatus::Degraded:
            return TEXT("#FFCC00"); // Yellow
            
        case EServiceHealthStatus::Critical:
            return TEXT("#FF8C00"); // Dark Orange
            
        case EServiceHealthStatus::Failed:
            return TEXT("#FF4500"); // Orange Red
            
        case EServiceHealthStatus::Unresponsive:
            return TEXT("#FF0000"); // Red
            
        case EServiceHealthStatus::Unknown:
            return TEXT("#808080"); // Gray
            
        case EServiceHealthStatus::NotRegistered:
            return TEXT("#C0C0C0"); // Silver
            
        case EServiceHealthStatus::Recovering:
            return TEXT("#4682B4"); // Steel Blue
            
        default:
            return TEXT("#808080"); // Gray
    }
}

void FServiceDebugVisualizer::UpdateMetricsHistory(const FName& ServiceKey, const FServiceMetrics& Metrics)
{
    if (!bIsInitialized)
    {
        return;
    }
    
    FScopeLock Lock(&VisualizerLock);
    
    // Create metrics history for this service if needed
    if (!MetricsHistory.Contains(ServiceKey))
    {
        MetricsHistory.Add(ServiceKey, TArray<TPair<double, FServiceMetrics>>());
    }
    
    // Add metrics with timestamp
    TPair<double, FServiceMetrics> TimestampedMetrics(FPlatformTime::Seconds(), Metrics);
    MetricsHistory[ServiceKey].Insert(TimestampedMetrics, 0);
    
    // Trim if needed
    if (MetricsHistory[ServiceKey].Num() > MaxMetricsHistoryEntries)
    {
        MetricsHistory[ServiceKey].RemoveAt(MaxMetricsHistoryEntries, 
            MetricsHistory[ServiceKey].Num() - MaxMetricsHistoryEntries);
    }
}

TArray<TPair<double, FServiceMetrics>> FServiceDebugVisualizer::GetMetricsHistory(const FName& ServiceKey, float TimeRangeSec) const
{
    FScopeLock Lock(&VisualizerLock);
    
    TArray<TPair<double, FServiceMetrics>> Result;
    
    if (!MetricsHistory.Contains(ServiceKey))
    {
        return Result;
    }
    
    // If no time range specified, return all history
    if (TimeRangeSec <= 0.0f)
    {
        return MetricsHistory[ServiceKey];
    }
    
    // Filter by time range
    double CurrentTime = FPlatformTime::Seconds();
    double MinTime = CurrentTime - TimeRangeSec;
    
    for (const TPair<double, FServiceMetrics>& TimestampedMetrics : MetricsHistory[ServiceKey])
    {
        if (TimestampedMetrics.Key >= MinTime)
        {
            Result.Add(TimestampedMetrics);
        }
        else
        {
            // History is sorted by time, so we can stop once we're out of range
            break;
        }
    }
    
    return Result;
}

TArray<FName> FServiceDebugVisualizer::IdentifyHotspots(TArray<FServiceVisualizationNode>& Services, 
    const TArray<FServiceVisualizationEdge>& Dependencies, 
    const FHotspotConfig& Config) const
{
    TArray<FName> Hotspots;
    
    // Check each service for hotspot criteria
    for (FServiceVisualizationNode& Node : Services)
    {
        bool bIsHotspot = false;
        FString HotspotReason;
        
        // Check failure rate
        int64 TotalOps = Node.Metrics.SuccessfulOperations.GetValue() + Node.Metrics.FailedOperations.GetValue();
        if (TotalOps > 0)
        {
            float FailureRate = static_cast<float>(Node.Metrics.FailedOperations.GetValue()) / static_cast<float>(TotalOps);
            
            if (FailureRate >= Config.FailureRateThreshold)
            {
                bIsHotspot = true;
                HotspotReason = FString::Printf(TEXT("High failure rate (%.1f%%)"), FailureRate * 100.0f);
            }
        }
        
        // Check memory usage (convert bytes to MB)
        float MemoryUsageMB = static_cast<float>(Node.Metrics.MemoryUsageBytes.GetValue()) / (1024.0f * 1024.0f);
        if (MemoryUsageMB >= Config.MemoryUsageThresholdMB)
        {
            bIsHotspot = true;
            if (!HotspotReason.IsEmpty())
            {
                HotspotReason += TEXT(", ");
            }
            HotspotReason += FString::Printf(TEXT("High memory usage (%.1f MB)"), MemoryUsageMB);
        }
        
        // Check average response time if we have operations
        if (TotalOps > 0)
        {
            float AvgResponseTime = static_cast<float>(Node.Metrics.TotalOperationTimeMs.GetValue()) / static_cast<float>(TotalOps);
            
            if (AvgResponseTime >= Config.ResponseTimeThresholdMs)
            {
                bIsHotspot = true;
                if (!HotspotReason.IsEmpty())
                {
                    HotspotReason += TEXT(", ");
                }
                HotspotReason += FString::Printf(TEXT("High response time (%.1f ms)"), AvgResponseTime);
            }
        }
        
        // Check max operation time
        float MaxOpTime = static_cast<float>(Node.Metrics.MaxOperationTimeMs.GetValue());
        if (MaxOpTime >= Config.ResponseTimeThresholdMs * 2.0f)
        {
            bIsHotspot = true;
            if (!HotspotReason.IsEmpty())
            {
                HotspotReason += TEXT(", ");
            }
            HotspotReason += FString::Printf(TEXT("High max operation time (%.1f ms)"), MaxOpTime);
        }
        
        // Mark as hotspot if criteria met
        if (bIsHotspot)
        {
            Node.bIsHotspot = true;
            Node.HotspotReason = HotspotReason;
            Hotspots.Add(Node.ServiceKey);
        }
    }
    
    // Now check for bottleneck dependencies
    for (int32 i = 0; i < Dependencies.Num(); ++i)
    {
        const FServiceVisualizationEdge& Edge = Dependencies[i];
        
        // Only consider active dependencies
        if (!Edge.bIsActive)
        {
            continue;
        }
        
        // Check for high average response time
        if (Edge.AverageResponseTimeMs >= Config.ResponseTimeThresholdMs)
        {
            // Find the target node and mark as hotspot if not already
            for (FServiceVisualizationNode& Node : Services)
            {
                if (Node.ServiceKey == Edge.TargetKey && !Node.bIsHotspot)
                {
                    Node.bIsHotspot = true;
                    Node.HotspotReason = FString::Printf(TEXT("Dependency bottleneck (%.1f ms average response)"), 
                        Edge.AverageResponseTimeMs);
                    Hotspots.AddUnique(Node.ServiceKey);
                    break;
                }
            }
        }
    }
    
    return Hotspots;
}

FString FServiceDebugVisualizer::GenerateDOTVisualization(const FServiceVisualizationResult& Result, const FServiceVisualizationOptions& Options) const
{
    FString DOT;
    
    // Graph header
    DOT += TEXT("digraph ServiceDependencies {\n");
    DOT += TEXT("  rankdir=LR;\n");
    DOT += TEXT("  node [shape=box, style=filled, fontname=\"Arial\"];\n");
    DOT += TEXT("  edge [fontname=\"Arial\"];\n\n");
    
    // Group services by zone/region if requested
    if (Options.bGroupByZone)
    {
        // Create map of zones to services
        TMap<int32, TArray<int32>> ZoneToServices;
        
        for (int32 i = 0; i < Result.Services.Num(); ++i)
        {
            const FServiceVisualizationNode& Node = Result.Services[i];
            
            if (Node.ZoneID != INDEX_NONE)
            {
                if (!ZoneToServices.Contains(Node.ZoneID))
                {
                    ZoneToServices.Add(Node.ZoneID, TArray<int32>());
                }
                
                ZoneToServices[Node.ZoneID].Add(i);
            }
        }
        
        // Create subgraphs for each zone
        for (const auto& Pair : ZoneToServices)
        {
            DOT += FString::Printf(TEXT("  subgraph cluster_zone_%d {\n"), Pair.Key);
            DOT += TEXT("    style=filled;\n");
            DOT += TEXT("    color=lightgrey;\n");
            DOT += FString::Printf(TEXT("    label=\"Zone %d\";\n"), Pair.Key);
            
            // Add services in this zone
            for (int32 ServiceIndex : Pair.Value)
            {
                const FServiceVisualizationNode& Node = Result.Services[ServiceIndex];
                AddServiceNodeToDOT(DOT, Node, Options);
            }
            
            DOT += TEXT("  }\n\n");
        }
        
        // Add services not in any zone
        DOT += TEXT("  subgraph cluster_no_zone {\n");
        DOT += TEXT("    style=filled;\n");
        DOT += TEXT("    color=white;\n");
        DOT += TEXT("    label=\"No Zone\";\n");
        
        for (int32 i = 0; i < Result.Services.Num(); ++i)
        {
            const FServiceVisualizationNode& Node = Result.Services[i];
            
            if (Node.ZoneID == INDEX_NONE)
            {
                AddServiceNodeToDOT(DOT, Node, Options);
            }
        }
        
        DOT += TEXT("  }\n\n");
    }
    else if (Options.bGroupByRegion)
    {
        // Create map of regions to services
        TMap<int32, TArray<int32>> RegionToServices;
        
        for (int32 i = 0; i < Result.Services.Num(); ++i)
        {
            const FServiceVisualizationNode& Node = Result.Services[i];
            
            if (Node.RegionID != INDEX_NONE)
            {
                if (!RegionToServices.Contains(Node.RegionID))
                {
                    RegionToServices.Add(Node.RegionID, TArray<int32>());
                }
                
                RegionToServices[Node.RegionID].Add(i);
            }
        }
        
        // Create subgraphs for each region
        for (const auto& Pair : RegionToServices)
        {
            DOT += FString::Printf(TEXT("  subgraph cluster_region_%d {\n"), Pair.Key);
            DOT += TEXT("    style=filled;\n");
            DOT += TEXT("    color=lightgrey;\n");
            DOT += FString::Printf(TEXT("    label=\"Region %d\";\n"), Pair.Key);
            
            // Add services in this region
            for (int32 ServiceIndex : Pair.Value)
            {
                const FServiceVisualizationNode& Node = Result.Services[ServiceIndex];
                AddServiceNodeToDOT(DOT, Node, Options);
            }
            
            DOT += TEXT("  }\n\n");
        }
        
        // Add services not in any region
        DOT += TEXT("  subgraph cluster_no_region {\n");
        DOT += TEXT("    style=filled;\n");
        DOT += TEXT("    color=white;\n");
        DOT += TEXT("    label=\"No Region\";\n");
        
        for (int32 i = 0; i < Result.Services.Num(); ++i)
        {
            const FServiceVisualizationNode& Node = Result.Services[i];
            
            if (Node.RegionID == INDEX_NONE)
            {
                AddServiceNodeToDOT(DOT, Node, Options);
            }
        }
        
        DOT += TEXT("  }\n\n");
    }
    else
    {
        // Add all services
        for (const FServiceVisualizationNode& Node : Result.Services)
        {
            AddServiceNodeToDOT(DOT, Node, Options);
        }
        
        DOT += TEXT("\n");
    }
    
    // Add dependencies
    for (const FServiceVisualizationEdge& Edge : Result.Dependencies)
    {
        AddDependencyEdgeToDOT(DOT, Edge, Options);
    }
    
    // Close graph
    DOT += TEXT("}\n");
    
    return DOT;
}

void FServiceDebugVisualizer::AddServiceNodeToDOT(FString& DOT, const FServiceVisualizationNode& Node, const FServiceVisualizationOptions& Options) const
{
    // Node name
    FString NodeName = Node.ServiceKey.ToString().Replace(TEXT("_"), TEXT("")); // Remove underscores for DOT
    
    // Node label
    FString NodeLabel = Node.ServiceKey.ToString();
    
    // Include more details if requested
    if (Options.bIncludeServiceDetails)
    {
        if (Node.InterfaceType)
        {
            NodeLabel += FString::Printf(TEXT("\\nType: %s"), *Node.InterfaceType->GetName());
        }
        
        if (Node.ZoneID != INDEX_NONE)
        {
            NodeLabel += FString::Printf(TEXT("\\nZone: %d"), Node.ZoneID);
        }
        
        if (Node.RegionID != INDEX_NONE)
        {
            NodeLabel += FString::Printf(TEXT("\\nRegion: %d"), Node.RegionID);
        }
    }
    
    // Include metrics if requested
    if (Options.bIncludePerformanceMetrics)
    {
        int64 TotalOps = Node.Metrics.SuccessfulOperations.GetValue() + Node.Metrics.FailedOperations.GetValue();
        
        if (TotalOps > 0)
        {
            float SuccessRate = 100.0f * static_cast<float>(Node.Metrics.SuccessfulOperations.GetValue()) / static_cast<float>(TotalOps);
            
            NodeLabel += FString::Printf(TEXT("\\nSuccess: %.1f%% (%lld/%lld)"),
                SuccessRate,
                Node.Metrics.SuccessfulOperations.GetValue(),
                TotalOps);
                
            float AvgTime = 0.0f;
            if (TotalOps > 0)
            {
                AvgTime = static_cast<float>(Node.Metrics.TotalOperationTimeMs.GetValue()) / static_cast<float>(TotalOps);
            }
            
            NodeLabel += FString::Printf(TEXT("\\nAvg: %.1f ms, Max: %lld ms"),
                AvgTime,
                Node.Metrics.MaxOperationTimeMs.GetValue());
                
            float MemoryMB = static_cast<float>(Node.Metrics.MemoryUsageBytes.GetValue()) / (1024.0f * 1024.0f);
            
            NodeLabel += FString::Printf(TEXT("\\nMemory: %.1f MB"),
                MemoryMB);
        }
    }
    
    // Include health status if requested
    if (Options.bIncludeHealthStatus)
    {
        FString StatusText;
        
        switch (Node.HealthStatus)
        {
            case EServiceHealthStatus::Healthy:
                StatusText = TEXT("Healthy");
                break;
                
            case EServiceHealthStatus::Degraded:
                StatusText = TEXT("Degraded");
                break;
                
            case EServiceHealthStatus::Critical:
                StatusText = TEXT("Critical");
                break;
                
            case EServiceHealthStatus::Failed:
                StatusText = TEXT("Failed");
                break;
                
            case EServiceHealthStatus::Unresponsive:
                StatusText = TEXT("Unresponsive");
                break;
                
            case EServiceHealthStatus::Unknown:
                StatusText = TEXT("Unknown");
                break;
                
            case EServiceHealthStatus::NotRegistered:
                StatusText = TEXT("Not Registered");
                break;
                
            case EServiceHealthStatus::Recovering:
                StatusText = TEXT("Recovering");
                break;
                
            default:
                StatusText = TEXT("Unknown");
                break;
        }
        
        NodeLabel += FString::Printf(TEXT("\\nHealth: %s"), *StatusText);
    }
    
    // Add hotspot reason if applicable
    if (Node.bIsHotspot && !Node.HotspotReason.IsEmpty())
    {
        NodeLabel += FString::Printf(TEXT("\\nHotspot: %s"), *Node.HotspotReason);
    }
    
    // Determine node color based on health or state
    FString FillColor;
    
    if (Options.bIncludeHealthStatus)
    {
        FillColor = GetHealthStatusColor(Node.HealthStatus);
    }
    else
    {
        FillColor = GetStateColor(Node.State);
    }
    
    // Darker border for hotspots
    FString BorderColor = Node.bIsHotspot ? TEXT("red") : TEXT("black");
    float PenWidth = Node.bIsHotspot ? 3.0f : 1.0f;
    
    // Add node to DOT
    DOT += FString::Printf(TEXT("  \"%s\" [label=\"%s\", fillcolor=\"%s\", color=\"%s\", penwidth=%.1f];\n"),
        *NodeName, *NodeLabel, *FillColor, *BorderColor, PenWidth);
}

void FServiceDebugVisualizer::AddDependencyEdgeToDOT(FString& DOT, const FServiceVisualizationEdge& Edge, const FServiceVisualizationOptions& Options) const
{
    // Source and target node names
    FString SourceName = Edge.SourceKey.ToString().Replace(TEXT("_"), TEXT(""));
    FString TargetName = Edge.TargetKey.ToString().Replace(TEXT("_"), TEXT(""));
    
    // Edge style based on dependency type
    FString EdgeStyle;
    FString EdgeColor = TEXT("black");
    float PenWidth = 1.0f;
    
    switch (Edge.DependencyType)
    {
        case FDependencyResolver::EDependencyType::Required:
            EdgeStyle = TEXT("solid");
            break;
            
        case FDependencyResolver::EDependencyType::Optional:
            EdgeStyle = TEXT("dashed");
            break;
            
        default:
            EdgeStyle = TEXT("dotted");
            break;
    }
    
    // Inactive dependencies are lighter
    if (!Edge.bIsActive)
    {
        EdgeColor = TEXT("gray");
    }
    
    // Bottlenecks are highlighted
    if (Edge.bIsBottleneck)
    {
        EdgeColor = TEXT("red");
        PenWidth = 2.0f;
    }
    
    // Edge label
    FString EdgeLabel;
    
    // Add interaction details for interaction visualization
    if (Options.VisualizationType == EServiceVisualizationType::Interactions && Edge.InteractionCount > 0)
    {
        EdgeLabel = FString::Printf(TEXT("%d calls (%.1f ms avg)"),
            Edge.InteractionCount, Edge.AverageResponseTimeMs);
    }
    
    // Add edge to DOT
    if (EdgeLabel.IsEmpty())
    {
        DOT += FString::Printf(TEXT("  \"%s\" -> \"%s\" [style=%s, color=\"%s\", penwidth=%.1f];\n"),
            *SourceName, *TargetName, *EdgeStyle, *EdgeColor, PenWidth);
    }
    else
    {
        DOT += FString::Printf(TEXT("  \"%s\" -> \"%s\" [label=\"%s\", style=%s, color=\"%s\", penwidth=%.1f];\n"),
            *SourceName, *TargetName, *EdgeLabel, *EdgeStyle, *EdgeColor, PenWidth);
    }
}

FString FServiceDebugVisualizer::GenerateJSONVisualization(const FServiceVisualizationResult& Result, const FServiceVisualizationOptions& Options) const
{
    TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);
    
    // Add metadata
    TSharedPtr<FJsonObject> MetadataObject = MakeShareable(new FJsonObject);
    MetadataObject->SetStringField(TEXT("GeneratedAt"), FDateTime::UtcNow().ToString());
    MetadataObject->SetStringField(TEXT("VisualizationType"), GetEnumString(Options.VisualizationType));
    RootObject->SetObjectField(TEXT("Metadata"), MetadataObject);
    
    // Add services array
    TArray<TSharedPtr<FJsonValue>> ServicesArray;
    
    for (const FServiceVisualizationNode& Node : Result.Services)
    {
        TSharedPtr<FJsonObject> ServiceObject = MakeShareable(new FJsonObject);
        
        ServiceObject->SetStringField(TEXT("ServiceKey"), Node.ServiceKey.ToString());
        
        if (Node.InterfaceType)
        {
            ServiceObject->SetStringField(TEXT("InterfaceType"), Node.InterfaceType->GetName());
        }
        
        ServiceObject->SetStringField(TEXT("State"), GetEnumString(Node.State));
        
        if (Options.bIncludeHealthStatus)
        {
            ServiceObject->SetStringField(TEXT("HealthStatus"), GetEnumString(Node.HealthStatus));
        }
        
        if (Node.ZoneID != INDEX_NONE)
        {
            ServiceObject->SetNumberField(TEXT("ZoneID"), Node.ZoneID);
        }
        
        if (Node.RegionID != INDEX_NONE)
        {
            ServiceObject->SetNumberField(TEXT("RegionID"), Node.RegionID);
        }
        
        // Add metrics if requested
        if (Options.bIncludePerformanceMetrics)
        {
            TSharedPtr<FJsonObject> MetricsObject = MakeShareable(new FJsonObject);
            
            MetricsObject->SetNumberField(TEXT("SuccessfulOperations"), Node.Metrics.SuccessfulOperations.GetValue());
            MetricsObject->SetNumberField(TEXT("FailedOperations"), Node.Metrics.FailedOperations.GetValue());
            MetricsObject->SetNumberField(TEXT("TotalOperationTimeMs"), Node.Metrics.TotalOperationTimeMs.GetValue());
            MetricsObject->SetNumberField(TEXT("MaxOperationTimeMs"), Node.Metrics.MaxOperationTimeMs.GetValue());
            MetricsObject->SetNumberField(TEXT("MemoryUsageBytes"), Node.Metrics.MemoryUsageBytes.GetValue());
            MetricsObject->SetNumberField(TEXT("ActiveInstances"), Node.Metrics.ActiveInstances.GetValue());
            MetricsObject->SetNumberField(TEXT("LastHealthCheckTime"), Node.Metrics.LastHealthCheckTime);
            MetricsObject->SetNumberField(TEXT("LastFailureTime"), Node.Metrics.LastFailureTime);
            MetricsObject->SetNumberField(TEXT("LastRecoveryTime"), Node.Metrics.LastRecoveryTime);
            
            ServiceObject->SetObjectField(TEXT("Metrics"), MetricsObject);
            
            // Add historical metrics if available
            if (Node.HistoricalMetrics.Num() > 0 && Options.HistoricalTimeRangeSec > 0.0f)
            {
                TArray<TSharedPtr<FJsonValue>> HistoryArray;
                
                for (const TPair<double, FServiceMetrics>& HistoryEntry : Node.HistoricalMetrics)
                {
                    TSharedPtr<FJsonObject> HistoryObject = MakeShareable(new FJsonObject);
                    
                    HistoryObject->SetNumberField(TEXT("Timestamp"), HistoryEntry.Key);
                    HistoryObject->SetNumberField(TEXT("SuccessfulOperations"), HistoryEntry.Value.SuccessfulOperations.GetValue());
                    HistoryObject->SetNumberField(TEXT("FailedOperations"), HistoryEntry.Value.FailedOperations.GetValue());
                    HistoryObject->SetNumberField(TEXT("TotalOperationTimeMs"), HistoryEntry.Value.TotalOperationTimeMs.GetValue());
                    HistoryObject->SetNumberField(TEXT("MaxOperationTimeMs"), HistoryEntry.Value.MaxOperationTimeMs.GetValue());
                    HistoryObject->SetNumberField(TEXT("MemoryUsageBytes"), HistoryEntry.Value.MemoryUsageBytes.GetValue());
                    
                    HistoryArray.Add(MakeShareable(new FJsonValueObject(HistoryObject)));
                }
                
                ServiceObject->SetArrayField(TEXT("History"), HistoryArray);
            }
        }
        
        if (Node.bIsHotspot)
        {
            ServiceObject->SetBoolField(TEXT("IsHotspot"), true);
            ServiceObject->SetStringField(TEXT("HotspotReason"), Node.HotspotReason);
        }
        
        ServicesArray.Add(MakeShareable(new FJsonValueObject(ServiceObject)));
    }
    
    RootObject->SetArrayField(TEXT("Services"), ServicesArray);
    
    // Add dependencies array
    TArray<TSharedPtr<FJsonValue>> DependenciesArray;
    
    for (const FServiceVisualizationEdge& Edge : Result.Dependencies)
    {
        TSharedPtr<FJsonObject> EdgeObject = MakeShareable(new FJsonObject);
        
        EdgeObject->SetStringField(TEXT("SourceKey"), Edge.SourceKey.ToString());
        EdgeObject->SetStringField(TEXT("TargetKey"), Edge.TargetKey.ToString());
        EdgeObject->SetStringField(TEXT("DependencyType"), 
            Edge.DependencyType == FDependencyResolver::EDependencyType::Required ? TEXT("Required") : TEXT("Optional"));
        EdgeObject->SetBoolField(TEXT("IsActive"), Edge.bIsActive);
        EdgeObject->SetBoolField(TEXT("IsBottleneck"), Edge.bIsBottleneck);
        
        if (Edge.InteractionCount > 0)
        {
            EdgeObject->SetNumberField(TEXT("InteractionCount"), Edge.InteractionCount);
            EdgeObject->SetNumberField(TEXT("AverageResponseTimeMs"), Edge.AverageResponseTimeMs);
        }
        
        DependenciesArray.Add(MakeShareable(new FJsonValueObject(EdgeObject)));
    }
    
    RootObject->SetArrayField(TEXT("Dependencies"), DependenciesArray);
    
    // Add interactions array if applicable
    if (Options.VisualizationType == EServiceVisualizationType::Interactions && Result.Interactions.Num() > 0)
    {
        TArray<TSharedPtr<FJsonValue>> InteractionsArray;
        
        for (const FServiceInteraction& Interaction : Result.Interactions)
        {
            TSharedPtr<FJsonObject> InteractionObject = MakeShareable(new FJsonObject);
            
            InteractionObject->SetStringField(TEXT("SourceKey"), Interaction.SourceKey.ToString());
            InteractionObject->SetStringField(TEXT("TargetKey"), Interaction.TargetKey.ToString());
            InteractionObject->SetNumberField(TEXT("Timestamp"), Interaction.Timestamp);
            InteractionObject->SetNumberField(TEXT("DurationMs"), Interaction.DurationMs);
            InteractionObject->SetBoolField(TEXT("Success"), Interaction.bSuccess);
            
            InteractionsArray.Add(MakeShareable(new FJsonValueObject(InteractionObject)));
        }
        
        RootObject->SetArrayField(TEXT("Interactions"), InteractionsArray);
    }
    
    // Add hotspots array
    if (Result.Hotspots.Num() > 0)
    {
        TArray<TSharedPtr<FJsonValue>> HotspotsArray;
        
        for (const FName& Hotspot : Result.Hotspots)
        {
            HotspotsArray.Add(MakeShareable(new FJsonValueString(Hotspot.ToString())));
        }
        
        RootObject->SetArrayField(TEXT("Hotspots"), HotspotsArray);
    }
    
    // Serialize to string
    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);
    
    return OutputString;
}

FString FServiceDebugVisualizer::GenerateTextVisualization(const FServiceVisualizationResult& Result, const FServiceVisualizationOptions& Options) const
{
    FString Text;
    
    // Add header
    Text += TEXT("Service Visualization Summary\n");
    Text += TEXT("=============================\n\n");
    
    // Add timestamp
    Text += FString::Printf(TEXT("Generated: %s\n"), *FDateTime::UtcNow().ToString());
    Text += FString::Printf(TEXT("Visualization Type: %s\n\n"), *GetEnumString(Options.VisualizationType));
    
    // Services section
    Text += FString::Printf(TEXT("Services (%d):\n"), Result.Services.Num());
    Text += TEXT("----------\n");
    
    for (const FServiceVisualizationNode& Node : Result.Services)
    {
        // Basic service info
        Text += FString::Printf(TEXT("Service: %s\n"), *Node.ServiceKey.ToString());
        
        if (Node.InterfaceType)
        {
            Text += FString::Printf(TEXT("  Type: %s\n"), *Node.InterfaceType->GetName());
        }
        
        Text += FString::Printf(TEXT("  State: %s\n"), *GetEnumString(Node.State));
        
        if (Options.bIncludeHealthStatus)
        {
            Text += FString::Printf(TEXT("  Health: %s\n"), *GetEnumString(Node.HealthStatus));
        }
        
        if (Node.ZoneID != INDEX_NONE)
        {
            Text += FString::Printf(TEXT("  Zone: %d\n"), Node.ZoneID);
        }
        
        if (Node.RegionID != INDEX_NONE)
        {
            Text += FString::Printf(TEXT("  Region: %d\n"), Node.RegionID);
        }
        
        // Add metrics if requested
        if (Options.bIncludePerformanceMetrics)
        {
            Text += TEXT("  Metrics:\n");
            
            int64 TotalOps = Node.Metrics.SuccessfulOperations.GetValue() + Node.Metrics.FailedOperations.GetValue();
            
            Text += FString::Printf(TEXT("    Successful Operations: %lld\n"), Node.Metrics.SuccessfulOperations.GetValue());
            Text += FString::Printf(TEXT("    Failed Operations: %lld\n"), Node.Metrics.FailedOperations.GetValue());
            
            if (TotalOps > 0)
            {
                float SuccessRate = 100.0f * static_cast<float>(Node.Metrics.SuccessfulOperations.GetValue()) / static_cast<float>(TotalOps);
                Text += FString::Printf(TEXT("    Success Rate: %.1f%%\n"), SuccessRate);
                
                float AvgTime = static_cast<float>(Node.Metrics.TotalOperationTimeMs.GetValue()) / static_cast<float>(TotalOps);
                Text += FString::Printf(TEXT("    Average Operation Time: %.1f ms\n"), AvgTime);
            }
            
            Text += FString::Printf(TEXT("    Max Operation Time: %lld ms\n"), Node.Metrics.MaxOperationTimeMs.GetValue());
            
            float MemoryMB = static_cast<float>(Node.Metrics.MemoryUsageBytes.GetValue()) / (1024.0f * 1024.0f);
            Text += FString::Printf(TEXT("    Memory Usage: %.1f MB\n"), MemoryMB);
            
            Text += FString::Printf(TEXT("    Active Instances: %d\n"), Node.Metrics.ActiveInstances.GetValue());
            
            if (Node.Metrics.LastFailureTime > 0.0)
            {
                double TimeSinceFailure = FPlatformTime::Seconds() - Node.Metrics.LastFailureTime;
                Text += FString::Printf(TEXT("    Last Failure: %.1f seconds ago\n"), TimeSinceFailure);
            }
            
            if (Node.Metrics.LastRecoveryTime > 0.0)
            {
                double TimeSinceRecovery = FPlatformTime::Seconds() - Node.Metrics.LastRecoveryTime;
                Text += FString::Printf(TEXT("    Last Recovery: %.1f seconds ago\n"), TimeSinceRecovery);
            }
        }
        
        // Add hotspot info if applicable
        if (Node.bIsHotspot)
        {
            Text += TEXT("  HOTSPOT: ");
            Text += Node.HotspotReason;
            Text += TEXT("\n");
        }
        
        Text += TEXT("\n");
    }
    
    // Dependencies section
    if (Result.Dependencies.Num() > 0)
    {
        Text += FString::Printf(TEXT("Dependencies (%d):\n"), Result.Dependencies.Num());
        Text += TEXT("---------------\n");
        
        for (const FServiceVisualizationEdge& Edge : Result.Dependencies)
        {
            FString DependencyType = Edge.DependencyType == FDependencyResolver::EDependencyType::Required ? 
                TEXT("Required") : TEXT("Optional");
                
            Text += FString::Printf(TEXT("%s -> %s (%s)\n"),
                *Edge.SourceKey.ToString(),
                *Edge.TargetKey.ToString(),
                *DependencyType);
                
            if (!Edge.bIsActive)
            {
                Text += TEXT("  (Inactive)\n");
            }
            
            if (Edge.bIsBottleneck)
            {
                Text += TEXT("  BOTTLENECK\n");
            }
            
            if (Edge.InteractionCount > 0)
            {
                Text += FString::Printf(TEXT("  Interactions: %d, Avg Time: %.1f ms\n"),
                    Edge.InteractionCount, Edge.AverageResponseTimeMs);
            }
        }
        
        Text += TEXT("\n");
    }
    
    // Interactions section
    if (Options.VisualizationType == EServiceVisualizationType::Interactions && Result.Interactions.Num() > 0)
    {
        Text += FString::Printf(TEXT("Recent Interactions (%d):\n"), Result.Interactions.Num());
        Text += TEXT("-------------------\n");
        
        for (int32 i = 0; i < FMath::Min(Result.Interactions.Num(), 20); ++i)
        {
            const FServiceInteraction& Interaction = Result.Interactions[i];
            
            double TimeSince = FPlatformTime::Seconds() - Interaction.Timestamp;
            
            Text += FString::Printf(TEXT("%.1f sec ago: %s -> %s (%.1f ms) %s\n"),
                TimeSince,
                *Interaction.SourceKey.ToString(),
                *Interaction.TargetKey.ToString(),
                Interaction.DurationMs,
                Interaction.bSuccess ? TEXT("SUCCESS") : TEXT("FAILED"));
        }
        
        if (Result.Interactions.Num() > 20)
        {
            Text += FString::Printf(TEXT("... and %d more interactions\n"), Result.Interactions.Num() - 20);
        }
        
        Text += TEXT("\n");
    }
    
    // Hotspots section
    if (Result.Hotspots.Num() > 0)
    {
        Text += FString::Printf(TEXT("Hotspots (%d):\n"), Result.Hotspots.Num());
        Text += TEXT("----------\n");
        
        for (const FName& Hotspot : Result.Hotspots)
        {
            // Find the node to get the reason
            FString Reason;
            for (const FServiceVisualizationNode& Node : Result.Services)
            {
                if (Node.ServiceKey == Hotspot)
                {
                    Reason = Node.HotspotReason;
                    break;
                }
            }
            
            if (Reason.IsEmpty())
            {
                Text += FString::Printf(TEXT("%s\n"), *Hotspot.ToString());
            }
            else
            {
                Text += FString::Printf(TEXT("%s - %s\n"), *Hotspot.ToString(), *Reason);
            }
        }
    }
    
    return Text;
}

FString FServiceDebugVisualizer::GetEnumString(EServiceVisualizationType Type) const
{
    switch (Type)
    {
        case EServiceVisualizationType::Dependencies:
            return TEXT("Dependencies");
            
        case EServiceVisualizationType::Performance:
            return TEXT("Performance");
            
        case EServiceVisualizationType::Health:
            return TEXT("Health");
            
        case EServiceVisualizationType::Interactions:
            return TEXT("Interactions");
            
        case EServiceVisualizationType::Memory:
            return TEXT("Memory");
            
        default:
            return TEXT("Unknown");
    }
}

FString FServiceDebugVisualizer::GetEnumString(EServiceState State) const
{
    switch (State)
    {
        case EServiceState::Uninitialized:
            return TEXT("Uninitialized");
            
        case EServiceState::Initializing:
            return TEXT("Initializing");
            
        case EServiceState::Active:
            return TEXT("Active");
            
        case EServiceState::Failing:
            return TEXT("Failing");
            
        case EServiceState::ShuttingDown:
            return TEXT("ShuttingDown");
            
        case EServiceState::Destroyed:
            return TEXT("Destroyed");
            
        default:
            return TEXT("Unknown");
    }
}

FString FServiceDebugVisualizer::GetEnumString(EServiceHealthStatus Status) const
{
    switch (Status)
    {
        case EServiceHealthStatus::Healthy:
            return TEXT("Healthy");
            
        case EServiceHealthStatus::Degraded:
            return TEXT("Degraded");
            
        case EServiceHealthStatus::Critical:
            return TEXT("Critical");
            
        case EServiceHealthStatus::Failed:
            return TEXT("Failed");
            
        case EServiceHealthStatus::Unresponsive:
            return TEXT("Unresponsive");
            
        case EServiceHealthStatus::Unknown:
            return TEXT("Unknown");
            
        case EServiceHealthStatus::NotRegistered:
            return TEXT("Not Registered");
            
        case EServiceHealthStatus::Recovering:
            return TEXT("Recovering");
            
        default:
            return TEXT("Unknown");
    }
}
