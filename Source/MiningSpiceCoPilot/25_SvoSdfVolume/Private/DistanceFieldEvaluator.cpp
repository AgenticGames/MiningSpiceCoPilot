// DistanceFieldEvaluator.cpp
// Evaluates distance fields for different materials at arbitrary positions

#include "25_SvoSdfVolume/Public/DistanceFieldEvaluator.h"
#include "25_SvoSdfVolume/Public/MaterialSDFManager.h"
#include "HAL/ThreadSafeCounter.h"
#include "Math/UnrealMathSSE.h"
#include "Math/Vector.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "HAL/CriticalSection.h"
#include "1_Core/Public/ServiceLocator.h"
#include "5_Config/Public/IConfigManager.h"
#include "3_Threading/Public/ITaskScheduler.h"

// SIMD-specific includes for different instruction sets
#if PLATFORM_ENABLE_VECTORINTRINSICS_NEON
    #include "Math/UnrealMathNeon.h"
#endif

FDistanceFieldEvaluator::FDistanceFieldEvaluator()
    : OctreeManager(nullptr)
    , MaterialManager(nullptr)
    , EvaluationAccuracy(0.01f)
    , MaxCacheSize(10000)
    , bCachingEnabled(true)
    , CacheHitCount(0)
    , CacheMissCount(0)
    , TotalEvaluationTime(0.0)
    , TotalEvaluations(0)
{
    // Detect hardware capabilities on construction
    DetectHardwareCapabilities();
}

FDistanceFieldEvaluator::~FDistanceFieldEvaluator()
{
    // Clear cache on destruction
    ClearCache();
}

void FDistanceFieldEvaluator::Initialize(FOctreeNodeManager* InOctreeManager, FMaterialSDFManager* InMaterialManager)
{
    OctreeManager = InOctreeManager;
    MaterialManager = InMaterialManager;
    
    // Get configuration settings from System 5: Configuration Management System
    auto* ConfigManager = IServiceLocator::Get().ResolveService<IConfigManager>();
    if (ConfigManager)
    {
        EvaluationAccuracy = ConfigManager->GetValue<float>("DistanceField.EvaluationAccuracy", 0.01f);
        MaxCacheSize = ConfigManager->GetValue<uint32>("DistanceField.MaxCacheSize", 10000);
        bCachingEnabled = ConfigManager->GetValue<bool>("DistanceField.EnableCaching", true);
    }
}

float FDistanceFieldEvaluator::EvaluateDistanceField(const FVector& Position, uint8 MaterialIndex) const
{
    // Check for valid material manager
    if (!MaterialManager)
    {
        return 0.0f;
    }
    
    // Check cache first if enabled
    if (bCachingEnabled)
    {
        uint64 CacheKey = CalculateCacheKey(Position, MaterialIndex);
        const FCacheEntry* Entry = EvaluationCache.Find(CacheKey);
        
        if (Entry)
        {
            // Cache hit
            FPlatformAtomics::InterlockedIncrement(&CacheHitCount);
            return Entry->Distance;
        }
    }
    
    // Cache miss or caching disabled, evaluate directly
    FPlatformAtomics::InterlockedIncrement(&CacheMissCount);
    
    double StartTime = FPlatformTime::Seconds();
    float Distance = EvaluateDistanceFieldInternal(Position, MaterialIndex);
    double EndTime = FPlatformTime::Seconds();
    
    // Update performance metrics
    double ElapsedTime = EndTime - StartTime;
    FPlatformAtomics::InterlockedAdd(&TotalEvaluations, 1);
    FPlatformAtomics::InterlockedAdd(&TotalEvaluationTime, ElapsedTime);
    
    // Update cache if enabled
    if (bCachingEnabled)
    {
        UpdateCache(Position, MaterialIndex, Distance);
        MaintainCacheSize();
    }
    
    return Distance;
}

TArray<float> FDistanceFieldEvaluator::EvaluateMultiChannelField(const FVector& Position) const
{
    if (!MaterialManager)
    {
        return TArray<float>();
    }
    
    return MaterialManager->EvaluateMultiChannelFieldAtPosition(Position);
}

FVector FDistanceFieldEvaluator::EvaluateGradient(const FVector& Position, uint8 MaterialIndex) const
{
    if (!MaterialManager)
    {
        return FVector::ZeroVector;
    }
    
    // Check cache first if enabled
    if (bCachingEnabled)
    {
        uint64 CacheKey = CalculateCacheKey(Position, MaterialIndex);
        FCacheEntry* Entry = EvaluationCache.Find(CacheKey);
        
        if (Entry && Entry->bHasGradient)
        {
            FPlatformAtomics::InterlockedIncrement(&CacheHitCount);
            return Entry->Gradient;
        }
    }
    
    // Cache miss or caching disabled, evaluate directly
    FVector Gradient = EvaluateGradientInternal(Position, MaterialIndex);
    
    // Update cache if enabled
    if (bCachingEnabled)
    {
        uint64 CacheKey = CalculateCacheKey(Position, MaterialIndex);
        FCacheEntry* Entry = EvaluationCache.Find(CacheKey);
        
        if (Entry)
        {
            Entry->Gradient = Gradient;
            Entry->bHasGradient = true;
            Entry->Timestamp = FPlatformTime::Seconds();
        }
        else
        {
            // Create new entry with both distance and gradient
            FCacheEntry NewEntry(EvaluateDistanceFieldInternal(Position, MaterialIndex), Position);
            NewEntry.Gradient = Gradient;
            NewEntry.bHasGradient = true;
            EvaluationCache.Add(CacheKey, NewEntry);
            
            MaintainCacheSize();
        }
    }
    
    return Gradient;
}

FVector FDistanceFieldEvaluator::EstimateNormal(const FVector& Position, uint8 MaterialIndex) const
{
    FVector Gradient = EvaluateGradient(Position, MaterialIndex);
    
    // Normalize the gradient to get the normal
    // Using a safe normalization to prevent division by zero
    float Length = Gradient.Size();
    if (Length > SMALL_NUMBER)
    {
        return Gradient / Length;
    }
    
    return FVector::UpVector; // Return a default up vector if gradient is too small
}

bool FDistanceFieldEvaluator::IsPositionInside(const FVector& Position, uint8 MaterialIndex) const
{
    if (!MaterialManager)
    {
        return false;
    }
    
    float Distance = EvaluateDistanceField(Position, MaterialIndex);
    return Distance < 0.0f;
}

bool FDistanceFieldEvaluator::IsIntersectingField(const FBox& Box, uint8 MaterialIndex, float Threshold) const
{
    if (!MaterialManager)
    {
        return false;
    }
    
    // Fast rejection test - evaluate field at center and corners
    FVector Center = Box.GetCenter();
    float CenterDistance = FMath::Abs(EvaluateDistanceField(Center, MaterialIndex));
    
    // If center is inside (with threshold), we know it intersects
    if (CenterDistance <= Threshold)
    {
        return true;
    }
    
    // If center distance is greater than threshold + half diagonal, definitely no intersection
    float HalfDiagonal = Box.GetExtent().Size();
    if (CenterDistance > HalfDiagonal + Threshold)
    {
        return false;
    }
    
    // Check corners for more precise intersection test
    FVector Extent = Box.GetExtent();
    TArray<FVector> Corners;
    Corners.Reserve(8);
    
    for (int x = -1; x <= 1; x += 2)
    {
        for (int y = -1; y <= 1; y += 2)
        {
            for (int z = -1; z <= 1; z += 2)
            {
                FVector Corner = Center + FVector(x * Extent.X, y * Extent.Y, z * Extent.Z);
                if (FMath::Abs(EvaluateDistanceField(Corner, MaterialIndex)) <= Threshold)
                {
                    return true;
                }
            }
        }
    }
    
    // More advanced tests could be done here, like checking edges, but often not needed
    
    return false;
}

bool FDistanceFieldEvaluator::TraceSphere(const FVector& Start, const FVector& End, float Radius, uint8 MaterialIndex, FVector& HitPosition) const
{
    if (!MaterialManager)
    {
        return false;
    }
    
    // Ray direction and length
    FVector Direction = End - Start;
    float Length = Direction.Size();
    
    if (Length < SMALL_NUMBER)
    {
        // Start and end are almost identical
        float StartDist = EvaluateDistanceField(Start, MaterialIndex);
        if (StartDist <= Radius)
        {
            HitPosition = Start;
            return true;
        }
        return false;
    }
    
    // Normalize direction
    Direction /= Length;
    
    // Basic sphere tracing algorithm
    FVector CurrentPosition = Start;
    float TotalDistance = 0.0f;
    const int32 MaxSteps = 128; // Maximum iteration limit
    const float MinStep = 0.01f; // Minimum step size
    
    for (int32 i = 0; i < MaxSteps && TotalDistance < Length; ++i)
    {
        float Distance = EvaluateDistanceField(CurrentPosition, MaterialIndex) - Radius;
        
        // Hit detected
        if (Distance <= EvaluationAccuracy)
        {
            HitPosition = CurrentPosition;
            return true;
        }
        
        // Adjust step size based on distance to surface
        float StepSize = FMath::Max(Distance * 0.8f, MinStep); // Using 0.8 as a safety factor
        
        // Check if we will overshoot
        if (TotalDistance + StepSize > Length)
        {
            CurrentPosition = End;
            float FinalDistance = EvaluateDistanceField(End, MaterialIndex) - Radius;
            if (FinalDistance <= EvaluationAccuracy)
            {
                HitPosition = End;
                return true;
            }
            break;
        }
        
        // Take a step
        CurrentPosition += Direction * StepSize;
        TotalDistance += StepSize;
    }
    
    return false;
}

void FDistanceFieldEvaluator::EvaluateFieldBatch(const TArray<FVector>& Positions, uint8 MaterialIndex, TArray<float>& OutDistances) const
{
    if (!MaterialManager)
    {
        OutDistances.Empty();
        return;
    }
    
    const int32 Count = Positions.Num();
    OutDistances.SetNum(Count);
    
    // Parallel evaluation for large batches
    if (Count > 64 && Capabilities.MaxThreadCount > 1)
    {
        auto* TaskScheduler = IServiceLocator::Get().ResolveService<ITaskScheduler>();
        if (TaskScheduler)
        {
            int32 ChunkSize = FMath::Max(16, Count / (Capabilities.MaxThreadCount * 2)); // Aim for 2x tasks per thread
            
            struct FBatchEvalContext
            {
                const FDistanceFieldEvaluator* Evaluator;
                const TArray<FVector>* InPositions;
                TArray<float>* OutResults;
                uint8 MaterialIdx;
                int32 StartIdx;
                int32 EndIdx;
            };
            
            for (int32 StartIdx = 0; StartIdx < Count; StartIdx += ChunkSize)
            {
                int32 EndIdx = FMath::Min(StartIdx + ChunkSize, Count);
                
                FBatchEvalContext* Context = new FBatchEvalContext{
                    this, &Positions, &OutDistances, MaterialIndex, StartIdx, EndIdx
                };
                
                TaskScheduler->ScheduleTask("DistanceFieldBatchEval", [Context]() {
                    for (int32 i = Context->StartIdx; i < Context->EndIdx; i++)
                    {
                        (*Context->OutResults)[i] = Context->Evaluator->EvaluateDistanceField((*Context->InPositions)[i], Context->MaterialIdx);
                    }
                    delete Context;
                }, ETaskPriority::Normal);
            }
            
            TaskScheduler->WaitForTasks("DistanceFieldBatchEval");
        }
        else
        {
            // Fallback to sequential if task scheduler not available
            for (int32 i = 0; i < Count; i++)
            {
                OutDistances[i] = EvaluateDistanceField(Positions[i], MaterialIndex);
            }
        }
    }
    else
    {
        // SIMD optimization based on hardware capabilities
        if (Capabilities.bHasAVX2 && Count >= 8)
        {
            // Process 8 vectors at a time with AVX2 (implementation details would go here)
            // This would require platform-specific code and vectorized field evaluation
            // For now, fall back to sequential evaluation
            for (int32 i = 0; i < Count; i++)
            {
                OutDistances[i] = EvaluateDistanceField(Positions[i], MaterialIndex);
            }
        }
        else if (Capabilities.bHasSSE4 && Count >= 4)
        {
            // Process 4 vectors at a time with SSE4
            // Similar to above, this requires platform-specific implementation
            for (int32 i = 0; i < Count; i++)
            {
                OutDistances[i] = EvaluateDistanceField(Positions[i], MaterialIndex);
            }
        }
        else
        {
            // Standard sequential evaluation
            for (int32 i = 0; i < Count; i++)
            {
                OutDistances[i] = EvaluateDistanceField(Positions[i], MaterialIndex);
            }
        }
    }
}

void FDistanceFieldEvaluator::EvaluateGradientBatch(const TArray<FVector>& Positions, uint8 MaterialIndex, TArray<FVector>& OutGradients) const
{
    if (!MaterialManager)
    {
        OutGradients.Empty();
        return;
    }
    
    const int32 Count = Positions.Num();
    OutGradients.SetNum(Count);
    
    // Similar parallel approach as EvaluateFieldBatch
    if (Count > 64 && Capabilities.MaxThreadCount > 1)
    {
        auto* TaskScheduler = IServiceLocator::Get().ResolveService<ITaskScheduler>();
        if (TaskScheduler)
        {
            int32 ChunkSize = FMath::Max(16, Count / (Capabilities.MaxThreadCount * 2));
            
            struct FBatchGradContext
            {
                const FDistanceFieldEvaluator* Evaluator;
                const TArray<FVector>* InPositions;
                TArray<FVector>* OutResults;
                uint8 MaterialIdx;
                int32 StartIdx;
                int32 EndIdx;
            };
            
            for (int32 StartIdx = 0; StartIdx < Count; StartIdx += ChunkSize)
            {
                int32 EndIdx = FMath::Min(StartIdx + ChunkSize, Count);
                
                FBatchGradContext* Context = new FBatchGradContext{
                    this, &Positions, &OutGradients, MaterialIndex, StartIdx, EndIdx
                };
                
                TaskScheduler->ScheduleTask("DistanceFieldGradientBatchEval", [Context]() {
                    for (int32 i = Context->StartIdx; i < Context->EndIdx; i++)
                    {
                        (*Context->OutResults)[i] = Context->Evaluator->EvaluateGradient((*Context->InPositions)[i], Context->MaterialIdx);
                    }
                    delete Context;
                }, ETaskPriority::Normal);
            }
            
            TaskScheduler->WaitForTasks("DistanceFieldGradientBatchEval");
        }
        else
        {
            // Sequential fallback
            for (int32 i = 0; i < Count; i++)
            {
                OutGradients[i] = EvaluateGradient(Positions[i], MaterialIndex);
            }
        }
    }
    else
    {
        // Standard sequential evaluation
        for (int32 i = 0; i < Count; i++)
        {
            OutGradients[i] = EvaluateGradient(Positions[i], MaterialIndex);
        }
    }
}

void FDistanceFieldEvaluator::SetEvaluationAccuracy(float Accuracy)
{
    EvaluationAccuracy = FMath::Max(Accuracy, 0.0001f); // Ensure minimum accuracy
}

void FDistanceFieldEvaluator::EnableCaching(bool bEnable)
{
    if (bCachingEnabled && !bEnable)
    {
        // Clear cache when disabling
        ClearCache();
    }
    
    bCachingEnabled = bEnable;
}

void FDistanceFieldEvaluator::SetMaxCacheSize(uint32 MaxEntries)
{
    MaxCacheSize = FMath::Max(MaxEntries, 100u); // Ensure reasonable minimum
    
    // If current cache is larger than new max, trim it
    if (bCachingEnabled && EvaluationCache.Num() > (int32)MaxCacheSize)
    {
        MaintainCacheSize();
    }
}

void FDistanceFieldEvaluator::ClearCache()
{
    EvaluationCache.Empty();
}

double FDistanceFieldEvaluator::GetAverageEvaluationTime() const
{
    if (TotalEvaluations == 0)
    {
        return 0.0;
    }
    
    return TotalEvaluationTime / TotalEvaluations;
}

uint32 FDistanceFieldEvaluator::GetCacheHitCount() const
{
    return CacheHitCount;
}

uint32 FDistanceFieldEvaluator::GetCacheMissCount() const
{
    return CacheMissCount;
}

float FDistanceFieldEvaluator::EvaluateDistanceFieldInternal(const FVector& Position, uint8 MaterialIndex) const
{
    if (!MaterialManager)
    {
        return 0.0f;
    }
    
    // Delegate to material manager for actual field evaluation
    return MaterialManager->EvaluateFieldAtPosition(Position, MaterialIndex);
}

FVector FDistanceFieldEvaluator::EvaluateGradientInternal(const FVector& Position, uint8 MaterialIndex) const
{
    if (!MaterialManager)
    {
        return FVector::ZeroVector;
    }
    
    // Delegate to material manager for gradient calculation
    return MaterialManager->EvaluateGradientAtPosition(Position, MaterialIndex);
}

uint64 FDistanceFieldEvaluator::CalculateCacheKey(const FVector& Position, uint8 MaterialIndex) const
{
    // Create a cache key from position and material index
    // Quantize position slightly to improve cache hit rate
    const float Quantization = EvaluationAccuracy;
    int32 X = FMath::RoundToInt(Position.X / Quantization);
    int32 Y = FMath::RoundToInt(Position.Y / Quantization);
    int32 Z = FMath::RoundToInt(Position.Z / Quantization);
    
    // Combine into a single 64-bit key
    // 20 bits per spatial component, 4 bits for material (supporting up to 16 materials per key)
    uint64 Key = ((uint64)X & 0xFFFFF) | (((uint64)Y & 0xFFFFF) << 20) | (((uint64)Z & 0xFFFFF) << 40) | (((uint64)MaterialIndex & 0xF) << 60);
    
    return Key;
}

void FDistanceFieldEvaluator::UpdateCache(const FVector& Position, uint8 MaterialIndex, float Distance) const
{
    // Add or update cache entry
    uint64 CacheKey = CalculateCacheKey(Position, MaterialIndex);
    FCacheEntry NewEntry(Distance, Position);
    EvaluationCache.Add(CacheKey, NewEntry);
}

void FDistanceFieldEvaluator::MaintainCacheSize() const
{
    // If cache exceeds maximum size, remove oldest entries
    if (EvaluationCache.Num() > (int32)MaxCacheSize)
    {
        // Create a sorted list of entries by timestamp
        struct FCacheKeyTime
        {
            uint64 Key;
            double Timestamp;
        };
        
        TArray<FCacheKeyTime> SortedEntries;
        SortedEntries.Reserve(EvaluationCache.Num());
        
        for (auto& Pair : EvaluationCache)
        {
            SortedEntries.Add({ Pair.Key, Pair.Value.Timestamp });
        }
        
        // Sort by timestamp (oldest first)
        SortedEntries.Sort([](const FCacheKeyTime& A, const FCacheKeyTime& B) {
            return A.Timestamp < B.Timestamp;
        });
        
        // Remove oldest entries to get back to 90% of max cache size
        int32 NumToRemove = EvaluationCache.Num() - (int32)(MaxCacheSize * 0.9f);
        for (int32 i = 0; i < NumToRemove && i < SortedEntries.Num(); ++i)
        {
            EvaluationCache.Remove(SortedEntries[i].Key);
        }
    }
}

void FDistanceFieldEvaluator::DetectHardwareCapabilities()
{
    // Reset capabilities
    Capabilities = FHardwareCapabilities();
    
    // Check for SIMD support
#if PLATFORM_ENABLE_VECTORINTRINSICS
    Capabilities.bHasSSE4 = true; // Base level on modern hardware
    
    #if PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_MAC
        // Detect AVX and AVX2 support
        #if WITH_AVX_SUPPORT
            Capabilities.bHasAVX = true;
            #if WITH_AVX2_SUPPORT
                Capabilities.bHasAVX2 = true;
            #endif
        #endif
    #elif PLATFORM_ANDROID || PLATFORM_IOS
        // Detect ARM NEON support
        Capabilities.bHasNeonSupport = true;
    #endif
#endif

    // Check for GPU compute capabilities
    // This would involve checking for compute shader support and available hardware
    // For now, default to false - would be set when checking engine capabilities
    Capabilities.bHasGPUAcceleration = false;
    
    // Get max thread count for parallel processing
    Capabilities.MaxThreadCount = FPlatformMisc::NumberOfCores();
    
    // Get configuration from the system
    auto* ConfigManager = IServiceLocator::Get().ResolveService<IConfigManager>();
    if (ConfigManager)
    {
        // Allow configuration override of detected capabilities
        Capabilities.bHasGPUAcceleration = ConfigManager->GetValue<bool>("Hardware.EnableGPUAcceleration", Capabilities.bHasGPUAcceleration);
        Capabilities.MaxThreadCount = ConfigManager->GetValue<uint32>("Hardware.MaxThreadsForFieldEvaluation", Capabilities.MaxThreadCount);
        
        // Check if we should force disable certain capabilities
        bool bForceDisableAVX = ConfigManager->GetValue<bool>("Hardware.ForceDisableAVX", false);
        if (bForceDisableAVX)
        {
            Capabilities.bHasAVX = false;
            Capabilities.bHasAVX2 = false;
        }
    }
}