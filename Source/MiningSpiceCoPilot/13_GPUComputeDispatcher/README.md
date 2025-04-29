# System 13: GPU Compute Dispatcher

## Overview

The GPU Compute Dispatcher system provides efficient GPU compute workload distribution specifically optimized for SDF operations. It implements adaptive CPU/GPU workload balancing, specialized compute shaders for mining operations, and intelligent batching for SDF field updates with hardware-specific optimizations.

## Key Components

- **GPUDispatcher**: Central component that manages compute shader dispatch for SDF operations
- **HardwareProfileManager**: Handles hardware capability detection and profile management for optimized SDF operations
- **WorkloadDistributor**: Provides dynamic workload distribution for SDF operations between CPU and GPU
- **SDFComputeKernelManager**: Manages specialized compute shaders for SDF operations
- **AsyncComputeCoordinator**: Handles asynchronous compute scheduling for non-critical SDF updates
- **ZeroCopyResourceManager**: Manages zero-copy memory for efficient CPU/GPU data sharing

## Integration Points

- **System 1 (Core Registry)**: For service registration and type management
- **System 2 (Memory Management)**: For memory allocation and zero-copy buffers
- **System 3 (Threading)**: For task scheduling and asynchronous compute
- **System 6 (Service Registry)**: For service registration and dependency management

## Key Capabilities

- Specialized compute shader dispatch for SDF field operations (union, subtraction, intersection)
- Adaptive workload distribution between CPU and GPU based on operation characteristics
- Efficient batching of similar SDF operations for reduced dispatch overhead
- Real-time performance monitoring to guide distribution decisions with learning capability 
- Hardware-specific optimization profiles with runtime refinement
- Narrow-band SDF update prioritization with importance-based scheduling
- Zero-copy memory architecture for GPU/CPU field data sharing
- Resource state tracking to minimize barriers during SDF operations
- Support for multi-channel SDF fields with material-specific operations

## Usage

```cpp
// Get the compute dispatcher from the service locator
IComputeDispatcher* ComputeDispatcher = IServiceLocator::Get().ResolveService<IComputeDispatcher>();

// Create a compute operation
FComputeOperation Operation;
Operation.OperationType = 0; // Union operation
Operation.Bounds = FBox(FVector(-100, -100, -100), FVector(100, 100, 100));
Operation.MaterialChannelId = 1;
Operation.Strength = 1.0f;
Operation.bUseNarrowBand = true;
Operation.bRequiresHighPrecision = false;

// Dispatch compute operation
bool bSuccess = ComputeDispatcher->DispatchCompute(Operation);

// Alternatively, dispatch asynchronously with completion callback
ComputeDispatcher->DispatchComputeAsync(Operation, [](bool bSuccess, float ElapsedMs) {
    // Handle completion
    UE_LOG(LogGPUDispatcher, Log, TEXT("Operation completed: Success=%d, Time=%.2fms"), 
        bSuccess ? 1 : 0, ElapsedMs);
});
```

## Advanced Features

### Hardware Profiling

The system automatically profiles the hardware on initialization and creates optimized settings for the current GPU. It takes into account:

- GPU compute capability
- Memory bandwidth
- Shader model support
- Vendor-specific optimizations

### Adaptive Distribution

The workload distributor learns from past operations to improve future distribution decisions:

- Tracks performance of operations on CPU vs GPU
- Considers operation complexity, size, and type
- Adapts to changing conditions (memory pressure, thermal throttling)
- Falls back to CPU for operations that perform better there

### Zero-Copy Memory

Efficient memory sharing between CPU and GPU:

- Minimizes data transfers between CPU and GPU
- Supports direct GPU access to CPU memory when available
- Falls back to efficient transfer methods on platforms without shared memory

### Batch Processing

Intelligent batching of similar operations:

- Groups operations by type and spatial locality
- Reduces dispatch overhead and improves cache utilization
- Supports dynamic batch sizing based on operation complexity

## Threading Model

The system is fully thread-safe and integrates with the Threading Task System (System 3):

- All public methods can be called from any thread
- Internal operations use appropriate synchronization
- Asynchronous operations integrate with the AsyncTaskManager
- Performance-critical paths are optimized for low overhead

## Performance Considerations

- Use batching for small, similar operations
- Consider spatial locality when grouping operations
- Prefer async dispatch for non-critical updates
- Use narrow-band optimization for interface regions
- Monitor performance metrics for optimization opportunities