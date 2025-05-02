#pragma once

#include "CoreMinimal.h"
#include "ComputeOperationTypes.h"

// Forward declarations for simplified RHI-free implementation
class FSimulatedGPUFence;
class FSimulatedComputeCommandList;

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
    int64 ScheduleAsyncOperation(const FComputeOperation& Operation, 
                                TFunction<void(bool)> CompletionCallback,
                                EAsyncPriority Priority = EAsyncPriority::Normal);
    bool CancelAsyncOperation(int64 OperationId);
    bool WaitForCompletion(int64 OperationId, uint32 TimeoutMS = ~0u);
    
    // Queue management
    void SetQueuePriorities(const TArray<float>& PriorityWeights);
    void SetFrameBudget(float MaxFrameTimeMS);
    void Flush(bool bWaitForCompletion);
    
    // Scheduling methods
    int64 ScheduleBackgroundOperation(const FComputeOperation& Operation);
    void RegisterCompletionCallback(int64 OperationId, TFunction<void()> Callback);
    
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
    FSimulatedGPUFence* AddFence(const TCHAR* Name);
    bool IsFenceComplete(FSimulatedGPUFence* Fence) const;
    void WaitForFence(FSimulatedGPUFence* Fence);
    void CleanupFences();
    
    // Resource management
    void CheckForStaleOperations();
    FSimulatedComputeCommandList* GetCommandList();
    
    // Member variables
    bool bAsyncComputeSupported;
    TMap<EAsyncPriority, TArray<FPendingAsyncOperation>> PriorityQueues;
    TMap<int64, FOperationState> PendingOperations;
    TMap<int64, TFunction<void()>> CompletionCallbacks;
    TMap<int64, FSimulatedGPUFence*> OperationFences;
    TArray<FSimulatedGPUFence*> CompletedFences;
    float FrameBudgetMS;
    FCriticalSection QueueLock;
    FThreadSafeCounter64 NextOperationId;
    float QueueUtilization;
    TArray<float> PriorityWeights;
    double LastFrameTime;
    uint32 FrameCounter;
    uint32 TimeoutFrames;
};