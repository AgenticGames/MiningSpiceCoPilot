// NetworkVolumeCoordinator.h
// Coordinates network syncing of volume changes between clients and server

#pragma once

#include "CoreMinimal.h"
#include "Net/UnrealNetwork.h"
#include "Engine/ActorChannel.h"
#include "Net/DataReplication.h"
#include "HAL/ThreadSafeBool.h"
#include "Containers/Map.h"
#include "25_SvoSdfVolume/Public/BoxHash.h"

// Forward declarations
class USVOHybridVolume;
class FVolumeSerializer;
class FOctreeNodeManager;
class FMaterialSDFManager;

/**
 * Network volume coordination system
 * Handles synchronization of volume data between clients and server
 * Implements conflict resolution, region locking, and delta compression
 */
class MININGSPICECOPILOT_API FNetworkVolumeCoordinator
{
public:
    FNetworkVolumeCoordinator();
    ~FNetworkVolumeCoordinator();

    // Network operation results
    enum class ENetworkResult : uint8
    {
        Success,
        Failure,
        Conflict,
        OutOfSync,
        Pending
    };

    // Authority levels for access control
    enum class EAuthorityLevel : uint8
    {
        None,
        ReadOnly,
        ReadWrite,
        Admin
    };

    // Types of pending operations
    enum class EOperationType : uint8
    {
        Modification,
        Query,
        Lock,
        Unlock,
        Sync
    };

    // Network operation tracking
    struct FPendingOperation
    {
        uint64 OperationId;
        EOperationType Type;
        FBox Region;
        uint8 MaterialIndex;
        uint64 BaseVersion;
        double Timestamp;
        bool bCompleted;
        ENetworkResult Result;
        
        // Constructor with defaults
        FPendingOperation()
            : OperationId(0)
            , Type(EOperationType::Modification)
            , Region(ForceInit)
            , MaterialIndex(0)
            , BaseVersion(0)
            , Timestamp(0.0)
            , bCompleted(false)
            , Result(ENetworkResult::Pending)
        {}
        
        // Constructor with parameters
        FPendingOperation(uint64 InOpId, EOperationType InType, const FBox& InRegion,
                          uint8 InMaterialIdx, uint64 InBaseVersion)
            : OperationId(InOpId)
            , Type(InType)
            , Region(InRegion)
            , MaterialIndex(InMaterialIdx)
            , BaseVersion(InBaseVersion)
            , Timestamp(FPlatformTime::Seconds())
            , bCompleted(false)
            , Result(ENetworkResult::Pending)
        {}
    };

    // Initialize with volume reference
    void Initialize(USVOHybridVolume* InVolume, FVolumeSerializer* InSerializer);
    
    // Volume synchronization
    ENetworkResult RequestRegionModification(const FBox& Region, uint8 MaterialIndex);
    ENetworkResult SubmitRegionModification(const FBox& Region, uint8 MaterialIndex, const TArray<uint8>& DeltaData, uint64 BaseVersion);
    ENetworkResult RequestRegionLock(const FBox& Region, float TimeoutSeconds);
    ENetworkResult ReleaseRegionLock(const FBox& Region);
    
    // Data synchronization
    ENetworkResult SynchronizeWithServer(uint64 ClientVersion);
    TArray<uint8> GenerateServerUpdate(uint64 BaseVersion);
    ENetworkResult ApplyClientUpdate(const TArray<uint8>& DeltaData, uint64 BaseVersion, uint64 ClientId);
    
    // Authority management
    void SetClientAuthority(uint64 ClientId, EAuthorityLevel Authority);
    EAuthorityLevel GetClientAuthority(uint64 ClientId) const;
    bool IsClientAuthorized(uint64 ClientId, const FBox& Region) const;
    
    // Conflict resolution
    void SetConflictResolutionStrategy(uint8 Strategy);
    void RegisterConflictHandler(TFunction<bool(const FBox&, uint8, uint64)> Handler);
    bool ResolveConflict(const FBox& Region, uint8 MaterialIndex, uint64 ClientId);
    
    // Region queries and tracking
    bool IsRegionLocked(const FBox& Region) const;
    bool IsRegionModifiedSince(const FBox& Region, uint64 BaseVersion) const;
    TArray<FBox> GetModifiedRegions(uint64 BaseVersion) const;
    TMap<FBox, uint8> GetActiveRegions() const;
    
    // Operation management
    uint64 GetPendingOperationCount() const;
    ENetworkResult GetOperationResult(uint64 OperationId) const;
    void CancelOperation(uint64 OperationId);
    void CleanupCompletedOperations(float TimeThresholdSeconds = 60.0f);
    
private:
    // Internal data
    USVOHybridVolume* Volume;
    FVolumeSerializer* Serializer;
    TMap<uint64, FPendingOperation> PendingOperations;
    TMap<FBox, uint8> ActiveRegions;
    TMap<FBox, uint64> RegionLocks;
    TMap<uint64, EAuthorityLevel> ClientAuthorities;
    uint8 ConflictResolutionStrategy;
    TFunction<bool(const FBox&, uint8, uint64)> ConflictHandler;
    FThreadSafeBool bServerMode;
    
    // Helper methods
    bool CheckRegionOverlap(const FBox& RegionA, const FBox& RegionB) const;
    uint64 GenerateOperationId() const;
    void TrackModifiedRegion(const FBox& Region, uint8 MaterialIndex);
    void UpdateOperationStatus(uint64 OperationId, ENetworkResult Result);
    void CleanupStaleRegionLocks();
    bool ValidateVersionConsistency(uint64 BaseVersion) const;
    ENetworkResult ProcessModificationRequest(const FBox& Region, uint8 MaterialIndex, uint64 ClientId);
};