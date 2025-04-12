// SVOHybridVolume.h
// Core hybrid volume representation combining sparse octree with multi-channel SDFs

#pragma once

#include "CoreMinimal.h"
#include "Containers/SparseArray.h"
#include "Math/UnrealMathSSE.h"
#include "HAL/ThreadSafeCounter64.h"
#include "Templates/Atomic.h"
#include "HAL/ThreadSafeBool.h"
#include "Net/UnrealNetwork.h"
#include "MiningSpiceCoPilot/2_MemoryManagement/Public/Interfaces/IMemoryTracker.h"
#include "MiningSpiceCoPilot/25_SvoSdfVolume/Public/OctreeNodeManager.h"
#include "MiningSpiceCoPilot/25_SvoSdfVolume/Public/MaterialSDFManager.h"
#include "MiningSpiceCoPilot/25_SvoSdfVolume/Public/DistanceFieldEvaluator.h"
#include "MiningSpiceCoPilot/25_SvoSdfVolume/Public/MaterialInteractionModel.h"
#include "MiningSpiceCoPilot/25_SvoSdfVolume/Public/VolumeSerializer.h"
#include "MiningSpiceCoPilot/25_SvoSdfVolume/Public/NetworkVolumeCoordinator.h"
#include "MiningSpiceCoPilot/25_SvoSdfVolume/Public/NarrowBandAllocator.h"
#include "SVOHybridVolume.generated.h"

/**
 * Core hybrid volume representation combining sparse octree with multi-channel SDFs
 * Provides high-fidelity terrain representation with sub-centimeter precision at material interfaces
 * while maintaining memory efficiency through adaptive precision and sparse representation.
 * Supports network synchronization for multiplayer environments.
 */
UCLASS()
class MININGSPICECOPILOT_API USVOHybridVolume : public UObject
{
    GENERATED_BODY()

public:
    USVOHybridVolume();
    virtual ~USVOHybridVolume();

    // Initialization and configuration
    void Initialize(const FIntVector& WorldDimensions, float LeafNodeSize, uint8 MaxDepth);
    void SetMaterialChannelCount(uint32 MaterialCount);

    // Core field access and evaluation
    float EvaluateDistanceField(const FVector& WorldPosition, uint8 MaterialIndex) const;
    TArray<float> EvaluateMultiChannelField(const FVector& WorldPosition) const;
    FVector EvaluateGradient(const FVector& WorldPosition, uint8 MaterialIndex) const;
    bool IsPositionInside(const FVector& WorldPosition, uint8 MaterialIndex) const;

    // Material interaction operations
    void UnionMaterial(const FVector& Position, float Radius, uint8 MaterialIndex, float Strength);
    void SubtractMaterial(const FVector& Position, float Radius, uint8 MaterialIndex, float Strength);
    void BlendMaterials(const FVector& Position, float Radius, uint8 SourceMaterial, uint8 TargetMaterial, float BlendFactor);
    
    // Network synchronization
    void RegisterNetworkVersion(uint64 VersionId);
    bool ValidateFieldModification(const FVector& Position, float Radius, uint8 MaterialIndex) const;
    void ApplyNetworkDelta(const TArray<uint8>& DeltaData, uint64 BaseVersion, uint64 TargetVersion);
    TArray<uint8> GenerateNetworkDelta(uint64 BaseVersion, uint64 CurrentVersion) const;
    
    // Memory management
    void OptimizeMemoryUsage();
    void PrioritizeRegion(const FBox& Region, uint8 Priority);
    FMemoryStats GetMemoryStats() const;

    // Serialization
    void SerializeState(FArchive& Ar);
    void SerializeStateDelta(FArchive& Ar, uint64 BaseVersion);

    // UPROPERTY and replication setup for Unreal networking
    UPROPERTY(Replicated)
    uint64 CurrentStateVersion;

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
    // Internal state and component references
    TUniquePtr<FOctreeNodeManager> OctreeManager;
    TUniquePtr<FMaterialSDFManager> MaterialManager;
    TUniquePtr<FDistanceFieldEvaluator> FieldEvaluator;
    TUniquePtr<FMaterialInteractionModel> InteractionModel;
    TUniquePtr<FVolumeSerializer> Serializer;
    TUniquePtr<FNetworkVolumeCoordinator> NetworkCoordinator;
    TUniquePtr<FNarrowBandAllocator> NarrowBandAllocator;

    // Internal implementation details
    void InitializeInternal(const FIntVector& WorldDimensions, float LeafNodeSize, uint8 MaxDepth);
    bool ValidateOperation(const FVector& Position, float Radius, uint8 MaterialIndex) const;
    void ProcessFieldUpdate(const FVector& Position, float Radius, uint8 MaterialIndex);
};