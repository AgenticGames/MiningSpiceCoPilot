// Copyright Epic Games, Inc. All Rights Reserved.

#include "TaskDependencyVisualizer.h"
#include "TaskScheduler.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Templates/Function.h"

/**
 * Test program for the task dependency visualizer
 * Creates a sample task graph and generates visualizations in different formats
 */
void TestTaskDependencyVisualizer()
{
    // Initialize the task scheduler
    FTaskScheduler* Scheduler = new FTaskScheduler();
    Scheduler->Initialize();
    
    // Create a task dependency visualizer
    FTaskDependencyVisualizer* Visualizer = new FTaskDependencyVisualizer();
    
    // Output directory for visualizations
    FString OutputDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TaskVisualizations"));
    IFileManager::Get().MakeDirectory(*OutputDir, true);
    
    // Create a complex task graph with dependencies
    
    // Task 1: Root task (Critical)
    FTaskConfig RootConfig;
    RootConfig.Priority = ETaskPriority::Critical;
    RootConfig.Type = ETaskType::General;
    RootConfig.bSupportsProgress = true;
    
    uint64 RootTaskId = Scheduler->ScheduleTask(
        []() { FPlatformProcess::Sleep(0.1f); },
        RootConfig,
        TEXT("Root Task")
    );
    
    // Tasks 2-4: Level 1 tasks dependent on root (High priority)
    TArray<uint64> Level1TaskIds;
    
    for (int32 i = 0; i < 3; ++i)
    {
        FTaskConfig Config;
        Config.Priority = ETaskPriority::High;
        Config.Type = (i == 0) ? ETaskType::MiningOperation : 
                      (i == 1) ? ETaskType::SDFOperation : 
                                 ETaskType::OctreeTraversal;
        Config.bSupportsProgress = true;
        
        // Add dependency on root task
        FTaskDependency Dependency;
        Dependency.TaskId = RootTaskId;
        Dependency.bRequired = true;
        Dependency.TimeoutMs = 0;
        
        Config.Dependencies.Add(Dependency);
        
        FString Description = FString::Printf(TEXT("Level 1 Task %d"), i + 1);
        
        uint64 TaskId = Scheduler->ScheduleTask(
            [i]() { FPlatformProcess::Sleep(0.1f); },
            Config,
            Description
        );
        
        Level1TaskIds.Add(TaskId);
    }
    
    // Tasks 5-10: Level 2 tasks dependent on level 1 (Normal priority)
    TArray<uint64> Level2TaskIds;
    
    for (int32 i = 0; i < 6; ++i)
    {
        FTaskConfig Config;
        Config.Priority = ETaskPriority::Normal;
        Config.Type = ETaskType::MaterialOperation;
        Config.bSupportsProgress = (i % 2 == 0);  // Every second task supports progress
        
        // Add dependency on one of the level 1 tasks
        FTaskDependency Dependency;
        Dependency.TaskId = Level1TaskIds[i % Level1TaskIds.Num()];
        Dependency.bRequired = (i % 2 == 0);  // Some dependencies are optional
        Dependency.TimeoutMs = 0;
        
        Config.Dependencies.Add(Dependency);
        
        // Add optional dependency on root for some tasks
        if (i < 3)
        {
            FTaskDependency RootDependency;
            RootDependency.TaskId = RootTaskId;
            RootDependency.bRequired = false;
            RootDependency.TimeoutMs = 0;
            
            Config.Dependencies.Add(RootDependency);
        }
        
        FString Description = FString::Printf(TEXT("Level 2 Task %d"), i + 1);
        
        uint64 TaskId = Scheduler->ScheduleTask(
            [i]() { 
                FPlatformProcess::Sleep(0.1f); 
                
                // Update progress for tasks that support it
                if (i % 2 == 0)
                {
                    FMiningTask* Task = static_cast<FTaskScheduler*>(&FTaskScheduler::Get())->GetTaskById(i);
                    if (Task)
                    {
                        Task->SetProgress(75);  // Set progress to 75%
                    }
                }
            },
            Config,
            Description
        );
        
        Level2TaskIds.Add(TaskId);
    }
    
    // Tasks 11-15: Level 3 tasks with dependencies on level 2 (Low priority)
    TArray<uint64> Level3TaskIds;
    
    for (int32 i = 0; i < 5; ++i)
    {
        FTaskConfig Config;
        Config.Priority = ETaskPriority::Low;
        Config.Type = ETaskType::ZoneTransaction;
        
        // Add dependencies on two level 2 tasks
        for (int32 j = 0; j < 2; ++j)
        {
            int32 DependencyIndex = (i + j) % Level2TaskIds.Num();
            
            FTaskDependency Dependency;
            Dependency.TaskId = Level2TaskIds[DependencyIndex];
            Dependency.bRequired = true;
            Dependency.TimeoutMs = 0;
            
            Config.Dependencies.Add(Dependency);
        }
        
        FString Description = FString::Printf(TEXT("Level 3 Task %d"), i + 1);
        
        uint64 TaskId = Scheduler->ScheduleTask(
            []() { FPlatformProcess::Sleep(0.1f); },
            Config,
            Description
        );
        
        Level3TaskIds.Add(TaskId);
    }
    
    // Visualize the entire task graph in DOT format
    FVisualizationOptions Options;
    Options.bIncludeTaskStats = true;
    Options.bGroupByType = false;
    
    FString DotVisualization = Visualizer->VisualizeAllTasks(Options, EVisualizationFormat::DOT);
    FString DotFilename = FPaths::Combine(OutputDir, TEXT("TaskGraph.dot"));
    Visualizer->SaveVisualization(DotFilename, DotVisualization, EVisualizationFormat::DOT);
    
    // Visualize the graph in JSON format
    FString JsonVisualization = Visualizer->VisualizeAllTasks(Options, EVisualizationFormat::JSON);
    FString JsonFilename = FPaths::Combine(OutputDir, TEXT("TaskGraph.json"));
    Visualizer->SaveVisualization(JsonFilename, JsonVisualization, EVisualizationFormat::JSON);
    
    // Generate a text report
    FString TextVisualization = Visualizer->VisualizeAllTasks(Options, EVisualizationFormat::Text);
    FString TextFilename = FPaths::Combine(OutputDir, TEXT("TaskReport.txt"));
    Visualizer->SaveVisualization(TextFilename, TextVisualization, EVisualizationFormat::Text);
    
    // Visualize just one subtree (from a level 1 task)
    FString SubtreeVisualization = Visualizer->VisualizeTask(Level1TaskIds[0], Options, EVisualizationFormat::DOT);
    FString SubtreeFilename = FPaths::Combine(OutputDir, TEXT("Subtree.dot"));
    Visualizer->SaveVisualization(SubtreeFilename, SubtreeVisualization, EVisualizationFormat::DOT);
    
    // Wait for all tasks to complete
    TArray<uint64> AllTaskIds = Level3TaskIds;
    AllTaskIds.Append(Level2TaskIds);
    AllTaskIds.Append(Level1TaskIds);
    AllTaskIds.Add(RootTaskId);
    
    Scheduler->WaitForTasks(AllTaskIds, true, 5000);
    
    // Generate visualizations again after tasks have completed
    FString CompletedDotVisualization = Visualizer->VisualizeAllTasks(Options, EVisualizationFormat::DOT);
    FString CompletedDotFilename = FPaths::Combine(OutputDir, TEXT("TaskGraph_Completed.dot"));
    Visualizer->SaveVisualization(CompletedDotFilename, CompletedDotVisualization, EVisualizationFormat::DOT);
    
    // Generate a report grouped by task type
    Options.bGroupByType = true;
    FString GroupedTextVisualization = Visualizer->VisualizeAllTasks(Options, EVisualizationFormat::Text);
    FString GroupedTextFilename = FPaths::Combine(OutputDir, TEXT("TaskReport_Grouped.txt"));
    Visualizer->SaveVisualization(GroupedTextFilename, GroupedTextVisualization, EVisualizationFormat::Text);
    
    // Clean up
    delete Visualizer;
    Scheduler->Shutdown();
    delete Scheduler;
    
    UE_LOG(LogTemp, Display, TEXT("Task dependency visualization test completed. Output saved to %s"), *OutputDir);
} 