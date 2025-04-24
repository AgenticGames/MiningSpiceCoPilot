// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "../../1_CoreRegistry/Public/Interfaces/IServiceProvider.h"
#include "../../1_CoreRegistry/Public/CommonServiceTypes.h"
#include "../../1_CoreRegistry/Public/SVONodeTypes.h"
#include "../../3_ThreadingTaskSystem/Public/Interfaces/ITaskScheduler.h"
#include "Containers/Map.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"
#include "HAL/CriticalSection.h"
#include "SVOServiceProvider.generated.h"

class FSVOTypeRegistry;
class IServiceLocator;
class INodeManager;
class IFieldOperator;
class INodeSerializer;

/**
 * Metadata for cached service entries
 * Tracks access time and other cache management information
 */
struct FServiceCacheMetadata
{
    /** Time when this entry was last accessed */
    double LastAccessTime;
    
    /** Number of times this entry has been accessed */
    int32 AccessCount;
    
    /** Priority level for cache retention (higher = keep longer) */
    int32 Priority;
    
    /** Whether this entry is for a high-frequency service */
    bool bIsHighFrequency;
    
    /** Default constructor */
    FServiceCacheMetadata()
        : LastAccessTime(0.0)
        , AccessCount(0)
        , Priority(0)
        , bIsHighFrequency(false)
    {
    }
};

/**
 * Specialized service provider for SVO+SDF hybrid volume components
 * Provides registration and resolution of node managers, field operators, and serializers
 * Implements optimized caching for high-frequency queries and cross-region coordination
 */
UCLASS()
class MININGSPICECOPILOT_API USVOServiceProvider : public UObject, public IServiceProvider
{
    GENERATED_BODY()

public:
    USVOServiceProvider();
    virtual ~USVOServiceProvider();
    
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
     * Registers a node manager service
     * @param InNodeClass Node class for this manager
     * @param InManager Manager implementation
     * @param InRegionID Optional region identifier
     * @return True if registration was successful
     */
    bool RegisterNodeManager(ESVONodeClass InNodeClass, TSharedPtr<INodeManager> InManager, int32 InRegionID = INDEX_NONE);
    
    /**
     * Resolves a node manager service for the specified node class
     * @param InNodeClass Node class to resolve manager for
     * @param InRegionID Optional region identifier
     * @return Node manager implementation or nullptr if not found
     */
    TSharedPtr<INodeManager> ResolveNodeManager(ESVONodeClass InNodeClass, int32 InRegionID = INDEX_NONE);
    
    /**
     * Registers a field operator service
     * @param InOperatorType Operator type identifier
     * @param InOperator Operator implementation
     * @param InRegionID Optional region identifier
     * @return True if registration was successful
     */
    bool RegisterFieldOperator(uint32 InOperatorType, TSharedPtr<IFieldOperator> InOperator, int32 InRegionID = INDEX_NONE);
    
    /**
     * Resolves a field operator service for the specified type
     * @param InOperatorType Operator type identifier
     * @param InRegionID Optional region identifier
     * @return Field operator implementation or nullptr if not found
     */
    TSharedPtr<IFieldOperator> ResolveFieldOperator(uint32 InOperatorType, int32 InRegionID = INDEX_NONE);
    
    /**
     * Registers a serializer service
     * @param InNodeClass Node class for this serializer
     * @param InSerializer Serializer implementation
     * @param InRegionID Optional region identifier
     * @return True if registration was successful
     */
    bool RegisterSerializer(ESVONodeClass InNodeClass, TSharedPtr<INodeSerializer> InSerializer, int32 InRegionID = INDEX_NONE);
    
    /**
     * Resolves a serializer service for the specified node class
     * @param InNodeClass Node class to resolve serializer for
     * @param InRegionID Optional region identifier
     * @return Serializer implementation or nullptr if not found
     */
    TSharedPtr<INodeSerializer> ResolveSerializer(ESVONodeClass InNodeClass, int32 InRegionID = INDEX_NONE);
    
    /**
     * Coordinates services across region boundaries for field operations
     * @param InSourceRegionID Source region identifier
     * @param InTargetRegionID Target region identifier
     * @param InOperatorType Operator type identifier
     * @return Field operator that can handle cross-region operations
     */
    TSharedPtr<class IFieldOperator> CoordinateCrossRegionOperation(int32 InSourceRegionID, int32 InTargetRegionID, uint32 InOperatorType);
    
    /**
     * Optimized lookup for high-frequency service queries
     * @param InServiceType Service type identifier
     * @param InRegionID Optional region identifier
     * @return Service instance or nullptr if not found
     */
    TSharedPtr<UObject> FastPathLookup(TSubclassOf<UInterface> InServiceType, int32 InRegionID = INDEX_NONE);
    
    /**
     * Clears the service cache to force fresh resolution
     */
    void ClearServiceCache();
    
    /**
     * Pre-warms the cache with frequently used services
     * @param InRegionID Optional region identifier to pre-warm for
     */
    void PreWarmServiceCache(int32 InRegionID = INDEX_NONE);

private:
    /**
     * Initializes the service provider with the SVO type registry
     */
    void InitializeWithRegistry();
    
    /**
     * Updates the cache for a service
     * @param InServiceType Service type identifier
     * @param InService Service instance
     * @param InRegionID Optional region identifier
     */
    void UpdateServiceCache(TSubclassOf<UInterface> InServiceType, TSharedPtr<UObject> InService, int32 InRegionID = INDEX_NONE);
    
    /**
     * Generates a cache key from service type and region
     * @param InServiceType Service type identifier
     * @param InRegionID Region identifier
     * @return Unique cache key
     */
    uint64 GenerateCacheKey(TSubclassOf<UInterface> InServiceType, int32 InRegionID) const;
    
    /**
     * Generates a cache key from type ID and region
     * @param TypeId Type identifier as uint32
     * @param InRegionID Region identifier
     * @return Unique cache key
     */
    uint64 GenerateCacheKey(uint32 TypeId, int32 InRegionID) const;
    
    /** Reference to the SVO type registry */
    TWeakPtr<FSVOTypeRegistry> TypeRegistry;
    
    /** Service locator reference */
    IServiceLocator* ServiceLocator;
    
    /** Node manager services by node class and region */
    TMap<uint64, TSharedPtr<class INodeManager>> NodeManagers;
    
    /** Field operator services by type and region */
    TMap<uint64, TSharedPtr<class IFieldOperator>> FieldOperators;
    
    /** Serializer services by node class and region */
    TMap<uint64, TSharedPtr<class INodeSerializer>> Serializers;
    
    /** Fast lookup cache for service resolution */
    TMap<uint64, TSharedPtr<UObject>> ServiceCache;
    
    /** Metadata for cached services */
    TMap<uint64, TSharedPtr<FServiceCacheMetadata>> ServiceCacheMetadata;
    
    /** Critical section for thread-safe access */
    mutable FCriticalSection ServiceLock;
    
    /** Configuration for this provider */
    FServiceConfig ServiceConfig;
    
    /** Health status for this provider */
    FServiceHealth ServiceHealth;
    
    /** List of service dependencies */
    TArray<FServiceDependency> ServiceDependencies;
    
    /** High-frequency service types for pre-warming */
    TArray<TSubclassOf<UInterface>> HighFrequencyServices;
    
    /** Flag to track initialization status */
    bool bInitialized;
};
