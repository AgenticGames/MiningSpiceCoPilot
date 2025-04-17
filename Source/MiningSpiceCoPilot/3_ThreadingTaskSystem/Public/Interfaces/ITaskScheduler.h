// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "TaskSystem/TaskTypes.h" // Include TaskTypes.h to use the UENUM marked enums
#include "ITaskScheduler.generated.h"

// Forward declarations
class FTaskGraphInterface;
class FRunnableThread;
// Using Engine's EThreadPriority enum from GenericPlatformAffinity.h, no need to forward declare

// Using enums from TaskTypes.h now

/**
 * Task dependency description for complex task chains
 */
struct MININGSPICECOPILOT_API FTaskDependency
{
    /** Unique ID of the dependent task */
    uint64 TaskId;
    
    /** Whether this dependency is required or optional */
    bool bRequired;
    
    /** Timeout in milliseconds (0 for no timeout) */
    uint32 TimeoutMs;
};

/**
 * Task configuration for creation and scheduling
 */
struct MININGSPICECOPILOT_API FTaskConfig
{
    /** Task priority level */
    ETaskPriority Priority;
    
    /** Task type for specialized handling */
    ETaskType Type;
    
    /** Preferred CPU core for execution (INDEX_NONE for no preference) */
    int32 PreferredCore;
    
    /** Whether the task can be canceled */
    bool bCancellable;
    
    /** Whether the task supports progress reporting */
    bool bSupportsProgress;
    
    /** Array of task dependencies that must complete before execution */
    TArray<FTaskDependency> Dependencies;
    
    /** Maximum execution time in milliseconds (0 for no limit) */
    uint32 MaxExecutionTimeMs;
    
    /** Whether to automatically retry on failure */
    bool bAutoRetry;
    
    /** Maximum number of retry attempts */
    uint32 MaxRetries;
    
    /** Priority boost for retried tasks */
    uint8 RetryPriorityBoost;
    
    /** Constructor with default values */
    FTaskConfig()
        : Priority(ETaskPriority::Normal)
        , Type(ETaskType::General)
        , PreferredCore(INDEX_NONE)
        , bCancellable(true)
        , bSupportsProgress(false)
        , MaxExecutionTimeMs(0)
        , bAutoRetry(false)
        , MaxRetries(0)
        , RetryPriorityBoost(0)
    {
    }
};

/**
 * Task statistics for performance monitoring
 */
struct MININGSPICECOPILOT_API FTaskStats
{
    /** Time spent queued in milliseconds */
    double QueueTimeMs;
    
    /** Time spent executing in milliseconds */
    double ExecutionTimeMs;
    
    /** Number of retry attempts */
    uint32 RetryCount;
    
    /** Peak memory usage in bytes */
    uint64 PeakMemoryBytes;
    
    /** Thread that executed the task */
    uint32 ExecutingThreadId;
    
    /** CPU core that executed the task */
    int32 ExecutingCore;
};

/**
 * Base interface for task schedulers in the SVO+SDF mining architecture
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UTaskScheduler : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for task scheduling in the SVO+SDF mining architecture
 * Provides task creation and management capabilities optimized for mining operations
 */
class MININGSPICECOPILOT_API ITaskScheduler
{
    GENERATED_BODY()

public:
    /**
     * Initializes the task scheduler and prepares it for use
     * @return True if initialization was successful
     */
    virtual bool Initialize() = 0;
    
    /**
     * Shuts down the task scheduler and cleans up resources
     */
    virtual void Shutdown() = 0;
    
    /**
     * Checks if the task scheduler has been initialized
     * @return True if initialized, false otherwise
     */
    virtual bool IsInitialized() const = 0;
    
    /**
     * Schedules a task for execution
     * @param TaskFunc Function to execute
     * @param Config Task configuration
     * @param Desc Optional task description for debugging
     * @return Unique task ID or 0 if scheduling failed
     */
    virtual uint64 ScheduleTask(TFunction<void()> TaskFunc, const FTaskConfig& Config, const FString& Desc = TEXT("")) = 0;
    
    /**
     * Schedules a task for execution with a result callback
     * @param TaskFunc Function to execute
     * @param OnComplete Function to call when task completes
     * @param Config Task configuration
     * @param Desc Optional task description for debugging
     * @return Unique task ID or 0 if scheduling failed
     */
    virtual uint64 ScheduleTaskWithCallback(TFunction<void()> TaskFunc, TFunction<void(bool)> OnComplete, 
        const FTaskConfig& Config, const FString& Desc = TEXT("")) = 0;
    
    /**
     * Cancels a previously scheduled task
     * @param TaskId ID of the task to cancel
     * @return True if task was cancelled, false if it couldn't be cancelled or doesn't exist
     */
    virtual bool CancelTask(uint64 TaskId) = 0;
    
    /**
     * Gets the status of a task
     * @param TaskId ID of the task to check
     * @return Current status of the task
     */
    virtual ETaskStatus GetTaskStatus(uint64 TaskId) const = 0;
    
    /**
     * Gets statistics for a task
     * @param TaskId ID of the task to get statistics for
     * @return Statistics structure for the task
     */
    virtual FTaskStats GetTaskStats(uint64 TaskId) const = 0;
    
    /**
     * Gets the progress of a task that supports progress reporting
     * @param TaskId ID of the task to check
     * @param OutProgress Output parameter for the progress value (0.0 to 1.0)
     * @return True if progress could be determined, false otherwise
     */
    virtual bool GetTaskProgress(uint64 TaskId, float& OutProgress) const = 0;
    
    /**
     * Waits for a task to complete
     * @param TaskId ID of the task to wait for
     * @param TimeoutMs Maximum time to wait in milliseconds (0 for indefinite)
     * @return True if the task completed successfully, false if it failed, was cancelled, or timed out
     */
    virtual bool WaitForTask(uint64 TaskId, uint32 TimeoutMs = 0) = 0;
    
    /**
     * Waits for multiple tasks to complete
     * @param TaskIds Array of task IDs to wait for
     * @param bWaitForAll If true, waits for all tasks to complete; if false, waits for any task to complete
     * @param TimeoutMs Maximum time to wait in milliseconds (0 for indefinite)
     * @return True if all (or any, depending on bWaitForAll) tasks completed successfully
     */
    virtual bool WaitForTasks(const TArray<uint64>& TaskIds, bool bWaitForAll = true, uint32 TimeoutMs = 0) = 0;
    
    /**
     * Gets the number of worker threads in the task scheduler
     * @return Number of worker threads
     */
    virtual uint32 GetWorkerThreadCount() const = 0;
    
    /**
     * Gets the current thread ID within the task system
     * @return Thread ID or INDEX_NONE if not a task system thread
     */
    virtual int32 GetCurrentThreadId() const = 0;
    
    /**
     * Checks if the current thread is a task system thread
     * @return True if called from a task system thread
     */
    virtual bool IsTaskThread() const = 0;
    
    /**
     * Sets the thread priority for a specific thread
     * @param ThreadId ID of the thread to modify
     * @param Priority New thread priority
     * @return True if successful
     */
    virtual bool SetThreadPriority(int32 ThreadId, EThreadPriority Priority) = 0;
    
    /**
     * Sets thread affinity for a specific thread
     * @param ThreadId ID of the thread to modify
     * @param CoreMask Bit mask of allowed cores (0 for no affinity)
     * @return True if successful
     */
    virtual bool SetThreadAffinity(int32 ThreadId, uint64 CoreMask) = 0;
    
    /**
     * Gets the current task count
     * @return Structure with task counts by status
     */
    virtual TMap<ETaskStatus, int32> GetTaskCounts() const = 0;
    
    /**
     * Gets the singleton instance
     * @return Reference to the task scheduler
     */
    static ITaskScheduler& Get();
};
