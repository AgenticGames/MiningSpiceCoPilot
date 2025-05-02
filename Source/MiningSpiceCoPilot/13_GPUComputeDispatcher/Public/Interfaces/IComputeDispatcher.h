#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "RenderGraphResources.h"
#include "../ComputeOperationTypes.h"
#include "IComputeDispatcher.generated.h"

UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UComputeDispatcher : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for GPU compute dispatch management
 * Provides methods for dispatching compute operations to GPU or CPU
 * with adaptive workload balancing and performance monitoring
 */
class MININGSPICECOPILOT_API IComputeDispatcher
{
    GENERATED_BODY()

public:
    /**
     * Dispatches a compute operation
     * @param Operation Operation to dispatch
     * @return True if dispatch was successful, false otherwise
     */
    virtual bool DispatchCompute(const FComputeOperation& Operation) = 0;
    
    /**
     * Batches multiple operations for more efficient processing
     * @param Operations Array of operations to batch
     * @return True if batching was successful, false otherwise
     */
    virtual bool BatchOperations(const TArray<FComputeOperation>& Operations) = 0;
    
    /**
     * Cancels an in-progress operation
     * @param OperationId ID of the operation to cancel
     * @return True if operation was canceled, false if not found or already completed
     */
    virtual bool CancelOperation(int64 OperationId) = 0;
    
    /**
     * Queries the status of an operation
     * @param OperationId ID of the operation to query
     * @param OutStatus Operation status information
     * @return True if operation was found, false otherwise
     */
    virtual bool QueryOperationStatus(int64 OperationId, FOperationStatus& OutStatus) = 0;
    
    /**
     * Gets the compute capabilities of the current system
     * @return Structure containing hardware capabilities
     */
    virtual FComputeCapabilities GetCapabilities() const = 0;

    /**
     * Dispatches a compute operation asynchronously
     * @param Operation Operation to dispatch
     * @param CompletionCallback Callback function to be called when operation completes
     * @return True if dispatch was successful, false otherwise
     */
    virtual bool DispatchComputeAsync(const FComputeOperation& Operation, TFunction<void(bool, float)> CompletionCallback) = 0;
    
    /**
     * Dispatches an SDF operation with the specified parameters
     * @param OpType SDF operation type
     * @param Bounds Operation bounds
     * @param InputBuffers Input RDG buffers
     * @param OutputBuffer Output RDG buffer
     * @return True if dispatch was successful, false otherwise
     */
    virtual bool DispatchSDFOperation(int32 OpType, const FBox& Bounds, 
        const TArray<FRDGBufferRef>& InputBuffers, FRDGBufferRef OutputBuffer) = 0;
    
    /**
     * Dispatches a material operation with the specified parameters
     * @param MaterialChannelId Material channel ID
     * @param Bounds Operation bounds
     * @param InputBuffers Input RDG buffers
     * @param OutputBuffer Output RDG buffer
     * @return True if dispatch was successful, false otherwise
     */
    virtual bool DispatchMaterialOperation(int32 MaterialChannelId, const FBox& Bounds, 
        const TArray<FRDGBufferRef>& InputBuffers, FRDGBufferRef OutputBuffer) = 0;
    
    /**
     * Flushes all pending operations and waits for completion
     * @param bWaitForCompletion Whether to wait for all operations to complete
     * @return True if all operations completed successfully, false otherwise
     */
    virtual bool FlushOperations(bool bWaitForCompletion) = 0;
};