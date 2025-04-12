// NetworkVolumeCoordinator.h
// Network state coordination for SVO+SDF volume across clients

#pragma once

#include "CoreMinimal.h"
#include "Net/UnrealNetwork.h"
#include "Engine/ActorChannel.h"
#include "Net/DataReplication.h"
#include "Net/Core/NetResult.h"
#include "HAL/ThreadSafeBool.h"

// Forward declarations
class USVOHybridVolume;
class FOctreeNodeManager;
class FMaterialSDFManager;
class FVolumeSerializer;

/**
 * Network state coordination for SVO+SDF volume across clients
 * Handles field modification tracking, authority validation, and replication
 * Supports bandwidth-optimized updates and conflict resolution
 */
class MININGSPICECOPILOT_API FNetworkVolumeCoordinator
{
public:
    FNetworkVolumeCoordinator();
    ~FNetworkVolumeCoordinator();

    // Operation authority levels
    enum class EOperationAuthority : uint8
    {
        Server,           // Server-only operations
        ServerValidated,  // Client request, server validated
        ClientAuthoritative, // Client operations with zone authority
        ReplicatedOnly    // Operations for replication only
    };

    // Initialization
    void Initialize(USVOHybridVolume* InVolume, FOctreeNodeManager* InNodeManager, 
                   FMaterialSDFManager* InMaterialManager, FVolumeSerializer* InSerializer);
    
    // Network state management
    void RegisterNetworkOperation(uint64 OperationId, const FVector& Position, float Radius, uint8 MaterialIndex);
    bool ValidateNetworkOperation(uint64 OperationId, const FVector& Position, float Radius, uint8 MaterialIndex);
    void CompleteNetworkOperation(uint64 OperationId, bool Success);
    void AbortNetworkOperation(uint64 OperationId);
    
    // Authority management
    bool HasAuthorityForOperation(const FVector& Position, float Radius) const;
    void RegisterClientAuthority(uint64 ClientId, const FBox& Zone);
    void RevokeClientAuthority(uint64 ClientId, const FBox& Zone);
    void SynchronizeAuthorityZones();
    
    // State synchronization
    void GenerateStateDelta(uint64 FromVersion, uint64 ToVersion, TArray<uint8>& OutDeltaData);
    bool ApplyStateDelta(const TArray<uint8>& DeltaData, uint64 FromVersion, uint64 ToVersion);
    void RequestFullStateSync();
    void RequestPartialStateSync(const FBox& Region, const TArray<uint8>& MaterialIndices);
    
    // Network traffic optimization
    void SetReplicationPriority(const FBox& Region, uint8 Priority);
    void SetMaterialReplicationPriority(uint8 MaterialIndex, uint8 Priority);
    void OptimizeBandwidthUsage(float AvailableBandwidth);
    
    // Network events
    void OnClientConnected(uint64 ClientId);
    void OnClientDisconnected(uint64 ClientId);
    void OnNetworkStateReceived(const TArray<uint8>& StateData, uint64 BaseVersion, uint64 TargetVersion);
    void OnNetworkStateSent(uint64 BaseVersion, uint64 TargetVersion);
    
    // Version management
    uint64 GetCurrentStateVersion() const;
    bool ValidateStateVersion(uint64 Version) const;
    void RegisterStateVersion(uint64 Version);

private:
    // Internal data structures
    struct FPendingOperation;
    struct FClientAuthZone;
    struct FReplicationState;
    struct FOperationConflict;
    
    // Implementation details
    USVOHybridVolume* Volume;
    FOctreeNodeManager* NodeManager;
    FMaterialSDFManager* MaterialManager;
    FVolumeSerializer* Serializer;
    TMap<uint64, FPendingOperation> PendingOperations;
    TMap<uint64, TArray<FClientAuthZone>> ClientAuthorityZones;
    TMap<FBox, uint8> RegionPriorities;
    TMap<uint8, uint8> MaterialPriorities;
    uint64 StateVersion;
    FThreadSafeBool IsSynchronizing;
    
    // Helper methods
    bool CheckOperationConflicts(const FVector& Position, float Radius, uint8 MaterialIndex, TArray<FOperationConflict>& Conflicts);
    void ResolveOperationConflict(const FOperationConflict& Conflict);
    void PrioritizeUpdates(TArray<FBox>& RegionsToUpdate, float AvailableBandwidth);
    bool IsMaterialReplicationRequired(uint8 MaterialIndex, const FBox& Region);
    void TrackBandwidthUsage(uint64 DataSize, bool IsOutgoing);
    bool ValidateClientAuthority(uint64 ClientId, const FBox& OperationZone);
    void UpdateNetworkStatistics();
};