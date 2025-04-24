// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DependencyResolver.h"
#include "ServiceManager.h"
#include "ServiceHealthMonitor.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/CriticalSection.h"
#include "Containers/Map.h"
#include "Templates/SharedPointer.h"
#include "Misc/Optional.h"

// Forward declarations
class FServiceManager;
class FDependencyResolver;
class FServiceHealthMonitor;
enum class EServiceState : uint8;
enum class EServiceHealthStatus : uint8;
struct FServiceMetrics;

/**
 * Visualization format enumeration
 */
enum class EServiceVisualizationFormat : uint8
{
    /** DOT format for Graphviz */
    DOT,
    
    /** JSON format for web visualization */
    JSON,
    
    /** Human-readable text format */
    Text,
    
    /** UE-native visualization */
    UE4
};

/**
 * Visualization type enumeration
 */
enum class EServiceVisualizationType : uint8
{
    /** Service dependencies */
    Dependencies,
    
    /** Service performance */
    Performance,
    
    /** Service health */
    Health,
    
    /** Service interactions */
    Interactions,
    
    /** Memory usage */
    Memory
};

/**
 * Hotspot threshold configuration
 */
struct FHotspotConfig
{
    /** Threshold for response time hotspots (ms) */
    float ResponseTimeThresholdMs;
    
    /** Threshold for memory usage hotspots (MB) */
    float MemoryUsageThresholdMB;
    
    /** Threshold for failure rate hotspots (0.0-1.0) */
    float FailureRateThreshold;
    
    /** Default constructor */
    FHotspotConfig()
        : ResponseTimeThresholdMs(100.0f)
        , MemoryUsageThresholdMB(500.0f)
        , FailureRateThreshold(0.05f)
    {
    }
};

/**
 * Visualization options
 */
struct FServiceVisualizationOptions
{
    /** Type of visualization */
    EServiceVisualizationType VisualizationType;
    
    /** Output format */
    EServiceVisualizationFormat Format;
    
    /** Whether to include inactive services */
    bool bIncludeInactiveServices;
    
    /** Whether to include service details */
    bool bIncludeServiceDetails;
    
    /** Whether to include performance metrics */
    bool bIncludePerformanceMetrics;
    
    /** Whether to include health status */
    bool bIncludeHealthStatus;
    
    /** Whether to highlight hotspots */
    bool bHighlightHotspots;
    
    /** Whether to group services by zone */
    bool bGroupByZone;
    
    /** Whether to group services by region */
    bool bGroupByRegion;
    
    /** Time range for historical data (seconds, 0 for current only) */
    float HistoricalTimeRangeSec;
    
    /** Configuration for hotspot detection */
    FHotspotConfig HotspotConfig;
    
    /** Maximum dependency depth to traverse */
    int32 MaxDependencyDepth;
    
    /** Default constructor */
    FServiceVisualizationOptions()
        : VisualizationType(EServiceVisualizationType::Dependencies)
        , Format(EServiceVisualizationFormat::DOT)
        , bIncludeInactiveServices(false)
        , bIncludeServiceDetails(true)
        , bIncludePerformanceMetrics(true)
        , bIncludeHealthStatus(true)
        , bHighlightHotspots(true)
        , bGroupByZone(false)
        , bGroupByRegion(false)
        , HistoricalTimeRangeSec(0.0f)
        , MaxDependencyDepth(0)
    {
    }
};

/**
 * Service node for visualization
 */
struct FServiceVisualizationNode
{
    /** Service key */
    FName ServiceKey;
    
    /** Service interface type */
    UClass* InterfaceType;
    
    /** Service state */
    EServiceState State;
    
    /** Service health status */
    EServiceHealthStatus HealthStatus;
    
    /** Zone ID */
    int32 ZoneID;
    
    /** Region ID */
    int32 RegionID;
    
    /** Service metrics */
    FServiceMetrics Metrics;
    
    /** Whether this service is a hotspot */
    bool bIsHotspot;
    
    /** Hotspot reason if applicable */
    FString HotspotReason;
    
    /** Historical metrics over time */
    TArray<TPair<double, FServiceMetrics>> HistoricalMetrics;
    
    /** Constructor */
    FServiceVisualizationNode();
};

/**
 * Service edge for visualization
 */
struct FServiceVisualizationEdge
{
    /** Source service key */
    FName SourceKey;
    
    /** Target service key */
    FName TargetKey;
    
    /** Dependency type */
    FDependencyResolver::EDependencyType DependencyType;
    
    /** Whether this dependency is active */
    bool bIsActive;
    
    /** Whether this dependency is a performance bottleneck */
    bool bIsBottleneck;
    
    /** Number of interactions */
    int32 InteractionCount;
    
    /** Average response time (ms) */
    float AverageResponseTimeMs;
    
    /** Constructor */
    FServiceVisualizationEdge();
};

/**
 * Service interaction record
 */
struct FServiceInteraction
{
    /** Source service key */
    FName SourceKey;
    
    /** Target service key */
    FName TargetKey;
    
    /** Timestamp */
    double Timestamp;
    
    /** Duration in milliseconds */
    float DurationMs;
    
    /** Whether the interaction was successful */
    bool bSuccess;
    
    /** Constructor */
    FServiceInteraction()
        : Timestamp(0.0)
        , DurationMs(0.0f)
        , bSuccess(true)
    {
    }
};

/**
 * Service visualization result
 */
struct FServiceVisualizationResult
{
    /** Services included in the visualization */
    TArray<FServiceVisualizationNode> Services;
    
    /** Dependencies between services */
    TArray<FServiceVisualizationEdge> Dependencies;
    
    /** Recent service interactions */
    TArray<FServiceInteraction> Interactions;
    
    /** Hotspot services */
    TArray<FName> Hotspots;
    
    /** Visualization string */
    FString VisualizationString;
    
    /** Creation timestamp */
    double CreationTime;
    
    /** Constructor */
    FServiceVisualizationResult();
};

/**
 * Service Debug Visualizer
 * Provides visualization of service relationships, dependencies, 
 * performance metrics, and runtime interactions for debugging.
 */
class MININGSPICECOPILOT_API FServiceDebugVisualizer
{
public:
    /** Constructor */
    FServiceDebugVisualizer();
    
    /** Destructor */
    ~FServiceDebugVisualizer();
    
    /**
     * Initialize the visualizer
     * @param InServiceManager Reference to the service manager
     * @param InDependencyResolver Reference to the dependency resolver
     * @param InHealthMonitor Optional reference to the health monitor
     * @return True if initialization was successful
     */
    bool Initialize(FServiceManager& InServiceManager, 
        FDependencyResolver& InDependencyResolver,
        FServiceHealthMonitor* InHealthMonitor = nullptr);
    
    /**
     * Shutdown the visualizer
     */
    void Shutdown();
    
    /**
     * Check if the visualizer is initialized
     * @return True if initialized
     */
    bool IsInitialized() const;
    
    /**
     * Visualize all services
     * @param Options Visualization options
     * @return Visualization result
     */
    FServiceVisualizationResult VisualizeServices(const FServiceVisualizationOptions& Options = FServiceVisualizationOptions());
    
    /**
     * Visualize a specific service and its dependencies
     * @param ServiceKey Service identifier
     * @param Options Visualization options
     * @return Visualization result
     */
    FServiceVisualizationResult VisualizeService(const FName& ServiceKey, const FServiceVisualizationOptions& Options = FServiceVisualizationOptions());
    
    /**
     * Visualize a set of services
     * @param ServiceKeys Array of service identifiers
     * @param Options Visualization options
     * @return Visualization result
     */
    FServiceVisualizationResult VisualizeServices(const TArray<FName>& ServiceKeys, const FServiceVisualizationOptions& Options = FServiceVisualizationOptions());
    
    /**
     * Visualize services in a specific zone
     * @param ZoneID Zone identifier
     * @param Options Visualization options
     * @return Visualization result
     */
    FServiceVisualizationResult VisualizeZoneServices(int32 ZoneID, const FServiceVisualizationOptions& Options = FServiceVisualizationOptions());
    
    /**
     * Visualize services in a specific region
     * @param RegionID Region identifier
     * @param Options Visualization options
     * @return Visualization result
     */
    FServiceVisualizationResult VisualizeRegionServices(int32 RegionID, const FServiceVisualizationOptions& Options = FServiceVisualizationOptions());
    
    /**
     * Visualize performance hotspots
     * @param Options Visualization options
     * @return Visualization result
     */
    FServiceVisualizationResult VisualizeHotspots(const FServiceVisualizationOptions& Options = FServiceVisualizationOptions());
    
    /**
     * Visualize service health
     * @param Options Visualization options
     * @return Visualization result
     */
    FServiceVisualizationResult VisualizeServiceHealth(const FServiceVisualizationOptions& Options = FServiceVisualizationOptions());
    
    /**
     * Record a service interaction
     * @param SourceKey Source service identifier
     * @param TargetKey Target service identifier
     * @param DurationMs Duration of the interaction in milliseconds
     * @param bSuccess Whether the interaction was successful
     */
    void RecordServiceInteraction(const FName& SourceKey, const FName& TargetKey, 
        float DurationMs, bool bSuccess = true);
    
    /**
     * Get recent service interactions
     * @param MaxCount Maximum number of interactions to return
     * @return Array of recent interactions, newest first
     */
    TArray<FServiceInteraction> GetRecentInteractions(int32 MaxCount = 100) const;
    
    /**
     * Save visualization to file
     * @param Visualization Visualization result
     * @param FilePath Path to save the file
     * @return True if successful
     */
    bool SaveVisualizationToFile(const FServiceVisualizationResult& Visualization, const FString& FilePath) const;
    
    /**
     * Generate a DOT format visualization
     * @param Result Visualization data
     * @param Options Visualization options
     * @return DOT format string
     */
    FString GenerateDOTVisualization(const FServiceVisualizationResult& Result, const FServiceVisualizationOptions& Options) const;
    
    /**
     * Generate a JSON format visualization
     * @param Result Visualization data
     * @param Options Visualization options
     * @return JSON format string
     */
    FString GenerateJSONVisualization(const FServiceVisualizationResult& Result, const FServiceVisualizationOptions& Options) const;
    
    /**
     * Generate a text format visualization
     * @param Result Visualization data
     * @param Options Visualization options
     * @return Text format string
     */
    FString GenerateTextVisualization(const FServiceVisualizationResult& Result, const FServiceVisualizationOptions& Options) const;
    
    /**
     * Identify performance hotspots
     * @param Services Array of service nodes
     * @param Dependencies Array of service dependencies
     * @param Config Hotspot configuration
     * @return Array of hotspot service keys
     */
    TArray<FName> IdentifyHotspots(TArray<FServiceVisualizationNode>& Services, 
        const TArray<FServiceVisualizationEdge>& Dependencies, 
        const FHotspotConfig& Config) const;
    
    /**
     * Get the singleton instance
     * @return Reference to the service debug visualizer
     */
    static FServiceDebugVisualizer& Get();

private:
    /** Whether the visualizer is initialized */
    FThreadSafeBool bIsInitialized;
    
    /** Reference to the service manager */
    FServiceManager* ServiceManager;
    
    /** Reference to the dependency resolver */
    FDependencyResolver* DependencyResolver;
    
    /** Reference to the health monitor (can be null) */
    FServiceHealthMonitor* HealthMonitor;
    
    /** Critical section for thread safety */
    mutable FCriticalSection VisualizerLock;
    
    /** Recent service interactions */
    TArray<FServiceInteraction> RecentInteractions;
    
    /** Maximum interactions to track */
    int32 MaxInteractionHistory;
    
    /** Historical service metrics */
    TMap<FName, TArray<TPair<double, FServiceMetrics>>> MetricsHistory;
    
    /** Maximum history entries per service */
    int32 MaxMetricsHistoryEntries;
    
    /** Singleton instance */
    static FServiceDebugVisualizer* Instance;
    
    /**
     * Build a visualization for a set of services
     * @param ServiceKeys Service keys to include
     * @param Options Visualization options
     * @param OutResult Output visualization result
     * @return True if successful
     */
    bool BuildVisualization(const TArray<FName>& ServiceKeys, 
        const FServiceVisualizationOptions& Options,
        FServiceVisualizationResult& OutResult);
    
    /**
     * Collect service nodes for visualization
     * @param ServiceKeys Service keys to include
     * @param Options Visualization options
     * @param OutNodes Output array of service nodes
     * @return True if successful
     */
    bool CollectServiceNodes(const TArray<FName>& ServiceKeys,
        const FServiceVisualizationOptions& Options,
        TArray<FServiceVisualizationNode>& OutNodes);
    
    /**
     * Collect service dependencies for visualization
     * @param ServiceNodes Service nodes to process
     * @param Options Visualization options
     * @param OutEdges Output array of service dependencies
     * @return True if successful
     */
    bool CollectServiceDependencies(const TArray<FServiceVisualizationNode>& ServiceNodes,
        const FServiceVisualizationOptions& Options,
        TArray<FServiceVisualizationEdge>& OutEdges);
    
    /**
     * Collect service interaction data
     * @param ServiceKeys Service keys to include
     * @param Options Visualization options
     * @param OutInteractions Output array of interactions
     * @return True if successful
     */
    bool CollectInteractionData(const TArray<FName>& ServiceKeys,
        const FServiceVisualizationOptions& Options,
        TArray<FServiceInteraction>& OutInteractions);
    
    /**
     * Get a color for a service state
     * @param State Service state
     * @return HTML color string
     */
    FString GetStateColor(EServiceState State) const;
    
    /**
     * Get a color for a service health status
     * @param Status Health status
     * @return HTML color string
     */
    FString GetHealthStatusColor(EServiceHealthStatus Status) const;
    
    /**
     * Update historical metrics
     * @param ServiceKey Service identifier
     * @param Metrics Current metrics
     */
    void UpdateMetricsHistory(const FName& ServiceKey, const FServiceMetrics& Metrics);
    
    /**
     * Retrieve historical metrics for a service
     * @param ServiceKey Service identifier
     * @param TimeRangeSec Time range in seconds (0 for all)
     * @return Array of timestamped metrics
     */
    TArray<TPair<double, FServiceMetrics>> GetMetricsHistory(const FName& ServiceKey, float TimeRangeSec = 0.0f) const;

    /**
     * Add a service node to DOT visualization
     * @param DOT DOT string to append to
     * @param Node Service node to add
     * @param Options Visualization options
     */
    void AddServiceNodeToDOT(FString& DOT, const FServiceVisualizationNode& Node, const FServiceVisualizationOptions& Options) const;
    
    /**
     * Add a dependency edge to DOT visualization
     * @param DOT DOT string to append to
     * @param Edge Dependency edge to add
     * @param Options Visualization options
     */
    void AddDependencyEdgeToDOT(FString& DOT, const FServiceVisualizationEdge& Edge, const FServiceVisualizationOptions& Options) const;
    
    /**
     * Get string representation of visualization type enum
     * @param Type Visualization type
     * @return String representation
     */
    FString GetEnumString(EServiceVisualizationType Type) const;
    
    /**
     * Get string representation of service state enum
     * @param State Service state
     * @return String representation
     */
    FString GetEnumString(EServiceState State) const;
    
    /**
     * Get string representation of health status enum
     * @param Status Health status
     * @return String representation
     */
    FString GetEnumString(EServiceHealthStatus Status) const;
};
