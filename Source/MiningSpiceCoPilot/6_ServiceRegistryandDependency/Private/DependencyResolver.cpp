// Copyright Epic Games, Inc. All Rights Reserved.

#include "DependencyResolver.h"
#include "Misc/ScopeLock.h"
#include "Misc/StringBuilder.h"
#include "Algo/Reverse.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProperties.h"
#include "RHI.h"
#include "Interfaces/IRegistry.h"
#include "MiningSpiceCoPilot/3_ThreadingTaskSystem/Public/PlatformMiscExtensions.h"
#include "MiningSpiceCoPilot/1_CoreRegistry/Public/MaterialRegistry.h"
#include "MiningSpiceCoPilot/1_CoreRegistry/Public/SDFTypeRegistry.h"
#include "MiningSpiceCoPilot/1_CoreRegistry/Public/SVOTypeRegistry.h"
#include "MiningSpiceCoPilot/1_CoreRegistry/Public/ZoneTypeRegistry.h"

FDependencyResolver::FDependencyResolver()
    : LastResolutionStatus(EResolutionStatus::NotAttempted)
    , HardwareCapabilities(0)
    , bHardwareCapabilitiesDetected(false)
    , NextNodeId(1) // Start from 1, reserve 0 for invalid
{
}

FDependencyResolver::~FDependencyResolver()
{
    Clear();
}

bool FDependencyResolver::RegisterDependency(uint32 SourceId, uint32 TargetId, EDependencyType Type)
{
    // Check if nodes exist
    if (!Nodes.Contains(SourceId) || !Nodes.Contains(TargetId))
    {
        return false;
    }
    
    // Check if this would create a cycle
    if (WouldCreateCycle(SourceId, TargetId))
    {
        return false;
    }
    
    // Create the dependency edge
    FDependencyEdge Edge;
    Edge.SourceId = SourceId;
    Edge.TargetId = TargetId;
    Edge.Type = Type;
    Edge.bIsActive = true;
    
    // Add the dependency to the source node
    FDependencyNode& SourceNode = Nodes[SourceId];
    SourceNode.Dependencies.Add(Edge);
    
    // Update the dependent IDs of the target node
    FDependencyNode& TargetNode = Nodes[TargetId];
    TargetNode.DependentIds.AddUnique(SourceId);
    
    return true;
}

bool FDependencyResolver::RegisterConditionalDependency(uint32 SourceId, uint32 TargetId, TFunction<bool()> Condition, EDependencyType Type)
{
    // Check if nodes exist
    if (!Nodes.Contains(SourceId) || !Nodes.Contains(TargetId))
    {
        return false;
    }
    
    // Check if this would create a cycle
    if (WouldCreateCycle(SourceId, TargetId))
    {
        return false;
    }
    
    // Create the dependency edge
    FDependencyEdge Edge;
    Edge.SourceId = SourceId;
    Edge.TargetId = TargetId;
    Edge.Type = Type;
    Edge.Condition = Condition;
    Edge.bIsActive = Condition ? Condition() : false;
    
    // Add the dependency to the source node
    FDependencyNode& SourceNode = Nodes[SourceId];
    SourceNode.Dependencies.Add(Edge);
    
    // Update the dependent IDs of the target node
    FDependencyNode& TargetNode = Nodes[TargetId];
    TargetNode.DependentIds.AddUnique(SourceId);
    
    return true;
}

bool FDependencyResolver::RegisterHardwareDependency(uint32 SourceId, uint32 TargetId, uint32 RequiredCapabilities, EDependencyType Type)
{
    // Check if nodes exist
    if (!Nodes.Contains(SourceId) || !Nodes.Contains(TargetId))
    {
        return false;
    }
    
    // Check if this would create a cycle
    if (WouldCreateCycle(SourceId, TargetId))
    {
        return false;
    }
    
    // Create the dependency edge
    FDependencyEdge Edge;
    Edge.SourceId = SourceId;
    Edge.TargetId = TargetId;
    Edge.Type = Type;
    Edge.RequiredCapabilities = RequiredCapabilities;
    
    // Evaluate if hardware capabilities are met
    if (!bHardwareCapabilitiesDetected)
    {
        HardwareCapabilities = DetectHardwareCapabilities();
        bHardwareCapabilitiesDetected = true;
    }
    
    Edge.bIsActive = (HardwareCapabilities & RequiredCapabilities) == RequiredCapabilities;
    
    // Add the dependency to the source node
    FDependencyNode& SourceNode = Nodes[SourceId];
    SourceNode.Dependencies.Add(Edge);
    
    // Update the dependent IDs of the target node
    FDependencyNode& TargetNode = Nodes[TargetId];
    TargetNode.DependentIds.AddUnique(SourceId);
    
    return true;
}

bool FDependencyResolver::RegisterNode(uint32 Id, const FName& Name, IRegistry* Registry, uint32 TypeId)
{
    // Check if ID already exists
    if (Nodes.Contains(Id))
    {
        return false;
    }
    
    // Check if name already exists
    if (NodeNameToIdMap.Contains(Name))
    {
        return false;
    }
    
    // Create and register the node
    FDependencyNode Node;
    Node.Id = Id;
    Node.Name = Name;
    Node.Registry = Registry;
    Node.TypeId = TypeId;
    Node.VisitStatus = ENodeVisitStatus::NotVisited;
    
    Nodes.Add(Id, Node);
    NodeNameToIdMap.Add(Name, Id);
    
    return true;
}

bool FDependencyResolver::RegisterRegistry(IRegistry* Registry)
{
    if (!Registry)
    {
        return false;
    }
    
    // Register the registry if not already registered
    if (!Registries.Contains(Registry))
    {
        Registries.Add(Registry);
        return true;
    }
    
    return false;
}

bool FDependencyResolver::BuildDependencyGraph(TArray<FString>& OutErrors)
{
    // Extract dependencies from all registered registries
    for (IRegistry* Registry : Registries)
    {
        if (!ExtractRegistryDependencies(Registry, OutErrors))
        {
            OutErrors.Add(FString::Printf(TEXT("Failed to extract dependencies from registry: %s"), 
                *Registry->GetRegistryName().ToString()));
            return false;
        }
    }
    
    // Evaluate conditional dependencies
    EvaluateConditionalDependencies();
    
    return true;
}

FDependencyResolver::EResolutionStatus FDependencyResolver::DetermineInitializationOrder(TArray<uint32>& OutOrder, TArray<FString>& OutErrors)
{
    OutOrder.Empty();
    
    // Detect cycles first
    TArray<FCycleInfo> Cycles;
    if (!DetectCycles(Cycles, OutErrors))
    {
        // Cycles detected, can't continue
        LastResolutionStatus = EResolutionStatus::FailedWithCycles;
        return LastResolutionStatus;
    }
    
    // Validate that all required dependencies exist
    if (!ValidateDependencies(OutErrors))
    {
        // Missing dependencies, can't continue
        LastResolutionStatus = EResolutionStatus::FailedWithMissingDependencies;
        return LastResolutionStatus;
    }
    
    // Perform topological sort
    if (!TopologicalSort(OutOrder, OutErrors))
    {
        // Sorting failed, likely due to cycles
        LastResolutionStatus = EResolutionStatus::FailedWithCycles;
        return LastResolutionStatus;
    }
    
    // Success
    LastResolutionStatus = EResolutionStatus::Success;
    return LastResolutionStatus;
}

bool FDependencyResolver::DetectCycles(TArray<FCycleInfo>& OutCycles, TArray<FString>& OutErrors)
{
    OutCycles.Empty();
    DetectedCycles.Empty();
    
    // Create a visit status map for DFS
    TMap<uint32, ENodeVisitStatus> VisitStatus;
    for (const auto& NodePair : Nodes)
    {
        VisitStatus.Add(NodePair.Key, ENodeVisitStatus::NotVisited);
    }
    
    // For each unvisited node, perform DFS to detect cycles
    for (const auto& NodePair : Nodes)
    {
        if (VisitStatus[NodePair.Key] == ENodeVisitStatus::NotVisited)
        {
            TArray<uint32> Path;
            TArray<uint32> CycleNodes;
            
            if (DetectCyclesDFS(NodePair.Key, VisitStatus, Path, CycleNodes))
            {
                // Cycle detected
                FCycleInfo CycleInfo;
                CycleInfo.CycleNodes = CycleNodes;
                
                // Collect node names for the cycle
                for (uint32 NodeId : CycleNodes)
                {
                    CycleInfo.CycleNodeNames.Add(Nodes[NodeId].Name);
                }
                
                // Format description
                CycleInfo.Description = FormatCycleDescription(CycleNodes);
                
                OutCycles.Add(CycleInfo);
                DetectedCycles.Add(CycleInfo);
                
                // Add error message
                OutErrors.Add(CycleInfo.Description);
            }
        }
    }
    
    return OutCycles.Num() == 0;
}

bool FDependencyResolver::ValidateDependencies(TArray<FString>& OutErrors)
{
    bool bValid = true;
    MissingDependencies.Empty();
    
    // Check if all nodes exist and have valid dependencies
    for (const auto& NodePair : Nodes)
    {
        const FDependencyNode& Node = NodePair.Value;
        
        // Check each dependency
        for (const FDependencyEdge& Edge : Node.Dependencies)
        {
            // Skip inactive dependencies
            if (!Edge.bIsActive)
            {
                continue;
            }
            
            // For required dependencies, make sure the target exists
            if (Edge.Type == EDependencyType::Required && !Nodes.Contains(Edge.TargetId))
            {
                FString ErrorMsg = FString::Printf(
                    TEXT("Node '%s' (ID: %u) has a required dependency on node ID %u, but that node doesn't exist."),
                    *Node.Name.ToString(), Node.Id, Edge.TargetId);
                
                OutErrors.Add(ErrorMsg);
                MissingDependencies.Add(ErrorMsg);
                bValid = false;
            }
            
            // For optional dependencies, just log a warning if the target doesn't exist
            if (Edge.Type == EDependencyType::Optional && !Nodes.Contains(Edge.TargetId))
            {
                FString WarningMsg = FString::Printf(
                    TEXT("Warning: Node '%s' (ID: %u) has an optional dependency on node ID %u, but that node doesn't exist."),
                    *Node.Name.ToString(), Node.Id, Edge.TargetId);
                
                // Just log warning, don't fail validation
                UE_LOG(LogTemp, Warning, TEXT("%s"), *WarningMsg);
            }
        }
    }
    
    return bValid;
}

bool FDependencyResolver::ValidateNodeDependencies(uint32 NodeId, TArray<FString>& OutErrors)
{
    if (!Nodes.Contains(NodeId))
    {
        OutErrors.Add(FString::Printf(TEXT("Node ID %u doesn't exist."), NodeId));
        return false;
    }
    
    bool bValid = true;
    const FDependencyNode& Node = Nodes[NodeId];
    
    // Check each dependency
    for (const FDependencyEdge& Edge : Node.Dependencies)
    {
        // Skip inactive dependencies
        if (!Edge.bIsActive)
        {
            continue;
        }
        
        // For required dependencies, make sure the target exists
        if (Edge.Type == EDependencyType::Required && !Nodes.Contains(Edge.TargetId))
        {
            FString ErrorMsg = FString::Printf(
                TEXT("Node '%s' (ID: %u) has a required dependency on node ID %u, but that node doesn't exist."),
                *Node.Name.ToString(), Node.Id, Edge.TargetId);
            
            OutErrors.Add(ErrorMsg);
            bValid = false;
        }
        
        // For optional dependencies, just log a warning if the target doesn't exist
        if (Edge.Type == EDependencyType::Optional && !Nodes.Contains(Edge.TargetId))
        {
            FString WarningMsg = FString::Printf(
                TEXT("Warning: Node '%s' (ID: %u) has an optional dependency on node ID %u, but that node doesn't exist."),
                *Node.Name.ToString(), Node.Id, Edge.TargetId);
            
            // Just log warning, don't fail validation
            UE_LOG(LogTemp, Warning, TEXT("%s"), *WarningMsg);
        }
    }
    
    return bValid;
}

void FDependencyResolver::SetHardwareCapabilities(uint32 Capabilities)
{
    HardwareCapabilities = Capabilities;
    bHardwareCapabilitiesDetected = true;
    
    // Re-evaluate conditional dependencies
    EvaluateConditionalDependencies();
}

uint32 FDependencyResolver::DetectHardwareCapabilities()
{
    uint32 Capabilities = 0;
    
    // Use PlatformMiscExtensions for hardware detection instead
    
    // Check for SIMD support
    if (FPlatformMiscExtensions::SupportsSSE2())
    {
        Capabilities |= static_cast<uint32>(EHardwareCapability::SSE2);
    }
    
    if (FPlatformMiscExtensions::SupportsAVX())
    {
        Capabilities |= static_cast<uint32>(EHardwareCapability::AVX);
    }
    
    if (FPlatformMiscExtensions::SupportsAVX2())
    {
        Capabilities |= static_cast<uint32>(EHardwareCapability::AVX2);
    }
    
    // Multicore support
    if (FPlatformMisc::NumberOfCores() > 1)
    {
        Capabilities |= static_cast<uint32>(EHardwareCapability::MultiCore);
    }
    
    // GPU support - check if RHI is initialized
    if (IsRunningRHIInSeparateThread())
    {
        Capabilities |= static_cast<uint32>(EHardwareCapability::GPU);
    }
    
    // Large memory support (16+ GB)
    if (FPlatformMemory::GetPhysicalGBRam() >= 16)
    {
        Capabilities |= static_cast<uint32>(EHardwareCapability::LargeMemory);
    }
    
    return Capabilities;
}

void FDependencyResolver::EvaluateConditionalDependencies()
{
    for (auto& NodePair : Nodes)
    {
        FDependencyNode& Node = NodePair.Value;
        
        for (FDependencyEdge& Edge : Node.Dependencies)
        {
            Edge.bIsActive = IsDependencyActive(Edge);
        }
    }
}

const FDependencyResolver::FDependencyNode* FDependencyResolver::GetNode(uint32 Id) const
{
    return Nodes.Find(Id);
}

const FDependencyResolver::FDependencyNode* FDependencyResolver::GetNodeByName(const FName& Name) const
{
    const uint32* IdPtr = NodeNameToIdMap.Find(Name);
    if (IdPtr)
    {
        return Nodes.Find(*IdPtr);
    }
    
    return nullptr;
}

void FDependencyResolver::Clear()
{
    Nodes.Empty();
    NodeNameToIdMap.Empty();
    Registries.Empty();
    DetectedCycles.Empty();
    MissingDependencies.Empty();
    LastResolutionStatus = EResolutionStatus::NotAttempted;
    NextNodeId = 1;
}

TArray<FDependencyResolver::FDependencyNode> FDependencyResolver::GetAllNodes() const
{
    TArray<FDependencyNode> Result;
    
    for (const auto& NodePair : Nodes)
    {
        Result.Add(NodePair.Value);
    }
    
    return Result;
}

TArray<FDependencyResolver::FCycleInfo> FDependencyResolver::GetCycleInformation() const
{
    return DetectedCycles;
}

bool FDependencyResolver::GenerateGraphVisualization(FString& OutVisualizationData) const
{
    TStringBuilder<4096> Builder;
    
    // Generate DOT language for graph visualization
    Builder.Append(TEXT("digraph DependencyGraph {\n"));
    Builder.Append(TEXT("  node [shape=box style=filled];\n"));
    
    // Add all nodes
    for (const auto& NodePair : Nodes)
    {
        const FDependencyNode& Node = NodePair.Value;
        Builder.Appendf(TEXT("  \"%s\" [label=\"%s (%u)\"];\n"), *Node.Name.ToString(), *Node.Name.ToString(), Node.Id);
    }
    
    // Add all edges
    for (const auto& NodePair : Nodes)
    {
        const FDependencyNode& Node = NodePair.Value;
        
        for (const FDependencyEdge& Edge : Node.Dependencies)
        {
            if (!Nodes.Contains(Edge.TargetId))
            {
                continue;
            }
            
            const FDependencyNode& TargetNode = Nodes[Edge.TargetId];
            
            // Different edge style based on dependency type
            if (Edge.Type == EDependencyType::Required)
            {
                if (Edge.bIsActive)
                {
                    Builder.Appendf(TEXT("  \"%s\" -> \"%s\" [style=solid];\n"), 
                        *Node.Name.ToString(), *TargetNode.Name.ToString());
                }
                else
                {
                    Builder.Appendf(TEXT("  \"%s\" -> \"%s\" [style=solid color=gray];\n"), 
                        *Node.Name.ToString(), *TargetNode.Name.ToString());
                }
            }
            else // Optional
            {
                if (Edge.bIsActive)
                {
                    Builder.Appendf(TEXT("  \"%s\" -> \"%s\" [style=dashed];\n"), 
                        *Node.Name.ToString(), *TargetNode.Name.ToString());
                }
                else
                {
                    Builder.Appendf(TEXT("  \"%s\" -> \"%s\" [style=dashed color=gray];\n"), 
                        *Node.Name.ToString(), *TargetNode.Name.ToString());
                }
            }
        }
    }
    
    Builder.Append(TEXT("}\n"));
    
    OutVisualizationData = Builder.ToString();
    return true;
}

TArray<FString> FDependencyResolver::GetMissingDependencies() const
{
    return MissingDependencies;
}

bool FDependencyResolver::WouldCreateCycle(uint32 SourceId, uint32 TargetId) const
{
    if (SourceId == TargetId)
    {
        // Self-dependency is a cycle
        return true;
    }
    
    // Check if target depends on source (directly or indirectly)
    TArray<uint32> VisitedNodes;
    TArray<uint32> NodesToVisit;
    
    NodesToVisit.Add(TargetId);
    
    while (NodesToVisit.Num() > 0)
    {
        uint32 CurrentId = NodesToVisit[0];
        NodesToVisit.RemoveAt(0);
        VisitedNodes.Add(CurrentId);
        
        const FDependencyNode* CurrentNode = Nodes.Find(CurrentId);
        if (!CurrentNode)
        {
            continue;
        }
        
        // Check each dependent
        for (uint32 DependentId : CurrentNode->DependentIds)
        {
            if (DependentId == SourceId)
            {
                // Found a path back to source, which means adding this edge would create a cycle
                return true;
            }
            
            if (!VisitedNodes.Contains(DependentId) && !NodesToVisit.Contains(DependentId))
            {
                NodesToVisit.Add(DependentId);
            }
        }
    }
    
    return false;
}

TArray<FDependencyResolver::FDependencyEdge> FDependencyResolver::GetNodeDependencies(uint32 NodeId) const
{
    if (const FDependencyNode* Node = Nodes.Find(NodeId))
    {
        return Node->Dependencies;
    }
    
    return TArray<FDependencyEdge>();
}

TArray<uint32> FDependencyResolver::GetNodeDependents(uint32 NodeId) const
{
    if (const FDependencyNode* Node = Nodes.Find(NodeId))
    {
        return Node->DependentIds;
    }
    
    return TArray<uint32>();
}

bool FDependencyResolver::DetectCyclesDFS(
    uint32 NodeId, 
    TMap<uint32, ENodeVisitStatus>& VisitStatus, 
    TArray<uint32>& Path,
    TArray<uint32>& OutCycle) const
{
    // Mark node as in-progress
    VisitStatus[NodeId] = ENodeVisitStatus::InProgress;
    Path.Add(NodeId);
    
    const FDependencyNode& Node = Nodes[NodeId];
    
    // Visit all dependencies
    for (const FDependencyEdge& Edge : Node.Dependencies)
    {
        // Skip inactive dependencies
        if (!Edge.bIsActive)
        {
            continue;
        }
        
        uint32 TargetId = Edge.TargetId;
        
        // If target doesn't exist, skip
        if (!Nodes.Contains(TargetId))
        {
            continue;
        }
        
        // Check visit status of target
        ENodeVisitStatus TargetStatus = VisitStatus[TargetId];
        
        if (TargetStatus == ENodeVisitStatus::InProgress)
        {
            // Cycle detected
            // Find where the cycle starts in the path
            int32 CycleStart = Path.Find(TargetId);
            
            if (CycleStart != INDEX_NONE)
            {
                // Extract the cycle nodes
                for (int32 i = CycleStart; i < Path.Num(); ++i)
                {
                    OutCycle.Add(Path[i]);
                }
                OutCycle.Add(TargetId); // Complete the cycle
                return true;
            }
        }
        else if (TargetStatus == ENodeVisitStatus::NotVisited)
        {
            // Continue DFS
            if (DetectCyclesDFS(TargetId, VisitStatus, Path, OutCycle))
            {
                return true;
            }
        }
    }
    
    // Mark node as visited
    VisitStatus[NodeId] = ENodeVisitStatus::Visited;
    Path.RemoveAt(Path.Num() - 1);
    
    return false;
}

bool FDependencyResolver::TopologicalSort(TArray<uint32>& OutOrder, TArray<FString>& OutErrors)
{
    OutOrder.Empty();
    
    // Count incoming edges for each node
    TMap<uint32, int32> IncomingEdgeCounts;
    for (const auto& NodePair : Nodes)
    {
        IncomingEdgeCounts.Add(NodePair.Key, 0);
    }
    
    // Count dependencies
    if (!UpdateDependencyCounts(IncomingEdgeCounts))
    {
        OutErrors.Add(TEXT("Failed to update dependency counts."));
        return false;
    }
    
    // Find nodes with no dependencies
    TArray<uint32> NoDependencyNodes;
    for (const auto& CountPair : IncomingEdgeCounts)
    {
        if (CountPair.Value == 0)
        {
            NoDependencyNodes.Add(CountPair.Key);
        }
    }
    
    // Process nodes
    while (NoDependencyNodes.Num() > 0)
    {
        uint32 NodeId = NoDependencyNodes[0];
        NoDependencyNodes.RemoveAt(0);
        
        OutOrder.Add(NodeId);
        
        // For each node that depends on this one
        const FDependencyNode& Node = Nodes[NodeId];
        for (uint32 DependentId : Node.DependentIds)
        {
            // Reduce the count of incoming edges
            IncomingEdgeCounts[DependentId]--;
            
            // If all dependencies are satisfied, add to the queue
            if (IncomingEdgeCounts[DependentId] == 0)
            {
                NoDependencyNodes.Add(DependentId);
            }
        }
    }
    
    // Check if all nodes are in the result
    if (OutOrder.Num() != Nodes.Num())
    {
        OutErrors.Add(TEXT("Not all nodes could be included in the topological sort, likely due to cycles."));
        return false;
    }
    
    return true;
}

bool FDependencyResolver::ExtractRegistryDependencies(IRegistry* Registry, TArray<FString>& OutErrors)
{
    if (!Registry)
    {
        OutErrors.Add(TEXT("Null registry passed to ExtractRegistryDependencies."));
        return false;
    }
    
    // Register the registry itself as a node if not already registered
    FName RegistryName = Registry->GetRegistryName();
    uint32 RegistryNodeId = 0;
    
    if (NodeNameToIdMap.Contains(RegistryName))
    {
        RegistryNodeId = NodeNameToIdMap[RegistryName];
    }
    else
    {
        RegistryNodeId = NextNodeId++;
        RegisterNode(RegistryNodeId, RegistryName, Registry, 0);
    }
    
    // Handle each registry type differently
    if (FMaterialRegistry* MaterialRegistry = dynamic_cast<FMaterialRegistry*>(Registry))
    {
        // Extract material types from MaterialRegistry
        TArray<FMaterialTypeInfo> MaterialTypes = MaterialRegistry->GetAllMaterialTypes();
        
        for (const FMaterialTypeInfo& TypeInfo : MaterialTypes)
        {
            uint32 TypeId = TypeInfo.TypeId;
            FName TypeName = TypeInfo.TypeName;
            
            // Create a qualified name to avoid conflicts
            FName QualifiedName = FName(*FString::Printf(TEXT("%s:%s"), *RegistryName.ToString(), *TypeName.ToString()));
            
            // Register the type as a node
            uint32 TypeNodeId = 0;
            if (NodeNameToIdMap.Contains(QualifiedName))
            {
                TypeNodeId = NodeNameToIdMap[QualifiedName];
            }
            else
            {
                TypeNodeId = NextNodeId++;
                RegisterNode(TypeNodeId, QualifiedName, Registry, TypeId);
            }
            
            // Register dependency of type on registry
            RegisterDependency(TypeNodeId, RegistryNodeId, EDependencyType::Required);
            
            // Register dependencies between types
            if (TypeInfo.ParentTypeId != 0)
            {
                const FMaterialTypeInfo* ParentTypeInfo = MaterialRegistry->GetMaterialTypeInfo(TypeInfo.ParentTypeId);
                if (ParentTypeInfo)
                {
                    FName ParentTypeName = ParentTypeInfo->TypeName;
                    FName QualifiedParentName = FName(*FString::Printf(TEXT("%s:%s"), 
                        *RegistryName.ToString(), *ParentTypeName.ToString()));
                    
                    // Register the parent type if not already registered
                    uint32 ParentNodeId = 0;
                    if (NodeNameToIdMap.Contains(QualifiedParentName))
                    {
                        ParentNodeId = NodeNameToIdMap[QualifiedParentName];
                    }
                    else
                    {
                        ParentNodeId = NextNodeId++;
                        RegisterNode(ParentNodeId, QualifiedParentName, Registry, TypeInfo.ParentTypeId);
                    }
                    
                    // Register dependency
                    RegisterDependency(TypeNodeId, ParentNodeId, EDependencyType::Required);
                }
            }
        }
    }
    else if (FSDFTypeRegistry* SDFRegistry = dynamic_cast<FSDFTypeRegistry*>(Registry))
    {
        // Extract field types from SDFTypeRegistry
        TArray<FSDFFieldTypeInfo> FieldTypes = SDFRegistry->GetAllFieldTypes();
        
        for (const FSDFFieldTypeInfo& TypeInfo : FieldTypes)
        {
            uint32 TypeId = TypeInfo.TypeId;
            FName TypeName = TypeInfo.TypeName;
            
            // Create a qualified name to avoid conflicts
            FName QualifiedName = FName(*FString::Printf(TEXT("%s:%s"), *RegistryName.ToString(), *TypeName.ToString()));
            
            // Register the type as a node
            uint32 TypeNodeId = 0;
            if (NodeNameToIdMap.Contains(QualifiedName))
            {
                TypeNodeId = NodeNameToIdMap[QualifiedName];
            }
            else
            {
                TypeNodeId = NextNodeId++;
                RegisterNode(TypeNodeId, QualifiedName, Registry, TypeId);
            }
            
            // Register dependency of type on registry
            RegisterDependency(TypeNodeId, RegistryNodeId, EDependencyType::Required);
        }
    }
    else if (FSVOTypeRegistry* SVORegistry = dynamic_cast<FSVOTypeRegistry*>(Registry))
    {
        // Extract node types from SVOTypeRegistry
        TArray<FSVONodeTypeInfo> NodeTypes = SVORegistry->GetAllNodeTypes();
        
        for (const FSVONodeTypeInfo& TypeInfo : NodeTypes)
        {
            uint32 TypeId = TypeInfo.TypeId;
            FName TypeName = TypeInfo.TypeName;
            
            // Create a qualified name to avoid conflicts
            FName QualifiedName = FName(*FString::Printf(TEXT("%s:%s"), *RegistryName.ToString(), *TypeName.ToString()));
            
            // Register the type as a node
            uint32 TypeNodeId = 0;
            if (NodeNameToIdMap.Contains(QualifiedName))
            {
                TypeNodeId = NodeNameToIdMap[QualifiedName];
            }
            else
            {
                TypeNodeId = NextNodeId++;
                RegisterNode(TypeNodeId, QualifiedName, Registry, TypeId);
            }
            
            // Register dependency of type on registry
            RegisterDependency(TypeNodeId, RegistryNodeId, EDependencyType::Required);
        }
    }
    else if (FZoneTypeRegistry* ZoneRegistry = dynamic_cast<FZoneTypeRegistry*>(Registry))
    {
        // Extract zone types from ZoneTypeRegistry
        TArray<FZoneTypeInfo> ZoneTypes = ZoneRegistry->GetAllZoneTypes();
        
        for (const FZoneTypeInfo& TypeInfo : ZoneTypes)
        {
            uint32 TypeId = TypeInfo.TypeId;
            FName TypeName = TypeInfo.TypeName;
            
            // Create a qualified name to avoid conflicts
            FName QualifiedName = FName(*FString::Printf(TEXT("%s:%s"), *RegistryName.ToString(), *TypeName.ToString()));
            
            // Register the type as a node
            uint32 TypeNodeId = 0;
            if (NodeNameToIdMap.Contains(QualifiedName))
            {
                TypeNodeId = NodeNameToIdMap[QualifiedName];
            }
            else
            {
                TypeNodeId = NextNodeId++;
                RegisterNode(TypeNodeId, QualifiedName, Registry, TypeId);
            }
            
            // Register dependency of type on registry
            RegisterDependency(TypeNodeId, RegistryNodeId, EDependencyType::Required);
            
            // Register parent dependency if applicable
            if (TypeInfo.ParentZoneTypeId != 0)
            {
                // Create a qualified name for the parent
                const FZoneTypeInfo* ParentInfo = nullptr;
                for (const FZoneTypeInfo& OtherInfo : ZoneTypes)
                {
                    if (OtherInfo.TypeId == TypeInfo.ParentZoneTypeId)
                    {
                        ParentInfo = &OtherInfo;
                        break;
                    }
                }
                
                if (ParentInfo)
                {
                    FName ParentTypeName = ParentInfo->TypeName;
                    FName QualifiedParentName = FName(*FString::Printf(TEXT("%s:%s"), 
                        *RegistryName.ToString(), *ParentTypeName.ToString()));
                    
                    // Register the parent type if not already registered
                    uint32 ParentNodeId = 0;
                    if (NodeNameToIdMap.Contains(QualifiedParentName))
                    {
                        ParentNodeId = NodeNameToIdMap[QualifiedParentName];
                    }
                    else
                    {
                        ParentNodeId = NextNodeId++;
                        RegisterNode(ParentNodeId, QualifiedParentName, Registry, TypeInfo.ParentZoneTypeId);
                    }
                    
                    // Register dependency
                    RegisterDependency(TypeNodeId, ParentNodeId, EDependencyType::Required);
                }
            }
        }
    }
    else
    {
        // Unknown registry type, log a warning but don't fail
        OutErrors.Add(FString::Printf(TEXT("Unknown registry type: %s. Cannot extract type dependencies."), 
            *RegistryName.ToString()));
    }
    
    return true;
}

FString FDependencyResolver::FormatCycleDescription(const TArray<uint32>& Cycle) const
{
    if (Cycle.Num() == 0)
    {
        return TEXT("Empty cycle detected.");
    }
    
    TStringBuilder<1024> Builder;
    Builder.Append(TEXT("Dependency cycle detected: "));
    
    for (int32 i = 0; i < Cycle.Num(); ++i)
    {
        uint32 NodeId = Cycle[i];
        
        if (!Nodes.Contains(NodeId))
        {
            Builder.Appendf(TEXT("Unknown(%u)"), NodeId);
        }
        else
        {
            const FDependencyNode& Node = Nodes[NodeId];
            Builder.Appendf(TEXT("%s(%u)"), *Node.Name.ToString(), NodeId);
        }
        
        if (i < Cycle.Num() - 1)
        {
            Builder.Append(TEXT(" -> "));
        }
    }
    
    return Builder.ToString();
}

bool FDependencyResolver::UpdateDependencyCounts(TMap<uint32, int32>& DependencyCounts)
{
    // Initialize all counts to zero
    for (const auto& NodePair : Nodes)
    {
        DependencyCounts.Add(NodePair.Key, 0);
    }
    
    // Count active dependencies for each node
    for (const auto& NodePair : Nodes)
    {
        const FDependencyNode& Node = NodePair.Value;
        
        for (const FDependencyEdge& Edge : Node.Dependencies)
        {
            // Only count active dependencies
            if (Edge.bIsActive && Nodes.Contains(Edge.TargetId))
            {
                // This is a dependency, so increment the count for the dependent
                DependencyCounts[NodePair.Key]++;
            }
        }
    }
    
    return true;
}

bool FDependencyResolver::IsDependencyActive(const FDependencyEdge& Edge) const
{
    // Check if hardware capabilities are satisfied
    if (Edge.RequiredCapabilities != 0)
    {
        if ((HardwareCapabilities & Edge.RequiredCapabilities) != Edge.RequiredCapabilities)
        {
            return false;
        }
    }
    
    // Check if condition is satisfied
    if (Edge.Condition)
    {
        return Edge.Condition();
    }
    
    return true;
}
