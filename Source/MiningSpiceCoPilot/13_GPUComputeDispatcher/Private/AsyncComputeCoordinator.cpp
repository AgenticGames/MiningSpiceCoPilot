#include "../Public/AsyncComputeCoordinator.h"
#include "../Public/GPUDispatcherLogging.h"
#include "../../3_ThreadingTaskSystem/Public/AsyncTaskManager.h"

#include "RenderCore.h"
#include "RenderGraphBuilder.h"
#include "TickableObjectRenderThread.h"

FAsyncComputeCoordinator::FAsyncComputeCoordinator()
    : bAsyncComputeSupported(false)
    , FrameBudgetMS(2.0f)
    , QueueUtilization(0.0f)
    , LastFrameTime(0.0)
    , FrameCounter(0)
    , TimeoutFrames(60) // Default timeout: 60 frames
{
    // Initialize priority weights with defaults
    PriorityWeights.Add(1.0f); // Critical
    PriorityWeights.Add(0.5f); // High
    PriorityWeights.Add(0.25f); // Normal
    PriorityWeights.Add(0.1f); // Low
    PriorityWeights.Add(0.05f); // Background
}

FAsyncComputeCoordinator::~FAsyncComputeCoordinator()
{
    // Clean up any remaining operations
    Flush(true);
    
    // Clean up any remaining fences
    CleanupFences();
}

bool FAsyncComputeCoordinator::Initialize(bool bSupportsAsyncCompute)
{
    bAsyncComputeSupported = bSupportsAsyncCompute && GSupportsEfficientAsyncCompute;
    
    if (!bAsyncComputeSupported)
    {
        GPU_DISPATCHER_LOG_WARNING("Async compute not supported on this device, using synchronous compute instead");
    }
    else
    {
        GPU_DISPATCHER_LOG_DEBUG("Async compute coordinator initialized successfully");
    }
    
    // Initialize queue capacity based on frame budget
    for (int32 i = 0; i < 5; ++i)
    {
        EAsyncPriority Priority = static_cast<EAsyncPriority>(i);
        PriorityQueues.Add(Priority, TArray<FPendingAsyncOperation>());
    }
    
    // Initialize frame time
    LastFrameTime = FPlatformTime::Seconds();
    
    return true;
}

int64 FAsyncComputeCoordinator::ScheduleAsyncOperation(const FComputeOperation& Operation, 
                                                      TFunction<void(bool)> CompletionCallback,
                                                      EAsyncPriority Priority)
{
    if (!bAsyncComputeSupported)
    {
        // If async compute is not supported, execute synchronously
        // but on the render thread
        
        // Queue a task on the render thread
        // Create local copies to avoid capturing 'this'
        FComputeOperation OpCopy = Operation;
        TFunction<void(bool)> CallbackCopy = CompletionCallback;
        
        ENQUEUE_RENDER_COMMAND(ExecuteComputeOperation)(
            [OpCopy, CallbackCopy](FRHICommandListImmediate& RHICmdList)
            {
                // Execute on the render thread
                bool bSuccess = true; // assume success for now
                
                // TODO: Implement actual execution
                // This would invoke the appropriate compute shader
                
                // Call completion callback
                if (CallbackCopy)
                {
                    CallbackCopy(bSuccess);
                }
            });
        
        // Return a dummy operation ID
        return static_cast<int64>(NextOperationId.Increment());
    }
    
    FScopeLock Lock(&QueueLock);
    
    // Generate operation ID
    int64 OperationId = static_cast<int64>(NextOperationId.Increment());
    
    // Create pending operation
    FPendingAsyncOperation PendingOp;
    PendingOp.OperationId = OperationId;
    PendingOp.Priority = Priority;
    PendingOp.CompletionCallback = CompletionCallback;
    PendingOp.QueueTime = FPlatformTime::Seconds();
    
    // Add to priority queue
    PriorityQueues.FindOrAdd(Priority).Add(PendingOp);
    
    // Create operation state
    FOperationState& State = PendingOperations.Add(OperationId);
    State.OperationId = OperationId;
    State.Status = EOperationStatus::Pending;
    
    // Update queue metrics
    UpdateQueueMetrics();
    
    GPU_DISPATCHER_LOG_VERBOSE("Scheduled async operation %lld with priority %d", 
        OperationId, static_cast<int32>(Priority));
    
    return OperationId;
}

bool FAsyncComputeCoordinator::CancelAsyncOperation(int64 OperationId)
{
    FScopeLock Lock(&QueueLock);
    
    // Check if operation exists
    FOperationState* State = PendingOperations.Find(OperationId);
    if (!State)
    {
        return false;
    }
    
    // Check if operation is still pending
    if (State->Status != EOperationStatus::Pending && State->Status != EOperationStatus::Running)
    {
        return false;
    }
    
    // Remove from priority queues
    for (auto& QueuePair : PriorityQueues)
    {
        TArray<FPendingAsyncOperation>& Queue = QueuePair.Value;
        
        for (int32 i = Queue.Num() - 1; i >= 0; --i)
        {
            if (Queue[i].OperationId == OperationId)
            {
                Queue.RemoveAt(i);
                break;
            }
        }
    }
    
    // Update state
    State->Status = EOperationStatus::Cancelled;
    
    // Clean up any associated resources
    FRHIGPUFence** Fence = OperationFences.Find(OperationId);
    if (Fence && *Fence)
    {
        // Add to completed fences for cleanup
        CompletedFences.Add(*Fence);
        OperationFences.Remove(OperationId);
    }
    
    GPU_DISPATCHER_LOG_VERBOSE("Cancelled async operation %lld", OperationId);
    
    // Update queue metrics
    UpdateQueueMetrics();
    
    return true;
}

bool FAsyncComputeCoordinator::WaitForCompletion(int64 OperationId, uint32 TimeoutMS)
{
    FScopeLock Lock(&QueueLock);
    
    // Check if operation exists
    FOperationState* State = PendingOperations.Find(OperationId);
    if (!State)
    {
        return false;
    }
    
    // If already completed, return success
    if (State->Status == EOperationStatus::Completed)
    {
        return true;
    }
    
    // If failed or cancelled, return false
    if (State->Status == EOperationStatus::Failed || State->Status == EOperationStatus::Cancelled)
    {
        return false;
    }
    
    // Check if there's a fence for this operation
    FRHIGPUFence** Fence = OperationFences.Find(OperationId);
    if (!Fence || !*Fence)
    {
        // No fence, can't wait
        return false;
    }
    
    // We need to wait for the fence, so release the lock to avoid blocking
    bool bCompleted = false;
    {
        // Save lock pointer and release lock to avoid deadlocks during wait
        FCriticalSection* QueueLockPtr = &QueueLock;
        Lock.Unlock();
        
        // Wait for fence with timeout
        uint32 StartTime = FPlatformTime::Cycles();
        double CyclesPerMsec = FPlatformTime::GetSecondsPerCycle() * 1000.0;
        uint32 EndTime = StartTime + static_cast<uint32>(CyclesPerMsec * TimeoutMS);
        
        while (FPlatformTime::Cycles() < EndTime)
        {
            if (IsFenceComplete(*Fence))
            {
                bCompleted = true;
                break;
            }
            
            // Sleep briefly to avoid busy-waiting
            FPlatformProcess::Sleep(0.001f);
        }
        
        // Reacquire lock with a new scope using the saved pointer
        FScopeLock NewLock(QueueLockPtr);
        
        // Update state if completed
        if (bCompleted)
        {
            State->Status = EOperationStatus::Completed;
            
            // Add fence to completed list for cleanup
            CompletedFences.Add(*Fence);
            OperationFences.Remove(OperationId);
        }
    }
    
    return bCompleted;
}
}

void FAsyncComputeCoordinator::SetQueuePriorities(const TArray<float>& PriorityWeights)
{
    FScopeLock Lock(&QueueLock);
    
    // Ensure the array has enough entries
    if (PriorityWeights.Num() < 5)
    {
        return;
    }
    
    this->PriorityWeights = PriorityWeights;
}

void FAsyncComputeCoordinator::SetFrameBudget(float MaxFrameTimeMS)
{
    FScopeLock Lock(&QueueLock);
    
    // Set frame budget
    FrameBudgetMS = FMath::Max(0.1f, MaxFrameTimeMS);
    
    GPU_DISPATCHER_LOG_VERBOSE("Set async compute frame budget to %.2f ms", FrameBudgetMS);
}

void FAsyncComputeCoordinator::Flush(bool bWaitForCompletion)
{
    FScopeLock Lock(&QueueLock);
    
    // If no async compute support, nothing to flush
    if (!bAsyncComputeSupported)
    {
        return;
    }
    
    // Submit all pending operations
    DispatchPendingOperations();
    
    if (bWaitForCompletion)
    {
        // Wait for all operations to complete
        for (const auto& FencePair : OperationFences)
        {
            if (FencePair.Value)
            {
                {
                    // Release and reacquire lock to avoid deadlocks during fence wait
                    FCriticalSection* QueueLockPtr = &QueueLock;
                    Lock.Unlock();
                    
                    // Wait for fence
                    WaitForFence(FencePair.Value);
                    
                    // Reacquire lock
                    FScopeLock NewLock(QueueLockPtr);
                }
            }
        }
    }
    
    // Clear queues
    for (auto& QueuePair : PriorityQueues)
    {
        QueuePair.Value.Empty();
    }
    
    // Update queue metrics
    UpdateQueueMetrics();
    
    GPU_DISPATCHER_LOG_VERBOSE("Flushed async compute queues, %d operations pending", PendingOperations.Num());
}

int64 FAsyncComputeCoordinator::ScheduleBackgroundOperation(const FComputeOperation& Operation)
{
    // Use AsyncTaskManager to schedule a background task
    FAsyncTaskManager& TaskManager = FAsyncTaskManager::Get();
    
    // Create async operation
    uint64 TaskId = TaskManager.CreateOperation("GPUCompute", "Background SDF Update");
    
    // Set parameters
    TMap<FString, FString> Params;
    Params.Add("OperationType", FString::FromInt(Operation.OperationType));
    Params.Add("Priority", FString::FromInt((int32)Operation.Priority));
    
    // Start operation
    TaskManager.StartOperation(TaskId, Params);
    
    // Schedule at lowest priority
    
    // Create a local copy of TaskManager to avoid capturing 'this' or using a reference
    FAsyncTaskManager* TaskManagerPtr = &TaskManager;
    
    return ScheduleAsyncOperation(Operation, [TaskId, TaskManagerPtr](bool bSuccess) {
        // Mark task as completed when operation finishes
        if (bSuccess) {
            FAsyncResult SuccessResult;
            // In UE5.5, FAsyncResult properties are accessed differently
            SuccessResult.SetSuccess(true);
            SuccessResult.SetCode(0);
            SuccessResult.SetMessage(TEXT("Operation completed successfully"));
            TaskManagerPtr->OnOperationCompleted(TaskId, SuccessResult);
        } else {
            FAsyncResult FailResult;
            // In UE5.5, FAsyncResult properties are accessed differently
            FailResult.SetSuccess(false);
            FailResult.SetCode(1);
            FailResult.SetMessage(TEXT("Operation failed"));
            TaskManagerPtr->OnOperationCompleted(TaskId, FailResult);
        }
    }, EAsyncPriority::Background);
}

void FAsyncComputeCoordinator::RegisterCompletionCallback(int64 OperationId, TFunction<void()> Callback)
{
    FScopeLock Lock(&QueueLock);
    
    // Check if operation exists
    if (!PendingOperations.Contains(OperationId))
    {
        return;
    }
    
    // Register callback
    CompletionCallbacks.Add(OperationId, Callback);
}

void FAsyncComputeCoordinator::ProcessFrame()
{
    // Update frame counter
    FrameCounter++;
    
    // Calculate delta time
    double CurrentTime = FPlatformTime::Seconds();
    double DeltaTime = CurrentTime - LastFrameTime;
    LastFrameTime = CurrentTime;
    
    // Process completed operations
    ProcessCompletedOperations();
    
    // Check for stale operations
    CheckForStaleOperations();
    
    // Dispatch pending operations if possible
    if (CanScheduleMoreOperations())
    {
        DispatchPendingOperations();
    }
    
    // Clean up fences
    CleanupFences();
}

void FAsyncComputeCoordinator::ProcessCompletedOperations()
{
    FScopeLock Lock(&QueueLock);
    
    // Check all operations with fences
    TArray<int64> CompletedOperations;
    
    for (const auto& FencePair : OperationFences)
    {
        int64 OperationId = FencePair.Key;
        FRHIGPUFence* Fence = FencePair.Value;
        
        if (Fence && IsFenceComplete(Fence))
        {
            // Operation completed
            CompletedOperations.Add(OperationId);
            
            // Add fence to completed list for cleanup
            CompletedFences.Add(Fence);
        }
    }
    
    // Update states and invoke callbacks
    for (int64 OperationId : CompletedOperations)
    {
        // Update state
        FOperationState* State = PendingOperations.Find(OperationId);
        if (State)
        {
            State->Status = EOperationStatus::Completed;
        }
        
        // Get callback
        TFunction<void(bool)> Callback;
        auto OpIt = PendingOperations.Find(OperationId);
        if (OpIt)
        {
            // Remove from pending operations
            PendingOperations.Remove(OperationId);
        }
        
        // Get and remove from operation fences
        OperationFences.Remove(OperationId);
        
        // Get completion callback
        TFunction<void()>* CompletionCallback = CompletionCallbacks.Find(OperationId);
        if (CompletionCallback)
        {
            // Copy callback
            TFunction<void()> Callback = *CompletionCallback;
            
            // Remove from map
            CompletionCallbacks.Remove(OperationId);
            
            {
                // Release and reacquire lock to avoid deadlocks when calling callback
                FCriticalSection* QueueLockPtr = &QueueLock;
                Lock.Unlock();
                
                // Invoke callback
                Callback();
                
                // Reacquire lock
                FScopeLock NewLock(QueueLockPtr);
        }
    }
}

bool FAsyncComputeCoordinator::DispatchPendingOperations()
{
    FScopeLock Lock(&QueueLock);
    
    if (!bAsyncComputeSupported)
    {
        return false;
    }
    
    // Ensure we have operations to dispatch
    bool bHasOperations = false;
    for (const auto& QueuePair : PriorityQueues)
    {
        if (QueuePair.Value.Num() > 0)
        {
            bHasOperations = true;
            break;
        }
    }
    
    if (!bHasOperations)
    {
        return false;
    }
    
    // Find highest priority operation to dispatch
    FPendingAsyncOperation* HighestPriorityOp = nullptr;
    EAsyncPriority HighestPriority = EAsyncPriority::Background;
    
    for (auto& QueuePair : PriorityQueues)
    {
        EAsyncPriority Priority = QueuePair.Key;
        TArray<FPendingAsyncOperation>& Queue = QueuePair.Value;
        
        if (Queue.Num() > 0 && Priority < HighestPriority)
        {
            HighestPriority = Priority;
            HighestPriorityOp = &Queue[0];
        }
    }
    
    if (!HighestPriorityOp)
    {
        return false;
    }
    
    // Copy operation data
    FPendingAsyncOperation PendingOp = *HighestPriorityOp;
    
    // Remove from queue
    TArray<FPendingAsyncOperation>& Queue = PriorityQueues.FindChecked(HighestPriority);
    Queue.RemoveAt(0);
    
    // Get operation state
    FOperationState* State = PendingOperations.Find(PendingOp.OperationId);
    if (!State)
    {
        // Operation no longer exists
        return false;
    }
    
    // Update state
    State->Status = EOperationStatus::Running;
    
    // Get callback
    TFunction<void(bool)> Callback = PendingOp.CompletionCallback;
    
    // Create a fence for this operation
    const TCHAR* FenceName = TEXT("AsyncComputeFence");
    FRHIGPUFence* Fence = AddFence(FenceName);
    
    if (Fence)
    {
        // Store fence
        OperationFences.Add(PendingOp.OperationId, Fence);
    }
    
    // Queue dispatch on render thread - avoid capturing 'this'
    // Create local copies
    FPendingAsyncOperation PendingOpCopy = PendingOp;
    TFunction<void(bool)> CallbackCopy = Callback;
    FRHIGPUFence* FenceCopy = Fence;
    
    ENQUEUE_RENDER_COMMAND(DispatchAsyncCompute)(
        [PendingOpCopy, CallbackCopy, FenceCopy](FRHICommandListImmediate& RHICmdList)
        {
            // Execute on the render thread
            bool bSuccess = true; // assume success for now
            
            // TODO: Implement actual execution
            // This would invoke the appropriate compute shader
            
            // Signal fence when done
            if (FenceCopy)
            {
                RHICmdList.WriteGPUFence(FenceCopy);
            }
            
            // If no fence, call completion callback immediately
            if (!FenceCopy && CallbackCopy)
            {
                CallbackCopy(bSuccess);
            }
        });
    
    // Update queue metrics
    UpdateQueueMetrics();
    
    return true;
}

bool FAsyncComputeCoordinator::IsQueueFull(EAsyncPriority Priority) const
{
    const TArray<FPendingAsyncOperation>* Queue = PriorityQueues.Find(Priority);
    if (!Queue)
    {
        return false;
    }
    
    // Queue capacity depends on priority
    uint32 MaxQueueSize = 0;
    
    switch (Priority)
    {
        case EAsyncPriority::Critical:
            MaxQueueSize = 10;
            break;
        case EAsyncPriority::High:
            MaxQueueSize = 20;
            break;
        case EAsyncPriority::Normal:
            MaxQueueSize = 50;
            break;
        case EAsyncPriority::Low:
            MaxQueueSize = 100;
            break;
        case EAsyncPriority::Background:
            MaxQueueSize = 200;
            break;
        default:
            MaxQueueSize = 50;
            break;
    }
    
    return Queue->Num() >= MaxQueueSize;
}

void FAsyncComputeCoordinator::UpdateQueueMetrics()
{
    // Calculate total utilization
    int32 TotalOperations = 0;
    int32 TotalCapacity = 0;
    
    for (const auto& QueuePair : PriorityQueues)
    {
        EAsyncPriority Priority = QueuePair.Key;
        const TArray<FPendingAsyncOperation>& Queue = QueuePair.Value;
        
        TotalOperations += Queue.Num();
        
        // Calculate capacity for this priority
        uint32 MaxQueueSize = 0;
        
        switch (Priority)
        {
            case EAsyncPriority::Critical:
                MaxQueueSize = 10;
                break;
            case EAsyncPriority::High:
                MaxQueueSize = 20;
                break;
            case EAsyncPriority::Normal:
                MaxQueueSize = 50;
                break;
            case EAsyncPriority::Low:
                MaxQueueSize = 100;
                break;
            case EAsyncPriority::Background:
                MaxQueueSize = 200;
                break;
            default:
                MaxQueueSize = 50;
                break;
        }
        
        TotalCapacity += MaxQueueSize;
    }
    
    // Calculate utilization
    QueueUtilization = TotalCapacity > 0 ? (float)TotalOperations / (float)TotalCapacity : 0.0f;
}

bool FAsyncComputeCoordinator::CanScheduleMoreOperations() const
{
    // Check if we're over budget
    return QueueUtilization < 0.8f;
}

FRHIGPUFence* FAsyncComputeCoordinator::AddFence(const TCHAR* Name)
{
    // Create a fence
    return RHICreateGPUFence(Name);
}

bool FAsyncComputeCoordinator::IsFenceComplete(FRHIGPUFence* Fence) const
{
    if (!Fence)
    {
        return true;
    }
    
    // Check if fence is complete
    return Fence->Poll();
}

void FAsyncComputeCoordinator::WaitForFence(FRHIGPUFence* Fence)
{
    if (!Fence)
    {
        return;
    }
    
    // Wait for fence
    while (!Fence->Poll())
    {
        // Sleep briefly to avoid busy-waiting
        FPlatformProcess::Sleep(0.001f);
    }
}

void FAsyncComputeCoordinator::CleanupFences()
{
    FScopeLock Lock(&QueueLock);
    
    // Clean up completed fences
    CompletedFences.Empty();
}

void FAsyncComputeCoordinator::CheckForStaleOperations()
{
    FScopeLock Lock(&QueueLock);
    
    // Check for operations that have timed out
    TArray<int64> TimedOutOperations;
    
    for (const auto& OpPair : PendingOperations)
    {
        const FOperationState& State = OpPair.Value;
        
        // Skip operations that are not running
        if (State.Status != EOperationStatus::Running)
        {
            continue;
        }
        
        // Check if operation has a fence
        FRHIGPUFence** Fence = OperationFences.Find(OpPair.Key);
        if (!Fence || !*Fence)
        {
            continue;
        }
        
        // Check if fence is stale
        // We consider an operation stale if it's running but not completed after a certain number of frames
        if (FrameCounter - State.LastFrameAccessed > TimeoutFrames)
        {
            TimedOutOperations.Add(OpPair.Key);
        }
    }
    
    // Handle timed out operations
    for (int64 OperationId : TimedOutOperations)
    {
        // Update state
        FOperationState* State = PendingOperations.Find(OperationId);
        if (State)
        {
            State->Status = EOperationStatus::Failed;
            State->ErrorType = EComputeErrorType::Timeout;
            State->ErrorMessage = TEXT("Operation timed out");
        }
        
        // Get callback
        TFunction<void(bool)> Callback;
        auto OpIt = PendingOperations.Find(OperationId);
        if (OpIt && OpIt->CompletionCallback)
        {
            // Copy callback
            Callback = OpIt->CompletionCallback;
            
            // Remove from pending operations
            PendingOperations.Remove(OperationId);
        }
        
        // Clean up fence
        FRHIGPUFence** Fence = OperationFences.Find(OperationId);
        if (Fence && *Fence)
        {
            // Add to completed fences for cleanup
            CompletedFences.Add(*Fence);
            OperationFences.Remove(OperationId);
        }
        
        // Call completion callback with failure
        if (Callback)
        {
            // Save lock pointer and release lock to avoid deadlocks during callback
            FCriticalSection* QueueLockPtr = &QueueLock;
            Lock.Unlock();
            
            // Invoke callback with failure
            Callback(false);
            
            // Reacquire lock 
            FScopeLock NewLock(QueueLockPtr);
        }
        
        GPU_DISPATCHER_LOG_WARNING("Async operation %lld timed out", OperationId);
    }
}

FRHIComputeCommandList* FAsyncComputeCoordinator::GetCommandList()
{
    // In a real implementation, this would get a command list from the RHI
    // For simplicity, we'll return nullptr
    return nullptr;
}