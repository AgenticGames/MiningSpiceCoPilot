// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IHibernationManager.h" // Fixed include path to use the file in the same directory
#include "IReactivationCoordinator.generated.h"

/**
 * Reactivation component type for prioritized loading
 */
enum class EReactivationComponent : uint8
{
    /** SVO octree structure */
    SVOStructure,
    
    /** SDF distance field data */
    SDFField,
    
    /** Material properties */
    MaterialProperties,
    
    /** Rendering data */
    RenderingData,
    
    /** Physics collision data */
    PhysicsData,
    
    /** Resource properties */
    ResourceProperties,
    
    /** Navigation data */
    NavigationData,
    
    /** Portal connections */
    PortalConnections,
    
    /** Cave topology */
    CaveTopology,
    
    /** Mining modification history */
    MiningHistory
};

/**
 * Reactivation stage for progressive loading
 */
enum class EReactivationStage : uint8
{
    /** Initial request queued */
    Queued,
    
    /** Metadata loaded */
    MetadataLoaded,
    
    /** Essential components loaded */
    EssentialsLoaded,
    
    /** Geometry loaded */
    GeometryLoaded,
    
    /** Material fields loaded */
    MaterialsLoaded,
    
    /** Detail components loaded */
    DetailsLoaded,
    
    /** Fully reactivated */
    Complete,
    
    /** Failed reactivation */
    Failed
};

/**
 * Structure containing reactivation task information
 */
struct MININGSPICECOPILOT_API FReactivationTask
{
    /** ID of the region being reactivated */
    int32 RegionId;
    
    /** Priority level for the reactivation */
    EReactivationPriority Priority;
    
    /** Current stage of reactivation */
    EReactivationStage CurrentStage;
    
    /** Timestamp when the task was created */
    FDateTime CreationTimestamp;
    
    /** Time spent on reactivation so far in milliseconds */
    float ElapsedTimeMs;
    
    /** Percentage of reactivation completed (0-1) */
    float CompletionPercentage;
    
    /** Reason for the reactivation */
    FString ReactivationReason;
    
    /** Whether this was a predicted reactivation */
    bool bWasPredicted;
    
    /** Whether the task is currently being processed */
    bool bIsProcessing;
    
    /** Whether the task is paused */
    bool bIsPaused;
    
    /** Components that have been loaded */
    TArray<EReactivationComponent> LoadedComponents;
    
    /** Components that still need to be loaded */
    TArray<EReactivationComponent> PendingComponents;
    
    /** Estimated remaining time in milliseconds */
    float EstimatedRemainingTimeMs;
    
    /** Original serialized size in bytes */
    uint64 SerializedSizeBytes;
    
    /** Dependencies that must be loaded first */
    TArray<int32> Dependencies;
    
    /** Whether the task is hardware accelerated */
    bool bIsHardwareAccelerated;
    
    /** Whether the task needs memory mapping */
    bool bNeedsMemoryMapping;
    
    /** Priority boost factor based on urgency (1.0 = normal) */
    float PriorityBoostFactor;
    
    /** Task execution frame budget in milliseconds */
    float FrameBudgetMs;
};

/**
 * Structure containing reactivation performance statistics
 */
struct MININGSPICECOPILOT_API FReactivationStats
{
    /** Total number of reactivations performed */
    uint32 TotalReactivations;
    
    /** Number of concurrent reactivations (current) */
    uint32 ConcurrentReactivations;
    
    /** Peak number of concurrent reactivations */
    uint32 PeakConcurrentReactivations;
    
    /** Average reactivation time in milliseconds */
    float AverageReactivationTimeMs;
    
    /** Average time per reactivation stage in milliseconds */
    TMap<EReactivationStage, float> AverageTimePerStageMs;
    
    /** Number of reactivations by priority level */
    TMap<EReactivationPriority, uint32> ReactivationsByPriority;
    
    /** Average size of reactivated regions in bytes */
    uint64 AverageReactivatedSizeBytes;
    
    /** Number of predicted reactivations */
    uint32 PredictedReactivations;
    
    /** Number of emergency reactivations */
    uint32 EmergencyReactivations;
    
    /** Number of failed reactivations */
    uint32 FailedReactivations;
    
    /** Average memory usage during reactivation in bytes */
    uint64 AverageMemoryUsageBytes;
    
    /** Peak memory usage during reactivation in bytes */
    uint64 PeakMemoryUsageBytes;
    
    /** Number of canceled reactivations */
    uint32 CanceledReactivations;
    
    /** Total time spent on reactivation in milliseconds */
    float TotalReactivationTimeMs;
    
    /** Average frame time impact in milliseconds */
    float AverageFrameTimeImpactMs;
    
    /** Peak frame time impact in milliseconds */
    float PeakFrameTimeImpactMs;
    
    /** Average CPU utilization during reactivation (0-1) */
    float AverageCPUUtilization;
    
    /** Average disk I/O during reactivation in bytes per second */
    float AverageDiskIOBytesPerSecond;
    
    /** Average number of frames per reactivation */
    float AverageFramesPerReactivation;
};

/**
 * Structure containing reactivation configuration
 */
struct MININGSPICECOPILOT_API FReactivationConfig
{
    /** Maximum concurrent reactivations */
    uint32 MaxConcurrentReactivations;
    
    /** Frame time budget for reactivation in milliseconds */
    float FrameBudgetMs;
    
    /** Whether to use incremental reactivation */
    bool bUseIncrementalReactivation;
    
    /** Whether to prioritize visible regions */
    bool bPrioritizeVisibleRegions;
    
    /** Whether to boost priority of emergency reactivations */
    bool bBoostEmergencyPriority;
    
    /** Whether to use memory mapping for large regions */
    bool bUseMemoryMapping;
    
    /** Threshold size for memory mapping in bytes */
    uint64 MemoryMappingThresholdBytes;
    
    /** Maximum memory budget for reactivation in bytes */
    uint64 MaxMemoryBudgetBytes;
    
    /** Whether to pause reactivation during heavy gameplay */
    bool bPauseDuringHeavyGameplay;
    
    /** Frame time threshold for heavy gameplay in milliseconds */
    float HeavyGameplayFrameTimeThresholdMs;
    
    /** Whether to defer non-critical reactivations */
    bool bDeferNonCritical;
    
    /** Time to defer non-critical reactivations in seconds */
    float DeferTimeSeconds;
    
    /** Whether to use hardware acceleration when available */
    bool bUseHardwareAcceleration;
    
    /** Whether to prioritize player-oriented portals */
    bool bPrioritizePlayerOrientedPortals;
    
    /** Factor to reduce priority per portal hop (0-1) */
    float PortalHopPriorityReductionFactor;
    
    /** Maximum size for essential components in bytes */
    uint64 MaxEssentialComponentsSizeBytes;
    
    /** Constructor with defaults */
    FReactivationConfig()
        : MaxConcurrentReactivations(3)
        , FrameBudgetMs(2.0f)
        , bUseIncrementalReactivation(true)
        , bPrioritizeVisibleRegions(true)
        , bBoostEmergencyPriority(true)
        , bUseMemoryMapping(true)
        , MemoryMappingThresholdBytes(16 * 1024 * 1024) // 16 MB
        , MaxMemoryBudgetBytes(256 * 1024 * 1024) // 256 MB
        , bPauseDuringHeavyGameplay(true)
        , HeavyGameplayFrameTimeThresholdMs(30.0f)
        , bDeferNonCritical(true)
        , DeferTimeSeconds(1.0f)
        , bUseHardwareAcceleration(true)
        , bPrioritizePlayerOrientedPortals(true)
        , PortalHopPriorityReductionFactor(0.5f)
        , MaxEssentialComponentsSizeBytes(4 * 1024 * 1024) // 4 MB
    {
    }
};

/**
 * Base interface for reactivation coordinators in the SVO+SDF mining architecture
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UReactivationCoordinator : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for coordinating prioritized region reactivation in the SVO+SDF mining architecture
 * Manages multi-tier reactivation with progressive loading and background processing
 */
class MININGSPICECOPILOT_API IReactivationCoordinator
{
    GENERATED_BODY()

public:
    /**
     * Initializes the reactivation coordinator and prepares it for use
     * @return True if initialization was successful
     */
    virtual bool Initialize() = 0;

    /**
     * Shuts down the reactivation coordinator and cleans up resources
     */
    virtual void Shutdown() = 0;
    
    /**
     * Checks if the reactivation coordinator has been initialized
     * @return True if initialized, false otherwise
     */
    virtual bool IsInitialized() const = 0;
    
    /**
     * Queues a region for reactivation
     * @param RegionId ID of the region to reactivate
     * @param Priority Reactivation priority level
     * @param Reason Optional reason for the reactivation
     * @param bWasPredicted Whether this reactivation was predicted
     * @return True if the region was queued successfully
     */
    virtual bool QueueReactivation(
        int32 RegionId,
        EReactivationPriority Priority = EReactivationPriority::Normal,
        const FString& Reason = FString(),
        bool bWasPredicted = false) = 0;
    
    /**
     * Processes pending reactivation tasks
     * @param FrameBudgetMs Maximum time to spend on processing in milliseconds
     * @return Number of tasks processed
     */
    virtual uint32 ProcessReactivations(float FrameBudgetMs = 2.0f) = 0;
    
    /**
     * Checks if a region is currently being reactivated
     * @param RegionId ID of the region to check
     * @return True if the region is being reactivated
     */
    virtual bool IsRegionReactivating(int32 RegionId) const = 0;
    
    /**
     * Gets information about a reactivation task
     * @param RegionId ID of the region being reactivated
     * @return Pointer to task info, or nullptr if not found
     */
    virtual const FReactivationTask* GetReactivationTask(int32 RegionId) const = 0;
    
    /**
     * Gets all active reactivation tasks
     * @return Array of active reactivation tasks
     */
    virtual TArray<FReactivationTask> GetActiveReactivations() const = 0;
    
    /**
     * Cancels a reactivation task
     * @param RegionId ID of the region to cancel
     * @return True if the task was canceled
     */
    virtual bool CancelReactivation(int32 RegionId) = 0;
    
    /**
     * Updates the priority of a reactivation task
     * @param RegionId ID of the region being reactivated
     * @param Priority New priority level
     * @return True if the priority was updated
     */
    virtual bool UpdateReactivationPriority(int32 RegionId, EReactivationPriority Priority) = 0;
    
    /**
     * Pauses a reactivation task
     * @param RegionId ID of the region being reactivated
     * @param bPause Whether to pause or resume the task
     * @return True if the pause state was changed
     */
    virtual bool PauseReactivation(int32 RegionId, bool bPause = true) = 0;
    
    /**
     * Gets reactivation performance statistics
     * @return Structure containing reactivation statistics
     */
    virtual FReactivationStats GetReactivationStats() const = 0;
    
    /**
     * Sets the configuration for the coordinator
     * @param Config New reactivation configuration
     */
    virtual void SetConfig(const FReactivationConfig& Config) = 0;
    
    /**
     * Gets the current coordinator configuration
     * @return Current reactivation configuration
     */
    virtual FReactivationConfig GetConfig() const = 0;
    
    /**
     * Prioritizes specific components for a region reactivation
     * @param RegionId ID of the region being reactivated
     * @param ComponentPriorities Ordered array of components by priority
     * @return True if the component priorities were set
     */
    virtual bool SetComponentPriorities(int32 RegionId, const TArray<EReactivationComponent>& ComponentPriorities) = 0;
    
    /**
     * Sets dependencies for a region reactivation
     * @param RegionId ID of the region being reactivated
     * @param DependencyRegionIds IDs of regions that must be reactivated first
     * @return True if the dependencies were set
     */
    virtual bool SetReactivationDependencies(int32 RegionId, const TArray<int32>& DependencyRegionIds) = 0;
    
    /**
     * Adjusts the frame budget for a specific reactivation task
     * @param RegionId ID of the region being reactivated
     * @param FrameBudgetMs New frame budget in milliseconds
     * @return True if the frame budget was adjusted
     */
    virtual bool SetReactivationFrameBudget(int32 RegionId, float FrameBudgetMs) = 0;
    
    /**
     * Boosts the priority of a reactivation task by a factor
     * @param RegionId ID of the region being reactivated
     * @param BoostFactor Factor to boost priority (1.0 = normal)
     * @return True if the boost was applied
     */
    virtual bool BoostReactivationPriority(int32 RegionId, float BoostFactor) = 0;
    
    /**
     * Notifies the coordinator of heavy gameplay to adjust reactivation pacing
     * @param bHeavyGameplay Whether heavy gameplay is occurring
     * @param GameplayIntensity Intensity level of gameplay (0-1)
     */
    virtual void NotifyGameplayIntensity(bool bHeavyGameplay, float GameplayIntensity = 1.0f) = 0;
    
    /**
     * Gets the singleton instance of the reactivation coordinator
     * @return Reference to the reactivation coordinator instance
     */
    static IReactivationCoordinator& Get();
};