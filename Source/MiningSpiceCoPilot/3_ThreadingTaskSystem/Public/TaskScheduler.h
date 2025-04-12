// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/ITaskScheduler.h"
#include "HAL/ThreadManager.h"
#include "HAL/RunnableThread.h"
#include "Containers/LockFreeList.h"
#include "HAL/PlatformAffinity.h"
#include "Async/TaskGraphInterfaces.h"
#include "HAL/ThreadSafeCounter.h"
#include "Misc/SpinLock.h"
#include "HAL/ThreadHeartBeat.h"

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

private:
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
    
    /** Thread local storage slot for worker ID */
    static uint32 WorkerThreadTLS;

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
    
    /** Determines worker thread count based on available hardware */
    int32 DetermineWorkerThreadCount() const;
    
    /** Creates worker threads */
    void CreateWorkerThreads(int32 ThreadCount);
    
    /** Generates a unique task ID */
    uint64 GenerateTaskId();
    
    /** Gets a task by ID */
    FMiningTask* GetTaskById(uint64 TaskId) const;
    
    /** Removes completed tasks older than the specified age */
    void CleanupCompletedTasks(double MaxAgeSeconds = 300.0);

    /** Singleton instance */
    static FTaskScheduler* Instance;
};