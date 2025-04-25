#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "HAL/ThreadSafeBool.h"
#include "Templates/SharedPointer.h"
#include "../../1_CoreRegistry/Public/Interfaces/IRegistry.h"
#include "../../1_CoreRegistry/Public/SDFTypeRegistry.h"
#include "OperationTypes.h"

/**
 * Structure for hardware capability information
 */
struct FHardwareCapabilityInfo
{
    // GPU capabilities
    bool bSupportsCompute;
    bool bSupportsAsyncCompute;
    int32 ComputeShaderModel;
    int32 MaxComputeUnits;
    int64 DedicatedGPUMemory;
    int64 SharedGPUMemory;
    FString GPUName;
    
    // CPU capabilities
    int32 NumLogicalCores;
    int32 NumPhysicalCores;
    bool bSupportsSIMD;
    bool bSupportsAVX;
    bool bSupportsAVX2;
    bool bSupportsSSE4;
    FString CPUName;
    
    // Memory capabilities
    int64 SystemRAM;
    bool bSupportsUnifiedMemory;
    
    // Default constructor
    FHardwareCapabilityInfo()
        : bSupportsCompute(false)
        , bSupportsAsyncCompute(false)
        , ComputeShaderModel(0)
        , MaxComputeUnits(0)
        , DedicatedGPUMemory(0)
        , SharedGPUMemory(0)
        , NumLogicalCores(0)
        , NumPhysicalCores(0)
        , bSupportsSIMD(false)
        , bSupportsAVX(false)
        , bSupportsAVX2(false)
        , bSupportsSSE4(false)
        , SystemRAM(0)
        , bSupportsUnifiedMemory(false)
    {
    }
};

/**
 * Operation characteristics for distribution decisions
 */
struct FOperationCharacteristics
{
    // Operation metadata
    uint32 OperationId;
    FName OperationType;
    uint32 DataSize;
    uint32 FieldTypeId;
    
    // Complexity metrics
    float ComputeComplexity;    // 0-1 where 1 is most complex
    float MemoryIntensity;      // 0-1 where 1 is most intensive
    float ParallelizationScore; // 0-1 where 1 is most parallelizable
    
    // Performance history
    float HistoricalGPUTimeMS;
    float HistoricalCPUTimeMS;
    int32 HistoricalSampleCount;
    
    // Spatial coherence 
    float SpatialCoherenceScore;  // 0-1 where 1 is high coherence
    
    // Additional factors
    bool bRequiresPrecision;
    bool bIsNarrowBand;
    bool bHasSIMDImplementation;
    bool bHasGPUImplementation;
    
    // Default constructor
    FOperationCharacteristics()
        : OperationId(0)
        , DataSize(0)
        , FieldTypeId(0)
        , ComputeComplexity(0.5f)
        , MemoryIntensity(0.5f)
        , ParallelizationScore(0.5f)
        , HistoricalGPUTimeMS(0.0f)
        , HistoricalCPUTimeMS(0.0f)
        , HistoricalSampleCount(0)
        , SpatialCoherenceScore(0.5f)
        , bRequiresPrecision(false)
        , bIsNarrowBand(false)
        , bHasSIMDImplementation(false)
        , bHasGPUImplementation(false)
    {
    }
};

/**
 * Distribution statistics for performance analysis
 */
struct FDistributionStatistics
{
    // Overall distribution
    float CPUWorkloadPercent;
    float GPUWorkloadPercent;
    float HybridWorkloadPercent;
    
    // Operation counts
    int32 TotalOperations;
    int32 CPUOperations;
    int32 GPUOperations;
    int32 HybridOperations;
    
    // Performance metrics
    float AverageGPUTimeMS;
    float AverageCPUTimeMS;
    float AverageHybridTimeMS;
    
    // Efficiency metrics
    float CPUEfficiencyScore;   // 0-1 where 1 is most efficient
    float GPUEfficiencyScore;   // 0-1 where 1 is most efficient
    float HybridEfficiencyScore; // 0-1 where 1 is most efficient
    
    // Utilization metrics
    float CPUUtilization;       // 0-1 where 1 is fully utilized
    float GPUUtilization;       // 0-1 where 1 is fully utilized
    
    // Decision quality
    float DecisionAccuracyScore; // 0-1 where 1 is perfect decisions
    
    // Default constructor
    FDistributionStatistics()
        : CPUWorkloadPercent(0.0f)
        , GPUWorkloadPercent(0.0f)
        , HybridWorkloadPercent(0.0f)
        , TotalOperations(0)
        , CPUOperations(0)
        , GPUOperations(0)
        , HybridOperations(0)
        , AverageGPUTimeMS(0.0f)
        , AverageCPUTimeMS(0.0f)
        , AverageHybridTimeMS(0.0f)
        , CPUEfficiencyScore(0.0f)
        , GPUEfficiencyScore(0.0f)
        , HybridEfficiencyScore(0.0f)
        , CPUUtilization(0.0f)
        , GPUUtilization(0.0f)
        , DecisionAccuracyScore(0.0f)
    {
    }
};

/**
 * Configuration struct for workload distribution
 */
struct FDistributionConfig
{
    // Distribution bias
    float GPUBias;         // 0-1 where higher values favor GPU
    float CPUBias;         // 0-1 where higher values favor CPU
    
    // Thresholds
    float HybridThreshold; // Complexity threshold for hybrid processing
    float PrecisionThreshold; // Precision threshold for CPU fallback
    
    // Learning rate for adaptive distribution
    float AdaptiveLearningRate; // 0-1 where higher values adapt faster
    
    // Sample size for history tracking
    int32 HistorySampleSize;
    
    // Load balancing settings
    bool bEnableDynamicLoadBalancing;
    float LoadImbalanceThreshold; // Threshold to trigger rebalancing
    
    // Memory thresholds
    float GPUMemoryThreshold; // 0-1 percentage of GPU memory to use before CPU fallback
    
    // Performance targets
    float TargetFrameTimeMS;
    
    // Default constructor with reasonable defaults
    FDistributionConfig()
        : GPUBias(0.5f)
        , CPUBias(0.5f)
        , HybridThreshold(0.7f)
        , PrecisionThreshold(0.8f)
        , AdaptiveLearningRate(0.1f)
        , HistorySampleSize(100)
        , bEnableDynamicLoadBalancing(true)
        , LoadImbalanceThreshold(0.3f)
        , GPUMemoryThreshold(0.9f)
        , TargetFrameTimeMS(16.0f) // Target ~60 FPS
    {
    }
};

/**
 * Interface for workload distribution between CPU and GPU
 * Makes intelligent decisions on where to execute operations based on
 * hardware capabilities, operation characteristics, and historical performance.
 * Provides adaptive learning to improve distribution decisions over time.
 */
class MININGSPICECOPILOT_API IWorkloadDistributor
{
public:
    /** Virtual destructor */
    virtual ~IWorkloadDistributor() {}
    
    /**
     * Initialize the distributor
     * Sets up internal resources and hardware capability detection
     * @return True if initialization was successful
     */
    virtual bool Initialize() = 0;
    
    /**
     * Shutdown the distributor
     * Releases all resources and performs any necessary cleanup
     */
    virtual void Shutdown() = 0;
    
    /**
     * Check if distributor is initialized
     * @return True if the distributor is properly initialized
     */
    virtual bool IsInitialized() const = 0;
    
    /**
     * Analyze an operation and recommend where to execute it
     * Examines operation characteristics and historical performance
     * to determine the optimal execution target
     * 
     * @param OperationInfo The SDF operation to analyze
     * @param Characteristics Additional operation characteristics
     * @return Recommended execution target
     */
    virtual EWorkloadTarget AnalyzeOperation(
        const FSDFOperationInfo& OperationInfo,
        const FOperationCharacteristics& Characteristics) = 0;
    
    /**
     * Analyze an operation and recommend where to execute it
     * Examines operation characteristics and historical performance
     * to determine the optimal execution target
     * 
     * @param OperationId ID of the registered SDF operation
     * @param Characteristics Additional operation characteristics
     * @return Recommended execution target
     */
    virtual EWorkloadTarget AnalyzeOperationById(
        uint32 OperationId,
        const FOperationCharacteristics& Characteristics) = 0;
    
    /**
     * Get the current hardware capabilities
     * @return Hardware capability information
     */
    virtual FHardwareCapabilityInfo GetHardwareCapabilities() const = 0;
    
    /**
     * Report operation performance for adaptive learning
     * Provides feedback on operation performance to improve future decisions
     * 
     * @param OperationId ID of the operation
     * @param Target Where the operation was executed
     * @param ExecutionTimeMS Execution time in milliseconds
     * @param bWasSuccessful Whether the operation was successful
     */
    virtual void ReportOperationPerformance(
        uint32 OperationId,
        EWorkloadTarget Target,
        float ExecutionTimeMS,
        bool bWasSuccessful) = 0;
    
    /**
     * Get performance history for an operation
     * @param OperationId ID of the operation
     * @return Map of targets to execution times
     */
    virtual TMap<EWorkloadTarget, float> GetOperationPerformanceHistory(uint32 OperationId) const = 0;
    
    /**
     * Get current distribution statistics
     * Retrieves statistics about workload distribution
     * 
     * @param bResetStats Whether to reset statistics after retrieval
     * @return Distribution statistics
     */
    virtual FDistributionStatistics GetDistributionStatistics(bool bResetStats = false) const = 0;
    
    /**
     * Set the distribution configuration
     * @param Config New distribution configuration
     */
    virtual void SetDistributionConfig(const FDistributionConfig& Config) = 0;
    
    /**
     * Get the current distribution configuration
     * @return Current distribution configuration
     */
    virtual FDistributionConfig GetDistributionConfig() const = 0;
    
    /**
     * Determine optimal thread counts for CPU execution
     * Calculates the optimal number of CPU threads for an operation
     * 
     * @param OperationId ID of the operation
     * @param Characteristics Operation characteristics
     * @return Recommended thread count
     */
    virtual int32 GetOptimalCPUThreadCount(
        uint32 OperationId,
        const FOperationCharacteristics& Characteristics) const = 0;
    
    /**
     * Determine optimal thread group counts for GPU execution
     * Calculates the optimal thread group configuration for GPU execution
     * 
     * @param OperationId ID of the operation
     * @param Characteristics Operation characteristics
     * @return Recommended thread group counts
     */
    virtual FIntVector GetOptimalGPUThreadGroupCounts(
        uint32 OperationId,
        const FOperationCharacteristics& Characteristics) const = 0;
    
    /**
     * Check if hardware supports a specific operation
     * @param OperationId ID of the operation
     * @param Target Target execution platform (CPU/GPU)
     * @return True if the operation is supported
     */
    virtual bool IsOperationSupported(uint32 OperationId, EWorkloadTarget Target) const = 0;
    
    /**
     * Reset the performance history
     * Clears all collected performance data
     */
    virtual void ResetPerformanceHistory() = 0;
    
    /**
     * Update current hardware utilization
     * Updates internal state with current CPU and GPU utilization
     */
    virtual void UpdateHardwareUtilization() = 0;
    
    /**
     * Estimate execution time for an operation
     * Provides an estimated execution time based on historical data
     * 
     * @param OperationId ID of the operation
     * @param Target Target execution platform
     * @param Characteristics Operation characteristics
     * @return Estimated execution time in milliseconds
     */
    virtual float EstimateExecutionTime(
        uint32 OperationId,
        EWorkloadTarget Target,
        const FOperationCharacteristics& Characteristics) const = 0;
    
    /**
     * Get recommended split ratio for hybrid execution
     * Determines how to split work between CPU and GPU for hybrid execution
     * 
     * @param OperationId ID of the operation
     * @param Characteristics Operation characteristics
     * @return Ratio of work to assign to CPU (0-1)
     */
    virtual float GetHybridSplitRatio(
        uint32 OperationId,
        const FOperationCharacteristics& Characteristics) const = 0;
    
    /**
     * Get name of this distributor implementation
     * @return Name of the distributor
     */
    virtual FName GetDistributorName() const = 0;
    
    /**
     * Get current workload capacity
     * Returns the available processing capacity for a given target
     * 
     * @param Target Target execution platform
     * @return Available capacity (0-1 where 1 is fully available)
     */
    virtual float GetCurrentCapacity(EWorkloadTarget Target) const = 0;
    
    /**
     * Check if there's a performance imbalance between CPU and GPU
     * @return True if there's a significant imbalance
     */
    virtual bool HasPerformanceImbalance() const = 0;
    
    /**
     * Generate a hardware profile
     * Creates and saves a detailed hardware profile for later use
     * 
     * @param ProfileName Name for the generated profile
     * @return True if profile was generated successfully
     */
    virtual bool GenerateHardwareProfile(const FName& ProfileName) = 0;
    
    /**
     * Calibrate operation performance models
     * Runs calibration tests to build accurate performance models
     * 
     * @param OperationIds Operations to calibrate (empty = all)
     * @param bDetailedCalibration Whether to perform detailed calibration
     * @return Number of operations calibrated
     */
    virtual int32 CalibrateOperationPerformance(
        const TArray<uint32>& OperationIds = TArray<uint32>(),
        bool bDetailedCalibration = false) = 0;
    
    /**
     * Export performance profile data
     * @param FilePath Path to export the data to
     * @return True if export was successful
     */
    virtual bool ExportPerformanceProfile(const FString& FilePath) const = 0;
    
    /**
     * Import performance profile data
     * @param FilePath Path to import the data from
     * @return True if import was successful
     */
    virtual bool ImportPerformanceProfile(const FString& FilePath) = 0;
    
    /**
     * Set the SDF type registry for this distributor
     * @param Registry Pointer to the SDF type registry
     */
    virtual void SetTypeRegistry(const FSDFTypeRegistry* Registry) = 0;
    
    /**
     * Analyze compatibility between a material type and execution target
     * Determines if a material's properties are well-suited for a specific execution target
     * 
     * @param MaterialTypeId ID of the material type from MaterialRegistry
     * @param Target Target execution platform
     * @return Compatibility score (0-1 where 1 is most compatible)
     */
    virtual float AnalyzeMaterialCompatibility(uint32 MaterialTypeId, EWorkloadTarget Target) const = 0;
    
    /**
     * Register a specialized distribution strategy for a material type
     * Allows customizing distribution logic for specific materials
     * 
     * @param MaterialTypeId ID of the material type from MaterialRegistry
     * @param Strategy Custom distribution strategy function
     * @return True if registration was successful
     */
    virtual bool RegisterMaterialDistributionStrategy(
        uint32 MaterialTypeId, 
        TFunction<EWorkloadTarget(const FOperationCharacteristics&)> Strategy) = 0;
};
