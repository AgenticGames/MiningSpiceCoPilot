#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "HAL/Platform.h" 
#include "ComputeOperationTypes.generated.h"

/** GPU vendor identifiers */
UENUM(BlueprintType)
enum class EGPUVendor : uint8
{
    Unknown = 0,
    NVIDIA = 1,
    AMD = 2,
    Intel = 3,
    Apple = 4,
    Microsoft = 5,
    Qualcomm = 6,
    ARM = 7
};

/** Target processor for computation */
UENUM(BlueprintType)
enum class EProcessingTarget : uint8
{
    CPU,        // Execute on CPU
    GPU,        // Execute on GPU
    Hybrid      // Split processing between CPU and GPU
};

/** Compute operation status */
UENUM(BlueprintType)
enum class EOperationStatus : uint8
{
    Pending,    // Operation is pending execution
    Running,    // Operation is currently executing
    Completed,  // Operation has completed successfully
    Failed,     // Operation has failed
    Cancelled   // Operation was cancelled
};

/** Compute operation error type */
UENUM(BlueprintType)
enum class EComputeErrorType : uint8
{
    None,                   // No error
    ResourceAllocationFailed, // Failed to allocate required resources
    ShaderCompilationFailed,  // Failed to compile shader
    InvalidParameters,      // Invalid operation parameters
    DeviceLost,             // GPU device was lost
    Timeout,                // Operation timed out
    UnsupportedOperation,   // Operation not supported on current hardware
    MemoryExhausted,        // Out of memory
    InternalError           // Unknown internal error
};

/** Priority levels for async compute operations */
UENUM(BlueprintType)
enum class EAsyncPriority : uint8
{
    Critical,   // Must execute as soon as possible
    High,       // High priority, execute before normal operations
    Normal,     // Normal priority
    Low,        // Low priority, can be deferred
    Background  // Very low priority, execute when system is idle
};

/**
 * Pipeline types (simplified without RHI)
 */
UENUM(BlueprintType)
enum class ESimplifiedPipeline : uint8
{
    Graphics,
    Compute,
    AsyncCompute,
    Copy
};

/**
 * Resource access types (simplified without RHI)
 */
UENUM(BlueprintType)
enum class ESimplifiedAccess : uint8
{
    None = 0,
    SRVRead = 1,      // Shader Resource View (read)
    UAVReadWrite = 2, // Unordered Access View (read/write)
    RTV = 3,          // Render Target View
    DSV = 4,          // Depth Stencil View
    CopyDest = 5,     // Copy Destination 
    CopySrc = 6,      // Copy Source
    ResolveDst = 7,   // Resolve Destination
    ResolveSrc = 8,   // Resolve Source
    General = 9       // General Access
};

/** Resource state for tracking */
struct FResourceState
{
    ESimplifiedAccess CurrentAccess = ESimplifiedAccess::SRVRead;
    ESimplifiedPipeline CurrentPipeline = ESimplifiedPipeline::Graphics;
    int32 LastFrameAccessed = 0;
};

/** Operation parameters for similarity comparison */
struct FOperationParameters
{
    float VolumeSize = 0.0f;
    int32 MaterialId = -1;
    int32 ChannelCount = 0;
    bool bUseNarrowBand = false;
    bool bHighPrecision = false;
};

/** Hardware profile for compute capabilities */
USTRUCT(BlueprintType)
struct FHardwareProfile
{
    GENERATED_BODY()

    /** Whether ray tracing is supported */
    UPROPERTY(BlueprintReadOnly, Category="Hardware")
    bool bSupportsRayTracing = false;

    /** Whether async compute is supported */
    UPROPERTY(BlueprintReadOnly, Category="Hardware")
    bool bSupportsAsyncCompute = false;

    /** Number of compute units available */
    UPROPERTY(BlueprintReadOnly, Category="Hardware")
    int32 ComputeUnits = 0;

    /** Maximum workgroup size */
    UPROPERTY(BlueprintReadOnly, Category="Hardware")
    int32 MaxWorkgroupSize = 1024;

    /** Wavefront size for SIMD operations */
    UPROPERTY(BlueprintReadOnly, Category="Hardware")
    int32 WavefrontSize = 32;

    /** Whether wave intrinsics are supported */
    UPROPERTY(BlueprintReadOnly, Category="Hardware")
    bool bSupportsWaveIntrinsics = false;

    /** Shared memory size in bytes */
    UPROPERTY(BlueprintReadOnly, Category="Hardware")
    int32 SharedMemoryBytes = 32768;

    /** L1 cache size in KB */
    UPROPERTY(BlueprintReadOnly, Category="Hardware")
    int32 L1CacheSizeKB = 64;

    /** L2 cache size in KB */
    UPROPERTY(BlueprintReadOnly, Category="Hardware")
    int32 L2CacheSizeKB = 1024;

    /** Compute to pipeline ratio */
    UPROPERTY(BlueprintReadOnly, Category="Hardware")
    float ComputeToPipelineRatio = 1.0f;

    /** Device performance tier (0-3, where 3 is highest) */
    UPROPERTY(BlueprintReadOnly, Category="Hardware")
    int32 PerformanceTier = 1;

    /** GPU vendor ID */
    UPROPERTY(BlueprintReadOnly, Category="Hardware")
    EGPUVendor VendorId = EGPUVendor::Unknown;

    /** GPU device name */
    UPROPERTY(BlueprintReadOnly, Category="Hardware")
    FString DeviceName;
};

/** Compute capabilities for the system */
USTRUCT(BlueprintType)
struct FComputeCapabilities
{
    GENERATED_BODY()

    /** Hardware profile for the current system */
    UPROPERTY(BlueprintReadOnly, Category="Compute")
    FHardwareProfile HardwareProfile;

    /** Whether compute shaders are available */
    UPROPERTY(BlueprintReadOnly, Category="Compute")
    bool bSupportsComputeShaders = false;

    /** Maximum dispatch size X */
    UPROPERTY(BlueprintReadOnly, Category="Compute")
    int32 MaxDispatchSizeX = 65535;

    /** Maximum dispatch size Y */
    UPROPERTY(BlueprintReadOnly, Category="Compute")
    int32 MaxDispatchSizeY = 65535;

    /** Maximum dispatch size Z */
    UPROPERTY(BlueprintReadOnly, Category="Compute")
    int32 MaxDispatchSizeZ = 65535;

    /** Maximum workgroup shared memory size in bytes */
    UPROPERTY(BlueprintReadOnly, Category="Compute")
    int32 MaxSharedMemorySize = 32768;

    /** List of supported compute shader formats */
    UPROPERTY(BlueprintReadOnly, Category="Compute")
    TArray<FString> SupportedShaderFormats;
};

/** Operation metrics for performance tracking */
USTRUCT(BlueprintType)
struct FOperationMetrics
{
    GENERATED_BODY()

    /** Operation type identifier */
    UPROPERTY(BlueprintReadOnly, Category="Metrics")
    int32 OperationTypeId = 0;

    /** Time spent executing on CPU in milliseconds */
    UPROPERTY(BlueprintReadOnly, Category="Metrics")
    float CPUExecutionTimeMS = 0.0f;

    /** Time spent executing on GPU in milliseconds */
    UPROPERTY(BlueprintReadOnly, Category="Metrics")
    float GPUExecutionTimeMS = 0.0f;

    /** Size of data processed in bytes */
    UPROPERTY(BlueprintReadOnly, Category="Metrics")
    int32 DataSize = 0;

    /** Number of iterations or elements processed */
    UPROPERTY(BlueprintReadOnly, Category="Metrics")
    int32 IterationCount = 0;

    /** Device utilization during operation (0.0-1.0) */
    UPROPERTY(BlueprintReadOnly, Category="Metrics")
    float DeviceUtilization = 0.0f;

    /** Whether the operation was successful */
    UPROPERTY(BlueprintReadOnly, Category="Metrics")
    bool SuccessfulExecution = true;
};

/** Operation status information */
USTRUCT(BlueprintType)
struct FOperationStatus
{
    GENERATED_BODY()

    /** Operation identifier */
    UPROPERTY(BlueprintReadOnly, Category="Operation")
    int64 OperationId = 0;

    /** Current status */
    UPROPERTY(BlueprintReadOnly, Category="Operation")
    EOperationStatus Status = EOperationStatus::Pending;

    /** Progress value (0.0-1.0) */
    UPROPERTY(BlueprintReadOnly, Category="Operation")
    float Progress = 0.0f;

    /** Execution time in milliseconds (if completed) */
    UPROPERTY(BlueprintReadOnly, Category="Operation")
    float ExecutionTimeMs = 0.0f;

    /** Error type if failed */
    UPROPERTY(BlueprintReadOnly, Category="Operation")
    EComputeErrorType ErrorType = EComputeErrorType::None;

    /** Error message if failed */
    UPROPERTY(BlueprintReadOnly, Category="Operation")
    FString ErrorMessage;
};

/** Internal state tracking for operations */
struct FOperationState
{
    int64 OperationId = 0;
    EOperationStatus Status = EOperationStatus::Pending;
    float Progress = 0.0f;
    double StartTime = 0.0;
    double EndTime = 0.0;
    float ExecutionTimeMs = 0.0f;
    EComputeErrorType ErrorType = EComputeErrorType::None;
    FString ErrorMessage;
    int32 OperationTypeId = 0;
    int32 DataSize = 0;
    TFunction<void(bool, float)> CompletionCallback;
};

/**
 * Simplified resource handle for tracking without RHI dependencies
 */
class MININGSPICECOPILOT_API FSimplifiedResource
{
public:
    FSimplifiedResource() : Id(NextResourceId++) {}
    virtual ~FSimplifiedResource() {}
    
    uint64 GetId() const { return Id; }
    
    bool operator==(const FSimplifiedResource& Other) const { return Id == Other.Id; }
    
    /** Get the resource type name */
    virtual FString GetTypeName() const { return TEXT("GenericResource"); }
    
    /** Get the resource size in bytes */
    virtual uint64 GetSizeBytes() const { return 0; }
    
private:
    uint64 Id;
    static uint64 NextResourceId;
};

/** Dispatch parameters for compute shaders */
struct FDispatchParameters
{
    int32 ThreadGroupSizeX = 8;
    int32 ThreadGroupSizeY = 8;
    int32 ThreadGroupSizeZ = 1;
    int32 SizeX = 0;
    int32 SizeY = 0;
    int32 SizeZ = 0;
    TMap<FSimplifiedResource*, FResourceState> Resources;
};

/** Compute operation batch for grouping similar operations */
struct FOperationBatch
{
    int32 OperationTypeId = 0;
    TArray<FBox> Regions;
    TArray<FMatrix> Transforms;
    TArray<float> Parameters;
    int32 EstimatedCost = 0;
    bool bUseWideExecutionStrategy = false;
};

/** Pending async operation */
struct FPendingAsyncOperation
{
    int64 OperationId = 0;
    EAsyncPriority Priority = EAsyncPriority::Normal;
    TFunction<void(bool)> CompletionCallback;
    double QueueTime = 0.0;
};

/** Compute shader variant information */
struct FShaderVariant
{
    FString PermutationName;
    int32 OptimizationLevel = 0;
    int32 FeatureBitmask = 0;
    bool bEnableFastMath = false;
    bool bEnableSpecialIntrinsics = false;
    TArray<uint8> Flags;
    bool bDebugInfo = false;
};

/** Distribution configuration for workload distributor */
struct FDistributionConfig
{
    bool bEnableAutotuning = true;
    float CPUAffinityForLowOperationCount = 0.8f;
    float GPUAffinityForBatchedOperations = 0.9f;
    float ComplexityThreshold = 100.0f;
    float GpuUtilizationThreshold = 0.9f;
    float PerformanceRatioThreshold = 0.8f;
    
    // Whether the device supports async compute
    bool bDeviceSupportsAsyncCompute = false;
    
    // Device performance tier (0-3, where 3 is highest)
    int32 DevicePerformanceTier = 1;
};

/** Compute operation parameters */
USTRUCT(BlueprintType)
struct FComputeOperation
{
    GENERATED_BODY()

    /** Unique operation identifier */
    UPROPERTY(BlueprintReadOnly, Category="Operation")
    int64 OperationId = 0;

    /** Operation type identifier */
    UPROPERTY(BlueprintReadWrite, Category="Operation")
    int32 OperationTypeId = 0;

    /** Operation type from SDF system */
    UPROPERTY(BlueprintReadWrite, Category="Operation")
    int32 OperationType = 0;

    /** Material channel identifier */
    UPROPERTY(BlueprintReadWrite, Category="Operation") 
    int32 MaterialChannelId = 0;

    /** Bounds of the operation */
    UPROPERTY(BlueprintReadWrite, Category="Operation")
    FBox Bounds;

    /** Strength or intensity of the operation */
    UPROPERTY(BlueprintReadWrite, Category="Operation")
    float Strength = 1.0f;

    /** Blend weight for the operation */
    UPROPERTY(BlueprintReadWrite, Category="Operation")
    float BlendWeight = 1.0f;

    /** Whether to use narrow-band optimization */
    UPROPERTY(BlueprintReadWrite, Category="Operation")
    bool bUseNarrowBand = false;

    /** Whether high precision is required */
    UPROPERTY(BlueprintReadWrite, Category="Operation")
    bool bRequiresHighPrecision = false;

    /** Whether the operation is compatible with SIMD instructions */
    UPROPERTY(BlueprintReadWrite, Category="Operation")
    bool bSIMDCompatible = false;

    /** Preferred processing target (suggestion only) */
    UPROPERTY(BlueprintReadWrite, Category="Operation")
    EProcessingTarget PreferredTarget = EProcessingTarget::GPU;

    /** Forced processing target (overrides distributor decision) */
    UPROPERTY(BlueprintReadWrite, Category="Operation")
    TOptional<EProcessingTarget> ForcedTarget;

    /** Priority for the operation */
    UPROPERTY(BlueprintReadWrite, Category="Operation")
    EAsyncPriority Priority = EAsyncPriority::Normal;

    /** Whether the operation can be batched with others */
    UPROPERTY(BlueprintReadWrite, Category="Operation")
    bool bCanBeBatched = true;

    /** Preferred batch size for batched operations */
    UPROPERTY(BlueprintReadWrite, Category="Operation")
    int32 PreferredBatchSize = 32;

    /** Importance scale for prioritization */
    UPROPERTY(BlueprintReadWrite, Category="Operation")
    float ImportanceScale = 1.0f;

    /** Input data pointers (not exposed to Blueprint) */
    TArray<void*> InputData;

    /** Output data pointer (not exposed to Blueprint) */
    void* OutputData = nullptr;

    /** Custom data for operation-specific parameters */
    TMap<FName, TSharedPtr<void>> CustomData;
};