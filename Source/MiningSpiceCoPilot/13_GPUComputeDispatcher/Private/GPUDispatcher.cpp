#include "13_GPUComputeDispatcher/Public/GPUDispatcher.h"
#include "13_GPUComputeDispatcher/Public/GPUDispatcherLogging.h"
#include "13_GPUComputeDispatcher/Public/HardwareProfileManager.h"
#include "13_GPUComputeDispatcher/Public/WorkloadDistributor.h"
#include "13_GPUComputeDispatcher/Public/SDFComputeKernelManager.h"
#include "13_GPUComputeDispatcher/Public/AsyncComputeCoordinator.h"
#include "13_GPUComputeDispatcher/Public/ZeroCopyResourceManager.h"
#include "13_GPUComputeDispatcher/Public/SDFShaderParameters.h"

#include "1_CoreRegistry/Public/CoreServiceLocator.h"
#include "1_CoreRegistry/Public/SDFTypeRegistry.h"
#include "1_CoreRegistry/Public/TypeRegistry.h"
#include "2_MemoryManagement/Public/Interfaces/IMemoryManager.h"
#include "2_MemoryManagement/Public/Interfaces/IBufferProvider.h"
#include "2_MemoryManagement/Public/NarrowBandAllocator.h"
#include "3_ThreadingTaskSystem/Public/AsyncTaskManager.h"
#include "3_ThreadingTaskSystem/Public/Interfaces/ITaskScheduler.h"
#include "3_ThreadingTaskSystem/Public/NumaHelpers.h"
#include "6_ServiceRegistryandDependency/Public/ServiceLocator.h"
#include "6_ServiceRegistryandDependency/Public/ServiceHealthMonitor.h"

#include "RenderGraphResources.h"
#include "RenderGraphBuilder.h"
#include "ShaderParameterStruct.h"
#include "RHICommandList.h"
#include "ShaderCore.h"
#include "PipelineStateCache.h"
#include "ShaderParameterUtils.h"
#include "ComputeShaderUtils.h"

DEFINE_LOG_CATEGORY(LogGPUDispatcher);

FGPUDispatcher::FGPUDispatcher()
    : AverageGPUUtilization(0.0f)
    , CPUToGPUPerformanceRatio(1.0f)
    , StagingBuffer(nullptr)
{
    PerformanceHistory.Init(MaxHistoryEntries);
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
    if (!WorkloadDistributor->Initialize(HardwareProfileManager->GetCurrentProfile()))
    {
        GPU_DISPATCHER_LOG_ERROR("Failed to initialize workload distributor");
        return false;
    }
    
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
                // Store for later use
                NarrowBandAllocators.Add(NBAllocator->GetTypeId(), NBAllocator);
                GPU_DISPATCHER_LOG_VERBOSE("Found narrow band allocator for type %u", NBAllocator->GetTypeId());
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
    StagingBuffer = RHICreateVertexBuffer(
        16 * 1024 * 1024,         // 16MB staging buffer
        BUF_ShaderResource | BUF_SourceCopy | BUF_Dynamic,
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
    
    // Configure operations based on hardware capabilities
    TArray<int32> OperationTypes = Registry->GetAllOperationTypes();
    for (int32 OpType : OperationTypes)
    {
        // Determine if this operation can run on GPU
        bool bGPUCompatible = Registry->IsOperationGPUCompatible(OpType);
        
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
        uint64 OperationId = NextOperationId.Increment();
        
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

bool FGPUDispatcher::CancelOperation(uint64 OperationId)
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

bool FGPUDispatcher::QueryOperationStatus(uint64 OperationId, FOperationStatus& OutStatus)
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
            static_cast<FServiceHealthMonitor*>(Locator.ResolveService(UServiceHealthMonitor::StaticClass()));
        
        if (HealthMonitor)
        {
            // Record operation failure
            HealthMonitor->RecordServiceOperation(
                "GPUDispatcher",
                false,  // operation failed
                State->ExecutionTimeMs,
                "ResourceExhaustion",
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
    
    // Set basic capabilities
    Capabilities.bSupportsComputeShaders = IsRHIDeviceComputeSupported();
    
    // Set maximum dispatch sizes
    Capabilities.MaxDispatchSizeX = 65535;
    Capabilities.MaxDispatchSizeY = 65535;
    Capabilities.MaxDispatchSizeZ = 65535;
    
    // Set maximum shared memory size
    Capabilities.MaxSharedMemorySize = 32768; // 32KB by default
    
    // Add supported shader formats
    TArray<FName> ShaderFormats;
    GetAllTargetPlatformShaderFormats(GMaxRHIShaderPlatform, ShaderFormats);
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
        uint64 OperationId = NextOperationId.Increment();
        
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
        AsyncComputeCoordinator->ScheduleAsyncOperation(
            ModifiedOperation, 
            [this, OperationId](bool bSuccess) {
                OnAsyncOperationComplete(OperationId, bSuccess);
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
    if (!Registry->IsOperationGPUCompatible(OpType))
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
    ENQUEUE_RENDER_COMMAND(DispatchSDFOperation)(
        [this, OpType, Bounds, InputBuffers, OutputBuffer, Kernel](FRHICommandListImmediate& RHICmdList)
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
            
            // Add compute pass
            TShaderMapRef<FComputeShaderType> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
            
            FComputeShaderUtils::AddPass(
                GraphBuilder,
                RDG_EVENT_NAME("SDFOperation"),
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
    ENQUEUE_RENDER_COMMAND(DispatchMaterialOperation)(
        [this, MaterialChannelId, Bounds, InputBuffers, OutputBuffer, Kernel](FRHICommandListImmediate& RHICmdList)
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
            
            // Add compute pass
            TShaderMapRef<FComputeShaderType> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
            
            FComputeShaderUtils::AddPass(
                GraphBuilder,
                RDG_EVENT_NAME("MaterialOperation"),
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
    // Save critical state information
    FMemoryWriter Writer(OutState);
    
    // Save version for compatibility
    const uint32 StateVersion = 1;
    Writer << StateVersion;
    
    // Save hardware profile
    FHardwareProfile Profile = HardwareProfileManager->GetCurrentProfile();
    Writer << Profile.DeviceName;
    Writer << (uint32)Profile.VendorId;
    Writer << Profile.ComputeUnits;
    
    // Save active operations count
    const int32 ActiveOpsCount = ActiveOperations.Num();
    Writer << ActiveOpsCount;
    
    // Save performance metrics
    Writer << AverageGPUUtilization;
    Writer << CPUToGPUPerformanceRatio;
    Writer << SuccessfulOperations.GetValue();
    Writer << FailedOperations.GetValue();
    
    // Save distribution config
    FDistributionConfig Config = WorkloadDistributor->GetDistributionConfig();
    Writer << Config.bEnableAutotuning;
    Writer << Config.CPUAffinityForLowOperationCount;
    Writer << Config.GPUAffinityForBatchedOperations;
    
    return true;
}

bool FGPUDispatcher::RestoreState(const TArray<uint8>& InState)
{
    if (InState.Num() == 0)
    {
        return false;
    }
    
    FMemoryReader Reader(const_cast<TArray<uint8>&>(InState));
    
    // Read and validate version
    uint32 StateVersion;
    Reader << StateVersion;
    if (StateVersion != 1)
    {
        GPU_DISPATCHER_LOG_WARNING("Incompatible state version %u, expected 1", StateVersion);
        return false;
    }
    
    // Read hardware profile info (just for validation)
    FString DeviceName;
    uint32 VendorId;
    uint32 ComputeUnits;
    Reader << DeviceName;
    Reader << VendorId;
    Reader << ComputeUnits;
    
    // Check if hardware matches
    const FHardwareProfile& CurrentProfile = HardwareProfileManager->GetCurrentProfile();
    if (DeviceName != CurrentProfile.DeviceName || 
        VendorId != (uint32)CurrentProfile.VendorId ||
        ComputeUnits != CurrentProfile.ComputeUnits)
    {
        GPU_DISPATCHER_LOG_WARNING("Hardware profile mismatch, saved state may not be optimal");
    }
    
    // Read active operations count (for metrics)
    int32 ActiveOpsCount;
    Reader << ActiveOpsCount;
    
    // Read performance metrics
    Reader >> AverageGPUUtilization;
    Reader >> CPUToGPUPerformanceRatio;
    
    uint64 SuccessfulOps, FailedOps;
    Reader >> SuccessfulOps;
    Reader >> FailedOps;
    
    SuccessfulOperations.Set(SuccessfulOps);
    FailedOperations.Set(FailedOps);
    
    // Read distribution config
    FDistributionConfig Config;
    Reader >> Config.bEnableAutotuning;
    Reader >> Config.CPUAffinityForLowOperationCount;
    Reader >> Config.GPUAffinityForBatchedOperations;
    
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
    GraphBuilder.AddComputePass(
        TEXT("SDFOperation"),
        nullptr,
        ERDGPassFlags::Compute,
        [this, Params, &ShaderMetadata](FRHIComputeCommandList& RHICmdList) 
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
        static_cast<FServiceDebugVisualizer*>(Locator.ResolveService(UServiceDebugVisualizer::StaticClass()));
    
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
    // This would typically use the RDG to submit a compute shader
    // For the purpose of this implementation, we'll just simulate a GPU operation
    
    // Get compute kernel for this operation
    FSDFComputeKernel* Kernel = KernelManager->FindBestKernelForOperation(Operation.OperationType, Operation);
    if (!Kernel)
    {
        GPU_DISPATCHER_LOG_WARNING("No suitable kernel found for operation %d", Operation.OperationType);
        return false;
    }
    
    // In a real implementation, this would queue an RDG operation
    // For now, we'll just simulate the GPU execution time
    
    // Simulate GPU processing time based on operation complexity
    float Complexity = Operation.Bounds.GetVolume() / 1000.0f;
    float SimulatedTimeMs = FMath::Max(0.5f, Complexity * 0.005f); // GPU typically 2x faster than CPU
    
    // Simulate GPU work with a small sleep
    FPlatformProcess::Sleep(SimulatedTimeMs / 1000.0f);
    
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
    
    // Notify WorkloadDistributor
    WorkloadDistributor->UpdatePerformanceMetrics(Metrics);
    
    // Update CPU to GPU performance ratio if we have both times
    if (Metrics.CPUExecutionTimeMS > 0.0f && Metrics.GPUExecutionTimeMS > 0.0f)
    {
        const float Alpha = 0.1f;
        float Ratio = Metrics.CPUExecutionTimeMS / Metrics.GPUExecutionTimeMS;
        CPUToGPUPerformanceRatio = Alpha * Ratio + (1.0f - Alpha) * CPUToGPUPerformanceRatio;
    }
    
    // Monitor memory pressure periodically
    if ((SuccessfulOperations.GetValue() + FailedOperations.GetValue()) % 100 == 0)
    {
        MonitorMemoryPressure();
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
    
    FScopeLock Lock(&OperationLock);
    
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
                    RHICmdList.Transition(FRHITransitionInfo(
                        Resource, 
                        CurrentState->CurrentAccess, 
                        TargetState.CurrentAccess, 
                        CurrentState->CurrentPipeline, 
                        TargetState.CurrentPipeline));
                    
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
    uint32 VoxelCount = (uint32)(Size.X * Size.Y * Size.Z);
    
    // Assume 4 bytes per voxel for SDF field
    uint32 BytesPerVoxel = 4;
    
    // If operation uses multiple channels, multiply by channel count
    if (Operation.MaterialChannelId >= 0)
    {
        BytesPerVoxel *= 4; // Assume 4 channels on average
    }
    
    return VoxelCount * BytesPerVoxel;
}