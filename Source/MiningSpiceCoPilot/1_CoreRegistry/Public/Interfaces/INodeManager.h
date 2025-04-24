// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "../../Public/SVONodeTypes.h"
#include "INodeManager.generated.h"

// Forward declarations
class ITaskScheduler;

/**
 * Interface for SVO node management services
 * Provides operations for creating, accessing, and manipulating nodes in the sparse voxel octree
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UNodeManager : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for SVO node management
 * Implementations handle region-specific node creation, destruction and traversal operations
 */
class MININGSPICECOPILOT_API INodeManager
{
    GENERATED_BODY()

public:
    /**
     * Creates a new node with the specified class
     * @param InNodeClass The class of node to create
     * @param InParentNode Optional parent node reference
     * @return Node identifier for the created node
     */
    virtual uint64 CreateNode(ESVONodeClass InNodeClass, uint64 InParentNode = 0) = 0;
    
    /**
     * Destroys a node and optionally its children
     * @param InNodeId Node identifier to destroy
     * @param bDestroyChildren Whether to recursively destroy child nodes
     * @return True if the node was successfully destroyed
     */
    virtual bool DestroyNode(uint64 InNodeId, bool bDestroyChildren = true) = 0;
    
    /**
     * Gets node data for the specified node
     * @param InNodeId Node identifier to query
     * @param OutNodeData Output structure to receive node data
     * @return True if the data was retrieved successfully
     */
    virtual bool GetNodeData(uint64 InNodeId, struct FSVONodeData& OutNodeData) const = 0;
    
    /**
     * Sets data for the specified node
     * @param InNodeId Node identifier to update
     * @param InNodeData New data for the node
     * @return True if the data was set successfully
     */
    virtual bool SetNodeData(uint64 InNodeId, const struct FSVONodeData& InNodeData) = 0;
    
    /**
     * Gets the children of a node
     * @param InNodeId Parent node identifier
     * @param OutChildNodes Array to receive child node identifiers
     * @return True if child nodes were retrieved successfully
     */
    virtual bool GetChildNodes(uint64 InNodeId, TArray<uint64>& OutChildNodes) const = 0;
    
    /**
     * Gets the region identifier this manager is responsible for
     * @return Region identifier
     */
    virtual int32 GetRegionId() const = 0;
    
    /**
     * Sets the task scheduler to use for async operations
     * @param InTaskScheduler The task scheduler to use
     */
    virtual void SetTaskScheduler(TSharedPtr<ITaskScheduler> InTaskScheduler) = 0;
    
    /**
     * Flushes pending operations and ensures consistency
     * @return True if the flush operation succeeded
     */
    virtual bool FlushOperations() = 0;
}; 