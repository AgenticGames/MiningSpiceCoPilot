// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TaskScheduler.h"

/**
 * Task dependency relationship types
 */
UENUM(BlueprintType)
enum class ETaskDependencyType : uint8
{
    /** Required dependency - task must complete before dependent can start */
    Required UMETA(DisplayName = "Required"),
    
    /** Optional dependency - task completion is preferred but not mandatory */
    Optional UMETA(DisplayName = "Optional"),
    
    /** Parallel dependency - tasks can run in parallel but have a relationship */
    Parallel UMETA(DisplayName = "Parallel"),
    
    /** Sequential dependency - tasks should run in sequence but not necessarily dependent */
    Sequential UMETA(DisplayName = "Sequential")
};

/**
 * Task dependency node for visualization
 */
struct MININGSPICECOPILOT_API FTaskDependencyNode
{
    /** Task ID */
    uint64 TaskId;
    
    /** Task description */
    FString Description;
    
    /** Task status */
    ETaskStatus Status;
    
    /** Task priority */
    ETaskPriority Priority;
    
    /** Task type */
    ETaskType Type;
    
    /** Task progress (0-100) */
    int32 Progress;
    
    /** Time spent in queue (milliseconds) */
    double QueueTimeMs;
    
    /** Execution time (milliseconds, 0 if not started) */
    double ExecutionTimeMs;
    
    /** Dependencies of this task */
    TArray<TPair<uint64, ETaskDependencyType>> Dependencies;
    
    /** Constructor */
    FTaskDependencyNode()
        : TaskId(0)
        , Status(ETaskStatus::Queued)
        , Priority(ETaskPriority::Normal)
        , Type(ETaskType::General)
        , Progress(0)
        , QueueTimeMs(0.0)
        , ExecutionTimeMs(0.0)
    {
    }
};

/**
 * Task dependency visualization options
 */
struct MININGSPICECOPILOT_API FVisualizationOptions
{
    /** Whether to include completed tasks */
    bool bIncludeCompletedTasks;
    
    /** Whether to include cancelled tasks */
    bool bIncludeCancelledTasks;
    
    /** Whether to include failed tasks */
    bool bIncludeFailedTasks;
    
    /** Maximum dependency depth to traverse (0 for unlimited) */
    int32 MaxDepth;
    
    /** Whether to include task statistics */
    bool bIncludeTaskStats;
    
    /** Whether to group tasks by type */
    bool bGroupByType;
    
    /** Constructor with default options */
    FVisualizationOptions()
        : bIncludeCompletedTasks(true)
        , bIncludeCancelledTasks(true)
        , bIncludeFailedTasks(true)
        , MaxDepth(0)
        , bIncludeTaskStats(true)
        , bGroupByType(false)
    {
    }
};

/**
 * Task dependency visualization format
 */
enum class EVisualizationFormat : uint8
{
    /** DOT format for Graphviz */
    DOT,
    
    /** JSON format */
    JSON,
    
    /** In-memory graph structure */
    Graph,
    
    /** Plain text summary */
    Text
};

/**
 * Task dependency visualizer for debugging complex task chains
 * Provides visualization of task dependencies and execution states for debugging.
 */
class MININGSPICECOPILOT_API FTaskDependencyVisualizer
{
public:
    /** Constructor */
    FTaskDependencyVisualizer();
    
    /** Destructor */
    ~FTaskDependencyVisualizer();
    
    /**
     * Visualizes the dependencies of a specific task
     * @param TaskId ID of the task to visualize
     * @param Options Visualization options
     * @param Format Output format
     * @return Visualization in the requested format
     */
    FString VisualizeTask(uint64 TaskId, const FVisualizationOptions& Options, EVisualizationFormat Format = EVisualizationFormat::DOT);
    
    /**
     * Visualizes a set of tasks and their dependencies
     * @param TaskIds Array of task IDs to visualize
     * @param Options Visualization options
     * @param Format Output format
     * @return Visualization in the requested format
     */
    FString VisualizeTasks(const TArray<uint64>& TaskIds, const FVisualizationOptions& Options, EVisualizationFormat Format = EVisualizationFormat::DOT);
    
    /**
     * Visualizes all current tasks in the scheduler
     * @param Options Visualization options
     * @param Format Output format
     * @return Visualization in the requested format
     */
    FString VisualizeAllTasks(const FVisualizationOptions& Options, EVisualizationFormat Format = EVisualizationFormat::DOT);
    
    /**
     * Saves a visualization to file
     * @param Filename File to save the visualization to
     * @param Visualization The visualization string
     * @param Format Visualization format
     * @return True if the file was saved successfully
     */
    bool SaveVisualization(const FString& Filename, const FString& Visualization, EVisualizationFormat Format);
    
    /**
     * Gets the singleton instance
     * @return Reference to the task dependency visualizer
     */
    static FTaskDependencyVisualizer& Get();

private:
    /**
     * Builds a dependency graph for a set of tasks
     * @param TaskIds Array of task IDs to include
     * @param Options Visualization options
     * @param OutNodes Output array of dependency nodes
     * @param OutEdges Output array of dependency edges
     */
    void BuildDependencyGraph(const TArray<uint64>& TaskIds, const FVisualizationOptions& Options, 
        TArray<FTaskDependencyNode>& OutNodes, TArray<TPair<uint64, uint64>>& OutEdges);
    
    /**
     * Collects all tasks from the scheduler
     * @param Options Visualization options
     * @return Array of task IDs
     */
    TArray<uint64> CollectAllTasks(const FVisualizationOptions& Options);
    
    /**
     * Generates a DOT format visualization
     * @param Nodes Array of dependency nodes
     * @param Edges Array of dependency edges
     * @param Options Visualization options
     * @return DOT format visualization string
     */
    FString GenerateDOTVisualization(const TArray<FTaskDependencyNode>& Nodes, 
        const TArray<TPair<uint64, uint64>>& Edges, const FVisualizationOptions& Options);
    
    /**
     * Generates a JSON format visualization
     * @param Nodes Array of dependency nodes
     * @param Edges Array of dependency edges
     * @param Options Visualization options
     * @return JSON format visualization string
     */
    FString GenerateJSONVisualization(const TArray<FTaskDependencyNode>& Nodes, 
        const TArray<TPair<uint64, uint64>>& Edges, const FVisualizationOptions& Options);
    
    /**
     * Generates a text format visualization
     * @param Nodes Array of dependency nodes
     * @param Edges Array of dependency edges
     * @param Options Visualization options
     * @return Text format visualization string
     */
    FString GenerateTextVisualization(const TArray<FTaskDependencyNode>& Nodes, 
        const TArray<TPair<uint64, uint64>>& Edges, const FVisualizationOptions& Options);
    
    /**
     * Gets a color string for a task status
     * @param Status Task status
     * @return HTML color string
     */
    FString GetStatusColor(ETaskStatus Status) const;
    
    /**
     * Gets a color string for a task priority
     * @param Priority Task priority
     * @return HTML color string
     */
    FString GetPriorityColor(ETaskPriority Priority) const;
    
    /**
     * Gets a shape string for a task type
     * @param Type Task type
     * @return DOT shape name
     */
    FString GetTypeShape(ETaskType Type) const;
    
    /** Singleton instance */
    static FTaskDependencyVisualizer* Instance;
};