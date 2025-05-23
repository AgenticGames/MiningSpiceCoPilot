// Copyright Epic Games, Inc. All Rights Reserved.

#include "TaskScheduler.h"
#include "Interfaces/ITaskScheduler.h"
#include "TaskSystem/TaskTypes.h"

// Include necessary core headers
#include "CoreMinimal.h"
#include "HAL/PlatformTLS.h"
#include "HAL/ThreadHeartBeat.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformAffinity.h"
#include "TaskDependencyVisualizer.h"
#include "Async/TaskGraphInterfaces.h"
#include "HAL/Event.h"
#include "HAL/PlatformTime.h"
#include "Misc/OutputDeviceRedirector.h"
#include "HAL/PlatformCrt.h"
#include "HAL/Thread.h"
#include "Misc/ScopeLock.h"
#include "Misc/Timespan.h"
#include "Misc/CoreStats.h"
#include "Misc/StringBuilder.h"
#include "Misc/SpinLock.h"
#include "HAL/ThreadManager.h"
#include "HAL/RunnableThread.h"
#include "Misc/Parse.h"
#include "ThreadSafety.h"
#include "Logging/LogMacros.h"
#include "RegistryLockLevelEnum.h"
#include "Math/NumericLimits.h"

// Include Windows-specific headers
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include <intrin.h>  // For __cpuid function
#include "Windows/HideWindowsPlatformTypes.h"
#endif

// Forward declare NumaHelpers namespace if not in ThreadSafety.h
namespace NumaHelpers
{
    uint64 GetAllCoresMask();
}

// Forward declaration of the scheduler singleton
static FTaskScheduler* GTaskScheduler = nullptr;

// Add at the top of the file, after includes
// Helper function for TArray type conversion from uint32 to int32
template<typename SourceType, typename DestType>
void ConvertArrayTypes(const TArray<SourceType>& SourceArray, TArray<DestType>& DestArray)
{
    DestArray.Empty(SourceArray.Num());
    for (const SourceType& Value : SourceArray)
    {
        DestArray.Add(static_cast<DestType>(Value));
    }
}

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

FMiningTask* FMiningTaskWorker::SelectNextTask()
{
    return GetScheduler()->GetNextTask(GetThreadId());
}

//----------------------------------------------------------------------
// FTaskScheduler Implementation
//----------------------------------------------------------------------

// Comment out the entire constructor implementation
/*
FTaskScheduler::FTaskScheduler()
{
    // Constructor logic here
    // ...
}
*/

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
    if (!FTaskScheduler::Instance)
    {
        FTaskScheduler::Instance = new FTaskScheduler();
        FTaskScheduler::Instance->Initialize();
    }
    return *FTaskScheduler::Instance;
}

bool FTaskScheduler::Initialize()
{
    FScopeLock Lock(&TaskMapLock);
    
    if (bIsInitialized)
    {
        return true;
    }
    
    // Initialize TLS for worker threads
    if (WorkerThreadTLS == 0)
    {
        WorkerThreadTLS = FPlatformTLS::AllocTlsSlot();
    }
    
    NumLogicalCores = FPlatformMisc::NumberOfCores();
    int32 ThreadCount = DetermineWorkerThreadCount();
    
    // Detect processor features
    ProcessorFeatures = DetectProcessorFeatures();
    
    // Reset task counters
    TasksScheduled.Set(0);
    TasksCompleted.Set(0);
    TasksCancelled.Set(0);
    TasksFailed.Set(0);
    
    // Create initial task count for all statuses
    for (int32 i = 0; i < static_cast<int32>(ETaskStatus::MaxValue); ++i)
    {
        TaskCountByStatus.Add(static_cast<ETaskStatus>(i), FThreadSafeCounter(0));
    }
    
    // Create thread workers
    CreateWorkerThreads(ThreadCount);
    
    // Set the initialized flag
    bIsInitialized = true;
    
    // Set the singleton instance
    Instance = this;
    
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
    for (FMiningTaskWorker* Worker : WorkerThreads)
    {
        Worker->Stop();
    }
    
    // Wait for threads to complete and clean up
    for (FMiningTaskWorker* Worker : WorkerThreads)
    {
        delete Worker;
    }
    
    WorkerThreads.Empty();
    
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
    return WorkerThreads.Num();
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
    if (ThreadId < 0 || ThreadId >= WorkerThreads.Num())
    {
        return false;
    }
    
    return WorkerThreads[ThreadId]->SetPriority(Priority);
}

bool FTaskScheduler::SetThreadAffinity(int32 ThreadId, uint64 CoreMask)
{
    if (ThreadId < 0 || ThreadId >= WorkerThreads.Num())
    {
        return false;
    }
    
    return WorkerThreads[ThreadId]->SetAffinity(CoreMask);
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
    FScopeLock Lock(&WorkerArrayLock);
    
    WorkerThreads.Empty();
    
    // Get NUMA topology from ThreadSafety
    FNUMATopology& NUMATopology = FThreadSafety::Get().NUMATopology;
    int32 NumDomains = NUMATopology.DomainCount;
    
    // Prepare NUMA node information
    TArray<FNumaNodeInfo> NumaNodes;
    for (int32 DomainId = 0; DomainId < NumDomains; ++DomainId)
    {
        FNumaNodeInfo NodeInfo;
        NodeInfo.NodeIndex = DomainId;
        // Use type conversion to fix compiler error when assigning TArray<uint32> to TArray<int32>
        TArray<int32> LogicalCores;
        ConvertArrayTypes(NUMATopology.GetLogicalCoresForDomain(DomainId), LogicalCores);
        NodeInfo.LogicalCores = LogicalCores;
        NumaNodes.Add(NodeInfo);
    }
    
    // Create balanced worker threads across NUMA domains
    for (int32 i = 0; i < ThreadCount; ++i)
    {
        EThreadPriority Priority = (i == 0) ? TPri_AboveNormal : TPri_Normal;
        
        FMiningTaskWorker* Worker = new FMiningTaskWorker(this, i, Priority);
        WorkerThreads.Add(Worker);
        
        // Calculate NUMA-aware affinity mask
        uint64 AffinityMask = CalculateNumaAwareAffinityMask(i, ThreadCount, NumaNodes);
        Worker->SetAffinity(AffinityMask);
        
        // Register this thread with ThreadSafety for NUMA tracking
        int32 DomainId = i % FMath::Max(1, NumDomains);
        FThreadSafety::Get().AssignThreadToNUMADomain(Worker->GetThreadId(), DomainId);
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

TMap<uint64, FMiningTask*> FTaskScheduler::GetAllTasks() const
{
    FScopeLock Lock(&TaskMapLock);
    return AllTasks;
}

// Initialize static members
uint32 FTaskScheduler::WorkerThreadTLS = 0;
FTaskScheduler* FTaskScheduler::Instance = nullptr;

// Add NUMA-aware affinity mask calculation
uint64 FTaskScheduler::CalculateNumaAwareAffinityMask(int32 ThreadIndex, int32 TotalThreads, const TArray<FNumaNodeInfo>& NumaNodes)
{
    // If no NUMA nodes are available, return all cores
    if (NumaNodes.Num() == 0)
    {
        return NumaHelpers::GetAllCoresMask();
    }

    // Get NUMA topology from ThreadSafety
    FNUMATopology& NUMATopology = FThreadSafety::Get().NUMATopology;
    
    // Distribute threads evenly across NUMA domains
    int32 NumDomains = NUMATopology.DomainCount;
    if (NumDomains == 0)
    {
        return NumaHelpers::GetAllCoresMask();
    }
    
    // Calculate which domain this thread belongs to
    int32 DomainIndex = ThreadIndex % NumDomains;
    
    // Get affinity mask for this domain
    uint64 DomainMask = NUMATopology.GetAffinityMaskForDomain(DomainIndex);
    if (DomainMask != 0)
    {
        return DomainMask;
    }
    
    // Fallback to all cores if domain mask couldn't be determined
    return NumaHelpers::GetAllCoresMask();
}

// Modify scheduler to select workers based on NUMA proximity to task type
int32 FTaskScheduler::FindBestWorkerForTask(const FMiningTask* Task)
{
    if (!Task || WorkerThreads.Num() == 0)
    {
        return -1;
    }
    
    FScopeLock Lock(&WorkerArrayLock);
    
    // First check for specialized workers if the task has a type
    if (Task->HasTypeId())
    {
        FScopeLock SpecializedLock(&SpecializedWorkerMapLock);
        
        // Get basic capabilities for the task type
        ETypeCapabilities Capabilities = GetTypeCapabilities(Task->GetTypeId(), Task->GetRegistryType());
        
        // Look for specialized workers with matching capabilities
        if (SpecializedWorkers.Contains(Capabilities))
        {
            const TArray<FSpecializedTaskWorker*>& MatchingWorkers = SpecializedWorkers[Capabilities];
            if (MatchingWorkers.Num() > 0)
            {
                // Find the least busy worker
                int32 BestWorkerIndex = -1;
                uint32 LowestTaskCount = TNumericLimits<uint32>::Max();
                
                for (FSpecializedTaskWorker* Worker : MatchingWorkers)
                {
                    if (Worker->IsIdle() && Worker->GetTasksProcessed() < LowestTaskCount)
                    {
                        BestWorkerIndex = Worker->GetThreadId();
                        LowestTaskCount = Worker->GetTasksProcessed();
                    }
                }
                
                if (BestWorkerIndex >= 0)
                {
                    return BestWorkerIndex;
                }
            }
        }
        
        // No specialized workers available, try to find a worker in the same NUMA domain
        // associated with this type
        if (Task->HasTypeId())
        {
            // Determine the preferred NUMA domain for this type of operation
            uint32 PreferredDomain = DeterminePreferredDomainForType(Task->GetTypeId(), Task->GetRegistryType());
            
            // Find a worker in this domain or closest to it
            if (PreferredDomain != TNumericLimits<uint32>::Max())
            {
                for (FMiningTaskWorker* Worker : WorkerThreads)
                {
                    uint32 WorkerDomain = FThreadSafety::Get().GetCurrentThreadNUMADomain();
                    if (WorkerDomain == PreferredDomain && Worker->IsIdle())
                    {
                        return Worker->GetThreadId();
                    }
                }
            }
        }
    }
    
    // No domain-specific worker found, fall back to generic assignment
    // Find the least busy worker
    int32 BestWorkerIndex = -1;
    uint32 LowestTaskCount = TNumericLimits<uint32>::Max();
    
    for (FMiningTaskWorker* Worker : WorkerThreads)
    {
        if (Worker->IsIdle() && Worker->GetTasksProcessed() < LowestTaskCount)
        {
            BestWorkerIndex = Worker->GetThreadId();
            LowestTaskCount = Worker->GetTasksProcessed();
        }
    }
    
    // If no idle worker, pick the least busy one
    if (BestWorkerIndex < 0)
    {
        LowestTaskCount = TNumericLimits<uint32>::Max();
        for (FMiningTaskWorker* Worker : WorkerThreads)
        {
            if (Worker->GetTasksProcessed() < LowestTaskCount)
            {
                BestWorkerIndex = Worker->GetThreadId();
                LowestTaskCount = Worker->GetTasksProcessed();
            }
        }
    }
    
    return BestWorkerIndex;
}

// Add a method to determine the preferred NUMA domain for a type
uint32 FTaskScheduler::DeterminePreferredDomainForType(uint32 TypeId, ERegistryType RegistryType)
{
    // This could be based on various heuristics:
    // 1. Historical access patterns
    // 2. Memory locality of related data
    // 3. Type of operation (compute-intensive vs memory-intensive)
    
    // For now, implement a simple round-robin assignment based on type ID
    FNUMATopology& NUMATopology = FThreadSafety::Get().NUMATopology;
    int32 NumDomains = NUMATopology.DomainCount;
    
    if (NumDomains <= 1)
    {
        return 0;
    }
    
    // Simple hash to distribute types across domains
    return TypeId % NumDomains;
}

// Implementation of static functions needed for FindBestWorkerForTask
ETypeCapabilities FTaskScheduler::GetTypeCapabilities(uint32 TypeId, ERegistryType RegistryType)
{
    // Default capabilities based on registry type
    ETypeCapabilities DefaultCapabilities = ETypeCapabilities::None;
    
    // Set default capabilities based on registry type
    switch (RegistryType)
    {
        case ERegistryType::Material:
            DefaultCapabilities = TypeCapabilitiesHelpers::AddBasicCapability(
                DefaultCapabilities, ETypeCapabilities::BatchOperations);
            DefaultCapabilities = TypeCapabilitiesHelpers::AddBasicCapability(
                DefaultCapabilities, ETypeCapabilities::ParallelProcessing);
            break;
        case ERegistryType::SVO:
            // SVO capabilities are now in the extended capabilities enum
            break;
        case ERegistryType::SDF:
            DefaultCapabilities = TypeCapabilitiesHelpers::AddBasicCapability(
                DefaultCapabilities, ETypeCapabilities::SIMDOperations);
            break;
        case ERegistryType::Zone:
            DefaultCapabilities = TypeCapabilitiesHelpers::AddBasicCapability(
                DefaultCapabilities, ETypeCapabilities::ThreadSafe);
            break;
        case ERegistryType::Service:
            DefaultCapabilities = TypeCapabilitiesHelpers::AddBasicCapability(
                DefaultCapabilities, ETypeCapabilities::AsyncOperations);
            DefaultCapabilities = TypeCapabilitiesHelpers::AddBasicCapability(
                DefaultCapabilities, ETypeCapabilities::PartialExecution);
            break;
        default:
            DefaultCapabilities = ETypeCapabilities::None;
            break;
    }
    
    // If the type ID is 0, just return the default capabilities
    if (TypeId == 0)
    {
        return DefaultCapabilities;
    }
    
    // Here you would implement logic to query your type registry for specific capabilities
    // For now, we'll just return the default capabilities based on registry type
    
    return DefaultCapabilities;
}

// Implementation of the new GetTypeCapabilitiesEx function
ETypeCapabilitiesEx FTaskScheduler::GetTypeCapabilitiesEx(uint32 TypeId, ERegistryType RegistryType)
{
    // Default capabilities based on registry type
    ETypeCapabilitiesEx DefaultCapabilities = ETypeCapabilitiesEx::None;
    
    // Set default capabilities based on registry type
    switch (RegistryType)
    {
        case ERegistryType::Material:
            // No extended capabilities for material types
            break;
        case ERegistryType::SVO:
            DefaultCapabilities = TypeCapabilitiesHelpers::AddAdvancedCapability(
                DefaultCapabilities, ETypeCapabilitiesEx::SpatialCoherence);
            DefaultCapabilities = TypeCapabilitiesHelpers::AddAdvancedCapability(
                DefaultCapabilities, ETypeCapabilitiesEx::CacheOptimized);
            break;
        case ERegistryType::SDF:
            DefaultCapabilities = TypeCapabilitiesHelpers::AddAdvancedCapability(
                DefaultCapabilities, ETypeCapabilitiesEx::Vectorizable);
            break;
        case ERegistryType::Zone:
            DefaultCapabilities = TypeCapabilitiesHelpers::AddAdvancedCapability(
                DefaultCapabilities, ETypeCapabilitiesEx::LowContention);
            break;
        case ERegistryType::Service:
            // No extended capabilities for service types
            break;
        default:
            DefaultCapabilities = ETypeCapabilitiesEx::None;
            break;
    }
    
    // If the type ID is 0, just return the default capabilities
    if (TypeId == 0)
    {
        return DefaultCapabilities;
    }
    
    // Here you would implement logic to query your type registry for specific extended capabilities
    
    return DefaultCapabilities;
}

// Maps type capabilities to thread optimization flags
EThreadOptimizationFlags FTaskScheduler::MapCapabilitiesToOptimizationFlags(ETypeCapabilities Capabilities)
{
    EThreadOptimizationFlags Flags = EThreadOptimizationFlags::None;
    
    // Map basic capabilities to optimization flags
    if (TypeCapabilitiesHelpers::HasBasicCapability(Capabilities, ETypeCapabilities::SIMDOperations))
    {
        Flags |= EThreadOptimizationFlags::EnableSIMD;
    }
    
    if (TypeCapabilitiesHelpers::HasBasicCapability(Capabilities, ETypeCapabilities::ThreadSafe))
    {
        Flags |= EThreadOptimizationFlags::ThreadSafetyEnabled;
    }
    
    if (TypeCapabilitiesHelpers::HasBasicCapability(Capabilities, ETypeCapabilities::BatchOperations))
    {
        Flags |= EThreadOptimizationFlags::BatchProcessingEnabled;
    }
    
    if (TypeCapabilitiesHelpers::HasBasicCapability(Capabilities, ETypeCapabilities::ParallelProcessing))
    {
        Flags |= EThreadOptimizationFlags::ParallelizationEnabled;
    }
    
    if (TypeCapabilitiesHelpers::HasBasicCapability(Capabilities, ETypeCapabilities::AsyncOperations))
    {
        Flags |= EThreadOptimizationFlags::AsynchronousEnabled;
    }
    
    return Flags;
}

// Maps both basic and extended type capabilities to thread optimization flags
EThreadOptimizationFlags FTaskScheduler::MapCapabilitiesToOptimizationFlags(ETypeCapabilities Capabilities, ETypeCapabilitiesEx CapabilitiesEx)
{
    // Start with the basic capabilities
    EThreadOptimizationFlags Flags = MapCapabilitiesToOptimizationFlags(Capabilities);
    
    // Add flags from extended capabilities
    if (TypeCapabilitiesHelpers::HasAdvancedCapability(CapabilitiesEx, ETypeCapabilitiesEx::SpatialCoherence))
    {
        Flags |= EThreadOptimizationFlags::SpatialCoherenceEnabled;
    }
    
    if (TypeCapabilitiesHelpers::HasAdvancedCapability(CapabilitiesEx, ETypeCapabilitiesEx::CacheOptimized))
    {
        Flags |= EThreadOptimizationFlags::CacheOptimizationEnabled;
    }
    
    if (TypeCapabilitiesHelpers::HasAdvancedCapability(CapabilitiesEx, ETypeCapabilitiesEx::Vectorizable))
    {
        Flags |= EThreadOptimizationFlags::VectorizationEnabled;
    }
    
    if (TypeCapabilitiesHelpers::HasAdvancedCapability(CapabilitiesEx, ETypeCapabilitiesEx::LowContention))
    {
        Flags |= EThreadOptimizationFlags::LowContentionEnabled;
    }
    
    return Flags;
}

// Find a worker with specific capabilities
int32 FTaskScheduler::FindWorkerWithCapabilities(ETypeCapabilities Capabilities)
{
    if (WorkerThreads.Num() == 0)
    {
        return -1;
    }
    
    FScopeLock Lock(&WorkerArrayLock);
    FScopeLock SpecializedLock(&SpecializedWorkerMapLock);
    
    // Look for specialized workers with matching capabilities
    for (auto& Pair : SpecializedWorkers)
    {
        ETypeCapabilities WorkerCapabilities = Pair.Key;
        
        // Check if this worker supports all required capabilities
        if (TypeCapabilitiesHelpers::HasBasicCapability(WorkerCapabilities, Capabilities))
        {
            // Find an available worker with these capabilities
            const TArray<FSpecializedTaskWorker*>& MatchingWorkers = Pair.Value;
            if (MatchingWorkers.Num() > 0)
            {
                for (FSpecializedTaskWorker* Worker : MatchingWorkers)
                {
                    if (Worker->IsIdle())
                    {
                        return Worker->GetThreadId();
                    }
                }
            }
        }
    }
    
    // No specialized worker found
    return -1;
}

// Find a worker with specific basic and extended capabilities
int32 FTaskScheduler::FindWorkerWithCapabilities(ETypeCapabilities Capabilities, ETypeCapabilitiesEx CapabilitiesEx)
{
    if (WorkerThreads.Num() == 0)
    {
        return -1;
    }
    
    FScopeLock Lock(&WorkerArrayLock);
    FScopeLock SpecializedLock(&SpecializedWorkerMapLock);
    
    // First try to find a worker that supports the basic capabilities
    int32 WorkerWithBasicCapabilities = FindWorkerWithCapabilities(Capabilities);
    if (WorkerWithBasicCapabilities >= 0)
    {
        // Check if this worker also supports the extended capabilities
        for (auto& Pair : SpecializedWorkers)
        {
            const TArray<FSpecializedTaskWorker*>& SpecializedWorkerList = Pair.Value;
            for (FSpecializedTaskWorker* Worker : SpecializedWorkerList)
            {
                if (Worker->GetThreadId() == WorkerWithBasicCapabilities && 
                    Worker->SupportsCapabilitiesEx(CapabilitiesEx))
                {
                    return Worker->GetThreadId();
                }
            }
        }
    }
    
    // No worker found that supports both basic and extended capabilities
    // Fall back to a worker that supports at least the basic capabilities
    return WorkerWithBasicCapabilities;
}

// Creates a specialized worker thread with basic capabilities
int32 FTaskScheduler::CreateSpecializedWorker(ETypeCapabilities Capabilities, EThreadPriority Priority)
{
    FScopeLock Lock(&WorkerArrayLock);
    FScopeLock SpecializedLock(&SpecializedWorkerMapLock);
    
    // Create a new thread ID
    int32 ThreadId = WorkerThreads.Num();
    
    // Create the specialized worker
    FSpecializedTaskWorker* Worker = new FSpecializedTaskWorker(this, ThreadId, Priority, Capabilities);
    WorkerThreads.Add(Worker);
    
    // Add to specialized workers map
    if (!SpecializedWorkers.Contains(Capabilities))
    {
        SpecializedWorkers.Add(Capabilities, TArray<FSpecializedTaskWorker*>());
    }
    SpecializedWorkers[Capabilities].Add(Worker);
    
    return ThreadId;
}

// Creates a specialized worker thread with both basic and extended capabilities
int32 FTaskScheduler::CreateSpecializedWorker(ETypeCapabilities Capabilities, ETypeCapabilitiesEx CapabilitiesEx, EThreadPriority Priority)
{
    FScopeLock Lock(&WorkerArrayLock);
    FScopeLock SpecializedLock(&SpecializedWorkerMapLock);
    
    // Create a new thread ID
    int32 ThreadId = WorkerThreads.Num();
    
    // Create the specialized worker with both capability types
    FSpecializedTaskWorker* Worker = new FSpecializedTaskWorker(this, ThreadId, Priority, Capabilities, CapabilitiesEx);
    WorkerThreads.Add(Worker);
    
    // Add to specialized workers map using basic capabilities as the key
    if (!SpecializedWorkers.Contains(Capabilities))
    {
        SpecializedWorkers.Add(Capabilities, TArray<FSpecializedTaskWorker*>());
    }
    SpecializedWorkers[Capabilities].Add(Worker);
    
    return ThreadId;
}

// Implementation of DetectProcessorFeatures
EProcessorFeatures FTaskScheduler::DetectProcessorFeatures()
{
    EProcessorFeatures Features = EProcessorFeatures::None;
    
    // Check for SIMD features
    // Since PLATFORM_SUPPORTS_* macros aren't defined, let's use a different approach
    
    // Most modern CPUs support at least SSE2
    Features |= EProcessorFeatures::SSE2;
    
    // Check for enhanced SIMD support based on CPU vendor
#if PLATFORM_WINDOWS
    int CPUInfo[4] = { -1 };
    
    // Get vendor ID
    __cpuid(CPUInfo, 0);
    
    // Get feature information
    __cpuid(CPUInfo, 1);
    
    // Check for SSE3
    if (CPUInfo[2] & (1 << 0))
        Features |= EProcessorFeatures::SSE3;
    
    // Check for SSSE3
    if (CPUInfo[2] & (1 << 9))
        Features |= EProcessorFeatures::SSSE3;
    
    // Check for SSE4.1
    if (CPUInfo[2] & (1 << 19))
        Features |= EProcessorFeatures::SSE41;
    
    // Check for SSE4.2
    if (CPUInfo[2] & (1 << 20))
        Features |= EProcessorFeatures::SSE42;
    
    // Check for AVX
    if (CPUInfo[2] & (1 << 28))
        Features |= EProcessorFeatures::AVX;
    
    // Check for AVX2 requires additional info
    if (CPUInfo[0] >= 7)
    {
        int CPUInfo2[4] = { 0 };
        __cpuid(CPUInfo2, 7);
        
        // Check for AVX2
        if (CPUInfo2[1] & (1 << 5))
            Features |= EProcessorFeatures::AVX2;
    }
#endif
    
    // Check for NUMA support
#if PLATFORM_WINDOWS
    ULONG HighestNodeNumber = 0;
    if (::GetNumaHighestNodeNumber(&HighestNodeNumber) && HighestNodeNumber > 0)
    {
        // No direct NUMA flag in our enum, combine with HTT
        Features |= EProcessorFeatures::HTT;
    }
#endif
    
    // Check for multi-core support
    if (FPlatformMisc::NumberOfCores() > 1)
    {
        // No direct multi-core flag, but HTT is similar
        Features |= EProcessorFeatures::HTT;
    }
    
    return Features;
}

