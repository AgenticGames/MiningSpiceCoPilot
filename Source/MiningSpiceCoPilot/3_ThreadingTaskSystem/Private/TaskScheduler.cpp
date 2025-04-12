// Copyright Epic Games, Inc. All Rights Reserved.

#include "TaskScheduler.h"
#include "Misc/ScopeLock.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Event.h"
#include "HAL/CriticalSection.h"
#include "Misc/CoreStats.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Stats/Stats.h"

DECLARE_CYCLE_STAT(TEXT("TaskScheduler_Schedule"), STAT_TaskScheduler_Schedule, STATGROUP_Threading);
DECLARE_CYCLE_STAT(TEXT("TaskScheduler_Execute"), STAT_TaskScheduler_Execute, STATGROUP_Threading);
DECLARE_CYCLE_STAT(TEXT("TaskScheduler_Rebalance"), STAT_TaskScheduler_Rebalance, STATGROUP_Threading);

CSV_DECLARE_CATEGORY_MODULE_EXTERN(MININGSPICECOPILOT_API, Threading);

// Maximum number of worker threads
const int32 MAX_WORKER_THREADS = 32;

// Thread local storage for current worker index
static uint32 WorkerTLS = FPlatformTLS::AllocTlsSlot();

// Thread names for debugging
static const TCHAR* WorkerNames[] = {
    TEXT("WorkerThread_0"), TEXT("WorkerThread_1"), TEXT("WorkerThread_2"), TEXT("WorkerThread_3"),
    TEXT("WorkerThread_4"), TEXT("WorkerThread_5"), TEXT("WorkerThread_6"), TEXT("WorkerThread_7"),
    TEXT("WorkerThread_8"), TEXT("WorkerThread_9"), TEXT("WorkerThread_10"), TEXT("WorkerThread_11"),
    TEXT("WorkerThread_12"), TEXT("WorkerThread_13"), TEXT("WorkerThread_14"), TEXT("WorkerThread_15"),
    TEXT("WorkerThread_16"), TEXT("WorkerThread_17"), TEXT("WorkerThread_18"), TEXT("WorkerThread_19"),
    TEXT("WorkerThread_20"), TEXT("WorkerThread_21"), TEXT("WorkerThread_22"), TEXT("WorkerThread_23"),
    TEXT("WorkerThread_24"), TEXT("WorkerThread_25"), TEXT("WorkerThread_26"), TEXT("WorkerThread_27"),
    TEXT("WorkerThread_28"), TEXT("WorkerThread_29"), TEXT("WorkerThread_30"), TEXT("WorkerThread_31")
};

// Worker thread function
static uint32 WorkerThreadFunc(void* ThreadParam)
{
    FWorkerThreadParams* Params = static_cast<FWorkerThreadParams*>(ThreadParam);
    FTaskScheduler* Scheduler = Params->Scheduler;
    int32 WorkerIndex = Params->WorkerIndex;
    
    // Store worker index in TLS
    FPlatformTLS::SetTlsValue(WorkerTLS, (void*)(UPTRINT)(WorkerIndex + 1));
    
    // Optional thread affinity
    if (Scheduler->ShouldUseThreadAffinity())
    {
        uint64 AffinityMask = Params->ThreadAffinityMask;
        if (AffinityMask != 0)
        {
            FPlatformProcess::SetThreadAffinityMask(AffinityMask);
        }
    }
    
    // Set thread priority
    FPlatformProcess::SetThreadPriority(FPlatformProcess::GetCurrentThread(), Params->ThreadPriority);
    
    // Name the thread for debugging
    FPlatformProcess::SetThreadName(WorkerNames[WorkerIndex % MAX_WORKER_THREADS]);
    
    // Signal that the thread is running
    Params->ThreadStartedEvent->Trigger();
    
    // Execute tasks until shutdown
    Scheduler->ExecuteWorkerLoop(WorkerIndex);
    
    // Signal that the thread has completed
    Params->ThreadCompletedEvent->Trigger();
    
    return 0;
}

// Implementation of task scheduler
FTaskScheduler::FTaskScheduler(const FTaskSchedulerConfig& Config)
    : bIsInitialized(false)
    , bIsShuttingDown(false)
    , bUseThreadAffinity(Config.bUseThreadAffinity)
    , ThreadPriority(Config.ThreadPriority)
    , IdleWaitTime(Config.IdleWaitTime)
    , StealingStrategy(Config.StealingStrategy)
    , RebalanceInterval(Config.RebalanceInterval)
    , LastRebalanceTime(0.0)
    , TasksBatchSize(Config.TasksBatchSize)
    , WorkStealingThreshold(Config.WorkStealingThreshold)
    , bEnableThreadProfiling(Config.bEnableThreadProfiling)
{
    // Create task queues for each priority level
    TaskQueues.SetNum(static_cast<int32>(EPriority::Count));
    
    // Initialize the task queues
    for (int32 i = 0; i < TaskQueues.Num(); ++i)
    {
        TaskQueues[i] = MakeShared<FPriorityTaskQueue>(8);
    }
    
    // Create an event for thread sync
    ShutdownEvent = MakeShared<FEvent>();
    ShutdownEvent->Create(false);
    
    // Initialize performance metrics
    PerformanceMetrics.TotalTasksProcessed = 0;
    PerformanceMetrics.TotalTasksScheduled = 0;
    PerformanceMetrics.TotalTaskStealAttempts = 0;
    PerformanceMetrics.SuccessfulTaskSteals = 0;
    PerformanceMetrics.WorkerUtilization.SetNum(MAX_WORKER_THREADS);
    
    for (float& Utilization : PerformanceMetrics.WorkerUtilization)
    {
        Utilization = 0.0f;
    }
}

FTaskScheduler::~FTaskScheduler()
{
    Shutdown();
}

void FTaskScheduler::Initialize(int32 NumThreads)
{
    FScopeLock Lock(&InitLock);
    
    if (bIsInitialized)
    {
        return;
    }
    
    // Calculate actual thread count based on hardware
    int32 HardwareThreads = FPlatformMisc::NumberOfWorkerThreadsToSpawn();
    ActualThreadCount = NumThreads > 0 ? FMath::Min(NumThreads, MAX_WORKER_THREADS) : HardwareThreads;
    
    // Initialize per-thread worker data
    WorkerData.SetNum(ActualThreadCount);
    
    for (int32 i = 0; i < ActualThreadCount; ++i)
    {
        FWorkerData& Data = WorkerData[i];
        Data.LocalTaskQueue = MakeShared<FLocalTaskQueue>(1024);
        Data.WorkerIndex = i;
        Data.LastActiveTime = FPlatformTime::Seconds();
        Data.TasksProcessed = 0;
        Data.TasksStolen = 0;
        Data.StealAttempts = 0;
        Data.State = EWorkerState::Idle;
        
        // Calculate thread affinity mask if enabled
        uint64 AffinityMask = 0;
        
        if (bUseThreadAffinity)
        {
            // Simple round-robin assignment of threads to cores
            int32 CoreIndex = i % HardwareThreads;
            AffinityMask = 1ULL << CoreIndex;
        }
        
        // Create events for thread synchronization
        Data.ThreadStartedEvent = MakeShared<FEvent>();
        Data.ThreadStartedEvent->Create(false);
        
        Data.ThreadCompletedEvent = MakeShared<FEvent>();
        Data.ThreadCompletedEvent->Create(false);
        
        // Setup thread parameters
        Data.ThreadParams.Scheduler = this;
        Data.ThreadParams.WorkerIndex = i;
        Data.ThreadParams.ThreadAffinityMask = AffinityMask;
        Data.ThreadParams.ThreadPriority = ThreadPriority;
        Data.ThreadParams.ThreadStartedEvent = Data.ThreadStartedEvent;
        Data.ThreadParams.ThreadCompletedEvent = Data.ThreadCompletedEvent;
        
        // Create and start the worker thread
        Data.Thread = FRunnableThread::Create(
            new FWorkerRunnable(Data.ThreadParams),
            WorkerNames[i % MAX_WORKER_THREADS],
            0,
            ThreadPriority,
            AffinityMask,
            FPlatformAffinity::GetNoAffinityMask()
        );
        
        // Wait for the thread to start
        Data.ThreadStartedEvent->Wait();
    }
    
    // Initialize profiling data
    if (bEnableThreadProfiling)
    {
        WorkerProfilingData.SetNum(ActualThreadCount);
        
        for (FWorkerProfilingData& ProfileData : WorkerProfilingData)
        {
            ProfileData.LastSampleTime = FPlatformTime::Seconds();
            ProfileData.ExecutionTimeMs = 0.0;
            ProfileData.WaitTimeMs = 0.0;
            ProfileData.StealTimeMs = 0.0;
            ProfileData.TaskCount = 0;
            ProfileData.TaskTypes.Empty();
        }
        
        // Start profiling timer
        ProfilingUpdateInterval = 5.0; // Update every 5 seconds
        LastProfilingUpdateTime = FPlatformTime::Seconds();
    }
    
    bIsInitialized = true;
}

void FTaskScheduler::Shutdown()
{
    FScopeLock Lock(&InitLock);
    
    if (!bIsInitialized || bIsShuttingDown)
    {
        return;
    }
    
    // Mark as shutting down
    bIsShuttingDown = true;
    
    // Signal shutdown event
    ShutdownEvent->Trigger();
    
    // Wait for all worker threads to complete
    for (FWorkerData& Data : WorkerData)
    {
        if (Data.Thread != nullptr)
        {
            Data.ThreadCompletedEvent->Wait();
            delete Data.Thread;
            Data.Thread = nullptr;
        }
    }
    
    // Clear task queues
    for (TSharedPtr<FPriorityTaskQueue> Queue : TaskQueues)
    {
        Queue.Reset();
    }
    
    TaskQueues.Empty();
    WorkerData.Empty();
    
    bIsInitialized = false;
    bIsShuttingDown = false;
}

bool FTaskScheduler::IsShuttingDown() const
{
    return bIsShuttingDown;
}

bool FTaskScheduler::ShouldUseThreadAffinity() const
{
    return bUseThreadAffinity;
}

int32 FTaskScheduler::GetCurrentThreadWorkerIndex()
{
    int32 Index = (int32)(UPTRINT)FPlatformTLS::GetTlsValue(WorkerTLS);
    return Index > 0 ? Index - 1 : -1;
}

bool FTaskScheduler::IsWorkerThread() const
{
    return GetCurrentThreadWorkerIndex() != -1;
}

int32 FTaskScheduler::GetWorkerThreadCount() const
{
    return ActualThreadCount;
}

bool FTaskScheduler::ScheduleTask(const FQueuedTask& Task, EPriority Priority)
{
    SCOPE_CYCLE_COUNTER(STAT_TaskScheduler_Schedule);
    CSV_SCOPED_TIMING_STAT(Threading, TaskScheduler_Schedule);
    
    if (!bIsInitialized || bIsShuttingDown)
    {
        UE_LOG(LogTemp, Warning, TEXT("Task scheduler is not initialized or is shutting down"));
        return false;
    }
    
    // Validate priority
    int32 PriorityIndex = static_cast<int32>(Priority);
    if (PriorityIndex < 0 || PriorityIndex >= TaskQueues.Num())
    {
        PriorityIndex = static_cast<int32>(EPriority::Normal);
    }
    
    // Get the current worker index
    int32 WorkerIndex = GetCurrentThreadWorkerIndex();
    
    // If we're on a worker thread, try to add the task to the local queue first
    if (WorkerIndex >= 0 && WorkerIndex < WorkerData.Num())
    {
        FWorkerData& Data = WorkerData[WorkerIndex];
        
        // For high priority tasks, add them to the global queue to ensure quick processing
        if (Priority == EPriority::Critical || Priority == EPriority::High)
        {
            // Add to global queue
            bool bSuccess = TaskQueues[PriorityIndex]->Enqueue(Task, Priority);
            
            if (bSuccess)
            {
                PerformanceMetrics.TotalTasksScheduled++;
                
                // Notify idle workers that there's a high priority task available
                for (FWorkerData& IdleData : WorkerData)
                {
                    if (IdleData.State == EWorkerState::Idle)
                    {
                        IdleData.SignalEvent->Trigger();
                    }
                }
            }
            
            return bSuccess;
        }
        else
        {
            // Try to add to local queue first
            if (Data.LocalTaskQueue->Enqueue(Task))
            {
                PerformanceMetrics.TotalTasksScheduled++;
                return true;
            }
            
            // If local queue is full, fall back to global queue
            bool bSuccess = TaskQueues[PriorityIndex]->Enqueue(Task, Priority);
            
            if (bSuccess)
            {
                PerformanceMetrics.TotalTasksScheduled++;
            }
            
            return bSuccess;
        }
    }
    else
    {
        // Not on a worker thread, add to global queue
        bool bSuccess = TaskQueues[PriorityIndex]->Enqueue(Task, Priority);
        
        if (bSuccess)
        {
            PerformanceMetrics.TotalTasksScheduled++;
        }
        
        return bSuccess;
    }
}

bool FTaskScheduler::ScheduleBatch(const TArray<FQueuedTask>& Tasks, EPriority Priority)
{
    if (!bIsInitialized || bIsShuttingDown || Tasks.Num() == 0)
    {
        return false;
    }
    
    // Validate priority
    int32 PriorityIndex = static_cast<int32>(Priority);
    if (PriorityIndex < 0 || PriorityIndex >= TaskQueues.Num())
    {
        PriorityIndex = static_cast<int32>(EPriority::Normal);
    }
    
    // Get the current worker index
    int32 WorkerIndex = GetCurrentThreadWorkerIndex();
    bool bSuccess = false;
    
    // If we're on a worker thread and the batch is small enough, try local queue first
    if (WorkerIndex >= 0 && WorkerIndex < WorkerData.Num() && 
        Tasks.Num() <= TasksBatchSize && 
        Priority != EPriority::Critical && Priority != EPriority::High)
    {
        FWorkerData& Data = WorkerData[WorkerIndex];
        
        // Try to add to local queue first
        bool bLocalSuccess = true;
        for (const FQueuedTask& Task : Tasks)
        {
            if (!Data.LocalTaskQueue->Enqueue(Task))
            {
                bLocalSuccess = false;
                break;
            }
        }
        
        if (bLocalSuccess)
        {
            PerformanceMetrics.TotalTasksScheduled += Tasks.Num();
            return true;
        }
    }
    
    // Add to global queue
    bSuccess = TaskQueues[PriorityIndex]->EnqueueBatch(Tasks, Priority);
    
    if (bSuccess)
    {
        PerformanceMetrics.TotalTasksScheduled += Tasks.Num();
    }
    
    return bSuccess;
}

void FTaskScheduler::ExecuteWorkerLoop(int32 WorkerIndex)
{
    if (WorkerIndex < 0 || WorkerIndex >= WorkerData.Num())
    {
        UE_LOG(LogTemp, Error, TEXT("Invalid worker index: %d"), WorkerIndex);
        return;
    }
    
    FWorkerData& Data = WorkerData[WorkerIndex];
    
    while (!bIsShuttingDown)
    {
        FQueuedTask Task;
        bool bGotTask = false;
        
        // Update worker state
        double StartLoopTime = FPlatformTime::Seconds();
        Data.State = EWorkerState::Searching;
        
        // Try to get a task from local queue first
        if (Data.LocalTaskQueue->Dequeue(Task))
        {
            bGotTask = true;
        }
        
        // If no local task, try global queues in priority order
        if (!bGotTask)
        {
            for (int32 PriorityIndex = 0; PriorityIndex < TaskQueues.Num() && !bGotTask; ++PriorityIndex)
            {
                if (TaskQueues[PriorityIndex]->Dequeue(Task, 0))
                {
                    bGotTask = true;
                }
            }
        }
        
        // If still no task, try work stealing
        if (!bGotTask && StealingStrategy != EStealingStrategy::None)
        {
            Data.State = EWorkerState::Stealing;
            double StealStartTime = FPlatformTime::Seconds();
            bGotTask = TryStealTask(WorkerIndex, Task);
            
            if (bEnableThreadProfiling && bGotTask)
            {
                WorkerProfilingData[WorkerIndex].StealTimeMs += (FPlatformTime::Seconds() - StealStartTime) * 1000.0;
            }
        }
        
        // If we got a task, execute it
        if (bGotTask)
        {
            SCOPE_CYCLE_COUNTER(STAT_TaskScheduler_Execute);
            CSV_SCOPED_TIMING_STAT(Threading, TaskScheduler_Execute);
            
            Data.State = EWorkerState::Working;
            double ExecutionStartTime = FPlatformTime::Seconds();
            
            // Execute the task
            Task.TaskFunction(Task.TaskData);
            
            // Update metrics
            Data.TasksProcessed++;
            Data.LastActiveTime = FPlatformTime::Seconds();
            PerformanceMetrics.TotalTasksProcessed++;
            
            // Update profiling data
            if (bEnableThreadProfiling)
            {
                FWorkerProfilingData& ProfileData = WorkerProfilingData[WorkerIndex];
                ProfileData.ExecutionTimeMs += (Data.LastActiveTime - ExecutionStartTime) * 1000.0;
                ProfileData.TaskCount++;
                
                // Track task type distribution
                const FName& TaskType = Task.TaskType;
                if (!TaskType.IsNone())
                {
                    if (ProfileData.TaskTypes.Contains(TaskType))
                    {
                        ProfileData.TaskTypes[TaskType]++;
                    }
                    else
                    {
                        ProfileData.TaskTypes.Add(TaskType, 1);
                    }
                }
            }
        }
        else
        {
            // No task found, go to sleep for a bit
            Data.State = EWorkerState::Idle;
            
            // Update idle time profiling
            if (bEnableThreadProfiling)
            {
                double IdleStartTime = FPlatformTime::Seconds();
                ShutdownEvent->Wait(IdleWaitTime);
                WorkerProfilingData[WorkerIndex].WaitTimeMs += (FPlatformTime::Seconds() - IdleStartTime) * 1000.0;
            }
            else
            {
                ShutdownEvent->Wait(IdleWaitTime);
            }
        }
        
        // Periodically rebalance work across threads
        MaybeRebalanceWork(WorkerIndex);
        
        // Periodically update profiling data
        if (bEnableThreadProfiling)
        {
            UpdateProfilingData();
        }
    }
}

bool FTaskScheduler::TryStealTask(int32 WorkerIndex, FQueuedTask& OutTask)
{
    if (WorkerIndex < 0 || WorkerIndex >= WorkerData.Num())
    {
        return false;
    }
    
    FWorkerData& Data = WorkerData[WorkerIndex];
    Data.StealAttempts++;
    PerformanceMetrics.TotalTaskStealAttempts++;
    
    // Adaptive stealing strategy based on system load
    static int32 ConsecutiveFailedSteals = 0;
    static double LastSuccessfulStealTime = FPlatformTime::Seconds();
    double CurrentTime = FPlatformTime::Seconds();
    
    // If we've had many failed steal attempts recently, try a different strategy
    EStealingStrategy CurrentStrategy = StealingStrategy;
    if (ConsecutiveFailedSteals > ActualThreadCount * 2)
    {
        // Switch temporarily to a different strategy if the current one is failing
        if (CurrentTime - LastSuccessfulStealTime > 1.0) // More than 1 second since last successful steal
        {
            // Cycle through strategies
            switch (StealingStrategy)
            {
                case EStealingStrategy::Random:
                    CurrentStrategy = EStealingStrategy::MostQueued;
                    break;
                case EStealingStrategy::LeastRecent:
                    CurrentStrategy = EStealingStrategy::Random;
                    break;
                case EStealingStrategy::MostQueued:
                    CurrentStrategy = EStealingStrategy::LeastRecent;
                    break;
                default:
                    CurrentStrategy = EStealingStrategy::Random;
                    break;
            }
        }
    }
    
    // Try actual stealing based on current strategy
    bool bStoleTask = false;
    switch (CurrentStrategy)
    {
        case EStealingStrategy::Random:
            bStoleTask = TryStealTaskRandom(WorkerIndex, OutTask);
            break;
            
        case EStealingStrategy::LeastRecent:
            bStoleTask = TryStealTaskLeastRecent(WorkerIndex, OutTask);
            break;
            
        case EStealingStrategy::MostQueued:
            bStoleTask = TryStealTaskMostQueued(WorkerIndex, OutTask);
            break;
            
        default: // None or unknown strategy
            return false;
    }
    
    // Update consecutive steals counter
    if (bStoleTask)
    {
        ConsecutiveFailedSteals = 0;
        LastSuccessfulStealTime = CurrentTime;
        
        // Track analytics
        WorkerData[WorkerIndex].TasksStolen++;
        PerformanceMetrics.SuccessfulTaskSteals++;
    }
    else
    {
        ConsecutiveFailedSteals++;
    }
    
    return bStoleTask;
}

bool FTaskScheduler::TryStealTaskRandom(int32 WorkerIndex, FQueuedTask& OutTask)
{
    // Generate a random victim worker index
    int32 VictimIndex = FMath::RandRange(0, ActualThreadCount - 1);
    
    // Don't steal from ourselves
    if (VictimIndex == WorkerIndex)
    {
        VictimIndex = (VictimIndex + 1) % ActualThreadCount;
    }
    
    // Try to steal a task from the victim
    FWorkerData& VictimData = WorkerData[VictimIndex];
    
    if (VictimData.LocalTaskQueue->GetCount() > WorkStealingThreshold)
    {
        if (VictimData.LocalTaskQueue->Dequeue(OutTask))
        {
            // Successfully stolen a task
            WorkerData[WorkerIndex].TasksStolen++;
            PerformanceMetrics.SuccessfulTaskSteals++;
            return true;
        }
    }
    
    return false;
}

bool FTaskScheduler::TryStealTaskLeastRecent(int32 WorkerIndex, FQueuedTask& OutTask)
{
    int32 LeastRecentIndex = -1;
    double LeastRecentTime = FPlatformTime::Seconds();
    
    // Find the thread that's been inactive the longest
    for (int32 i = 0; i < ActualThreadCount; ++i)
    {
        if (i != WorkerIndex && WorkerData[i].LastActiveTime < LeastRecentTime &&
            WorkerData[i].LocalTaskQueue->GetCount() > WorkStealingThreshold)
        {
            LeastRecentIndex = i;
            LeastRecentTime = WorkerData[i].LastActiveTime;
        }
    }
    
    // If we found a victim, try to steal a task
    if (LeastRecentIndex >= 0)
    {
        FWorkerData& VictimData = WorkerData[LeastRecentIndex];
        
        if (VictimData.LocalTaskQueue->Dequeue(OutTask))
        {
            // Successfully stolen a task
            WorkerData[WorkerIndex].TasksStolen++;
            PerformanceMetrics.SuccessfulTaskSteals++;
            return true;
        }
    }
    
    return false;
}

bool FTaskScheduler::TryStealTaskMostQueued(int32 WorkerIndex, FQueuedTask& OutTask)
{
    int32 MostQueuedIndex = -1;
    int32 MaxQueueSize = WorkStealingThreshold; // Only steal if at least this many queued tasks
    
    // Find the thread with the most queued tasks
    for (int32 i = 0; i < ActualThreadCount; ++i)
    {
        if (i != WorkerIndex)
        {
            int32 QueuedCount = WorkerData[i].LocalTaskQueue->GetCount();
            
            if (QueuedCount > MaxQueueSize)
            {
                MostQueuedIndex = i;
                MaxQueueSize = QueuedCount;
            }
        }
    }
    
    // If we found a victim, try to steal a task
    if (MostQueuedIndex >= 0)
    {
        FWorkerData& VictimData = WorkerData[MostQueuedIndex];
        
        if (VictimData.LocalTaskQueue->Dequeue(OutTask))
        {
            // Successfully stolen a task
            WorkerData[WorkerIndex].TasksStolen++;
            PerformanceMetrics.SuccessfulTaskSteals++;
            return true;
        }
    }
    
    return false;
}

void FTaskScheduler::MaybeRebalanceWork(int32 WorkerIndex)
{
    double CurrentTime = FPlatformTime::Seconds();
    
    // Rebalance occasionally (not every loop)
    if (CurrentTime - LastRebalanceTime > RebalanceInterval)
    {
        // Use atomic flag to ensure only one thread does the rebalancing
        static std::atomic<bool> IsRebalancing(false);
        bool Expected = false;
        
        if (IsRebalancing.compare_exchange_strong(Expected, true))
        {
            SCOPE_CYCLE_COUNTER(STAT_TaskScheduler_Rebalance);
            CSV_SCOPED_TIMING_STAT(Threading, TaskScheduler_Rebalance);
            
            // Find workers with too many tasks
            TArray<int32> OverloadedWorkers;
            TArray<int32> UnderloadedWorkers;
            TArray<FWorkerLoadInfo> WorkerLoadInfo;
            WorkerLoadInfo.SetNum(ActualThreadCount);
            
            // Calculate average task count and collect worker information
            int32 TotalLocalTasks = 0;
            int32 TotalActiveWorkers = 0;
            
            for (int32 i = 0; i < ActualThreadCount; ++i)
            {
                int32 QueueSize = WorkerData[i].LocalTaskQueue->GetCount();
                TotalLocalTasks += QueueSize;
                
                WorkerLoadInfo[i].WorkerIndex = i;
                WorkerLoadInfo[i].QueueSize = QueueSize;
                WorkerLoadInfo[i].IsActive = (WorkerData[i].State == EWorkerState::Working);
                WorkerLoadInfo[i].LastActiveTime = WorkerData[i].LastActiveTime;
                
                if (WorkerLoadInfo[i].IsActive)
                {
                    TotalActiveWorkers++;
                }
            }
            
            // Calculate thresholds based on active workers and total tasks
            float AverageTaskCount = TotalActiveWorkers > 0 ? 
                static_cast<float>(TotalLocalTasks) / TotalActiveWorkers : 
                static_cast<float>(TotalLocalTasks) / ActualThreadCount;
                
            float HighThreshold = FMath::Max(AverageTaskCount * 1.5f, 5.0f); // At least 5 tasks to be considered overloaded
            float LowThreshold = FMath::Max(AverageTaskCount * 0.5f, 1.0f);  // Consider underloaded if below half average
            
            // Sort workers by queue size (descending)
            WorkerLoadInfo.Sort([](const FWorkerLoadInfo& A, const FWorkerLoadInfo& B) {
                return A.QueueSize > B.QueueSize;
            });
            
            // Classify workers as overloaded or underloaded
            for (const FWorkerLoadInfo& Info : WorkerLoadInfo)
            {
                if (Info.QueueSize > HighThreshold)
                {
                    OverloadedWorkers.Add(Info.WorkerIndex);
                }
                else if (Info.QueueSize < LowThreshold || Info.QueueSize == 0)
                {
                    // Prioritize idle workers or those with empty queues
                    UnderloadedWorkers.Add(Info.WorkerIndex);
                }
            }
            
            // Perform load balancing - transfer tasks from overloaded to underloaded workers
            if (OverloadedWorkers.Num() > 0 && UnderloadedWorkers.Num() > 0)
            {
                int32 OverloadedIndex = 0;
                int32 UnderloadedIndex = 0;
                
                while (OverloadedIndex < OverloadedWorkers.Num() && UnderloadedIndex < UnderloadedWorkers.Num())
                {
                    int32 OverIdx = OverloadedWorkers[OverloadedIndex];
                    int32 UnderIdx = UnderloadedWorkers[UnderloadedIndex];
                    
                    FLocalTaskQueue* SrcQueue = WorkerData[OverIdx].LocalTaskQueue.Get();
                    FLocalTaskQueue* DstQueue = WorkerData[UnderIdx].LocalTaskQueue.Get();
                    
                    int32 SrcCount = SrcQueue->GetCount();
                    int32 DstCount = DstQueue->GetCount();
                    
                    // Calculate target distribution - make both workers have roughly equal load
                    int32 TargetCount = (SrcCount + DstCount) / 2;
                    int32 TasksToMove = FMath::Max(0, SrcCount - TargetCount);
                    
                    // Limit batch size to avoid moving too many at once
                    TasksToMove = FMath::Min(TasksToMove, FMath::Max(5, SrcCount / 4)); // At most 25% of tasks
                    
                    int32 TasksMoved = 0;
                    
                    // Move tasks from overloaded to underloaded worker
                    for (int32 i = 0; i < TasksToMove; ++i)
                    {
                        FQueuedTask Task;
                        if (SrcQueue->Dequeue(Task))
                        {
                            if (DstQueue->Enqueue(Task))
                            {
                                TasksMoved++;
                            }
                            else
                            {
                                // If destination queue is full, put it back
                                SrcQueue->Enqueue(Task);
                                break;
                            }
                        }
                        else
                        {
                            // Source queue is empty
                            break;
                        }
                    }
                    
                    // Wake up underloaded worker if it's idle and received tasks
                    if (TasksMoved > 0 && WorkerData[UnderIdx].State == EWorkerState::Idle)
                    {
                        WorkerData[UnderIdx].SignalEvent->Trigger();
                    }
                    
                    // Update analytics
                    if (TasksMoved > 0)
                    {
                        // Update queue counts
                        SrcCount = SrcQueue->GetCount();
                        DstCount = DstQueue->GetCount();
                        
                        // Log for debugging if significant rebalancing occurred
                        if (TasksMoved >= 5 || TasksMoved >= SrcCount / 4)
                        {
                            UE_LOG(LogTemp, Verbose, TEXT("Rebalanced %d tasks from Worker %d (%d remaining) to Worker %d (%d now queued)"),
                                TasksMoved, OverIdx, SrcCount, UnderIdx, DstCount);
                        }
                    }
                    
                    // Move to next underloaded worker if this one is now sufficiently loaded
                    // or if we couldn't move any more tasks
                    if (DstCount >= LowThreshold || TasksMoved == 0)
                    {
                        UnderloadedIndex++;
                    }
                    
                    // Move to next overloaded worker if this one is now sufficiently balanced
                    // or if we couldn't move any more tasks
                    if (SrcCount <= HighThreshold || TasksMoved == 0)
                    {
                        OverloadedIndex++;
                    }
                }
            }
            
            LastRebalanceTime = CurrentTime;
            IsRebalancing = false;
        }
    }
}

void FTaskScheduler::UpdateProfilingData()
{
    if (!bEnableThreadProfiling)
    {
        return;
    }
    
    double CurrentTime = FPlatformTime::Seconds();
    
    // Update every few seconds
    if (CurrentTime - LastProfilingUpdateTime > ProfilingUpdateInterval)
    {
        // Calculate worker utilization
        for (int32 i = 0; i < ActualThreadCount; ++i)
        {
            FWorkerProfilingData& ProfileData = WorkerProfilingData[i];
            
            double TotalTimeMs = ProfileData.ExecutionTimeMs + ProfileData.WaitTimeMs + ProfileData.StealTimeMs;
            
            if (TotalTimeMs > 0.0)
            {
                // Calculate utilization percentage
                float Utilization = static_cast<float>(ProfileData.ExecutionTimeMs / TotalTimeMs);
                PerformanceMetrics.WorkerUtilization[i] = Utilization;
            }
            
            // Reset counters for next interval
            ProfileData.ExecutionTimeMs = 0.0;
            ProfileData.WaitTimeMs = 0.0;
            ProfileData.StealTimeMs = 0.0;
            ProfileData.TaskCount = 0;
        }
        
        LastProfilingUpdateTime = CurrentTime;
    }
}

FTaskSchedulerMetrics FTaskScheduler::GetPerformanceMetrics() const
{
    return PerformanceMetrics;
}

TArray<FWorkerThreadStatus> FTaskScheduler::GetWorkerThreadStatus() const
{
    TArray<FWorkerThreadStatus> Status;
    Status.SetNum(ActualThreadCount);
    
    for (int32 i = 0; i < ActualThreadCount; ++i)
    {
        const FWorkerData& Data = WorkerData[i];
        
        Status[i].WorkerIndex = i;
        Status[i].QueuedTaskCount = Data.LocalTaskQueue->GetCount();
        Status[i].TasksProcessed = Data.TasksProcessed;
        Status[i].TasksStolen = Data.TasksStolen;
        Status[i].State = Data.State;
        
        if (bEnableThreadProfiling && i < PerformanceMetrics.WorkerUtilization.Num())
        {
            Status[i].Utilization = PerformanceMetrics.WorkerUtilization[i];
        }
        else
        {
            Status[i].Utilization = 0.0f;
        }
        
        if (bEnableThreadProfiling && i < WorkerProfilingData.Num())
        {
            Status[i].TaskTypeDistribution = WorkerProfilingData[i].TaskTypes;
        }
    }
    
    return Status;
}