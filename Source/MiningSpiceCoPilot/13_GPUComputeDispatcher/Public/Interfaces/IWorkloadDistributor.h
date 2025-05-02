#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "../Public/ComputeOperationTypes.h"
#include "IWorkloadDistributor.generated.h"

UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UWorkloadDistributor : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for workload distribution between CPU and GPU
 * Provides methods for determining optimal processing target for operations
 * with adaptive learning based on performance metrics
 */
class MININGSPICECOPILOT_API IWorkloadDistributor
{
    GENERATED_BODY()

public:
    /**
     * Determines the processing target for an operation
     * @param Operation Operation to evaluate
     * @return Target processor (CPU, GPU, or Hybrid)
     */
    virtual EProcessingTarget DetermineProcessingTarget(const FComputeOperation& Operation) = 0;
    
    /**
     * Updates performance metrics for learning
     * @param Metrics Operation performance metrics
     */
    virtual void UpdatePerformanceMetrics(const FOperationMetrics& Metrics) = 0;
    
    /**
     * Resets metrics and learning data
     */
    virtual void ResetMetrics() = 0;
    
    /**
     * Splits an operation into multiple sub-operations for parallel processing
     * @param Operation Operation to split
     * @param OutSubOperations Output array of sub-operations
     * @return True if splitting was successful, false otherwise
     */
    virtual bool SplitOperation(const FComputeOperation& Operation, TArray<FComputeOperation>& OutSubOperations) = 0;
    
    /**
     * Merges similar operations into batches for more efficient processing
     * @param Operations Operations to merge
     * @param OutBatches Output array of operation batches
     * @return True if merging was successful, false otherwise
     */
    virtual bool MergeOperations(const TArray<FComputeOperation>& Operations, TArray<FOperationBatch>& OutBatches) = 0;
    
    /**
     * Sets the distribution configuration
     * @param Config Distribution configuration
     */
    virtual void SetDistributionConfig(const FDistributionConfig& Config) = 0;
    
    /**
     * Gets the current distribution configuration
     * @return Current distribution configuration
     */
    virtual FDistributionConfig GetDistributionConfig() const = 0;
    
    /**
     * Adjusts distribution strategy for memory pressure
     * @param AvailableBytes Available memory in bytes
     */
    virtual void AdjustForMemoryPressure(int64 AvailableBytes) = 0;
    
    /**
     * Increases CPU workload ratio for fault tolerance
     * @param AdditionalRatio Additional ratio to allocate to CPU (0.0-1.0)
     */
    virtual void IncreaseCPUWorkloadRatio(float AdditionalRatio) = 0;
};