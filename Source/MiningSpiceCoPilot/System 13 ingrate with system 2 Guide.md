# System 13 GPU Compute Dispatcher Memory Integration Guide

This comprehensive guide outlines all critical components in the Memory Management subsystem that System 13 (GPU Compute Dispatcher) needs to integrate with. Understanding these integration points will help ensure a well-integrated implementation without duplicating functionality.

## 1. Critical Memory Management Classes

### Core Manager Classes
- **FMemoryPoolManager** - Central memory manager implementation
  - `GetPool(FName PoolName)` - Retrieves pool allocators
  - `CreateBuffer(FName, uint64 Size, bool bZeroCopy, bool bGPUWritable)` - Creates shared buffers
  - `GetBuffer(FName BufferName)` - Retrieves shared buffers
  - `RegisterAllocation/UnregisterAllocation` - For tracking memory usage
  - `DefragmentMemory()` - Critical for managing fragmentation during mining operations
  - `IsUnderMemoryPressure` - Detects system memory constraints
  - `ReduceMemoryUsage` - Forces memory reduction under pressure
  - `AdjustPoolSizes` - Dynamically adjusts pool sizes based on usage
  - `SetNUMAPolicy` - Controls NUMA awareness for improved memory access

### Specialized Allocators
- **FNarrowBandAllocator** - Optimized for SDF operations
  - `SetPrecisionTier()` - Controls precision for SDF computations
  - `SetChannelCount()` - Sets number of material channels
  - `ConfigureSIMDLayout()` - Critical for compute shader alignment requirements
  - `OptimizeNarrowBand()` - Specifically optimizes memory layout for narrow-band SDF
  - `SetDistanceFromSurface()` - Tags memory with distance from surface for prioritization
  - `PackBlocksByPosition()` - Reorganizes memory by spatial position
  - `AllocateChannelMemory()` - Allocates specific material channel memory
  - `SetupSharedChannels()` - Establishes channel sharing between materials
  - Multiple precision tiers: `EMemoryTier::Hot`, `EMemoryTier::Warm`, etc.

- **FSVOAllocator** - Manages SVO node memory
  - `SetZOrderMappingFunction()` - Optimizes spatial locality (critical for compute operations)
  - `ConfigureTypeLayout()` - Configures memory layouts for specific types
  - `LookupTableZOrderMapping()` - Optimized lookup-based mapping
  - `PositionToZOrder()` - Converts 3D coordinates to memory-coherent 1D index

### Buffer Management 
- **FZeroCopyBuffer** - Essential for GPU compute operations
  - `Map/Unmap` - Provides CPU access to buffer contents
  - `SyncToGPU/SyncFromGPU` - Synchronizes memory between CPU and GPU
  - `GetRHIBuffer()` - Provides direct access to the RHI buffer
  - `GetShaderResourceView()` - Gets the SRV for shader binding
  - `GetUnorderedAccessView()` - Gets the UAV for compute shader writing
  - `SetActiveMiningState()` - Optimizes buffer for active mining operations
  - `RecordMemoryAccess()` - Tracks memory access patterns to predict future access

- **FSharedBufferManager** - Provides buffer sharing between systems
  - `CreateZone()` - Creates named subregions for more granular control
  - `MapZone/UnmapZone` - Map specific regions of memory
  - `Synchronize()` - Ensures data consistency

### Memory Telemetry
- **FMemoryTelemetry** - Tracks memory usage and performance
  - `UpdateSVOSDFMetrics()` - Updates SDF and SVO memory metrics
  - `SetAllocationTier/AccessPattern` - Helps optimize memory usage
  - `CalculateFragmentation()` - Measures memory fragmentation
  - `GetMemoryUsageTimeline()` - Historical memory usage data
  - `GetMemoryPressure()` - System memory pressure metric
  - `TakeMemorySnapshot/CompareWithSnapshot()` - Performance comparison tools

### Compression Systems
- **FCompressionUtility** - Provides specialized compression for SDF data
  - `CompressSDFData/DecompressSDFData` - Specialized for SDF field compression
  - `CompressHomogeneousSDFRegion` - Optimized for uniform regions (critical for performance)
  - `CreateDeltaCompression/ApplyDeltaDecompression` - Essential for efficient updates
  - `AnalyzeDataForCompression` - Auto-selects optimal compression algorithm
  - Support for multiple compression algorithms: RLE, LZ4, Zlib, Zstd, Delta

### Memory Defragmentation
- **FMemoryDefragmenter** - Handles memory defragmentation
  - `MoveNextFragmentedAllocation()` - Crucial for maintaining memory coherence
  - `RegisterAllocationReferences/UpdateReferences` - Updates pointers during compaction
  - `RegisterVersionedType()` - Important for handling type migration during updates
  - `SetDefragmentationThreshold()` - Controls when defragmentation activates
  - `CalculateFragmentation()` - Monitors memory fragmentation levels

## 2. Key Interface Implementations

### Memory Management Interfaces
- **IMemoryManager** - Central memory management interface
  - `Get()` - Static accessor to retrieve the singleton instance
  - `GetPoolForType()` - Retrieves pool allocator for specific type ID
  - `ConfigurePoolCapabilities()` - Configures memory pools based on hardware

- **IPoolAllocator** - Interface for memory pools
  - `GetAccessPattern()` - Retrieves the access pattern
  - `MoveNextFragmentedAllocation()` - Used during defragmentation
  - `UpdateTypeVersion()` - Handles memory layout changes during updates
  - `SetNumaNode()` - Controls physical memory location for NUMA optimization

- **IBufferProvider** - Interface for buffer management
  - `GetBufferName()` - Returns the name of the buffer
  - `GetGPUResource()` - Returns the underlying GPU resource

## 3. Critical RHI Integration Points

### Unreal Engine RHI Integration
- **RenderGraphBuilder (RDG)**
  - The GPU dispatcher should leverage RDG for efficient GPU resource management
  - All compute dispatches should use RDG to ensure proper resource transitions

- **Compute Shaders**
  - Must use appropriate parameters structs compatible with `ShaderParameterStruct.h`
  - Need to handle resource transitions properly using RDG

- **Required Headers**
  ```cpp
  #include "RenderGraphResources.h"
  #include "RenderGraphBuilder.h"
  #include "ShaderParameterStruct.h"
  #include "RHICommandList.h"
  #include "ShaderCore.h"
  #include "PipelineStateCache.h"
  #include "ShaderParameterUtils.h"
  #include "ComputeShaderUtils.h"
  ```

## 4. Memory Access Pattern Optimization

### Access Patterns to Support
- **EMemoryAccessPattern::SDFOperation** - Optimized for SDF field calculations
- **EMemoryAccessPattern::OctreeTraversal** - Optimized for SVO traversal
- **EMemoryAccessPattern::Mining** - Focused locality around active zones

### Memory Tiers
- **EMemoryTier::Hot** - High precision (32-bit float), used for active mining areas
- **EMemoryTier::Warm** - Medium precision (16-bit float)
- **EMemoryTier::Cold** - Low precision (8-bit), used for distant regions
- **EMemoryTier::Archive** - Compressed format for inactive regions

### SIMD Instruction Sets
- **ESIMDInstructionSet enum**
  - `None`, `SSE`, `AVX`, `AVX2`, `Neon` instruction set support
  - Critical for ensuring memory alignment matches compute shader expectations

## 5. Thread Safety Considerations

### Critical Sections
- Most memory managers use `FScopeLock` with a private `FCriticalSection`
- Always acquire locks in the same order to prevent deadlocks
- The GPU dispatcher should follow the same lock ordering

### Atomic Operations
- Use `FThreadSafeCounter` for thread-safe counters
- `FThreadSafeBool` for boolean flags accessed from multiple threads

## 6. Service Locator Integration

### Registration Pattern
The GPU dispatcher should register itself with the service locator:
```cpp
// From the integration guide:
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

### Service Resolution
To access memory systems:
```cpp
IMemoryManager* MemoryManager = IServiceLocator::Get().ResolveService<IMemoryManager>();
FNarrowBandAllocator* NBAllocator = static_cast<FNarrowBandAllocator*>(MemoryManager->GetPool(FName("HighPrecisionNBPool")));
FZeroCopyBuffer* ZeroCopyBuffer = IServiceLocator::Get().ResolveService<FZeroCopyBuffer>();
```

## 7. SDF Type Registry Integration

### Critical Methods
- `RegisterFieldType()` - Register a new SDF field type
- `RegisterOperation()` - Register a new SDF operation
- `IsOperationGPUCompatible()` - Check if an operation can run on GPU
- `GetOptimalThreadBlockSize()` - Get optimal thread block size for compute

## 8. Type Version Migration System

### Type Version Management
- **FTypeVersionMigrationInfo**
  - Critical for handling memory layout changes during updates
  - `UpdateTypeVersion` methods in allocators handle migration
  - Ensures backward compatibility of memory structures

## 9. Implementation Examples

### Initialize Memory Resources
```cpp
bool InitializeMemoryResources()
{
    // Get memory manager
    MemoryManager = IServiceLocator::Get().ResolveService<IMemoryManager>();
    if (!MemoryManager)
    {
        return false;
    }
    
    // Get critical memory pools
    NarrowBandPool = static_cast<FNarrowBandAllocator*>(
        MemoryManager->GetPool(FName("HighPrecisionNBPool")));
    
    // Create zero-copy buffers for compute operations
    for (const auto& MaterialType : MaterialRegistry.GetAllMaterialTypes())
    {
        FZeroCopyBuffer* Buffer = static_cast<FZeroCopyBuffer*>(
            MemoryManager->CreateBuffer(
                FName(*FString::Printf(TEXT("Material_%u_Buffer"), MaterialType.TypeId)),
                MaterialType.MemoryRequirements,
                true, // Zero-copy
                true  // GPU writable
            )
        );
        
        if (Buffer)
        {
            MaterialBuffers.Add(MaterialType.TypeId, Buffer);
        }
    }
    
    return true;
}
```

### Execute SDF Compute Operation
```cpp
bool ExecuteSDFOperation(const FSDFOperation& Operation)
{
    // Get buffer for this material
    FZeroCopyBuffer* Buffer = MaterialBuffers.FindRef(Operation.MaterialTypeId);
    if (!Buffer)
    {
        return false;
    }
    
    // Map buffer for current access mode
    void* Data = Buffer->Map(EBufferAccessMode::ReadWrite);
    if (!Data)
    {
        return false;
    }
    
    // Get RHI buffer for compute shader
    FRHIBuffer* RHIBuffer = Buffer->GetRHIBuffer();
    FRHIUnorderedAccessView* UAV = Buffer->GetUnorderedAccessView();
    
    // Execute compute shader...
    
    // Unmap buffer, ensuring changes are visible
    Buffer->Unmap();
    
    return true;
}
```

### Compress SDF Region
```cpp
bool CompressSDFRegion(const FBox& Region, uint32 MaterialTypeId) 
{
    FZeroCopyBuffer* Buffer = GetMaterialBuffer(MaterialTypeId);
    void* Data = Buffer->Map(EBufferAccessMode::ReadWrite);
    
    void* CompressedData = nullptr;
    uint32 CompressedSize = 0;
    
    bool Success = FCompressionUtility::CompressSDFData(
        Data, Buffer->GetBufferSize(), 
        CompressedData, CompressedSize,
        GetMaterialChannelCount(MaterialTypeId),
        ECompressionLevel::Normal
    );
    
    Buffer->Unmap();
    return Success;
}
```

### Optimize Memory Locality for NUMA
```cpp
void OptimizeMemoryLocality()
{
    // For GPU compute, locating memory close to the PCIe controller is optimal
    IMemoryManager* MemoryManager = IServiceLocator::Get().ResolveService<IMemoryManager>();
    
    // Get NUMA node closest to GPU
    int32 GpuPreferredNode = HardwareProfileManager->GetGPUPreferredNumaNode();
    
    // Configure pools to use this node
    for (const auto& PoolName : MemoryManager->GetPoolNames())
    {
        IPoolAllocator* Pool = MemoryManager->GetPool(PoolName);
        if (Pool)
        {
            Pool->SetNumaNode(GpuPreferredNode);
        }
    }
}
```

### Configure Memory for Vectorized Compute
```cpp
void ConfigureMemoryForVectorizedCompute(uint32 MaterialTypeId)
{
    FNarrowBandAllocator* Allocator = static_cast<FNarrowBandAllocator*>(
        MemoryManager->GetPoolForType(MaterialTypeId)
    );
    
    if (Allocator)
    {
        // Configure for AVX2 with 32-byte alignment
        // This ensures compute shaders can efficiently process data
        Allocator->ConfigureSIMDLayout(
            MaterialTypeId,
            32, // 32-byte alignment for AVX2
            true, // Enable vectorization
            ESIMDInstructionSet::AVX2
        );
    }
}
```

### Monitor Memory Pressure
```cpp
void MonitorMemoryPressure()
{
    IMemoryManager* MemoryManager = IServiceLocator::Get().ResolveService<IMemoryManager>();
    
    uint64 AvailableBytes = 0;
    if (MemoryManager->IsUnderMemoryPressure(&AvailableBytes))
    {
        // Scale back compute operations
        WorkloadDistributor->AdjustForMemoryPressure(AvailableBytes);
        
        // Use more CPU operations to reduce GPU memory pressure
        WorkloadDistributor->IncreaseCPUWorkloadRatio(0.3f);
        
        // Release non-critical compute resources
        ReleaseInactiveComputeResources();
    }
}
```

### Optimize Spatial Memory Layout
```cpp
void OptimizeSpatialMemoryLayout()
{
    FSVOAllocator* SvoAllocator = static_cast<FSVOAllocator*>(
        MemoryManager->GetPool(FName("SVONodePool"))
    );
    
    if (SvoAllocator)
    {
        // Configure for optimal spatial locality
        SvoAllocator->SetZOrderMappingFunction(&FSVOAllocator::LookupTableZOrderMapping);
        
        // Configure specific node types for Z-order curve optimization
        SvoAllocator->ConfigureTypeLayout(
            GetNodeTypeId(),
            true, // Use Z-order curve
            true, // Enable prefetching
            EMemoryAccessPattern::OctreeTraversal
        );
    }
}
```

### Register Compute Buffers with Defragmenter
```cpp
void RegisterComputeBuffersWithDefragmenter(FMemoryDefragmenter* Defragmenter)
{
    for (const auto& BufferPair : MaterialBuffers)
    {
        // Register buffers that may be relocated during defragmentation
        TArray<void*> References;
        // Collect references to this buffer
        Defragmenter->RegisterAllocationReferences(BufferPair.Value->GetRawBuffer(), References);
    }
}
```

### Set Up Material Channel Compute
```cpp
void SetupMaterialChannelCompute(uint32 MaterialTypeId)
{
    const FMaterialTypeInfo* MaterialInfo = MaterialRegistry->GetMaterialTypeInfo(MaterialTypeId);
    if (!MaterialInfo)
    {
        return;
    }
    
    // Get material channel allocator
    FNarrowBandAllocator* ChannelAllocator = static_cast<FNarrowBandAllocator*>(
        MemoryManager->GetPool(FName("MaterialChannelPool"))
    );
    
    if (ChannelAllocator)
    {
        // Allocate memory for this material's channels
        int32 ChannelId = ChannelAllocator->AllocateChannelMemory(
            MaterialTypeId,
            MaterialInfo->ChannelCount,
            EMemoryTier::Hot, // Use highest precision for active materials
            EMaterialCompressionLevel::Normal
        );
        
        // Set up shared channels for inherited material properties
        if (MaterialInfo->ParentTypeId != 0)
        {
            ChannelAllocator->SetupSharedChannels(
                MaterialTypeId,
                MaterialInfo->ParentTypeId,
                ChannelId,
                GetParentChannelId(MaterialInfo->ParentTypeId)
            );
        }
        
        // Register channel with compute shader system
        KernelManager->RegisterMaterialChannels(MaterialTypeId, ChannelId, MaterialInfo->ChannelCount);
    }
}
```

### Optimize Narrow-Band for Compute
```cpp
void OptimizeNarrowBandForCompute(const FBox& Region, uint32 MaterialTypeId)
{
    FNarrowBandAllocator* NBAllocator = static_cast<FNarrowBandAllocator*>(
        MemoryManager->GetPool(FName("HighPrecisionNBPool"))
    );
    
    if (NBAllocator)
    {
        // Set mining direction for prefetching
        NBAllocator->SetMiningDirection(GetActiveToolDirection());
        
        // Reorganize memory by spatial position
        NBAllocator->PackBlocksByPosition(5.0f); // 5ms time budget
        
        // Optimize narrow-band layout
        NBAllocator->OptimizeNarrowBand(10.0f); // 10ms time budget
    }
}
```

### Track Compute Performance
```cpp
void TrackComputePerformance()
{
    IMemoryTracker* MemoryTracker = MemoryManager->GetMemoryTracker();
    if (!MemoryTracker)
    {
        return;
    }
    
    // Take pre-operation snapshot
    MemoryTracker->TakeMemorySnapshot(TEXT("BeforeSDFOperation"));
    
    // Perform SDF operation
    // ...
    
    // Take post-operation snapshot
    MemoryTracker->TakeMemorySnapshot(TEXT("AfterSDFOperation"));
    
    // Compare and analyze
    FString PerformanceReport = MemoryTracker->CompareWithSnapshot(TEXT("BeforeSDFOperation"));
    
    // Update performance model based on results
    UpdatePerformanceModel(PerformanceReport);
}
```

### Update SDF Type Version
```cpp
void UpdateSDFTypeVersion(uint32 TypeId, uint32 OldVersion, uint32 NewVersion)
{
    IPoolAllocator* Pool = MemoryManager->GetPoolForType(TypeId);
    if (!Pool)
    {
        return;
    }
    
    // Create migration info
    FTypeVersionMigrationInfo MigrationInfo;
    MigrationInfo.TypeId = TypeId;
    MigrationInfo.TypeName = FName(*FString::Printf(TEXT("SDFType_%u"), TypeId));
    MigrationInfo.OldVersion = OldVersion;
    MigrationInfo.NewVersion = NewVersion;
    
    // Update version and migrate memory layout
    if (Pool->UpdateTypeVersion(MigrationInfo))
    {
        // Update compute shaders for new memory layout
        KernelManager->UpdateShaderForTypeVersion(TypeId, NewVersion);
    }
}
```

### Optimize Memory Prefetching
```cpp
void OptimizeMemoryPrefetching(const FBox& ActiveRegion)
{
    for (const auto& BufferPair : MaterialBuffers)
    {
        FZeroCopyBuffer* Buffer = BufferPair.Value;
        if (!Buffer)
        {
            continue;
        }
        
        // Enable active mining state for prefetching
        Buffer->SetActiveMiningState(true);
        
        // Optimize for mining operation patterns
        uint64 Offset = CalculateBufferOffset(ActiveRegion, BufferPair.Key);
        uint64 Size = CalculateRegionSize(ActiveRegion);
        
        // Record access pattern to improve future prefetching
        Buffer->RecordMemoryAccess(Offset, Size);
    }
}
```

## 10. Core Dispatcher Class Structure

```cpp
// Main GPU Compute Dispatcher implementation
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
    
    // Memory management integration
    bool InitializeMemoryResources();
    void RegisterComputeBuffersWithDefragmenter();
    void OptimizeMemoryLocality();
    void MonitorMemoryPressure();
    void TrackComputePerformance();
    
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
    
    // Memory system integration
    IMemoryManager* MemoryManager;
    TMap<uint32, FZeroCopyBuffer*> MaterialBuffers;
    TMap<uint32, FNarrowBandAllocator*> NarrowBandAllocators;
    
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

This comprehensive guide provides all the critical integration points necessary for implementing System 13 (GPU Compute Dispatcher) with the existing Memory Management subsystem. Following these guidelines will ensure optimal performance, proper memory handling, and seamless integration with the overall architecture.
