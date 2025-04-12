// Copyright Epic Games, Inc. All Rights Reserved.

#include "TaskScheduler.h"
#include "Misc/ScopeLock.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Event.h"

// Initialize static members
FTaskScheduler* FTaskScheduler::Instance = nullptr;
uint32 FTaskScheduler::WorkerThreadTLS = FPlatformTLS::AllocTlsSlot();

// Implementation of FMiningTask

FMiningTask::FMiningTask(uint64 InId, TFunction<void()> InTaskFunction, const FTaskConfig& InConfig, const FString& InDesc)
    : Id(InId)
    , TaskFunction(MoveTemp(InTaskFunction))
    , Config(InConfig)
    , Description(InDesc)
    , ExecutingThreadId(INDEX_NONE)
{
    // Initialize counters and timestamps
    Status.Set(static_cast<int32>(ETaskStatus::Queued));
    Progress.Set(0);
    AttemptCount.Set(0);
    
    // Initialize timestamps
    CreationTime = FPlatformTime::Seconds();
    StartTime = 0.0;
    CompletionTime = 0.0;
    
    // Copy dependencies
    Dependencies = Config.Dependencies;
    
    // Initialize stats
    Stats.QueueTimeMs = 0.0;
    Stats.ExecutionTimeMs = 0.0;
    Stats.RetryCount = 0;
    Stats.PeakMemoryBytes = 0;
    Stats.ExecutingThreadId = 0;
    Stats.ExecutingCore = INDEX_NONE;
}

void FMiningTask::SetProgress(int32 InProgress)
{
    // Clamp progress to 0-100
    int32 ClampedProgress = FMath::Clamp(InProgress, 0, 100);
    Progress.Set(ClampedProgress);
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
    // Mark as running
    SetStatus(ETaskStatus::Running);
    
    // Record execution start time
    StartTime = FPlatformTime::Seconds();
    
    // Record queue time
    Stats.QueueTimeMs = (StartTime - CreationTime) * 1000.0;
    
    // Get current thread and core
    ExecutingThreadId = FTaskScheduler::Get().GetCurrentThreadId();
    Stats.ExecutingThreadId = ExecutingThreadId;
    Stats.ExecutingCore = FPlatformAffinity::GetCurrentCore();
    
    // Execute the task function with timeout protection
    if (Config.MaxExecutionTimeMs > 0)
    {
        // Create a timeout thread to monitor execution
        bool bTimedOut = false;
        FThreadSafeCounter ExecutionComplete;
        
        // Run the task function
        try
        {
            TaskFunction();
        }
        catch (const std::exception& Exception)
        {
            // Task failed with exception
            UE_LOG(LogTemp, Error, TEXT("Task %llu failed with exception: %s"), Id, UTF8_TO_TCHAR(Exception.what()));
            SetStatus(ETaskStatus::Failed);
            ExecutionComplete.Set(1);
            return;
        }
        catch (...)
        {
            // Task failed with unknown exception
            UE_LOG(LogTemp, Error, TEXT("Task %llu failed with unknown exception"), Id);
            SetStatus(ETaskStatus::Failed);
            ExecutionComplete.Set(1);
            return;
        }
        
        // Mark as completed successfully
        ExecutionComplete.Set(1);
    }
    else
    {
        // Run the task function without timeout protection
        try
        {
            TaskFunction();
        }
        catch (const std::exception& Exception)
        {
            // Task failed with exception
            UE_LOG(LogTemp, Error, TEXT("Task %llu failed with exception: %s"), Id, UTF8_TO_TCHAR(Exception.what()));
            SetStatus(ETaskStatus::Failed);
            return;
        }
        catch (...)
        {
            // Task failed with unknown exception
            UE_LOG(LogTemp, Error, TEXT("Task %llu failed with unknown exception"), Id);
            SetStatus(ETaskStatus::Failed);
            return;
        }
    }

    // Record execution completion time
    CompletionTime = FPlatformTime::Seconds();
    Stats.ExecutionTimeMs = (CompletionTime - StartTime) * 1000.0;
    
    // Mark as completed
    SetStatus(ETaskStatus::Completed);
}

void FMiningTask::Complete(bool bSuccess)
{
    // Update task status
    SetStatus(bSuccess ? ETaskStatus::Completed : ETaskStatus::Failed);
    
    // Record completion time if not already set
    if (CompletionTime <= 0.0)
    {
        CompletionTime = FPlatformTime::Seconds();
        Stats.ExecutionTimeMs = (CompletionTime - StartTime) * 1000.0;
    }
    
    // Execute completion callback if provided
    if (CompletionCallback)
    {
        CompletionCallback(bSuccess);
    }
}

bool FMiningTask::HasTimedOut() const
{
    // Check if the task has a timeout and has exceeded it
    if (Config.MaxExecutionTimeMs > 0 && StartTime > 0.0)
    {
        double ElapsedTimeMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
        return ElapsedTimeMs > Config.MaxExecutionTimeMs;
    }
    return false;
}

bool FMiningTask::AreDependenciesSatisfied(const TMap<uint64, FMiningTask*>& TaskMap) const
{
    // Check if all required dependencies are satisfied
    for (const FTaskDependency& Dependency : Dependencies)
    {
        // Find the dependency task
        const FMiningTask* const* DependencyTaskPtr = TaskMap.Find(Dependency.TaskId);
        
        if (!DependencyTaskPtr || !(*DependencyTaskPtr))
        {
            // Dependency not found and it's required
            if (Dependency.bRequired)
            {
                return false;
            }
            // Optional dependency, so continue
            continue;
        }
        
        // Check dependency status
        const FMiningTask* DependencyTask = *DependencyTaskPtr;
        ETaskStatus DependencyStatus = DependencyTask->GetStatus();
        
        if (DependencyStatus != ETaskStatus::Completed)
        {
            // Required dependency not completed
            if (Dependency.bRequired)
            {
                return false;
            }
            
            // Optional dependency not completed
            // Check timeout - if timeout specified and elapsed, treat as satisfied
            if (Dependency.TimeoutMs > 0)
            {
                double ElapsedMs = (FPlatformTime::Seconds() - DependencyTask->CreationTime) * 1000.0;
                if (ElapsedMs < Dependency.TimeoutMs)
                {
                    return false;
                }
                // Timeout elapsed, so continue to next dependency
            }
            else
            {
                // No timeout and dependency not completed
                return false;
            }
        }
    }
    
    // All dependencies satisfied
    return true;
}

// Implementation of FMiningTaskWorker

FMiningTaskWorker::FMiningTaskWorker(FTaskScheduler* InScheduler, int32 InThreadId, EThreadPriority InPriority)
    : Scheduler(InScheduler)
    , ThreadId(InThreadId)
    , Priority(InPriority)
    , AffinityMask(0)
    , LastIdleTime(0.0)
{
    // Initialize counters
    bRunning.Set(0);
    TasksProcessed.Set(0);
    CurrentTaskId.Set(0);
    IdleTimeMs.Set(0);
    ProcessingTimeMs.Set(0);
    StatsTaskCount.Set(0);
    StatsTaskTimeMs.Set(0);
    
    // Initialize stats
    LastStatsResetTime = FPlatformTime::Seconds();
}

FMiningTaskWorker::~FMiningTaskWorker()
{
    // Ensure the thread is stopped
    if (bRunning.GetValue() > 0)
    {
        Stop();
    }
    
    // Wait for thread to complete
    if (Thread)
    {
        Thread->WaitForCompletion();
        delete Thread;
        Thread = nullptr;
    }
}

bool FMiningTaskWorker::Init()
{
    // Store thread ID in TLS
    FPlatformTLS::SetTlsValue(FTaskScheduler::WorkerThreadTLS, (void*)(UPTRINT)ThreadId);
    
    // Set running flag
    bRunning.Set(1);
    
    // Thread initialization successful
    return true;
}

uint32 FMiningTaskWorker::Run()
{
    // Main worker thread loop
    while (bRunning.GetValue() > 0)
    {
        // Reset idle time measurement
        double StartProcessingTime = FPlatformTime::Seconds();
        if (LastIdleTime == 0.0)
        {
            LastIdleTime = StartProcessingTime;
        }
        
        // Get the next task to execute
        FMiningTask* Task = Scheduler->GetNextTask(ThreadId);
        
        if (Task)
        {
            // Record idle time
            double IdleEnd = FPlatformTime::Seconds();
            double IdleTimeMsElapsed = (IdleEnd - LastIdleTime) * 1000.0;
            IdleTimeMs.Add(static_cast<int32>(IdleTimeMsElapsed));
            LastIdleTime = 0.0;
            
            // Set current task
            CurrentTaskId.Set(static_cast<int32>(Task->Id));
            
            // Execute the task
            Task->Execute();
            
            // Clear current task
            CurrentTaskId.Set(0);
            
            // Update statistics
            TasksProcessed.Increment();
            StatsTaskCount.Increment();
            
            // Record task execution time
            double TaskEnd = FPlatformTime::Seconds();
            double TaskTimeMs = (TaskEnd - IdleEnd) * 1000.0;
            StatsTaskTimeMs.Add(static_cast<int32>(TaskTimeMs));
            ProcessingTimeMs.Add(static_cast<int32>(TaskTimeMs));
        }
        else
        {
            // No task available, sleep briefly
            if (LastIdleTime == 0.0)
            {
                LastIdleTime = FPlatformTime::Seconds();
            }
            FPlatformProcess::Sleep(0.001f); // 1ms sleep
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
    // Signal thread to stop
    bRunning.Set(0);
}

void FMiningTaskWorker::Exit()
{
    // Thread exit - nothing to do
}

int32 FMiningTaskWorker::GetThreadId() const
{
    return ThreadId;
}

bool FMiningTaskWorker::SetPriority(EThreadPriority InPriority)
{
    // Store the new priority
    Priority = InPriority;
    
    // Apply the priority to the thread if it exists
    if (Thread)
    {
        return Thread->SetPriority(Priority);
    }
    
    return true;
}

bool FMiningTaskWorker::SetAffinity(uint64 CoreMask)
{
    // Store the affinity mask
    AffinityMask = CoreMask;
    
    // Apply the affinity to the thread if it exists
    if (Thread)
    {
        return FPlatformAffinity::SetThreadAffinity(Thread->GetThreadHandle(), AffinityMask);
    }
    
    return true;
}

uint32 FMiningTaskWorker::GetTasksProcessed() const
{
    return TasksProcessed.GetValue();
}

uint64 FMiningTaskWorker::GetCurrentTaskId() const
{
    return static_cast<uint64>(CurrentTaskId.GetValue());
}

bool FMiningTaskWorker::IsIdle() const
{
    return CurrentTaskId.GetValue() == 0;
}

float FMiningTaskWorker::GetUtilization() const
{
    int32 TotalTimeMs = IdleTimeMs.GetValue() + ProcessingTimeMs.GetValue();
    if (TotalTimeMs == 0)
    {
        return 0.0f;
    }
    
    return static_cast<float>(ProcessingTimeMs.GetValue()) / static_cast<float>(TotalTimeMs);
}

void FMiningTaskWorker::GetStats(double& OutAverageTaskTimeMs, double& OutIdleTimePercent) const
{
    int32 TaskCount = StatsTaskCount.GetValue();
    int32 TaskTimeMs = StatsTaskTimeMs.GetValue();
    int32 TotalTimeMs = IdleTimeMs.GetValue() + ProcessingTimeMs.GetValue();
    
    // Calculate average task time
    OutAverageTaskTimeMs = (TaskCount > 0) ? static_cast<double>(TaskTimeMs) / TaskCount : 0.0;
    
    // Calculate idle time percentage
    OutIdleTimePercent = (TotalTimeMs > 0) 
        ? static_cast<double>(IdleTimeMs.GetValue()) / TotalTimeMs * 100.0 
        : 0.0;
}

// Implementation of FTaskScheduler

FTaskScheduler::FTaskScheduler()
    : bIsInitialized(false)
{
    // Store singleton instance
    Instance = this;
    
    // Initialize task counters
    NextTaskId.Set(1); // Start IDs from 1
    
    // Initialize task count by status
    TaskCountByStatus.Add(ETaskStatus::Queued, FThreadSafeCounter(0));
    TaskCountByStatus.Add(ETaskStatus::Running, FThreadSafeCounter(0));
    TaskCountByStatus.Add(ETaskStatus::Completed, FThreadSafeCounter(0));
    TaskCountByStatus.Add(ETaskStatus::Cancelled, FThreadSafeCounter(0));
    TaskCountByStatus.Add(ETaskStatus::Failed, FThreadSafeCounter(0));
    
    // Determine the number of logical cores
    NumLogicalCores = FPlatformMisc::NumberOfCores();
}

FTaskScheduler::~FTaskScheduler()
{
    // Shutdown the scheduler if it's still initialized
    if (bIsInitialized)
    {
        Shutdown();
    }
    
    // Clear singleton instance if it's this instance
    if (Instance == this)
    {
        Instance = nullptr;
    }
}

bool FTaskScheduler::Initialize()
{
    // Check if already initialized
    if (bIsInitialized)
    {
        return true;
    }
    
    // Determine worker thread count
    int32 WorkerThreadCount = DetermineWorkerThreadCount();
    
    // Create worker threads
    CreateWorkerThreads(WorkerThreadCount);
    
    // Mark as initialized
    bIsInitialized = true;
    
    UE_LOG(LogTemp, Log, TEXT("Task Scheduler initialized with %d worker threads"), WorkerThreadCount);
    
    return true;
}

void FTaskScheduler::Shutdown()
{
    // Check if initialized
    if (!bIsInitialized)
    {
        return;
    }
    
    // Stop all worker threads
    for (FMiningTaskWorker* Worker : Workers)
    {
        Worker->Stop();
    }
    
    // Wait for all workers to complete
    for (FMiningTaskWorker* Worker : Workers)
    {
        delete Worker;
    }
    
    // Clear worker array
    Workers.Empty();
    
    // Cancel all pending tasks
    FScopeLock Lock(&TaskMapLock);
    for (auto& TaskPair : AllTasks)
    {
        FMiningTask* Task = TaskPair.Value;
        if (Task && Task->GetStatus() == ETaskStatus::Queued)
        {
            Task->SetStatus(ETaskStatus::Cancelled);
            TaskCountByStatus.FindChecked(ETaskStatus::Queued).Decrement();
            TaskCountByStatus.FindChecked(ETaskStatus::Cancelled).Increment();
            TasksCancelled.Increment();
        }
    }
    
    // Clean up task maps
    for (auto& QueuePair : TaskQueues)
    {
        for (FMiningTask* Task : QueuePair.Value)
        {
            delete Task;
        }
        QueuePair.Value.Empty();
    }
    TaskQueues.Empty();
    
    // Clean up all tasks map
    for (auto& TaskPair : AllTasks)
    {
        delete TaskPair.Value;
    }
    AllTasks.Empty();
    
    // Mark as not initialized
    bIsInitialized = false;
    
    UE_LOG(LogTemp, Log, TEXT("Task Scheduler shutdown"));
}

bool FTaskScheduler::IsInitialized() const
{
    return bIsInitialized;
}

uint64 FTaskScheduler::ScheduleTask(TFunction<void()> TaskFunc, const FTaskConfig& Config, const FString& Desc)
{
    // Create a task with no completion callback
    return ScheduleTaskWithCallback(MoveTemp(TaskFunc), TFunction<void(bool)>(), Config, Desc);
}

uint64 FTaskScheduler::ScheduleTaskWithCallback(TFunction<void()> TaskFunc, TFunction<void(bool)> OnComplete, 
    const FTaskConfig& Config, const FString& Desc)
{
    // Check if initialized
    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Error, TEXT("Task Scheduler not initialized"));
        return 0;
    }
    
    // Check if task function is valid
    if (!TaskFunc)
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot schedule task with null function"));
        return 0;
    }
    
    // Generate a unique task ID
    uint64 TaskId = GenerateTaskId();
    
    // Create the task
    FMiningTask* Task = new FMiningTask(TaskId, MoveTemp(TaskFunc), Config, Desc);
    if (OnComplete)
    {
        Task->CompletionCallback = MoveTemp(OnComplete);
    }
    
    // Add to task map
    {
        FScopeLock Lock(&TaskMapLock);
        AllTasks.Add(TaskId, Task);
    }
    
    // Add to the appropriate priority queue
    {
        FScopeLock Lock(&TaskQueueLock);
        
        // Create queue for this priority level if it doesn't exist
        if (!TaskQueues.Contains(Config.Priority))
        {
            TaskQueues.Add(Config.Priority, TArray<FMiningTask*>());
        }
        
        // Add task to queue
        TaskQueues[Config.Priority].Add(Task);
    }
    
    // Update task counts
    TaskCountByStatus.FindChecked(ETaskStatus::Queued).Increment();
    TasksScheduled.Increment();
    
    UE_LOG(LogTemp, Verbose, TEXT("Task %llu scheduled: %s"), TaskId, *Desc);
    
    return TaskId;
}

bool FTaskScheduler::CancelTask(uint64 TaskId)
{
    // Check if initialized
    if (!bIsInitialized)
    {
        return false;
    }
    
    // Find the task
    FMiningTask* Task = GetTaskById(TaskId);
    if (!Task)
    {
        UE_LOG(LogTemp, Warning, TEXT("Cannot cancel task %llu: Task not found"), TaskId);
        return false;
    }
    
    // Check if task can be cancelled
    if (!Task->Config.bCancellable)
    {
        UE_LOG(LogTemp, Warning, TEXT("Task %llu cannot be cancelled"), TaskId);
        return false;
    }
    
    // Check current status
    ETaskStatus CurrentStatus = Task->GetStatus();
    if (CurrentStatus != ETaskStatus::Queued && CurrentStatus != ETaskStatus::Running)
    {
        UE_LOG(LogTemp, Warning, TEXT("Cannot cancel task %llu: Status is %d"), TaskId, static_cast<int32>(CurrentStatus));
        return false;
    }
    
    // Set status to cancelled
    if (CurrentStatus == ETaskStatus::Queued)
    {
        Task->SetStatus(ETaskStatus::Cancelled);
        
        // Update task counts
        TaskCountByStatus.FindChecked(CurrentStatus).Decrement();
        TaskCountByStatus.FindChecked(ETaskStatus::Cancelled).Increment();
        TasksCancelled.Increment();
        
        // Remove from queue if still in queue
        FScopeLock Lock(&TaskQueueLock);
        TArray<FMiningTask*>* Queue = TaskQueues.Find(Task->Config.Priority);
        if (Queue)
        {
            Queue->Remove(Task);
        }
        
        // Call completion callback
        if (Task->CompletionCallback)
        {
            Task->CompletionCallback(false);
        }
        
        UE_LOG(LogTemp, Verbose, TEXT("Task %llu cancelled"), TaskId);
        return true;
    }
    else if (CurrentStatus == ETaskStatus::Running)
    {
        // For running tasks, we can only mark them as cancelled
        // The actual cancellation depends on the task implementation
        Task->SetStatus(ETaskStatus::Cancelled);
        
        // Update task counts
        TaskCountByStatus.FindChecked(CurrentStatus).Decrement();
        TaskCountByStatus.FindChecked(ETaskStatus::Cancelled).Increment();
        TasksCancelled.Increment();
        
        UE_LOG(LogTemp, Verbose, TEXT("Task %llu marked for cancellation"), TaskId);
        return true;
    }
    
    return false;
}

ETaskStatus FTaskScheduler::GetTaskStatus(uint64 TaskId) const
{
    // Find the task
    FMiningTask* Task = GetTaskById(TaskId);
    if (!Task)
    {
        return ETaskStatus::Failed;
    }
    
    return Task->GetStatus();
}

FTaskStats FTaskScheduler::GetTaskStats(uint64 TaskId) const
{
    // Find the task
    FMiningTask* Task = GetTaskById(TaskId);
    if (!Task)
    {
        return FTaskStats();
    }
    
    return Task->Stats;
}

bool FTaskScheduler::GetTaskProgress(uint64 TaskId, float& OutProgress) const
{
    // Find the task
    FMiningTask* Task = GetTaskById(TaskId);
    if (!Task || !Task->Config.bSupportsProgress)
    {
        OutProgress = 0.0f;
        return false;
    }
    
    OutProgress = Task->GetProgress() / 100.0f;
    return true;
}

bool FTaskScheduler::WaitForTask(uint64 TaskId, uint32 TimeoutMs)
{
    // Find the task
    FMiningTask* Task = GetTaskById(TaskId);
    if (!Task)
    {
        return false;
    }
    
    const double StartTime = FPlatformTime::Seconds();
    const double TimeoutSeconds = TimeoutMs / 1000.0;
    const bool bHasTimeout = TimeoutMs > 0;
    
    // Wait until task is no longer queued or running
    while (true)
    {
        ETaskStatus Status = Task->GetStatus();
        
        // Check if task is complete
        if (Status != ETaskStatus::Queued && Status != ETaskStatus::Running)
        {
            return Status == ETaskStatus::Completed;
        }
        
        // Check timeout
        if (bHasTimeout)
        {
            const double ElapsedSeconds = FPlatformTime::Seconds() - StartTime;
            if (ElapsedSeconds >= TimeoutSeconds)
            {
                return false;
            }
        }
        
        // Sleep briefly
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
    
    const double StartTime = FPlatformTime::Seconds();
    const double TimeoutSeconds = TimeoutMs / 1000.0;
    const bool bHasTimeout = TimeoutMs > 0;
    
    // Find all tasks
    TArray<FMiningTask*> Tasks;
    Tasks.Reserve(TaskIds.Num());
    
    for (uint64 TaskId : TaskIds)
    {
        FMiningTask* Task = GetTaskById(TaskId);
        if (Task)
        {
            Tasks.Add(Task);
        }
    }
    
    if (Tasks.Num() == 0)
    {
        return false;
    }
    
    // Wait for tasks
    while (true)
    {
        int32 CompletedCount = 0;
        int32 SuccessCount = 0;
        
        for (FMiningTask* Task : Tasks)
        {
            ETaskStatus Status = Task->GetStatus();
            
            if (Status != ETaskStatus::Queued && Status != ETaskStatus::Running)
            {
                CompletedCount++;
                
                if (Status == ETaskStatus::Completed)
                {
                    SuccessCount++;
                }
            }
        }
        
        // Check if we're done
        if (bWaitForAll)
        {
            // All tasks must complete
            if (CompletedCount == Tasks.Num())
            {
                return SuccessCount == Tasks.Num();
            }
        }
        else
        {
            // Any task completing is sufficient
            if (CompletedCount > 0)
            {
                return SuccessCount > 0;
            }
        }
        
        // Check timeout
        if (bHasTimeout)
        {
            const double ElapsedSeconds = FPlatformTime::Seconds() - StartTime;
            if (ElapsedSeconds >= TimeoutSeconds)
            {
                return false;
            }
        }
        
        // Sleep briefly
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
    return static_cast<int32>((UPTRINT)FPlatformTLS::GetTlsValue(WorkerThreadTLS));
}

bool FTaskScheduler::IsTaskThread() const
{
    return GetCurrentThreadId() != 0;
}

bool FTaskScheduler::SetThreadPriority(int32 ThreadId, EThreadPriority Priority)
{
    // Check if thread ID is valid
    if (ThreadId < 0 || ThreadId >= Workers.Num())
    {
        return false;
    }
    
    return Workers[ThreadId]->SetPriority(Priority);
}

bool FTaskScheduler::SetThreadAffinity(int32 ThreadId, uint64 CoreMask)
{
    // Check if thread ID is valid
    if (ThreadId < 0 || ThreadId >= Workers.Num())
    {
        return false;
    }
    
    return Workers[ThreadId]->SetAffinity(CoreMask);
}

TMap<ETaskStatus, int32> FTaskScheduler::GetTaskCounts() const
{
    TMap<ETaskStatus, int32> Result;
    
    // Copy the values from thread-safe counters
    for (auto& Pair : TaskCountByStatus)
    {
        Result.Add(Pair.Key, Pair.Value.GetValue());
    }
    
    return Result;
}

ITaskScheduler& FTaskScheduler::Get()
{
    if (!Instance)
    {
        Instance = new FTaskScheduler();
        Instance->Initialize();
    }
    
    return *Instance;
}

FMiningTask* FTaskScheduler::GetNextTask(int32 WorkerId)
{
    // If not initialized, no tasks available
    if (!bIsInitialized)
    {
        return nullptr;
    }
    
    FScopeLock Lock(&TaskQueueLock);
    
    // Start with highest priority tasks
    static const ETaskPriority PriorityOrder[] = {
        ETaskPriority::Critical,
        ETaskPriority::High,
        ETaskPriority::Normal,
        ETaskPriority::Low,
        ETaskPriority::Minimal
    };
    
    for (ETaskPriority Priority : PriorityOrder)
    {
        TArray<FMiningTask*>* QueuePtr = TaskQueues.Find(Priority);
        if (!QueuePtr || QueuePtr->Num() == 0)
        {
            continue;
        }
        
        TArray<FMiningTask*>& Queue = *QueuePtr;
        
        // Find a task that has all its dependencies satisfied
        for (int32 i = 0; i < Queue.Num(); ++i)
        {
            FMiningTask* Task = Queue[i];
            
            // Check if the dependencies are satisfied
            if (Task->AreDependenciesSatisfied(AllTasks))
            {
                // Remove from queue
                Queue.RemoveAt(i);
                
                // Update status
                TaskCountByStatus.FindChecked(ETaskStatus::Queued).Decrement();
                TaskCountByStatus.FindChecked(ETaskStatus::Running).Increment();
                
                return Task;
            }
        }
    }
    
    // No task available
    return nullptr;
}

int32 FTaskScheduler::DetermineWorkerThreadCount() const
{
    // Determine worker thread count based on available logical cores
    int32 DesiredThreadCount = FMath::Max(1, NumLogicalCores - 2);
    
    // Limit to a reasonable maximum
    return FMath::Min(DesiredThreadCount, 32);
}

void FTaskScheduler::CreateWorkerThreads(int32 ThreadCount)
{
    Workers.Empty(ThreadCount);
    
    // Create worker threads
    for (int32 i = 0; i < ThreadCount; ++i)
    {
        // Create worker
        FMiningTaskWorker* Worker = new FMiningTaskWorker(this, i, TPri_Normal);
        Workers.Add(Worker);
        
        // Create thread name
        FString ThreadName = FString::Printf(TEXT("TaskWorker%d"), i);
        
        // Create thread
        Worker->Thread = FRunnableThread::Create(Worker, *ThreadName);
    }
    
    // Set affinities for NUMA optimization
    if (ThreadCount > 1 && NumLogicalCores > 1)
    {
        const uint64 AllCores = FPlatformAffinity::GetProcessorMask();
        
        for (int32 i = 0; i < ThreadCount; ++i)
        {
            // Calculate core mask for this thread
            const uint64 CoreMask = 1ULL << (i % NumLogicalCores);
            
            // Apply affinity if the core is valid
            if ((CoreMask & AllCores) != 0)
            {
                Workers[i]->SetAffinity(CoreMask);
            }
        }
    }
}

uint64 FTaskScheduler::GenerateTaskId()
{
    // Generate a unique task ID
    return NextTaskId.Increment();
}

FMiningTask* FTaskScheduler::GetTaskById(uint64 TaskId) const
{
    FScopeLock Lock(&TaskMapLock);
    
    // Find the task
    FMiningTask* const* TaskPtr = AllTasks.Find(TaskId);
    return TaskPtr ? *TaskPtr : nullptr;
}

void FTaskScheduler::CleanupCompletedTasks(double MaxAgeSeconds)
{
    const double CurrentTime = FPlatformTime::Seconds();
    
    FScopeLock MapLock(&TaskMapLock);
    
    TArray<uint64> TasksToRemove;
    
    // Find tasks that can be removed
    for (const auto& Pair : AllTasks)
    {
        FMiningTask* Task = Pair.Value;
        
        if (!Task)
        {
            TasksToRemove.Add(Pair.Key);
            continue;
        }
        
        // Check if the task is completed, cancelled, or failed
        ETaskStatus Status = Task->GetStatus();
        if (Status == ETaskStatus::Completed || Status == ETaskStatus::Cancelled || Status == ETaskStatus::Failed)
        {
            // Check age
            double AgeSeconds = CurrentTime - Task->CompletionTime;
            if (AgeSeconds > MaxAgeSeconds)
            {
                TasksToRemove.Add(Pair.Key);
            }
        }
    }
    
    // Remove tasks
    for (uint64 TaskId : TasksToRemove)
    {
        FMiningTask* Task = AllTasks.FindAndRemoveChecked(TaskId);
        delete Task;
    }
}