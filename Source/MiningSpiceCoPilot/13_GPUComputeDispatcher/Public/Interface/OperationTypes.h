#pragma once

#include "CoreMinimal.h"

/**
 * Enum for workload distribution recommendations
 */
enum class EWorkloadTarget : uint8
{
    GPU,            // Process on GPU
    CPU,            // Process on CPU
    Hybrid,         // Split between CPU and GPU
    CPUFallback,    // Prefer CPU fallback
    GPUFallback,    // Prefer GPU fallback
    Auto            // Let distributor decide
};

/**
 * Operation execution mode
 */
enum class EOperationExecutionMode : uint8
{
    Parallel,       // Execute in parallel
    Sequential,     // Execute sequentially
    Interleaved,    // Execute with interleaved pattern
    Tiled           // Execute in tiles
};

/**
 * Memory access pattern for operations
 */
enum class EOperationMemoryPattern : uint8
{
    Sequential,     // Sequential memory access
    Scattered,      // Scattered memory access
    Strided,        // Strided memory access
    Random          // Random access pattern
};

/**
 * Operation complexity class
 */
enum class EOperationComplexity : uint8
{
    Trivial,        // O(1) operations
    Simple,         // O(log n) operations
    Linear,         // O(n) operations
    Linearithmic,   // O(n log n) operations
    Quadratic,      // O(n²) operations
    Complex         // Higher complexity
};

/**
 * Operation synchronization requirements
 */
enum class EOperationSynchronization : uint8
{
    None,           // No synchronization required
    Lightweight,    // Minimal synchronization
    Moderate,       // Some synchronization points
    Heavy           // Many synchronization points
};

/**
 * Operation data locality
 */
enum class EOperationDataLocality : uint8
{
    High,           // High data locality
    Medium,         // Medium data locality
    Low             // Low data locality
};

/**
 * Operation scaling behavior
 */
enum class EOperationScaling : uint8
{
    Excellent,      // Scales almost linearly
    Good,           // Scales well
    Fair,           // Scales moderately
    Poor            // Scales poorly
};

/**
 * Operation profiling information
 */
struct FOperationProfile
{
    // Operation identification
    uint32 OperationId;
    FName OperationName;
    
    // Execution characteristics
    EOperationExecutionMode PreferredExecutionMode;
    EOperationMemoryPattern MemoryPattern;
    EOperationComplexity Complexity;
    EOperationSynchronization SynchronizationRequirements;
    EOperationDataLocality DataLocality;
    EOperationScaling Scaling;
    
    // Performance metrics
    float AverageGPUTimeMS;
    float AverageCPUTimeMS;
    int32 SampleCount;
    
    // Memory metrics
    uint32 AverageGPUMemoryUsage;
    uint32 AverageCPUMemoryUsage;
    
    // Optimal parameters
    FIntVector OptimalThreadGroupSize;
    FIntVector OptimalThreadGroupCount;
    int32 OptimalBatchSize;
    int32 OptimalCPUThreadCount;
    
    // Constructor with sensible defaults
    FOperationProfile()
        : OperationId(0)
        , PreferredExecutionMode(EOperationExecutionMode::Parallel)
        , MemoryPattern(EOperationMemoryPattern::Sequential)
        , Complexity(EOperationComplexity::Linear)
        , SynchronizationRequirements(EOperationSynchronization::None)
        , DataLocality(EOperationDataLocality::Medium)
        , Scaling(EOperationScaling::Good)
        , AverageGPUTimeMS(0.0f)
        , AverageCPUTimeMS(0.0f)
        , SampleCount(0)
        , AverageGPUMemoryUsage(0)
        , AverageCPUMemoryUsage(0)
        , OptimalThreadGroupSize(8, 8, 1)
        , OptimalThreadGroupCount(1, 1, 1)
        , OptimalBatchSize(16)
        , OptimalCPUThreadCount(4)
    {
    }
};

/**
 * Hardware acceleration features
 */
enum class EHardwareAcceleration : uint32
{
    None        = 0,
    SSE         = 1 << 0,
    SSE2        = 1 << 1,
    SSE3        = 1 << 2,
    SSSE3       = 1 << 3,
    SSE4_1      = 1 << 4,
    SSE4_2      = 1 << 5,
    AVX         = 1 << 6,
    AVX2        = 1 << 7,
    AVX512      = 1 << 8,
    NEON        = 1 << 9,
    GPUCompute  = 1 << 10,
    CUDA        = 1 << 11,
    OpenCL      = 1 << 12,
    DirectCompute = 1 << 13,
    Metal       = 1 << 14
};

/**
 * Operation result information
 */
struct FOperationResult
{
    // Status information
    bool bWasSuccessful;
    FString ErrorMessage;
    
    // Timing information
    float ExecutionTimeMS;
    float SetupTimeMS;
    float SynchronizationTimeMS;
    float TotalTimeMS;
    
    // Resource usage
    uint32 MemoryUsage;
    int32 ThreadsUsed;
    
    // Target information
    EWorkloadTarget ExecutionTarget;
    
    // Default constructor
    FOperationResult()
        : bWasSuccessful(false)
        , ExecutionTimeMS(0.0f)
        , SetupTimeMS(0.0f)
        , SynchronizationTimeMS(0.0f)
        , TotalTimeMS(0.0f)
        , MemoryUsage(0)
        , ThreadsUsed(0)
        , ExecutionTarget(EWorkloadTarget::Auto)
    {
    }
}; 