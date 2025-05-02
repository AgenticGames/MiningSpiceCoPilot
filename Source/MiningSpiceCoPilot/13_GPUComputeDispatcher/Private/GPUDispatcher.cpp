#include "../Public/GPUDispatcher.h"
#include "../Public/GPUDispatcherLogging.h"
#include "../Public/HardwareProfileManager.h"
#include "../Public/WorkloadDistributor.h"
#include "../Public/SDFComputeKernelManager.h"
#include "../Public/AsyncComputeCoordinator.h"
#include "../Public/ZeroCopyResourceManager.h"
#include "../Public/SDFShaderParameters.h"
#include "../Public/ComputeShaderUtils.h"
#include "SimulatedGPUBuffer.h" // Include the simulated GPU buffer classes
#include "SerializationHelpers.h" // Include serialization helpers for FMemoryWriter/Reader

#include "../../1_CoreRegistry/Public/CoreServiceLocator.h"
#include "../../1_CoreRegistry/Public/SDFTypeRegistry.h"
#include "../../1_CoreRegistry/Public/TypeRegistry.h"
#include "../../3_ThreadingTaskSystem/Public/Interfaces/ITaskScheduler.h"
#include "../../2_MemoryManagement/Public/Interfaces/IMemoryManager.h"
#include "../../2_MemoryManagement/Public/Interfaces/IBufferProvider.h"
#include "../../2_MemoryManagement/Public/NarrowBandAllocator.h"
#include "../../3_ThreadingTaskSystem/Public/AsyncTaskManager.h"
#include "../../3_ThreadingTaskSystem/Public/Interfaces/ITaskScheduler.h"
#include "../../3_ThreadingTaskSystem/Public/NumaHelpers.h"
#include "../../6_ServiceRegistryandDependency/Public/ServiceLocator.h"
#include "../../6_ServiceRegistryandDependency/Public/ServiceHealthMonitor.h"

// Include necessary headers for configuration settings
#include "HAL/IConsoleManager.h"
#include "Misc/ConfigCacheIni.h"

// Forward declarations for service component classes
class UServiceHealthMonitor;
class UServiceDebugVisualizer;

DEFINE_LOG_CATEGORY(LogGPUDispatcher);

FGPUDispatcher::FGPUDispatcher()
    : AverageGPUUtilization(0.0f)
    , CPUToGPUPerformanceRatio(1.0f)
    , StagingBuffer(nullptr)
{
    // Initialize TArray for performance history
    bIsInitialized.AtomicSet(false);
}

FGPUDispatcher::~FGPUDispatcher()
{
    Shutdown();
}

bool FGPUDispatcher::Initialize()
{
    GPU_DISPATCHER_SCOPED_TIMER(Initialize);
    
    // Create necessary components
    HardwareProfileManager = MakeShared<FHardwareProfileManager>();
    WorkloadDistributor = MakeShared<FWorkloadDistributor>();
    KernelManager = MakeShared<FSDFComputeKernelManager>();
    AsyncComputeCoordinator = MakeShared<FAsyncComputeCoordinator>();
    
    // Initialize hardware profile manager
    if (!HardwareProfileManager->DetectHardwareCapabilities())
    {
        GPU_DISPATCHER_LOG_ERROR("Failed to detect hardware capabilities");
        return false;
    }
    
    // Initialize workload distributor with detected hardware profile
    FDistributionConfig DistConfig;
    DistConfig.bEnableAutotuning = true;
    DistConfig.CPUAffinityForLowOperationCount = 0.8f;
    DistConfig.GPUAffinityForBatchedOperations = 0.9f;
    
    // Apply hardware profile settings to the config
    DistConfig.bDeviceSupportsAsyncCompute = HardwareProfileManager->GetCurrentProfile().bSupportsAsyncCompute;
    DistConfig.DevicePerformanceTier = HardwareProfileManager->GetCurrentProfile().PerformanceTier;
    
    WorkloadDistributor->SetDistributionConfig(DistConfig);
    
    // Initialize kernel manager
    if (!KernelManager->PrecompileCommonKernels())
    {
        GPU_DISPATCHER_LOG_WARNING("Failed to precompile common kernels");
        // Continue anyway, will compile on demand
    }
    
    // Initialize async compute coordinator
    bool bSupportsAsyncCompute = HardwareProfileManager->GetCurrentProfile().bSupportsAsyncCompute;
    if (!AsyncComputeCoordinator->Initialize(bSupportsAsyncCompute))
    {
        GPU_DISPATCHER_LOG_WARNING("Failed to initialize async compute coordinator");
        // Continue anyway, will fall back to sync compute
    }
    
    // Load configuration
    LoadConfigSettings();
    
    // Initialize memory resources
    if (!InitializeMemoryResources())
    {
        GPU_DISPATCHER_LOG_WARNING("Failed to initialize memory resources");
        // Continue anyway with reduced functionality
    }
    
    // Register with service locator
    if (!RegisterWithServiceLocator())
    {
        GPU_DISPATCHER_LOG_ERROR("Failed to register with service locator");
        return false;
    }
    
    // Initialize Zero-Copy Buffers
    if (!InitializeZeroCopyBuffers())
    {
        GPU_DISPATCHER_LOG_WARNING("Failed to initialize zero-copy buffers, falling back to standard buffers");
        // Continue anyway, using standard buffers
    }
    
    // Configure SDF operations based on GPU capabilities
    if (!ConfigureSDFOperationsForGPU())
    {
        GPU_DISPATCHER_LOG_WARNING("Failed to configure SDF operations for GPU");
        // Continue anyway, will fall back to CPU
    }
    
    GPU_DISPATCHER_LOG_DEBUG("GPU Dispatcher initialized successfully");
    bIsInitialized.AtomicSet(true);
    return true;
}

void FGPUDispatcher::Shutdown()
{
    if (!IsInitialized())
    {
        return;
    }
    
    // Flush any pending operations
    FlushOperations(true);
    
    // Release resources
    if (StagingBuffer)
    {
        StagingBuffer = nullptr;
    }
    
    // Clear mapped buffers
    MaterialBuffers.Empty();
    NarrowBandAllocators.Empty();
    
    // Release components
    ZeroCopyManager.Reset();
    AsyncComputeCoordinator.Reset();
    KernelManager.Reset();
    WorkloadDistributor.Reset();
    HardwareProfileManager.Reset();
    
    bIsInitialized.AtomicSet(false);
    GPU_DISPATCHER_LOG_DEBUG("GPU Dispatcher shut down");
}

bool FGPUDispatcher::RegisterWithServiceLocator()
{
    // Get service locator
    IServiceLocator& ServiceLocator = IServiceLocator::Get();
    
    // Register as IComputeDispatcher using the name-based registration method
    // This avoids the template specialization issues
    if (!ServiceLocator.RegisterServiceByTypeName(TEXT("IComputeDispatcher"), this))
    {
        GPU_DISPATCHER_LOG_ERROR("Failed to register as IComputeDispatcher");
        return false;
    }
    
    // Use our simpler name-based dependency declaration approach to avoid template issues
    UE_LOG(LogTemp, Log, TEXT("Registering GPU Dispatcher dependencies"));
    
    // Register dependencies using string names instead of types
    ServiceLocator.DeclareDependencyByName(TEXT("IComputeDispatcher"), TEXT("IMemoryManager"), EServiceDependencyType::Required);
    ServiceLocator.DeclareDependencyByName(TEXT("IComputeDispatcher"), TEXT("ITaskScheduler"), EServiceDependencyType::Required);
    ServiceLocator.DeclareDependencyByName(TEXT("IComputeDispatcher"), TEXT("FSDFTypeRegistry"), EServiceDependencyType::Required);
    
    GPU_DISPATCHER_LOG_DEBUG("Dependencies registered successfully");
    
    // Dependency on FSDFTypeRegistry - this isn't a UClass, so we can skip it
    GPU_DISPATCHER_LOG_DEBUG("Registered service dependencies successfully");
    
    GPU_DISPATCHER_LOG_DEBUG("Registered service dependencies successfully");
    
    GPU_DISPATCHER_LOG_DEBUG("Registered with service locator");
    return true;
}

bool FGPUDispatcher::InitializeMemoryResources()
{
    // Get memory manager
    MemoryManager = IServiceLocator::Get().ResolveService<IMemoryManager>();
    if (!MemoryManager)
    {
        GPU_DISPATCHER_LOG_ERROR("Failed to resolve memory manager");
        return false;
    }
    
    // Get critical memory pools
    TArray<FName> PoolNames = MemoryManager->GetPoolNames();
    for (const FName& PoolName : PoolNames)
    {
        if (PoolName.ToString().Contains("NarrowBand") || PoolName.ToString().Contains("NBPool"))
        {
            FNarrowBandAllocator* NBAllocator = static_cast<FNarrowBandAllocator*>(
                MemoryManager->GetPool(PoolName));
                
            if (NBAllocator)
            {
                // Store for later use - use the hash of the pool name as the key
                uint32 PoolNameHash = GetTypeHash(NBAllocator->GetPoolName());
                NarrowBandAllocators.Add(PoolNameHash, NBAllocator);
                GPU_DISPATCHER_LOG_VERBOSE("Found narrow band allocator: %s", *NBAllocator->GetPoolName().ToString());
            }
        }
    }
    
    // Get SDF type registry
    FSDFTypeRegistry* Registry = IServiceLocator::Get().ResolveService<FSDFTypeRegistry>();
    if (!Registry)
    {
        GPU_DISPATCHER_LOG_WARNING("Failed to resolve SDF type registry");
        return true; // Non-fatal
    }
    
    // Create buffers for material fields (simplified version - no RHI)
    
    return true;
}

bool FGPUDispatcher::InitializeZeroCopyBuffers()
{
    // Create zero-copy resource manager
    ZeroCopyManager = MakeShared<FZeroCopyResourceManager>();
    if (!ZeroCopyManager->Initialize())
    {
        GPU_DISPATCHER_LOG_ERROR("Failed to initialize zero-copy resource manager");
        return false;
    }
    
    // Create staging buffer for transfers (simplified - no RHI)
    GPU_DISPATCHER_LOG_DEBUG("Using simplified zero-copy buffer implementation");
    
    GPU_DISPATCHER_LOG_DEBUG("Zero-copy buffers initialized successfully");
    return true;
}

bool FGPUDispatcher::ConfigureSDFOperationsForGPU()
{
    // Get SDF type registry
    FSDFTypeRegistry* Registry = IServiceLocator::Get().ResolveService<FSDFTypeRegistry>();
    if (!Registry)
    {
        GPU_DISPATCHER_LOG_ERROR("Cannot configure SDF operations: Type registry not available");
        return false;
    }
    
    // Get current hardware profile
    const FHardwareProfile& Profile = HardwareProfileManager->GetCurrentProfile();
    
    // Get all operation types from the registry
    TArray<int32> OperationTypes;
    TArray<FSDFOperationInfo> Operations = Registry->GetAllOperations();
    for (const FSDFOperationInfo& OpInfo : Operations)
    {
        OperationTypes.Add(static_cast<int32>(OpInfo.OperationType));
    }
    for (int32 OpType : OperationTypes)
    {
        // Determine if this operation can run on GPU
        ESDFOperationType OpEnum = static_cast<ESDFOperationType>(OpType);
        bool bGPUCompatible = Registry->IsOperationGPUCompatible(OpEnum);
        
        if (bGPUCompatible)
        {
            // Get optimal block size based on hardware
            uint32 OptimalBlockSize = HardwareProfileManager->GetOptimalBlockSizeForOperation(OpType);
            
            GPU_DISPATCHER_LOG_VERBOSE("Configured SDF operation %d for GPU: Compatible, BlockSize=%u",
                OpType, OptimalBlockSize);
        }
        else
        {
            GPU_DISPATCHER_LOG_VERBOSE("SDF operation %d is not GPU compatible", OpType);
        }
    }
    
    return true;
}

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

bool FGPUDispatcher::DispatchCompute(const FComputeOperation& Operation)
{
    if (!IsInitialized())
    {
        GPU_DISPATCHER_LOG_ERROR("Cannot dispatch compute: Not initialized");
        return false;
    }
    
    GPU_DISPATCHER_SCOPED_TIMER(DispatchCompute);
    
    // Determine if this should be processed on CPU or GPU
    EProcessingTarget Target = WorkloadDistributor->DetermineProcessingTarget(Operation);
    
    if (Target == EProcessingTarget::GPU)
    {
        // Create operation state
        int64 OperationId = static_cast<int64>(NextOperationId.Increment());
        
        FComputeOperation ModifiedOperation = Operation;
        ModifiedOperation.OperationId = OperationId;
        
        FScopeLock Lock(&OperationLock);
        FOperationState& State = ActiveOperations.Add(OperationId);
        State.OperationId = OperationId;
        State.Status = EOperationStatus::Running;
        State.StartTime = FPlatformTime::Seconds();
        State.OperationTypeId = Operation.OperationTypeId;
        State.DataSize = CalculateOperationDataSize(ModifiedOperation);
        
        // Process on GPU synchronously - simplified implementation without RHI
        bool bSuccess = ProcessOnGPU(ModifiedOperation);
        
        // Update operation state
        State.Status = bSuccess ? EOperationStatus::Completed : EOperationStatus::Failed;
        State.EndTime = FPlatformTime::Seconds();
        State.ExecutionTimeMs = (State.EndTime - State.StartTime) * 1000.0f;
        
        // Record metrics
        FOperationMetrics Metrics;
        Metrics.OperationTypeId = State.OperationTypeId;
        Metrics.CPUExecutionTimeMS = 0.0f;
        Metrics.GPUExecutionTimeMS = State.ExecutionTimeMs;
        Metrics.DataSize = State.DataSize;
        Metrics.IterationCount = 1;
        Metrics.DeviceUtilization = GetCurrentGPUUtilization();
        Metrics.SuccessfulExecution = bSuccess;
        
        UpdatePerformanceMetrics(Metrics);
        
        return bSuccess;
    }
    else if (Target == EProcessingTarget::Hybrid)
    {
        // Split operation between CPU and GPU
        TArray<FComputeOperation> SplitOperations;
        if (WorkloadDistributor->SplitOperation(Operation, SplitOperations))
        {
            // Process split operations
            bool bAllSucceeded = true;
            for (const FComputeOperation& SplitOp : SplitOperations)
            {
                bAllSucceeded &= DispatchCompute(SplitOp);
            }
            return bAllSucceeded;
        }
        else
        {
            // Fall back to CPU if split failed
            ProcessOnCPU(Operation, nullptr);
            return true;
        }
    }
    else
    {
        // Process on CPU
        ProcessOnCPU(Operation, nullptr);
        return true;
    }
}

bool FGPUDispatcher::BatchOperations(const TArray<FComputeOperation>& Operations)
{
    if (!IsInitialized() || Operations.Num() == 0)
    {
        return false;
    }
    
    GPU_DISPATCHER_SCOPED_TIMER(BatchOperations);
    
    // Create batches for similar operations
    TArray<FOperationBatch> Batches;
    if (!WorkloadDistributor->MergeOperations(Operations, Batches))
    {
        // Fall back to individual processing if batching failed
        bool bAllSucceeded = true;
        for (const FComputeOperation& Op : Operations)
        {
            bAllSucceeded &= DispatchCompute(Op);
        }
        return bAllSucceeded;
    }
    
    // Process each batch
    bool bAllSucceeded = true;
    for (const FOperationBatch& Batch : Batches)
    {
        // Create a single operation for the batch
        FComputeOperation BatchOperation;
        BatchOperation.OperationTypeId = Batch.OperationTypeId;
        BatchOperation.OperationType = Batch.OperationTypeId; // Same as type ID for this case
        
        // Use wide execution strategy if specified
        if (Batch.bUseWideExecutionStrategy)
        {
            BatchOperation.bUseNarrowBand = false;
        }
        
        // Combine bounds of all operations in the batch
        FBox CombinedBounds(ForceInit);
        for (const FBox& Box : Batch.Regions)
        {
            CombinedBounds += Box;
        }
        BatchOperation.Bounds = CombinedBounds;
        
        // Dispatch the batch operation
        bAllSucceeded &= DispatchCompute(BatchOperation);
    }
    
    return bAllSucceeded;
}

bool FGPUDispatcher::CancelOperation(int64 OperationId)
{
    if (!IsInitialized())
    {
        return false;
    }
    
    FScopeLock Lock(&OperationLock);
    
    FOperationState* State = ActiveOperations.Find(OperationId);
    if (!State)
    {
        return false;
    }
    
    // If the operation is still pending or running, try to cancel it
    if (State->Status == EOperationStatus::Pending || State->Status == EOperationStatus::Running)
    {
        // If it's an async operation, try to cancel it through the coordinator
        bool bCancelled = AsyncComputeCoordinator->CancelAsyncOperation(OperationId);
        
        if (bCancelled)
        {
            State->Status = EOperationStatus::Cancelled;
            State->EndTime = FPlatformTime::Seconds();
            State->ExecutionTimeMs = (State->EndTime - State->StartTime) * 1000.0f;
            return true;
        }
    }
    
    return false;
}

bool FGPUDispatcher::QueryOperationStatus(int64 OperationId, FOperationStatus& OutStatus)
{
    if (!IsInitialized())
    {
        return false;
    }
    
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
    OutStatus.ErrorType = State->ErrorType;
    OutStatus.ErrorMessage = State->ErrorMessage;
    
    // Report critical failures to service health monitor
    if (State->Status == EOperationStatus::Failed)
    {
        // Get service health monitor
        IServiceLocator& Locator = IServiceLocator::Get();
        FServiceHealthMonitor* HealthMonitor = 
            Locator.ResolveService<FServiceHealthMonitor>();
        
        if (HealthMonitor)
        {
            // Record operation failure using correct enum value
            HealthMonitor->RecordServiceOperation(
                "GPUDispatcher",
                false,  // operation failed
                State->ExecutionTimeMs,
                EServiceFailureReason::ResourceExhaustion,
                State->ErrorMessage);
        }
    }
    
    return true;
}

FComputeCapabilities FGPUDispatcher::GetCapabilities() const
{
    FComputeCapabilities Capabilities;
    
    if (HardwareProfileManager)
    {
        Capabilities.HardwareProfile = HardwareProfileManager->GetCurrentProfile();
    }
    
    // Set basic capabilities (simplified implementation)
    Capabilities.bSupportsComputeShaders = true;
    
    // Set maximum dispatch sizes
    Capabilities.MaxDispatchSizeX = 65535;
    Capabilities.MaxDispatchSizeY = 65535;
    Capabilities.MaxDispatchSizeZ = 65535;
    
    // Set maximum shared memory size
    Capabilities.MaxSharedMemorySize = 32768; // 32KB by default
    
    // Add supported shader formats - simplified
    Capabilities.SupportedShaderFormats.Add("SF_METAL_SM5");
    Capabilities.SupportedShaderFormats.Add("SF_VULKAN_SM5");
    
    return Capabilities;
}

bool FGPUDispatcher::DispatchComputeAsync(const FComputeOperation& Operation, TFunction<void(bool, float)> CompletionCallback)
{
    if (!IsInitialized())
    {
        if (CompletionCallback)
        {
            CompletionCallback(false, 0.0f);
        }
        return false;
    }
    
    // Determine if this should be processed on CPU or GPU
    EProcessingTarget Target = WorkloadDistributor->DetermineProcessingTarget(Operation);
    
    if (Target == EProcessingTarget::GPU)
    {
        // Create operation state
        int64 OperationId = static_cast<int64>(NextOperationId.Increment());
        
        FComputeOperation ModifiedOperation = Operation;
        ModifiedOperation.OperationId = OperationId;
        
        {
            FScopeLock Lock(&OperationLock);
            FOperationState& State = ActiveOperations.Add(OperationId);
            State.OperationId = OperationId;
            State.Status = EOperationStatus::Pending;
            State.CompletionCallback = CompletionCallback;
            State.StartTime = FPlatformTime::Seconds();
            State.OperationTypeId = Operation.OperationTypeId;
            State.DataSize = CalculateOperationDataSize(ModifiedOperation);
        }
        
        // Submit to async compute coordinator with fixed approach to avoid casting issues
        FGPUDispatcher* Dispatcher = this;
        AsyncComputeCoordinator->ScheduleAsyncOperation(
            ModifiedOperation, 
            [Dispatcher, OperationId](bool bSuccess) {
                Dispatcher->OnAsyncOperationComplete(OperationId, bSuccess);
            },
            GetOperationPriority(ModifiedOperation));
        
        return true;
    }
    else
    {
        // Process on CPU
        ProcessOnCPU(Operation, CompletionCallback);
        return true;
    }
}

bool FGPUDispatcher::DispatchSDFOperation(int32 OpType, const FBox& Bounds, 
                                       const TArray<FRDGBufferRef>& InputBuffers, 
                                       FRDGBufferRef OutputBuffer)
{
    if (!IsInitialized())
    {
        return false;
    }
    
    // Create a compute operation for this SDF operation
    FComputeOperation Operation;
    Operation.OperationType = OpType;
    Operation.OperationTypeId = static_cast<uint32>(OpType);
    Operation.Bounds = Bounds;
    
    // Get SDF Type Registry
    FSDFTypeRegistry* Registry = IServiceLocator::Get().ResolveService<FSDFTypeRegistry>();
    if (!Registry)
    {
        GPU_DISPATCHER_LOG_ERROR("Cannot dispatch SDF operation: Type registry not available");
        return false;
    }
    
    // Check if operation is GPU compatible
    ESDFOperationType OpEnum = static_cast<ESDFOperationType>(OpType);
    if (!Registry->IsOperationGPUCompatible(OpEnum))
    {
        GPU_DISPATCHER_LOG_VERBOSE("SDF operation %d is not GPU compatible, falling back to CPU", OpType);
        
        // Execute on CPU through the SDF system
        // This part would typically call into the SDF system's CPU implementation
        return false;
    }
    
    // Determine processing target
    EProcessingTarget Target = WorkloadDistributor->DetermineProcessingTarget(Operation);
    
    if (Target != EProcessingTarget::GPU)
    {
        // If not targeting GPU, fallback to CPU
        GPU_DISPATCHER_LOG_VERBOSE("SDF operation %d assigned to CPU by distributor", OpType);
        return false;
    }
    
    // Add input buffers and output buffer to the operation
    Operation.InputData.SetNum(InputBuffers.Num());
    for (int32 i = 0; i < InputBuffers.Num(); ++i)
    {
        // Store buffer references in the operation for simplified implementation 
        Operation.CustomData.Add(FName(*FString::Printf(TEXT("InputBuffer%d"), i)), 
            TSharedPtr<void>(static_cast<void*>(new FRDGBufferRef(InputBuffers[i]))));
    }
    
    // Store output buffer reference 
    Operation.CustomData.Add(FName(TEXT("OutputBuffer")), 
        TSharedPtr<void>(static_cast<void*>(new FRDGBufferRef(OutputBuffer))));
    
    // Get compute kernel for this operation
    FSDFComputeKernel* Kernel = KernelManager->FindBestKernelForOperation(OpType, Operation);
    if (!Kernel)
    {
        GPU_DISPATCHER_LOG_WARNING("No suitable kernel found for SDF operation %d", OpType);
        return false;
    }
    
    // Process the operation
    return ProcessOnGPU(Operation);
}

bool FGPUDispatcher::DispatchMaterialOperation(int32 MaterialChannelId, const FBox& Bounds, 
                                            const TArray<FRDGBufferRef>& InputBuffers, 
                                            FRDGBufferRef OutputBuffer)
{
    if (!IsInitialized())
    {
        return false;
    }
    
    // Create a compute operation for this material operation
    FComputeOperation Operation;
    Operation.MaterialChannelId = MaterialChannelId;
    Operation.Bounds = Bounds;
    
    // For material operations, we'll set the operation type based on the material channel
    // Using ESDFShaderOperation::MaterialBlend as the default operation type for material operations
    Operation.OperationType = static_cast<int32>(ESDFShaderOperation::MaterialBlend);
    Operation.OperationTypeId = static_cast<int32>(ESDFShaderOperation::MaterialBlend);
    
    // Determine processing target
    EProcessingTarget Target = WorkloadDistributor->DetermineProcessingTarget(Operation);
    
    if (Target != EProcessingTarget::GPU)
    {
        // If not targeting GPU, fallback to CPU
        GPU_DISPATCHER_LOG_VERBOSE("Material operation on channel %d assigned to CPU by distributor", MaterialChannelId);
        return false;
    }
    
    // Add input buffers and output buffer to the operation
    Operation.InputData.SetNum(InputBuffers.Num());
    for (int32 i = 0; i < InputBuffers.Num(); ++i)
    {
        // Store buffer references in the operation for simplified implementation 
        Operation.CustomData.Add(FName(*FString::Printf(TEXT("InputBuffer%d"), i)), 
            TSharedPtr<void>(static_cast<void*>(new FRDGBufferRef(InputBuffers[i]))));
    }
    
    // Store output buffer reference
    Operation.CustomData.Add(FName(TEXT("OutputBuffer")), 
        TSharedPtr<void>(static_cast<void*>(new FRDGBufferRef(OutputBuffer))));
    
    // Get compute kernel for this operation
    FSDFComputeKernel* Kernel = KernelManager->FindBestKernelForOperation(Operation.OperationType, Operation);
    if (!Kernel)
    {
        GPU_DISPATCHER_LOG_WARNING("No suitable kernel found for material operation on channel %d", MaterialChannelId);
        return false;
    }
    
    // Process the operation
    return ProcessOnGPU(Operation);
}

bool FGPUDispatcher::FlushOperations(bool bWaitForCompletion)
{
    if (!IsInitialized())
    {
        return false;
    }
    
    // Flush async compute operations
    AsyncComputeCoordinator->Flush(bWaitForCompletion);
    
    return true;
}

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
    
    // Add staging buffer memory - simplified without RHI
    TotalMemory += 16 * 1024 * 1024; // Assume 16MB for staging buffer
    
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
    
    // Trim resource state map for old entries
    ResourceStateMap.Empty();
    
    // Clean up operation history
    {
        FScopeLock Lock(&OperationLock);
        
        // Remove completed operations older than 60 seconds
        double CurrentTime = FPlatformTime::Seconds();
        for (auto It = ActiveOperations.CreateIterator(); It; ++It)
        {
            const FOperationState& State = It.Value();
            if ((State.Status == EOperationStatus::Completed || 
                 State.Status == EOperationStatus::Failed ||
                 State.Status == EOperationStatus::Cancelled) &&
                 (CurrentTime - State.EndTime > 60.0))
            {
                It.RemoveCurrent();
            }
        }
    }
    
    // Report current usage after trimming
    return GetMemoryUsage() <= TargetUsageBytes;
}

bool FGPUDispatcher::SaveState(TArray<uint8>& OutState)
{
    // Take lock here to ensure consistent state while saving
    FScopeLock Lock(&OperationLock);
    
    // Save critical state information
    FMemoryWriter Writer(OutState);
    
    // Save version for compatibility
    const uint32 StateVersion = 1;
    Writer << StateVersion;
    
    // Save hardware profile
    FHardwareProfile Profile = HardwareProfileManager->GetCurrentProfile();
    WriteString(Writer, Profile.DeviceName);
    
    // Save VendorId as uint32
    uint32 VendorIdCasted = static_cast<uint32>(Profile.VendorId);
    Writer << VendorIdCasted;
    
    // Save ComputeUnits
    Writer << Profile.ComputeUnits;
    
    // Save active operations count
    const int32 ActiveOpsCount = ActiveOperations.Num();
    Writer << ActiveOpsCount;
    
    // Save performance metrics
    Writer << AverageGPUUtilization;
    Writer << CPUToGPUPerformanceRatio;
    
    // Get and save atomic counter values
    uint64 SuccessfulOps = SuccessfulOperations.GetValue();
    uint64 FailedOps = FailedOperations.GetValue();
    Writer << SuccessfulOps;
    Writer << FailedOps;
    
    // Save distribution config
    FDistributionConfig Config = WorkloadDistributor->GetDistributionConfig();
    Writer << Config.bEnableAutotuning;
    Writer << Config.CPUAffinityForLowOperationCount;
    Writer << Config.GPUAffinityForBatchedOperations;
    
    return true;
}

bool FGPUDispatcher::RestoreState(const TArray<uint8>& InState)
{
    // Take lock here to ensure consistent state during restoration
    FScopeLock Lock(&OperationLock);
    
    if (InState.Num() == 0)
    {
        return false;
    }
    
    FMemoryReader Reader(const_cast<TArray<uint8>&>(InState));
    
    // Read and validate version
    uint32 StateVersion;
    Reader >> StateVersion;
    if (StateVersion != 1)
    {
        UE_LOG(LogTemp, Warning, TEXT("Incompatible state version %u, expected 1"), StateVersion);
        return false;
    }
    
    // Read hardware profile info (just for validation)
    FString DeviceName;
    uint32 VendorId;
    uint32 ComputeUnits;
    
    ReadString(Reader, DeviceName);
    Reader >> VendorId;
    Reader >> ComputeUnits;
    
    // Check if hardware matches
    const FHardwareProfile& CurrentProfile = HardwareProfileManager->GetCurrentProfile();
    uint32 CurrentVendorId = static_cast<uint32>(CurrentProfile.VendorId);
    if (DeviceName != CurrentProfile.DeviceName || 
        VendorId != CurrentVendorId ||
        ComputeUnits != CurrentProfile.ComputeUnits)
    {
        UE_LOG(LogTemp, Warning, TEXT("Hardware profile mismatch, saved state may not be optimal"));
    }
    
    // Read active operations count (for metrics)
    int32 ActiveOpsCount;
    Reader >> ActiveOpsCount;
    
    // Read performance metrics
    float AvgGPUUtil, CPUToGPURatio;
    Reader >> AvgGPUUtil;
    Reader >> CPUToGPURatio;
    AverageGPUUtilization = AvgGPUUtil;
    CPUToGPUPerformanceRatio = CPUToGPURatio;
    
    uint64 SuccessfulOps, FailedOps;
    Reader >> SuccessfulOps;
    Reader >> FailedOps;
    
    SuccessfulOperations.Set(SuccessfulOps);
    FailedOperations.Set(FailedOps);
    
    // Read distribution config
    FDistributionConfig Config;
    bool EnableAutotuning;
    float CPUAffinity, GPUAffinity;
    Reader >> EnableAutotuning;
    Reader >> CPUAffinity;
    Reader >> GPUAffinity;
    
    Config.bEnableAutotuning = EnableAutotuning;
    Config.CPUAffinityForLowOperationCount = CPUAffinity;
    Config.GPUAffinityForBatchedOperations = GPUAffinity;
    
    WorkloadDistributor->SetDistributionConfig(Config);
    
    return true;
}

void* FGPUDispatcher::PinMemoryForGPU(void* CPUAddress, SIZE_T Size, uint32& OutBufferIndex)
{
    if (!ZeroCopyManager)
    {
        return nullptr;
    }
    
    return ZeroCopyManager->PinMemory(CPUAddress, Size, OutBufferIndex);
}

FSimulatedGPUReadback* FGPUDispatcher::GetGPUBuffer(uint32 BufferIndex)
{
    if (!ZeroCopyManager)
    {
        return nullptr;
    }
    
    // Now returns FSimulatedGPUReadback* directly, matching ZeroCopyManager's implementation
    return ZeroCopyManager->GetGPUBuffer(BufferIndex);
}

void FGPUDispatcher::ReleaseMemory(uint32 BufferIndex)
{
    if (ZeroCopyManager)
    {
        ZeroCopyManager->ReleaseMemory(BufferIndex);
    }
}

void FGPUDispatcher::LogOperationCompletion(const FComputeOperation& Operation, bool bSuccess, float DurationMs)
{
    // Log operation completion
    GPU_DISPATCHER_LOG_VERBOSE("Operation %llu %s in %.2f ms", 
        Operation.OperationId, bSuccess ? TEXT("completed") : TEXT("failed"), DurationMs);
    
    // Get debug visualizer
    IServiceLocator& Locator = IServiceLocator::Get();
    FServiceDebugVisualizer* Visualizer = 
        Locator.ResolveService<FServiceDebugVisualizer>();
    
    if (Visualizer)
    {
        // Record service interaction
        FName SourceKey = FName(*FString::Printf(TEXT("GPUDispatcher")));
        FName TargetKey = FName(*FString::Printf(TEXT("GPUImplementation")));
        
        Visualizer->RecordServiceInteraction(SourceKey, TargetKey, DurationMs, bSuccess);
    }
}

void FGPUDispatcher::ProcessOnCPU(const FComputeOperation& Operation, TFunction<void(bool, float)> CompletionCallback)
{
    // This would typically delegate to the CPU implementation of the appropriate system
    // For the purpose of this implementation, we'll just simulate a CPU operation
    
    double StartTime = FPlatformTime::Seconds();
    
    // Simulate CPU processing time based on operation complexity
    float Complexity = Operation.Bounds.GetVolume() / 1000.0f;
    float SimulatedTimeMs = FMath::Max(1.0f, Complexity * 0.01f);
    
    // Simulate work with a small sleep
    FPlatformProcess::Sleep(SimulatedTimeMs / 1000.0f);
    
    double EndTime = FPlatformTime::Seconds();
    float ElapsedMs = (EndTime - StartTime) * 1000.0f;
    
    // Record metrics
    FOperationMetrics Metrics;
    Metrics.OperationTypeId = Operation.OperationTypeId;
    Metrics.CPUExecutionTimeMS = ElapsedMs;
    Metrics.GPUExecutionTimeMS = 0.0f;
    Metrics.DataSize = CalculateOperationDataSize(Operation);
    Metrics.IterationCount = 1;
    Metrics.DeviceUtilization = 0.0f; // CPU operation
    Metrics.SuccessfulExecution = true;
    
    UpdatePerformanceMetrics(Metrics);
    
    // Invoke callback if provided
    if (CompletionCallback)
    {
        CompletionCallback(true, ElapsedMs);
    }
}

bool FGPUDispatcher::ProcessOnGPU(const FComputeOperation& Operation)
{
    // Get compute kernel for this operation
    FSDFComputeKernel* Kernel = KernelManager->FindBestKernelForOperation(Operation.OperationType, Operation);
    if (!Kernel)
    {
        GPU_DISPATCHER_LOG_WARNING("No suitable kernel found for operation %d", Operation.OperationType);
        return false;
    }
    
    // Simplified implementation that uses our non-RHI based approach
    double StartTime = FPlatformTime::Seconds();
    
    // Use the utility class for operation dispatch
    bool bSuccess = FMiningSDFComputeUtils::DispatchOperation(Operation);
    
    double EndTime = FPlatformTime::Seconds();
    float ElapsedMs = (EndTime - StartTime) * 1000.0f;
    
    // Log the completion
    LogOperationCompletion(Operation, bSuccess, ElapsedMs);
    
    return bSuccess;
}

void FGPUDispatcher::UpdatePerformanceMetrics(const FOperationMetrics& Metrics)
{
    // Add the new metrics to the history array
    PerformanceHistory.Add(Metrics);
    
    // If we have more than our maximum entries, remove the oldest one
    if (PerformanceHistory.Num() > MaxHistoryEntries)
    {
        PerformanceHistory.RemoveAt(0);
    }
    
    // Update workload distributor with the new metrics
    if (WorkloadDistributor)
    {
        WorkloadDistributor->UpdatePerformanceMetrics(Metrics);
    }
    
    // Update success/failure counters
    if (Metrics.SuccessfulExecution)
    {
        SuccessfulOperations.Increment();
    }
    else
    {
        FailedOperations.Increment();
    }
    
    // Update performance ratio metrics
    if (Metrics.GPUExecutionTimeMS > 0.0f && Metrics.CPUExecutionTimeMS > 0.0f)
    {
        // Calculate ratio of CPU time to GPU time
        float Ratio = Metrics.CPUExecutionTimeMS / Metrics.GPUExecutionTimeMS;
        
        // Weighted update to the overall ratio
        const float WeightFactor = 0.2f; // 20% weight for new value
        CPUToGPUPerformanceRatio = (CPUToGPUPerformanceRatio * (1.0f - WeightFactor)) + (Ratio * WeightFactor);
    }
    
    // Update GPU utilization metric
    if (Metrics.DeviceUtilization >= 0.0f)
    {
        const float WeightFactor = 0.1f; // 10% weight for new value
        AverageGPUUtilization = (AverageGPUUtilization * (1.0f - WeightFactor)) + (Metrics.DeviceUtilization * WeightFactor);
    }
}

void FGPUDispatcher::OnAsyncOperationComplete(int64 OperationId, bool bSuccess)
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
    
    // Record metrics
    FOperationMetrics Metrics;
    Metrics.OperationTypeId = State->OperationTypeId;
    Metrics.CPUExecutionTimeMS = 0.0f;
    Metrics.GPUExecutionTimeMS = State->ExecutionTimeMs;
    Metrics.DataSize = State->DataSize;
    Metrics.IterationCount = 1;
    Metrics.DeviceUtilization = GetCurrentGPUUtilization();
    Metrics.SuccessfulExecution = bSuccess;
    
    UpdatePerformanceMetrics(Metrics);
}

float FGPUDispatcher::GetCurrentGPUUtilization() const
{
    // In a real implementation, this would query the GPU's current utilization
    // For simplicity, we'll return a simulated value based on active operations
    
    // Create a mutable reference to OperationLock before using it with FScopeLock
    FCriticalSection& MutableLock = const_cast<FCriticalSection&>(OperationLock);
    FScopeLock Lock(&MutableLock);
    
    int32 ActiveCount = 0;
    for (const auto& Pair : ActiveOperations)
    {
        if (Pair.Value.Status == EOperationStatus::Running)
        {
            ActiveCount++;
        }
    }
    
    // Simple model: each operation adds 10% utilization, capped at 95%
    return FMath::Min(0.95f, ActiveCount * 0.1f);
}

void FGPUDispatcher::MonitorMemoryPressure()
{
    IMemoryManager* Manager = IServiceLocator::Get().ResolveService<IMemoryManager>();
    if (!Manager)
    {
        return;
    }
    
    uint64 AvailableBytes = 0;
    if (Manager->IsUnderMemoryPressure(&AvailableBytes))
    {
        // Scale back compute operations
        WorkloadDistributor->AdjustForMemoryPressure(AvailableBytes);
        
        // Use more CPU operations to reduce GPU memory pressure
        WorkloadDistributor->IncreaseCPUWorkloadRatio(0.3f);
        
        // Release non-critical resources
        TrimMemory(GetMemoryUsage() * 0.8); // Try to reduce by 20%
    }
}

EAsyncPriority FGPUDispatcher::GetOperationPriority(const FComputeOperation& Operation) const
{
    // Use the operation's priority if specified
    return Operation.Priority;
}

uint32 FGPUDispatcher::CalculateOperationDataSize(const FComputeOperation& Operation) const
{
    // Calculate approximate data size for metrics
    FVector Size = Operation.Bounds.GetSize();
    
    // Get the cell size from the HardwareProfileManager if available
    FVector CellSize(1.0f, 1.0f, 1.0f);
    if (HardwareProfileManager)
    {
        const FHardwareProfile& Profile = HardwareProfileManager->GetCurrentProfile();
        FSDFComputeKernel* Kernel = KernelManager->FindBestKernelForOperation(Operation.OperationType, Operation);
        if (Kernel)
        {
            CellSize = Kernel->CellSize;
        }
    }
    
    // Calculate number of voxels based on the bounds and cell size
    int32 VoxelsX = FMath::Max(1, FMath::CeilToInt(Size.X / CellSize.X));
    int32 VoxelsY = FMath::Max(1, FMath::CeilToInt(Size.Y / CellSize.Y));
    int32 VoxelsZ = FMath::Max(1, FMath::CeilToInt(Size.Z / CellSize.Z));
    uint32 VoxelCount = VoxelsX * VoxelsY * VoxelsZ;
    
    // If using narrow band optimization, we process fewer voxels
    if (Operation.bUseNarrowBand)
    {
        // In narrow band optimization, we typically only process 20-30% of the voxels
        VoxelCount = static_cast<uint32>(VoxelCount * 0.3f);
    }
    
    // Assume 4 bytes per voxel for SDF field (32-bit float)
    uint32 BytesPerVoxel = 4;
    
    // If high precision is required, use 8 bytes per voxel (64-bit double)
    if (Operation.bRequiresHighPrecision)
    {
        BytesPerVoxel = 8;
    }
    
    // If operation uses multiple material channels, multiply by channel count
    if (Operation.MaterialChannelId >= 0)
    {
        BytesPerVoxel *= 4; // Assume 4 channels on average for materials
    }
    
    return VoxelCount * BytesPerVoxel;
}