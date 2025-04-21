// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TaskTypes.generated.h"

// Forward declare the ETaskType enum used from ITaskScheduler.h
enum class ETaskType : uint8;

/**
 * Task status enumeration
 */
UENUM(BlueprintType)
enum class ETaskStatus : uint8
{
    /** Task is waiting in the queue */
    Queued UMETA(DisplayName = "Queued"),
    
    /** Task is currently executing */
    Executing UMETA(DisplayName = "Executing"),
    
    /** Task completed successfully */
    Completed UMETA(DisplayName = "Completed"),
    
    /** Task was cancelled before completion */
    Cancelled UMETA(DisplayName = "Cancelled"),
    
    /** Task failed during execution */
    Failed UMETA(DisplayName = "Failed"),
    
    /** Task is waiting for dependencies */
    Waiting UMETA(DisplayName = "Waiting for Dependencies"),
    
    /** Task is suspended and will resume later */
    Suspended UMETA(DisplayName = "Suspended"),
    
    /** Sentinel value for enumeration count */
    MaxValue UMETA(DisplayName = "Max Value", Hidden)
};

/**
 * Task priority enumeration
 */
UENUM(BlueprintType)
enum class ETaskPriority : uint8
{
    /** Critical priority - processed before all others */
    Critical UMETA(DisplayName = "Critical"),
    
    /** High priority tasks */
    High UMETA(DisplayName = "High"),
    
    /** Normal priority tasks (default) */
    Normal UMETA(DisplayName = "Normal"),
    
    /** Low priority background tasks */
    Low UMETA(DisplayName = "Low"),
    
    /** Lowest priority - only run when system is idle */
    Background UMETA(DisplayName = "Background")
};

/**
 * Task type enumeration categorizing different processing types
 */
UENUM(BlueprintType)
enum class ETaskType : uint8
{
    /** General purpose task */
    General UMETA(DisplayName = "General"),
    
    /** Mining operation task with specific optimization */
    MiningOperation UMETA(DisplayName = "Mining Operation"),
    
    /** SDF field operation with SIMD optimization */
    SDFOperation UMETA(DisplayName = "SDF Operation"),
    
    /** Octree traversal operation with spatial coherence */
    OctreeTraversal UMETA(DisplayName = "Octree Traversal"),
    
    /** Material processing operation with channel awareness */
    MaterialOperation UMETA(DisplayName = "Material Operation"),
    
    /** Zone-based transaction task with concurrency control */
    ZoneTransaction UMETA(DisplayName = "Zone Transaction"),
    
    /** CPU-intensive computation task */
    Computation UMETA(DisplayName = "Computation"),
    
    /** I/O bound task */
    IO UMETA(DisplayName = "I/O"),
    
    /** Network task */
    Network UMETA(DisplayName = "Network"),
    
    /** Graphics or rendering task */
    Rendering UMETA(DisplayName = "Rendering"),
    
    /** Physics simulation task */
    Physics UMETA(DisplayName = "Physics"),
    
    /** Mining operation task */
    Mining UMETA(DisplayName = "Mining"),
    
    /** Data compression task */
    Compression UMETA(DisplayName = "Compression"),
    
    /** Memory management task */
    Memory UMETA(DisplayName = "Memory"),
    
    /** Maintenance or utility task */
    Maintenance UMETA(DisplayName = "Maintenance")
};

/**
 * Thread optimization flags for task scheduling
 * These flags guide how tasks should be scheduled and executed
 */
UENUM()
enum class EThreadOptimizationFlags : uint32
{
    /** No special optimizations */
    None = 0,
    
    /** Task benefits from CPU cache locality */
    CacheLocality = 1 << 0,
    
    /** Task benefits from NUMA-aware scheduling */
    NumaAware = 1 << 1,
    
    /** Task benefits from CPU affinity */
    CoreAffinity = 1 << 2,
    
    /** Task benefits from SIMD-aware thread selection */
    SIMDAware = 1 << 3,
    
    /** Task benefits from being run on a specialized worker */
    SpecializedWorker = 1 << 4,
    
    /** Task benefits from running on same thread as related tasks */
    ThreadAffinity = 1 << 5,
    
    /** Task should avoid rescheduling to minimize latency */
    LowLatency = 1 << 6,
    
    /** Task is GPU-bound and should be scheduled accordingly */
    GPUBound = 1 << 7,
    
    /** Task is I/O-bound and should be scheduled accordingly */
    IOBound = 1 << 8,
    
    /** Task performs network operations */
    NetworkBound = 1 << 9,
    
    /** Task performs memory-intensive operations */
    MemoryIntensive = 1 << 10,
    
    /** Task performs compute-intensive operations */
    ComputeIntensive = 1 << 11,
    
    /** Task should run on same thread that created it if possible */
    PreferCreatorThread = 1 << 12,
    
    /** Task benefits from power efficiency optimizations */
    PowerEfficient = 1 << 13,
    
    /** Task can run at a lower priority when system is busy */
    BackgroundPriority = 1 << 14,
    
    /** Task should use the thread scheduler's default behavior */
    DefaultScheduling = 1 << 15
};
ENUM_CLASS_FLAGS(EThreadOptimizationFlags);

/**
 * Registry type enumeration for type-safe task scheduling
 * Identifies which registry a type ID belongs to
 */
UENUM(BlueprintType)
enum class ERegistryType : uint8
{
    /** No specific registry */
    None = 0 UMETA(DisplayName = "None"),
    
    /** SDF (Signed Distance Field) registry */
    SDF = 1 UMETA(DisplayName = "SDF Registry"),
    
    /** SVO (Sparse Voxel Octree) registry */
    SVO = 2 UMETA(DisplayName = "SVO Registry"),
    
    /** Zone registry */
    Zone = 3 UMETA(DisplayName = "Zone Registry"),
    
    /** Material registry */
    Material = 4 UMETA(DisplayName = "Material Registry"),
    
    /** Service registry */
    Service = 5 UMETA(DisplayName = "Service Registry")
};

/**
 * Type capabilities enumeration
 * Defines special capabilities that a type may support
 */
UENUM()
enum class ETypeCapabilities : uint32
{
    /** No special capabilities */
    None = 0,
    
    /** Type supports SIMD operations */
    SIMDOperations = 1 << 0,
    
    /** Type is thread-safe */
    ThreadSafe = 1 << 1,
    
    /** Type supports batch operations */
    BatchOperations = 1 << 2,
    
    /** Type supports parallel processing */
    ParallelProcessing = 1 << 3,
    
    /** Type supports incremental updates */
    IncrementalUpdates = 1 << 4,
    
    /** Type supports async operations */
    AsyncOperations = 1 << 5,
    
    /** Type supports partial execution */
    PartialExecution = 1 << 6,
    
    /** Type supports result merging */
    ResultMerging = 1 << 7,
    
    /** Type has spatial coherence */
    SpatialCoherence = 1 << 8,
    
    /** Type supports cache optimization */
    CacheOptimized = 1 << 9,
    
    /** Type is memory efficient */
    MemoryEfficient = 1 << 10,
    
    /** Type has low contention properties */
    LowContention = 1 << 11,
    
    /** Type supports vectorization */
    Vectorizable = 1 << 12
};
ENUM_CLASS_FLAGS(ETypeCapabilities);

/**
 * SIMD instruction variant for optimized task execution
 * Identifies which SIMD instruction set to use for processing
 */
UENUM(BlueprintType)
enum class ESIMDVariant : uint8
{
    /** No SIMD instructions (scalar fallback) */
    None UMETA(DisplayName = "None (Scalar)"),
    
    /** SSE2 instruction set */
    SSE2 UMETA(DisplayName = "SSE2"),
    
    /** SSE4 instruction set */
    SSE4 UMETA(DisplayName = "SSE4"),
    
    /** AVX instruction set */
    AVX UMETA(DisplayName = "AVX"),
    
    /** AVX2 instruction set */
    AVX2 UMETA(DisplayName = "AVX2"),
    
    /** AVX-512 instruction set */
    AVX512 UMETA(DisplayName = "AVX-512"),
    
    /** ARM NEON instruction set */
    Neon UMETA(DisplayName = "ARM Neon")
};

/**
 * Processor features for optimized task execution
 * Represents the available CPU features for specialized processing
 */
UENUM()
enum class EProcessorFeatures : uint32
{
    /** Basic x86 instructions only */
    None = 0,
    
    /** SSE (Streaming SIMD Extensions) */
    SSE = 1 << 0,
    
    /** SSE2 (Streaming SIMD Extensions 2) */
    SSE2 = 1 << 1,
    
    /** SSE3 (Streaming SIMD Extensions 3) */
    SSE3 = 1 << 2,
    
    /** SSSE3 (Supplemental Streaming SIMD Extensions 3) */
    SSSE3 = 1 << 3,
    
    /** SSE4.1 (Streaming SIMD Extensions 4.1) */
    SSE41 = 1 << 4,
    
    /** SSE4.2 (Streaming SIMD Extensions 4.2) */
    SSE42 = 1 << 5,
    
    /** AVX (Advanced Vector Extensions) */
    AVX = 1 << 6,
    
    /** AVX2 (Advanced Vector Extensions 2) */
    AVX2 = 1 << 7,
    
    /** AVX-512 Foundation */
    AVX512F = 1 << 8,
    
    /** AVX-512 Conflict Detection Instructions */
    AVX512CD = 1 << 9,
    
    /** AVX-512 Byte and Word Instructions */
    AVX512BW = 1 << 10,
    
    /** AVX-512 Doubleword and Quadword Instructions */
    AVX512DQ = 1 << 11,
    
    /** AVX-512 Vector Length Extensions */
    AVX512VL = 1 << 12,
    
    /** ARM NEON SIMD instructions */
    NEON = 1 << 13,
    
    /** Advanced Encryption Standard instructions */
    AES = 1 << 14,
    
    /** Fused Multiply-Add instructions */
    FMA = 1 << 15,
    
    /** Half-precision floating-point support */
    F16C = 1 << 16,
    
    /** POPCNT instruction (population count) */
    POPCNT = 1 << 17,
    
    /** BMI1 (Bit Manipulation Instruction Set 1) */
    BMI1 = 1 << 18,
    
    /** BMI2 (Bit Manipulation Instruction Set 2) */
    BMI2 = 1 << 19,
    
    /** LZCNT instruction (leading zero count) */
    LZCNT = 1 << 20,
    
    /** Cacheability control, including CLFLUSH */
    CLFSH = 1 << 21,
    
    /** Cache line write-back without RFO */
    CLWB = 1 << 22,
    
    /** Multi-threading capability */
    HTT = 1 << 23,
    
    /** Hardware lock elision */
    HLE = 1 << 24,
    
    /** Restricted transactional memory */
    RTM = 1 << 25
};
ENUM_CLASS_FLAGS(EProcessorFeatures);