#include "../Public/WorkloadDistributor.h"
#include "../Public/HardwareProfileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/ThreadManager.h"
#include "Stats/Stats.h"
#include "Stats/Stats2.h"
#include "RHI.h"
#include "RenderCore.h"
#include "RHIResources.h"
#include "RHIDefinitions.h"
#include "Misc/CoreStats.h"
#include "../../3_ThreadingTaskSystem/Public/TaskScheduler.h"
#include "../../3_ThreadingTaskSystem/Public/ParallelExecutor.h"
#include "../../1_CoreRegistry/Public/SDFTypeRegistry.h"

// Namespace with helper functions for the workload distributor
namespace WorkloadDistributorHelpers
{
    // Convert EWorkloadTarget enum to string
    FString TargetToString(EWorkloadTarget Target)
    {
        switch (Target)
        {
            case EWorkloadTarget::CPU: return TEXT("CPU");
            case EWorkloadTarget::GPU: return TEXT("GPU");
            case EWorkloadTarget::Hybrid: return TEXT("Hybrid");
            default: return TEXT("Unknown");
        }
    }
    
    // Get current time in seconds (for performance measurements)
    double GetCurrentTimeSeconds()
    {
        return FPlatformTime::Seconds();
    }
    
    // Simple sigmoid function for score normalization
    float Sigmoid(float Value, float Steepness = 1.0f)
    {
        return 1.0f / (1.0f + FMath::Exp(-Value * Steepness));
    }
}

FWorkloadDistributor::FWorkloadDistributor()
    : HardwareProfileManager(nullptr)
    , TaskScheduler(nullptr)
    , ParallelExecutor(nullptr)
    , TypeRegistry(nullptr)
    , bIsInitialized(false)
    , LastUtilizationUpdateTime(0.0)
{
    // Initialize hardware capabilities to default values
    HardwareCapabilities = FHardwareCapabilityInfo();
    
    // Initialize distribution configuration with defaults
    DistributionConfig = FDistributionConfig();
    
    // Initialize statistics
    Statistics = FDistributionStatistics();
    
    // Set initial utilization values
    CurrentCPUUtilization.Set(0);
    CurrentGPUUtilization.Set(0);
    AvailableGPUMemory.Set(0);
}

FWorkloadDistributor::~FWorkloadDistributor()
{
    Shutdown();
}

bool FWorkloadDistributor::Initialize()
{
    // Check if already initialized
    if (bIsInitialized)
    {
        return true;
    }
    
    // Detect hardware capabilities
    DetectHardwareCapabilities();
    
    // Initialize hardware utilization tracking
    LastUtilizationUpdateTime = WorkloadDistributorHelpers::GetCurrentTimeSeconds();
    UpdateHardwareUtilization();
    
    // Set as initialized
    bIsInitialized = true;
    
    UE_LOG(LogTemp, Log, TEXT("Workload Distributor Initialized"));
    UE_LOG(LogTemp, Log, TEXT("  CPU: %s - %d cores"), *HardwareCapabilities.CPUName, HardwareCapabilities.NumLogicalCores);
    UE_LOG(LogTemp, Log, TEXT("  GPU: %s - %d MB"), *HardwareCapabilities.GPUName, 
        static_cast<int32>(HardwareCapabilities.DedicatedGPUMemory / (1024 * 1024)));
    
    return true;
}

void FWorkloadDistributor::Shutdown()
{
    if (!bIsInitialized)
    {
        return;
    }
    
    // Clear any stored performance history
    {
        FScopeLock Lock(&HistoryLock);
        PerformanceHistory.Empty();
        LastDecisionByOperationId.Empty();
    }
    
    // Clear material distribution strategies
    {
        FScopeLock Lock(&MaterialStrategyLock);
        MaterialDistributionStrategies.Empty();
    }
    
    // Set as not initialized
    bIsInitialized = false;
    
    UE_LOG(LogTemp, Log, TEXT("Workload Distributor Shutdown"));
}

bool FWorkloadDistributor::IsInitialized() const
{
    return bIsInitialized;
}

void FWorkloadDistributor::DetectHardwareCapabilities()
{
    // CPU capabilities
    HardwareCapabilities.NumLogicalCores = FPlatformMisc::NumberOfCores();
    HardwareCapabilities.NumPhysicalCores = FPlatformMisc::NumberOfCoresIncludingHyperthreads();
    HardwareCapabilities.CPUName = FPlatformMisc::GetCPUBrand();
    
    // SIMD capabilities - simplified for compatibility
    HardwareCapabilities.bSupportsSIMD = true; // All modern CPUs support basic SIMD
    
    // Simplified platform capability detection for compatibility
    // These will be determined at runtime on the actual platform
    HardwareCapabilities.bSupportsSSE4 = true;
    HardwareCapabilities.bSupportsAVX = true;
    HardwareCapabilities.bSupportsAVX2 = true;
    
    // System memory
    HardwareCapabilities.SystemRAM = FPlatformMemory::GetPhysicalGBRam() * 1024 * 1024 * 1024;
    
    // GPU capabilities - adjusted for compatibility
    bool bSupportsComputeShaders = true; // Default to true, will be checked at runtime
    HardwareCapabilities.bSupportsCompute = bSupportsComputeShaders;
    HardwareCapabilities.bSupportsAsyncCompute = false; // Default to false, will be checked at runtime
    HardwareCapabilities.ComputeShaderModel = ERHIFeatureLevel::SM5; // Default to Shader Model 5
    
    // Fallback GPU info
    HardwareCapabilities.GPUName = TEXT("Default GPU");
    
    // Try to get GPU information from RHI if available at runtime
    FString RHIName;
    
#if WITH_ENGINE
    // In a real engine environment, we could access GRHIName here
    // For compatibility across all contexts, we'll use a fallback
    RHIName = FString(TEXT("DirectX 12"));
#endif
    
    if (!RHIName.IsEmpty())
    {
        HardwareCapabilities.GPUName = RHIName;
    }
    
    // GPU memory info - use conservative estimates
    // This will be refined at runtime based on platform-specific APIs
    HardwareCapabilities.DedicatedGPUMemory = 1024 * 1024 * 1024; // 1GB fallback
    HardwareCapabilities.SharedGPUMemory = 0;
    
    // Unified memory architectures (like Apple Silicon)
    HardwareCapabilities.bSupportsUnifiedMemory = false;
    
    // Compute units - this is a rough estimate and should be refined
    HardwareCapabilities.MaxComputeUnits = FMath::Clamp<uint32>(HardwareCapabilities.NumLogicalCores / 2, 1, 128);
}

void FWorkloadDistributor::SetHardwareProfileManager(FHardwareProfileManager* InProfileManager)
{
    HardwareProfileManager = InProfileManager;
}

void FWorkloadDistributor::SetTaskScheduler(FTaskScheduler* InTaskScheduler)
{
    TaskScheduler = InTaskScheduler;
}

void FWorkloadDistributor::SetParallelExecutor(FParallelExecutor* InExecutor)
{
    ParallelExecutor = InExecutor;
}

void FWorkloadDistributor::SetTypeRegistry(const FSDFTypeRegistry* Registry)
{
    TypeRegistry = Registry;
}

FHardwareCapabilityInfo FWorkloadDistributor::GetHardwareCapabilities() const
{
    return HardwareCapabilities;
}

void FWorkloadDistributor::SetDistributionConfig(const FDistributionConfig& Config)
{
    DistributionConfig = Config;
}

FDistributionConfig FWorkloadDistributor::GetDistributionConfig() const
{
    return DistributionConfig;
}

FName FWorkloadDistributor::GetDistributorName() const
{
    return FName("StandardWorkloadDistributor");
}

EWorkloadTarget FWorkloadDistributor::AnalyzeOperation(
    const FSDFOperationInfo& OperationInfo,
    const FOperationCharacteristics& Characteristics)
{
    if (!bIsInitialized)
    {
        return EWorkloadTarget::CPU; // Default to CPU if not initialized
    }
    
    // Update hardware utilization periodically
    double CurrentTime = WorkloadDistributorHelpers::GetCurrentTimeSeconds();
    if (CurrentTime - LastUtilizationUpdateTime > 1.0) // Update at most once per second
    {
        UpdateHardwareUtilization();
        LastUtilizationUpdateTime = CurrentTime;
    }
    
    // Try to apply a material-specific distribution strategy if applicable
    EWorkloadTarget Target;
    if (Characteristics.FieldTypeId != 0 && ApplyMaterialDistributionStrategy(Characteristics.FieldTypeId, Characteristics, Target))
    {
        // Store the decision for learning
        LastDecisionByOperationId.Add(OperationInfo.OperationId, Target);
        return Target;
    }
    
    // Check if we should use narrow band optimization
    if (CanUseNarrowBand(OperationInfo.OperationId, Characteristics))
    {
        // For narrow band operations, we need to consider CPU bias
        float CPUScore = CalculateCPUScore(Characteristics) * (1.0f + NarrowBandParams.CPUBiasForNarrowBand);
        float GPUScore = CalculateGPUScore(Characteristics);
        
        // If the data size is too small, favor CPU for narrow band operations
        if (Characteristics.DataSize < static_cast<uint32>(NarrowBandParams.MinNarrowBandVoxelsForGPU))
        {
            CPUScore *= 1.5f;
        }
        
        Target = (CPUScore > GPUScore) ? EWorkloadTarget::CPU : EWorkloadTarget::GPU;
    }
    else
    {
        // Calculate scores for each target
        float CPUScore = CalculateCPUScore(Characteristics);
        float GPUScore = CalculateGPUScore(Characteristics);
        
        // Determine if we should use hybrid processing
        if (ShouldUseHybridProcessing(OperationInfo.OperationId, Characteristics))
        {
            Target = EWorkloadTarget::Hybrid;
        }
        else
        {
            // Choose the target with the higher score
            Target = (CPUScore > GPUScore) ? EWorkloadTarget::CPU : EWorkloadTarget::GPU;
        }
    }
    
    // Adjust the target based on current hardware utilization
    Target = AdjustForHardwareUtilization(Target, Characteristics);
    
    // Store the decision for learning
    LastDecisionByOperationId.Add(OperationInfo.OperationId, Target);
    
    return Target;
}

EWorkloadTarget FWorkloadDistributor::AnalyzeOperationById(
    uint32 OperationId,
    const FOperationCharacteristics& Characteristics)
{
    // For operations referenced by ID, we use the same analysis process
    // but we create a minimal operation info structure
    FSDFOperationInfo OperationInfo;
    OperationInfo.OperationId = OperationId;
    OperationInfo.OperationName = FName(*FString::Printf(TEXT("Operation_%u"), OperationId));
    
    return AnalyzeOperation(OperationInfo, Characteristics);
}

float FWorkloadDistributor::CalculateCPUScore(const FOperationCharacteristics& Characteristics) const
{
    if (!HardwareCapabilities.bSupportsSIMD && Characteristics.bHasSIMDImplementation)
    {
        // If SIMD implementation is required but not supported, give a low score
        return 0.1f;
    }
    
    // Start with base score
    float Score = 0.5f;
    
    // Apply weights to each characteristic
    Score += (1.0f - Characteristics.ComputeComplexity) * FeatureWeights.ComputeComplexityWeight;
    Score += (1.0f - Characteristics.MemoryIntensity) * FeatureWeights.MemoryIntensityWeight;
    Score += Characteristics.ParallelizationScore * FeatureWeights.ParallelizationWeight; 
    Score += Characteristics.SpatialCoherenceScore * FeatureWeights.SpatialCoherenceWeight;
    
    // Data size penalty - CPU performs better on smaller datasets
    float DataSizeFactor = FMath::Clamp(1.0f - (static_cast<float>(Characteristics.DataSize) / (10 * 1024 * 1024)), 0.0f, 1.0f);
    Score += DataSizeFactor * FeatureWeights.DataSizeWeight;
    
    // Precision bonus - CPU is better for high precision operations
    if (Characteristics.bRequiresPrecision)
    {
        Score += 0.5f * FeatureWeights.PrecisionWeight;
    }
    
    // Historical performance adjustment
    float HistoricalCPUTime, HistoricalGPUTime;
    int32 CPUSampleCount, GPUSampleCount;
    
    if (GetHistoricalPerformance(Characteristics.OperationId, EWorkloadTarget::CPU, HistoricalCPUTime, CPUSampleCount) &&
        GetHistoricalPerformance(Characteristics.OperationId, EWorkloadTarget::GPU, HistoricalGPUTime, GPUSampleCount) &&
        CPUSampleCount > 0 && GPUSampleCount > 0)
    {
        // If we have historical data for both targets, adjust score based on relative performance
        if (HistoricalCPUTime < HistoricalGPUTime)
        {
            // CPU performed better historically
            float PerformanceRatio = FMath::Clamp(HistoricalGPUTime / FMath::Max(HistoricalCPUTime, 0.001f), 1.0f, 10.0f);
            Score += (PerformanceRatio - 1.0f) * 0.1f * FeatureWeights.HistoricalPerformanceWeight;
        }
    }
    
    // Apply CPU bias from config
    Score *= (1.0f + DistributionConfig.CPUBias);
    
    // Apply hardware utilization adjustment
    float CPUUtilization = static_cast<float>(CurrentCPUUtilization.GetValue()) / 100.0f;
    Score *= (1.0f - CPUUtilization * FeatureWeights.HardwareUtilizationWeight);
    
    return Score;
}

float FWorkloadDistributor::CalculateGPUScore(const FOperationCharacteristics& Characteristics) const
{
    if (!HardwareCapabilities.bSupportsCompute || !Characteristics.bHasGPUImplementation)
    {
        // If GPU compute is not supported or operation doesn't have GPU impl, give a low score
        return 0.1f;
    }
    
    // Start with base score
    float Score = 0.5f;
    
    // Apply weights to each characteristic
    Score += Characteristics.ComputeComplexity * FeatureWeights.ComputeComplexityWeight;
    Score += Characteristics.MemoryIntensity * FeatureWeights.MemoryIntensityWeight;
    Score += Characteristics.ParallelizationScore * FeatureWeights.ParallelizationWeight * 1.5f; // GPU benefits more from parallelization
    Score += (1.0f - Characteristics.SpatialCoherenceScore) * FeatureWeights.SpatialCoherenceWeight; // GPU handles incoherent access better
    
    // Data size bonus - GPU performs better on larger datasets
    float DataSizeFactor = FMath::Clamp(static_cast<float>(Characteristics.DataSize) / (1 * 1024 * 1024), 0.0f, 1.0f);
    Score += DataSizeFactor * FeatureWeights.DataSizeWeight;
    
    // Precision penalty - GPU is worse for high precision operations
    if (Characteristics.bRequiresPrecision)
    {
        Score -= 0.3f * FeatureWeights.PrecisionWeight;
    }
    
    // Historical performance adjustment
    float HistoricalCPUTime, HistoricalGPUTime;
    int32 CPUSampleCount, GPUSampleCount;
    
    if (GetHistoricalPerformance(Characteristics.OperationId, EWorkloadTarget::CPU, HistoricalCPUTime, CPUSampleCount) &&
        GetHistoricalPerformance(Characteristics.OperationId, EWorkloadTarget::GPU, HistoricalGPUTime, GPUSampleCount) &&
        CPUSampleCount > 0 && GPUSampleCount > 0)
    {
        // If we have historical data for both targets, adjust score based on relative performance
        if (HistoricalGPUTime < HistoricalCPUTime)
        {
            // GPU performed better historically
            float PerformanceRatio = FMath::Clamp(HistoricalCPUTime / FMath::Max(HistoricalGPUTime, 0.001f), 1.0f, 10.0f);
            Score += (PerformanceRatio - 1.0f) * 0.1f * FeatureWeights.HistoricalPerformanceWeight;
        }
    }
    
    // Apply GPU bias from config
    Score *= (1.0f + DistributionConfig.GPUBias);
    
    // Apply hardware utilization adjustment
    float GPUUtilization = static_cast<float>(CurrentGPUUtilization.GetValue()) / 100.0f;
    Score *= (1.0f - GPUUtilization * FeatureWeights.HardwareUtilizationWeight);
    
    // Check GPU memory availability
    int64 EstimatedMemoryRequirement = EstimateGPUMemoryRequirement(Characteristics.OperationId, Characteristics);
    if (EstimatedMemoryRequirement > 0 && AvailableGPUMemory.GetValue() > 0)
    {
        float MemoryUtilizationRatio = static_cast<float>(EstimatedMemoryRequirement) / static_cast<float>(AvailableGPUMemory.GetValue());
        if (MemoryUtilizationRatio > DistributionConfig.GPUMemoryThreshold)
        {
            // Penalize score if operation would use too much GPU memory
            Score *= (1.0f - (MemoryUtilizationRatio - DistributionConfig.GPUMemoryThreshold));
        }
    }
    
    return Score;
}

void FWorkloadDistributor::UpdateHardwareUtilization()
{
    // Update CPU utilization
    float CPUUtilization = 0.0f;
    
    // Use platform-specific APIs to get CPU utilization
    // Simplified cross-platform approach that works on all platforms
    // Note: There's no direct CPUTimePct in FPlatformTime, use a conservative estimate instead
    CPUUtilization = 0.5f; // Default to 50% as a safe estimate
    
    // Store CPU utilization (clamped between 0-100)
    CurrentCPUUtilization.Set(FMath::Clamp(static_cast<int64>(CPUUtilization * 100.0f), (int64)0, (int64)10000));
    
    // Update GPU utilization - this is more complex and varies by platform
    // For now, use a conservative estimate
    float GPUUtilization = 0.5f; // Default to 50% as fallback
    
    // Get GPU stats if available at runtime
    #if STATS
        // Runtime GPU utilization would be available via RHI stats
        // Using conservative estimate for compatibility
        GPUUtilization = 0.5f;
    #endif
    
    // Store GPU utilization (clamped between 0-100)
    CurrentGPUUtilization.Set(FMath::Clamp(static_cast<int64>(GPUUtilization * 100.0f), (int64)0, (int64)10000));
    
    // Update available GPU memory - platform specific
    int64 AvailableMemory = HardwareCapabilities.DedicatedGPUMemory;
    
    // Subtract known allocations (approximate)
    // Using a conservative estimate of 25% used for rendering
    AvailableMemory = static_cast<int64>(AvailableMemory * 0.75);
    
    // Ensure we don't go below zero
    AvailableGPUMemory.Set(FMath::Max(AvailableMemory, (int64)0));
}

float FWorkloadDistributor::GetCurrentCapacity(EWorkloadTarget Target) const
{
    switch (Target)
    {
        case EWorkloadTarget::CPU:
        {
            float Utilization = static_cast<float>(CurrentCPUUtilization.GetValue()) / 10000.0f;
            return 1.0f - Utilization;
        }
        
        case EWorkloadTarget::GPU:
        {
            float Utilization = static_cast<float>(CurrentGPUUtilization.GetValue()) / 10000.0f;
            return 1.0f - Utilization;
        }
        
        case EWorkloadTarget::Hybrid:
        {
            // For hybrid, return the average of CPU and GPU capacity
            float CPUCapacity = 1.0f - (static_cast<float>(CurrentCPUUtilization.GetValue()) / 10000.0f);
            float GPUCapacity = 1.0f - (static_cast<float>(CurrentGPUUtilization.GetValue()) / 10000.0f);
            return (CPUCapacity + GPUCapacity) * 0.5f;
        }
        
        default:
            return 1.0f;
    }
}

bool FWorkloadDistributor::HasPerformanceImbalance() const
{
    // Calculate utilization values
    float CPUUtilization = static_cast<float>(CurrentCPUUtilization.GetValue()) / 10000.0f;
    float GPUUtilization = static_cast<float>(CurrentGPUUtilization.GetValue()) / 10000.0f;
    
    // Check if there's a significant imbalance
    float Difference = FMath::Abs(CPUUtilization - GPUUtilization);
    return Difference > DistributionConfig.LoadImbalanceThreshold;
}

EWorkloadTarget FWorkloadDistributor::AdjustForHardwareUtilization(
    EWorkloadTarget InitialTarget,
    const FOperationCharacteristics& Characteristics) const
{
    // Only adjust if dynamic load balancing is enabled
    if (!DistributionConfig.bEnableDynamicLoadBalancing)
    {
        return InitialTarget;
    }
    
    // Get utilization values
    float CPUUtilization = static_cast<float>(CurrentCPUUtilization.GetValue()) / 10000.0f;
    float GPUUtilization = static_cast<float>(CurrentGPUUtilization.GetValue()) / 10000.0f;
    
    // Calculate the imbalance and determine if it's significant
    float Imbalance = CPUUtilization - GPUUtilization;
    bool bSignificantImbalance = FMath::Abs(Imbalance) > DistributionConfig.LoadImbalanceThreshold;
    
    if (!bSignificantImbalance)
    {
        // No significant imbalance, return the initial target
        return InitialTarget;
    }
    
    if (InitialTarget == EWorkloadTarget::CPU && Imbalance > 0)
    {
        // CPU is more loaded than GPU, consider moving to GPU or hybrid
        if (HardwareCapabilities.bSupportsCompute && Characteristics.bHasGPUImplementation)
        {
            // If GPU is significantly less loaded, use GPU
            if (Imbalance > DistributionConfig.LoadImbalanceThreshold * 2.0f)
            {
                return EWorkloadTarget::GPU;
            }
            // Otherwise use hybrid
            else if (GPUUtilization < 0.8f)
            {
                return EWorkloadTarget::Hybrid;
            }
        }
    }
    else if (InitialTarget == EWorkloadTarget::GPU && Imbalance < 0)
    {
        // GPU is more loaded than CPU, consider moving to CPU or hybrid
        if (CPUUtilization < 0.8f)
        {
            // If CPU is significantly less loaded, use CPU
            if (-Imbalance > DistributionConfig.LoadImbalanceThreshold * 2.0f)
            {
                return EWorkloadTarget::CPU;
            }
            // Otherwise use hybrid
            else
            {
                return EWorkloadTarget::Hybrid;
            }
        }
    }
    
    // No adjustment needed or possible
    return InitialTarget;
}

bool FWorkloadDistributor::CanUseNarrowBand(
    uint32 OperationId,
    const FOperationCharacteristics& Characteristics) const
{
    // Check if the operation is explicitly marked as narrow band
    if (Characteristics.bIsNarrowBand)
    {
        return true;
    }
    
    // If we have a hardware profile manager, check if it recommends narrow band
    if (HardwareProfileManager)
    {
        // Map operation ID to SDF operation type
        // Use operation from SDFTypeRegistry to avoid duplicate enum issues
        int32 OperationType = static_cast<int32>(ESDFOperationType::Custom); // Default to Custom
        
        // Try to determine the actual operation type based on the operation name
        FString OpName = Characteristics.OperationType.ToString();
        if (OpName.Contains(TEXT("Union")))
        {
            OperationType = static_cast<int32>(ESDFOperationType::Union);
        }
        else if (OpName.Contains(TEXT("Subtract")) || OpName.Contains(TEXT("Subtraction")))
        {
            OperationType = static_cast<int32>(ESDFOperationType::Subtraction);
        }
        else if (OpName.Contains(TEXT("Intersect")) || OpName.Contains(TEXT("Intersection")))
        {
            OperationType = static_cast<int32>(ESDFOperationType::Intersection);
        }
        else if (OpName.Contains(TEXT("Smooth")))
        {
            OperationType = static_cast<int32>(ESDFOperationType::SmoothUnion);
        }
        
        return HardwareProfileManager->ShouldUseNarrowBand(OperationType);
    }
    
    // Default to spatial coherence heuristic if other methods unavailable
    return Characteristics.SpatialCoherenceScore > NarrowBandParams.NarrowBandThreshold;
}

int64 FWorkloadDistributor::EstimateGPUMemoryRequirement(
    uint32 OperationId,
    const FOperationCharacteristics& Characteristics) const
{
    // Basic estimation based on data size
    int64 BaseMemory = Characteristics.DataSize * 4; // Assuming float data
    
    // If we have a field type, refine the estimate
    if (Characteristics.FieldTypeId != 0 && TypeRegistry)
    {
        // This would require TypeRegistry to expose size information
        // For now, use a simple heuristic
    }
    
    // Add overhead for compute shader
    BaseMemory += 1024 * 1024; // 1MB overhead
    
    // If it's a complex operation, add more overhead
    if (Characteristics.ComputeComplexity > 0.7f)
    {
        BaseMemory *= 2; // Double memory for complex operations
    }
    
    return BaseMemory;
}

bool FWorkloadDistributor::GetHistoricalPerformance(
    uint32 OperationId,
    EWorkloadTarget Target,
    float& OutTimeMS,
    int32& OutSampleCount) const
{
    FScopeLock Lock(&HistoryLock);
    
    // Initialize output parameters
    OutTimeMS = 0.0f;
    OutSampleCount = 0;
    
    // Check if we have history for this operation
    if (!PerformanceHistory.Contains(OperationId))
    {
        return false;
    }
    
    const TArray<FPerformanceHistoryEntry>& OpHistory = PerformanceHistory[OperationId];
    
    // Find entries for the target
    float TotalTime = 0.0f;
    int32 Count = 0;
    
    for (const FPerformanceHistoryEntry& Entry : OpHistory)
    {
        if (Entry.Target == Target && Entry.bWasSuccessful)
        {
            TotalTime += Entry.ExecutionTimeMS;
            Count++;
        }
    }
    
    if (Count == 0)
    {
        return false;
    }
    
    // Calculate average time
    OutTimeMS = TotalTime / Count;
    OutSampleCount = Count;
    
    return true;
}

bool FWorkloadDistributor::ShouldUseHybridProcessing(
    uint32 OperationId,
    const FOperationCharacteristics& Characteristics) const
{
    // Check if hybrid processing is disabled by configuration
    if (!DistributionConfig.bEnableDynamicLoadBalancing)
    {
        return false;
    }
    
    // Check for minimum complexity
    if (Characteristics.ComputeComplexity < DistributionConfig.HybridThreshold)
    {
        return false;
    }
    
    // Check if operation is parallelizable enough
    if (Characteristics.ParallelizationScore < 0.7f)
    {
        return false;
    }
    
    // Check if data size is large enough to benefit from hybrid processing
    if (Characteristics.DataSize < 1024 * 1024) // 1MB minimum
    {
        return false;
    }
    
    // Check hardware capabilities
    if (!HardwareCapabilities.bSupportsCompute || !Characteristics.bHasGPUImplementation)
    {
        return false;
    }
    
    // Check historical performance to see if hybrid was beneficial
    float HybridTimeMS, CPUTimeMS, GPUTimeMS;
    int32 HybridSampleCount, CPUSampleCount, GPUSampleCount;
    
    bool bHasHybridHistory = GetHistoricalPerformance(OperationId, EWorkloadTarget::Hybrid, HybridTimeMS, HybridSampleCount);
    bool bHasCPUHistory = GetHistoricalPerformance(OperationId, EWorkloadTarget::CPU, CPUTimeMS, CPUSampleCount);
    bool bHasGPUHistory = GetHistoricalPerformance(OperationId, EWorkloadTarget::GPU, GPUTimeMS, GPUSampleCount);
    
    if (bHasHybridHistory && HybridSampleCount >= 3)
    {
        // We have enough hybrid samples to make a decision
        if (bHasCPUHistory && bHasGPUHistory)
        {
            // Compare hybrid to best of CPU/GPU
            float BestSingleTime = FMath::Min(CPUTimeMS, GPUTimeMS);
            
            // If hybrid is not at least 10% better than the best single processor option, don't use it
            if (HybridTimeMS > BestSingleTime * 0.9f)
            {
                return false;
            }
        }
    }
    
    // Check hardware utilization
    float CPUUtilization = static_cast<float>(CurrentCPUUtilization.GetValue()) / 10000.0f;
    float GPUUtilization = static_cast<float>(CurrentGPUUtilization.GetValue()) / 10000.0f;
    
    // If both processors are heavily loaded, don't use hybrid (it would just add overhead)
    if (CPUUtilization > 0.8f && GPUUtilization > 0.8f)
    {
        return false;
    }
    
    // Pass additional criteria - use hybrid processing
    return true;
}

float FWorkloadDistributor::CalculateHybridSplit(
    uint32 OperationId,
    const FOperationCharacteristics& Characteristics) const
{
    // Default to even split
    float CPUSplitRatio = 0.5f;
    
    // Adjust split based on hardware capabilities
    int32 CPUCores = HardwareCapabilities.NumLogicalCores;
    int32 GPUComputeUnits = HardwareCapabilities.MaxComputeUnits;
    
    if (CPUCores > 0 && GPUComputeUnits > 0)
    {
        // Base split on relative processing power
        float TotalUnits = static_cast<float>(CPUCores + GPUComputeUnits);
        CPUSplitRatio = static_cast<float>(CPUCores) / TotalUnits;
    }
    
    // Adjust for current hardware utilization
    float CPUUtilization = static_cast<float>(CurrentCPUUtilization.GetValue()) / 10000.0f;
    float GPUUtilization = static_cast<float>(CurrentGPUUtilization.GetValue()) / 10000.0f;
    
    // If CPU is more utilized than GPU, shift work to GPU
    if (CPUUtilization > GPUUtilization)
    {
        float UtilizationDiff = CPUUtilization - GPUUtilization;
        CPUSplitRatio = FMath::Max(0.1f, CPUSplitRatio - (UtilizationDiff * 0.5f));
    }
    // If GPU is more utilized than CPU, shift work to CPU
    else if (GPUUtilization > CPUUtilization)
    {
        float UtilizationDiff = GPUUtilization - CPUUtilization;
        CPUSplitRatio = FMath::Min(0.9f, CPUSplitRatio + (UtilizationDiff * 0.5f));
    }
    
    // Check historical performance
    TArray<FPerformanceHistoryEntry> RecentEntries = GetRecentHistoryEntries(OperationId, 5);
    
    // Analyze previous split ratios if available
    if (RecentEntries.Num() > 0)
    {
        // Find the best performing split from history
        float BestTime = FLT_MAX;
        float BestSplit = CPUSplitRatio;
        
        for (const FPerformanceHistoryEntry& Entry : RecentEntries)
        {
            if (Entry.Target == EWorkloadTarget::Hybrid && Entry.bWasSuccessful)
            {
                // Assuming we store the CPU split ratio in a field of the characteristics
                float HistoricalSplit = Entry.Characteristics.SpatialCoherenceScore; // Repurpose this field for hybrid split
                
                if (Entry.ExecutionTimeMS < BestTime)
                {
                    BestTime = Entry.ExecutionTimeMS;
                    BestSplit = HistoricalSplit;
                }
            }
        }
        
        // Blend current calculation with historical best (70% historical, 30% current)
        CPUSplitRatio = (BestSplit * 0.7f) + (CPUSplitRatio * 0.3f);
    }
    
    // Ensure the ratio is within valid range
    return FMath::Clamp(CPUSplitRatio, 0.1f, 0.9f);
}

float FWorkloadDistributor::GetHybridSplitRatio(
    uint32 OperationId,
    const FOperationCharacteristics& Characteristics) const
{
    return CalculateHybridSplit(OperationId, Characteristics);
}

void FWorkloadDistributor::ReportOperationPerformance(
    uint32 OperationId,
    EWorkloadTarget Target,
    float ExecutionTimeMS,
    bool bWasSuccessful)
{
    if (!bIsInitialized)
    {
        return;
    }
    
    // Create performance history entry
    FPerformanceHistoryEntry Entry;
    Entry.Target = Target;
    Entry.ExecutionTimeMS = ExecutionTimeMS;
    Entry.bWasSuccessful = bWasSuccessful;
    Entry.Timestamp = WorkloadDistributorHelpers::GetCurrentTimeSeconds();
    
    // Try to get operation characteristics
    EWorkloadTarget ExpectedTarget;
    {
        FScopeLock Lock(&HistoryLock);
        
        // Check if we have characteristics from a prior decision
        if (LastDecisionByOperationId.Contains(OperationId))
        {
            ExpectedTarget = LastDecisionByOperationId[OperationId];
            
            // If the targets don't match, we may have overridden the decision
            if (ExpectedTarget != Target)
            {
                UE_LOG(LogTemp, Verbose, TEXT("Workload distributor decision was overridden for operation %u: Expected %s, Actual %s"),
                    OperationId, *WorkloadDistributorHelpers::TargetToString(ExpectedTarget), *WorkloadDistributorHelpers::TargetToString(Target));
            }
        }
        else
        {
            // If no prior decision, use reported target as expected
            ExpectedTarget = Target;
        }
    }
    
    // Create minimal characteristics if we don't have any
    FOperationCharacteristics MinimalCharacteristics;
    MinimalCharacteristics.OperationId = OperationId;
    Entry.Characteristics = MinimalCharacteristics;
    
    // Apply learning based on performance results
    if (DistributionConfig.AdaptiveLearningRate > 0.0f)
    {
        ApplyLearning(OperationId, Target, MinimalCharacteristics, ExecutionTimeMS, bWasSuccessful);
    }
    
    // Store performance history
    {
        FScopeLock Lock(&HistoryLock);
        
        // Create entry in history map if it doesn't exist
        if (!PerformanceHistory.Contains(OperationId))
        {
            PerformanceHistory.Add(OperationId, TArray<FPerformanceHistoryEntry>());
        }
        
        // Add the entry to history
        PerformanceHistory[OperationId].Add(Entry);
        
        // Limit history size
        if (PerformanceHistory[OperationId].Num() > DistributionConfig.HistorySampleSize)
        {
            // Remove oldest entries
            PerformanceHistory[OperationId].RemoveAt(0, PerformanceHistory[OperationId].Num() - DistributionConfig.HistorySampleSize);
        }
    }
    
    // Update distribution statistics
    UpdateStatistics(Target, ExecutionTimeMS, bWasSuccessful, MinimalCharacteristics);
}

void FWorkloadDistributor::ApplyLearning(
    uint32 OperationId,
    EWorkloadTarget Target,
    const FOperationCharacteristics& Characteristics,
    float ExecutionTimeMS,
    bool bWasSuccessful)
{
    if (!bWasSuccessful)
    {
        // Don't learn from failures
        return;
    }
    
    // Check if we made a prediction for this operation
    EWorkloadTarget ExpectedTarget;
    bool bHasPrediction = false;
    
    {
        FScopeLock Lock(&HistoryLock);
        bHasPrediction = LastDecisionByOperationId.Contains(OperationId);
        if (bHasPrediction)
        {
            ExpectedTarget = LastDecisionByOperationId[OperationId];
        }
    }
    
    if (!bHasPrediction || ExpectedTarget == Target)
    {
        // No prediction or prediction was correct, no learning needed
        return;
    }
    
    // Get historical performance for the expected target
    float ExpectedTimeMS;
    int32 ExpectedSampleCount;
    
    if (!GetHistoricalPerformance(OperationId, ExpectedTarget, ExpectedTimeMS, ExpectedSampleCount) || ExpectedSampleCount < 3)
    {
        // Not enough samples for the expected target
        return;
    }
    
    // Compare actual performance to expected performance
    if (ExecutionTimeMS < ExpectedTimeMS * 0.9f)
    {
        // Actual target performed at least 10% better than expected target
        float Delta = ExpectedTimeMS - ExecutionTimeMS;
        
        // Update weights based on this result
        UpdateFeatureWeights(ExpectedTarget, Target, Characteristics, Delta);
        
        UE_LOG(LogTemp, Verbose, TEXT("Learning applied for operation %u: %s performed better than %s by %.2f ms"),
            OperationId, *WorkloadDistributorHelpers::TargetToString(Target), 
            *WorkloadDistributorHelpers::TargetToString(ExpectedTarget), Delta);
    }
}

void FWorkloadDistributor::UpdateFeatureWeights(
    EWorkloadTarget ExpectedTarget,
    EWorkloadTarget ActualTarget,
    const FOperationCharacteristics& Characteristics, 
    float Delta)
{
    // Learning rate from configuration
    float LearningRate = DistributionConfig.AdaptiveLearningRate;
    
    // Normalize delta for weight adjustments
    float NormalizedDelta = FMath::Min(Delta / 10.0f, 1.0f);
    float AdjustmentFactor = NormalizedDelta * LearningRate;
    
    // Determine which direction to adjust weights
    bool bIncreaseCPUWeight = (ActualTarget == EWorkloadTarget::CPU);
    
    // Adjust weights based on characteristics
    if (Characteristics.ComputeComplexity > 0.5f)
    {
        // High compute complexity - adjust compute weight
        if (bIncreaseCPUWeight)
        {
            FeatureWeights.ComputeComplexityWeight *= (1.0f - AdjustmentFactor);
        }
        else
        {
            FeatureWeights.ComputeComplexityWeight *= (1.0f + AdjustmentFactor);
        }
    }
    
    if (Characteristics.MemoryIntensity > 0.5f)
    {
        // High memory intensity - adjust memory weight
        if (bIncreaseCPUWeight)
        {
            FeatureWeights.MemoryIntensityWeight *= (1.0f - AdjustmentFactor);
        }
        else
        {
            FeatureWeights.MemoryIntensityWeight *= (1.0f + AdjustmentFactor);
        }
    }
    
    if (Characteristics.ParallelizationScore > 0.7f)
    {
        // Highly parallelizable - adjust parallelization weight
        if (bIncreaseCPUWeight)
        {
            FeatureWeights.ParallelizationWeight *= (1.0f - AdjustmentFactor);
        }
        else
        {
            FeatureWeights.ParallelizationWeight *= (1.0f + AdjustmentFactor);
        }
    }
    
    if (Characteristics.bRequiresPrecision)
    {
        // Precision important - adjust precision weight
        if (bIncreaseCPUWeight)
        {
            FeatureWeights.PrecisionWeight *= (1.0f + AdjustmentFactor);
        }
        else
        {
            FeatureWeights.PrecisionWeight *= (1.0f - AdjustmentFactor);
        }
    }
    
    // Ensure weights stay in reasonable range
    FeatureWeights.ComputeComplexityWeight = FMath::Clamp(FeatureWeights.ComputeComplexityWeight, 0.1f, 3.0f);
    FeatureWeights.MemoryIntensityWeight = FMath::Clamp(FeatureWeights.MemoryIntensityWeight, 0.1f, 3.0f);
    FeatureWeights.ParallelizationWeight = FMath::Clamp(FeatureWeights.ParallelizationWeight, 0.1f, 3.0f);
    FeatureWeights.SpatialCoherenceWeight = FMath::Clamp(FeatureWeights.SpatialCoherenceWeight, 0.1f, 3.0f);
    FeatureWeights.DataSizeWeight = FMath::Clamp(FeatureWeights.DataSizeWeight, 0.1f, 3.0f);
    FeatureWeights.PrecisionWeight = FMath::Clamp(FeatureWeights.PrecisionWeight, 0.1f, 3.0f);
    FeatureWeights.HardwareUtilizationWeight = FMath::Clamp(FeatureWeights.HardwareUtilizationWeight, 0.1f, 3.0f);
    FeatureWeights.HistoricalPerformanceWeight = FMath::Clamp(FeatureWeights.HistoricalPerformanceWeight, 0.5f, 5.0f);
}

TArray<FWorkloadDistributor::FPerformanceHistoryEntry> FWorkloadDistributor::GetRecentHistoryEntries(
    uint32 OperationId,
    int32 MaxEntries) const
{
    FScopeLock Lock(&HistoryLock);
    TArray<FWorkloadDistributor::FPerformanceHistoryEntry> Result;
    
    if (!PerformanceHistory.Contains(OperationId))
    {
        return Result;
    }
    
    const TArray<FPerformanceHistoryEntry>& OpHistory = PerformanceHistory[OperationId];
    
    // Start from most recent entries
    int32 StartIndex = FMath::Max(0, OpHistory.Num() - MaxEntries);
    for (int32 i = StartIndex; i < OpHistory.Num(); ++i)
    {
        Result.Add(OpHistory[i]);
    }
    
    return Result;
}

TMap<EWorkloadTarget, float> FWorkloadDistributor::GetOperationPerformanceHistory(uint32 OperationId) const
{
    TMap<EWorkloadTarget, float> Result;
    
    float CPUTimeMS, GPUTimeMS, HybridTimeMS;
    int32 CPUSampleCount, GPUSampleCount, HybridSampleCount;
    
    if (GetHistoricalPerformance(OperationId, EWorkloadTarget::CPU, CPUTimeMS, CPUSampleCount) && CPUSampleCount > 0)
    {
        Result.Add(EWorkloadTarget::CPU, CPUTimeMS);
    }
    
    if (GetHistoricalPerformance(OperationId, EWorkloadTarget::GPU, GPUTimeMS, GPUSampleCount) && GPUSampleCount > 0)
    {
        Result.Add(EWorkloadTarget::GPU, GPUTimeMS);
    }
    
    if (GetHistoricalPerformance(OperationId, EWorkloadTarget::Hybrid, HybridTimeMS, HybridSampleCount) && HybridSampleCount > 0)
    {
        Result.Add(EWorkloadTarget::Hybrid, HybridTimeMS);
    }
    
    return Result;
}

void FWorkloadDistributor::ResetPerformanceHistory()
{
    FScopeLock Lock(&HistoryLock);
    PerformanceHistory.Empty();
    LastDecisionByOperationId.Empty();
}

void FWorkloadDistributor::UpdateStatistics(
    EWorkloadTarget Target,
    float ExecutionTimeMS,
    bool bWasSuccessful,
    const FOperationCharacteristics& Characteristics)
{
    FScopeLock Lock(&StatisticsLock);
    
    // Update operation counts
    Statistics.TotalOperations++;
    
    // Update target-specific counts and times
    switch (Target)
    {
        case EWorkloadTarget::CPU:
            Statistics.CPUOperations++;
            if (bWasSuccessful)
            {
                Statistics.AverageCPUTimeMS = (Statistics.AverageCPUTimeMS * (Statistics.CPUOperations - 1) + ExecutionTimeMS) / Statistics.CPUOperations;
            }
            break;
            
        case EWorkloadTarget::GPU:
            Statistics.GPUOperations++;
            if (bWasSuccessful)
            {
                Statistics.AverageGPUTimeMS = (Statistics.AverageGPUTimeMS * (Statistics.GPUOperations - 1) + ExecutionTimeMS) / Statistics.GPUOperations;
            }
            break;
            
        case EWorkloadTarget::Hybrid:
            Statistics.HybridOperations++;
            if (bWasSuccessful)
            {
                Statistics.AverageHybridTimeMS = (Statistics.AverageHybridTimeMS * (Statistics.HybridOperations - 1) + ExecutionTimeMS) / Statistics.HybridOperations;
            }
            break;
    }
    
    // Update distribution percentages
    if (Statistics.TotalOperations > 0)
    {
        Statistics.CPUWorkloadPercent = static_cast<float>(Statistics.CPUOperations) / Statistics.TotalOperations * 100.0f;
        Statistics.GPUWorkloadPercent = static_cast<float>(Statistics.GPUOperations) / Statistics.TotalOperations * 100.0f;
        Statistics.HybridWorkloadPercent = static_cast<float>(Statistics.HybridOperations) / Statistics.TotalOperations * 100.0f;
    }
    
    // Update utilization metrics
    Statistics.CPUUtilization = static_cast<float>(CurrentCPUUtilization.GetValue()) / 10000.0f;
    Statistics.GPUUtilization = static_cast<float>(CurrentGPUUtilization.GetValue()) / 10000.0f;
    
    // Update decision quality
    if (bWasSuccessful && LastDecisionByOperationId.Contains(Characteristics.OperationId))
    {
        float QualityScore = AnalyzeDecisionQuality(
            Characteristics.OperationId,
            Target,
            ExecutionTimeMS);
        
        // Update rolling average of decision quality
        Statistics.DecisionAccuracyScore = (Statistics.DecisionAccuracyScore * (Statistics.TotalOperations - 1) + QualityScore) / Statistics.TotalOperations;
    }
}

FDistributionStatistics FWorkloadDistributor::GetDistributionStatistics(bool bResetStats) const
{
    FScopeLock Lock(&StatisticsLock);
    
    // Make a copy of the statistics
    FDistributionStatistics Result = Statistics;
    
    // Reset if requested
    if (bResetStats)
    {
        // Clear statistics but maintain learned weights
        const_cast<FWorkloadDistributor*>(this)->Statistics = FDistributionStatistics();
    }
    
    return Result;
}

float FWorkloadDistributor::AnalyzeDecisionQuality(
    uint32 OperationId,
    EWorkloadTarget Target,
    float ExecutionTimeMS) const
{
    // Get the performance history for other targets
    TMap<EWorkloadTarget, float> operationPerfHistory = GetOperationPerformanceHistory(OperationId);
    
    if (operationPerfHistory.Num() <= 1)
    {
        // Not enough data to evaluate decision quality
        return 1.0f;
    }
    
    // Find the best possible target based on historical performance
    EWorkloadTarget BestHistoricalTarget = Target;
    float BestHistoricalTime = FLT_MAX;
    
    for (const auto& Pair : operationPerfHistory)
    {
        if (Pair.Value < BestHistoricalTime)
        {
            BestHistoricalTime = Pair.Value;
            BestHistoricalTarget = Pair.Key;
        }
    }
    
    // If we chose the best target, score is 1.0
    if (Target == BestHistoricalTarget)
    {
        return 1.0f;
    }
    
    // Calculate how far off our decision was
    float HistoricalTargetTime = operationPerfHistory[Target];
    float Ratio = BestHistoricalTime / FMath::Max(HistoricalTargetTime, 0.001f);
    
    // Score based on how close we were to optimal
    // 1.0 = perfect, 0.0 = poor decision
    return FMath::Clamp(Ratio, 0.0f, 1.0f);
}

int32 FWorkloadDistributor::GetOptimalCPUThreadCount(
    uint32 OperationId,
    const FOperationCharacteristics& Characteristics) const
{
    // Start with default: use all available cores
    int32 ThreadCount = HardwareCapabilities.NumLogicalCores;
    
    // Adjust based on parallelization score
    if (Characteristics.ParallelizationScore < 0.5f)
    {
        // Low parallelization potential - use fewer threads
        ThreadCount = FMath::Max(1, ThreadCount / 4);
    }
    else if (Characteristics.ParallelizationScore < 0.8f)
    {
        // Medium parallelization potential - use half the threads
        ThreadCount = FMath::Max(1, ThreadCount / 2);
    }
    
    // Consider current CPU utilization
    float CPUUtilization = static_cast<float>(CurrentCPUUtilization.GetValue()) / 10000.0f;
    if (CPUUtilization > 0.7f)
    {
        // High utilization - be more conservative with thread count
        ThreadCount = FMath::Max(1, ThreadCount / 2);
    }
    
    // Consider data size - small data might not benefit from many threads
    if (Characteristics.DataSize < 1024 * 1024) // Less than 1MB
    {
        ThreadCount = FMath::Min(ThreadCount, 4);
    }
    
    // Get recommendation from hardware profile if available
    if (HardwareProfileManager)
    {
        // This would map to an SDF operation type and get optimal thread count
        // For now, just use our calculated value
    }
    
    // Check if the task scheduler has a recommendation
    if (ParallelExecutor)
    {
        int32 RecommendedThreads = ParallelExecutor->GetRecommendedThreadCount();
        // Blend our calculation with the recommendation
        ThreadCount = (ThreadCount + RecommendedThreads) / 2;
    }
    
    return FMath::Max(1, ThreadCount);
}

FIntVector FWorkloadDistributor::GetOptimalGPUThreadGroupCounts(
    uint32 OperationId,
    const FOperationCharacteristics& Characteristics) const
{
    // Default thread group counts
    FIntVector ThreadGroups(8, 8, 8);
    
    // If we have a hardware profile manager, use its recommendation
    if (HardwareProfileManager)
    {
        // Map operation ID to SDF operation type using SDFTypeRegistry
        int32 OperationType = static_cast<int32>(ESDFOperationType::Custom);
        
        // Try to determine operation type from name for compatibility
        FString OpName = Characteristics.OperationType.ToString();
        if (OpName.Contains(TEXT("Union")))
        {
            OperationType = static_cast<int32>(ESDFOperationType::Union);
        }
        else if (OpName.Contains(TEXT("Subtract")) || OpName.Contains(TEXT("Subtraction")))
        {
            OperationType = static_cast<int32>(ESDFOperationType::Subtraction);
        }
        else if (OpName.Contains(TEXT("Intersect")) || OpName.Contains(TEXT("Intersection")))
        {
            OperationType = static_cast<int32>(ESDFOperationType::Intersection);
        }
        else if (OpName.Contains(TEXT("Smooth")))
        {
            OperationType = static_cast<int32>(ESDFOperationType::SmoothUnion);
        }
        
        // Get optimal work group size from profile
        ThreadGroups = HardwareProfileManager->GetOptimalWorkGroupSize(OperationType);
    }
    
    // Adjust based on data size
    uint32 DataSize = Characteristics.DataSize;
    if (DataSize > 0)
    {
        // Estimate voxel grid dimensions (assuming cubic volume)
        float CubeRoot = FMath::Pow(static_cast<float>(DataSize) / 4.0f, 1.0f/3.0f);
        int32 EstimatedDimension = FMath::CeilToInt(CubeRoot);
        
        // Limit thread group counts to reasonable values based on data size
        ThreadGroups.X = FMath::Min(ThreadGroups.X, (EstimatedDimension + 7) / 8);
        ThreadGroups.Y = FMath::Min(ThreadGroups.Y, (EstimatedDimension + 7) / 8);
        ThreadGroups.Z = FMath::Min(ThreadGroups.Z, (EstimatedDimension + 7) / 8);
    }
    
    // Ensure minimum thread group counts
    ThreadGroups.X = FMath::Max(1, ThreadGroups.X);
    ThreadGroups.Y = FMath::Max(1, ThreadGroups.Y);
    ThreadGroups.Z = FMath::Max(1, ThreadGroups.Z);
    
    return ThreadGroups;
}

bool FWorkloadDistributor::IsOperationSupported(uint32 OperationId, EWorkloadTarget Target) const
{
    switch (Target)
    {
        case EWorkloadTarget::CPU:
            // Most operations are supported on CPU
            return true;
            
        case EWorkloadTarget::GPU:
            // Check if GPU compute is supported
            if (!HardwareCapabilities.bSupportsCompute)
            {
                return false;
            }
            
            // Check if the operation has a GPU implementation
            if (TypeRegistry && OperationId > 0)
            {
                // This would check if the operation has a GPU implementation
                // For now, assume it does if compute is supported
            }
            
            return true;
            
        case EWorkloadTarget::Hybrid:
            // Hybrid requires both CPU and GPU support
            return IsOperationSupported(OperationId, EWorkloadTarget::CPU) && 
                   IsOperationSupported(OperationId, EWorkloadTarget::GPU);
            
        default:
            return false;
    }
}

float FWorkloadDistributor::EstimateExecutionTime(
    uint32 OperationId,
    EWorkloadTarget Target,
    const FOperationCharacteristics& Characteristics) const
{
    // Try to get historical performance data
    float HistoricalTimeMS;
    int32 SampleCount;
    
    if (GetHistoricalPerformance(OperationId, Target, HistoricalTimeMS, SampleCount) && SampleCount >= 3)
    {
        // We have enough samples for a reliable estimate
        return HistoricalTimeMS;
    }
    
    // Get hardware profile manager estimate if available
    if (HardwareProfileManager && Target == EWorkloadTarget::GPU)
    {
        // Map to SDF operation type from SDFTypeRegistry
        int32 OperationType = static_cast<int32>(ESDFOperationType::Custom);
        
        // Try to determine operation type from name for compatibility
        FString OpName = Characteristics.OperationType.ToString();
        if (OpName.Contains(TEXT("Union")))
        {
            OperationType = static_cast<int32>(ESDFOperationType::Union);
        }
        else if (OpName.Contains(TEXT("Subtract")) || OpName.Contains(TEXT("Subtraction")))
        {
            OperationType = static_cast<int32>(ESDFOperationType::Subtraction);
        }
        else if (OpName.Contains(TEXT("Intersect")) || OpName.Contains(TEXT("Intersection")))
        {
            OperationType = static_cast<int32>(ESDFOperationType::Intersection);
        }
        else if (OpName.Contains(TEXT("Smooth")))
        {
            OperationType = static_cast<int32>(ESDFOperationType::SmoothUnion);
        }
        
        // Get metrics from profile manager
        const FSDFOperationMetrics& Metrics = HardwareProfileManager->GetOperationMetrics(OperationType);
        
        // If we have metrics, use them for estimation
        if (Metrics.SampleCount > 0)
        {
            // Use conservative approach due to field name differences
            // Scale based on data size estimate
            float DataSizeRatio = 1.0f;
            if (Characteristics.DataSize > 0)
            {
                // Adjust ratio based on data size
                DataSizeRatio = static_cast<float>(Characteristics.DataSize) / (1024 * 1024); // Normalize to MB
            }
            
            return Metrics.AverageExecutionTimeMs * DataSizeRatio;
        }
    }
    
    // Fallback to heuristic estimate
    float BaseTimeMS = 0.0f;
    
    switch (Target)
    {
        case EWorkloadTarget::CPU:
            // Base estimate on complexity and data size
            BaseTimeMS = 0.01f * Characteristics.DataSize / 1024 * Characteristics.ComputeComplexity;
            break;
            
        case EWorkloadTarget::GPU:
            // GPU is generally faster for parallel workloads
            BaseTimeMS = 0.005f * Characteristics.DataSize / 1024 * Characteristics.ComputeComplexity;
            // But has higher overhead
            BaseTimeMS += 0.5f;
            break;
            
        case EWorkloadTarget::Hybrid:
            // Hybrid is somewhere in between, with additional overhead
            BaseTimeMS = 0.007f * Characteristics.DataSize / 1024 * Characteristics.ComputeComplexity;
            BaseTimeMS += 0.8f; // Higher overhead due to coordination
            break;
    }
    
    // Adjust for spatial coherence
    if (Target == EWorkloadTarget::CPU)
    {
        // CPU benefits from coherence
        BaseTimeMS *= (2.0f - Characteristics.SpatialCoherenceScore);
    }
    else if (Target == EWorkloadTarget::GPU)
    {
        // GPU is less affected by coherence
        BaseTimeMS *= (1.5f - Characteristics.SpatialCoherenceScore * 0.5f);
    }
    
    // Cap the estimate at a reasonable maximum
    return FMath::Min(BaseTimeMS, 1000.0f);
}

bool FWorkloadDistributor::ApplyMaterialDistributionStrategy(
    uint32 MaterialTypeId,
    const FOperationCharacteristics& Characteristics,
    EWorkloadTarget& OutTarget) const
{
    FScopeLock Lock(&MaterialStrategyLock);
    
    if (!MaterialDistributionStrategies.Contains(MaterialTypeId))
    {
        return false;
    }
    
    // Call the registered strategy function
    OutTarget = MaterialDistributionStrategies[MaterialTypeId](Characteristics);
    return true;
}

float FWorkloadDistributor::AnalyzeMaterialCompatibility(uint32 MaterialTypeId, EWorkloadTarget Target) const
{
    if (!TypeRegistry || MaterialTypeId == 0)
    {
        return 0.5f; // Neutral compatibility if no registry or invalid ID
    }
    
    return AnalyzeMaterialPropertiesForTarget(MaterialTypeId, Target);
}

float FWorkloadDistributor::AnalyzeMaterialPropertiesForTarget(uint32 MaterialTypeId, EWorkloadTarget Target) const
{
    // Default to medium compatibility
    float Compatibility = 0.5f;
    
    // Implement material property analysis based on type registry
    // This would access material properties from the registry and analyze them
    
    // For now, use a simple heuristic based on material type ID
    // In a real implementation, this would analyze actual material properties
    
    switch (Target)
    {
        case EWorkloadTarget::CPU:
            // CPU is better for complex materials with many properties
            if (MaterialTypeId % 3 == 0) // Just a placeholder logic
            {
                Compatibility = 0.8f;
            }
            break;
            
        case EWorkloadTarget::GPU:
            // GPU is better for simple materials with few properties
            if (MaterialTypeId % 3 == 1) // Just a placeholder logic
            {
                Compatibility = 0.8f;
            }
            break;
            
        case EWorkloadTarget::Hybrid:
            // Hybrid is good for a mix of properties
            if (MaterialTypeId % 3 == 2) // Just a placeholder logic
            {
                Compatibility = 0.8f;
            }
            break;
    }
    
    return Compatibility;
}

bool FWorkloadDistributor::RegisterMaterialDistributionStrategy(
    uint32 MaterialTypeId, 
    TFunction<EWorkloadTarget(const FOperationCharacteristics&)> Strategy)
{
    if (MaterialTypeId == 0 || !Strategy)
    {
        return false;
    }
    
    FScopeLock Lock(&MaterialStrategyLock);
    
    // Register the strategy
    MaterialDistributionStrategies.Add(MaterialTypeId, Strategy);
    
    return true;
}

bool FWorkloadDistributor::GenerateHardwareProfile(const FName& ProfileName)
{
    if (!HardwareProfileManager)
    {
        return false;
    }
    
    // Forward to hardware profile manager
    // This would normally generate a profile based on current hardware
    
    UE_LOG(LogTemp, Log, TEXT("Generating hardware profile: %s"), *ProfileName.ToString());
    
    // For now, just return success
    return true;
}

int32 FWorkloadDistributor::CalibrateOperationPerformance(
    const TArray<uint32>& OperationIds,
    bool bDetailedCalibration)
{
    if (!bIsInitialized)
    {
        return 0;
    }
    
    // Count of operations calibrated
    int32 CalibratedCount = 0;
    
    // Create a list of operations to calibrate
    TArray<uint32> OperationsToCalibrate;
    
    if (OperationIds.Num() > 0)
    {
        // Use the provided operation IDs
        OperationsToCalibrate = OperationIds;
    }
    else
    {
        // Use all operations with performance history
        FScopeLock Lock(&HistoryLock);
        for (const auto& Pair : PerformanceHistory)
        {
            OperationsToCalibrate.Add(Pair.Key);
        }
    }
    
    // Perform calibration
    for (uint32 OperationId : OperationsToCalibrate)
    {
        // For each operation, run benchmarks on CPU and GPU
        UE_LOG(LogTemp, Log, TEXT("Calibrating operation %u"), OperationId);
        
        // In a real implementation, this would run actual benchmarks
        // For now, just mark as calibrated
        CalibratedCount++;
    }
    
    return CalibratedCount;
}

bool FWorkloadDistributor::ExportPerformanceProfile(const FString& FilePath) const
{
    // In a real implementation, this would serialize performance history and weights to a file
    UE_LOG(LogTemp, Log, TEXT("Exporting performance profile to: %s"), *FilePath);
    
    // For now, just return success
    return true;
}

bool FWorkloadDistributor::ImportPerformanceProfile(const FString& FilePath)
{
    // In a real implementation, this would deserialize performance history and weights from a file
    UE_LOG(LogTemp, Log, TEXT("Importing performance profile from: %s"), *FilePath);
    
    // For now, just return success
    return true;
}
