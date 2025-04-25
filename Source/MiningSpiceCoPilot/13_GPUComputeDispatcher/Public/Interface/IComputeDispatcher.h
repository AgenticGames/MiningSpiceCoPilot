#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "RHIDefinitions.h"
#include "RHIResources.h"
#include "HAL/ThreadSafeBool.h"
#include "Templates/SharedPointer.h"
#include "../../1_CoreRegistry/Public/Interfaces/IRegistry.h"
#include "../../3_ThreadingTaskSystem/Public/TaskSystem/TaskTypes.h"
#include "../../1_CoreRegistry/Public/SDFTypeRegistry.h"
#include "OperationTypes.h"
#include "Misc/EnumClassFlags.h"

// Forward declarations
class FRDGBuilder;
class IWorkloadDistributor;

/**
 * Enum for compute operation priority levels
 */
enum class EComputePriority : uint8
{
    Critical,   // Must execute immediately
    High,       // High priority, execute soon
    Normal,     // Standard priority
    Low,        // Background operations
    Deferred    // Execute when resources are available
};

/**
 * Flags for compute dispatch behavior
 */
enum class EComputeDispatchFlags : uint32
{
    None = 0,
    EnableBatching = 1 << 0,            // Allow operation batching
    ForceGPU = 1 << 1,                  // Force GPU execution
    ForceCPU = 1 << 2,                  // Force CPU execution
    AllowAsyncCompute = 1 << 3,         // Allow async compute queue
    WaitForCompletion = 1 << 4,         // Wait for operation to complete
    EnableProfiling = 1 << 5,           // Enable detailed profiling
    SkipValidation = 1 << 6,            // Skip input validation for performance
    PreferNarrowBand = 1 << 7,          // Prefer narrow-band update strategy
    RequiresSynchronization = 1 << 8,    // Operation requires sync points
    AllowHybridExecution = 1 << 9       // Allow split between CPU/GPU
};
ENUM_CLASS_FLAGS(EComputeDispatchFlags);

/**
 * Compute operation status
 */
enum class EComputeOperationStatus : uint8
{
    Pending,        // Operation is queued
    InProgress,     // Operation is executing
    Completed,      // Operation completed successfully
    Failed,         // Operation failed
    Canceled        // Operation was canceled
};

/**
 * Dispatch statistics for performance tracking
 */
struct FDispatchStatistics
{
    // Timing information
    float TotalTimeMS;          // Total time in milliseconds
    float SetupTimeMS;          // Setup time (parameter binding, etc.)
    float ExecutionTimeMS;      // Pure execution time
    float SynchronizationTimeMS; // Time spent in synchronization

    // Resource usage
    int32 ThreadGroupsX;
    int32 ThreadGroupsY;
    int32 ThreadGroupsZ;
    uint32 TotalThreads;
    uint32 MemoryUsageBytes;

    // Batch information
    int32 OperationsInBatch;
    int32 BatchIndex;

    // Performance metrics
    float OperationsPerSecond;
    float EfficiencyScore;      // 0-1 efficiency rating
    
    // Default constructor
    FDispatchStatistics()
        : TotalTimeMS(0.0f)
        , SetupTimeMS(0.0f)
        , ExecutionTimeMS(0.0f)
        , SynchronizationTimeMS(0.0f)
        , ThreadGroupsX(0)
        , ThreadGroupsY(0)
        , ThreadGroupsZ(0)
        , TotalThreads(0)
        , MemoryUsageBytes(0)
        , OperationsInBatch(0)
        , BatchIndex(0)
        , OperationsPerSecond(0.0f)
        , EfficiencyScore(0.0f)
    {
    }
};

/**
 * Configuration for a compute dispatch operation
 */
struct FComputeDispatchConfig
{
    // Operation configuration
    EComputePriority Priority;          // Operation priority
    EComputeDispatchFlags Flags;        // Dispatch flags
    
    // Timing constraints
    float MaxExecutionTimeMS;           // Maximum allowed execution time
    
    // Batching parameters
    int32 BatchID;                      // Batch identifier (0 = auto-assign)
    int32 MaxBatchSize;                 // Maximum operations in batch
    
    // Dispatch dimensions
    FIntVector ThreadGroupCounts;       // Thread group counts
    FIntVector ThreadGroupSize;         // Thread group size
    
    // Resource allocation
    uint32 EstimatedMemoryUsage;        // Estimated memory usage in bytes
    
    // Callback information
    TFunction<void(EComputeOperationStatus, const FDispatchStatistics&)> CompletionCallback;
    
    // Default constructor with reasonable defaults
    FComputeDispatchConfig()
        : Priority(EComputePriority::Normal)
        , Flags(EComputeDispatchFlags::EnableBatching)
        , MaxExecutionTimeMS(0.0f)      // 0 = no time limit
        , BatchID(0)
        , MaxBatchSize(32)
        , ThreadGroupCounts(1, 1, 1)
        , ThreadGroupSize(64, 1, 1)
        , EstimatedMemoryUsage(0)
        , CompletionCallback(nullptr)
    {
    }
};

/**
 * Operation handle for tracking dispatch operations
 */
struct FComputeOperationHandle
{
    uint64 OperationId;
    EComputeOperationStatus Status;
    
    bool IsValid() const { return OperationId != 0; }
    
    FComputeOperationHandle()
        : OperationId(0)
        , Status(EComputeOperationStatus::Pending)
    {
    }
    
    explicit FComputeOperationHandle(uint64 InOperationId)
        : OperationId(InOperationId)
        , Status(EComputeOperationStatus::Pending)
    {
    }
};

/**
 * Interface for compute dispatcher implementations
 * Handles GPU compute shader dispatching and management for SDF operations
 * in the mining system. Provides capabilities for dispatching operations,
 * batching similar operations, tracking performance, and managing resources.
 */
class MININGSPICECOPILOT_API IComputeDispatcher
{
public:
    /** Virtual destructor */
    virtual ~IComputeDispatcher() {}
    
    /**
     * Initialize the dispatcher
     * Sets up internal resources and connects to required systems
     * @return True if initialization was successful
     */
    virtual bool Initialize() = 0;
    
    /**
     * Shutdown the dispatcher
     * Releases all resources and performs any necessary cleanup
     */
    virtual void Shutdown() = 0;
    
    /**
     * Check if dispatcher is initialized
     * @return True if the dispatcher is properly initialized
     */
    virtual bool IsInitialized() const = 0;
    
    /**
     * Dispatch an SDF operation using the specified operation info
     * Submits an SDF operation for execution on the appropriate hardware
     * 
     * @param OperationInfo The SDF operation to dispatch
     * @param FieldData Array of field data pointers for the operation
     * @param Config Dispatch configuration
     * @return Handle to the dispatched operation
     */
    virtual FComputeOperationHandle DispatchSDFOperation(
        const FSDFOperationInfo& OperationInfo,
        const TArray<void*>& FieldData,
        const FComputeDispatchConfig& Config = FComputeDispatchConfig()) = 0;
    
    /**
     * Dispatch an SDF operation using the operation ID
     * Looks up the operation in the registry and dispatches it
     * 
     * @param OperationId ID of the registered SDF operation
     * @param FieldData Array of field data pointers for the operation
     * @param Config Dispatch configuration
     * @return Handle to the dispatched operation
     */
    virtual FComputeOperationHandle DispatchSDFOperationById(
        uint32 OperationId,
        const TArray<void*>& FieldData,
        const FComputeDispatchConfig& Config = FComputeDispatchConfig()) = 0;
    
    /**
     * Dispatch a custom compute shader
     * Allows execution of custom compute shaders not in the registry
     * 
     * @param ShaderName Name of the shader to dispatch
     * @param Parameters Parameters for the shader
     * @param Config Dispatch configuration
     * @return Handle to the dispatched operation
     */
    virtual FComputeOperationHandle DispatchCustomCompute(
        const FName& ShaderName,
        const TMap<FName, TSharedPtr<void>>& Parameters,
        const FComputeDispatchConfig& Config = FComputeDispatchConfig()) = 0;
    
    /**
     * Batch multiple SDF operations for efficient execution
     * Groups operations together to reduce dispatch overhead
     * 
     * @param OperationInfos Array of SDF operations to batch
     * @param FieldDatas Array of field data arrays for each operation
     * @param Config Dispatch configuration
     * @return Handle to the batched operation
     */
    virtual FComputeOperationHandle BatchSDFOperations(
        const TArray<FSDFOperationInfo>& OperationInfos,
        const TArray<TArray<void*>>& FieldDatas,
        const FComputeDispatchConfig& Config = FComputeDispatchConfig()) = 0;
    
    /**
     * Check the status of an operation
     * @param Handle Handle to the operation
     * @return Current status of the operation
     */
    virtual EComputeOperationStatus GetOperationStatus(const FComputeOperationHandle& Handle) const = 0;
    
    /**
     * Wait for an operation to complete
     * Blocks until the operation is done or timeout is reached
     * 
     * @param Handle Handle to the operation
     * @param TimeoutMS Timeout in milliseconds (0 = wait indefinitely)
     * @return True if operation completed, false if timed out
     */
    virtual bool WaitForCompletion(const FComputeOperationHandle& Handle, uint32 TimeoutMS = 0) = 0;
    
    /**
     * Cancel a pending or in-progress operation
     * Stops an operation that hasn't completed yet
     * 
     * @param Handle Handle to the operation
     * @param bWaitForCancellation Whether to wait for cancellation to complete
     * @return True if operation was canceled
     */
    virtual bool CancelOperation(const FComputeOperationHandle& Handle, bool bWaitForCancellation = false) = 0;
    
    /**
     * Get statistics for a completed operation
     * Retrieves detailed performance statistics for an operation
     * 
     * @param Handle Handle to the operation
     * @param OutStats Output statistics structure
     * @return True if statistics were retrieved
     */
    virtual bool GetOperationStatistics(const FComputeOperationHandle& Handle, FDispatchStatistics& OutStats) const = 0;
    
    /**
     * Get overall dispatcher statistics
     * Retrieves global statistics for the dispatcher
     * 
     * @param bResetStats Whether to reset statistics after retrieval
     * @return Map of statistic names to values
     */
    virtual TMap<FString, float> GetDispatcherStatistics(bool bResetStats = false) = 0;
    
    /**
     * Check if a specific operation type is supported by the dispatcher
     * @param OperationType The operation type to check
     * @return True if the operation type is supported
     */
    virtual bool IsOperationTypeSupported(ESDFOperationType OperationType) const = 0;
    
    /**
     * Get the SDF type registry used by this dispatcher
     * @return Pointer to the SDF type registry
     */
    virtual const FSDFTypeRegistry* GetTypeRegistry() const = 0;
    
    /**
     * Set the SDF type registry for this dispatcher
     * @param Registry Pointer to the SDF type registry
     */
    virtual void SetTypeRegistry(const FSDFTypeRegistry* Registry) = 0;
    
    /**
     * Check if the dispatcher has pending operations
     * @return True if there are pending operations
     */
    virtual bool HasPendingOperations() const = 0;
    
    /**
     * Process pending operations (call each frame)
     * Processes a batch of queued operations
     * 
     * @param RDGBuilder Optional render dependency graph builder for GPU operations
     * @param MaxTimeMS Maximum time to spend processing (0 = no limit)
     * @return Number of operations processed
     */
    virtual int32 ProcessPendingOperations(FRDGBuilder* RDGBuilder = nullptr, float MaxTimeMS = 0.0f) = 0;
    
    /**
     * Get the name of this dispatcher implementation
     * @return Name of the dispatcher
     */
    virtual FName GetDispatcherName() const = 0;
    
    /**
     * Flush all pending operations
     * Processes all queued operations immediately
     * 
     * @param bWaitForCompletion Whether to wait for all operations to complete
     */
    virtual void FlushOperations(bool bWaitForCompletion = true) = 0;
    
    /**
     * Check if hardware supports compute operations
     * @return True if compute operations are supported
     */
    virtual bool SupportsCompute() const = 0;
    
    /**
     * Get the workload distributor used by this dispatcher
     * Returns the workload distributor responsible for CPU/GPU work assignment
     * 
     * @return Interface to the workload distributor
     */
    virtual IWorkloadDistributor* GetWorkloadDistributor() const = 0;
    
    /**
     * Register custom kernels for specialized operations
     * Allows registration of custom compute kernels for specific operation types
     * 
     * @param OperationId The operation ID to register the kernel for
     * @param ShaderName The name of the shader to use
     * @param ShaderEntryPoint The entry point in the shader
     * @return True if registration was successful
     */
    virtual bool RegisterCustomKernel(uint32 OperationId, const FName& ShaderName, const FName& ShaderEntryPoint) = 0;
    
    /**
     * Enable or disable telemetry collection
     * Controls whether detailed performance data is collected
     * 
     * @param bEnable Whether to enable telemetry
     * @param DetailLevel Level of detail for telemetry (0-3)
     */
    virtual void SetTelemetryEnabled(bool bEnable, int32 DetailLevel = 1) = 0;
    
    /**
     * Export telemetry data to a file
     * @param FilePath Path to export the data to
     * @return True if export was successful
     */
    virtual bool ExportTelemetryData(const FString& FilePath) const = 0;
    
    /**
     * Precompile shaders for registered operations
     * Precompiles shaders to avoid runtime hitches
     * 
     * @param OperationIds Array of operation IDs to precompile (empty = all)
     * @return Number of shaders precompiled
     */
    virtual int32 PrecompileShaders(const TArray<uint32>& OperationIds = TArray<uint32>()) = 0;
};
