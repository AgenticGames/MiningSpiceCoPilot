// DistanceFieldEvaluator.h
// Evaluates distance fields for different materials at arbitrary positions

#pragma once

#include "CoreMinimal.h"
#include "Containers/Map.h"
#include "25_SvoSdfVolume/Public/BoxHash.h"

// Forward declarations
class FOctreeNodeManager;
class FMaterialSDFManager;

/**
 * Distance field evaluator for multi-channel SDF volume
 * Provides efficient field sampling with hardware acceleration when available
 * Includes gradient computation, normal estimation, and intersection testing
 */
class MININGSPICECOPILOT_API FDistanceFieldEvaluator
{
public:
    FDistanceFieldEvaluator();
    ~FDistanceFieldEvaluator();
    
    // Hardware acceleration capabilities
    struct FHardwareCapabilities
    {
        bool bHasSSE4;
        bool bHasAVX;
        bool bHasAVX2;
        bool bHasNeonSupport;
        bool bHasGPUAcceleration;
        uint32 MaxThreadCount;
        
        // Constructor with defaults
        FHardwareCapabilities() 
            : bHasSSE4(false)
            , bHasAVX(false)
            , bHasAVX2(false)
            , bHasNeonSupport(false)
            , bHasGPUAcceleration(false)
            , MaxThreadCount(1)
        {}
    };
    
    // Cache entry for distance field results
    struct FCacheEntry
    {
        float Distance;
        FVector Position;
        FVector Gradient;
        double Timestamp;
        bool bHasGradient;
        
        // Constructor
        FCacheEntry()
            : Distance(0.0f)
            , Position(FVector::ZeroVector)
            , Gradient(FVector::ZeroVector)
            , Timestamp(0.0)
            , bHasGradient(false)
        {}
        
        // Constructor with values
        FCacheEntry(float InDistance, const FVector& InPosition)
            : Distance(InDistance)
            , Position(InPosition)
            , Gradient(FVector::ZeroVector)
            , Timestamp(FPlatformTime::Seconds())
            , bHasGradient(false)
        {}
    };
    
    // Initialize with managers
    void Initialize(FOctreeNodeManager* InOctreeManager, FMaterialSDFManager* InMaterialManager);
    
    // Distance field evaluation
    float EvaluateDistanceField(const FVector& Position, uint8 MaterialIndex) const;
    TArray<float> EvaluateMultiChannelField(const FVector& Position) const;
    
    // Gradient and normal computation
    FVector EvaluateGradient(const FVector& Position, uint8 MaterialIndex) const;
    FVector EstimateNormal(const FVector& Position, uint8 MaterialIndex) const;
    
    // Intersection and inside/outside tests
    bool IsPositionInside(const FVector& Position, uint8 MaterialIndex) const;
    bool IsIntersectingField(const FBox& Box, uint8 MaterialIndex, float Threshold = 0.0f) const;
    bool TraceSphere(const FVector& Start, const FVector& End, float Radius, uint8 MaterialIndex, FVector& HitPosition) const;
    
    // Batch operations
    void EvaluateFieldBatch(const TArray<FVector>& Positions, uint8 MaterialIndex, TArray<float>& OutDistances) const;
    void EvaluateGradientBatch(const TArray<FVector>& Positions, uint8 MaterialIndex, TArray<FVector>& OutGradients) const;
    
    // Configuration
    void SetEvaluationAccuracy(float Accuracy);
    void EnableCaching(bool bEnable);
    void SetMaxCacheSize(uint32 MaxEntries);
    void ClearCache();
    
    // Performance monitoring
    double GetAverageEvaluationTime() const;
    uint32 GetCacheHitCount() const;
    uint32 GetCacheMissCount() const;
    
private:
    // Internal data
    FOctreeNodeManager* OctreeManager;
    FMaterialSDFManager* MaterialManager;
    
    // Evaluation cache
    mutable TMap<uint64, FCacheEntry> EvaluationCache;
    mutable FHardwareCapabilities Capabilities;
    
    // Configuration
    float EvaluationAccuracy;
    uint32 MaxCacheSize;
    bool bCachingEnabled;
    
    // Performance counters
    mutable uint32 CacheHitCount;
    mutable uint32 CacheMissCount;
    mutable double TotalEvaluationTime;
    mutable uint32 TotalEvaluations;
    
    // Helper methods
    float EvaluateDistanceFieldInternal(const FVector& Position, uint8 MaterialIndex) const;
    FVector EvaluateGradientInternal(const FVector& Position, uint8 MaterialIndex) const;
    uint64 CalculateCacheKey(const FVector& Position, uint8 MaterialIndex) const;
    void UpdateCache(const FVector& Position, uint8 MaterialIndex, float Distance) const;
    void MaintainCacheSize() const;
    void DetectHardwareCapabilities();
};