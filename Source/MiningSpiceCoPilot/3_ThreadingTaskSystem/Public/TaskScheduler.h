// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/ITaskScheduler.h"
#include "TaskSystem/TaskTypes.h"
#include "HAL/ThreadManager.h"
#include "HAL/RunnableThread.h"
#include "Containers/LockFreeList.h"
#include "HAL/PlatformAffinity.h"
#include "Async/TaskGraphInterfaces.h"
#include "HAL/ThreadSafeCounter.h"
#include "Misc/SpinLock.h"
#include "HAL/ThreadHeartBeat.h"
#include "HAL/Runnable.h"

// Forward declarations
class FTaskDependencyVisualizer;

// Using FTaskConfig and FTaskDependency from ITaskScheduler.h to avoid redefinition
// struct MININGSPICECOPILOT_API FTaskConfig
// {
//     // Definition moved to ITaskScheduler.h
// };

// struct MININGSPICECOPILOT_API FTaskDependency
// {
//     // Definition moved to ITaskScheduler.h
// };

/**
 * NUMA node information for thread affinity optimization
 */
struct FNumaNodeInfo
{
    /** Index of this NUMA node */
    int32 NodeIndex;
    
    /** List of logical cores (hardware threads) belonging to this node */
    TArray<int32> LogicalCores;
};

/**
 * Task implementation class used by the scheduler
 */
class MININGSPICECOPILOT_API FMiningTask
{
public:
    /** Unique task identifier */
    uint64 Id;
    
    /** Task execution function */
    TFunction<void()> TaskFunction;
    
    /** Task completion callback */
    TFunction<void(bool)> CompletionCallback;
    
    /** Task configuration */
    FTaskConfig Config;
    
    /** Task description for debugging */
    FString Description;
    
    /** Current status of the task */
    FThreadSafeCounter Status;
    
    /** Task statistics */
    FTaskStats Stats;
    
    /** Task progress value (0.0-1.0) */
    FThreadSafeCounter Progress;
    
    /** Creation timestamp */
    double CreationTime;
    
    /** Start timestamp */
    double StartTime;
    
    /** Completion timestamp */
    double CompletionTime;
    
    /** Attempt count for retried tasks */
    FThreadSafeCounter AttemptCount;
    
    /** Dependencies that must be completed before this task */
    TArray<FTaskDependency> Dependencies;
    
    /** Worker thread ID that executed the task */
    int32 ExecutingThreadId;
    
    /** Type ID for registry integration */
    uint32 TypeId;
    
    /** Registry type for the type ID */
    ERegistryType RegistryType;
    
    /** Thread optimization flags */
    EThreadOptimizationFlags OptimizationFlags;
    
    /** SIMD variant for execution */
    ESIMDVariant SIMDVariant;
    
    /** Constructor */
    FMiningTask(uint64 InId, TFunction<void()> InTaskFunction, const FTaskConfig& InConfig, const FString& InDesc);
    
    /** Sets the task progress (0-100) */
    void SetProgress(int32 InProgress);
    
    /** Gets the task progress (0-100) */
    int32 GetProgress() const;
    
    /** Sets the task status */
    void SetStatus(ETaskStatus InStatus);
    
    /** Gets the task status */
    ETaskStatus GetStatus() const;
    
    /** Increments the attempt count */
    int32 IncrementAttempt();
    
    /** Gets the current attempt count */
    int32 GetAttemptCount() const;
    
    /** Executes the task */
    void Execute();
    
    /** Completes the task with a result */
    void Complete(bool bSuccess);
    
    /** Checks if the task has timed out */
    bool HasTimedOut() const;
    
    /** Checks if the task dependencies are satisfied */
    bool AreDependenciesSatisfied(const TMap<uint64, FMiningTask*>& TaskMap) const;
    
    /** Checks if this task has a type ID */
    bool HasTypeId() const
    {
        return TypeId != 0;
    }
    
    /** Gets the type ID */
    uint32 GetTypeId() const
    {
        return TypeId;
    }
    
    /** Gets the registry type */
    ERegistryType GetRegistryType() const
    {
        return RegistryType;
    }
    
    /** Gets the thread optimization flags */
    EThreadOptimizationFlags GetOptimizationFlags() const
    {
        return OptimizationFlags;
    }
    
    /** Gets the SIMD variant */
    ESIMDVariant GetSIMDVariant() const
    {
        return SIMDVariant;
    }
};

/**
 * Worker thread implementation for the task scheduler
 */
class MININGSPICECOPILOT_API FMiningTaskWorker : public FRunnable
{
public:
    /** Constructor */
    FMiningTaskWorker(class FTaskScheduler* InScheduler, int32 InThreadId, EThreadPriority InPriority);
    
    /** Destructor */
    virtual ~FMiningTaskWorker();
    
    //~ Begin FRunnable Interface
    virtual bool Init() override;
    virtual uint32 Run() override;
    virtual void Stop() override;
    virtual void Exit() override;
    //~ End FRunnable Interface
    
    /** Gets the thread ID */
    int32 GetThreadId() const;
    
    /** Sets the thread priority */
    bool SetPriority(EThreadPriority InPriority);
    
    /** Sets the thread affinity mask */
    bool SetAffinity(uint64 CoreMask);
    
    /** Gets the number of tasks processed by this worker */
    uint32 GetTasksProcessed() const;
    
    /** Gets the current task being processed */
    uint64 GetCurrentTaskId() const;
    
    /** Gets whether the worker is idle */
    bool IsIdle() const;
    
    /** Gets the worker's utilization (0.0-1.0) */
    float GetUtilization() const;
    
    /** Gets the worker's processing stats */
    void GetStats(double& OutAverageTaskTimeMs, double& OutIdleTimePercent) const;

protected:
    /** The task scheduler */
    class FTaskScheduler* Scheduler;
    
    /** Thread ID within the task system */
    int32 ThreadId;
    
    /** Whether the thread should continue running */
    FThreadSafeCounter bRunning;
    
    /** The runnable thread instance */
    FRunnableThread* Thread;
    
    /** The priority of the thread */
    EThreadPriority Priority;
    
    /** Affinity mask for the thread */
    uint64 AffinityMask;
    
    /** Number of tasks processed */
    FThreadSafeCounter TasksProcessed;
    
    /** ID of the current task being processed */
    FThreadSafeCounter CurrentTaskId;
    
    /** Last time the thread was idle */
    double LastIdleTime;
    
    /** Total time spent idle */
    FThreadSafeCounter IdleTimeMs;
    
    /** Total time spent processing tasks */
    FThreadSafeCounter ProcessingTimeMs;
    
    /** Total number of tasks processed in the stats period */
    FThreadSafeCounter StatsTaskCount;
    
    /** Total task time in the stats period */
    FThreadSafeCounter StatsTaskTimeMs;
    
    /** Time of last stats reset */
    double LastStatsResetTime;
    
    /** Accessor methods needed by specialized workers */
    class FTaskScheduler* GetScheduler() const { return Scheduler; }
    void IncrementTasksProcessed() { TasksProcessed.Increment(); }
    
    /** Virtual method for task selection that can be overridden */
    virtual FMiningTask* SelectNextTask();
};

/**
 * Specialized worker thread implementation that optimizes for specific type capabilities
 * Prioritizes tasks that match supported capabilities
 */
class MININGSPICECOPILOT_API FSpecializedTaskWorker : public FMiningTaskWorker
{
public:
    /** Constructor */
    FSpecializedTaskWorker(class FTaskScheduler* InScheduler, int32 InThreadId, EThreadPriority InPriority, ETypeCapabilities InSupportedCapabilities)
        : FMiningTaskWorker(InScheduler, InThreadId, InPriority)
        , SupportedCapabilities(InSupportedCapabilities)
    {
    }
    
    /** Gets the supported capabilities */
    ETypeCapabilities GetSupportedCapabilities() const
    {
        return SupportedCapabilities;
    }
    
    /** Sets the supported capabilities */
    void SetSupportedCapabilities(ETypeCapabilities InCapabilities)
    {
        SupportedCapabilities = InCapabilities;
    }
    
    /** Checks if this worker supports the given capabilities */
    bool SupportsCapabilities(ETypeCapabilities InCapabilities) const
    {
        // Check if all required capabilities are supported
        return (static_cast<uint32>(SupportedCapabilities) & static_cast<uint32>(InCapabilities)) == static_cast<uint32>(InCapabilities);
    }

protected:
    /** Specialized capabilities supported by this worker */
    ETypeCapabilities SupportedCapabilities;
    
    /** Override for specialized task selection */
    virtual FMiningTask* SelectNextTask() override;
};

/**
 * Task scheduler implementation for the Mining system
 * Manages the scheduling and execution of tasks with support for priorities,
 * dependencies, and specialized processing for mining operations.
 */
class MININGSPICECOPILOT_API FTaskScheduler : public ITaskScheduler
{
public:
    /** Constructor */
    FTaskScheduler();
    
    /** Destructor */
    virtual ~FTaskScheduler();

    //~ Begin ITaskScheduler Interface
    virtual bool Initialize() override;
    virtual void Shutdown() override;
    virtual bool IsInitialized() const override;
    
    virtual uint64 ScheduleTask(TFunction<void()> TaskFunc, const FTaskConfig& Config, const FString& Desc = TEXT("")) override;
    virtual uint64 ScheduleTaskWithCallback(TFunction<void()> TaskFunc, TFunction<void(bool)> OnComplete, 
        const FTaskConfig& Config, const FString& Desc = TEXT("")) override;
    
    virtual bool CancelTask(uint64 TaskId) override;
    virtual ETaskStatus GetTaskStatus(uint64 TaskId) const override;
    virtual FTaskStats GetTaskStats(uint64 TaskId) const override;
    virtual bool GetTaskProgress(uint64 TaskId, float& OutProgress) const override;
    
    virtual bool WaitForTask(uint64 TaskId, uint32 TimeoutMs = 0) override;
    virtual bool WaitForTasks(const TArray<uint64>& TaskIds, bool bWaitForAll = true, uint32 TimeoutMs = 0) override;
    
    virtual uint32 GetWorkerThreadCount() const override;
    virtual int32 GetCurrentThreadId() const override;
    virtual bool IsTaskThread() const override;
    
    virtual bool SetThreadPriority(int32 ThreadId, EThreadPriority Priority) override;
    virtual bool SetThreadAffinity(int32 ThreadId, uint64 CoreMask) override;
    
    virtual TMap<ETaskStatus, int32> GetTaskCounts() const override;
    
    static ITaskScheduler& Get();
    //~ End ITaskScheduler Interface
    
    /** Gets the next task to execute */
    FMiningTask* GetNextTask(int32 WorkerId);
    
    /** Gets a task by ID - exposed for task dependency visualization */
    FMiningTask* GetTaskById(uint64 TaskId) const;
    
    /** Gets all tasks - exposed for task dependency visualization */
    TMap<uint64, FMiningTask*> GetAllTasks() const;
    
    /**
     * Creates a specialized worker thread
     * @param Capabilities The type capabilities this worker should specialize in
     * @param Priority Thread priority for the worker
     * @return Thread ID of the created worker
     */
    int32 CreateSpecializedWorker(ETypeCapabilities Capabilities, EThreadPriority Priority = TPri_Normal);
    
    /**
     * Gets all specialized worker threads
     * @return Map of capabilities to worker thread IDs
     */
    TMap<ETypeCapabilities, TArray<int32>> GetSpecializedWorkers() const;
    
    /**
     * Gets type capabilities from a registry type and ID
     * @param TypeId The type ID to query
     * @param RegistryType The registry containing the type
     * @return Capabilities of the specified type
     */
    static ETypeCapabilities GetTypeCapabilities(uint32 TypeId, ERegistryType RegistryType);
    
    /**
     * Maps type capabilities to thread optimization flags
     * @param Capabilities The capabilities to map
     * @return Corresponding optimization flags
     */
    static EThreadOptimizationFlags MapCapabilitiesToOptimizationFlags(ETypeCapabilities Capabilities);
    
    /**
     * Detects available processor features
     * @return Mask of available processor features
     */
    static EProcessorFeatures DetectProcessorFeatures();
    
    /**
     * Registers an optimized function variant for a type operation
     * @param TypeId The type ID
     * @param RegistryType The registry containing the type
     * @param Variant The SIMD variant
     * @param ImplFunc The implementation function
     * @return True if registration succeeded
     */
    bool RegisterTypeOperationVariant(uint32 TypeId, ERegistryType RegistryType, ESIMDVariant Variant, TFunction<void(void*,void*)> ImplFunc);
    
    /**
     * Gets the best available implementation for a type operation
     * @param TypeId The type ID
     * @param RegistryType The registry containing the type
     * @return The implementation function or nullptr if not found
     */
    TFunction<void(void*,void*)> GetBestTypeOperationVariant(uint32 TypeId, ERegistryType RegistryType);
    
    /**
     * Finds the best worker for a specific task
     * @param Task The task to find a worker for
     * @return Index of the most suitable worker, or -1 if no specialized workers are available
     */
    int32 FindBestWorkerForTask(const FMiningTask* Task);
    
    /**
     * Finds a worker with specific capabilities
     * @param Capabilities The required capabilities
     * @return Index of a suitable worker, or -1 if no suitable worker is available
     */
    int32 FindWorkerWithCapabilities(ETypeCapabilities Capabilities);
    
    /** Thread local storage slot for worker ID */
    static uint32 WorkerThreadTLS;
    
    /** The singleton instance of the scheduler */
    static FTaskScheduler* Instance;
    
    /** Friend class for dependency visualization */
    friend class FTaskDependencyVisualizer;

private:
    /** Whether the scheduler has been initialized */
    bool bIsInitialized;
    
    /** Worker threads */
    TArray<FMiningTaskWorker*> Workers;
    
    /** Task maps for each priority level */
    TMap<ETaskPriority, TArray<FMiningTask*>> TaskQueues;
    
    /** Map of all tasks by ID */
    TMap<uint64, FMiningTask*> AllTasks;
    
    /** Number of logical cores */
    int32 NumLogicalCores;
    
    /** Next task ID */
    FThreadSafeCounter NextTaskId;
    
    /** Lock for task queues */
    mutable FCriticalSection TaskQueueLock;
    
    /** Lock for task map */
    mutable FCriticalSection TaskMapLock;
    
    /** Number of tasks scheduled */
    FThreadSafeCounter TasksScheduled;
    
    /** Number of tasks completed */
    FThreadSafeCounter TasksCompleted;
    
    /** Number of tasks cancelled */
    FThreadSafeCounter TasksCancelled;
    
    /** Number of tasks failed */
    FThreadSafeCounter TasksFailed;
    
    /** Task count by status */
    TMap<ETaskStatus, FThreadSafeCounter> TaskCountByStatus;
    
    /** Cleanup thread */
    FRunnableThread* CleanupThread;
    
    /** Specialized worker threads */
    TMap<ETypeCapabilities, TArray<FSpecializedTaskWorker*>> SpecializedWorkers;
    
    /** Type operation implementations by variant */
    TMap<uint64, TMap<ESIMDVariant, TFunction<void(void*,void*)>>> TypeOperationVariants;
    
    /** Available processor features */
    EProcessorFeatures ProcessorFeatures;
    
    /** Lock for worker array access */
    mutable FCriticalSection WorkerArrayLock;
    
    /** Lock for specialized worker map access */
    mutable FCriticalSection SpecializedWorkerMapLock;
    
    /** Lock for type operation variants access */
    mutable FCriticalSection TypeOperationVariantsLock;
    
    /** Determines worker thread count based on available hardware */
    int32 DetermineWorkerThreadCount() const;
    
    /** Creates worker threads */
    void CreateWorkerThreads(int32 ThreadCount);
    
    /** Generates a unique task ID */
    uint64 GenerateTaskId();
    
    /** Cleans up completed tasks */
    void CleanupCompletedTasks(double MaxAgeSeconds = 300.0);
    
    /**
     * Creates a combined key for type operation variants
     * @param TypeId The type ID
     * @param RegistryType The registry type
     * @return Combined key
     */
    static uint64 CreateTypeOperationKey(uint32 TypeId, ERegistryType RegistryType)
    {
        return (static_cast<uint64>(RegistryType) << 32) | TypeId;
    }
    
    /** Calculates NUMA-aware affinity mask */
    uint64 CalculateNumaAwareAffinityMask(int32 ThreadIndex, int32 TotalThreads, const TArray<FNumaNodeInfo>& NumaNodes);
};

/**
 * Helper function to schedule a task using the task scheduler
 * This function is used by registry implementations to avoid circular dependencies
 * 
 * @param TaskFunc The task function to execute
 * @param Config Configuration for the task
 * @return Task ID of the scheduled task
 */
// inline MININGSPICECOPILOT_API uint64 ScheduleTaskWithScheduler(TFunction<void()> TaskFunc, const struct FTaskConfig& Config);