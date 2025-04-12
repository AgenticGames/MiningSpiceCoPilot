// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IEventHandler.h"

/**
 * SVO node operation types
 */
enum class ESVONodeOperation : uint8
{
    /** Node created */
    Created,
    
    /** Node split into children */
    Split,
    
    /** Node merged from children */
    Merged,
    
    /** Node material changed */
    MaterialChanged,
    
    /** Node deleted */
    Deleted,
    
    /** Node value changed */
    ValueChanged,
    
    /** Node detail level changed */
    LODChanged,
    
    /** Node neighbors updated */
    NeighborsUpdated
};

/**
 * SVO event types as FName constants
 */
struct MININGSPICECOPILOT_API FSVOEventTypes
{
    static const FName NodeCreated;
    static const FName NodeSplit;
    static const FName NodeMerged;
    static const FName NodeMaterialChanged;
    static const FName NodeDeleted;
    static const FName NodeValueChanged;
    static const FName NodeLODChanged;
    static const FName NodeNeighborsUpdated;
    static const FName VolumeModified;
    static const FName RegionStructureChanged;
    static const FName HierarchyRebuilt;
    static const FName NarrowBandUpdated;
    static const FName MaterialBoundaryUpdated;
};

/**
 * SVO node event payload
 */
struct MININGSPICECOPILOT_API FSVONodeEventData
{
    /** Node ID */
    FGuid NodeId;
    
    /** Operation performed */
    ESVONodeOperation Operation;
    
    /** Node depth in the octree */
    int32 Depth;
    
    /** Node position in octree coordinates */
    FIntVector Position;
    
    /** Node size in octree coordinates */
    int32 Size;
    
    /** Material indices for the node */
    TArray<uint8> Materials;
    
    /** Density values for the node */
    TArray<float> Densities;
    
    /** Parent node ID (if applicable) */
    FGuid ParentId;
    
    /** Child node IDs (if applicable) */
    TArray<FGuid> ChildIds;
    
    /** Previous material indices (for material change) */
    TArray<uint8> PreviousMaterials;
    
    /** Previous density values (for value change) */
    TArray<float> PreviousDensities;
    
    /** Whether this node contains a material boundary */
    bool bHasMaterialBoundary;
    
    /** Whether this node is in the narrow band */
    bool bIsInNarrowBand;
    
    /** Tool ID that caused the change (if applicable) */
    FGuid ToolId;
    
    /**
     * Converts the node event data to JSON
     * @return Node event data as JSON object
     */
    TSharedRef<FJsonObject> ToJson() const;
    
    /**
     * Creates node event data from JSON
     * @param JsonObject JSON object to parse
     * @return Node event data
     */
    static FSVONodeEventData FromJson(const TSharedRef<FJsonObject>& JsonObject);
    
    /** Default constructor */
    FSVONodeEventData()
        : Operation(ESVONodeOperation::Created)
        , Depth(0)
        , Size(1)
        , bHasMaterialBoundary(false)
        , bIsInNarrowBand(false)
    {
    }
};

/**
 * Volume modification event payload
 */
struct MININGSPICECOPILOT_API FSVOVolumeModificationData
{
    /** Affected region ID */
    int32 RegionId;
    
    /** Affected zone ID (INDEX_NONE for region-wide) */
    int32 ZoneId;
    
    /** Center of the modification */
    FVector Center;
    
    /** Radius of the modification */
    float Radius;
    
    /** Tool type used for the modification */
    FName ToolType;
    
    /** Tool ID that caused the change */
    FGuid ToolId;
    
    /** Operation strength */
    float Strength;
    
    /** Materials affected by the modification */
    TArray<uint8> AffectedMaterials;
    
    /** Volume of material removed (per material) */
    TMap<uint8, float> MaterialVolumesRemoved;
    
    /** Volume of material added (per material) */
    TMap<uint8, float> MaterialVolumesAdded;
    
    /** Nodes modified by this operation */
    TArray<FGuid> ModifiedNodeIds;
    
    /** Whether this modification created any material boundaries */
    bool bCreatedMaterialBoundaries;
    
    /** Whether this modification merged any nodes */
    bool bMergedNodes;
    
    /** Whether this modification split any nodes */
    bool bSplitNodes;
    
    /** Transaction ID for grouped modifications */
    FGuid TransactionId;
    
    /**
     * Converts the volume modification data to JSON
     * @return Volume modification data as JSON object
     */
    TSharedRef<FJsonObject> ToJson() const;
    
    /**
     * Creates volume modification data from JSON
     * @param JsonObject JSON object to parse
     * @return Volume modification data
     */
    static FSVOVolumeModificationData FromJson(const TSharedRef<FJsonObject>& JsonObject);
    
    /** Default constructor */
    FSVOVolumeModificationData()
        : RegionId(INDEX_NONE)
        , ZoneId(INDEX_NONE)
        , Radius(0.0f)
        , Strength(0.0f)
        , bCreatedMaterialBoundaries(false)
        , bMergedNodes(false)
        , bSplitNodes(false)
    {
    }
};

/**
 * Helper class for creating SVO events
 */
class MININGSPICECOPILOT_API FSVOEventFactory
{
public:
    /**
     * Creates a node operation event
     * @param NodeData Node event data
     * @param RegionId Region ID
     * @param ZoneId Zone ID (optional)
     * @param Priority Event priority
     * @return Event data ready for publishing
     */
    static FEventData CreateNodeEvent(
        const FSVONodeEventData& NodeData,
        int32 RegionId,
        int32 ZoneId = INDEX_NONE,
        EEventPriority Priority = EEventPriority::Normal
    );
    
    /**
     * Creates a volume modification event
     * @param ModificationData Volume modification data
     * @param Priority Event priority
     * @return Event data ready for publishing
     */
    static FEventData CreateVolumeModificationEvent(
        const FSVOVolumeModificationData& ModificationData,
        EEventPriority Priority = EEventPriority::Normal
    );
    
    /**
     * Creates a region structure change event
     * @param RegionId Region ID
     * @param TransactionId Transaction ID
     * @param NumNodesAdded Number of nodes added
     * @param NumNodesRemoved Number of nodes removed
     * @param NumNodesSplit Number of nodes split
     * @param NumNodesMerged Number of nodes merged
     * @param Priority Event priority
     * @return Event data ready for publishing
     */
    static FEventData CreateRegionStructureChangedEvent(
        int32 RegionId,
        const FGuid& TransactionId,
        int32 NumNodesAdded = 0,
        int32 NumNodesRemoved = 0,
        int32 NumNodesSplit = 0,
        int32 NumNodesMerged = 0,
        EEventPriority Priority = EEventPriority::Normal
    );
    
    /**
     * Creates a hierarchy rebuild event
     * @param RegionId Region ID
     * @param RebuildReason Reason for the rebuild
     * @param NumNodesBeforeRebuild Number of nodes before rebuild
     * @param NumNodesAfterRebuild Number of nodes after rebuild
     * @param Priority Event priority
     * @return Event data ready for publishing
     */
    static FEventData CreateHierarchyRebuiltEvent(
        int32 RegionId,
        const FString& RebuildReason,
        int32 NumNodesBeforeRebuild,
        int32 NumNodesAfterRebuild,
        EEventPriority Priority = EEventPriority::Normal
    );
    
    /**
     * Creates a narrow band update event
     * @param RegionId Region ID
     * @param ZoneId Zone ID (optional)
     * @param UpdatedNodeIds IDs of updated nodes
     * @param CenterPosition Center of the update
     * @param Radius Radius of the update
     * @param Priority Event priority
     * @return Event data ready for publishing
     */
    static FEventData CreateNarrowBandUpdatedEvent(
        int32 RegionId,
        int32 ZoneId,
        const TArray<FGuid>& UpdatedNodeIds,
        const FVector& CenterPosition,
        float Radius,
        EEventPriority Priority = EEventPriority::Normal
    );
    
    /**
     * Creates a material boundary update event
     * @param RegionId Region ID
     * @param ZoneId Zone ID (optional)
     * @param UpdatedNodeIds IDs of nodes with updated material boundaries
     * @param Materials Materials involved in the boundaries
     * @param BoundaryArea Approximate surface area of the boundaries
     * @param Priority Event priority
     * @return Event data ready for publishing
     */
    static FEventData CreateMaterialBoundaryUpdatedEvent(
        int32 RegionId,
        int32 ZoneId,
        const TArray<FGuid>& UpdatedNodeIds,
        const TArray<uint8>& Materials,
        float BoundaryArea,
        EEventPriority Priority = EEventPriority::Normal
    );
};