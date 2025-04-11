// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Interfaces/Hibernation/IHibernationManager.h"
#include "ITopologicalPredictor.generated.h"

/**
 * Structure containing portal information for topological prediction
 */
struct MININGSPICECOPILOT_API FPortalInfo
{
    /** Unique ID of the portal */
    int32 PortalId;
    
    /** ID of the source region */
    int32 SourceRegionId;
    
    /** ID of the destination region */
    int32 DestinationRegionId;
    
    /** Portal position in world space */
    FVector Position;
    
    /** Portal normal direction (facing into destination region) */
    FVector Normal;
    
    /** Portal dimensions (width, height) */
    FVector2D Dimensions;
    
    /** Whether the portal is currently visible to the player */
    bool bIsVisible;
    
    /** Distance from player to portal in centimeters */
    float DistanceToPlayer;
    
    /** Whether the portal connects to a hibernated region */
    bool bConnectsToHibernatedRegion;
    
    /** Importance score for the portal (higher means more important) */
    float ImportanceScore;
    
    /** Whether this portal leads to an important area (resource-rich, etc.) */
    bool bLeadsToImportantArea;
    
    /** Estimated travel time through portal in seconds */
    float EstimatedTravelTimeSeconds;
    
    /** Whether the player has previously used this portal */
    bool bPreviouslyTraversed;
};

/**
 * Structure containing player movement prediction data
 */
struct MININGSPICECOPILOT_API FMovementPrediction
{
    /** Current player position */
    FVector CurrentPosition;
    
    /** Current player velocity */
    FVector CurrentVelocity;
    
    /** Predicted position after time horizon */
    FVector PredictedPosition;
    
    /** Confidence in the prediction (0-1) */
    float Confidence;
    
    /** Time horizon of the prediction in seconds */
    float TimeHorizonSeconds;
    
    /** ID of the current region containing the player */
    int32 CurrentRegionId;
    
    /** Array of predicted region IDs in order of traversal */
    TArray<int32> PredictedRegionPath;
    
    /** Array of portal IDs in the predicted path */
    TArray<int32> PredictedPortalPath;
    
    /** Whether the prediction includes mining activity */
    bool bIncludesMiningActivity;
    
    /** Estimated time until each region traversal in seconds */
    TArray<float> EstimatedTraversalTimes;
    
    /** Portal visibility chain depth */
    int32 VisibilityChainDepth;
    
    /** Primary direction of movement */
    FVector PrimaryDirection;
};

/**
 * Structure containing prediction performance metrics
 */
struct MININGSPICECOPILOT_API FPredictionMetrics
{
    /** Total number of predictions made */
    uint64 TotalPredictions;
    
    /** Number of accurate predictions (region was actually traversed) */
    uint64 AccuratePredictions;
    
    /** Number of inaccurate predictions (region was not traversed) */
    uint64 InaccuratePredictions;
    
    /** Prediction accuracy rate (0-1) */
    float AccuracyRate;
    
    /** Average time between prediction and actual traversal in seconds */
    float AveragePredictionLeadTimeSeconds;
    
    /** Number of emergency reactivations needed (unpredicted traversals) */
    uint32 EmergencyReactivationCount;
    
    /** Average confidence value for accurate predictions (0-1) */
    float AverageAccurateConfidence;
    
    /** Average confidence value for inaccurate predictions (0-1) */
    float AverageInaccurateConfidence;
    
    /** Number of predictions by confidence level */
    TMap<int32, uint32> PredictionsByConfidenceBucket;
    
    /** Number of predictions that resulted in region reactivation */
    uint32 PredictionsResultingInReactivation;
    
    /** Number of predicted regions already in cache */
    uint32 AlreadyCachedPredictions;
    
    /** Average computation time for predictions in milliseconds */
    float AverageComputationTimeMs;
    
    /** Total number of portal visibility checks */
    uint64 PortalVisibilityChecks;
    
    /** Total number of visible portals detected */
    uint64 VisiblePortalsDetected;
    
    /** Maximum visibility chain depth achieved */
    int32 MaxVisibilityChainDepth;
};

/**
 * Structure containing prediction configuration parameters
 */
struct MININGSPICECOPILOT_API FPredictionConfig
{
    /** Maximum prediction time horizon in seconds */
    float MaxTimeHorizonSeconds;
    
    /** Minimum confidence threshold for region reactivation (0-1) */
    float MinConfidenceThreshold;
    
    /** Whether to consider mining tool selection in predictions */
    bool bConsiderMiningTools;
    
    /** Whether to use portal visibility for prediction */
    bool bUsePortalVisibility;
    
    /** Maximum depth for portal visibility chains */
    int32 MaxVisibilityChainDepth;
    
    /** Whether to use historical traversal patterns */
    bool bUseHistoricalPatterns;
    
    /** Maximum number of regions to predict per update */
    uint32 MaxRegionsPredictedPerUpdate;
    
    /** Minimum distance threshold for portal consideration in cm */
    float MinPortalDistanceThreshold;
    
    /** Whether to use player head direction for prediction */
    bool bUsePlayerHeadDirection;
    
    /** Weighting factor for player velocity (0-1) */
    float VelocityWeightFactor;
    
    /** Weighting factor for head direction (0-1) */
    float HeadDirectionWeightFactor;
    
    /** Weighting factor for historical patterns (0-1) */
    float HistoricalPatternWeightFactor;
    
    /** Weighting factor for portal visibility (0-1) */
    float PortalVisibilityWeightFactor;
    
    /** Weighting factor for mining intent (0-1) */
    float MiningIntentWeightFactor;
    
    /** Constructor with defaults */
    FPredictionConfig()
        : MaxTimeHorizonSeconds(30.0f)
        , MinConfidenceThreshold(0.6f)
        , bConsiderMiningTools(true)
        , bUsePortalVisibility(true)
        , MaxVisibilityChainDepth(3)
        , bUseHistoricalPatterns(true)
        , MaxRegionsPredictedPerUpdate(5)
        , MinPortalDistanceThreshold(5000.0f)
        , bUsePlayerHeadDirection(true)
        , VelocityWeightFactor(0.5f)
        , HeadDirectionWeightFactor(0.2f)
        , HistoricalPatternWeightFactor(0.15f)
        , PortalVisibilityWeightFactor(0.1f)
        , MiningIntentWeightFactor(0.05f)
    {
    }
};

/**
 * Base interface for topological predictors in the SVO+SDF mining architecture
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UTopologicalPredictor : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for region reactivation prediction in the SVO+SDF mining architecture
 * Provides topology-based prediction for seamless cave exploration
 */
class MININGSPICECOPILOT_API ITopologicalPredictor
{
    GENERATED_BODY()

public:
    /**
     * Initializes the topological predictor and prepares it for use
     * @return True if initialization was successful
     */
    virtual bool Initialize() = 0;

    /**
     * Shuts down the topological predictor and cleans up resources
     */
    virtual void Shutdown() = 0;
    
    /**
     * Checks if the topological predictor has been initialized
     * @return True if initialized, false otherwise
     */
    virtual bool IsInitialized() const = 0;
    
    /**
     * Updates player position and velocity for prediction
     * @param PlayerPosition Current player position
     * @param PlayerVelocity Current player velocity
     * @param PlayerHeadDirection Current player head direction
     * @param CurrentRegionId ID of the region containing the player
     */
    virtual void UpdatePlayerState(
        const FVector& PlayerPosition,
        const FVector& PlayerVelocity,
        const FVector& PlayerHeadDirection,
        int32 CurrentRegionId) = 0;
    
    /**
     * Updates the currently equipped mining tool
     * @param ToolType Type ID of the mining tool
     * @param ToolRange Effective range of the tool in cm
     */
    virtual void UpdateMiningTool(int32 ToolType, float ToolRange) = 0;
    
    /**
     * Predicts regions that will need to be reactivated based on player movement
     * @param TimeHorizonSeconds Time horizon for prediction in seconds
     * @param MinConfidence Minimum confidence threshold (0-1)
     * @return Array of region IDs predicted to need reactivation
     */
    virtual TArray<int32> PredictRegionsForReactivation(
        float TimeHorizonSeconds = 15.0f,
        float MinConfidence = 0.6f) = 0;
    
    /**
     * Gets detailed movement prediction information
     * @param TimeHorizonSeconds Time horizon for prediction in seconds
     * @return Structure containing detailed prediction information
     */
    virtual FMovementPrediction GetMovementPrediction(float TimeHorizonSeconds = 15.0f) = 0;
    
    /**
     * Registers a portal between two regions for topological prediction
     * @param PortalInfo Structure containing portal information
     * @return True if portal was registered successfully
     */
    virtual bool RegisterPortal(const FPortalInfo& PortalInfo) = 0;
    
    /**
     * Updates portal visibility state
     * @param PortalId ID of the portal
     * @param bIsVisible Whether the portal is currently visible to the player
     * @param DistanceToPlayer Distance from player to portal in cm
     * @return True if the portal state was updated
     */
    virtual bool UpdatePortalVisibility(int32 PortalId, bool bIsVisible, float DistanceToPlayer) = 0;
    
    /**
     * Gets all visible portals from the player's current position
     * @return Array of portal information structures for visible portals
     */
    virtual TArray<FPortalInfo> GetVisiblePortals() const = 0;
    
    /**
     * Registers player traversal through a portal
     * @param PortalId ID of the traversed portal
     * @param TraversalTimeSeconds Time taken to traverse the portal in seconds
     * @param bWasPredicted Whether this traversal was predicted
     */
    virtual void RegisterPortalTraversal(int32 PortalId, float TraversalTimeSeconds, bool bWasPredicted) = 0;
    
    /**
     * Gets prediction performance metrics
     * @return Structure containing prediction metrics
     */
    virtual FPredictionMetrics GetPredictionMetrics() const = 0;
    
    /**
     * Sets the configuration for the predictor
     * @param Config New prediction configuration
     */
    virtual void SetConfig(const FPredictionConfig& Config) = 0;
    
    /**
     * Gets the current predictor configuration
     * @return Current prediction configuration
     */
    virtual FPredictionConfig GetConfig() const = 0;
    
    /**
     * Performs a portal visibility chain analysis from the player position
     * @param MaxDepth Maximum chain depth
     * @return Array of visible region IDs through portal chains
     */
    virtual TArray<int32> PerformVisibilityChainAnalysis(int32 MaxDepth = 3) = 0;
    
    /**
     * Updates the importance of a region for prediction prioritization
     * @param RegionId ID of the region to update
     * @param Importance Importance score (higher means more important)
     * @return True if importance was updated
     */
    virtual bool UpdateRegionImportance(int32 RegionId, float Importance) = 0;
    
    /**
     * Resets prediction history data for clean state
     */
    virtual void ResetPredictionHistory() = 0;
    
    /**
     * Gets the confidence level for a specific region prediction
     * @param RegionId ID of the region to check
     * @return Confidence level (0-1), or -1 if region is not in prediction set
     */
    virtual float GetRegionPredictionConfidence(int32 RegionId) const = 0;
    
    /**
     * Gets the estimated time until player will reach a region
     * @param RegionId ID of the region to check
     * @return Estimated time in seconds, or -1 if region is not in prediction set
     */
    virtual float GetEstimatedTimeToRegion(int32 RegionId) const = 0;
    
    /**
     * Gets the singleton instance of the topological predictor
     * @return Reference to the topological predictor instance
     */
    static ITopologicalPredictor& Get();
};