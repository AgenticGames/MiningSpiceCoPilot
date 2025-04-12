// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IEventHandler.h"

/**
 * Material boundary operation types
 */
enum class EMaterialBoundaryOperation : uint8
{
    /** Boundary created */
    Created,
    
    /** Boundary modified */
    Modified,
    
    /** Boundary removed */
    Removed,
    
    /** Materials on boundary changed */
    MaterialsChanged,
    
    /** Boundary properties changed */
    PropertiesChanged
};

/**
 * Material boundary event types as FName constants
 */
struct MININGSPICECOPILOT_API FMaterialBoundaryEventTypes
{
    static const FName BoundaryCreated;
    static const FName BoundaryModified;
    static const FName BoundaryRemoved;
    static const FName BoundaryMaterialsChanged;
    static const FName BoundaryPropertiesChanged;
    static const FName BoundaryIntersection;
    static const FName BoundaryTransition;
    static const FName MaterialGradientUpdated;
    static const FName MaterialErosion;
    static const FName MaterialDeposition;
    static const FName CSGOperationResult;
};

/**
 * Material boundary event data
 */
struct MININGSPICECOPILOT_API FMaterialBoundaryEventData
{
    /** Boundary ID */
    FGuid BoundaryId;
    
    /** Operation that occurred */
    EMaterialBoundaryOperation Operation;
    
    /** Region ID */
    int32 RegionId;
    
    /** Zone ID (INDEX_NONE for region-wide) */
    int32 ZoneId;
    
    /** Affected SVO node IDs */
    TArray<FGuid> NodeIds;
    
    /** Materials on first side of boundary */
    TArray<uint8> MaterialsA;
    
    /** Materials on second side of boundary */
    TArray<uint8> MaterialsB;
    
    /** Previous materials on first side (if changed) */
    TArray<uint8> PreviousMaterialsA;
    
    /** Previous materials on second side (if changed) */
    TArray<uint8> PreviousMaterialsB;
    
    /** Center position of boundary */
    FVector CenterPosition;
    
    /** Approximate surface area */
    float SurfaceArea;
    
    /** Tool ID that caused the change (if applicable) */
    FGuid ToolId;
    
    /** Transaction ID for grouped modifications */
    FGuid TransactionId;
    
    /**
     * Converts the boundary event data to JSON
     * @return Boundary event data as JSON object
     */
    TSharedRef<FJsonObject> ToJson() const;
    
    /**
     * Creates boundary event data from JSON
     * @param JsonObject JSON object to parse
     * @return Boundary event data
     */
    static FMaterialBoundaryEventData FromJson(const TSharedRef<FJsonObject>& JsonObject);
    
    /** Default constructor */
    FMaterialBoundaryEventData()
        : Operation(EMaterialBoundaryOperation::Created)
        , RegionId(INDEX_NONE)
        , ZoneId(INDEX_NONE)
        , SurfaceArea(0.0f)
    {
    }
};

/**
 * Material gradient update event data
 */
struct MININGSPICECOPILOT_API FMaterialGradientEventData
{
    /** Region ID */
    int32 RegionId;
    
    /** Zone ID (INDEX_NONE for region-wide) */
    int32 ZoneId;
    
    /** Center position of gradient update */
    FVector CenterPosition;
    
    /** Radius of update */
    float Radius;
    
    /** Materials involved */
    TArray<uint8> Materials;
    
    /** Strength of gradient (0-1) */
    float Strength;
    
    /** Affected SVO node IDs */
    TArray<FGuid> NodeIds;
    
    /** Whether this update created new boundaries */
    bool bCreatedNewBoundaries;
    
    /** Whether this update removed existing boundaries */
    bool bRemovedBoundaries;
    
    /** Tool ID that caused the change (if applicable) */
    FGuid ToolId;
    
    /** Transaction ID for grouped modifications */
    FGuid TransactionId;
    
    /**
     * Converts the gradient event data to JSON
     * @return Gradient event data as JSON object
     */
    TSharedRef<FJsonObject> ToJson() const;
    
    /**
     * Creates gradient event data from JSON
     * @param JsonObject JSON object to parse
     * @return Gradient event data
     */
    static FMaterialGradientEventData FromJson(const TSharedRef<FJsonObject>& JsonObject);
    
    /** Default constructor */
    FMaterialGradientEventData()
        : RegionId(INDEX_NONE)
        , ZoneId(INDEX_NONE)
        , Radius(0.0f)
        , Strength(0.0f)
        , bCreatedNewBoundaries(false)
        , bRemovedBoundaries(false)
    {
    }
};

/**
 * CSG operation result event data
 */
struct MININGSPICECOPILOT_API FCSGOperationEventData
{
    /** Operation type */
    FName OperationType;
    
    /** Region ID */
    int32 RegionId;
    
    /** Zone ID (INDEX_NONE for region-wide) */
    int32 ZoneId;
    
    /** Center position of operation */
    FVector CenterPosition;
    
    /** Radius of effect */
    float Radius;
    
    /** SDF channel used (0 for default) */
    int32 ChannelId;
    
    /** Materials involved */
    TArray<uint8> Materials;
    
    /** Number of nodes modified */
    int32 NodesModified;
    
    /** Volume added (cubic units) */
    float VolumeAdded;
    
    /** Volume removed (cubic units) */
    float VolumeRemoved;
    
    /** Tool ID that caused the operation */
    FGuid ToolId;
    
    /** Transaction ID for grouped operations */
    FGuid TransactionId;
    
    /** Whether this operation was successful */
    bool bSuccess;
    
    /** Error message if operation failed */
    FString ErrorMessage;
    
    /**
     * Converts the CSG operation data to JSON
     * @return CSG operation data as JSON object
     */
    TSharedRef<FJsonObject> ToJson() const;
    
    /**
     * Creates CSG operation data from JSON
     * @param JsonObject JSON object to parse
     * @return CSG operation data
     */
    static FCSGOperationEventData FromJson(const TSharedRef<FJsonObject>& JsonObject);
    
    /** Default constructor */
    FCSGOperationEventData()
        : RegionId(INDEX_NONE)
        , ZoneId(INDEX_NONE)
        , Radius(0.0f)
        , ChannelId(0)
        , NodesModified(0)
        , VolumeAdded(0.0f)
        , VolumeRemoved(0.0f)
        , bSuccess(true)
    {
    }
};

/**
 * Helper class for creating material boundary events
 */
class MININGSPICECOPILOT_API FMaterialBoundaryEventFactory
{
public:
    /**
     * Creates a boundary event
     * @param BoundaryData Boundary event data
     * @param Priority Event priority
     * @return Event data ready for publishing
     */
    static FEventData CreateBoundaryEvent(
        const FMaterialBoundaryEventData& BoundaryData,
        EEventPriority Priority = EEventPriority::Normal
    );
    
    /**
     * Creates a gradient update event
     * @param GradientData Gradient event data
     * @param Priority Event priority
     * @return Event data ready for publishing
     */
    static FEventData CreateGradientEvent(
        const FMaterialGradientEventData& GradientData,
        EEventPriority Priority = EEventPriority::Normal
    );
    
    /**
     * Creates a CSG operation result event
     * @param OperationData CSG operation data
     * @param Priority Event priority
     * @return Event data ready for publishing
     */
    static FEventData CreateCSGOperationEvent(
        const FCSGOperationEventData& OperationData,
        EEventPriority Priority = EEventPriority::Normal
    );
    
    /**
     * Creates a material erosion event
     * @param RegionId Region ID
     * @param ZoneId Zone ID (optional)
     * @param Position Center position
     * @param Radius Radius of effect
     * @param Material Material being eroded
     * @param Amount Amount of material eroded
     * @param ToolId Tool ID causing the erosion
     * @param Priority Event priority
     * @return Event data ready for publishing
     */
    static FEventData CreateMaterialErosionEvent(
        int32 RegionId,
        int32 ZoneId,
        const FVector& Position,
        float Radius,
        uint8 Material,
        float Amount,
        const FGuid& ToolId,
        EEventPriority Priority = EEventPriority::Normal
    );
    
    /**
     * Creates a material deposition event
     * @param RegionId Region ID
     * @param ZoneId Zone ID (optional)
     * @param Position Center position
     * @param Radius Radius of effect
     * @param Material Material being deposited
     * @param Amount Amount of material deposited
     * @param ToolId Tool ID causing the deposition
     * @param Priority Event priority
     * @return Event data ready for publishing
     */
    static FEventData CreateMaterialDepositionEvent(
        int32 RegionId,
        int32 ZoneId,
        const FVector& Position,
        float Radius,
        uint8 Material,
        float Amount,
        const FGuid& ToolId,
        EEventPriority Priority = EEventPriority::Normal
    );
};