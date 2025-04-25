#pragma once

#include "CoreMinimal.h"
#include "Interface/IWorkloadDistributor.h"
#include "Containers/Map.h"
#include "Containers/Queue.h"
#include "HAL/CriticalSection.h"
#include "Templates/SharedPointer.h"
#include "HAL/ThreadSafeBool.h"
#include "Misc/ScopeLock.h"
#include "Async/AsyncWork.h"
#include "HAL/ThreadSafeCounter.h"

class FHardwareProfileManager;

/**
 * Workload distributor implementation for SDF operations
 * Provides intelligent routing of operations between CPU and GPU
 * based on operation characteristics, hardware capabilities, and
 * learned performance history.
 */
class MININGSPICECOPILOT_API FWorkloadDistributor : public IWorkloadDistributor
{
public:
    /** Constructor */
    FWorkloadDistributor();
    
    /** Destructor */
    virtual ~FWorkloadDistributor();

    //~ Begin IWorkloadDistributor Interface
    virtual bool Initialize() override;
    virtual void Shutdown() override;
    virtual bool IsInitialized() const override;
    
    virtual EWorkloadTarget AnalyzeOperation(
        const FSDFOperationInfo& OperationInfo,
        const FOperationCharacteristics& Characteristics) override;
    
    virtual EWorkloadTarget AnalyzeOperationById(
        uint32 OperationId,
        const FOperationCharacteristics& Characteristics) override;
    
    virtual FHardwareCapabilityInfo GetHardwareCapabilities() const override;
    
    virtual void ReportOperationPerformance(
        uint32 OperationId,
        EWorkloadTarget Target,
        float ExecutionTimeMS,
        bool bWasSuccessful) override;
    
    virtual TMap<EWorkloadTarget, float> GetOperationPerformanceHistory(uint32 OperationId) const override;
    
    virtual FDistributionStatistics GetDistributionStatistics(bool bResetStats = false) const override;
    
    virtual void SetDistributionConfig(const FDistributionConfig& Config) override;
    virtual FDistributionConfig GetDistributionConfig() const override;
    
    virtual int32 GetOptimalCPUThreadCount(
        uint32 OperationId,
        const FOperationCharacteristics& Characteristics) const override;
    
    virtual FIntVector GetOptimalGPUThreadGroupCounts(
        uint32 OperationId,
        const FOperationCharacteristics& Characteristics) const override;
    
    virtual bool IsOperationSupported(uint32 OperationId, EWorkloadTarget Target) const override;
    
    virtual void ResetPerformanceHistory() override;
    
    virtual void UpdateHardwareUtilization() override;
    
    virtual float EstimateExecutionTime(
        uint32 OperationId,
        EWorkloadTarget Target,
        const FOperationCharacteristics& Characteristics) const override;
    
    virtual float GetHybridSplitRatio(
        uint32 OperationId,
        const FOperationCharacteristics& Characteristics) const override;
    
    virtual FName GetDistributorName() const override;
    
    virtual float GetCurrentCapacity(EWorkloadTarget Target) const override;
    
    virtual bool HasPerformanceImbalance() const override;
    
    virtual bool GenerateHardwareProfile(const FName& ProfileName) override;
    
    virtual int32 CalibrateOperationPerformance(
        const TArray<uint32>& OperationIds = TArray<uint32>(),
        bool bDetailedCalibration = false) override;
    
    virtual bool ExportPerformanceProfile(const FString& FilePath) const override;
    
    virtual bool ImportPerformanceProfile(const FString& FilePath) override;
    
    virtual void SetTypeRegistry(const FSDFTypeRegistry* Registry) override;
    
    virtual float AnalyzeMaterialCompatibility(uint32 MaterialTypeId, EWorkloadTarget Target) const override;
    
    virtual bool RegisterMaterialDistributionStrategy(
        uint32 MaterialTypeId, 
        TFunction<EWorkloadTarget(const FOperationCharacteristics&)> Strategy) override;
    //~ End IWorkloadDistributor Interface

    /**
     * Set reference to hardware profile manager
     * @param InProfileManager Pointer to the hardware profile manager
     */
    void SetHardwareProfileManager(FHardwareProfileManager* InProfileManager);

    /**
     * Set the task scheduler reference for CPU-side execution
     * @param InTaskScheduler Pointer to the task scheduler
     */
    void SetTaskScheduler(class FTaskScheduler* InTaskScheduler);

    /**
     * Set the parallel executor reference
     * @param InExecutor Pointer to the parallel executor
     */
    void SetParallelExecutor(class FParallelExecutor* InExecutor);

private:
    /** Hardware profile manager */
    FHardwareProfileManager* HardwareProfileManager;
    
    /** Task scheduler for CPU-side execution */
    class FTaskScheduler* TaskScheduler;
    
    /** Parallel executor for CPU-side parallel execution */
    class FParallelExecutor* ParallelExecutor;
    
    /** Type registry reference */
    const FSDFTypeRegistry* TypeRegistry;
    
    /** Hardware capability information */
    FHardwareCapabilityInfo HardwareCapabilities;
    
    /** Distribution configuration */
    FDistributionConfig DistributionConfig;
    
    /** Distribution statistics */
    mutable FDistributionStatistics Statistics;
    
    /** Flag for initialization status */
    FThreadSafeBool bIsInitialized;
    
    /** Last hardware utilization update time */
    double LastUtilizationUpdateTime;
    
    /** Current CPU utilization (0-1) */
    FThreadSafeCounter64 CurrentCPUUtilization;
    
    /** Current GPU utilization (0-1) */
    FThreadSafeCounter64 CurrentGPUUtilization;
    
    /** Current available GPU memory (bytes) */
    FThreadSafeCounter64 AvailableGPUMemory;
    
    /** Critical section for statistics access */
    mutable FCriticalSection StatisticsLock;
    
    /** Critical section for history data access */
    mutable FCriticalSection HistoryLock;
    
    /** Critical section for materiall strategy registration */
    mutable FCriticalSection MaterialStrategyLock;
    
    /** Operation performance history */
    struct FPerformanceHistoryEntry
    {
        /** Target where operation was executed */
        EWorkloadTarget Target;
        
        /** Execution time in milliseconds */
        float ExecutionTimeMS;
        
        /** Operation characteristics */
        FOperationCharacteristics Characteristics;
        
        /** Whether execution was successful */
        bool bWasSuccessful;
        
        /** Timestamp when entry was recorded */
        double Timestamp;
    };
    
    /** Performance history by operation ID */
    TMap<uint32, TArray<FPerformanceHistoryEntry>> PerformanceHistory;
    
    /** Last decision result by operation ID */
    TMap<uint32, EWorkloadTarget> LastDecisionByOperationId;
    
    /** Custom distribution strategies by material type ID */
    TMap<uint32, TFunction<EWorkloadTarget(const FOperationCharacteristics&)>> MaterialDistributionStrategies;
    
    /** Learning weights for distribution decision features */
    struct FDistributionWeights
    {
        float ComputeComplexityWeight;
        float MemoryIntensityWeight;
        float ParallelizationWeight;
        float SpatialCoherenceWeight;
        float DataSizeWeight;
        float PrecisionWeight;
        float HardwareUtilizationWeight;
        float HistoricalPerformanceWeight;
        
        FDistributionWeights()
            : ComputeComplexityWeight(1.0f)
            , MemoryIntensityWeight(1.0f)
            , ParallelizationWeight(1.0f)
            , SpatialCoherenceWeight(0.5f)
            , DataSizeWeight(0.8f)
            , PrecisionWeight(1.5f)
            , HardwareUtilizationWeight(1.2f)
            , HistoricalPerformanceWeight(2.0f)
        {
        }
    };
    
    /** Decision weights with learning capability */
    FDistributionWeights FeatureWeights;
    
    /** Narrow-band SDF parameters */
    struct FNarrowBandParameters
    {
        /** Threshold for narrow band optimization */
        float NarrowBandThreshold;
        
        /** Bias toward CPU for narrow band operations */
        float CPUBiasForNarrowBand;
        
        /** Minimum voxel count for GPU processing of narrow band */
        int32 MinNarrowBandVoxelsForGPU;
        
        /** Constructor with default values */
        FNarrowBandParameters()
            : NarrowBandThreshold(0.05f)  // 5% of field size
            , CPUBiasForNarrowBand(0.3f)  // 30% bias toward CPU
            , MinNarrowBandVoxelsForGPU(1024) // Minimum voxels for GPU to be efficient
        {
        }
    };
    
    /** Narrow band parameters */
    FNarrowBandParameters NarrowBandParams;
    
    /** Detect hardware capabilities */
    void DetectHardwareCapabilities();
    
    /**
     * Calculate score for CPU execution
     * @param Characteristics Operation characteristics
     * @return Score where higher values favor CPU execution
     */
    float CalculateCPUScore(const FOperationCharacteristics& Characteristics) const;
    
    /**
     * Calculate score for GPU execution
     * @param Characteristics Operation characteristics
     * @return Score where higher values favor GPU execution
     */
    float CalculateGPUScore(const FOperationCharacteristics& Characteristics) const;
    
    /**
     * Determine if operation should use hybrid processing
     * @param Characteristics Operation characteristics
     * @return True if hybrid processing is recommended
     */
    bool ShouldUseHybridProcessing(
        uint32 OperationId,
        const FOperationCharacteristics& Characteristics) const;
    
    /**
     * Apply learning from execution results
     * @param OperationId ID of the operation
     * @param Target Target that was used
     * @param Characteristics Operation characteristics
     * @param ExecutionTimeMS Actual execution time
     * @param bWasSuccessful Whether execution was successful
     */
    void ApplyLearning(
        uint32 OperationId,
        EWorkloadTarget Target,
        const FOperationCharacteristics& Characteristics,
        float ExecutionTimeMS,
        bool bWasSuccessful);
    
    /**
     * Update feature weights based on execution results
     * @param ExpectedTarget Expected target from analysis
     * @param ActualTarget Actual target that performed better
     * @param Characteristics Operation characteristics
     * @param Delta Performance difference (in milliseconds)
     */
    void UpdateFeatureWeights(
        EWorkloadTarget ExpectedTarget,
        EWorkloadTarget ActualTarget,
        const FOperationCharacteristics& Characteristics,
        float Delta);
    
    /**
     * Get historical performance for an operation
     * @param OperationId ID of the operation
     * @param Target Execution target
     * @param OutTimeMS Output parameter for average execution time
     * @param OutSampleCount Output parameter for sample count
     * @return True if historical data exists
     */
    bool GetHistoricalPerformance(
        uint32 OperationId,
        EWorkloadTarget Target,
        float& OutTimeMS,
        int32& OutSampleCount) const;
    
    /**
     * Check if operation can be effectively processed in narrow band
     * @param OperationId ID of the operation
     * @param Characteristics Operation characteristics
     * @return True if narrow band optimization should be used
     */
    bool CanUseNarrowBand(
        uint32 OperationId,
        const FOperationCharacteristics& Characteristics) const;
    
    /**
     * Calculate optimal split for hybrid processing
     * @param OperationId ID of the operation
     * @param Characteristics Operation characteristics
     * @return Percentage of work to assign to CPU (0-1)
     */
    float CalculateHybridSplit(
        uint32 OperationId,
        const FOperationCharacteristics& Characteristics) const;
    
    /**
     * Analyze material properties for execution target compatibility
     * @param MaterialTypeId ID of the material type
     * @param Target Execution target
     * @return Compatibility score (0-1)
     */
    float AnalyzeMaterialPropertiesForTarget(uint32 MaterialTypeId, EWorkloadTarget Target) const;
    
    /**
     * Re-evaluate distribution decision based on hardware utilization
     * @param InitialTarget Initially chosen target
     * @param Characteristics Operation characteristics
     * @return Possibly adjusted target based on hardware utilization
     */
    EWorkloadTarget AdjustForHardwareUtilization(
        EWorkloadTarget InitialTarget,
        const FOperationCharacteristics& Characteristics) const;
    
    /**
     * Update distribution statistics with operation data
     * @param Target Execution target
     * @param ExecutionTimeMS Execution time in milliseconds
     * @param bWasSuccessful Whether execution was successful
     * @param Characteristics Operation characteristics
     */
    void UpdateStatistics(
        EWorkloadTarget Target,
        float ExecutionTimeMS,
        bool bWasSuccessful,
        const FOperationCharacteristics& Characteristics);
    
    /**
     * Apply material-specific distribution strategy if registered
     * @param MaterialTypeId ID of the material type
     * @param Characteristics Operation characteristics
     * @param OutTarget Output parameter for recommended target
     * @return True if a strategy was applied
     */
    bool ApplyMaterialDistributionStrategy(
        uint32 MaterialTypeId,
        const FOperationCharacteristics& Characteristics,
        EWorkloadTarget& OutTarget) const;
    
    /**
     * Get the most recent performance history entries
     * @param OperationId ID of the operation
     * @param MaxEntries Maximum number of entries to return
     * @return Array of recent history entries
     */
    TArray<FPerformanceHistoryEntry> GetRecentHistoryEntries(
        uint32 OperationId,
        int32 MaxEntries) const;
    
    /**
     * Estimate GPU memory requirements for an operation
     * @param OperationId ID of the operation
     * @param Characteristics Operation characteristics
     * @return Estimated GPU memory requirement in bytes
     */
    int64 EstimateGPUMemoryRequirement(
        uint32 OperationId,
        const FOperationCharacteristics& Characteristics) const;
    
    /**
     * Analyze distribution decision quality
     * @param OperationId ID of the operation
     * @param Target Chosen execution target
     * @param ExecutionTimeMS Actual execution time
     * @return Distribution decision quality score (0-1)
     */
    float AnalyzeDecisionQuality(
        uint32 OperationId,
        EWorkloadTarget Target,
        float ExecutionTimeMS) const;
};
