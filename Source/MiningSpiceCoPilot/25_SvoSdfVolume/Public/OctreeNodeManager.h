// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/SparseArray.h"
#include "Math/UnrealMathSSE.h"
#include "HAL/ThreadSafeCounter64.h"
#include "Templates/Atomic.h"
#include "HAL/ThreadSafeBool.h"
#include "Containers/StaticArray.h"
#include "MiningSpiceCoPilot/2_MemoryManagement/Public/Interfaces/IMemoryTracker.h"
#include "25_SvoSdfVolume/Public/ZOrderCurve.h"
#include "25_SvoSdfVolume/Public/BoxHash.h"

// Forward declarations
class USVOHybridVolume;
class FMaterialSDFManager;
class FMemoryTelemetry;

/**
 * Manages the octree node hierarchy for the SVOHybridVolume
 * Provides sparse octree node allocation and lifecycle management with specialized pools
 * Supports adaptive subdivision based on detail requirements and material complexity
 */
class MININGSPICECOPILOT_API FOctreeNodeManager
{
public:
    // Node classification types
    enum class ENodeType : uint8
    {
        Empty,      // Contains no materials or boundaries
        Homogeneous, // Contains a single material throughout
        Interface   // Contains material boundaries
    };
    
    // Node data structure
    struct FOctreeNode
    {
        ENodeType Type;                          // Type classification
        uint32 MaterialId;                       // Primary material ID for homogeneous nodes
        uint8 Depth;                             // Depth in octree hierarchy
        uint8 SubdivisionLevel;                  // Dynamic subdivision detail level
        FVector Position;                        // Center position in world space
        float Size;                              // Node size (cubic)
        uint32 ParentIndex;                      // Index to parent node (0 for root)
        TStaticArray<uint32, 8> ChildIndices;    // Indices to child nodes
        uint32 MaterialDataIndex;                // Index to material field data
        FThreadSafeBool bLocked;                 // Synchronization lock
        uint64 VersionId;                        // Version tracking for network sync
        
        // Constructor with defaults
        FOctreeNode()
            : Type(ENodeType::Empty)
            , MaterialId(0)
            , Depth(0)
            , SubdivisionLevel(0)
            , Position(FVector::ZeroVector)
            , Size(0.0f)
            , ParentIndex(INDEX_NONE)
            , MaterialDataIndex(INDEX_NONE)
            , bLocked(false)
            , VersionId(0)
        {
            for (int32 i = 0; i < 8; ++i)
            {
                ChildIndices[i] = INDEX_NONE;
            }
        }
        
        // Constructor with parameters
        FOctreeNode(ENodeType InType, uint8 InDepth, const FVector& InPosition, float InSize, uint32 InParentIndex = INDEX_NONE)
            : Type(InType)
            , MaterialId(0)
            , Depth(InDepth)
            , SubdivisionLevel(0)
            , Position(InPosition)
            , Size(InSize)
            , ParentIndex(InParentIndex)
            , MaterialDataIndex(INDEX_NONE)
            , bLocked(false)
            , VersionId(0)
        {
            for (int32 i = 0; i < 8; ++i)
            {
                ChildIndices[i] = INDEX_NONE;
            }
        }
    };
    
    // Statistics for memory tracking
    struct FOctreeStats
    {
        uint32 TotalNodes;
        uint32 EmptyNodes;
        uint32 HomogeneousNodes;
        uint32 InterfaceNodes;
        uint32 LeafNodes;
        uint32 NonLeafNodes;
        uint32 MaxDepth;
        float AverageDepth;
        TMap<int32, uint32> NodesByDepth;
        uint64 TotalMemoryUsage;
        
        FOctreeStats()
            : TotalNodes(0)
            , EmptyNodes(0)
            , HomogeneousNodes(0)
            , InterfaceNodes(0)
            , LeafNodes(0)
            , NonLeafNodes(0)
            , MaxDepth(0)
            , AverageDepth(0.0f)
            , TotalMemoryUsage(0)
        {}
    };

public:
    // Constructor and destructor
    FOctreeNodeManager();
    virtual ~FOctreeNodeManager();
    
    // Initialization
    void Initialize(const FIntVector& WorldDimensions, float LeafNodeSize, uint8 MaxDepth);
    void SetMaterialManager(FMaterialSDFManager* InMaterialManager);
    void SetMemoryTelemetry(FMemoryTelemetry* InTelemetry);
    
    // Core node operations
    uint32 CreateNode(ENodeType Type, const FVector& Position, float Size, uint32 ParentIndex = INDEX_NONE);
    void SubdivideNode(uint32 NodeIndex, bool bRecursive = false);
    void CollapseNode(uint32 NodeIndex, bool bRecursive = false);
    void RemoveNode(uint32 NodeIndex, bool bRecursiveRemoveChildren = true);
    void UpdateNodeType(uint32 NodeIndex, ENodeType NewType);
    
    // Node access and queries
    FOctreeNode* GetNode(uint32 NodeIndex);
    const FOctreeNode* GetNode(uint32 NodeIndex) const;
    uint32 FindNodeAtPosition(const FVector& Position) const;
    uint32 FindNodeAtPositionAndDepth(const FVector& Position, uint8 TargetDepth) const;
    uint32 FindClosestNode(const FVector& Position, ENodeType TypeFilter = ENodeType::Interface) const;
    TArray<uint32> FindNodesInBox(const FBox& Box, bool bIncludePartial = true) const;
    TArray<uint32> FindNodesInSphere(const FVector& Center, float Radius) const;
    TArray<uint32> FindLeafNodes(const FBox& Box) const;
    
    // Material interface
    void SetNodeMaterial(uint32 NodeIndex, uint32 MaterialId);
    uint32 GetNodeMaterial(uint32 NodeIndex) const;
    void LinkNodeToMaterialData(uint32 NodeIndex, uint32 MaterialDataIndex);
    uint32 GetNodeMaterialDataIndex(uint32 NodeIndex) const;
    
    // Adaptive subdivision
    void SetMinimumSubdivision(uint8 MinLevel);
    void SetMaximumSubdivision(uint8 MaxLevel);
    void SubdivideRegion(const FBox& Region, uint8 TargetDepth);
    void OptimizeRegion(const FBox& Region, float DetailThreshold = 0.1f);
    void AdaptSubdivisionToDetail(uint32 NodeIndex, float DetailThreshold = 0.1f);
    
    // Memory optimization
    void OptimizeMemoryUsage();
    void PrioritizeRegion(const FBox& Region, uint8 Priority);
    void CompactNodes();
    void ReleaseUnusedMemory();
    
    // Thread safety
    bool LockNode(uint32 NodeIndex, bool bWait = true);
    void UnlockNode(uint32 NodeIndex);
    bool TryModifyNode(uint32 NodeIndex, TFunction<void(FOctreeNode*)> ModifyFunc);
    
    // Traversal and iteration
    void TraverseDepthFirst(TFunction<bool(uint32, FOctreeNode*)> VisitorFunc, uint32 StartNodeIndex = 0) const;
    void TraverseBreadthFirst(TFunction<bool(uint32, FOctreeNode*)> VisitorFunc, uint32 StartNodeIndex = 0) const;
    void TraverseLeafNodes(TFunction<bool(uint32, FOctreeNode*)> VisitorFunc) const;
    
    // Serialization
    void Serialize(FArchive& Ar);
    TArray<uint8> SerializeToBuffer(bool bCompressed = true) const;
    bool DeserializeFromBuffer(const TArray<uint8>& Data);
    
    // Network synchronization
    void RegisterNodeVersion(uint32 NodeIndex, uint64 VersionId);
    uint64 GetNodeVersion(uint32 NodeIndex) const;
    TArray<uint32> GetNodesModifiedSince(uint64 BaseVersion) const;
    TArray<uint8> GenerateNetworkDelta(uint64 BaseVersion) const;
    bool ApplyNetworkDelta(const TArray<uint8>& DeltaData, uint64 BaseVersion);
    
    // Statistics and info
    FOctreeStats GetStatistics() const;
    uint32 GetRootNodeIndex() const { return RootNodeIndex; }
    uint32 GetNodeCount() const { return static_cast<uint32>(Nodes.Num()); }
    uint8 GetMaxDepth() const { return MaxOctreeDepth; }
    float GetLeafNodeSize() const { return LeafNodeSize; }
    FBox GetWorldBounds() const { return WorldBounds; }
    
    // Utility
    bool IsValidNodeIndex(uint32 NodeIndex) const;
    bool IsLeafNode(uint32 NodeIndex) const;
    uint32 GetParentNode(uint32 NodeIndex) const;
    TArray<uint32> GetChildNodes(uint32 NodeIndex) const;
    FVector GetNodePosition(uint32 NodeIndex) const;
    float GetNodeSize(uint32 NodeIndex) const;
    FBox GetNodeBounds(uint32 NodeIndex) const;
    
private:
    // Internal data
    TSparseArray<FOctreeNode> Nodes;
    uint32 RootNodeIndex;
    FMaterialSDFManager* MaterialManager;
    FMemoryTelemetry* MemoryTelemetry;
    FZOrderCurve ZCurve;
    
    // Configuration
    FIntVector WorldDimensions;
    float LeafNodeSize;
    uint8 MaxOctreeDepth;
    uint8 MinSubdivisionLevel;
    uint8 MaxSubdivisionLevel;
    FBox WorldBounds;
    
    // Memory pools
    TArray<FOctreeNode*> EmptyNodePool;
    TArray<FOctreeNode*> HomogeneousNodePool;
    TArray<FOctreeNode*> InterfaceNodePool;
    
    // Network state tracking
    FThreadSafeCounter64 CurrentVersionCounter;
    TMap<uint64, TSet<uint32>> VersionNodeMap;
    
    // Internal helpers
    uint32 InternalCreateNode(ENodeType Type, const FVector& Position, float Size, uint32 ParentIndex);
    void RecursiveSubdivide(uint32 NodeIndex, uint8 TargetDepth);
    void RecursiveCollapse(uint32 NodeIndex);
    uint32 FindNodeRecursive(const FVector& Position, uint32 CurrentNodeIndex) const;
    uint32 FindNodeAtDepthRecursive(const FVector& Position, uint8 TargetDepth, uint32 CurrentNodeIndex, uint8 CurrentDepth) const;
    void CollectNodesInBox(const FBox& Box, uint32 NodeIndex, TArray<uint32>& OutIndices, bool bIncludePartial) const;
    void UpdateStats();
    void AllocateNodeFromPool(FOctreeNode& Node);
    void ReturnNodeToPool(FOctreeNode& Node);
    bool BoxIntersectsNode(const FBox& Box, uint32 NodeIndex) const;
    bool SphereIntersectsNode(const FVector& Center, float Radius, uint32 NodeIndex) const;
    uint32 GetOctant(const FVector& Position, const FVector& NodeCenter) const;
    FVector GetChildPosition(const FVector& ParentPosition, float ParentSize, uint32 ChildOctant) const;
};
