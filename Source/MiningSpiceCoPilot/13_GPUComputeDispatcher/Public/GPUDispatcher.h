#pragma once

#include "CoreMinimal.h"
#include "Containers/CircularBuffer.h"
#include "RenderGraphResources.h"
#include "RenderGraphBuilder.h"
#include "RHIGPUReadback.h"
#include "Interfaces/IComputeDispatcher.h"
#include "Interfaces/IWorkloadDistributor.h"
#include "../../6_ServiceRegistryandDependency/Public/Interfaces/IMemoryAwareService.h"
#include "../../6_ServiceRegistryandDependency/Public/Interfaces/ISaveableService.h"

class FHardwareProfileManager;
class FSDFComputeKernelManager;
class FAsyncComputeCoordinator;
class FZeroCopyResourceManager;

/**
 * Main GPU compute dispatcher implementation
 * Manages compute shader dispatch for SDF operations
 * with adaptive CPU/GPU workload balancing
 */
class MININGSPICECOPILOT_API FGPUDispatcher : public IComputeDispatcher, 
                                             public IMemoryAwareService,
                                             public ISaveableService
{
public:
    FGPUDispatcher();
    virtual ~FGPUDispatcher();
    
    //~ Begin IComputeDispatcher Interface
    virtual bool DispatchCompute(const FComputeOperation& Operation) override;
    virtual bool BatchOperations(const TArray<FComputeOperation>& Operations) override;
    virtual bool CancelOperation(int64 OperationId) override;
    virtual bool QueryOperationStatus(int64 OperationId, FOperationStatus& OutStatus) override;
    virtual FComputeCapabilities GetCapabilities() const override;
    virtual bool DispatchComputeAsync(const FComputeOperation& Operation, TFunction<void(bool, float)> CompletionCallback) override;
    virtual bool DispatchSDFOperation(int32 OpType, const FBox& Bounds, 
        const TArray<FRDGBufferRef>& InputBuffers, FRDGBufferRef OutputBuffer) override;
    virtual bool DispatchMaterialOperation(int32 MaterialChannelId, const FBox& Bounds, 
        const TArray<FRDGBufferRef>& InputBuffers, FRDGBufferRef OutputBuffer) override;
    virtual bool FlushOperations(bool bWaitForCompletion) override;
    //~ End IComputeDispatcher Interface
    
    //~ Begin IMemoryAwareService Interface
    virtual uint64 GetMemoryUsage() const override;
    virtual bool TrimMemory(uint64 TargetUsageBytes) override;
    //~ End IMemoryAwareService Interface
    
    //~ Begin ISaveableService Interface
    virtual bool SaveState(TArray<uint8>& OutState) override;
    virtual bool RestoreState(const TArray<uint8>& InState) override;
    //~ End ISaveableService Interface
    
    // Initialization and shutdown
    bool Initialize();
    void Shutdown();
    
    // Registration with service locator
    bool RegisterWithServiceLocator();
    
    // RDG Integration
    void ExecuteComputePass(FRDGBuilder& GraphBuilder, 
                          const FShaderParametersMetadata& ShaderMetadata, 
                          const FDispatchParameters& Params);
                          
    // Zero-copy buffer management
    void* PinMemoryForGPU(void* CPUAddress, SIZE_T Size, uint32& OutBufferIndex);
    FRHIGPUBufferReadback* GetGPUBuffer(uint32 BufferIndex);
    void ReleaseMemory(uint32 BufferIndex);
    
    // Log and debug
    void LogOperationCompletion(const FComputeOperation& Operation, bool bSuccess, float DurationMs);
    
private:
    // Internal implementation methods
    bool InitializeMemoryResources();
    bool ConfigureSDFOperationsForGPU();
    bool InitializeZeroCopyBuffers();
    void LoadConfigSettings();
    void ProcessOnCPU(const FComputeOperation& Operation, TFunction<void(bool, float)> CompletionCallback);
    void UpdatePerformanceMetrics(const FOperationMetrics& Metrics);
    void OnAsyncOperationComplete(int64 OperationId, bool bSuccess);
    float GetCurrentGPUUtilization() const;
    void MonitorMemoryPressure();
    EAsyncPriority GetOperationPriority(const FComputeOperation& Operation) const;
    void ResourceBarrierTracking(FRDGBuilder& GraphBuilder, const TMap<FRHIResource*, FResourceState>& Resources);
    
    // Member variables
    TSharedPtr<FHardwareProfileManager> HardwareProfileManager;
    TSharedPtr<IWorkloadDistributor> WorkloadDistributor;
    TSharedPtr<FSDFComputeKernelManager> KernelManager;
    TSharedPtr<FAsyncComputeCoordinator> AsyncComputeCoordinator;
    
    // Memory system integration
    class IMemoryManager* MemoryManager;
    TMap<uint32, class FZeroCopyBuffer*> MaterialBuffers;
    TMap<uint32, class FNarrowBandAllocator*> NarrowBandAllocators;
    
    // State tracking
    TMap<int64, FOperationState> ActiveOperations;
    FCriticalSection OperationLock;
    FThreadSafeCounter64 NextOperationId;
    FThreadSafeBool bIsInitialized;
    
    // Performance tracking
    TArray<FOperationMetrics> PerformanceHistory;
    static constexpr int32 MaxHistoryEntries = 100;
    float AverageGPUUtilization;
    float CPUToGPUPerformanceRatio;
    FThreadSafeCounter64 SuccessfulOperations;
    FThreadSafeCounter64 FailedOperations;
    
    // Resource management
    TMap<FRHIResource*, FResourceState> ResourceStateMap;
    TSharedPtr<FZeroCopyResourceManager> ZeroCopyManager;
    FRHIBuffer* StagingBuffer;
};