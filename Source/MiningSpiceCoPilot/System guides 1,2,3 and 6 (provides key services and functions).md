# Comprehensive GPU Compute Dispatcher Integration Guide

This comprehensive guide will help Claude Code implement System 13 (GPU Compute Dispatcher System) by highlighting all key integration points and existing functionality within the Mining System codebase.

## 1. System Overview

The GPU Compute Dispatcher System (System 13) provides efficient GPU compute workload distribution specifically for SDF operations, implementing:
- Adaptive CPU/GPU workload balancing
- Specialized compute shaders for mining operations
- Intelligent batching for SDF field updates
- Hardware-specific optimizations
- Performance-driven fallbacks to CPU when appropriate

## 2. Core Architecture Components

### Base Interfaces
- **IComputeDispatcher**: Main interface for compute dispatch
- **IWorkloadDistributor**: Interface for workload distribution
- **IServiceProvider**: For registering with service locator

### Key Components
- **FGPUDispatcher**: Main implementation of IComputeDispatcher
- **FHardwareProfileManager**: Hardware capability detection and profiles
- **FWorkloadDistributor**: Dynamic workload distribution
- **FSDFComputeKernelManager**: Specialized compute shader management
- **FAsyncComputeCoordinator**: Async compute scheduling

## 3. Key Registry Integration Points

### Core Service Locator
```cpp
// FCoreServiceLocator - Central service registry
// Provides access to all subsystems via interface-based lookup

// Key methods:
bool RegisterService(void* InService, const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE);
void* ResolveService(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE);
bool RegisterServiceWithVersion(void* InService, const UClass* InInterfaceType, const FServiceVersion& InVersion);
bool DeclareDependency(const UClass* InDependentType, const UClass* InDependencyType, EServiceDependencyType InDependencyKind);
```

### SDF Type Registry
```cpp
// FSDFTypeRegistry - Manages SDF field types and operations

// Key methods:
uint32 RegisterFieldType(const FName& InTypeName, uint32 InChannelCount, uint32 InAlignmentRequirement, bool bInSupportsGPU);
uint32 RegisterOperation(const FName& InOperationName, ESDFOperationType InOperationType, uint32 InInputCount, bool bInSupportsSmoothing);
bool IsOperationGPUCompatible(ESDFOperationType InOperationType) const;
uint32 GetOptimalThreadBlockSize(ESDFOperationType InOperationType) const;
```

### Material Registry
```cpp
// FMaterialRegistry - Manages material types and properties

// Key methods:
const FMaterialTypeInfo* GetMaterialTypeInfo(uint32 InTypeId) const;
EMaterialCapabilities GetMaterialCapabilities(uint32 TypeId) const;
bool SetupMaterialFields(uint32 InTypeId, bool bEnableVectorization);
```

### Memory Management Integration
```cpp
// Accessing memory systems
IMemoryManager* MemoryManager = IServiceLocator::Get().ResolveService<IMemoryManager>();
FNarrowBandAllocator* NBAllocator = static_cast<FNarrowBandAllocator*>(MemoryManager->GetPool(FName("HighPrecisionNBPool")));
FZeroCopyBuffer* ZeroCopyBuffer = IServiceLocator::Get().ResolveService<FZeroCopyBuffer>();
```

### Threading Task System Integration
```cpp
// Task scheduling integration
FTaskConfig TypedConfig = Config;
TypedConfig.SetTypeId(TypeId, ERegistryType::SDF);
TypedConfig.SetOptimizationFlags(OptimizationFlags);
uint64 TaskId = ScheduleTaskWithScheduler(TaskFunc, TypedConfig);
```

## 4. Required Classes to Implement

### 1. FGPUDispatcher
```cpp
class FGPUDispatcher : public IComputeDispatcher
{
public:
    // Constructor/Destructor
    FGPUDispatcher();
    virtual ~FGPUDispatcher();
    
    // IComputeDispatcher Interface
    virtual bool DispatchCompute(const FComputeOperation& Operation) override;
    virtual bool BatchOperations(const TArray<FComputeOperation>& Operations) override;
    virtual bool CancelOperation(uint64 OperationId) override;
    virtual bool QueryOperationStatus(uint64 OperationId, FOperationStatus& OutStatus) override;
    virtual FComputeCapabilities GetCapabilities() const override;
    
    // Initialization and shutdown
    bool Initialize();
    void Shutdown();
    
    // Registration with service locator
    bool RegisterWithServiceLocator();
    
    // SDF-specific operations
    bool DispatchSDFOperation(ESDFOperationType OpType, const FBox& Bounds, const TArray<FRDGBufferRef>& InputBuffers, FRDGBufferRef OutputBuffer);
    bool DispatchMaterialOperation(int32 MaterialChannelId, const FBox& Bounds, const TArray<FRDGBufferRef>& InputBuffers, FRDGBufferRef OutputBuffer);
    
    // RDG Integration
    void ExecuteComputePass(FRDGBuilder& GraphBuilder, const FComputeShaderMetadata& ShaderMetadata, const FDispatchParameters& Params);
    
private:
    // Member variables
    TSharedPtr<FHardwareProfileManager> HardwareProfileManager;
    TSharedPtr<FWorkloadDistributor> WorkloadDistributor;
    TSharedPtr<FSDFComputeKernelManager> KernelManager;
    TSharedPtr<FAsyncComputeCoordinator> AsyncComputeCoordinator;
    
    // State tracking
    TMap<uint64, FOperationState> ActiveOperations;
    FCriticalSection OperationLock;
    FThreadSafeBool bIsInitialized;
    
    // Performance tracking
    TCircularBuffer<FOperationMetrics> PerformanceHistory;
    float AverageGPUUtilization;
    float CPUToGPUPerformanceRatio;
    
    // Resource management
    TMap<FRHIResource*, FResourceState> ResourceStateMap;
    TSharedPtr<FZeroCopyResourceManager> ZeroCopyManager;
};
```

### 2. FHardwareProfileManager
```cpp
class FHardwareProfileManager
{
public:
    FHardwareProfileManager();
    ~FHardwareProfileManager();
    
    // Hardware detection
    bool DetectHardwareCapabilities();
    const FHardwareProfile& GetCurrentProfile() const;
    
    // Profile selection and management
    bool LoadProfileForHardware(const FString& DeviceName, EGPUVendor VendorId);
    bool CreateCustomProfile(const FHardwareProfile& Profile);
    bool SaveProfiles();
    
    // Profile-based optimizations
    uint32 GetOptimalBlockSizeForOperation(ESDFOperationType OpType);
    bool ShouldUseAsyncCompute() const;
    bool SupportsRayTracing() const;
    
private:
    // Member variables
    TMap<FString, FHardwareProfile> KnownProfiles;
    FHardwareProfile CurrentProfile;
    bool bProfilesLoaded;
    TArray<FString> SupportedExtensions;
    
    // Hardware information
    uint32 ComputeUnits;
    uint32 TotalVRAM;
    FString GPUName;
    EGPUVendor GPUVendor;
    
    // Detection helpers
    void DetectGPUSpecs();
    void DetectMemoryLimits();
    void DetectShaderSupport();
};
```

### 3. FWorkloadDistributor
```cpp
class FWorkloadDistributor : public IWorkloadDistributor
{
public:
    FWorkloadDistributor();
    virtual ~FWorkloadDistributor();
    
    // Initialization with hardware profile
    bool Initialize(const FHardwareProfile& Profile);
    
    // IWorkloadDistributor Interface
    virtual EProcessingTarget DetermineProcessingTarget(const FComputeOperation& Operation) override;
    virtual void UpdatePerformanceMetrics(const FOperationMetrics& Metrics) override;
    virtual void ResetMetrics() override;
    
    // Workload splitting
    bool SplitOperation(const FComputeOperation& Operation, TArray<FComputeOperation>& OutSubOperations);
    bool MergeOperations(const TArray<FComputeOperation>& Operations, TArray<FOperationBatch>& OutBatches);
    
    // Adaptive scheduling
    void RefineDistributionStrategy(const FPerformanceHistory& History);
    
private:
    // Distribution strategies
    float CalculateOperationComplexity(const FComputeOperation& Operation);
    EProcessingTarget SelectTargetBasedOnComplexity(float Complexity);
    bool IsNarrowBandOperation(const FBox& Bounds, int32 MaterialChannel);
    
    // Learning system
    void UpdateDecisionModel(const FOperationMetrics& Metrics);
    float PredictGPUPerformance(const FComputeOperation& Operation);
    float PredictCPUPerformance(const FComputeOperation& Operation);
    
    // Member variables
    FDistributionConfig Config;
    TMap<ESDFOperationType, FOperationStats> OperationStats;
    FThreadSafeCounter64 GPUOperationCount;
    FThreadSafeCounter64 CPUOperationCount;
    TArray<float> CPUToGPUPerformanceRatio;
    FHardwareProfile HardwareProfile;
};
```

### 4. FSDFComputeKernelManager
```cpp
class FSDFComputeKernelManager
{
public:
    FSDFComputeKernelManager();
    ~FSDFComputeKernelManager();
    
    // Kernel registration and management
    bool RegisterKernel(ESDFOperationType OpType, const FSDFComputeKernel& Kernel);
    bool RegisterKernelPermutation(ESDFOperationType OpType, const FString& PermutationName, const FShaderVariant& Variant);
    
    // Kernel selection
    FSDFComputeKernel* GetBestKernelForOperation(ESDFOperationType OpType, const FComputeOperation& Operation);
    
    // Kernel fusion
    bool FuseKernels(const TArray<FSDFOperation>& OperationChain, FSDFComputeKernel& OutFusedKernel);
    
    // Shader compilation and caching
    bool PrecompileCommonKernels();
    void PurgeUnusedKernels();
    
    // Specialized kernels for mining operations
    FSDFComputeKernel* GetDrillOperationKernel(float Radius, bool bSmooth);
    FSDFComputeKernel* GetExplosiveOperationKernel(float Radius, float Falloff);
    FSDFComputeKernel* GetPrecisionToolKernel(const FBox& Bounds, bool bHighPrecision);
    
private:
    // Shader management
    bool CompileShader(FSDFComputeKernel& Kernel);
    bool LoadCachedShader(FSDFComputeKernel& Kernel);
    bool SaveShaderToCache(const FSDFComputeKernel& Kernel);
    
    // Member variables
    TMap<ESDFOperationType, TArray<FSDFComputeKernel>> Kernels;
    TMap<FString, TSharedPtr<FRHIComputeShader>> CompiledShaders;
    TMap<uint32, uint32> KernelUsageCounts;
    FString ShaderCachePath;
    FCriticalSection KernelLock;
};
```

### 5. FAsyncComputeCoordinator
```cpp
class FAsyncComputeCoordinator
{
public:
    FAsyncComputeCoordinator();
    ~FAsyncComputeCoordinator();
    
    // Initialization
    bool Initialize(bool bSupportsAsyncCompute);
    
    // Async compute scheduling
    uint64 ScheduleAsyncOperation(const FComputeOperation& Operation, EAsyncPriority Priority);
    bool CancelAsyncOperation(uint64 OperationId);
    bool WaitForCompletion(uint64 OperationId, uint32 TimeoutMS = ~0u);
    
    // Queue management
    void SetQueuePriorities(const TArray<EAsyncPriority>& Priorities);
    void SetFrameBudget(float MaxFrameTimeMS);
    void Flush(bool bWaitForCompletion);
    
    // Completion handling
    void RegisterCompletionCallback(uint64 OperationId, TFunction<void()> Callback);
    
private:
    // Internal command list management
    void ProcessCompletedOperations();
    bool DispatchPendingOperations();
    bool IsQueueFull(EAsyncPriority Priority) const;
    
    // Fence management
    FRHIGPUFence* AddFence(const TCHAR* Name);
    bool IsFenceComplete(FRHIGPUFence* Fence) const;
    void WaitForFence(FRHIGPUFence* Fence);
    
    // Member variables
    bool bAsyncComputeSupported;
    TMap<EAsyncPriority, TArray<FPendingAsyncOperation>> PriorityQueues;
    TMap<uint64, FOperationState> PendingOperations;
    TMap<uint64, TFunction<void()>> CompletionCallbacks;
    TMap<uint64, FRHIGPUFence*> OperationFences;
    float FrameBudgetMS;
    FCriticalSection QueueLock;
};
```

## 5. Critical Data Structures

### Core Structures
```cpp
// Hardware capability profile
struct FHardwareProfile {
    bool bSupportsRayTracing;
    bool bSupportsAsyncCompute;
    uint32 ComputeUnits;
    uint32 MaxWorkgroupSize;
    uint32 WavefrontSize;
    bool bSupportsWaveIntrinsics;
    uint32 SharedMemoryBytes;
    uint32 L1CacheSizeKB;
    uint32 L2CacheSizeKB;
    float ComputeToPipelineRatio;
    EGPUVendor VendorId;
    FString DeviceName;
};

// Operation metrics for performance tracking
struct FOperationMetrics {
    uint32 OperationTypeId;
    float CPUExecutionTimeMS;
    float GPUExecutionTimeMS;
    uint32 DataSize;
    uint32 IterationCount;
    float DeviceUtilization;
};

// Resource state tracking
struct FResourceState {
    ERHIAccess CurrentAccess;
    ERHIPipeline CurrentPipeline;
    uint32 LastFrameAccessed;
};

// Compute operation batch for better throughput
struct FOperationBatch {
    uint32 OperationTypeId;
    TArray<FBox> Regions;
    TArray<FMatrix> Transforms;
    TArray<float> Parameters;
    uint32 EstimatedCost;
    bool bUseWideExecutionStrategy;
};

// Compute shader variant
struct FShaderVariant {
    FString PermutationName;
    uint32 OptimizationLevel;
    uint32 FeatureBitmask;
    bool bEnableFastMath;
    bool bEnableSpecialIntrinsics;
    TArray<uint8> Flags;
    bool bDebugInfo;
};

// SDF operation specific data
struct FSDFOperationData {
    ESDFOperationType OperationType;
    int32 MaterialChannelId;
    FBox Bounds;
    float Strength;
    bool bUseNarrowBand;
    bool bUseHighPrecision;
    float BlendWeight;
};

// Narrow-band prioritization
struct FNarrowBandRegion {
    FBox Bounds;
    float Priority;
    int32 MaterialChannelId;
    bool bRequiresHighPrecision;
};

// Compute task configuration
struct FComputeTaskConfig {
    uint32 TypeId;
    uint32 OperationId;
    EPriority Priority;
    bool bAsynchronous;
    bool bCanBeBatched;
    bool bRequiresHighPrecision;
    float ImportanceScale;
    TFunction<void(bool)> CompletionCallback;
};

// Error handling & diagnostics
struct FComputeDispatchDiagnostics {
    bool bWasSuccessful;
    EComputeErrorType ErrorType;
    int32 NumOperationsDispatched;
    float DispatchTimeMS;
    bool bFallbackToCPU;
    FString ErrorMessage;
};
```

## 6. Integration with Unreal Engine Systems

### RDG (Render Dependency Graph) Integration
```cpp
void ExecuteComputePass(FRDGBuilder& GraphBuilder, const FShaderParametersMetadata& ShaderParametersMetadata)
{
    // Create compute pass
    FRDGComputePassParameters* PassParameters = GraphBuilder.AllocParameters<FRDGComputePassParameters>();
    
    // Execute the compute pass with proper resource transitions
    GraphBuilder.AddComputePass(
        TEXT("SDFOperation"),
        PassParameters,
        ERDGPassFlags::Compute,
        [this, PassParameters](FRHIComputeCommandList& RHICmdList) {
            // Dispatch compute shader
        }
    );
}
```

### Zero-Copy Buffer Management
```cpp
class FZeroCopyResourceManager {
public:
    // Pin memory for zero-copy access
    void* PinMemory(void* CPUAddress, SIZE_T Size, uint32& OutBufferIndex);
    
    // Get GPU address for pinned memory
    FRHIGPUBufferReadback* GetGPUBuffer(uint32 BufferIndex);
    
    // Release pinned memory
    void ReleaseMemory(uint32 BufferIndex);
};
```

### Fence Synchronization
```cpp
// Synchronization between CPU and GPU work
class FSyncPoints {
public:
    // Create a fence to signal when GPU work is complete
    FRHIGPUFence* AddFence(const TCHAR* Name);
    
    // Wait for a fence to be signaled
    void WaitForFence(FRHIGPUFence* Fence);
    
    // Check if a fence has been signaled
    bool IsFenceSignaled(FRHIGPUFence* Fence);
};
```

### Critical RHI/Render Graph Headers
```cpp
// Include these headers for GPU resource access
#include "RenderGraphResources.h"
#include "RenderGraphBuilder.h"
#include "ShaderParameterStruct.h"
#include "RHICommandList.h"
#include "ShaderCore.h"
#include "PipelineStateCache.h"
#include "ShaderParameterUtils.h"
#include "ComputeShaderUtils.h"
```

## 7. Implementation Sequence

1. Create interface classes first:
   - `IComputeDispatcher.h`
   - `IWorkloadDistributor.h`

2. Implement the hardware detection and profile system:
   - `FHardwareProfileManager`
   - Define capability flags and profile structures

3. Implement the compute kernel management:
   - `FSDFComputeKernelManager`
   - Define shader parameters and dispatch logic

4. Implement the workload distributor:
   - `FWorkloadDistributor`
   - Create decision logic for CPU vs GPU dispatch

5. Implement the main dispatcher:
   - `FGPUDispatcher`
   - Register with service locator
   - Integrate with memory system for zero-copy buffers

6. Implement the async compute coordinator:
   - `FAsyncComputeCoordinator`
   - Integrate with threading system

## 8. Advanced Integration Methods

### Register with Service Locator
```cpp
bool RegisterWithServiceLocator()
{
    // Register the dispatcher as an IComputeDispatcher
    IServiceLocator::Get().RegisterService<IComputeDispatcher>(this);
    
    // Declare dependencies
    IServiceLocator::Get().DeclareDependency<IComputeDispatcher, IMemoryManager>(EServiceDependencyType::Required);
    IServiceLocator::Get().DeclareDependency<IComputeDispatcher, ITaskScheduler>(EServiceDependencyType::Required);
    
    return true;
}
```

### Initialize Zero-Copy Buffers
```cpp
bool InitializeZeroCopyBuffers()
{
    FZeroCopyBuffer* ZeroCopyBuffer = IServiceLocator::Get().ResolveService<FZeroCopyBuffer>();
    if (!ZeroCopyBuffer)
    {
        return false;
    }
    
    // Initialize buffers for SDF operations
    return true;
}
```

### Configure SDF Operations for GPU
```cpp
bool ConfigureSDFOperationsForGPU()
{
    FSDFTypeRegistry& Registry = FSDFTypeRegistry::Get();
    
    // Configure operations based on hardware capabilities
    for (const FSDFOperationInfo& OperationInfo : Registry.GetAllOperations())
    {
        if (Registry.IsOperationGPUCompatible(OperationInfo.OperationType))
        {
            // Configure for GPU execution
        }
    }
    
    return true;
}
```

### Register Shader Permutations
```cpp
void RegisterShaderPermutations() {
    // Register different permutations based on precision needs
    // e.g., high precision for detail areas, lower precision for broad areas
}
```

### Coordinate Cross-Region Operations
```cpp
bool CoordinateCrossRegionOperation(const TArray<int32>& RegionIds, const FSDFOperation& Operation) {
    // Coordinate operations that span multiple regions
    // Ensure proper boundary handling
}
```

### Frame Budget Management
```cpp
bool AllocateFrameBudget(float& OutBudgetMS) {
    // Allocate appropriate frame time budget for compute operations
    // Prevent frame rate drops during heavy compute
}
```

### Kernel Fusion for Operation Chains
```cpp
bool FuseKernels(const TArray<FSDFOperation>& OperationChain, FSDFComputeKernel& OutFusedKernel) {
    // Combine multiple operations into a single optimized kernel
    // Reduce dispatch overhead for common operation sequences
}
```

### Material Channel Optimizations
```cpp
void OptimizeMaterialChannelOperations(int32 MaterialChannelId, TArray<FSDFOperation>& Operations) {
    // Apply material-specific optimizations to SDF operations
    // Different materials may have different optimal algorithms
}
```

### Visualization System Integration
```cpp
void SetupVisualizationFeedback(FRDGBuilder& GraphBuilder, FRDGTexture* OutputTexture) {
    // Provide visual feedback for debugging and monitoring
}
```

## 9. Optimization Strategies

### 1. Batch Similar Operations
```cpp
// Create batches of similar operations for efficient processing
TArray<FOperationBatch> CreateOptimalBatches(const TArray<FPendingOperation>& PendingOps)
{
    TArray<FOperationBatch> Batches;
    
    // Sort operations by type
    TMap<uint32, TArray<FPendingOperation>> OperationsByType;
    for (const FPendingOperation& Op : PendingOps)
    {
        OperationsByType.FindOrAdd(Op.OperationType).Add(Op);
    }
    
    // Create batches for each type
    for (const auto& TypePair : OperationsByType)
    {
        FOperationBatch Batch;
        Batch.OperationTypeId = TypePair.Key;
        
        // Group operations by spatial locality
        // Add operations to the batch that are spatially coherent
        // This improves cache locality and reduces dispatch overhead
        
        Batches.Add(Batch);
    }
    
    return Batches;
}
```

### 2. Adaptive CPU/GPU Decision Making
```cpp
EProcessingTarget DetermineProcessingTarget(const FComputeOperation& Operation)
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

### 3. Narrow-Band Prioritization
```cpp
void PrioritizeNarrowBandRegions(TArray<FNarrowBandRegion>& Regions)
{
    // Sort regions by priority
    Regions.Sort([](const FNarrowBandRegion& A, const FNarrowBandRegion& B) {
        return A.Priority > B.Priority;
    });
    
    // Process high-priority regions first
    // This ensures the most important areas get updated promptly
}
```

### 4. Shader Specialization
```cpp
FSDFComputeKernel* GetBestKernelForOperation(ESDFOperationType OpType, const FComputeOperation& Operation)
{
    bool bHighPrecision = Operation.bRequiresHighPrecision;
    bool bNarrowBand = Operation.bUseNarrowBand;
    
    // Select specialized kernel based on operation parameters
    FString KernelVariant = FString::Printf(TEXT("%s_%s_%s"),
        *GetOperationTypeName(OpType),
        bHighPrecision ? TEXT("HighPrecision") : TEXT("StandardPrecision"),
        bNarrowBand ? TEXT("NarrowBand") : TEXT("FullField"));
    
    // Find the best matching kernel
    return FindKernel(KernelVariant);
}
```

## 10. Potential Pitfalls and Solutions

### Thread Safety Issues
- Always use proper locking when accessing shared data structures
- Use thread-safe containers and atomic operations for counters
- Ensure memory visibility with proper memory barriers

### Memory Management
- Always release GPU resources when no longer needed
- Use Unreal's resource tracking mechanisms
- Consider NUMA awareness for CPU-side memory operations

### Hardware Compatibility
- Always provide CPU fallbacks for all operations
- Test on a variety of hardware configurations
- Use feature detection rather than hardcoded vendor checks

### Error Handling
- Properly handle all shader compilation errors
- Gracefully handle GPU memory allocation failures
- Have robust fallback mechanisms for when GPU operations fail

### Performance Degradation
- Monitor operation performance over time
- Adapt to changing conditions (thermal throttling, etc.)
- Have clear thresholds for switching to CPU processing

### Memory Bandwidth Limitations
- Batch operations to minimize memory transfers
- Use zero-copy buffers for large data sets
- Consider compression for SDF field data

### Synchronization Issues
- Proper use of fences for GPU-CPU synchronization
- Clear ownership rules for shared resources
- Careful handling of dependencies between operations

### Resource Transitions
- Minimize state transitions for better performance
- Track resource states to avoid redundant transitions
- Use RDG for automatic resource handling where possible

### Debuggability
- Add comprehensive error reporting
- Include visualization options for debugging
- Support RGP/GPU performance profiling tools
