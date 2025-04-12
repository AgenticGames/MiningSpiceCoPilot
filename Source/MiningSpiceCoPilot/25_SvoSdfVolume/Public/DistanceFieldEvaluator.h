// DistanceFieldEvaluator.h
// High-performance distance field evaluation with SIMD optimization

#pragma once

#include "CoreMinimal.h"
#include "Math/SIMDFloat.h"
#include "Math/VectorRegister.h"
#include "Math/Vector.h"

// Forward declarations
class FOctreeNodeManager;
class FMaterialSDFManager;

/**
 * High-performance distance field evaluation with SIMD optimization
 * Provides efficient field sampling, hierarchical traversal, and gradient computation
 * Supports multi-channel evaluation and hardware-specific optimizations
 */
class MININGSPICECOPILOT_API FDistanceFieldEvaluator
{
public:
    FDistanceFieldEvaluator();
    ~FDistanceFieldEvaluator();

    // Initialization
    void Initialize(FOctreeNodeManager* InNodeManager, FMaterialSDFManager* InMaterialManager);
    void DetectHardwareCapabilities();

    // Field evaluation
    float EvaluateField(const FVector& WorldPosition, uint8 MaterialIndex) const;
    TArray<float> EvaluateMultiMaterialField(const FVector& WorldPosition, const TArray<uint8>& MaterialIndices) const;
    
    // SIMD-accelerated batch evaluation
    void EvaluateFieldBatch(const TArray<FVector>& WorldPositions, uint8 MaterialIndex, TArray<float>& OutValues) const;
    void EvaluateMultiMaterialFieldBatch(const TArray<FVector>& WorldPositions, 
                                         const TArray<uint8>& MaterialIndices, 
                                         TArray<TArray<float>>& OutValues) const;
    
    // Gradient and normal calculation
    FVector EvaluateGradient(const FVector& WorldPosition, uint8 MaterialIndex) const;
    FVector EvaluateNormal(const FVector& WorldPosition, uint8 MaterialIndex) const;
    void EvaluateGradientBatch(const TArray<FVector>& WorldPositions, uint8 MaterialIndex, TArray<FVector>& OutGradients) const;
    
    // Material boundary detection
    bool FindMaterialBoundary(const FVector& Start, const FVector& Direction, float MaxDistance, 
                              uint8 MaterialIndex, FVector& OutHitPoint, FVector& OutNormal) const;
    
    // Performance optimization
    void SetEvaluationQuality(float Quality);
    void EnableCaching(bool bEnable);
    void ClearCache();
    void PreCacheRegion(const FBox& Region, float Spacing, uint8 MaterialIndex);
    
    // Network-related functionality
    void RegisterNetworkQuery(const FVector& WorldPosition, uint8 MaterialIndex, float Value);
    float PredictNetworkValue(const FVector& WorldPosition, uint8 MaterialIndex) const;
    void ReconcileNetworkPrediction(const FVector& WorldPosition, uint8 MaterialIndex, float ActualValue);

private:
    // Internal data structures
    struct FCacheEntry;
    struct FHardwareCapabilities;
    
    // Implementation details
    FOctreeNodeManager* NodeManager;
    FMaterialSDFManager* MaterialManager;
    TMap<uint64, FCacheEntry> EvaluationCache;
    FHardwareCapabilities Capabilities;
    float EvaluationQuality;
    bool bCachingEnabled;
    
    // Helper methods
    float EvaluateFieldSIMD(VectorRegister4Float Position, uint8 MaterialIndex) const;
    float EvaluateFieldAccurate(const FVector& WorldPosition, uint8 MaterialIndex) const;
    float EvaluateFieldFast(const FVector& WorldPosition, uint8 MaterialIndex) const;
    FVector CalculateNumericalGradient(const FVector& WorldPosition, uint8 MaterialIndex, float Delta) const;
    uint64 GenerateCacheKey(const FVector& Position, uint8 MaterialIndex) const;
    void UpdateQueryStatistics(const FVector& Position, uint8 MaterialIndex) const;
};