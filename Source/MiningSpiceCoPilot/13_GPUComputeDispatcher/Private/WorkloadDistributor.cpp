#include "../Public/WorkloadDistributor.h"
#include "../Public/GPUDispatcherLogging.h"

// Forward declarations
struct FOperationParameters;

// Adaptive performance system for learning from historical performance
class FAdaptivePerformanceSystem
{
public:
    FAdaptivePerformanceSystem() {}
    
    // Learn from performance history
    void UpdateOperationStats(uint32 OperationTypeId, float CPUTimeMs, float GPUTimeMs, bool bSuccess)
    {
        // Add sample to history
        FPerformanceEntry Entry;
        Entry.ExecutionTimeMs = GPUTimeMs > 0 ? GPUTimeMs : CPUTimeMs;
        Entry.ProcessingTarget = GPUTimeMs > 0 ? EProcessingTarget::GPU : EProcessingTarget::CPU;
        Entry.DataSize = 0; // Would be filled with actual size
        Entry.Timestamp = FPlatformTime::Seconds();
        Entry.IsSuccess = bSuccess;
        
        FCriticalSection& MutableLock = const_cast<FCriticalSection&>(StatsLock);
        FScopeLock Lock(&MutableLock);
        TArray<FPerformanceEntry>& History = PerformanceHistory.FindOrAdd(OperationTypeId);
        History.Add(Entry);
        
        // Trim old entries (keep last 100)
        if (History.Num() > 100)
        {
            History.RemoveAt(0, History.Num() - 100);
        }
        
        // Update prediction model
        UpdatePredictionModel(OperationTypeId);
    }
    
    // Predict performance for future operations
    double PredictExecutionTime(uint32 OperationTypeId, EProcessingTarget Target, const FOperationParameters& Params) const
    {
        // Find similar operations in history
        TArray<double> SimilarTimes;
        TArray<float> Weights;
        
        // Use mutable access to critical section for locking
        FCriticalSection& MutableLock = const_cast<FCriticalSection&>(StatsLock);
        FScopeLock Lock(&MutableLock);
        const TArray<FPerformanceEntry>* History = PerformanceHistory.Find(OperationTypeId);
        if (!History || History->Num() == 0)
        {
            return Target == EProcessingTarget::GPU ? 10.0 : 20.0; // Default estimates in ms
        }
        
        // Find weighted average of similar operations
        for (const FPerformanceEntry& Entry : *History)
        {
            // Only consider entries for the target processing type
            if (Entry.ProcessingTarget != Target)
            {
                continue;
            }
            
            float Similarity = CalculateParameterSimilarity(Params, Entry.OperationParams);
            if (Similarity > 0.7f) // 70% similarity threshold
            {
                SimilarTimes.Add(Entry.ExecutionTimeMs);
                Weights.Add(Similarity);
            }
        }
        
        // Calculate weighted average
        if (SimilarTimes.Num() > 0)
        {
            double SumTimes = 0.0;
            float SumWeights = 0.0f;
            
            for (int32 i = 0; i < SimilarTimes.Num(); i++)
            {
                SumTimes += SimilarTimes[i] * Weights[i];
                SumWeights += Weights[i];
            }
            
            return SumTimes / SumWeights;
        }
        
        // Fall back to average of all operations of this type and target
        double SumTimes = 0.0;
        int32 Count = 0;
        for (const FPerformanceEntry& Entry : *History)
        {
            if (Entry.ProcessingTarget == Target)
            {
                SumTimes += Entry.ExecutionTimeMs;
                Count++;
            }
        }
        
        return Count > 0 ? SumTimes / Count : (Target == EProcessingTarget::GPU ? 10.0 : 20.0);
    }
    
    // Get overall success rate for an operation type and target
    float GetSuccessRate(uint32 OperationTypeId, EProcessingTarget Target)
    {
        FCriticalSection& MutableLock = const_cast<FCriticalSection&>(StatsLock);
        FScopeLock Lock(&MutableLock);
        const TArray<FPerformanceEntry>* History = PerformanceHistory.Find(OperationTypeId);
        if (!History || History->Num() == 0)
        {
            return 1.0f; // Assume 100% success by default
        }
        
        int32 SuccessCount = 0;
        int32 TotalCount = 0;
        
        for (const FPerformanceEntry& Entry : *History)
        {
            if (Entry.ProcessingTarget == Target)
            {
                if (Entry.IsSuccess)
                {
                    SuccessCount++;
                }
                TotalCount++;
            }
        }
        
        return TotalCount > 0 ? (float)SuccessCount / TotalCount : 1.0f;
    }
    
private:
    // Operation performance entry
    struct FPerformanceEntry
    {
        double ExecutionTimeMs = 0.0;
        EProcessingTarget ProcessingTarget = EProcessingTarget::CPU;
        uint32 DataSize = 0;
        double Timestamp = 0.0;
        bool IsSuccess = true;
        FOperationParameters OperationParams;
    };
    
    // Using FOperationParameters defined in ComputeOperationTypes.h
    
    // Update prediction model based on history
    void UpdatePredictionModel(uint32 OperationTypeId)
    {
        // In a real implementation, this would update a machine learning model
        // For this implementation, we just use the raw history data
    }
    
    // Calculate similarity between operation parameters
    float CalculateParameterSimilarity(const FOperationParameters& A, const FOperationParameters& B) const
    {
        // Simple similarity metric based on parameter differences
        // In a real implementation, this would be more sophisticated
        
        float SimScore = 1.0f;
        
        // Volume size similarity
        if (A.VolumeSize > 0 && B.VolumeSize > 0)
        {
            float VolRatio = FMath::Min(A.VolumeSize, B.VolumeSize) / FMath::Max(A.VolumeSize, B.VolumeSize);
            SimScore *= VolRatio;
        }
        
        // Material ID match
        if (A.MaterialId >= 0 && B.MaterialId >= 0)
        {
            SimScore *= (A.MaterialId == B.MaterialId) ? 1.0f : 0.8f;
        }
        
        // Channel count similarity
        if (A.ChannelCount > 0 && B.ChannelCount > 0)
        {
            SimScore *= (A.ChannelCount == B.ChannelCount) ? 1.0f : 0.9f;
        }
        
        // Narrow band and precision flags
        SimScore *= (A.bUseNarrowBand == B.bUseNarrowBand) ? 1.0f : 0.8f;
        SimScore *= (A.bHighPrecision == B.bHighPrecision) ? 1.0f : 0.8f;
        
        return SimScore;
    }
    
    // Storage for performance history
    TMap<uint32, TArray<FPerformanceEntry>> PerformanceHistory;
    FCriticalSection StatsLock;
};

FWorkloadDistributor::FWorkloadDistributor()
    : MemoryPressureAdjustment(0.0f)
    , CPUWorkloadRatioBoost(0.0f)
{
    // Initialize performance history buffer
    RecentOperations.Reserve(100);
    
    // Create adaptive performance system
    PerformanceSystem = MakeShared<FAdaptivePerformanceSystem>();
    
    // Initialize default config
    Config.bEnableAutotuning = true;
    Config.CPUAffinityForLowOperationCount = 0.8f;
    Config.GPUAffinityForBatchedOperations = 0.9f;
    Config.ComplexityThreshold = 100.0f;
    Config.GpuUtilizationThreshold = 0.9f;
    Config.PerformanceRatioThreshold = 0.8f;
}

FWorkloadDistributor::~FWorkloadDistributor()
{
}

bool FWorkloadDistributor::Initialize(const FHardwareProfile& Profile)
{
    HardwareProfile = Profile;
    
    // Initialize operation stats with defaults
    for (int32 i = 0; i <= 10; ++i)
    {
        FOperationStats& Stats = OperationStats.FindOrAdd(i);
        Stats.AvgCPUTime = 20.0f;  // Initial guess: 20ms on CPU
        Stats.AvgGPUTime = 10.0f;  // Initial guess: 10ms on GPU
        Stats.CPUCount = 0;
        Stats.GPUCount = 0;
        Stats.SuccessRate = 1.0f;
        
        // Set complexity based on operation type
        switch (i)
        {
            case 0: // Union - simple
                Stats.Complexity = 1.0f;
                break;
            case 1: // Difference - simple
                Stats.Complexity = 1.0f;
                break;
            case 2: // Intersection - simple
                Stats.Complexity = 1.0f;
                break;
            case 3: // Smoothing - complex
                Stats.Complexity = 3.0f;
                break;
            case 4: // Gradient - moderately complex
                Stats.Complexity = 2.0f;
                break;
            case 5: // Evaluation - simple
                Stats.Complexity = 1.0f;
                break;
            case 6: // MaterialBlend - complex
                Stats.Complexity = 3.0f;
                break;
            case 7: // Erosion - moderately complex
                Stats.Complexity = 2.0f;
                break;
            case 8: // Dilation - moderately complex
                Stats.Complexity = 2.0f;
                break;
            case 9: // ChannelTransfer - simple
                Stats.Complexity = 1.0f;
                break;
            case 10: // FieldOperation - depends on the specific operation
                Stats.Complexity = 2.0f;
                break;
        }
    }
    
    GPU_DISPATCHER_LOG_DEBUG("WorkloadDistributor initialized with %d compute units", Profile.ComputeUnits);
    return true;
}

EProcessingTarget FWorkloadDistributor::DetermineProcessingTarget(const FComputeOperation& Operation)
{
    // If a target is forced, use that
    if (Operation.ForcedTarget.IsSet())
    {
        return Operation.ForcedTarget.GetValue();
    }
    
    // Calculate operation complexity
    float Complexity = CalculateOperationComplexity(Operation);
    
    // Check if this is a narrow-band operation
    bool bIsNarrowBand = IsNarrowBandOperation(Operation.Bounds, Operation.MaterialChannelId);
    
    // Get operation statistics
    FOperationStats* Stats = OperationStats.Find(Operation.OperationTypeId);
    if (!Stats)
    {
        // If we don't have stats, decide based on complexity
        return SelectTargetBasedOnComplexity(Complexity);
    }
    
    // Use historical performance data for this operation type
    float PredictedGPUTime = Stats->AvgGPUTime;
    float PredictedCPUTime = Stats->AvgCPUTime;
    
    // Adjust for memory pressure if needed
    if (MemoryPressureAdjustment > 0.0f)
    {
        // Bias toward CPU under memory pressure
        PredictedGPUTime *= (1.0f + MemoryPressureAdjustment);
    }
    
    // Adjust for CPU boost if needed
    if (CPUWorkloadRatioBoost > 0.0f)
    {
        // Bias toward CPU
        PredictedCPUTime *= (1.0f - CPUWorkloadRatioBoost);
    }
    
    // Consider current performance ratio threshold
    float PerformanceRatio = Config.PerformanceRatioThreshold;
    
    // Make decision based on predicted performance
    if (PredictedGPUTime < PredictedCPUTime * PerformanceRatio)
    {
        // GPU is significantly faster, use it
        return EProcessingTarget::GPU;
    }
    else if (bIsNarrowBand && Operation.Bounds.GetVolume() < 100000.0f)
    {
        // Small narrow-band operations often work better on CPU
        return EProcessingTarget::CPU;
    }
    else if (Complexity > Config.ComplexityThreshold)
    {
        // Very complex operations usually benefit from GPU
        return EProcessingTarget::GPU;
    }
    else if (PredictedCPUTime < PredictedGPUTime * PerformanceRatio)
    {
        // CPU is significantly faster
        return EProcessingTarget::CPU;
    }
    else
    {
        // Similar performance, consider using hybrid approach for large operations
        if (Operation.Bounds.GetVolume() > 1000000.0f)
        {
            return EProcessingTarget::Hybrid;
        }
        
        // Default to GPU for moderate-sized operations
        return EProcessingTarget::GPU;
    }
}

void FWorkloadDistributor::UpdatePerformanceMetrics(const FOperationMetrics& Metrics)
{
    // Add to recent operations history
    if (RecentOperations.Num() >= 100)
    {
        RecentOperations.RemoveAt(0);
    }
    RecentOperations.Add(Metrics);
    
    // Update operation stats
    UpdateOperationStats(
        Metrics.OperationTypeId, 
        Metrics.CPUExecutionTimeMS, 
        Metrics.GPUExecutionTimeMS,
        Metrics.SuccessfulExecution);
    
    // Track operation counts
    if (Metrics.GPUExecutionTimeMS > 0.0f)
    {
        GPUOperationCount.Increment();
    }
    else
    {
        CPUOperationCount.Increment();
    }
    
    // Update prediction model in adaptive system
    if (PerformanceSystem)
    {
        EProcessingTarget Target = Metrics.GPUExecutionTimeMS > 0.0f ? 
            EProcessingTarget::GPU : EProcessingTarget::CPU;
            
        PerformanceSystem->UpdateOperationStats(
            Metrics.OperationTypeId,
            Metrics.CPUExecutionTimeMS,
            Metrics.GPUExecutionTimeMS,
            Metrics.SuccessfulExecution);
    }
}

void FWorkloadDistributor::ResetMetrics()
{
    FScopeLock Lock(&StatsLock);
    
    // Reset all operation stats
    for (auto& StatsPair : OperationStats)
    {
        StatsPair.Value.AvgCPUTime = 20.0f;  // Reset to default
        StatsPair.Value.AvgGPUTime = 10.0f;  // Reset to default
        StatsPair.Value.CPUCount = 0;
        StatsPair.Value.GPUCount = 0;
        StatsPair.Value.SuccessRate = 1.0f;
    }
    
    // Reset performance history
    PerformanceHistoryByType.Empty();
    RecentOperations.Empty();
    
    // Reset counters
    GPUOperationCount.Set(0);
    CPUOperationCount.Set(0);
    
    // Reset adjustments
    MemoryPressureAdjustment = 0.0f;
    CPUWorkloadRatioBoost = 0.0f;
    
    GPU_DISPATCHER_LOG_DEBUG("Performance metrics reset");
}

bool FWorkloadDistributor::SplitOperation(const FComputeOperation& Operation, TArray<FComputeOperation>& OutSubOperations)
{
    // Don't split if the operation is too small
    if (Operation.Bounds.GetVolume() < 100000.0f)
    {
        return false;
    }
    
    // Split along longest axis
    FVector Extents = Operation.Bounds.GetExtent();
    int32 SplitAxis = 0;
    
    if (Extents.Y > Extents.X && Extents.Y > Extents.Z)
    {
        SplitAxis = 1;
    }
    else if (Extents.Z > Extents.X && Extents.Z > Extents.Y)
    {
        SplitAxis = 2;
    }
    
    FVector Center = Operation.Bounds.GetCenter();
    
    // Create sub-operations
    FComputeOperation SubOp1 = Operation;
    FComputeOperation SubOp2 = Operation;
    
    // Set bounds for first sub-operation
    FVector SubMax1 = Operation.Bounds.Max;
    if (SplitAxis == 0)
    {
        SubMax1.X = Center.X;
    }
    else if (SplitAxis == 1)
    {
        SubMax1.Y = Center.Y;
    }
    else
    {
        SubMax1.Z = Center.Z;
    }
    
    SubOp1.Bounds = FBox(Operation.Bounds.Min, SubMax1);
    
    // Set bounds for second sub-operation
    FVector SubMin2 = Operation.Bounds.Min;
    if (SplitAxis == 0)
    {
        SubMin2.X = Center.X;
    }
    else if (SplitAxis == 1)
    {
        SubMin2.Y = Center.Y;
    }
    else
    {
        SubMin2.Z = Center.Z;
    }
    
    SubOp2.Bounds = FBox(SubMin2, Operation.Bounds.Max);
    
    // Assign processing targets
    SubOp1.ForcedTarget = EProcessingTarget::CPU;
    SubOp2.ForcedTarget = EProcessingTarget::GPU;
    
    // Add sub-operations to output array
    OutSubOperations.Add(SubOp1);
    OutSubOperations.Add(SubOp2);
    
    GPU_DISPATCHER_LOG_VERBOSE("Split operation along axis %d into two sub-operations", SplitAxis);
    return true;
}

bool FWorkloadDistributor::MergeOperations(const TArray<FComputeOperation>& Operations, TArray<FOperationBatch>& OutBatches)
{
    if (Operations.Num() <= 1)
    {
        return false;
    }
    
    // Group operations by type
    TMap<uint32, TArray<int32>> OperationsByType;
    for (int32 i = 0; i < Operations.Num(); ++i)
    {
        if (!Operations[i].bCanBeBatched)
        {
            continue;
        }
        
        OperationsByType.FindOrAdd(Operations[i].OperationTypeId).Add(i);
    }
    
    // For each type, group by spatial locality
    for (const auto& TypePair : OperationsByType)
    {
        uint32 OperationTypeId = TypePair.Key;
        const TArray<int32>& Indices = TypePair.Value;
        
        // Skip if too few operations of this type
        if (Indices.Num() < 2)
        {
            continue;
        }
        
        // Group by spatial locality
        TArray<TArray<int32>> Groups;
        GroupBySpatialLocality(Operations, Groups);
        
        // Create batches from groups
        for (const TArray<int32>& Group : Groups)
        {
            // Skip singleton groups
            if (Group.Num() < 2)
            {
                continue;
            }
            
            // Create a batch for this group
            FOperationBatch Batch;
            Batch.OperationTypeId = OperationTypeId;
            
            // Calculate combined bounds
            FBox CombinedBounds = GetBoundingBox(Operations, Group);
            
            // Add regions to batch
            for (int32 OpIndex : Group)
            {
                Batch.Regions.Add(Operations[OpIndex].Bounds);
                
                // Add transform if available (optional, depends on the operation)
                // In a real implementation, this would be more sophisticated
                Batch.Transforms.Add(FMatrix::Identity);
                
                // Add parameters if available (optional, depends on the operation)
                // In a real implementation, this would extract actual parameters
                Batch.Parameters.Add(Operations[OpIndex].Strength);
            }
            
            // Estimate batch cost
            Batch.EstimatedCost = Batch.Regions.Num() * 10; // Simple heuristic
            
            // Determine execution strategy
            Batch.bUseWideExecutionStrategy = CombinedBounds.GetVolume() > 1000000.0f;
            
            OutBatches.Add(Batch);
        }
    }
    
    return OutBatches.Num() > 0;
}

void FWorkloadDistributor::SetDistributionConfig(const FDistributionConfig& InConfig)
{
    Config = InConfig;
}

FDistributionConfig FWorkloadDistributor::GetDistributionConfig() const
{
    return Config;
}

void FWorkloadDistributor::AdjustForMemoryPressure(int64 AvailableBytes)
{
    // Calculate memory pressure as a factor (0.0 to 1.0)
    // where 1.0 means severe memory pressure
    
    // Get total system memory
    uint64 TotalPhysicalGB = FPlatformMemory::GetConstants().TotalPhysicalGB;
    uint64 TotalMemoryBytes = TotalPhysicalGB * 1024 * 1024 * 1024;
    
    // Calculate available percentage
    float AvailablePercentage = (float)AvailableBytes / (float)TotalMemoryBytes;
    
    // Set memory pressure adjustment
    if (AvailablePercentage < 0.1f)
    {
        // Severe memory pressure, heavily bias toward CPU
        this->MemoryPressureAdjustment = 0.5f;
        GPU_DISPATCHER_LOG_WARNING("Severe memory pressure detected (%.1f%% available), adjusting workload distribution",
            AvailablePercentage * 100.0f);
    }
    else if (AvailablePercentage < 0.25f)
    {
        // Moderate memory pressure
        this->MemoryPressureAdjustment = 0.25f;
        GPU_DISPATCHER_LOG_DEBUG("Moderate memory pressure detected (%.1f%% available), adjusting workload distribution",
            AvailablePercentage * 100.0f);
    }
    else
    {
        // Normal memory conditions
        this->MemoryPressureAdjustment = 0.0f;
    }
}

void FWorkloadDistributor::IncreaseCPUWorkloadRatio(float AdditionalRatio)
{
    // Clamp additional ratio to reasonable range
    float ClampedRatio = FMath::Clamp(AdditionalRatio, 0.0f, 0.5f);
    
    // Add to current boost, clamping total to 0.5
    CPUWorkloadRatioBoost = FMath::Min(CPUWorkloadRatioBoost + ClampedRatio, 0.5f);
    
    GPU_DISPATCHER_LOG_DEBUG("Increased CPU workload ratio by %.2f to %.2f", 
        ClampedRatio, CPUWorkloadRatioBoost);
}

void FWorkloadDistributor::RefineDistributionStrategy(const FPerformanceHistory& History)
{
    // Update performance ratio between CPU and GPU
    if (History.AverageCPUTime > 0.0f && History.AverageGPUTime > 0.0f)
    {
        CPUToGPUPerformanceRatio.Add(History.AverageCPUTime / History.AverageGPUTime);
        
        // Keep only the last 10 data points
        if (CPUToGPUPerformanceRatio.Num() > 10)
        {
            CPUToGPUPerformanceRatio.RemoveAt(0, CPUToGPUPerformanceRatio.Num() - 10);
        }
    }
    
    // Calculate average ratio
    float AvgRatio = 0.0f;
    for (float Ratio : CPUToGPUPerformanceRatio)
    {
        AvgRatio += Ratio;
    }
    
    if (CPUToGPUPerformanceRatio.Num() > 0)
    {
        AvgRatio /= CPUToGPUPerformanceRatio.Num();
        
        // Adjust performance ratio threshold based on observed data
        Config.PerformanceRatioThreshold = FMath::Clamp(AvgRatio * 0.8f, 0.5f, 2.0f);
    }
}

bool FWorkloadDistributor::ApplyFallbackStrategy(FComputeOperation& Operation, int32 Strategy)
{
    // Apply the specified fallback strategy
    switch (Strategy)
    {
        case 0: // Reduce precision
            Operation.bRequiresHighPrecision = false;
            return true;
            
        case 1: // Reduce batch size
            Operation.PreferredBatchSize = FMath::Max<int32>(1, Operation.PreferredBatchSize / 2);
            return true;
            
        case 2: // Switch to hybrid CPU/GPU
            {
                TArray<FComputeOperation> SubOperations;
                return SplitOperation(Operation, SubOperations);
            }
            
        case 3: // Switch to CPU
            Operation.ForcedTarget = EProcessingTarget::CPU;
            return true;
            
        default:
            return false;
    }
}

bool FWorkloadDistributor::SplitBetweenCPUAndGPU(FComputeOperation& Operation, TArray<FComputeOperation>& OutOperations)
{
    // Similar to SplitOperation, but returns individual operations instead of modifying the input
    TArray<FComputeOperation> SubOps;
    if (SplitOperation(Operation, SubOps))
    {
        OutOperations = SubOps;
        return true;
    }
    return false;
}

float FWorkloadDistributor::CalculateOperationComplexity(const FComputeOperation& Operation)
{
    // Base complexity on volume size
    float Volume = Operation.Bounds.GetVolume();
    float Complexity = Volume / 1000.0f;
    
    // Adjust based on operation type
    FOperationStats* Stats = OperationStats.Find(Operation.OperationTypeId);
    if (Stats)
    {
        Complexity *= Stats->Complexity;
    }
    
    // High precision increases complexity
    if (Operation.bRequiresHighPrecision)
    {
        Complexity *= 1.5f;
    }
    
    // Narrow band can reduce complexity for surface operations
    if (Operation.bUseNarrowBand)
    {
        // Estimate surface area vs. volume ratio
        float SurfaceArea = 2.0f * (
            Operation.Bounds.GetExtent().X * Operation.Bounds.GetExtent().Y +
            Operation.Bounds.GetExtent().X * Operation.Bounds.GetExtent().Z +
            Operation.Bounds.GetExtent().Y * Operation.Bounds.GetExtent().Z
        );
        
        // Narrow band is more efficient when surface area is small relative to volume
        float SurfaceToVolumeRatio = SurfaceArea / Volume;
        if (SurfaceToVolumeRatio < 0.1f)
        {
            Complexity *= 0.5f;
        }
    }
    
    return Complexity;
}

EProcessingTarget FWorkloadDistributor::SelectTargetBasedOnComplexity(float Complexity)
{
    // Simple heuristic: use GPU for complex operations, CPU for simpler ones
    if (Complexity > Config.ComplexityThreshold)
    {
        return EProcessingTarget::GPU;
    }
    else if (Complexity > Config.ComplexityThreshold / 2.0f)
    {
        return EProcessingTarget::Hybrid;
    }
    else
    {
        return EProcessingTarget::CPU;
    }
}

bool FWorkloadDistributor::IsNarrowBandOperation(const FBox& Bounds, int32 MaterialChannel)
{
    // This would typically check if the operation is targeting a surface region
    // For simplicity, we'll use a heuristic based on the bounds
    
    FVector Size = Bounds.GetSize();
    
    // Calculate surface area to volume ratio
    float SurfaceArea = 2.0f * (Size.X * Size.Y + Size.X * Size.Z + Size.Y * Size.Z);
    float Volume = Size.X * Size.Y * Size.Z;
    float SurfaceToVolumeRatio = SurfaceArea / Volume;
    
    // Higher ratio indicates a "thinner" region, more likely to be a surface
    return SurfaceToVolumeRatio > 0.5f;
}

bool FWorkloadDistributor::GroupBySpatialLocality(const TArray<FComputeOperation>& Operations, TArray<TArray<int32>>& OutGroups)
{
    // Basic spatial locality grouping
    // In a real implementation, this would use more sophisticated spatial partitioning
    
    const float CoherenceThreshold = 0.5f;
    TArray<int32> UnassignedIndices;
    
    // Start with all operations unassigned
    for (int32 i = 0; i < Operations.Num(); ++i)
    {
        UnassignedIndices.Add(i);
    }
    
    // Group operations with spatial coherence
    while (UnassignedIndices.Num() > 0)
    {
        // Start a new group with the first unassigned operation
        TArray<int32> Group;
        Group.Add(UnassignedIndices[0]);
        UnassignedIndices.RemoveAt(0);
        
        // Get the bounds of the first operation in the group
        FBox GroupBounds = Operations[Group[0]].Bounds;
        
        // Try to add spatially coherent operations to the group
        for (int32 i = 0; i < UnassignedIndices.Num(); )
        {
            int32 OpIndex = UnassignedIndices[i];
            const FBox& OpBounds = Operations[OpIndex].Bounds;
            
            // Check if this operation is spatially coherent with the group
            if (IsSpatiallyCoherent(GroupBounds, OpBounds, CoherenceThreshold))
            {
                // Add to group
                Group.Add(OpIndex);
                
                // Expand group bounds
                GroupBounds += OpBounds;
                
                // Remove from unassigned
                UnassignedIndices.RemoveAt(i);
            }
            else
            {
                ++i;
            }
        }
        
        // Add group to output
        OutGroups.Add(Group);
    }
    
    return OutGroups.Num() > 0;
}

FBox FWorkloadDistributor::GetBoundingBox(const TArray<FComputeOperation>& Operations, const TArray<int32>& Indices)
{
    if (Indices.Num() == 0)
    {
        return FBox(ForceInit);
    }
    
    // Start with the bounds of the first operation
    FBox Result = Operations[Indices[0]].Bounds;
    
    // Expand to include all other operations
    for (int32 i = 1; i < Indices.Num(); ++i)
    {
        Result += Operations[Indices[i]].Bounds;
    }
    
    return Result;
}

bool FWorkloadDistributor::IsSpatiallyCoherent(const FBox& BoxA, const FBox& BoxB, float Threshold)
{
    // Check for overlap
    if (BoxA.Intersect(BoxB))
    {
        return true;
    }
    
    // Calculate distance between box centers
    FVector CenterA = BoxA.GetCenter();
    FVector CenterB = BoxB.GetCenter();
    float Distance = FVector::Distance(CenterA, CenterB);
    
    // Calculate average box radius
    float RadiusA = BoxA.GetExtent().Size();
    float RadiusB = BoxB.GetExtent().Size();
    float AvgRadius = (RadiusA + RadiusB) * 0.5f;
    
    // Boxes are spatially coherent if they're close relative to their size
    return Distance < AvgRadius * (1.0f + Threshold);
}

void FWorkloadDistributor::UpdateDecisionModel(const FOperationMetrics& Metrics)
{
    // In a real implementation, this would update a machine learning model
    // or statistical model used to predict performance
    // For simplicity, we'll just update simple statistics
    
    FCriticalSection& MutableLock = const_cast<FCriticalSection&>(StatsLock);
    FScopeLock Lock(&MutableLock);
    
    FPerformanceHistory& History = PerformanceHistoryByType.FindOrAdd(Metrics.OperationTypeId);
    
    // Add to history
    History.History.Add(Metrics);
    
    // Keep history bounded
    const int32 MaxHistorySize = 100;
    if (History.History.Num() > MaxHistorySize)
    {
        History.History.RemoveAt(0, History.History.Num() - MaxHistorySize);
    }
    
    // Recalculate averages
    double TotalCPUTime = 0.0, TotalGPUTime = 0.0;
    int32 CPUCount = 0, GPUCount = 0;
    
    for (const FOperationMetrics& Entry : History.History)
    {
        if (Entry.CPUExecutionTimeMS > 0.0f)
        {
            TotalCPUTime += Entry.CPUExecutionTimeMS;
            CPUCount++;
        }
        
        if (Entry.GPUExecutionTimeMS > 0.0f)
        {
            TotalGPUTime += Entry.GPUExecutionTimeMS;
            GPUCount++;
        }
    }
    
    // Update history averages
    History.AverageCPUTime = CPUCount > 0 ? TotalCPUTime / CPUCount : 0.0f;
    History.AverageGPUTime = GPUCount > 0 ? TotalGPUTime / GPUCount : 0.0f;
    
    // Calculate ratio
    if (History.AverageCPUTime > 0.0f && History.AverageGPUTime > 0.0f)
    {
        History.CPUToGPURatio = History.AverageCPUTime / History.AverageGPUTime;
    }
    
    // Update operation stats
    FOperationStats& Stats = OperationStats.FindOrAdd(Metrics.OperationTypeId);
    
    // Update averages with exponential moving average
    const float Alpha = 0.1f;
    
    if (Metrics.CPUExecutionTimeMS > 0.0f)
    {
        Stats.AvgCPUTime = (1.0f - Alpha) * Stats.AvgCPUTime + Alpha * Metrics.CPUExecutionTimeMS;
        Stats.CPUCount++;
    }
    
    if (Metrics.GPUExecutionTimeMS > 0.0f)
    {
        Stats.AvgGPUTime = (1.0f - Alpha) * Stats.AvgGPUTime + Alpha * Metrics.GPUExecutionTimeMS;
        Stats.GPUCount++;
    }
    
    // Update success rate
    const float SuccessAlpha = 0.05f;
    float NewSuccessRate = Metrics.SuccessfulExecution ? 1.0f : 0.0f;
    Stats.SuccessRate = (1.0f - SuccessAlpha) * Stats.SuccessRate + SuccessAlpha * NewSuccessRate;
}

float FWorkloadDistributor::PredictGPUPerformance(const FComputeOperation& Operation)
{
    // Get operation stats
    FOperationStats* Stats = OperationStats.Find(Operation.OperationTypeId);
    if (!Stats || Stats->GPUCount == 0)
    {
        // Fall back to complexity-based estimate
        return CalculateOperationComplexity(Operation) * 0.1f;
    }
    
    // Base prediction on historical average
    float Prediction = Stats->AvgGPUTime;
    
    // Adjust for operation size
    float VolumeRatio = Operation.Bounds.GetVolume() / 100000.0f;
    Prediction *= FMath::Max(0.1f, VolumeRatio);
    
    // Adjust for high precision
    if (Operation.bRequiresHighPrecision)
    {
        Prediction *= 1.5f;
    }
    
    // Adjust for material channel complexity
    if (Operation.MaterialChannelId >= 0)
    {
        // Multi-channel operations are more complex
        Prediction *= 1.2f;
    }
    
    return Prediction;
}

float FWorkloadDistributor::PredictCPUPerformance(const FComputeOperation& Operation)
{
    // Get operation stats
    FOperationStats* Stats = OperationStats.Find(Operation.OperationTypeId);
    if (!Stats || Stats->CPUCount == 0)
    {
        // Fall back to complexity-based estimate
        return CalculateOperationComplexity(Operation) * 0.2f;
    }
    
    // Base prediction on historical average
    float Prediction = Stats->AvgCPUTime;
    
    // Adjust for operation size
    float VolumeRatio = Operation.Bounds.GetVolume() / 100000.0f;
    Prediction *= FMath::Max(0.1f, VolumeRatio);
    
    // Adjust for high precision
    if (Operation.bRequiresHighPrecision)
    {
        Prediction *= 1.5f;
    }
    
    // CPU often handles small narrow band operations efficiently
    if (Operation.bUseNarrowBand && Operation.Bounds.GetVolume() < 100000.0f)
    {
        Prediction *= 0.8f;
    }
    
    return Prediction;
}

void FWorkloadDistributor::UpdateOperationStats(uint32 OperationTypeId, float CPUTime, float GPUTime, bool bSuccess)
{
    FScopeLock Lock(&StatsLock);
    
    FOperationStats& Stats = OperationStats.FindOrAdd(OperationTypeId);
    
    // Update with exponential moving average
    const float Alpha = 0.1f;
    
    if (CPUTime > 0.0f)
    {
        Stats.AvgCPUTime = (1.0f - Alpha) * Stats.AvgCPUTime + Alpha * CPUTime;
        Stats.CPUCount++;
    }
    
    if (GPUTime > 0.0f)
    {
        Stats.AvgGPUTime = (1.0f - Alpha) * Stats.AvgGPUTime + Alpha * GPUTime;
        Stats.GPUCount++;
    }
    
    // Update success rate
    const float SuccessAlpha = 0.05f;
    float NewSuccessRate = bSuccess ? 1.0f : 0.0f;
    Stats.SuccessRate = (1.0f - SuccessAlpha) * Stats.SuccessRate + SuccessAlpha * NewSuccessRate;
}