# Service Registry and Dependency Integration Guide for System 13 GPU Compute Dispatcher

This comprehensive guide details how to integrate the GPU Compute Dispatcher (System 13) with the project's Service Registry and Dependency framework, ensuring proper communication between systems and efficient resource utilization.

## 1. Service Registry Architecture Overview

The project employs a sophisticated service registry and dependency framework with these key components:

### Core Components

- **IServiceLocator**: Central interface for service registration and resolution
  - `RegisterService()`: Registers a service with the locator
  - `ResolveService()`: Resolves a service instance based on type and context
  - `UnregisterService()`: Removes a service from the registry
  - `DeclareDependency()`: Declares a dependency between services

- **FServiceLocator**: Implementation of IServiceLocator
  - Supports hierarchical resolution (zone → region → global)
  - Thread-safe service registration and resolution
  - Fast-path optimization for high-frequency services
  - Thread-local caching for performance
  - NUMA-aware resolution for optimal hardware utilization

- **FServiceManager**: Manages service lifecycle
  - `RegisterService()`: Registers a service with configuration
  - `StartService()`: Initializes a service
  - `StopService()`: Shuts down a service
  - `RestartService()`: Restarts a service with state preservation
  - `RecordServiceMetrics()`: Records service performance metrics

- **FDependencyResolver**: Manages dependencies between services
  - `RegisterDependency()`: Creates dependencies between nodes
  - `RegisterConditionalDependency()`: Creates conditional dependencies
  - `RegisterHardwareDependency()`: Creates hardware-dependent dependencies
  - `DetermineInitializationOrder()`: Orders services for initialization

- **FServiceHealthMonitor**: Monitors service health
  - Tracks service metrics and status
  - Implements recovery strategies
  - Predictive failure detection
  - Automatic health state management

- **FServiceDebugVisualizer**: Provides debugging visualization
  - Visualizes service dependencies and status
  - Records service interactions
  - Identifies performance hotspots
  - Generates DOT, JSON, or text visualizations

### Service States and Metrics

- **EServiceState**: Defines service lifecycle states
  - `Uninitialized`, `Initializing`, `Active`, `Failing`, `ShuttingDown`, `Destroyed`

- **FServiceMetrics**: Tracks service performance data
  - `SuccessfulOperations`, `FailedOperations`, `TotalOperationTimeMs`
  - `MaxOperationTimeMs`, `MemoryUsageBytes`, `ActiveInstances`

- **FServiceHealth**: Tracks service health information
  - `Status`, `DiagnosticMessage`, `ErrorCount`, `WarningCount`

## 2. System 13 Dependencies and Interfaces

### Critical Dependencies

System 13 (GPU Compute Dispatcher) has these critical dependencies:
- **System 1 (Core Registry)**: For service registration and type management
- **System 2 (Memory Management)**: For memory allocation and zero-copy buffers
- **System 3 (Threading)**: For task scheduling and asynchronous compute

And is a dependency for:
- **System 25 (SVO+SDF Volume)**: Uses compute operations for volume updates
- **System 26 (Multi-Channel Distance Field)**: Uses compute operations for field updates
- **System 44 (Mining Operations)**: Uses compute operations for mining simulations

### IComputeDispatcher Interface

The main interface that must be implemented:

```cpp
// IComputeDispatcher.h
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ComputeOperationTypes.h"

UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UComputeDispatcher : public UInterface
{
    GENERATED_BODY()
};

class MININGSPICECOPILOT_API IComputeDispatcher
{
    GENERATED_BODY()

public:
    /**
     * Dispatches a compute operation
     * @param Operation Operation to dispatch
     * @return True if dispatch was successful, false otherwise
     */
    virtual bool DispatchCompute(const FComputeOperation& Operation) = 0;
    
    /**
     * Batches multiple operations for more efficient processing
     * @param Operations Array of operations to batch
     * @return True if batching was successful, false otherwise
     */
    virtual bool BatchOperations(const TArray<FComputeOperation>& Operations) = 0;
    
    /**
     * Cancels an in-progress operation
     * @param OperationId ID of the operation to cancel
     * @return True if operation was canceled, false if not found or already completed
     */
    virtual bool CancelOperation(uint64 OperationId) = 0;
    
    /**
     * Queries the status of an operation
     * @param OperationId ID of the operation to query
     * @param OutStatus Operation status information
     * @return True if operation was found, false otherwise
     */
    virtual bool QueryOperationStatus(uint64 OperationId, FOperationStatus& OutStatus) = 0;
    
    /**
     * Gets the compute capabilities of the current system
     * @return Structure containing hardware capabilities
     */
    virtual FComputeCapabilities GetCapabilities() const = 0;
};
```

### IWorkloadDistributor Interface

```cpp
// IWorkloadDistributor.h
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ComputeOperationTypes.h"

UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UWorkloadDistributor : public UInterface
{
    GENERATED_BODY()
};

class MININGSPICECOPILOT_API IWorkloadDistributor
{
    GENERATED_BODY()

public:
    /**
     * Determines the processing target for an operation
     * @param Operation Operation to evaluate
     * @return Target processor (CPU or GPU)
     */
    virtual EProcessingTarget DetermineProcessingTarget(const FComputeOperation& Operation) = 0;
    
    /**
     * Updates performance metrics for learning
     * @param Metrics Operation performance metrics
     */
    virtual void UpdatePerformanceMetrics(const FOperationMetrics& Metrics) = 0;
    
    /**
     * Resets metrics and learning data
     */
    virtual void ResetMetrics() = 0;
};
```

## 3. Service Registration and Initialization

### Service Registration

```cpp
// Register the GPUDispatcher with the service locator
bool FGPUDispatcher::RegisterWithServiceLocator()
{
    // Register as IComputeDispatcher
    IServiceLocator::Get().RegisterService<IComputeDispatcher>(this);
    
    // Declare dependencies
    IServiceLocator::Get().DeclareDependency<IComputeDispatcher, IMemoryManager>(EServiceDependencyType::Required);
    IServiceLocator::Get().DeclareDependency<IComputeDispatcher, ITaskScheduler>(EServiceDependencyType::Required);
    IServiceLocator::Get().DeclareDependency<IComputeDispatcher, FSDFTypeRegistry>(EServiceDependencyType::Required);
    
    return true;
}
```

### Initialization Sequence

```cpp
bool FGPUDispatcher::Initialize()
{
    // Create necessary components
    HardwareProfileManager = MakeShared<FHardwareProfileManager>();
    WorkloadDistributor = MakeShared<FWorkloadDistributor>();
    KernelManager = MakeShared<FSDFComputeKernelManager>();
    AsyncComputeCoordinator = MakeShared<FAsyncComputeCoordinator>();
    
    // Initialize hardware profile manager
    if (!HardwareProfileManager->DetectHardwareCapabilities())
    {
        UE_LOG(LogGPUDispatcher, Error, TEXT("Failed to detect hardware capabilities"));
        return false;
    }
    
    // Initialize workload distributor with detected hardware profile
    if (!WorkloadDistributor->Initialize(HardwareProfileManager->GetCurrentProfile()))
    {
        UE_LOG(LogGPUDispatcher, Error, TEXT("Failed to initialize workload distributor"));
        return false;
    }
    
    // Initialize kernel manager
    if (!KernelManager->PrecompileCommonKernels())
    {
        UE_LOG(LogGPUDispatcher, Warning, TEXT("Failed to precompile common kernels"));
        // Continue anyway, will compile on demand
    }
    
    // Initialize async compute coordinator
    bool bSupportsAsyncCompute = HardwareProfileManager->GetCurrentProfile().bSupportsAsyncCompute;
    if (!AsyncComputeCoordinator->Initialize(bSupportsAsyncCompute))
    {
        UE_LOG(LogGPUDispatcher, Warning, TEXT("Failed to initialize async compute coordinator"));
        // Continue anyway, will fall back to sync compute
    }
    
    // Load configuration
    LoadConfigSettings();
    
    // Register with service locator
    if (!RegisterWithServiceLocator())
    {
        UE_LOG(LogGPUDispatcher, Error, TEXT("Failed to register with service locator"));
        return false;
    }
    
    // Initialize Zero-Copy Buffers
    if (!InitializeZeroCopyBuffers())
    {
        UE_LOG(LogGPUDispatcher, Warning, TEXT("Failed to initialize zero-copy buffers, falling back to standard buffers"));
        // Continue anyway, using standard buffers
    }
    
    // Configure SDF operations based on GPU capabilities
    if (!ConfigureSDFOperationsForGPU())
    {
        UE_LOG(LogGPUDispatcher, Warning, TEXT("Failed to configure SDF operations for GPU"));
        // Continue anyway, will fall back to CPU
    }
    
    bIsInitialized = true;
    return true;
}
```

## 4. Service Resolution and Usage

### Resolving the GPU Compute Dispatcher

Other systems will resolve the GPU Compute Dispatcher like this:

```cpp
// Get compute dispatcher from service locator
IComputeDispatcher* ComputeDispatcher = IServiceLocator::Get().ResolveService<IComputeDispatcher>();
if (ComputeDispatcher)
{
    // Use the dispatcher
    FComputeOperation Operation;
    Operation.OperationType = ESDFOperationType::Union;
    Operation.Bounds = FBox(FVector(-100, -100, -100), FVector(100, 100, 100));
    // ... set other operation parameters
    
    ComputeDispatcher->DispatchCompute(Operation);
}
```

### Resolving Dependent Services

```cpp
// In FGPUDispatcher.cpp
bool FGPUDispatcher::DispatchSDFOperation(ESDFOperationType OpType, const FBox& Bounds, 
    const TArray<FRDGBufferRef>& InputBuffers, FRDGBufferRef OutputBuffer)
{
    // Resolve SDF Type Registry
    FSDFTypeRegistry* Registry = static_cast<FSDFTypeRegistry*>(
        IServiceLocator::Get().ResolveService(USDFTypeRegistry::StaticClass()));
    
    if (!Registry)
    {
        UE_LOG(LogGPUDispatcher, Error, TEXT("Cannot dispatch SDF operation: Type registry not available"));
        return false;
    }
    
    // Check if operation is GPU compatible
    if (!Registry->IsOperationGPUCompatible(OpType))
    {
        UE_LOG(LogGPUDispatcher, Verbose, TEXT("SDF operation %d is not GPU compatible, falling back to CPU"),
            static_cast<int32>(OpType));
        return false;
    }
    
    // Get optimal thread block size for this operation
    uint32 BlockSize = Registry->GetOptimalThreadBlockSize(OpType);
    
    // Get compute kernel for this operation
    FSDFComputeKernel* Kernel = KernelManager->GetBestKernelForOperation(OpType, Operation);
    if (!Kernel)
    {
        UE_LOG(LogGPUDispatcher, Warning, TEXT("No suitable kernel found for SDF operation %d"),
            static_cast<int32>(OpType));
        return false;
    }
    
    // Dispatch compute shader
    // ... implementation details
    
    return true;
}
```

## 5. Memory Management and Zero-Copy Integration

```cpp
// In FGPUDispatcher.cpp
bool FGPUDispatcher::InitializeZeroCopyBuffers()
{
    // Get memory manager
    IMemoryManager* MemoryManager = IServiceLocator::Get().ResolveService<IMemoryManager>();
    if (!MemoryManager)
    {
        UE_LOG(LogGPUDispatcher, Error, TEXT("Cannot initialize zero-copy buffers: Memory manager not available"));
        return false;
    }
    
    // Create zero-copy buffer manager
    ZeroCopyManager = MakeShared<FZeroCopyResourceManager>();
    
    // Request memory pools for SDF operations
    FMemoryPoolSpec PoolSpec;
    PoolSpec.PoolName = FName("ComputeZeroCopyPool");
    PoolSpec.ElementSize = 65536; // 64KB chunks
    PoolSpec.ElementCount = 256;  // 16MB total
    PoolSpec.Alignment = 256;     // GPU cache line alignment
    PoolSpec.AllocationStrategy = EAllocationStrategy::ThreadSafe;
    PoolSpec.ZeroMemory = false;  // No need to zero for compute
    
    MemoryManager->CreatePool(PoolSpec);
    
    // Create staging buffer for transfers
    FRHIResourceCreateInfo CreateInfo(TEXT("ComputeStaging"));
    StagingBuffer = RHICreateVertexBuffer(
        16 * 1024 * 1024,         // 16MB staging buffer
        BUF_ShaderResource | BUF_SourceCopy | BUF_Dynamic,
        CreateInfo);
    
    UE_LOG(LogGPUDispatcher, Log, TEXT("Zero-copy buffers initialized successfully"));
    return true;
}

// Example usage of zero-copy buffers
void* FGPUDispatcher::PinMemoryForGPU(void* CPUAddress, SIZE_T Size, uint32& OutBufferIndex)
{
    if (!ZeroCopyManager)
    {
        return nullptr;
    }
    
    return ZeroCopyManager->PinMemory(CPUAddress, Size, OutBufferIndex);
}

FRHIGPUBufferReadback* FGPUDispatcher::GetGPUBuffer(uint32 BufferIndex)
{
    if (!ZeroCopyManager)
    {
        return nullptr;
    }
    
    return ZeroCopyManager->GetGPUBuffer(BufferIndex);
}

void FGPUDispatcher::ReleaseMemory(uint32 BufferIndex)
{
    if (ZeroCopyManager)
    {
        ZeroCopyManager->ReleaseMemory(BufferIndex);
    }
}
```

## 6. RDG (Render Dependency Graph) Integration

```cpp
// In FGPUDispatcher.cpp
void FGPUDispatcher::ExecuteComputePass(FRDGBuilder& GraphBuilder, const FComputeShaderMetadata& ShaderMetadata, const FDispatchParameters& Params)
{
    // Create compute pass
    FRDGComputePassParameters* PassParameters = GraphBuilder.AllocParameters<FRDGComputePassParameters>();
    
    // Track resource states and ensure proper transitions
    for (const auto& ResourcePair : Params.Resources)
    {
        FRHIResource* Resource = ResourcePair.Key;
        const FResourceState& State = ResourcePair.Value;
        
        // Get current state
        FResourceState* CurrentState = ResourceStateMap.Find(Resource);
        if (!CurrentState)
        {
            // Initialize state tracking for this resource
            FResourceState NewState;
            NewState.CurrentAccess = ERHIAccess::SRVMask;
            NewState.CurrentPipeline = ERHIPipeline::Graphics;
            NewState.LastFrameAccessed = GFrameCounter;
            ResourceStateMap.Add(Resource, NewState);
            CurrentState = ResourceStateMap.Find(Resource);
        }
        
        // Determine if transition is needed
        bool bNeedsTransition = CurrentState->CurrentAccess != State.CurrentAccess ||
                                CurrentState->CurrentPipeline != State.CurrentPipeline;
        
        if (bNeedsTransition)
        {
            // Add resource transition
            GraphBuilder.AddPass(
                RDG_EVENT_NAME("ResourceTransition"),
                ERDGPassFlags::None,
                [Resource, CurrentState, State](FRHICommandListImmediate& RHICmdList)
                {
                    RHICmdList.Transition(FRHITransitionInfo(
                        Resource, 
                        CurrentState->CurrentAccess, 
                        State.CurrentAccess, 
                        CurrentState->CurrentPipeline, 
                        State.CurrentPipeline));
                    
                    // Update current state
                    *CurrentState = State;
                });
        }
    }
    
    // Execute the compute pass with proper resource transitions
    GraphBuilder.AddComputePass(
        TEXT("SDFOperation"),
        PassParameters,
        ERDGPassFlags::Compute,
        [this, PassParameters, ShaderMetadata, Params](FRHIComputeCommandList& RHICmdList) {
            // Set compute shader
            TShaderMapRef<FSDFComputeShader> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
            RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());
            
            // Set shader parameters
            FSDFComputeShader::FParameters* ShaderParams = GraphBuilder.AllocParameters<FSDFComputeShader::FParameters>();
            // ... setup parameters
            
            SetShaderParameters(RHICmdList, ComputeShader, ShaderParams, Params);
            
            // Dispatch compute shader
            RHICmdList.DispatchComputeShader(
                Params.ThreadGroupCountX, 
                Params.ThreadGroupCountY, 
                Params.ThreadGroupCountZ);
            
            // Unbind shader parameters
            UnsetShaderParameters(RHICmdList, ComputeShader, ShaderParams);
        });
}
```

## 7. Health Monitoring Integration

```cpp
// In FGPUDispatcher.cpp
bool FGPUDispatcher::QueryOperationStatus(uint64 OperationId, FOperationStatus& OutStatus)
{
    FScopeLock Lock(&OperationLock);
    
    FOperationState* State = ActiveOperations.Find(OperationId);
    if (!State)
    {
        return false;
    }
    
    OutStatus.OperationId = OperationId;
    OutStatus.Status = State->Status;
    OutStatus.Progress = State->Progress;
    OutStatus.ExecutionTimeMs = State->ExecutionTimeMs;
    
    // Report critical failures to service health monitor
    if (State->Status == EOperationStatus::Failed)
    {
        // Get service health monitor
        IServiceLocator& Locator = IServiceLocator::Get();
        FServiceHealthMonitor* HealthMonitor = 
            static_cast<FServiceHealthMonitor*>(Locator.ResolveService(UServiceHealthMonitor::StaticClass()));
        
        if (HealthMonitor)
        {
            // Record operation failure
            HealthMonitor->RecordServiceOperation(
                GetServiceName(),
                false,  // operation failed
                State->ExecutionTimeMs,
                EServiceFailureReason::ResourceExhaustion,
                State->ErrorMessage);
        }
    }
    
    return true;
}

void FGPUDispatcher::UpdatePerformanceMetrics(const FOperationMetrics& Metrics)
{
    // Record operation metrics
    if (Metrics.SuccessfulExecution)
    {
        SuccessfulOperations.Increment();
    }
    else
    {
        FailedOperations.Increment();
    }
    
    // Update GPU utilization metrics
    float CurrentUtilization = Metrics.DeviceUtilization;
    if (CurrentUtilization > 0.0f)
    {
        // Use exponential moving average to smooth utilization
        const float Alpha = 0.1f;
        AverageGPUUtilization = Alpha * CurrentUtilization + (1.0f - Alpha) * AverageGPUUtilization;
    }
    
    // Update performance history for learning
    PerformanceHistory.Add(Metrics);
    if (PerformanceHistory.Num() > MaxHistoryEntries)
    {
        PerformanceHistory.RemoveAt(0);
    }
    
    // Notify WorkloadDistributor
    WorkloadDistributor->UpdatePerformanceMetrics(Metrics);
}
```

## 8. Implementing Required Interfaces

### IMemoryAwareService Implementation

```cpp
// In FGPUDispatcher.h
class FGPUDispatcher : public IComputeDispatcher, public IMemoryAwareService
{
public:
    // IMemoryAwareService interface
    virtual uint64 GetMemoryUsage() const override;
    virtual bool TrimMemory(uint64 TargetUsageBytes) override;
    
    // ... other methods
};

// In FGPUDispatcher.cpp
uint64 FGPUDispatcher::GetMemoryUsage() const
{
    // Calculate total memory usage across all GPU resources
    uint64 TotalMemory = 0;
    
    // Add buffer memory
    if (ZeroCopyManager)
    {
        TotalMemory += ZeroCopyManager->GetTotalAllocatedMemory();
    }
    
    // Add shader memory
    if (KernelManager)
    {
        TotalMemory += KernelManager->GetTotalShaderMemoryUsage();
    }
    
    // Add operation tracking memory
    TotalMemory += ActiveOperations.GetAllocatedSize();
    
    // Add performance history memory
    TotalMemory += PerformanceHistory.GetAllocatedSize();
    
    return TotalMemory;
}

bool FGPUDispatcher::TrimMemory(uint64 TargetUsageBytes)
{
    uint64 CurrentUsage = GetMemoryUsage();
    if (CurrentUsage <= TargetUsageBytes)
    {
        return true; // Already below target
    }
    
    // Try to trim various caches
    
    // First, trim kernel manager caches
    if (KernelManager)
    {
        KernelManager->PurgeUnusedKernels();
    }
    
    // Trim performance history if needed
    if (CurrentUsage > TargetUsageBytes && PerformanceHistory.Num() > 10)
    {
        // Keep only the 10 most recent entries
        PerformanceHistory.RemoveAt(10, PerformanceHistory.Num() - 10);
    }
    
    // Trim resource state map for old entries
    for (auto It = ResourceStateMap.CreateIterator(); It; ++It)
    {
        if (It.Value().LastFrameAccessed < GFrameCounter - 60) // Remove if not used in last 60 frames
        {
            It.RemoveCurrent();
        }
    }
    
    // Report current usage after trimming
    return GetMemoryUsage() <= TargetUsageBytes;
}
```

### ISaveableService Implementation

```cpp
// In FGPUDispatcher.h
class FGPUDispatcher : public IComputeDispatcher, 
                       public IMemoryAwareService,
                       public ISaveableService
{
public:
    // ISaveableService interface
    virtual bool SaveState(TArray<uint8>& OutState) override;
    virtual bool RestoreState(const TArray<uint8>& InState) override;
    
    // ... other methods
};

// In FGPUDispatcher.cpp
bool FGPUDispatcher::SaveState(TArray<uint8>& OutState)
{
    // Save critical state information
    FMemoryWriter Writer(OutState);
    
    // Save version for compatibility
    const uint32 StateVersion = 1;
    Writer << StateVersion;
    
    // Save hardware profile
    Writer << HardwareProfileManager->GetCurrentProfile();
    
    // Save active operations count
    const int32 ActiveOpsCount = ActiveOperations.Num();
    Writer << ActiveOpsCount;
    
    // Save critical workload distribution settings
    Writer << WorkloadDistributor->GetDistributionConfig();
    
    return true;
}

bool FGPUDispatcher::RestoreState(const TArray<uint8>& InState)
{
    if (InState.Num() == 0)
    {
        return false;
    }
    
    FMemoryReader Reader(InState);
    
    // Read and validate version
    uint32 StateVersion;
    Reader << StateVersion;
    if (StateVersion != 1)
    {
        UE_LOG(LogGPUDispatcher, Warning, TEXT("Incompatible state version %u, expected 1"), StateVersion);
        return false;
    }
    
    // Restore hardware profile
    FHardwareProfile Profile;
    Reader << Profile;
    HardwareProfileManager->LoadProfile(Profile);
    
    // Restore active operations count (for metrics)
    int32 ActiveOpsCount;
    Reader << ActiveOpsCount;
    
    // Restore workload distribution settings
    FDistributionConfig Config;
    Reader << Config;
    WorkloadDistributor->SetDistributionConfig(Config);
    
    return true;
}
```

## 9. Asynchronous Compute Integration

```cpp
// In FGPUDispatcher.h
bool DispatchComputeAsync(const FComputeOperation& Operation, TFunction<void(bool, float)> CompletionCallback);

// In FGPUDispatcher.cpp
bool FGPUDispatcher::DispatchComputeAsync(const FComputeOperation& Operation, TFunction<void(bool, float)> CompletionCallback)
{
    // Determine if this should be processed on CPU or GPU
    EProcessingTarget Target = WorkloadDistributor->DetermineProcessingTarget(Operation);
    
    if (Target == EProcessingTarget::GPU)
    {
        // Create operation state
        uint64 OperationId = NextOperationId.Increment();
        
        FScopeLock Lock(&OperationLock);
        FOperationState& State = ActiveOperations.Add(OperationId);
        State.OperationId = OperationId;
        State.Status = EOperationStatus::Pending;
        State.CompletionCallback = CompletionCallback;
        State.StartTime = FPlatformTime::Seconds();
        
        // Submit to async compute coordinator
        AsyncComputeCoordinator->ScheduleAsyncOperation(
            Operation, 
            [this, OperationId](bool bSuccess) {
                OnAsyncOperationComplete(OperationId, bSuccess);
            },
            GetOperationPriority(Operation));
        
        return true;
    }
    else
    {
        // Process on CPU now
        double StartTime = FPlatformTime::Seconds();
        bool bSuccess = ProcessOnCPU(Operation);
        double EndTime = FPlatformTime::Seconds();
        float ElapsedMs = (EndTime - StartTime) * 1000.0f;
        
        // Invoke callback immediately
        if (CompletionCallback)
        {
            CompletionCallback(bSuccess, ElapsedMs);
        }
        
        return bSuccess;
    }
}

void FGPUDispatcher::OnAsyncOperationComplete(uint64 OperationId, bool bSuccess)
{
    FOperationState* State = nullptr;
    TFunction<void(bool, float)> CompletionCallback;
    
    {
        FScopeLock Lock(&OperationLock);
        
        State = ActiveOperations.Find(OperationId);
        if (!State)
        {
            return;
        }
        
        // Update operation state
        State->Status = bSuccess ? EOperationStatus::Completed : EOperationStatus::Failed;
        State->EndTime = FPlatformTime::Seconds();
        State->ExecutionTimeMs = (State->EndTime - State->StartTime) * 1000.0f;
        
        // Capture callback to invoke outside of lock
        CompletionCallback = State->CompletionCallback;
    }
    
    // Invoke completion callback
    if (CompletionCallback)
    {
        CompletionCallback(bSuccess, State->ExecutionTimeMs);
    }
    
    // Update metrics
    FOperationMetrics Metrics;
    Metrics.OperationTypeId = State->OperationTypeId;
    Metrics.CPUExecutionTimeMS = 0.0f;
    Metrics.GPUExecutionTimeMS = State->ExecutionTimeMs;
    Metrics.DataSize = State->DataSize;
    Metrics.IterationCount = 1;
    Metrics.DeviceUtilization = GetCurrentGPUUtilization();
    
    UpdatePerformanceMetrics(Metrics);
}
```

## 10. Hardware Profile Management

```cpp
// In FHardwareProfileManager.cpp
bool FHardwareProfileManager::DetectHardwareCapabilities()
{
    // Clear previous data
    SupportedExtensions.Empty();
    
    // Get RHI info
    FString RHIName = GDynamicRHI->GetName();
    
    // Get GPU info
    RHIGetGPUInfo(GPUName, GPUVendor, ComputeUnits, TotalVRAM);
    
    // Check for vendor-specific features
    switch (GPUVendor)
    {
        case EGPUVendor::AMD:
            SupportedExtensions.Add(TEXT("GL_AMD_shader_ballot"));
            SupportedExtensions.Add(TEXT("GL_AMD_gpu_shader_half_float"));
            bSupportsRayTracing = GRHISupportsRayTracing && ComputeUnits >= 36;
            bSupportsWaveIntrinsics = true;
            WavefrontSize = 64;
            break;
            
        case EGPUVendor::NVIDIA:
            SupportedExtensions.Add(TEXT("GL_NV_shader_thread_group"));
            SupportedExtensions.Add(TEXT("GL_NV_shader_atomic_float"));
            bSupportsRayTracing = GRHISupportsRayTracing && TotalVRAM > 6*1024*1024*1024ull;
            bSupportsWaveIntrinsics = true;
            WavefrontSize = 32;
            break;
            
        case EGPUVendor::Intel:
            bSupportsRayTracing = false;
            bSupportsWaveIntrinsics = false;
            WavefrontSize = 16;
            break;
            
        default:
            bSupportsRayTracing = false;
            bSupportsWaveIntrinsics = false;
            WavefrontSize = 32;
            break;
    }
    
    // Detect shared memory size
    SharedMemoryBytes = 32 * 1024; // Default 32KB
    
    // Check for async compute support
    bSupportsAsyncCompute = GRHISupportsAsyncCompute;
    
    // Log hardware info
    UE_LOG(LogGPUDispatcher, Log, TEXT("GPU: %s, Vendor: %s, CUs: %u, VRAM: %u MB"), 
        *GPUName, *GetVendorString(GPUVendor), ComputeUnits, TotalVRAM / (1024*1024));
    UE_LOG(LogGPUDispatcher, Log, TEXT("Async Compute: %s, Ray Tracing: %s, Wave Intrinsics: %s"),
        bSupportsAsyncCompute ? TEXT("Supported") : TEXT("Not Supported"),
        bSupportsRayTracing ? TEXT("Supported") : TEXT("Not Supported"),
        bSupportsWaveIntrinsics ? TEXT("Supported") : TEXT("Not Supported"));
    
    // Create compatible profile
    CurrentProfile.bSupportsRayTracing = bSupportsRayTracing;
    CurrentProfile.bSupportsAsyncCompute = bSupportsAsyncCompute;
    CurrentProfile.ComputeUnits = ComputeUnits;
    CurrentProfile.WavefrontSize = WavefrontSize;
    CurrentProfile.bSupportsWaveIntrinsics = bSupportsWaveIntrinsics;
    CurrentProfile.SharedMemoryBytes = SharedMemoryBytes;
    CurrentProfile.VendorId = GPUVendor;
    CurrentProfile.DeviceName = GPUName;
    
    return true;
}
```

## 11. Workload Distribution Optimization

```cpp
// In FWorkloadDistributor.cpp
EProcessingTarget FWorkloadDistributor::DetermineProcessingTarget(const FComputeOperation& Operation)
{
    // Calculate operation complexity
    float Complexity = CalculateOperationComplexity(Operation);
    
    // Consider operation size
    float OperationSize = Operation.Bounds.GetVolume();
    
    // Check if this is a narrow-band operation
    bool bIsNarrowBand = IsNarrowBandOperation(Operation.Bounds, Operation.MaterialChannelId);
    
    // Consider current GPU utilization
    float CurrentGPUUtilization = GetCurrentGPUUtilization();
    
    // Use historical performance data
    float PredictedGPUTime = PredictGPUPerformance(Operation);
    float PredictedCPUTime = PredictCPUPerformance(Operation);
    
    // Make the decision
    if (PredictedGPUTime < PredictedCPUTime * 0.8f && CurrentGPUUtilization < 0.9f)
    {
        return EProcessingTarget::GPU;
    }
    else if (bIsNarrowBand && OperationSize < 100000.0f)
    {
        return EProcessingTarget::CPU;
    }
    else if (Complexity > 100.0f)
    {
        return EProcessingTarget::GPU;
    }
    
    return EProcessingTarget::CPU;
}
```

## 12. Debug Visualization Integration

```cpp
// In FGPUDispatcher.cpp
void FGPUDispatcher::LogOperationCompletion(const FComputeOperation& Operation, bool bSuccess, float DurationMs)
{
    // Log operation completion
    UE_LOG(LogGPUDispatcher, Verbose, TEXT("Operation %llu %s in %.2f ms"), 
        Operation.OperationId, bSuccess ? TEXT("completed") : TEXT("failed"), DurationMs);
    
    // Get debug visualizer
    IServiceLocator& Locator = IServiceLocator::Get();
    FServiceDebugVisualizer* Visualizer = 
        static_cast<FServiceDebugVisualizer*>(Locator.ResolveService(UServiceDebugVisualizer::StaticClass()));
    
    if (Visualizer)
    {
        // Record service interaction
        FName SourceKey = FName(*FString::Printf(TEXT("GPUDispatcher")));
        FName TargetKey = FName(*FString::Printf(TEXT("RHI")));
        
        Visualizer->RecordServiceInteraction(SourceKey, TargetKey, DurationMs, bSuccess);
    }
}
```

## 13. Service Configuration and Dynamic Optimization

```cpp
// In FGPUDispatcher.cpp
void FGPUDispatcher::LoadConfigSettings()
{
    // Get configuration values from project settings or INI
    bool bEnableAutotuning = true;
    float CPUAffinityForLowOps = 0.8f;
    float GPUAffinityForBatchedOps = 0.9f;
    
    // Load from project config
    GConfig->GetBool(TEXT("GPUDispatcher"), TEXT("EnableAutotuning"), bEnableAutotuning, GEngineIni);
    GConfig->GetFloat(TEXT("GPUDispatcher"), TEXT("CPUAffinityForLowOps"), CPUAffinityForLowOps, GEngineIni);
    GConfig->GetFloat(TEXT("GPUDispatcher"), TEXT("GPUAffinityForBatchedOps"), GPUAffinityForBatchedOps, GEngineIni);
    
    // Apply configuration
    FDistributionConfig Config;
    Config.bEnableAutotuning = bEnableAutotuning;
    Config.CPUAffinityForLowOperationCount = CPUAffinityForLowOps;
    Config.GPUAffinityForBatchedOperations = GPUAffinityForBatchedOps;
    
    // Apply to workload distributor
    if (WorkloadDistributor)
    {
        WorkloadDistributor->SetDistributionConfig(Config);
    }
}
```

## 14. Thread Safety Implementation

The service registry system uses these thread safety mechanisms:

- **FScopeLock**: For critical section protection
- **FMiningReaderWriterLock**: For read-heavy scenarios
- **FThreadSafeBool/Counter**: For atomic operations
- **FNUMAOptimizedSpinLock**: For NUMA-aware locking

GPU Compute Dispatcher should implement appropriate thread safety:

```cpp
// In FGPUDispatcher.h
class FGPUDispatcher : public IComputeDispatcher, public IMemoryAwareService, public ISaveableService
{
    // ... other members
    
private:
    // Thread safety
    FCriticalSection OperationLock;
    FThreadSafeCounter64 NextOperationId;
    FThreadSafeBool bIsInitialized;
};

// In Dispatcher operations
bool FGPUDispatcher::DispatchCompute(const FComputeOperation& Operation)
{
    FScopeLock Lock(&OperationLock);
    
    // Generate operation ID
    uint64 OperationId = NextOperationId.Increment();
    
    // ... dispatch logic
    
    return true;
}
```

## 15. Type Registry Integration for SDF Operations

```cpp
// In FGPUDispatcher.cpp
bool FGPUDispatcher::ConfigureSDFOperationsForGPU()
{
    // Get SDF type registry
    FSDFTypeRegistry* Registry = static_cast<FSDFTypeRegistry*>(
        IServiceLocator::Get().ResolveService(USDFTypeRegistry::StaticClass()));
    
    if (!Registry)
    {
        UE_LOG(LogGPUDispatcher, Error, TEXT("Cannot configure SDF operations: Type registry not available"));
        return false;
    }
    
    // Get current hardware profile
    const FHardwareProfile& Profile = HardwareProfileManager->GetCurrentProfile();
    
    // Configure operations based on hardware capabilities
    for (const FSDFOperationInfo& OperationInfo : Registry->GetAllOperations())
    {
        bool bGPUCompatible = DetermineGPUCompatibility(OperationInfo.OperationType, Profile);
        
        // Register optimal dispatch configuration
        FSDFOperationConfig Config;
        Config.bGPUCompatible = bGPUCompatible;
        Config.OptimalThreadBlockSize = DetermineOptimalBlockSize(OperationInfo.OperationType, Profile);
        Config.PreferredDispatchStrategy = DetermineDispatchStrategy(OperationInfo.OperationType, Profile);
        
        Registry->SetOperationConfig(OperationInfo.OperationType, Config);
        
        UE_LOG(LogGPUDispatcher, Verbose, TEXT("Configured SDF operation %s for GPU: %s"),
            *OperationInfo.OperationName.ToString(),
            Config.bGPUCompatible ? TEXT("Compatible") : TEXT("Not Compatible"));
    }
    
    return true;
}
```

## 16. Implementation Checklist

1. **Service Registration**:
   - Register main service after all components are initialized
   - Declare all dependencies explicitly
   - Check for dependency services during operations

2. **Thread Safety**:
   - Use appropriate locking for shared state
   - Use atomic operations for counters when possible
   - Consider lock-free approaches for performance-critical paths

3. **Resource Management**:
   - Implement proper cleanup in destructor
   - Handle resource state transitions correctly
   - Provide memory usage reporting

4. **Error Handling**:
   - Always check for service resolution success
   - Implement fallback paths when services are missing
   - Report errors to the health monitoring system

5. **Performance Optimization**:
   - Use fast paths for common operations
   - Implement batching for similar operations
   - Consider NUMA locality for CPU operations

6. **Health Monitoring**:
   - Record operation success/failure
   - Track performance metrics
   - Report detailed error information

7. **State Preservation**:
   - Implement ISaveableService interface
   - Save critical configuration settings
   - Handle version compatibility

8. **Resource State Tracking**:
   - Track RHI resource states
   - Minimize state transitions
   - Use RDG for automatic resource handling

This guide provides all the necessary information for implementing the GPU Compute Dispatcher (System 13) with proper integration into the service registry and dependency systems of the project.
