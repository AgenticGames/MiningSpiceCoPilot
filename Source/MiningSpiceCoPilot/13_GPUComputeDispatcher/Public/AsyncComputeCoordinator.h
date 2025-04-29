#pragma once

#include "CoreMinimal.h"
#include "RHICommandList.h"
#include "13_GPUComputeDispatcher/Public/ComputeOperationTypes.h"

/**
 * Asynchronous compute scheduling for non-critical SDF updates
 * Manages queue priorities and dependencies for efficient GPU utilization
 * Coordinates with AsyncTaskManager for background processing
 */
class MININGSPICECOPILOT_API FAsyncComputeCoordinator
{
public:
    FAsyncComputeCoordinator();
    ~FAsyncComputeCoordinator();
    
    // Initialization
    bool Initialize(bool bSupportsAsyncCompute);
    
    // Async compute scheduling
    uint64 ScheduleAsyncOperation(const FComputeOperation& Operation, 
                                TFunction<void(bool)> CompletionCallback,
                                EAsyncPriority Priority = EAsyncPriority::Normal);
    bool CancelAsyncOperation(uint64 OperationId);
    bool WaitForCompletion(uint64 OperationId, uint32 TimeoutMS = ~0u);
    
    // Queue management
    void SetQueuePriorities(const TArray<float>& PriorityWeights);
    void SetFrameBudget(float MaxFrameTimeMS);
    void Flush(bool bWaitForCompletion);
    
    // Scheduling methods
    uint64 ScheduleBackgroundOperation(const FComputeOperation& Operation);
    void RegisterCompletionCallback(uint64 OperationId, TFunction<void()> Callback);
    
    // Frame update
    void ProcessFrame();
    
private:
    // Internal command list management
    void ProcessCompletedOperations();
    bool DispatchPendingOperations();
    bool IsQueueFull(EAsyncPriority Priority) const;
    void UpdateQueueMetrics();
    bool CanScheduleMoreOperations() const;
    
    // Fence management
    FRHIGPUFence* AddFence(const TCHAR* Name);
    bool IsFenceComplete(FRHIGPUFence* Fence) const;
    void WaitForFence(FRHIGPUFence* Fence);
    void CleanupFences();
    
    // Resource management
    void CheckForStaleOperations();
    FRHIComputeCommandList* GetCommandList();
    
    // Member variables
    bool bAsyncComputeSupported;
    TMap<EAsyncPriority, TArray<FPendingAsyncOperation>> PriorityQueues;
    TMap<uint64, FOperationState> PendingOperations;
    TMap<uint64, TFunction<void()>> CompletionCallbacks;
    TMap<uint64, FRHIGPUFence*> OperationFences;
    TArray<FRHIGPUFence*> CompletedFences;
    float FrameBudgetMS;
    FCriticalSection QueueLock;
    FThreadSafeCounter64 NextOperationId;
    float QueueUtilization;
    TArray<float> PriorityWeights;
    double LastFrameTime;
    uint32 FrameCounter;
    uint32 TimeoutFrames;
};