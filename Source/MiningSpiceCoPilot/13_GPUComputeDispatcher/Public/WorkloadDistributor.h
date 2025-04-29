#pragma once

#include "CoreMinimal.h"
#include "Containers/CircularBuffer.h"
#include "13_GPUComputeDispatcher/Public/Interfaces/IWorkloadDistributor.h"
#include "13_GPUComputeDispatcher/Public/ComputeOperationTypes.h"

struct FPerformanceHistory
{
    TArray<FOperationMetrics> History;
    float AverageCPUTime = 0.0f;
    float AverageGPUTime = 0.0f;
    float CPUToGPURatio = 1.0f;
};

struct FOperationStats
{
    float AvgCPUTime = 0.0f;
    float AvgGPUTime = 0.0f;
    uint32 CPUCount = 0;
    uint32 GPUCount = 0;
    float Complexity = 0.0f;
    float SuccessRate = 1.0f;
};

class FAdaptivePerformanceSystem;

/**
 * Workload distributor for CPU/GPU operation routing
 * Determines optimal processing target based on operation parameters
 * Learns from historical performance to improve decisions
 */
class MININGSPICECOPILOT_API FWorkloadDistributor : public IWorkloadDistributor
{
public:
    FWorkloadDistributor();
    virtual ~FWorkloadDistributor();
    
    // Initialization with hardware profile
    bool Initialize(const FHardwareProfile& Profile);
    
    //~ Begin IWorkloadDistributor Interface
    virtual EProcessingTarget DetermineProcessingTarget(const FComputeOperation& Operation) override;
    virtual void UpdatePerformanceMetrics(const FOperationMetrics& Metrics) override;
    virtual void ResetMetrics() override;
    virtual bool SplitOperation(const FComputeOperation& Operation, TArray<FComputeOperation>& OutSubOperations) override;
    virtual bool MergeOperations(const TArray<FComputeOperation>& Operations, TArray<FOperationBatch>& OutBatches) override;
    virtual void SetDistributionConfig(const FDistributionConfig& Config) override;
    virtual FDistributionConfig GetDistributionConfig() const override;
    virtual void AdjustForMemoryPressure(uint64 AvailableBytes) override;
    virtual void IncreaseCPUWorkloadRatio(float AdditionalRatio) override;
    //~ End IWorkloadDistributor Interface
    
    // Adaptive performance refinement
    void RefineDistributionStrategy(const FPerformanceHistory& History);
    
    // Fallback mechanisms
    bool ApplyFallbackStrategy(FComputeOperation& Operation, int32 Strategy);
    bool SplitBetweenCPUAndGPU(FComputeOperation& Operation, TArray<FComputeOperation>& OutOperations);
    
private:
    // Distribution strategies
    float CalculateOperationComplexity(const FComputeOperation& Operation);
    EProcessingTarget SelectTargetBasedOnComplexity(float Complexity);
    bool IsNarrowBandOperation(const FBox& Bounds, int32 MaterialChannel);
    
    // Batch optimization
    bool GroupBySpatialLocality(const TArray<FComputeOperation>& Operations, TArray<TArray<int32>>& OutGroups);
    FBox GetBoundingBox(const TArray<FComputeOperation>& Operations, const TArray<int32>& Indices);
    bool IsSpatiallyCoherent(const FBox& BoxA, const FBox& BoxB, float Threshold);
    
    // Learning system
    void UpdateDecisionModel(const FOperationMetrics& Metrics);
    float PredictGPUPerformance(const FComputeOperation& Operation);
    float PredictCPUPerformance(const FComputeOperation& Operation);
    void UpdateOperationStats(uint32 OperationTypeId, float CPUTime, float GPUTime, bool bSuccess);
    
    // Member variables
    FDistributionConfig Config;
    TMap<uint32, FOperationStats> OperationStats;
    FThreadSafeCounter64 GPUOperationCount;
    FThreadSafeCounter64 CPUOperationCount;
    TArray<float> CPUToGPUPerformanceRatio;
    FHardwareProfile HardwareProfile;
    
    // Learning system
    TSharedPtr<FAdaptivePerformanceSystem> PerformanceSystem;
    TMap<uint32, FPerformanceHistory> PerformanceHistoryByType;
    TCircularBuffer<FOperationMetrics> RecentOperations;
    float MemoryPressureAdjustment;
    float CPUWorkloadRatioBoost;
    FCriticalSection StatsLock;
};