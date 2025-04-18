// Copyright Epic Games, Inc. All Rights Reserved.

#include "TaskScheduler.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformAffinity.h"
#include "Misc/OutputDeviceRedirector.h"
#include "HAL/ThreadHeartBeat.h"
#include "HAL/PlatformCrt.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Thread.h"
#include "Misc/ScopeLock.h"
#include "Misc/Timespan.h"
#include "Async/TaskGraphInterfaces.h"

// Include Windows-specific headers
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

// Initialize static member variables
FTaskScheduler* FTaskScheduler::Instance = nullptr;
uint32 FTaskScheduler::WorkerThreadTLS = FPlatformTLS::AllocTlsSlot();

//----------------------------------------------------------------------
// FMiningTask Implementation
//----------------------------------------------------------------------

FMiningTask::FMiningTask(uint64 InId, TFunction<void()> InTaskFunction, const FTaskConfig& InConfig, const FString& InDesc)
    : Id(InId)
    , TaskFunction(InTaskFunction)
    , Config(InConfig)
    , Description(InDesc)
    , CreationTime(FPlatformTime::Seconds())
    , StartTime(0.0)
    , CompletionTime(0.0)
    , ExecutingThreadId(INDEX_NONE)
{
    // Initialize stats
    Stats.QueueTimeMs = 0.0;
    Stats.ExecutionTimeMs = 0.0;
    Stats.RetryCount = 0;
    Stats.PeakMemoryBytes = 0;
    Stats.ExecutingThreadId = 0;
    Stats.ExecutingCore = INDEX_NONE;
    
    // Initialize counters
    Progress.Set(0);
    Status.Set(static_cast<int32>(ETaskStatus::Queued));
    AttemptCount.Set(0);
    
    // Copy dependencies
    Dependencies = Config.Dependencies;
}

void FMiningTask::SetProgress(int32 InProgress)
{
    check(InProgress >= 0 && InProgress <= 100);
    Progress.Set(InProgress);
}

int32 FMiningTask::GetProgress() const
{
    return Progress.GetValue();
}

void FMiningTask::SetStatus(ETaskStatus InStatus)
{
    Status.Set(static_cast<int32>(InStatus));
}

ETaskStatus FMiningTask::GetStatus() const
{
    return static_cast<ETaskStatus>(Status.GetValue());
}

int32 FMiningTask::IncrementAttempt()
{
    return AttemptCount.Increment();
}

int32 FMiningTask::GetAttemptCount() const
{
    return AttemptCount.GetValue();
}

void FMiningTask::Execute()
{
    // Record start time
    StartTime = FPlatformTime::Seconds();
    
    // Calculate time spent in queue
    Stats.QueueTimeMs = (StartTime - CreationTime) * 1000.0;
    
    // Update status
    SetStatus(ETaskStatus::Executing);
    
    // Record thread details
    ExecutingThreadId = FPlatformTLS::GetCurrentThreadId();
    Stats.ExecutingThreadId = ExecutingThreadId;
    Stats.ExecutingCore = FPlatformTLS::GetCurrentThreadId() % FPlatformMisc::NumberOfCores();
    
    try
    {
        // Execute the task
        if (TaskFunction)
        {
            // Allow heartbeat checks to prevent thread timeout detection
            FThreadHeartBeat::Get().HeartBeat();
            
            // Execute the function
            TaskFunction();
            
            // Allow heartbeat checks again after execution
            FThreadHeartBeat::Get().HeartBeat();
            
            // Mark successful completion
            Complete(true);
        }
        else
        {
            // Task function is null, mark as failed
            Complete(false);
        }
    }
    catch (const std::exception& Exception)
    {
        // Log the exception
        UE_LOG(LogTemp, Error, TEXT("Task %llu (%s) threw an exception: %s"), 
            Id, *Description, ANSI_TO_TCHAR(Exception.what()));
        
        // Mark as failed
        Complete(false);
    }
    catch (...)
    {
        // Log the unknown exception
        UE_LOG(LogTemp, Error, TEXT("Task %llu (%s) threw an unknown exception"), 
            Id, *Description);
        
        // Mark as failed
        Complete(false);
    }
}

void FMiningTask::Complete(bool bSuccess)
{
    // Record completion time
    CompletionTime = FPlatformTime::Seconds();
    
    // Calculate execution time
    Stats.ExecutionTimeMs = (CompletionTime - StartTime) * 1000.0;
    
    // Update status
    SetStatus(bSuccess ? ETaskStatus::Completed : ETaskStatus::Failed);
    
    // Update retry count
    Stats.RetryCount = GetAttemptCount();
    
    // Call completion callback if set
    if (CompletionCallback)
    {
        CompletionCallback(bSuccess);
    }
}

bool FMiningTask::HasTimedOut() const
{
    if (Config.MaxExecutionTimeMs == 0 || GetStatus() != ETaskStatus::Executing)
    {
        return false;
    }
    
    double CurrentTime = FPlatformTime::Seconds();
    double ElapsedTimeMs = (CurrentTime - StartTime) * 1000.0;
    
    return ElapsedTimeMs > Config.MaxExecutionTimeMs;
}

bool FMiningTask::AreDependenciesSatisfied(const TMap<uint64, FMiningTask*>& TaskMap) const
{
    for (const FTaskDependency& Dependency : Dependencies)
    {
        FMiningTask* DependentTask = TaskMap.FindRef(Dependency.TaskId);
        
        // If the dependent task doesn't exist, ignore it
        if (!DependentTask)
        {
            continue;
        }
        
        // Check if the dependency is satisfied
        ETaskStatus DependencyStatus = DependentTask->GetStatus();
        bool bIsComplete = (DependencyStatus == ETaskStatus::Completed);
        
        // If the dependency is required and not complete, return false
        if (Dependency.bRequired && !bIsComplete)
        {
            return false;
        }
        
        // Check if the dependency has timed out
        if (Dependency.TimeoutMs > 0)
        {
            double CurrentTime = FPlatformTime::Seconds();
            double DependencyTime = DependentTask->StartTime;
            
            // If the dependency hasn't started yet, use creation time
            if (DependencyTime == 0.0)
            {
                DependencyTime = DependentTask->CreationTime;
            }
            
            double ElapsedTimeMs = (CurrentTime - DependencyTime) * 1000.0;
            
            // If the dependency has timed out, consider it satisfied
            if (ElapsedTimeMs > Dependency.TimeoutMs)
            {
                continue;
            }
            
            // If the dependency hasn't timed out and isn't complete, return false
            if (Dependency.bRequired && !bIsComplete)
            {
                return false;
            }
        }
    }
    
    return true;
}

//----------------------------------------------------------------------
// FMiningTaskWorker Implementation
//----------------------------------------------------------------------

FMiningTaskWorker::FMiningTaskWorker(class FTaskScheduler* InScheduler, int32 InThreadId, EThreadPriority InPriority)
    : Scheduler(InScheduler)
    , ThreadId(InThreadId)
    , Priority(InPriority)
    , AffinityMask(0)
    , LastIdleTime(FPlatformTime::Seconds())
    , LastStatsResetTime(FPlatformTime::Seconds())
{
    bRunning.Set(1);
    TasksProcessed.Set(0);
    CurrentTaskId.Set(0);
    IdleTimeMs.Set(0);
    ProcessingTimeMs.Set(0);
    StatsTaskCount.Set(0);
    StatsTaskTimeMs.Set(0);
}

FMiningTaskWorker::~FMiningTaskWorker()
{
    Stop();
    
    if (Thread)
    {
        Thread->WaitForCompletion();
        delete Thread;
        Thread = nullptr;
    }
}

bool FMiningTaskWorker::Init()
{
    // Store the worker thread ID in thread local storage
    FPlatformTLS::SetTlsValue(FTaskScheduler::WorkerThreadTLS, (void*)(UPTRINT)(ThreadId + 1));
    
    // Set thread affinity if specified
    if (AffinityMask != 0)
    {
        SetAffinity(AffinityMask);
    }
    
    return true;
}

uint32 FMiningTaskWorker::Run()
{
    // Worker thread main loop
    while (bRunning.GetValue() > 0)
    {
        double LoopStartTime = FPlatformTime::Seconds();
        
        // Try to get a task from the scheduler
        FMiningTask* Task = Scheduler->GetNextTask(ThreadId);
        
        if (Task)
        {
            // Record the current task ID
            CurrentTaskId.Set(Task->Id);
            
            // Execute the task
            Task->Execute();
            
            // Increment task counter
            TasksProcessed.Increment();
            StatsTaskCount.Increment();
            
            // Record execution time for stats
            double TaskEndTime = FPlatformTime::Seconds();
            double TaskTimeMs = (TaskEndTime - LoopStartTime) * 1000.0;
            StatsTaskTimeMs.Add(static_cast<int32>(TaskTimeMs));
            ProcessingTimeMs.Add(static_cast<int32>(TaskTimeMs));
            
            // Clear current task ID
            CurrentTaskId.Set(0);
        }
        else
        {
            // No task available, sleep for a short time
            FPlatformProcess::Sleep(0.001f);
            
            // Record idle time
            double CurrentTime = FPlatformTime::Seconds();
            double IdleTime = CurrentTime - LoopStartTime;
            IdleTimeMs.Add(static_cast<int32>(IdleTime * 1000.0));
            LastIdleTime = CurrentTime;
        }
        
        // Reset stats periodically
        double CurrentTime = FPlatformTime::Seconds();
        if (CurrentTime - LastStatsResetTime > 60.0) // Reset every minute
        {
            StatsTaskCount.Set(0);
            StatsTaskTimeMs.Set(0);
            LastStatsResetTime = CurrentTime;
        }
    }
    
    return 0;
}

void FMiningTaskWorker::Stop()
{
    bRunning.Set(0);
}

void FMiningTaskWorker::Exit()
{
    // Clear our thread local storage
    FPlatformTLS::SetTlsValue(FTaskScheduler::WorkerThreadTLS, nullptr);
}

int32 FMiningTaskWorker::GetThreadId() const
{
    return ThreadId;
}

bool FMiningTaskWorker::SetPriority(EThreadPriority InPriority)
{
    Priority = InPriority;
    
    // If thread exists, update priority
    if (Thread)
    {
        // Since FRunnableThread doesn't have a direct SetPriority method,
        // we need to use platform-specific methods or store it for when thread is created
        // For now, just store the priority for future thread creation
        return true;
    }
    
    return true;
}

bool FMiningTaskWorker::SetAffinity(uint64 CoreMask)
{
    AffinityMask = CoreMask;
    
    // If thread exists, update affinity
    if (Thread)
    {
        // For thread affinity, we need to use platform-specific code
        #if PLATFORM_WINDOWS
            // Use the generic implementation that works across platforms
            FPlatformProcess::SetThreadAffinityMask(CoreMask);
            return true;
        #else
            // For other platforms, we'll use a generic approach
            FPlatformProcess::SetThreadAffinityMask(CoreMask);
            return true;
        #endif
    }
    
    return true;
}

uint32 FMiningTaskWorker::GetTasksProcessed() const
{
    return TasksProcessed.GetValue();
}

uint64 FMiningTaskWorker::GetCurrentTaskId() const
{
    return CurrentTaskId.GetValue();
}

bool FMiningTaskWorker::IsIdle() const
{
    return CurrentTaskId.GetValue() == 0;
}

float FMiningTaskWorker::GetUtilization() const
{
    int32 TotalTime = ProcessingTimeMs.GetValue() + IdleTimeMs.GetValue();
    if (TotalTime <= 0)
    {
        return 0.0f;
    }
    
    return static_cast<float>(ProcessingTimeMs.GetValue()) / static_cast<float>(TotalTime);
}

void FMiningTaskWorker::GetStats(double& OutAverageTaskTimeMs, double& OutIdleTimePercent) const
{
    int32 TaskCount = StatsTaskCount.GetValue();
    int32 TaskTimeMs = StatsTaskTimeMs.GetValue();
    
    OutAverageTaskTimeMs = (TaskCount > 0) ? (static_cast<double>(TaskTimeMs) / TaskCount) : 0.0;
    
    int32 TotalTime = ProcessingTimeMs.GetValue() + IdleTimeMs.GetValue();
    OutIdleTimePercent = (TotalTime > 0) ? (static_cast<double>(IdleTimeMs.GetValue()) * 100.0 / TotalTime) : 0.0;
}

//----------------------------------------------------------------------
// FTaskScheduler Implementation
//----------------------------------------------------------------------

FTaskScheduler::FTaskScheduler()
    : bIsInitialized(false)
    , NumLogicalCores(0)
{
    // Set the singleton instance
    Instance = this;
    
    // Ensure task counts are initialized to zero
    TasksScheduled.Set(0);
    TasksCompleted.Set(0);
    TasksCancelled.Set(0);
    TasksFailed.Set(0);
    
    // Initialize task count by status
    TaskCountByStatus.Add(ETaskStatus::Queued, FThreadSafeCounter(0));
    TaskCountByStatus.Add(ETaskStatus::Executing, FThreadSafeCounter(0));
    TaskCountByStatus.Add(ETaskStatus::Completed, FThreadSafeCounter(0));
    TaskCountByStatus.Add(ETaskStatus::Cancelled, FThreadSafeCounter(0));
    TaskCountByStatus.Add(ETaskStatus::Failed, FThreadSafeCounter(0));
    
    // Initialize task queues for all priority levels
    TaskQueues.Add(ETaskPriority::Critical, TArray<FMiningTask*>());
    TaskQueues.Add(ETaskPriority::High, TArray<FMiningTask*>());
    TaskQueues.Add(ETaskPriority::Normal, TArray<FMiningTask*>());
    TaskQueues.Add(ETaskPriority::Low, TArray<FMiningTask*>());
    TaskQueues.Add(ETaskPriority::Background, TArray<FMiningTask*>());
    
    // Get the number of logical cores
    NumLogicalCores = FPlatformMisc::NumberOfCores();
}

FTaskScheduler::~FTaskScheduler()
{
    Shutdown();
    
    // Clear the singleton instance if it's us
    if (Instance == this)
    {
        Instance = nullptr;
    }
}

ITaskScheduler& FTaskScheduler::Get()
{
    check(Instance != nullptr);
    return *Instance;
}

bool FTaskScheduler::Initialize()
{
    FScopeLock Lock(&TaskQueueLock);
    
    if (bIsInitialized)
    {
        return true;
    }
    
    // Create worker threads
    int32 ThreadCount = DetermineWorkerThreadCount();
    CreateWorkerThreads(ThreadCount);
    
    bIsInitialized = true;
    return true;
}

void FTaskScheduler::Shutdown()
{
    FScopeLock Lock(&TaskQueueLock);
    
    if (!bIsInitialized)
    {
        return;
    }
    
    // Stop all worker threads
    for (FMiningTaskWorker* Worker : Workers)
    {
        Worker->Stop();
    }
    
    // Wait for threads to complete and clean up
    for (FMiningTaskWorker* Worker : Workers)
    {
        delete Worker;
    }
    
    Workers.Empty();
    
    // Clean up completed tasks
    CleanupCompletedTasks();
    
    // Clean up remaining tasks
    FScopeLock MapLock(&TaskMapLock);
    
    for (auto& TaskPair : AllTasks)
    {
        delete TaskPair.Value;
    }
    
    AllTasks.Empty();
    
    for (auto& QueuePair : TaskQueues)
    {
        QueuePair.Value.Empty();
    }
    
    bIsInitialized = false;
}

bool FTaskScheduler::IsInitialized() const
{
    return bIsInitialized;
}

uint64 FTaskScheduler::ScheduleTask(TFunction<void()> TaskFunc, const FTaskConfig& Config, const FString& Desc)
{
    return ScheduleTaskWithCallback(TaskFunc, TFunction<void(bool)>(), Config, Desc);
}

uint64 FTaskScheduler::ScheduleTaskWithCallback(TFunction<void()> TaskFunc, TFunction<void(bool)> OnComplete, 
    const FTaskConfig& Config, const FString& Desc)
{
    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Warning, TEXT("Task scheduler not initialized. Cannot schedule task: %s"), *Desc);
        return 0;
    }
    
    if (!TaskFunc)
    {
        UE_LOG(LogTemp, Warning, TEXT("Cannot schedule null task function: %s"), *Desc);
        return 0;
    }
    
    // Generate a unique task ID
    uint64 TaskId = GenerateTaskId();
    
    // Create the task
    FMiningTask* Task = new FMiningTask(TaskId, TaskFunc, Config, Desc);
    Task->CompletionCallback = OnComplete;
    
    // Add the task to the map
    {
        FScopeLock Lock(&TaskMapLock);
        AllTasks.Add(TaskId, Task);
    }
    
    // Add the task to the appropriate queue
    {
        FScopeLock Lock(&TaskQueueLock);
        TaskQueues[Config.Priority].Add(Task);
        
        // Update task counts
        TasksScheduled.Increment();
        TaskCountByStatus[ETaskStatus::Queued].Increment();
    }
    
    return TaskId;
}

bool FTaskScheduler::CancelTask(uint64 TaskId)
{
    FScopeLock MapLock(&TaskMapLock);
    
    FMiningTask* Task = GetTaskById(TaskId);
    if (!Task)
    {
        return false;
    }
    
    ETaskStatus Status = Task->GetStatus();
    
    // Can't cancel completed, cancelled, or failed tasks
    if (Status == ETaskStatus::Completed || Status == ETaskStatus::Cancelled || Status == ETaskStatus::Failed)
    {
        return false;
    }
    
    // If task isn't cancellable, return false
    if (!Task->Config.bCancellable)
    {
        return false;
    }
    
    // Update task status
    ETaskStatus OldStatus = Task->GetStatus();
    Task->SetStatus(ETaskStatus::Cancelled);
    
    // Update task counts
    TaskCountByStatus[OldStatus].Decrement();
    TaskCountByStatus[ETaskStatus::Cancelled].Increment();
    TasksCancelled.Increment();
    
    // Call completion callback with failure result
    if (Task->CompletionCallback)
    {
        Task->CompletionCallback(false);
    }
    
    return true;
}

ETaskStatus FTaskScheduler::GetTaskStatus(uint64 TaskId) const
{
    FScopeLock Lock(&TaskMapLock);
    
    FMiningTask* Task = GetTaskById(TaskId);
    if (!Task)
    {
        return ETaskStatus::Failed;
    }
    
    return Task->GetStatus();
}

FTaskStats FTaskScheduler::GetTaskStats(uint64 TaskId) const
{
    FScopeLock Lock(&TaskMapLock);
    
    FMiningTask* Task = GetTaskById(TaskId);
    if (!Task)
    {
        return FTaskStats();
    }
    
    return Task->Stats;
}

bool FTaskScheduler::GetTaskProgress(uint64 TaskId, float& OutProgress) const
{
    FScopeLock Lock(&TaskMapLock);
    
    FMiningTask* Task = GetTaskById(TaskId);
    if (!Task || !Task->Config.bSupportsProgress)
    {
        OutProgress = 0.0f;
        return false;
    }
    
    OutProgress = static_cast<float>(Task->GetProgress()) / 100.0f;
    return true;
}

bool FTaskScheduler::WaitForTask(uint64 TaskId, uint32 TimeoutMs)
{
    double StartTime = FPlatformTime::Seconds();
    double EndTime = (TimeoutMs > 0) ? (StartTime + TimeoutMs / 1000.0) : DBL_MAX;
    
    FMiningTask* Task = GetTaskById(TaskId);
    if (!Task)
    {
        return false;
    }
    
    while (true)
    {
        // Check if we've timed out
        if (FPlatformTime::Seconds() >= EndTime)
        {
            return false;
        }
        
        // Get the task status
        ETaskStatus Status = Task->GetStatus();
        
        // If the task is completed, cancelled, or failed, we're done
        if (Status == ETaskStatus::Completed)
        {
            return true;
        }
        if (Status == ETaskStatus::Cancelled || Status == ETaskStatus::Failed)
        {
            return false;
        }
        
        // Sleep a bit to avoid spinning
        FPlatformProcess::Sleep(0.001f);
    }
    
    return false;
}

bool FTaskScheduler::WaitForTasks(const TArray<uint64>& TaskIds, bool bWaitForAll, uint32 TimeoutMs)
{
    if (TaskIds.Num() == 0)
    {
        return true;
    }
    
    double StartTime = FPlatformTime::Seconds();
    double EndTime = (TimeoutMs > 0) ? (StartTime + TimeoutMs / 1000.0) : DBL_MAX;
    
    // Keep track of completed task ids
    TArray<uint64> CompletedIds;
    
    while (true)
    {
        // Check if we've timed out
        if (FPlatformTime::Seconds() >= EndTime)
        {
            return false;
        }
        
        // Check the status of each task
        bool bAllCompleted = true;
        bool bAnyCompleted = false;
        
        for (uint64 TaskId : TaskIds)
        {
            // Skip already completed tasks
            if (CompletedIds.Contains(TaskId))
            {
                bAnyCompleted = true;
                continue;
            }
            
            // Get the task status
            ETaskStatus Status = GetTaskStatus(TaskId);
            
            if (Status == ETaskStatus::Completed)
            {
                CompletedIds.Add(TaskId);
                bAnyCompleted = true;
            }
            else if (Status == ETaskStatus::Cancelled || Status == ETaskStatus::Failed)
            {
                // Failed tasks are considered "complete" but unsuccessful
                CompletedIds.Add(TaskId);
                bAllCompleted = false;
                bAnyCompleted = true;
            }
            else
            {
                bAllCompleted = false;
            }
        }
        
        // Check if we're done
        if ((bWaitForAll && bAllCompleted) || (!bWaitForAll && bAnyCompleted))
        {
            return bWaitForAll ? bAllCompleted : bAnyCompleted;
        }
        
        // Sleep a bit to avoid spinning
        FPlatformProcess::Sleep(0.001f);
    }
    
    return false;
}

uint32 FTaskScheduler::GetWorkerThreadCount() const
{
    return Workers.Num();
}

int32 FTaskScheduler::GetCurrentThreadId() const
{
    void* TlsValue = FPlatformTLS::GetTlsValue(WorkerThreadTLS);
    if (TlsValue == nullptr)
    {
        return INDEX_NONE;
    }
    
    return static_cast<int32>((UPTRINT)TlsValue) - 1;
}

bool FTaskScheduler::IsTaskThread() const
{
    return GetCurrentThreadId() != INDEX_NONE;
}

bool FTaskScheduler::SetThreadPriority(int32 ThreadId, EThreadPriority Priority)
{
    if (ThreadId < 0 || ThreadId >= Workers.Num())
    {
        return false;
    }
    
    return Workers[ThreadId]->SetPriority(Priority);
}

bool FTaskScheduler::SetThreadAffinity(int32 ThreadId, uint64 CoreMask)
{
    if (ThreadId < 0 || ThreadId >= Workers.Num())
    {
        return false;
    }
    
    return Workers[ThreadId]->SetAffinity(CoreMask);
}

TMap<ETaskStatus, int32> FTaskScheduler::GetTaskCounts() const
{
    TMap<ETaskStatus, int32> Result;
    
    for (auto& Pair : TaskCountByStatus)
    {
        Result.Add(Pair.Key, Pair.Value.GetValue());
    }
    
    return Result;
}

int32 FTaskScheduler::DetermineWorkerThreadCount() const
{
    // Use 75% of available cores (minimum 2, maximum 16)
    int32 CoreCount = FMath::Max(2, FMath::Min(16, FMath::CeilToInt(NumLogicalCores * 0.75f)));
    
    // Adjust based on hardware capabilities
    if (NumLogicalCores > 16)
    {
        // For high-core-count systems, use a lower percentage to avoid contention
        CoreCount = FMath::Max(CoreCount, FMath::CeilToInt(NumLogicalCores * 0.5f));
    }
    else if (NumLogicalCores <= 4)
    {
        // For low-core-count systems, ensure we have at least 2 threads
        CoreCount = FMath::Max(CoreCount, 2);
    }
    
    return CoreCount;
}

void FTaskScheduler::CreateWorkerThreads(int32 ThreadCount)
{
    if (ThreadCount <= 0)
    {
        ThreadCount = DetermineWorkerThreadCount();
    }
    
    // Create worker threads
    for (int32 i = 0; i < ThreadCount; ++i)
    {
        // Create with TPri_Normal instead of EThreadPriority::Normal
        FMiningTaskWorker* Worker = new FMiningTaskWorker(this, i, TPri_Normal);
        
        // Create the thread
        FString ThreadName = FString::Printf(TEXT("MiningTask%d"), i);
        
        // Use Worker's SetPriority method to configure the thread
        Worker->SetPriority(TPri_Normal);
        
        // Store the worker
        Workers.Add(Worker);
    }
    
    // Set affinity for threads if on a system with multiple NUMA nodes
    if (ThreadCount > 2 && NumLogicalCores > 4)
    {
        // NUMA awareness not implemented, using simple core distribution instead
        for (int32 i = 0; i < ThreadCount; ++i)
        {
            // Simple core distribution - no NUMA awareness
            uint64 CoreMask = (1ULL << (i % NumLogicalCores));
            Workers[i]->SetAffinity(CoreMask);
        }
    }
}

uint64 FTaskScheduler::GenerateTaskId()
{
    // Use a combination of counter and timestamp for unique IDs
    uint32 Counter = NextTaskId.Increment();
    uint64 Timestamp = static_cast<uint64>(FPlatformTime::Seconds() * 1000.0);
    
    return (Timestamp << 32) | Counter;
}

FMiningTask* FTaskScheduler::GetTaskById(uint64 TaskId) const
{
    return AllTasks.FindRef(TaskId);
}

void FTaskScheduler::CleanupCompletedTasks(double MaxAgeSeconds)
{
    FScopeLock MapLock(&TaskMapLock);
    
    double CurrentTime = FPlatformTime::Seconds();
    TArray<uint64> TasksToRemove;
    
    // Find completed, cancelled, or failed tasks that are older than the max age
    for (auto& TaskPair : AllTasks)
    {
        FMiningTask* Task = TaskPair.Value;
        ETaskStatus Status = Task->GetStatus();
        
        if (Status == ETaskStatus::Completed || Status == ETaskStatus::Cancelled || Status == ETaskStatus::Failed)
        {
            double TaskAge = CurrentTime - Task->CompletionTime;
            
            if (TaskAge > MaxAgeSeconds)
            {
                TasksToRemove.Add(TaskPair.Key);
            }
        }
    }
    
    // Remove and delete the old tasks
    for (uint64 TaskId : TasksToRemove)
    {
        FMiningTask* Task = AllTasks.FindRef(TaskId);
        if (Task)
        {
            delete Task;
            AllTasks.Remove(TaskId);
        }
    }
}

FMiningTask* FTaskScheduler::GetNextTask(int32 WorkerId)
{
    FScopeLock Lock(&TaskQueueLock);
    
    // Check for no work
    bool bHasWork = false;
    for (const auto& QueuePair : TaskQueues)
    {
        if (QueuePair.Value.Num() > 0)
        {
            bHasWork = true;
            break;
        }
    }
    
    if (!bHasWork)
    {
        return nullptr;
    }
    
    // Process tasks in priority order
    for (ETaskPriority Priority : { ETaskPriority::Critical, ETaskPriority::High, ETaskPriority::Normal, ETaskPriority::Low, ETaskPriority::Background })
    {
        TArray<FMiningTask*>& Queue = TaskQueues[Priority];
        
        if (Queue.Num() > 0)
        {
            // Check each task in the queue for this priority level
            for (int32 i = 0; i < Queue.Num(); ++i)
            {
                FMiningTask* Task = Queue[i];
                
                // Make sure task is still queued
                if (Task->GetStatus() != ETaskStatus::Queued)
                {
                    // Task is no longer queued, remove it
                    Queue.RemoveAt(i);
                    --i;
                    continue;
                }
                
                // Check if all dependencies are satisfied
                if (!Task->AreDependenciesSatisfied(AllTasks))
                {
                    // Dependencies not satisfied, skip this task
                    continue;
                }
                
                // Found a task to execute
                Queue.RemoveAt(i);
                
                // Update task counts
                TaskCountByStatus[ETaskStatus::Queued].Decrement();
                TaskCountByStatus[ETaskStatus::Executing].Increment();
                
                return Task;
            }
        }
    }
    
    return nullptr;
}

