// Copyright Epic Games, Inc. All Rights Reserved.

#include "TaskDependencyVisualizer.h"
#include "TaskScheduler.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"

// Initialize static instance to nullptr
FTaskDependencyVisualizer* FTaskDependencyVisualizer::Instance = nullptr;

FTaskDependencyVisualizer::FTaskDependencyVisualizer()
{
    // Set the singleton instance
    check(Instance == nullptr);
    Instance = this;
}

FTaskDependencyVisualizer::~FTaskDependencyVisualizer()
{
    // Clear the singleton instance if it's us
    if (Instance == this)
    {
        Instance = nullptr;
    }
}

FTaskDependencyVisualizer& FTaskDependencyVisualizer::Get()
{
    // Create instance if needed
    if (Instance == nullptr)
    {
        Instance = new FTaskDependencyVisualizer();
    }
    
    return *Instance;
}

FString FTaskDependencyVisualizer::VisualizeTask(uint64 TaskId, const FVisualizationOptions& Options, EVisualizationFormat Format)
{
    TArray<uint64> TaskIds;
    TaskIds.Add(TaskId);
    
    return VisualizeTasks(TaskIds, Options, Format);
}

FString FTaskDependencyVisualizer::VisualizeTasks(const TArray<uint64>& TaskIds, const FVisualizationOptions& Options, EVisualizationFormat Format)
{
    // Build the dependency graph
    TArray<FTaskDependencyNode> Nodes;
    TArray<TPair<uint64, uint64>> Edges;
    
    BuildDependencyGraph(TaskIds, Options, Nodes, Edges);
    
    // Generate visualization in the requested format
    switch (Format)
    {
        case EVisualizationFormat::DOT:
            return GenerateDOTVisualization(Nodes, Edges, Options);
        
        case EVisualizationFormat::JSON:
            return GenerateJSONVisualization(Nodes, Edges, Options);
        
        case EVisualizationFormat::Text:
            return GenerateTextVisualization(Nodes, Edges, Options);
        
        default:
            return TEXT("Unsupported visualization format");
    }
}

FString FTaskDependencyVisualizer::VisualizeAllTasks(const FVisualizationOptions& Options, EVisualizationFormat Format)
{
    // Collect all tasks from the scheduler
    TArray<uint64> TaskIds = CollectAllTasks(Options);
    
    return VisualizeTasks(TaskIds, Options, Format);
}

bool FTaskDependencyVisualizer::SaveVisualization(const FString& Filename, const FString& Visualization, EVisualizationFormat Format)
{
    // Ensure the directory exists
    FString Directory = FPaths::GetPath(Filename);
    if (!Directory.IsEmpty())
    {
        IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
        if (!PlatformFile.DirectoryExists(*Directory))
        {
            PlatformFile.CreateDirectoryTree(*Directory);
        }
    }
    
    // Save the visualization to file
    bool bSuccess = FFileHelper::SaveStringToFile(Visualization, *Filename);
    
    return bSuccess;
}

TArray<uint64> FTaskDependencyVisualizer::CollectAllTasks(const FVisualizationOptions& Options)
{
    TArray<uint64> TaskIds;
    
    // Get task counts from the scheduler
    TMap<ETaskStatus, int32> TaskCounts = static_cast<FTaskScheduler&>(FTaskScheduler::Get()).GetTaskCounts();
    
    // Create a work set with all task IDs
    // This is implementation-specific and requires access to the internal task map
    // For this example, we'll create a new scheduler API to get all task IDs
    
    // Get all task IDs from the scheduler
    // This would require a new method in FTaskScheduler to expose all task IDs
    // For now, we'll use a placeholder implementation
    
    // Placeholder: Retrieve task IDs from scheduler
    const TMap<uint64, class FMiningTask*>& AllTasks = static_cast<FTaskScheduler&>(FTaskScheduler::Get()).GetAllTasks();
    
    for (const auto& Pair : AllTasks)
    {
        uint64 TaskId = Pair.Key;
        FMiningTask* Task = Pair.Value;
        
        // Apply filters based on options
        ETaskStatus Status = Task->GetStatus();
        
        if (Status == ETaskStatus::Completed && !Options.bIncludeCompletedTasks)
        {
            continue;
        }
        
        if (Status == ETaskStatus::Cancelled && !Options.bIncludeCancelledTasks)
        {
            continue;
        }
        
        if (Status == ETaskStatus::Failed && !Options.bIncludeFailedTasks)
        {
            continue;
        }
        
        TaskIds.Add(TaskId);
    }
    
    return TaskIds;
}

void FTaskDependencyVisualizer::BuildDependencyGraph(const TArray<uint64>& TaskIds, const FVisualizationOptions& Options,
    TArray<FTaskDependencyNode>& OutNodes, TArray<TPair<uint64, uint64>>& OutEdges)
{
    // Clear output arrays
    OutNodes.Empty();
    OutEdges.Empty();
    
    // Track visited tasks to avoid cycles
    TSet<uint64> VisitedTasks;
    
    // Process the initial tasks
    TArray<uint64> WorkList = TaskIds;
    
    // Track depth for each task if max depth is set
    TMap<uint64, int32> TaskDepth;
    for (uint64 TaskId : TaskIds)
    {
        TaskDepth.Add(TaskId, 0);
    }
    
    while (WorkList.Num() > 0)
    {
        uint64 CurrentTaskId = WorkList[0];
        WorkList.RemoveAt(0);
        
        // Skip if already processed
        if (VisitedTasks.Contains(CurrentTaskId))
        {
            continue;
        }
        
        VisitedTasks.Add(CurrentTaskId);
        
        // Get the task
        FMiningTask* Task = static_cast<FTaskScheduler&>(FTaskScheduler::Get()).GetTaskById(CurrentTaskId);
        
        if (!Task)
        {
            continue; // Task no longer exists
        }
        
        // Apply filters based on options
        ETaskStatus Status = Task->GetStatus();
        
        if (Status == ETaskStatus::Completed && !Options.bIncludeCompletedTasks)
        {
            continue;
        }
        
        if (Status == ETaskStatus::Cancelled && !Options.bIncludeCancelledTasks)
        {
            continue;
        }
        
        if (Status == ETaskStatus::Failed && !Options.bIncludeFailedTasks)
        {
            continue;
        }
        
        // Create a node for this task
        FTaskDependencyNode Node;
        Node.TaskId = CurrentTaskId;
        Node.Description = Task->Description;
        Node.Status = Status;
        Node.Priority = Task->Config.Priority;
        Node.Type = Task->Config.Type;
        Node.Progress = Task->GetProgress();
        Node.QueueTimeMs = Task->Stats.QueueTimeMs;
        Node.ExecutionTimeMs = Task->Stats.ExecutionTimeMs;
        
        // Add the node
        OutNodes.Add(Node);
        
        // Process dependencies
        int32 CurrentDepth = TaskDepth.FindChecked(CurrentTaskId);
        
        // Check max depth
        if (Options.MaxDepth > 0 && CurrentDepth >= Options.MaxDepth)
        {
            continue;
        }
        
        for (const FTaskDependency& Dependency : Task->Dependencies)
        {
            // Add edge
            OutEdges.Add(TPair<uint64, uint64>(Dependency.TaskId, CurrentTaskId));
            
            // Add dependency to work list if not already visited
            if (!VisitedTasks.Contains(Dependency.TaskId))
            {
                WorkList.Add(Dependency.TaskId);
                TaskDepth.Add(Dependency.TaskId, CurrentDepth + 1);
                
                // Add dependency type to the node
                ETaskDependencyType Type = Dependency.bRequired ? ETaskDependencyType::Required : ETaskDependencyType::Optional;
                Node.Dependencies.Add(TPair<uint64, ETaskDependencyType>(Dependency.TaskId, Type));
            }
        }
    }
}

FString FTaskDependencyVisualizer::GenerateDOTVisualization(const TArray<FTaskDependencyNode>& Nodes,
    const TArray<TPair<uint64, uint64>>& Edges, const FVisualizationOptions& Options)
{
    FString Result = TEXT("digraph TaskDependencies {\n");
    Result += TEXT("  rankdir=LR;\n");
    Result += TEXT("  node [shape=box, style=filled, fontname=\"Arial\"];\n");
    
    // Add nodes
    for (const FTaskDependencyNode& Node : Nodes)
    {
        // Build node label
        FString Label = FString::Printf(TEXT("%llu: %s"), Node.TaskId, *Node.Description);
        
        if (Options.bIncludeTaskStats)
        {
            // Add status
            Label += FString::Printf(TEXT("\\nStatus: %s"),
                *StaticEnum<ETaskStatus>()->GetNameStringByValue(static_cast<int64>(Node.Status)));
            
            // Add priority
            Label += FString::Printf(TEXT("\\nPriority: %s"),
                *StaticEnum<ETaskPriority>()->GetNameStringByValue(static_cast<int64>(Node.Priority)));
            
            // Add type
            Label += FString::Printf(TEXT("\\nType: %s"),
                *StaticEnum<ETaskType>()->GetNameStringByValue(static_cast<int64>(Node.Type)));
            
            // Add progress if task supports it
            if (Node.Progress > 0)
            {
                Label += FString::Printf(TEXT("\\nProgress: %d%%"), Node.Progress);
            }
            
            // Add timing information
            Label += FString::Printf(TEXT("\\nQueue: %.2f ms"), Node.QueueTimeMs);
            
            if (Node.ExecutionTimeMs > 0)
            {
                Label += FString::Printf(TEXT("\\nExecution: %.2f ms"), Node.ExecutionTimeMs);
            }
        }
        
        // Get node style based on status and priority
        FString Color = GetStatusColor(Node.Status);
        FString Shape = GetTypeShape(Node.Type);
        
        // Add node
        Result += FString::Printf(TEXT("  \"%llu\" [label=\"%s\", shape=%s, fillcolor=\"%s\"];\n"),
            Node.TaskId, *Label, *Shape, *Color);
    }
    
    // Add edges
    for (const TPair<uint64, uint64>& Edge : Edges)
    {
        // Find the dependency type
        ETaskDependencyType DependencyType = ETaskDependencyType::Required;
        
        for (const FTaskDependencyNode& Node : Nodes)
        {
            if (Node.TaskId == Edge.Value)
            {
                for (const TPair<uint64, ETaskDependencyType>& Dependency : Node.Dependencies)
                {
                    if (Dependency.Key == Edge.Key)
                    {
                        DependencyType = Dependency.Value;
                        break;
                    }
                }
                break;
            }
        }
        
        // Set edge style based on dependency type
        FString Style;
        FString Color;
        
        switch (DependencyType)
        {
            case ETaskDependencyType::Required:
                Style = TEXT("solid");
                Color = TEXT("black");
                break;
            
            case ETaskDependencyType::Optional:
                Style = TEXT("dashed");
                Color = TEXT("gray");
                break;
            
            case ETaskDependencyType::Parallel:
                Style = TEXT("dotted");
                Color = TEXT("blue");
                break;
            
            case ETaskDependencyType::Sequential:
                Style = TEXT("solid");
                Color = TEXT("green");
                break;
        }
        
        // Add edge
        Result += FString::Printf(TEXT("  \"%llu\" -> \"%llu\" [style=%s, color=%s];\n"),
            Edge.Key, Edge.Value, *Style, *Color);
    }
    
    Result += TEXT("}\n");
    
    return Result;
}

FString FTaskDependencyVisualizer::GenerateJSONVisualization(const TArray<FTaskDependencyNode>& Nodes,
    const TArray<TPair<uint64, uint64>>& Edges, const FVisualizationOptions& Options)
{
    FString Result = TEXT("{\n");
    
    // Add nodes
    Result += TEXT("  \"nodes\": [\n");
    
    for (int32 i = 0; i < Nodes.Num(); ++i)
    {
        const FTaskDependencyNode& Node = Nodes[i];
        
        Result += TEXT("    {\n");
        Result += FString::Printf(TEXT("      \"id\": %llu,\n"), Node.TaskId);
        Result += FString::Printf(TEXT("      \"description\": \"%s\",\n"), *Node.Description.Replace(TEXT("\""), TEXT("\\\"")));
        Result += FString::Printf(TEXT("      \"status\": \"%s\",\n"),
            *StaticEnum<ETaskStatus>()->GetNameStringByValue(static_cast<int64>(Node.Status)));
        Result += FString::Printf(TEXT("      \"priority\": \"%s\",\n"),
            *StaticEnum<ETaskPriority>()->GetNameStringByValue(static_cast<int64>(Node.Priority)));
        Result += FString::Printf(TEXT("      \"type\": \"%s\",\n"),
            *StaticEnum<ETaskType>()->GetNameStringByValue(static_cast<int64>(Node.Type)));
        Result += FString::Printf(TEXT("      \"progress\": %d,\n"), Node.Progress);
        Result += FString::Printf(TEXT("      \"queueTimeMs\": %.2f,\n"), Node.QueueTimeMs);
        Result += FString::Printf(TEXT("      \"executionTimeMs\": %.2f,\n"), Node.ExecutionTimeMs);
        
        // Add dependencies
        Result += TEXT("      \"dependencies\": [\n");
        
        for (int32 j = 0; j < Node.Dependencies.Num(); ++j)
        {
            const TPair<uint64, ETaskDependencyType>& Dependency = Node.Dependencies[j];
            
            Result += TEXT("        {\n");
            Result += FString::Printf(TEXT("          \"id\": %llu,\n"), Dependency.Key);
            Result += FString::Printf(TEXT("          \"type\": \"%s\"\n"),
                *StaticEnum<ETaskDependencyType>()->GetNameStringByValue(static_cast<int64>(Dependency.Value)));
            
            if (j < Node.Dependencies.Num() - 1)
            {
                Result += TEXT("        },\n");
            }
            else
            {
                Result += TEXT("        }\n");
            }
        }
        
        Result += TEXT("      ]\n");
        
        if (i < Nodes.Num() - 1)
        {
            Result += TEXT("    },\n");
        }
        else
        {
            Result += TEXT("    }\n");
        }
    }
    
    Result += TEXT("  ],\n");
    
    // Add edges
    Result += TEXT("  \"edges\": [\n");
    
    for (int32 i = 0; i < Edges.Num(); ++i)
    {
        const TPair<uint64, uint64>& Edge = Edges[i];
        
        Result += TEXT("    {\n");
        Result += FString::Printf(TEXT("      \"source\": %llu,\n"), Edge.Key);
        Result += FString::Printf(TEXT("      \"target\": %llu\n"), Edge.Value);
        
        if (i < Edges.Num() - 1)
        {
            Result += TEXT("    },\n");
        }
        else
        {
            Result += TEXT("    }\n");
        }
    }
    
    Result += TEXT("  ]\n");
    Result += TEXT("}\n");
    
    return Result;
}

FString FTaskDependencyVisualizer::GenerateTextVisualization(const TArray<FTaskDependencyNode>& Nodes,
    const TArray<TPair<uint64, uint64>>& Edges, const FVisualizationOptions& Options)
{
    FString Result = TEXT("Task Dependency Visualization\n");
    Result += TEXT("=============================\n\n");
    
    // Add task count information
    Result += FString::Printf(TEXT("Total Tasks: %d\n\n"), Nodes.Num());
    
    // Group tasks by status
    TMap<ETaskStatus, TArray<const FTaskDependencyNode*>> TasksByStatus;
    
    for (const FTaskDependencyNode& Node : Nodes)
    {
        TasksByStatus.FindOrAdd(Node.Status).Add(&Node);
    }
    
    // Report task counts by status
    Result += TEXT("Tasks by Status:\n");
    
    for (auto& Pair : TasksByStatus)
    {
        FString StatusName = StaticEnum<ETaskStatus>()->GetNameStringByValue(static_cast<int64>(Pair.Key));
        Result += FString::Printf(TEXT("  %s: %d\n"), *StatusName, Pair.Value.Num());
    }
    
    Result += TEXT("\n");
    
    if (Options.bGroupByType)
    {
        // Group tasks by type
        TMap<ETaskType, TArray<const FTaskDependencyNode*>> TasksByType;
        
        for (const FTaskDependencyNode& Node : Nodes)
        {
            TasksByType.FindOrAdd(Node.Type).Add(&Node);
        }
        
        // List tasks by type
        Result += TEXT("Tasks by Type:\n");
        
        for (auto& Pair : TasksByType)
        {
            FString TypeName = StaticEnum<ETaskType>()->GetNameStringByValue(static_cast<int64>(Pair.Key));
            Result += FString::Printf(TEXT("  %s (%d tasks):\n"), *TypeName, Pair.Value.Num());
            
            for (const FTaskDependencyNode* Node : Pair.Value)
            {
                FString StatusName = StaticEnum<ETaskStatus>()->GetNameStringByValue(static_cast<int64>(Node->Status));
                
                Result += FString::Printf(TEXT("    [%llu] %s (%s)\n"), Node->TaskId, *Node->Description, *StatusName);
                
                if (Options.bIncludeTaskStats)
                {
                    if (Node->Progress > 0)
                    {
                        Result += FString::Printf(TEXT("      Progress: %d%%\n"), Node->Progress);
                    }
                    
                    Result += FString::Printf(TEXT("      Queue Time: %.2f ms\n"), Node->QueueTimeMs);
                    
                    if (Node->ExecutionTimeMs > 0)
                    {
                        Result += FString::Printf(TEXT("      Execution Time: %.2f ms\n"), Node->ExecutionTimeMs);
                    }
                }
                
                // List dependencies
                TArray<uint64> DependencyIds;
                
                for (const TPair<uint64, uint64>& Edge : Edges)
                {
                    if (Edge.Value == Node->TaskId)
                    {
                        DependencyIds.Add(Edge.Key);
                    }
                }
                
                if (DependencyIds.Num() > 0)
                {
                    Result += TEXT("      Dependencies: ");
                    
                    for (int32 i = 0; i < DependencyIds.Num(); ++i)
                    {
                        if (i > 0)
                        {
                            Result += TEXT(", ");
                        }
                        
                        Result += FString::Printf(TEXT("%llu"), DependencyIds[i]);
                    }
                    
                    Result += TEXT("\n");
                }
                
                Result += TEXT("\n");
            }
        }
    }
    else
    {
        // List all tasks
        Result += TEXT("Task Details:\n");
        
        for (const FTaskDependencyNode& Node : Nodes)
        {
            FString StatusName = StaticEnum<ETaskStatus>()->GetNameStringByValue(static_cast<int64>(Node.Status));
            FString PriorityName = StaticEnum<ETaskPriority>()->GetNameStringByValue(static_cast<int64>(Node.Priority));
            FString TypeName = StaticEnum<ETaskType>()->GetNameStringByValue(static_cast<int64>(Node.Type));
            
            Result += FString::Printf(TEXT("  [%llu] %s\n"), Node.TaskId, *Node.Description);
            Result += FString::Printf(TEXT("    Status: %s\n"), *StatusName);
            Result += FString::Printf(TEXT("    Priority: %s\n"), *PriorityName);
            Result += FString::Printf(TEXT("    Type: %s\n"), *TypeName);
            
            if (Options.bIncludeTaskStats)
            {
                if (Node.Progress > 0)
                {
                    Result += FString::Printf(TEXT("    Progress: %d%%\n"), Node.Progress);
                }
                
                Result += FString::Printf(TEXT("    Queue Time: %.2f ms\n"), Node.QueueTimeMs);
                
                if (Node.ExecutionTimeMs > 0)
                {
                    Result += FString::Printf(TEXT("    Execution Time: %.2f ms\n"), Node.ExecutionTimeMs);
                }
            }
            
            // List dependencies
            TArray<uint64> DependencyIds;
            
            for (const TPair<uint64, uint64>& Edge : Edges)
            {
                if (Edge.Value == Node.TaskId)
                {
                    DependencyIds.Add(Edge.Key);
                }
            }
            
            if (DependencyIds.Num() > 0)
            {
                Result += TEXT("    Dependencies: ");
                
                for (int32 i = 0; i < DependencyIds.Num(); ++i)
                {
                    if (i > 0)
                    {
                        Result += TEXT(", ");
                    }
                    
                    Result += FString::Printf(TEXT("%llu"), DependencyIds[i]);
                }
                
                Result += TEXT("\n");
            }
            
            // List dependents
            TArray<uint64> DependentIds;
            
            for (const TPair<uint64, uint64>& Edge : Edges)
            {
                if (Edge.Key == Node.TaskId)
                {
                    DependentIds.Add(Edge.Value);
                }
            }
            
            if (DependentIds.Num() > 0)
            {
                Result += TEXT("    Dependents: ");
                
                for (int32 i = 0; i < DependentIds.Num(); ++i)
                {
                    if (i > 0)
                    {
                        Result += TEXT(", ");
                    }
                    
                    Result += FString::Printf(TEXT("%llu"), DependentIds[i]);
                }
                
                Result += TEXT("\n");
            }
            
            Result += TEXT("\n");
        }
    }
    
    return Result;
}

FString FTaskDependencyVisualizer::GetStatusColor(ETaskStatus Status) const
{
    switch (Status)
    {
        case ETaskStatus::Queued:
            return TEXT("#FFB347"); // Orange
        
        case ETaskStatus::Executing:
            return TEXT("#77DD77"); // Light green
        
        case ETaskStatus::Completed:
            return TEXT("#B0E0E6"); // Light blue
        
        case ETaskStatus::Cancelled:
            return TEXT("#FFD1DC"); // Pink
        
        case ETaskStatus::Failed:
            return TEXT("#FF6961"); // Light red
        
        default:
            return TEXT("#FFFFFF"); // White
    }
}

FString FTaskDependencyVisualizer::GetPriorityColor(ETaskPriority Priority) const
{
    switch (Priority)
    {
        case ETaskPriority::Critical:
            return TEXT("#FF0000"); // Red
        
        case ETaskPriority::High:
            return TEXT("#FFA500"); // Orange
        
        case ETaskPriority::Normal:
            return TEXT("#FFFF00"); // Yellow
        
        case ETaskPriority::Low:
            return TEXT("#ADFF2F"); // Green-yellow
        
        case ETaskPriority::Background:
            return TEXT("#90EE90"); // Light green
        
        default:
            return TEXT("#FFFFFF"); // White
    }
}

FString FTaskDependencyVisualizer::GetTypeShape(ETaskType Type) const
{
    switch (Type)
    {
        case ETaskType::General:
            return TEXT("box");
        
        case ETaskType::MiningOperation:
            return TEXT("ellipse");
        
        case ETaskType::SDFOperation:
            return TEXT("diamond");
        
        case ETaskType::OctreeTraversal:
            return TEXT("triangle");
        
        case ETaskType::MaterialOperation:
            return TEXT("hexagon");
        
        case ETaskType::ZoneTransaction:
            return TEXT("octagon");
        
        default:
            return TEXT("box");
    }
} 