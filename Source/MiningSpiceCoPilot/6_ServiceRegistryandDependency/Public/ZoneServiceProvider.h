// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "../../1_CoreRegistry/Public/Interfaces/IServiceProvider.h"
#include "../../1_CoreRegistry/Public/CommonServiceTypes.h"
#include "Templates/SharedPointer.h"
#include "Containers/Map.h"
#include "Containers/Array.h"
#include "HAL/CriticalSection.h"
#include "Math/Vector.h"
#include "ZoneServiceProvider.generated.h"

class FZoneTypeRegistry;
class IServiceLocator;
class ITransactionService;
class IZoneManager;

/**
 * Specialized service provider for zone-based transaction components
 * Provides zone-specific service registration and resolution with spatial context
 * Supports transaction coordination across zone boundaries and fast-path resolution
 */
UCLASS()
class MININGSPICECOPILOT_API UZoneServiceProvider : public UObject, public IServiceProvider
{
    GENERATED_BODY()

public:
    UZoneServiceProvider();
    virtual ~UZoneServiceProvider();
    
    //~ Begin IServiceProvider Interface
    virtual TArray<TSubclassOf<UInterface>> GetProvidedServices() const override;
    virtual bool RegisterServices(IServiceLocator* InServiceLocator, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) override;
    virtual bool UnregisterServices(IServiceLocator* InServiceLocator, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) override;
    virtual bool InitializeServices() override;
    virtual void ShutdownServices() override;
    virtual FName GetProviderName() const override;
    virtual TArray<FServiceDependency> GetServiceDependencies() const override;
    virtual bool HandleLifecyclePhase(EServiceLifecyclePhase Phase) override;
    virtual EServiceScope GetServiceScope() const override;
    virtual FServiceHealth GetServiceHealth() const override;
    virtual bool RecoverServices() override;
    virtual FServiceConfig GetServiceConfig() const override;
    virtual bool UpdateServiceConfig(const FServiceConfig& InConfig) override;
    virtual bool ValidateServiceDependencies(IServiceLocator* InServiceLocator, TArray<FServiceDependency>& OutMissingDependencies) override;
    virtual TArray<TSubclassOf<UInterface>> GetDependentServices(IServiceLocator* InServiceLocator) override;
    //~ End IServiceProvider Interface
    
    /**
     * Registers a zone-specific transaction service
     * @param InZoneID Zone identifier
     * @param InTransactionType Transaction type identifier
     * @param InService Transaction service implementation
     * @return True if registration was successful
     */
    bool RegisterZoneTransactionService(int32 InZoneID, uint32 InTransactionType, TSharedPtr<ITransactionService> InService);
    
    /**
     * Resolves a transaction service for a specific zone
     * @param InZoneID Zone identifier
     * @param InTransactionType Transaction type identifier
     * @return Transaction service implementation or nullptr if not found
     */
    TSharedPtr<ITransactionService> ResolveZoneTransactionService(int32 InZoneID, uint32 InTransactionType);
    
    /**
     * Resolves a service based on spatial coordinates
     * @param InWorldLocation World location to resolve service for
     * @param InServiceType Service type identifier
     * @return Service instance or nullptr if not found
     */
    TSharedPtr<UObject> ResolveSpatialService(const FVector& InWorldLocation, TSubclassOf<UInterface> InServiceType);
    
    /**
     * Fast-path resolution for critical mining operations
     * @param InZoneID Zone identifier
     * @param InTransactionType Transaction type identifier
     * @return Transaction service implementation with optimized resolution
     */
    TSharedPtr<ITransactionService> FastPathResolve(int32 InZoneID, uint32 InTransactionType);
    
    /**
     * Coordinates a transaction that spans multiple zones
     * @param InSourceZoneID Source zone identifier
     * @param InTargetZoneID Target zone identifier
     * @param InTransactionType Transaction type identifier
     * @return Transaction service that can coordinate across zones
     */
    TSharedPtr<ITransactionService> CoordinateCrossZoneTransaction(
        int32 InSourceZoneID, 
        int32 InTargetZoneID, 
        uint32 InTransactionType);
    
    /**
     * Gets all neighboring zones for a specified zone
     * @param InZoneID Zone identifier
     * @return Array of neighboring zone identifiers
     */
    TArray<int32> GetNeighboringZones(int32 InZoneID) const;
    
    /**
     * Converts world coordinates to a zone identifier
     * @param InWorldLocation World location
     * @return Zone identifier containing the location
     */
    int32 WorldLocationToZoneID(const FVector& InWorldLocation) const;
    
    /**
     * Gets the zone manager for a specific region
     * @param InRegionID Region identifier
     * @return Zone manager implementation or nullptr if not found
     */
    TSharedPtr<IZoneManager> GetZoneManager(int32 InRegionID = INDEX_NONE) const;
    
    /**
     * Updates transaction conflict statistics for dynamic optimization
     * @param InZoneID Zone identifier
     * @param InTransactionType Transaction type identifier
     * @param InConflictRate New conflict rate value
     */
    void UpdateTransactionConflictRate(int32 InZoneID, uint32 InTransactionType, float InConflictRate);

private:
    /**
     * Initializes the service provider with the zone type registry
     */
    void InitializeWithRegistry();
    
    /**
     * Generates a zone service key for lookups
     * @param InZoneID Zone identifier
     * @param InTransactionType Transaction type identifier
     * @return Unique zone service key
     */
    uint64 GenerateZoneServiceKey(int32 InZoneID, uint32 InTransactionType) const;
    
    /**
     * Updates the fast path threshold based on conflict statistics
     * @param InZoneID Zone identifier
     * @param InTransactionType Transaction type identifier
     */
    void UpdateFastPathThreshold(int32 InZoneID, uint32 InTransactionType);
    
    /** Reference to the zone type registry */
    TWeakPtr<FZoneTypeRegistry> TypeRegistry;
    
    /** Service locator reference */
    IServiceLocator* ServiceLocator;
    
    /** Zone transaction services by zone ID and transaction type */
    TMap<uint64, TSharedPtr<ITransactionService>> ZoneTransactionServices;
    
    /** Zone managers by region ID */
    TMap<int32, TSharedPtr<IZoneManager>> ZoneManagers;
    
    /** Fast path threshold by zone ID and transaction type */
    TMap<uint64, float> FastPathThresholds;
    
    /** Transaction conflict rates by zone ID and transaction type */
    TMap<uint64, float> ConflictRates;
    
    /** Critical section for thread-safe access */
    mutable FCriticalSection ServiceLock;
    
    /** Configuration for this provider */
    FServiceConfig ServiceConfig;
    
    /** Health status for this provider */
    FServiceHealth ServiceHealth;
    
    /** List of service dependencies */
    TArray<FServiceDependency> ServiceDependencies;
    
    /** Critical transaction types for fast path optimization */
    TArray<uint32> CriticalTransactionTypes;
    
    /** Zone grid configuration name */
    FName ZoneGridConfigName;
    
    /** Flag to track initialization status */
    bool bInitialized;
};
