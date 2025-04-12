// OctreeNodeManager.h
// Manages the sparse octree node allocation and lifecycle

#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "HAL/ThreadSafeCounter64.h"
#include "Templates/UniquePtr.h"

/**
 * Manages sparse octree node allocation and lifecycle with specialized pools
 * Handles node type classification, adaptive subdivision, and memory optimization
 * Supports thread-safe access for parallel operations and network replication
 */
class MININGSPICECOPILOT_API FOctreeNodeManager
{
public:
    FOctreeNodeManager();
    ~FOctreeNodeManager();

    // Node structure enumerations
    enum class ENodeType : uint8
    {
        Empty,
        Homogeneous,
        Interface
    };

    // Initialization
    void Initialize(uint8 MaxDepth, uint32 InitialCapacity);
    void SetWorldDimensions(const FIntVector& WorldDimensions, float LeafNodeSize);

    // Node access and manipulation
    uint32 AllocateNode(ENodeType NodeType, uint8 Depth);
    void ReleaseNode(uint32 NodeIndex);
    void SubdivideNode(uint32 NodeIndex);
    void CollapseNode(uint32 NodeIndex);

    // Node traversal
    uint32 FindLeafNodeAt(const FVector& WorldPosition) const;
    TArray<uint32> FindNodesInRegion(const FBox& Region, bool OnlyLeafNodes = false) const;
    bool TraceRay(const FVector& Start, const FVector& Direction, float MaxDistance, FVector& OutHitPoint) const;

    // Node properties
    ENodeType GetNodeType(uint32 NodeIndex) const;
    FBox GetNodeBounds(uint32 NodeIndex) const;
    uint8 GetNodeDepth(uint32 NodeIndex) const;
    bool IsNodeLeaf(uint32 NodeIndex) const;
    FVector GetNodeCenter(uint32 NodeIndex) const;
    float GetNodeSize(uint32 NodeIndex) const;

    // Memory management
    void OptimizeMemoryUsage();
    uint64 GetMemoryUsage() const;
    void PreallocateNodes(ENodeType NodeType, uint32 Count);

    // Network synchronization
    void SerializeNodeTree(FArchive& Ar);
    void SerializeNodeTreeDelta(FArchive& Ar, uint64 BaseVersion);
    TArray<uint8> GenerateNodeDelta(uint64 BaseVersion, uint64 CurrentVersion) const;
    void ApplyNodeDelta(const TArray<uint8>& DeltaData);
    bool ValidateNodeModification(uint32 NodeIndex) const;
    uint64 GetCurrentNodeVersion() const;

private:
    // Internal data structures
    struct FNodePool;
    struct FNodeData;
    struct FNodeChild;
    
    // Implementation details
    TUniquePtr<FNodePool> NodePools[3]; // One pool per node type
    TArray<FNodeData> Nodes;
    uint8 MaxTreeDepth;
    FVector WorldOrigin;
    FVector WorldExtent;
    float BaseNodeSize;
    FThreadSafeCounter64 NodeVersionCounter;

    // Internal helper methods
    uint32 CreateRootNode();
    void RecursiveBuildTree(uint32 NodeIndex, uint8 Depth);
    bool ShouldSubdivideNode(uint32 NodeIndex) const;
    bool TryOptimisticNodeAccess(uint32 NodeIndex) const;
    void ReleaseNodeAndChildren(uint32 NodeIndex);
    uint32 FindChildNodeContaining(uint32 ParentIndex, const FVector& WorldPosition) const;
};