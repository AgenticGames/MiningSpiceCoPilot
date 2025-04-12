// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "../../../2.1_TieredCompression/Public/Interfaces/ICompressionManager.h" // Using relative path
#include "IHibernationCache.h" // Include this to use the EReactivationPriority enum
#include "IHibernationManager.generated.h"

/**
 * Region activity state for hibernation decisions
 */
enum class ERegionActivityState : uint8
{
    /** Region containing player or active gameplay */
    Active,
    
    /** Recently active region (player left recently) */
    RecentlyActive,
    
    /** Visible but inactive region (through portal or window) */
    VisibleInactive,
    
    /** Nearby inactive region (connected by portal) */
    NearbyInactive,
    
    /** Distant inactive region (not connected directly) */
    DistantInactive,
    
    /** Hibernated region offloaded to disk */
    Hibernated
};

/**
 * Hibernation trigger condition
 */
enum class EHibernationTrigger : uint8
{
    /** Memory pressure requiring immediate hibernation */
    MemoryPressure,
    
    /** Distance-based hibernation (player moved far away) */
    Distance,
    
    /** Time-based hibernation (inactive for long period) */
    InactivityTime,
    
    /** Portal-based (no connected portals visible) */
    NoVisiblePortals,
    
    /** Manual hibernation triggered by code */
    Manual,
    
    /** Emergency hibernation for critical memory situations */
    Emergency
};

/**
 * Structure containing hibernation parameters
 */
struct MININGSPICECOPILOT_API FHibernationParameters
{
    /** Compression tier to use for hibernation */
    ECompressionTier CompressionTier;
    
    /** Whether to use incremental serialization */
    bool bUseIncrementalSerialization;
    
    /** Whether to preserve narrow-band precision around interfaces */
    bool bPreserveNarrowBand;
    
    /** Width of narrow band to preserve (in voxel units) */
    float NarrowBandWidth;
    
    /** Whether to use delta serialization if available */
    bool bUseDeltaSerialization;
    
    /** Whether to memory map the serialized data */
    bool bUseMemoryMapping;
    
    /** Whether this is an emergency hibernation */
    bool bIsEmergency;
    
    /** Material IDs that should preserve precision */
    TArray<uint32> PrecisionPreserveMaterialIDs;
    
    /** Maximum time per incremental serialization frame in milliseconds */
    float MaxIncrementalTimeMs;
    
    /** Whether to use background thread for serialization */
    bool bUseBackgroundThread;
    
    /** Whether to compress mining modifications separately */
    bool bCompressMiningModificationsSeparately;
    
    /** Whether to use hardware acceleration if available */
    bool bUseHardwareAcceleration;
    
    /** Whether to prioritize important materials */
    bool bPrioritizeImportantMaterials;
    
    /** Constructor with defaults */
    FHibernationParameters()
        : CompressionTier(ECompressionTier::Standard)
        , bUseIncrementalSerialization(true)
        , bPreserveNarrowBand(true)
        , NarrowBandWidth(3.0f)
        , bUseDeltaSerialization(true)
        , bUseMemoryMapping(true)
        , bIsEmergency(false)
        , MaxIncrementalTimeMs(5.0f)
        , bUseBackgroundThread(true)
        , bCompressMiningModificationsSeparately(true)
        , bUseHardwareAcceleration(true)
        , bPrioritizeImportantMaterials(true)
    {
    }
};

/**
 * Structure containing hibernation status information
 */
struct MININGSPICECOPILOT_API FHibernationStatus
{
    /** ID of the hibernated region */
    int32 RegionId;
    
    /** Current activity state of the region */
    ERegionActivityState ActivityState;
    
    /** Timestamp when the region was last active */
    FDateTime LastActiveTime;
    
    /** Timestamp when the region was hibernated */
    FDateTime HibernationTime;
    
    /** Trigger that caused hibernation */
    EHibernationTrigger HibernationTrigger;
    
    /** Original memory usage before hibernation in bytes */
    uint64 OriginalMemoryUsageBytes;
    
    /** Serialized size after hibernation in bytes */
    uint64 SerializedSizeBytes;
    
    /** Compression ratio achieved (original/serialized) */
    float CompressionRatio;
    
    /** Whether the region is currently in cache */
    bool bIsInCache;
    
    /** Estimated reactivation time in milliseconds */
    float EstimatedReactivationTimeMs;
    
    /** Distance from player in cm */
    float DistanceFromPlayer;
    
    /** Whether hibernation was incremental */
    bool bWasIncrementalHibernation;
    
    /** Compression tier used */
    ECompressionTier CompressionTier;
    
    /** Whether portal connections are preserved */
    bool bPortalConnectionsPreserved;
    
    /** Predicted likelihood of reactivation (0-1) */
    float ReactivationLikelihood;
    
    /** Whether the region has been modified by mining */
    bool bHasMiningModifications;
    
    /** Cache priority level if in cache */
    EReactivationPriority CachePriority;
    
    /** Whether narrow-band precision was preserved */
    bool bNarrowBandPreserved;
    
    /** How long hibernation took in milliseconds */
    float HibernationTimeMs;
    
    /** Number of frames used for incremental hibernation */
    uint32 IncrementalHibernationFrameCount;
    
    /** Whether the region is flagged for priority reactivation */
    bool bIsFlaggedForReactivation;
};

/**
 * Structure containing hibernation system metrics
 */
struct MININGSPICECOPILOT_API FHibernationMetrics
{
    /** Total number of hibernated regions */
    uint32 HibernatedRegionCount;
    
    /** Total memory reclaimed through hibernation in bytes */
    uint64 TotalReclaimedMemoryBytes;
    
    /** Average compression ratio across all hibernated regions */
    float AverageCompressionRatio;
    
    /** Total disk space used for hibernated regions in bytes */
    uint64 TotalDiskSpaceUsedBytes;
    
    /** Number of regions in each activity state */
    TMap<ERegionActivityState, uint32> RegionCountByActivityState;
    
    /** Average hibernation time in milliseconds */
    float AverageHibernationTimeMs;
    
    /** Average reactivation time in milliseconds */
    float AverageReactivationTimeMs;
    
    /** Number of hibernations by trigger type */
    TMap<EHibernationTrigger, uint32> HibernationCountByTrigger;
    
    /** Number of cached hibernated regions */
    uint32 CachedHibernatedRegionCount;
    
    /** Total memory used for cached regions in bytes */
    uint64 TotalCachedMemoryBytes;
    
    /** Number of incremental hibernations */
    uint32 IncrementalHibernationCount;
    
    /** Number of delta serializations */
    uint32 DeltaSerializationCount;
    
    /** Number of emergency hibernations */
    uint32 EmergencyHibernationCount;
    
    /** Number of regions with mining modifications */
    uint32 ModifiedRegionCount;
    
    /** Peak memory reclamation rate in bytes per second */
    float PeakMemoryReclamationRateBytes;
    
    /** Number of prediction-based reactivations */
    uint32 PredictedReactivationCount;
    
    /** Number of emergency reactivations */
    uint32 EmergencyReactivationCount;
    
    /** Average prediction accuracy for reactivations (0-1) */
    float AveragePredictionAccuracy;
    
    /** Number of hibernation errors */
    uint32 HibernationErrorCount;
    
    /** Number of reactivation errors */
    uint32 ReactivationErrorCount;
};

/**
 * Structure containing hibernation system configuration
 */
struct MININGSPICECOPILOT_API FHibernationConfig
{
    /** Whether the hibernation system is enabled */
    bool bEnabled;
    
    /** Maximum memory budget before hibernation triggers in bytes */
    uint64 MaxMemoryBudgetBytes;
    
    /** Emergency memory threshold as percentage of budget (0-1) */
    float EmergencyMemoryThreshold;
    
    /** Minimum inactive time before hibernation in seconds */
    float MinInactiveTimeBeforeHibernationSeconds;
    
    /** Default compression tier for hibernation */
    ECompressionTier DefaultCompressionTier;
    
    /** Whether to use incremental hibernation by default */
    bool bUseIncrementalHibernation;
    
    /** Whether to preserve narrow band precision by default */
    bool bPreserveNarrowBand;
    
    /** Default narrow band width in voxel units */
    float DefaultNarrowBandWidth;
    
    /** Maximum distance for active regions in cm */
    float MaxActiveRegionDistance;
    
    /** Maximum distance for recently active regions in cm */
    float MaxRecentlyActiveRegionDistance;
    
    /** Maximum distance for visible inactive regions in cm */
    float MaxVisibleInactiveRegionDistance;
    
    /** Maximum distance for nearby inactive regions in cm */
    float MaxNearbyInactiveRegionDistance;
    
    /** Whether to use delta serialization by default */
    bool bUseDeltaSerialization;
    
    /** Whether to use background threading by default */
    bool bUseBackgroundThreading;
    
    /** Whether to enable memory mapping for hibernated regions */
    bool bUseMemoryMapping;
    
    /** Max concurrent hibernation operations */
    uint32 MaxConcurrentHibernations;
    
    /** Max memory pressure threshold before emergency hibernation (0-1) */
    float MaxMemoryPressureThreshold;
    
    /** Whether to use prediction-based reactivation */
    bool bUsePredictiveReactivation;
    
    /** Whether to prioritize mining-modified regions in cache */
    bool bPrioritizeModifiedRegions;
    
    /** Frame budget for hibernation operations in milliseconds */
    float HibernationFrameBudgetMs;
    
    /** Whether to compress mining modifications separately */
    bool bCompressMiningModificationsSeparately;
    
    /** Constructor with defaults */
    FHibernationConfig()
        : bEnabled(true)
        , MaxMemoryBudgetBytes(2ULL * 1024 * 1024 * 1024) // 2 GB
        , EmergencyMemoryThreshold(0.95f)
        , MinInactiveTimeBeforeHibernationSeconds(300.0f) // 5 minutes
        , DefaultCompressionTier(ECompressionTier::Standard)
        , bUseIncrementalHibernation(true)
        , bPreserveNarrowBand(true)
        , DefaultNarrowBandWidth(3.0f)
        , MaxActiveRegionDistance(1000.0f) // 10 meters
        , MaxRecentlyActiveRegionDistance(3000.0f) // 30 meters
        , MaxVisibleInactiveRegionDistance(10000.0f) // 100 meters
        , MaxNearbyInactiveRegionDistance(20000.0f) // 200 meters
        , bUseDeltaSerialization(true)
        , bUseBackgroundThreading(true)
        , bUseMemoryMapping(true)
        , MaxConcurrentHibernations(2)
        , MaxMemoryPressureThreshold(0.9f)
        , bUsePredictiveReactivation(true)
        , bPrioritizeModifiedRegions(true)
        , HibernationFrameBudgetMs(2.0f)
        , bCompressMiningModificationsSeparately(true)
    {
    }
};

/**
 * Base interface for hibernation managers in the SVO+SDF mining architecture
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UHibernationManager : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for the region hibernation system in the SVO+SDF mining architecture
 * Provides memory reclamation through serialization of inactive regions
 */
class MININGSPICECOPILOT_API IHibernationManager
{
    GENERATED_BODY()

public:
    /**
     * Initializes the hibernation manager and prepares it for use
     * @return True if initialization was successful
     */
    virtual bool Initialize() = 0;

    /**
     * Shuts down the hibernation manager and cleans up resources
     */
    virtual void Shutdown() = 0;
    
    /**
     * Checks if the hibernation manager has been initialized
     * @return True if initialized, false otherwise
     */
    virtual bool IsInitialized() const = 0;
    
    /**
     * Updates the hibernation system, processing pending operations
     * @param DeltaTime Time since last update in seconds
     * @param FrameBudgetMs Maximum time to spend on hibernation in milliseconds
     */
    virtual void Update(float DeltaTime, float FrameBudgetMs = 2.0f) = 0;
    
    /**
     * Hibernates a region to reclaim memory
     * @param RegionId ID of the region to hibernate
     * @param RegionData Pointer to the region data
     * @param Trigger Trigger that caused the hibernation
     * @param Parameters Hibernation parameters
     * @return True if hibernation was initiated successfully
     */
    virtual bool HibernateRegion(
        int32 RegionId,
        void* RegionData,
        EHibernationTrigger Trigger = EHibernationTrigger::Manual,
        const FHibernationParameters& Parameters = FHibernationParameters()) = 0;
    
    /**
     * Reactivates a hibernated region
     * @param RegionId ID of the region to reactivate
     * @param OutRegionData Pointer to store the reactivated region data
     * @param Priority Priority level for the reactivation
     * @param bWasPredicted Whether this reactivation was predicted
     * @return True if reactivation was initiated successfully
     */
    virtual bool ReactivateRegion(
        int32 RegionId,
        void*& OutRegionData,
        EReactivationPriority Priority = EReactivationPriority::Normal,
        bool bWasPredicted = false) = 0;
    
    /**
     * Updates the activity state of a region
     * @param RegionId ID of the region
     * @param ActivityState New activity state
     * @param PlayerPosition Current player position for distance calculation
     * @return True if the activity state was updated
     */
    virtual bool UpdateRegionActivityState(
        int32 RegionId,
        ERegionActivityState ActivityState,
        const FVector& PlayerPosition = FVector::ZeroVector) = 0;
    
    /**
     * Gets the hibernation status of a region
     * @param RegionId ID of the region
     * @return Structure containing hibernation status information
     */
    virtual FHibernationStatus GetRegionHibernationStatus(int32 RegionId) const = 0;
    
    /**
     * Gets metrics for the hibernation system
     * @return Structure containing hibernation metrics
     */
    virtual FHibernationMetrics GetHibernationMetrics() const = 0;
    
    /**
     * Sets the configuration for the hibernation system
     * @param Config New hibernation configuration
     */
    virtual void SetHibernationConfig(const FHibernationConfig& Config) = 0;
    
    /**
     * Gets the current hibernation system configuration
     * @return Current hibernation configuration
     */
    virtual FHibernationConfig GetHibernationConfig() const = 0;
    
    /**
     * Checks if a region is hibernated
     * @param RegionId ID of the region to check
     * @return True if the region is hibernated
     */
    virtual bool IsRegionHibernated(int32 RegionId) const = 0;
    
    /**
     * Checks if a region is currently being hibernated
     * @param RegionId ID of the region to check
     * @return True if the region is in the process of being hibernated
     */
    virtual bool IsRegionHibernating(int32 RegionId) const = 0;
    
    /**
     * Checks if a region is currently being reactivated
     * @param RegionId ID of the region to check
     * @return True if the region is in the process of being reactivated
     */
    virtual bool IsRegionReactivating(int32 RegionId) const = 0;
    
    /**
     * Flags a region for priority reactivation
     * @param RegionId ID of the region
     * @param Priority Priority level for reactivation
     * @return True if the region was flagged
     */
    virtual bool FlagForReactivation(int32 RegionId, EReactivationPriority Priority = EReactivationPriority::Normal) = 0;
    
    /**
     * Cancels hibernation of a region
     * @param RegionId ID of the region
     * @return True if hibernation was canceled
     */
    virtual bool CancelHibernation(int32 RegionId) = 0;
    
    /**
     * Cancels reactivation of a region
     * @param RegionId ID of the region
     * @return True if reactivation was canceled
     */
    virtual bool CancelReactivation(int32 RegionId) = 0;
    
    /**
     * Gets all hibernated region IDs
     * @return Array of hibernated region IDs
     */
    virtual TArray<int32> GetAllHibernatedRegionIds() const = 0;
    
    /**
     * Gets all regions in a specific activity state
     * @param ActivityState Activity state to filter by
     * @return Array of region IDs in the specified state
     */
    virtual TArray<int32> GetRegionsByActivityState(ERegionActivityState ActivityState) const = 0;
    
    /**
     * Forces an immediate memory reclamation if needed
     * @param RequiredMemoryBytes Number of bytes that need to be freed
     * @param bEmergency Whether this is an emergency reclamation
     * @return Number of bytes reclaimed
     */
    virtual uint64 ForceMemoryReclamation(uint64 RequiredMemoryBytes, bool bEmergency = false) = 0;
    
    /**
     * Preloads essential components for a hibernated region
     * @param RegionId ID of the region
     * @return True if preloading was initiated
     */
    virtual bool PreloadEssentialComponents(int32 RegionId) = 0;
    
    /**
     * Notifies the hibernation system of player movement
     * @param PlayerPosition Current player position
     * @param PlayerVelocity Current player velocity
     * @param PlayerViewDirection Current player view direction
     */
    virtual void NotifyPlayerMovement(
        const FVector& PlayerPosition,
        const FVector& PlayerVelocity,
        const FVector& PlayerViewDirection) = 0;
    
    /**
     * Gets the singleton instance of the hibernation manager
     * @return Reference to the hibernation manager instance
     */
    static IHibernationManager& Get();
};