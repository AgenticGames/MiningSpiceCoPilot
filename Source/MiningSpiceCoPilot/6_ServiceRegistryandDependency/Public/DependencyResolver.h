// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IRegistry.h"
#include "Containers/Map.h"
#include "Templates/SharedPointer.h"
#include "Templates/Function.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/CriticalSection.h"
#include "Containers/Queue.h"
#include "Misc/Optional.h"

/**
 * Dependency resolver for service registry and dependency management
 * Provides dependency tracking, initialization order determination, and cycle detection
 * Works with registry system to ensure proper service initialization based on dependencies
 */
class MININGSPICECOPILOT_API FDependencyResolver
{
public:
    /** Default constructor */
    FDependencyResolver();
    
    /** Destructor */
    ~FDependencyResolver();

    /** Dependency type enumeration */
    enum class EDependencyType : uint8
    {
        /** Dependency must be satisfied for initialization */
        Required,
        
        /** Dependency is optional and can be missing */
        Optional
    };

    /** Hardware capability flags for conditional dependencies */
    enum class EHardwareCapability : uint32
    {
        None        = 0,
        SSE2        = 1 << 0,
        AVX         = 1 << 1,
        AVX2        = 1 << 2,
        AVX512      = 1 << 3,
        GPU         = 1 << 4,
        MultiCore   = 1 << 5,
        LargeMemory = 1 << 6,
        CUDA        = 1 << 7,
        DirectX12   = 1 << 8
    };

    /** Status of dependency resolution */
    enum class EResolutionStatus : uint8
    {
        /** Resolution successful with no issues */
        Success,
        
        /** Resolution failed due to cycles */
        FailedWithCycles,
        
        /** Resolution failed due to missing dependencies */
        FailedWithMissingDependencies,
        
        /** Resolution failed due to invalid nodes */
        FailedWithInvalidNodes,
        
        /** Resolution not attempted yet */
        NotAttempted
    };

    /** Node status for cycle detection */
    enum class ENodeVisitStatus : uint8
    {
        /** Node not yet visited */
        NotVisited,
        
        /** Node currently being visited (used for cycle detection) */
        InProgress,
        
        /** Node has been fully visited */
        Visited
    };

    /** Structure representing an edge in the dependency graph */
    struct FDependencyEdge
    {
        /** ID of the source node */
        uint32 SourceId;
        
        /** ID of the target node */
        uint32 TargetId;
        
        /** Type of dependency */
        EDependencyType Type;
        
        /** Optional condition function that determines if this dependency is active */
        TFunction<bool()> Condition;
        
        /** Hardware capabilities required for this dependency */
        uint32 RequiredCapabilities;
        
        /** Whether this edge is currently active based on conditions and hardware capabilities */
        bool bIsActive;
        
        /** Default constructor */
        FDependencyEdge()
            : SourceId(0)
            , TargetId(0)
            , Type(EDependencyType::Required)
            , RequiredCapabilities(0)
            , bIsActive(true)
        {
        }
        
        /** Constructor with source and target */
        FDependencyEdge(uint32 InSourceId, uint32 InTargetId, EDependencyType InType = EDependencyType::Required)
            : SourceId(InSourceId)
            , TargetId(InTargetId)
            , Type(InType)
            , RequiredCapabilities(0)
            , bIsActive(true)
        {
        }
    };

    /** Structure representing a node in the dependency graph */
    struct FDependencyNode
    {
        /** Unique ID of the node */
        uint32 Id;
        
        /** Name of the node */
        FName Name;
        
        /** Registry this node belongs to */
        IRegistry* Registry;
        
        /** Type ID within the registry (if applicable) */
        uint32 TypeId;
        
        /** Outgoing dependency edges */
        TArray<FDependencyEdge> Dependencies;
        
        /** IDs of nodes that depend on this node */
        TArray<uint32> DependentIds;
        
        /** Visit status for cycle detection */
        ENodeVisitStatus VisitStatus;
        
        /** Default constructor */
        FDependencyNode()
            : Id(0)
            , Registry(nullptr)
            , TypeId(0)
            , VisitStatus(ENodeVisitStatus::NotVisited)
        {
        }
    };

    /** Information about a detected cycle */
    struct FCycleInfo
    {
        /** Nodes involved in the cycle, in order */
        TArray<uint32> CycleNodes;
        
        /** Names of nodes in the cycle, for error reporting */
        TArray<FName> CycleNodeNames;
        
        /** Formatted description of the cycle */
        FString Description;
    };

    /**
     * Registers a dependency between two nodes
     * @param SourceId ID of the dependent node
     * @param TargetId ID of the node being depended on
     * @param Type Type of dependency (required or optional)
     * @return True if registration was successful
     */
    bool RegisterDependency(uint32 SourceId, uint32 TargetId, EDependencyType Type = EDependencyType::Required);

    /**
     * Registers a conditional dependency between two nodes
     * @param SourceId ID of the dependent node
     * @param TargetId ID of the node being depended on
     * @param Condition Function that returns whether the dependency is active
     * @param Type Type of dependency (required or optional)
     * @return True if registration was successful
     */
    bool RegisterConditionalDependency(
        uint32 SourceId, 
        uint32 TargetId, 
        TFunction<bool()> Condition,
        EDependencyType Type = EDependencyType::Required);

    /**
     * Registers a dependency based on hardware capabilities
     * @param SourceId ID of the dependent node
     * @param TargetId ID of the node being depended on
     * @param RequiredCapabilities Bit flags of required hardware capabilities
     * @param Type Type of dependency (required or optional)
     * @return True if registration was successful
     */
    bool RegisterHardwareDependency(
        uint32 SourceId, 
        uint32 TargetId, 
        uint32 RequiredCapabilities,
        EDependencyType Type = EDependencyType::Required);

    /**
     * Registers a node in the dependency graph
     * @param Id Unique ID for the node
     * @param Name Name of the node
     * @param Registry Registry this node belongs to (if any)
     * @param TypeId Type ID within the registry (if applicable)
     * @return True if registration was successful
     */
    bool RegisterNode(uint32 Id, const FName& Name, IRegistry* Registry = nullptr, uint32 TypeId = 0);

    /**
     * Registers a registry to automatically extract its type dependencies
     * @param Registry Registry to register
     * @return True if registration was successful
     */
    bool RegisterRegistry(IRegistry* Registry);

    /**
     * Builds the dependency graph based on registered nodes and dependencies
     * @param OutErrors Array to receive any errors encountered during building
     * @return True if graph was built successfully
     */
    bool BuildDependencyGraph(TArray<FString>& OutErrors);

    /**
     * Determines the initialization order based on dependencies
     * @param OutOrder Array to receive the initialization order
     * @param OutErrors Array to receive any errors encountered during ordering
     * @return Status of the resolution process
     */
    EResolutionStatus DetermineInitializationOrder(TArray<uint32>& OutOrder, TArray<FString>& OutErrors);

    /**
     * Detects cycles in the dependency graph
     * @param OutCycles Array to receive information about detected cycles
     * @param OutErrors Array to receive error descriptions
     * @return True if no cycles were detected
     */
    bool DetectCycles(TArray<FCycleInfo>& OutCycles, TArray<FString>& OutErrors);

    /**
     * Validates the dependency graph for completeness and correctness
     * @param OutErrors Array to receive validation error descriptions
     * @return True if validation was successful
     */
    bool ValidateDependencies(TArray<FString>& OutErrors);

    /**
     * Validates a specific node's dependencies
     * @param NodeId ID of the node to validate
     * @param OutErrors Array to receive validation error descriptions
     * @return True if validation was successful
     */
    bool ValidateNodeDependencies(uint32 NodeId, TArray<FString>& OutErrors);

    /**
     * Sets hardware capabilities for conditional dependencies
     * @param Capabilities Bit flags of available hardware capabilities
     */
    void SetHardwareCapabilities(uint32 Capabilities);

    /**
     * Detects available hardware capabilities on the current system
     * @return Bit flags of detected hardware capabilities
     */
    uint32 DetectHardwareCapabilities();

    /**
     * Evaluates conditional dependencies to determine which are active
     */
    void EvaluateConditionalDependencies();

    /**
     * Gets a node by ID
     * @param Id Node ID to find
     * @return Pointer to the node, or nullptr if not found
     */
    const FDependencyNode* GetNode(uint32 Id) const;

    /**
     * Gets a node by name
     * @param Name Node name to find
     * @return Pointer to the node, or nullptr if not found
     */
    const FDependencyNode* GetNodeByName(const FName& Name) const;

    /**
     * Clears the dependency graph and all registrations
     */
    void Clear();

    /**
     * Gets the count of registered nodes
     * @return Number of registered nodes
     */
    uint32 GetNodeCount() const;

    /**
     * Gets all registered nodes
     * @return Array of dependency nodes
     */
    TArray<FDependencyNode> GetAllNodes() const;

    /**
     * Gets information about cycles for visualization
     * @return Array of cycle information structures
     */
    TArray<FCycleInfo> GetCycleInformation() const;

    /**
     * Generates a visualization of the dependency graph
     * @param OutVisualizationData String containing visualization data
     * @return True if visualization was generated successfully
     */
    bool GenerateGraphVisualization(FString& OutVisualizationData) const;

    /**
     * Gets required dependencies that are missing
     * @return Array of missing dependency descriptions
     */
    TArray<FString> GetMissingDependencies() const;

    /**
     * Checks if a cycle exists between two nodes
     * @param SourceId Source node ID
     * @param TargetId Target node ID
     * @return True if adding a dependency would create a cycle
     */
    bool WouldCreateCycle(uint32 SourceId, uint32 TargetId) const;

    /**
     * Gets dependencies of a specific node
     * @param NodeId ID of the node
     * @return Array of dependency edges
     */
    TArray<FDependencyEdge> GetNodeDependencies(uint32 NodeId) const;

    /**
     * Gets nodes that depend on a specific node
     * @param NodeId ID of the node
     * @return Array of dependent node IDs
     */
    TArray<uint32> GetNodeDependents(uint32 NodeId) const;

    /**
     * Gets a node ID by name
     * @param NodeName The name of the node to find
     * @return The node ID, or 0 if not found
     */
    uint32 GetNodeIdByName(const FName& NodeName) const;

    /**
     * Gets dependencies for a node
     * @param NodeId The ID of the node
     * @return Array of dependency edges
     */
    TArray<FDependencyEdge> GetDependencies(uint32 NodeId) const;

    /**
     * Gets a node name by ID
     * @param NodeId The ID of the node
     * @return The node name, or NAME_None if not found
     */
    FName GetNodeNameById(uint32 NodeId) const;

private:
    /** Map of nodes by ID */
    TMap<uint32, FDependencyNode> Nodes;
    
    /** Map of node names to IDs for fast lookup */
    TMap<FName, uint32> NodeNameToIdMap;
    
    /** List of registered registries */
    TArray<IRegistry*> Registries;
    
    /** Last resolution status */
    EResolutionStatus LastResolutionStatus;
    
    /** Available hardware capabilities */
    uint32 HardwareCapabilities;
    
    /** Cached information about detected cycles */
    TArray<FCycleInfo> DetectedCycles;
    
    /** Cached list of missing dependencies */
    TArray<FString> MissingDependencies;
    
    /** Whether hardware capabilities have been detected */
    bool bHardwareCapabilitiesDetected;

    /**
     * Helper method for cycle detection using depth-first search
     * @param NodeId Current node being visited
     * @param VisitStatus Map of node visit status
     * @param Path Current path being explored
     * @param OutCycle Array to receive cycle nodes if found
     * @return True if a cycle was detected
     */
    bool DetectCyclesDFS(
        uint32 NodeId, 
        TMap<uint32, ENodeVisitStatus>& VisitStatus, 
        TArray<uint32>& Path,
        TArray<uint32>& OutCycle) const;

    /**
     * Helper method for topological sorting using Kahn's algorithm
     * @param OutOrder Array to receive the sorted node IDs
     * @param OutErrors Array to receive error descriptions
     * @return True if sorting was successful
     */
    bool TopologicalSort(TArray<uint32>& OutOrder, TArray<FString>& OutErrors);

    /**
     * Extracts type dependencies from a registry
     * @param Registry Registry to extract from
     * @param OutErrors Array to receive any errors
     * @return True if extraction was successful
     */
    bool ExtractRegistryDependencies(IRegistry* Registry, TArray<FString>& OutErrors);

    /**
     * Formats a cycle for error reporting
     * @param Cycle Array of node IDs in the cycle
     * @return Formatted description of the cycle
     */
    FString FormatCycleDescription(const TArray<uint32>& Cycle) const;

    /**
     * Updates node dependency counts
     * @param DependencyCounts Map of node IDs to dependency counts
     * @return True if the counts were updated successfully
     */
    bool UpdateDependencyCounts(TMap<uint32, int32>& DependencyCounts);

    /**
     * Evaluates if a dependency is active based on conditions and hardware capabilities
     * @param Edge The dependency edge to evaluate
     * @return True if the dependency is active
     */
    bool IsDependencyActive(const FDependencyEdge& Edge) const;
    
    /** Internal counter for generating unique IDs */
    uint32 NextNodeId;
};

/**
 * Inline functions
 */

inline uint32 FDependencyResolver::GetNodeCount() const
{
    return Nodes.Num();
}
