#include "../Public/GPUDispatcher.h"
#include "../Public/GPUDispatcherLogging.h"
#include "../Public/HardwareProfileManager.h"
#include "../Public/WorkloadDistributor.h"
#include "../Public/SDFComputeKernelManager.h"
#include "../Public/AsyncComputeCoordinator.h"
#include "../Public/ZeroCopyResourceManager.h"
#include "../Public/SDFShaderParameters.h"

#include "../../1_CoreRegistry/Public/CoreServiceLocator.h"
#include "../../1_CoreRegistry/Public/SDFTypeRegistry.h"
#include "../../1_CoreRegistry/Public/TypeRegistry.h"
#include "../../2_MemoryManagement/Public/Interfaces/IMemoryManager.h"
#include "../../2_MemoryManagement/Public/Interfaces/IBufferProvider.h"
#include "../../2_MemoryManagement/Public/NarrowBandAllocator.h"
#include "../../3_ThreadingTaskSystem/Public/AsyncTaskManager.h"
#include "../../3_ThreadingTaskSystem/Public/Interfaces/ITaskScheduler.h"
#include "../../3_ThreadingTaskSystem/Public/NumaHelpers.h"
#include "../../6_ServiceRegistryandDependency/Public/ServiceLocator.h"
#include "../../6_ServiceRegistryandDependency/Public/ServiceHealthMonitor.h"

#include "RenderGraphResources.h"
#include "RenderGraphBuilder.h"
#include "ShaderParameterStruct.h"
#include "RHICommandList.h"
#include "ShaderCore.h"
#include "PipelineStateCache.h"
#include "ShaderParameterUtils.h"
#include "../Public/ComputeShaderUtils.h"

// Define UClass types for service components to fix compile errors
UCLASS()
class UServiceHealthMonitor : public UObject
{
    GENERATED_BODY()
};

UCLASS()
class UServiceDebugVisualizer : public UObject
{
    GENERATED_BODY()
};

DEFINE_LOG_CATEGORY(LogGPUDispatcher);

FGPUDispatcher::FGPUDispatcher()
    : AverageGPUUtilization(0.0f)
    , CPUToGPUPerformanceRatio(1.0f)
    , StagingBuffer(nullptr)
{
    // TArray doesn't need explicit initialization like TCircularBuffer
    bIsInitialized = false;
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
    bIsInitialized = true;
    return true;
}

void FGPUDispatcher::Shutdown()
{
    if (!bIsInitialized)
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
    
    bIsInitialized = false;
    GPU_DISPATCHER_LOG_DEBUG("GPU Dispatcher shut down");
}

bool FGPUDispatcher::RegisterWithServiceLocator()
{
    // Get service locator
    IServiceLocator& ServiceLocator = IServiceLocator::Get();
    
    // Register as IComputeDispatcher
    if (!ServiceLocator.RegisterService<IComputeDispatcher>(this))
    {
        GPU_DISPATCHER_LOG_ERROR("Failed to register as IComputeDispatcher");
        return false;
    }
    
    // Declare dependencies
    ServiceLocator.DeclareDependency<IComputeDispatcher, IMemoryManager>(EServiceDependencyType::Required);
    ServiceLocator.DeclareDependency<IComputeDispatcher, ITaskScheduler>(EServiceDependencyType::Required);
    ServiceLocator.DeclareDependency<IComputeDispatcher, FSDFTypeRegistry>(EServiceDependencyType::Required);
    
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
    
    // Create zero-copy buffers for material fields
    // Note: Actual buffer creation will happen through material registration
    
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
    
    // Create staging buffer for transfers
    FRHIResourceCreateInfo CreateInfo(TEXT("ComputeStaging"));
    StagingBuffer = RHICreateBuffer(
        16 * 1024 * 1024,         // 16MB staging buffer
        BUF_ShaderResource | BUF_SourceCopy | BUF_Dynamic,
        0,
        ERHIAccess::SRVMask,
        CreateInfo);
        
    if (!StagingBuffer)
    {
        GPU_DISPATCHER_LOG_ERROR("Failed to create staging buffer");
        return false;
    }
    
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
    if (!bIsInitialized)
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
        
        // Process on GPU synchronously (async is handled by DispatchComputeAsync)
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
    if (!bIsInitialized || Operations.Num() == 0)
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
    if (!bIsInitialized)
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
    if (!bIsInitialized)
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
    
    // Set basic capabilities using modern UE5.5 API
    Capabilities.bSupportsComputeShaders = EnumHasAnyFlags(GMaxRHIFeatureLevel, ERHIFeatureLevel::SM5);
    
    // Set maximum dispatch sizes
    Capabilities.MaxDispatchSizeX = 65535;
    Capabilities.MaxDispatchSizeY = 65535;
    Capabilities.MaxDispatchSizeZ = 65535;
    
    // Set maximum shared memory size
    Capabilities.MaxSharedMemorySize = 32768; // 32KB by default
    
    // Add supported shader formats
    TArray<FName> ShaderFormats;
    // Use some common shader formats as a fallback since direct API may not be accessible
    ShaderFormats.Add(FName("SF_METAL_SM5"));
    ShaderFormats.Add(FName("SF_METAL_SM5_NOTESS"));
    ShaderFormats.Add(FName("SF_VULKAN_SM5"));
    ShaderFormats.Add(FName("SF_VULKAN_SM6"));
    for (const FName& Format : ShaderFormats)
    {
        Capabilities.SupportedShaderFormats.Add(Format.ToString());
    }
    
    return Capabilities;
}

bool FGPUDispatcher::DispatchComputeAsync(const FComputeOperation& Operation, TFunction<void(bool, float)> CompletionCallback)
{
    if (!bIsInitialized)
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
        
        // Submit to async compute coordinator
        // Capture class instance by value instead of using 'this' pointer for UE5.5 compatibility
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
    if (!bIsInitialized)
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
    
    // Get compute kernel for this operation
    FSDFComputeKernel* Kernel = KernelManager->FindBestKernelForOperation(OpType, Operation);
    if (!Kernel)
    {
        GPU_DISPATCHER_LOG_WARNING("No suitable kernel found for SDF operation %d", OpType);
        return false;
    }
    
    // Execute using RDG
    // Capture class instance by value instead of using 'this' pointer for UE5.5 compatibility
    FGPUDispatcher* Dispatcher = this;
    ENQUEUE_RENDER_COMMAND(DispatchSDFOperation)(
        [Dispatcher, OpType, Bounds, InputBuffers, OutputBuffer, Kernel](FRHICommandListImmediate& RHICmdList)
        {
            FRDGBuilder GraphBuilder(RHICmdList);
            
            // Set up shader parameters
            FSDFOperationParameters* ShaderParams = GraphBuilder.AllocParameters<FSDFOperationParameters>();
            ShaderParams->OutputField = GraphBuilder.CreateUAV(OutputBuffer);
            
            // Set input buffers
            if (InputBuffers.Num() > 0)
            {
                ShaderParams->InputField1 = GraphBuilder.CreateSRV(InputBuffers[0]);
            }
            
            if (InputBuffers.Num() > 1)
            {
                ShaderParams->InputField2 = GraphBuilder.CreateSRV(InputBuffers[1]);
            }
            
            // Set operation parameters
            ShaderParams->OperationType = static_cast<uint32>(OpType);
            ShaderParams->BoundsMin = FVector3f(Bounds.Min);
            ShaderParams->BoundsMax = FVector3f(Bounds.Max);
            
            // Set up dispatch parameters
            const FIntVector GroupSize(
                FMath::DivideAndRoundUp((int32)(Bounds.Max.X - Bounds.Min.X), (int32)Kernel->ThreadGroupSizeX),
                FMath::DivideAndRoundUp((int32)(Bounds.Max.Y - Bounds.Min.Y), (int32)Kernel->ThreadGroupSizeY),
                FMath::DivideAndRoundUp((int32)(Bounds.Max.Z - Bounds.Min.Z), (int32)Kernel->ThreadGroupSizeZ)
            );
            
            // Get the appropriate shader based on operation type
            FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
            TShaderMapRef<FComputeShaderType> ComputeShader(ShaderMap);
            
            FMiningSDFComputeShaderUtils::AddPass(
                GraphBuilder,
                TEXT("SDFOperation"),
                ComputeShader,
                ShaderParams,
                GroupSize
            );
            
            // Execute graph
            GraphBuilder.Execute();
        });
    
    return true;
}

bool FGPUDispatcher::DispatchMaterialOperation(int32 MaterialChannelId, const FBox& Bounds, 
                                            const TArray<FRDGBufferRef>& InputBuffers, 
                                            FRDGBufferRef OutputBuffer)
{
    if (!bIsInitialized)
    {
        return false;
    }
    
    // Create a compute operation for this material operation
    FComputeOperation Operation;
    Operation.MaterialChannelId = MaterialChannelId;
    Operation.Bounds = Bounds;
    
    // For material operations, we'll set the operation type based on the material channel
    // This is a simplified version - in a real implementation, you'd get this from the material system
    Operation.OperationType = 10; // Assuming 10 is a material field operation
    Operation.OperationTypeId = 10;
    
    // Determine processing target
    EProcessingTarget Target = WorkloadDistributor->DetermineProcessingTarget(Operation);
    
    if (Target != EProcessingTarget::GPU)
    {
        // If not targeting GPU, fallback to CPU
        GPU_DISPATCHER_LOG_VERBOSE("Material operation on channel %d assigned to CPU by distributor", MaterialChannelId);
        return false;
    }
    
    // Get compute kernel for this operation
    FSDFComputeKernel* Kernel = KernelManager->FindBestKernelForOperation(Operation.OperationType, Operation);
    if (!Kernel)
    {
        GPU_DISPATCHER_LOG_WARNING("No suitable kernel found for material operation on channel %d", MaterialChannelId);
        return false;
    }
    
    // Execute using RDG
    // Capture class instance by value instead of using 'this' pointer for UE5.5 compatibility
    FGPUDispatcher* Dispatcher = this;
    ENQUEUE_RENDER_COMMAND(DispatchMaterialOperation)(
        [Dispatcher, MaterialChannelId, Bounds, InputBuffers, OutputBuffer, Kernel](FRHICommandListImmediate& RHICmdList)
        {
            FRDGBuilder GraphBuilder(RHICmdList);
            
            // Set up shader parameters
            FSDFOperationParameters* ShaderParams = GraphBuilder.AllocParameters<FSDFOperationParameters>();
            ShaderParams->OutputField = GraphBuilder.CreateUAV(OutputBuffer);
            
            // Set input buffers
            if (InputBuffers.Num() > 0)
            {
                ShaderParams->InputField1 = GraphBuilder.CreateSRV(InputBuffers[0]);
            }
            
            if (InputBuffers.Num() > 1)
            {
                ShaderParams->InputField2 = GraphBuilder.CreateSRV(InputBuffers[1]);
            }
            
            // Set operation parameters
            ShaderParams->OperationType = 10; // Material field operation
            ShaderParams->MaterialChannelId = MaterialChannelId;
            ShaderParams->BoundsMin = FVector3f(Bounds.Min);
            ShaderParams->BoundsMax = FVector3f(Bounds.Max);
            
            // Set up dispatch parameters
            const FIntVector GroupSize(
                FMath::DivideAndRoundUp((int32)(Bounds.Max.X - Bounds.Min.X), (int32)Kernel->ThreadGroupSizeX),
                FMath::DivideAndRoundUp((int32)(Bounds.Max.Y - Bounds.Min.Y), (int32)Kernel->ThreadGroupSizeY),
                FMath::DivideAndRoundUp((int32)(Bounds.Max.Z - Bounds.Min.Z), (int32)Kernel->ThreadGroupSizeZ)
            );
            
            // Get the appropriate shader based on operation type
            FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
            TShaderMapRef<FComputeShaderType> ComputeShader(ShaderMap);
            
            FMiningSDFComputeShaderUtils::AddPass(
                GraphBuilder,
                TEXT("MaterialOperation"),
                ComputeShader,
                ShaderParams,
                GroupSize
            );
            
            // Execute graph
            GraphBuilder.Execute();
        });
    
    return true;
}

bool FGPUDispatcher::FlushOperations(bool bWaitForCompletion)
{
    if (!bIsInitialized)
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
    
    // Add staging buffer memory
    if (StagingBuffer)
    {
        TotalMemory += StagingBuffer->GetSize();
    }
    
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
    for (auto It = ResourceStateMap.CreateIterator(); It; ++It)
    {
        if (It.Value().LastFrameAccessed < GFrameCounter - 60) // Remove if not used in last 60 frames
        {
            It.RemoveCurrent();
        }
    }
    
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
    Writer.Serialize(&StateVersion, sizeof(uint32));
    
    // Save hardware profile
    FHardwareProfile Profile = HardwareProfileManager->GetCurrentProfile();
    // Make a copy of the string as non-const char array to use with Serialize
    int32 StringLen = Profile.DeviceName.Len() + 1;
    char* DeviceNameBuffer = new char[StringLen];
    FCStringAnsi::Strncpy(DeviceNameBuffer, TCHAR_TO_ANSI(*Profile.DeviceName), StringLen);
    Writer.Serialize(DeviceNameBuffer, StringLen);
    delete[] DeviceNameBuffer;
    
    // Save VendorId as uint32
    uint32 VendorIdCasted = static_cast<uint32>(Profile.VendorId);
    Writer.Serialize(&VendorIdCasted, sizeof(uint32));
    
    // Save ComputeUnits - creating a mutable copy for serialization
    uint32 ComputeUnitsCopy = Profile.ComputeUnits;
    Writer.Serialize(&ComputeUnitsCopy, sizeof(uint32));
    
    // Save active operations count
    const int32 ActiveOpsCount = ActiveOperations.Num();
    Writer.Serialize(&ActiveOpsCount, sizeof(int32));
    
    // Save performance metrics
    // Create local copies for serialization
    float AverageGPUUtil = AverageGPUUtilization;
    float CPUToGPURatio = CPUToGPUPerformanceRatio;
    Writer.Serialize(&AverageGPUUtil, sizeof(float));
    Writer.Serialize(&CPUToGPURatio, sizeof(float));
    
    // Get and save atomic counter values
    uint64 SuccessfulOps = SuccessfulOperations.GetValue();
    uint64 FailedOps = FailedOperations.GetValue();
    Writer.Serialize(&SuccessfulOps, sizeof(uint64));
    Writer.Serialize(&FailedOps, sizeof(uint64));
    
    // Save distribution config
    FDistributionConfig Config = WorkloadDistributor->GetDistributionConfig();
    Writer.Serialize(&Config.bEnableAutotuning, sizeof(bool));
    Writer.Serialize(&Config.CPUAffinityForLowOperationCount, sizeof(float));
    Writer.Serialize(&Config.GPUAffinityForBatchedOperations, sizeof(float));
    
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
    Reader.Serialize(&StateVersion, sizeof(uint32));
    if (StateVersion != 1)
    {
        GPU_DISPATCHER_LOG_WARNING("Incompatible state version %u, expected 1", StateVersion);
        return false;
    }
    
    // Read hardware profile info (just for validation)
    FString DeviceName;
    uint32 VendorId;
    uint32 ComputeUnits;
    
    // Read DeviceName string
    char DeviceNameBuffer[256];
    Reader.Serialize(DeviceNameBuffer, 256);
    DeviceName = FString(ANSI_TO_TCHAR(DeviceNameBuffer));
    
    Reader.Serialize(&VendorId, sizeof(uint32));
    Reader.Serialize(&ComputeUnits, sizeof(uint32));
    
    // Check if hardware matches
    const FHardwareProfile& CurrentProfile = HardwareProfileManager->GetCurrentProfile();
    uint32 CurrentVendorId = static_cast<uint32>(CurrentProfile.VendorId);
    if (DeviceName != CurrentProfile.DeviceName || 
        VendorId != CurrentVendorId ||
        ComputeUnits != CurrentProfile.ComputeUnits)
    {
        GPU_DISPATCHER_LOG_WARNING("Hardware profile mismatch, saved state may not be optimal");
    }
    
    // Read active operations count (for metrics)
    int32 ActiveOpsCount;
    Reader.Serialize(&ActiveOpsCount, sizeof(int32));
    
    // Read performance metrics
    float AvgGPUUtil, CPUToGPURatio;
    Reader.Serialize(&AvgGPUUtil, sizeof(float));
    Reader.Serialize(&CPUToGPURatio, sizeof(float));
    AverageGPUUtilization = AvgGPUUtil;
    CPUToGPUPerformanceRatio = CPUToGPURatio;
    
    uint64 SuccessfulOps, FailedOps;
    Reader.Serialize(&SuccessfulOps, sizeof(uint64));
    Reader.Serialize(&FailedOps, sizeof(uint64));
    
    SuccessfulOperations.Set(SuccessfulOps);
    FailedOperations.Set(FailedOps);
    
    // Read distribution config
    FDistributionConfig Config;
    bool EnableAutotuning;
    float CPUAffinity, GPUAffinity;
    Reader.Serialize(&EnableAutotuning, sizeof(bool));
    Reader.Serialize(&CPUAffinity, sizeof(float));
    Reader.Serialize(&GPUAffinity, sizeof(float));
    
    Config.bEnableAutotuning = EnableAutotuning;
    Config.CPUAffinityForLowOperationCount = CPUAffinity;
    Config.GPUAffinityForBatchedOperations = GPUAffinity;
    
    WorkloadDistributor->SetDistributionConfig(Config);
    
    return true;
}

void FGPUDispatcher::ExecuteComputePass(FRDGBuilder& GraphBuilder, 
                                      const FShaderParametersMetadata& ShaderMetadata, 
                                      const FDispatchParameters& Params)
{
    // Track resource states and ensure proper transitions
    ResourceBarrierTracking(GraphBuilder, Params.Resources);
    
    // Execute the compute pass with proper resource transitions
    // Fixed AddPass call to match UE5.5 API
    // Capture class instance by value instead of using 'this' pointer for UE5.5 compatibility
    FGPUDispatcher* Dispatcher = this;
    GraphBuilder.AddPass(
        RDG_EVENT_NAME("SDFOperation"),
        nullptr,
        ERDGPassFlags::Compute,
        [Dispatcher, Params, &ShaderMetadata](FRHIComputeCommandList& RHICmdList) 
        {
            // Set compute shader and parameters
            // This is just a placeholder, actual shader setup would be more complex
            uint32 GroupSizeX = FMath::DivideAndRoundUp(Params.SizeX, Params.ThreadGroupSizeX);
            uint32 GroupSizeY = FMath::DivideAndRoundUp(Params.SizeY, Params.ThreadGroupSizeY);
            uint32 GroupSizeZ = FMath::DivideAndRoundUp(Params.SizeZ, Params.ThreadGroupSizeZ);
            
            // Dispatch compute shader
            RHICmdList.DispatchComputeShader(GroupSizeX, GroupSizeY, GroupSizeZ);
        }
    );
}

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
        FName TargetKey = FName(*FString::Printf(TEXT("RHI")));
        
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
    
    // Prepare dispatch command to process the operation on GPU
    bool bSuccess = false;
    
    // Queue render command to execute the operation
    // Capture class instance by value instead of using 'this' pointer for UE5.5 compatibility
    FGPUDispatcher* Dispatcher = this;
    ENQUEUE_RENDER_COMMAND(ProcessComputeOperation)(
        [Dispatcher, &Operation, Kernel, &bSuccess](FRHICommandListImmediate& RHICmdList)
        {
            FRDGBuilder GraphBuilder(RHICmdList);
            
            // Set up shader parameters
            FSDFOperationParameters* ShaderParams = GraphBuilder.AllocParameters<FSDFOperationParameters>();
            
            // Set operation parameters
            ShaderParams->OperationType = Operation.OperationType;
            ShaderParams->BoundsMin = FVector3f(Operation.Bounds.Min);
            ShaderParams->BoundsMax = FVector3f(Operation.Bounds.Max);
            ShaderParams->Strength = Operation.Strength;
            ShaderParams->BlendWeight = Operation.BlendWeight;
            ShaderParams->UseNarrowBand = Operation.bUseNarrowBand ? 1 : 0;
            ShaderParams->UseHighPrecision = Operation.bRequiresHighPrecision ? 1 : 0;
            
            // Calculate volume size based on bounds
            FVector VolumeSize = Operation.Bounds.GetSize();
            ShaderParams->VolumeSize = FVector3f(VolumeSize);
            
            // Set up dispatch parameters
            FIntVector VolumeRes(
                FMath::Max(1, FMath::CeilToInt(VolumeSize.X / Kernel->CellSize.X)),
                FMath::Max(1, FMath::CeilToInt(VolumeSize.Y / Kernel->CellSize.Y)),
                FMath::Max(1, FMath::CeilToInt(VolumeSize.Z / Kernel->CellSize.Z))
            );
            
            ShaderParams->VolumeWidth = VolumeRes.X;
            ShaderParams->VolumeHeight = VolumeRes.Y;
            ShaderParams->VolumeDepth = VolumeRes.Z;
            
            // Create dispatch parameters
            const FIntVector GroupSize(
                FMath::DivideAndRoundUp(VolumeRes.X, (int32)Kernel->ThreadGroupSizeX),
                FMath::DivideAndRoundUp(VolumeRes.Y, (int32)Kernel->ThreadGroupSizeY),
                FMath::DivideAndRoundUp(VolumeRes.Z, (int32)Kernel->ThreadGroupSizeZ)
            );
            
            // Get the appropriate shader based on operation type
            FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
            TShaderMapRef<FComputeShaderType> ComputeShader(ShaderMap);
            
            // Add compute pass
            FMiningSDFComputeShaderUtils::AddPass(
                GraphBuilder,
                TEXT("SDFOperation"),
                ComputeShader,
                ShaderParams,
                GroupSize
            );
            
            // Execute graph
            GraphBuilder.Execute();
            
            // Operation completed successfully
            bSuccess = true;
        });
    
    // Wait for command to complete
    FlushRenderingCommands();
    
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

void FGPUDispatcher::ResourceBarrierTracking(FRDGBuilder& GraphBuilder, const TMap<FRHIResource*, FResourceState>& Resources)
{
    for (const auto& ResourcePair : Resources)
    {
        FRHIResource* Resource = ResourcePair.Key;
        const FResourceState& TargetState = ResourcePair.Value;
        
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
        bool bNeedsTransition = CurrentState->CurrentAccess != TargetState.CurrentAccess ||
                              CurrentState->CurrentPipeline != TargetState.CurrentPipeline;
        
        if (bNeedsTransition)
        {
            // Add resource transition
            GraphBuilder.AddPass(
                RDG_EVENT_NAME("ResourceTransition"),
                ERDGPassFlags::None,
                [Resource, CurrentState, TargetState](FRHICommandListImmediate& RHICmdList)
                {
                    TArray<FRHITransitionInfo> Transitions;
                    // Use UE5.5 compatible FRHITransitionInfo API
                    // Create transition with only access states (not pipeline states)
                    Transitions.Add(FRHITransitionInfo::CreateAccessTransition(
                        static_cast<FRHITexture*>(Resource), 
                        CurrentState->CurrentAccess, 
                        TargetState.CurrentAccess));
                    RHICmdList.Transition(Transitions);
                    
                    // Update current state
                    *CurrentState = TargetState;
                });
        }
        
        // Update last accessed frame
        CurrentState->LastFrameAccessed = GFrameCounter;
    }
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