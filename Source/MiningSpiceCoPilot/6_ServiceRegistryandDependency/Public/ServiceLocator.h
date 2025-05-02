// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "../../1_CoreRegistry/Public/Interfaces/IServiceLocator.h"
#include "../../1_CoreRegistry/Public/CoreServiceLocator.h"
#include "../../3_ThreadingTaskSystem/Public/ThreadSafety.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/ThreadSafeCounter.h"
#include "Containers/Map.h"
#include "Containers/Array.h"
#include "Templates/SharedPointer.h"
#include "Templates/Function.h"
#include "UObject/WeakObjectPtr.h"
#include "Misc/SpinLock.h"
#include "../../1_CoreRegistry/Public/Interfaces/IServiceProvider.h"
#include "Interfaces/IService.h"

/**
 * Structure for service instance and its context
 */
struct FServiceEntry
{
    /** The service instance */
    void* ServiceInstance;
    
    /** Zone ID this service is associated with (INDEX_NONE for global) */
    int32 ZoneID;
    
    /** Region ID this service is associated with (INDEX_NONE for global) */
    int32 RegionID;
    
    /** Version information for this service */
    FServiceVersion Version;
    
    /** Service health status */
    EServiceHealthStatus HealthStatus;
    
    /** Service scope */
    EServiceScope Scope;
    
    /** Priority of this service instance for resolution ordering */
    int32 Priority;
    
    /** Constructor */
    FServiceEntry()
        : ServiceInstance(nullptr)
        , ZoneID(INDEX_NONE)
        , RegionID(INDEX_NONE)
        , HealthStatus(EServiceHealthStatus::Healthy)
        , Scope(EServiceScope::Global)
        , Priority(0)
    {}
    
    /** Constructor with parameters */
    FServiceEntry(void* InServiceInstance, int32 InZoneID, int32 InRegionID, 
                 const FServiceVersion& InVersion = FServiceVersion(), 
                 EServiceScope InScope = EServiceScope::Global,
                 int32 InPriority = 0)
        : ServiceInstance(InServiceInstance)
        , ZoneID(InZoneID)
        , RegionID(InRegionID)
        , Version(InVersion)
        , HealthStatus(EServiceHealthStatus::Healthy)
        , Scope(InScope)
        , Priority(InPriority)
    {}
};

/**
 * Structure for fast-path service lookup
 */
struct FFastPathEntry
{
    /** The service instance */
    void* ServiceInstance;
    
    /** Type hash for quick validation */
    uint32 TypeHash;
    
    /** Zone ID this service is associated with */
    int32 ZoneID;
    
    /** Region ID this service is associated with */
    int32 RegionID;
    
    /** Counter for usage frequency */
    FThreadSafeCounter UsageCount;
    
    /** Constructor */
    FFastPathEntry()
        : ServiceInstance(nullptr)
        , TypeHash(0)
        , ZoneID(INDEX_NONE)
        , RegionID(INDEX_NONE)
    {}
    
    /** Constructor with parameters */
    FFastPathEntry(void* InServiceInstance, uint32 InTypeHash, int32 InZoneID, int32 InRegionID)
        : ServiceInstance(InServiceInstance)
        , TypeHash(InTypeHash)
        , ZoneID(InZoneID)
        , RegionID(InRegionID)
    {}
};

// Forward declaration for service type key
struct FServiceTypeKey;

/**
 * High-performance implementation of the service locator optimized for mining operations
 * Provides thread-safe service registration, resolution, and lifecycle management
 * with hierarchical scoping, NUMA-aware caching, and fast-path optimization
 */
class MININGSPICECOPILOT_API FServiceLocator : public IServiceLocator
{
public:
    /** Structure for uniquely identifying a service type and context */
    struct FServiceTypeKey
    {
        /** Interface type name */
        FName TypeName;
        
        /** Zone identifier */
        int32 ZoneID;
        
        /** Region identifier */
        int32 RegionID;
        
        /** Constructor */
        FServiceTypeKey(const UClass* InType, int32 InZoneID, int32 InRegionID)
            : TypeName(InType ? InType->GetFName() : NAME_None)
            , ZoneID(InZoneID)
            , RegionID(InRegionID)
        {}
        
        /** Default constructor */
        FServiceTypeKey()
            : ZoneID(INDEX_NONE)
            , RegionID(INDEX_NONE)
        {}
        
        /** Equality operator */
        bool operator==(const FServiceTypeKey& Other) const
        {
            return TypeName == Other.TypeName && ZoneID == Other.ZoneID && RegionID == Other.RegionID;
        }
        
        /** Get hash code for the key */
        friend uint32 GetTypeHash(const FServiceTypeKey& Key)
        {
            return HashCombine(HashCombine(GetTypeHash(Key.TypeName), GetTypeHash(Key.ZoneID)), GetTypeHash(Key.RegionID));
        }
    };
    
    /** Structure for thread-local service cache */
    struct FThreadLocalServiceCache
    {
        /** Cached services mapped by type and context */
        TMap<FServiceTypeKey, FCachedServiceEntry> Cache;
        
        /** Constructor */
        FThreadLocalServiceCache() {}
        
        /** Gets the thread-local cache instance */
        static FThreadLocalServiceCache& Get();
        
        /** Invalidates the cache */
        void Invalidate()
        {
            Cache.Empty();
        }
        
        /** Invalidates a specific cache entry */
        void InvalidateEntry(const UClass* InType, int32 InZoneID, int32 InRegionID)
        {
            Cache.Remove(FServiceTypeKey(InType, InZoneID, InRegionID));
        }
        
        /**
         * Helper function to add a cached service with proper type handling
         * @param Key The service type key
         * @param InService The service to cache
         * @param InVersion The version of the service
         */
        void AddCachedService(const FServiceTypeKey& Key, void* InService, uint32 InVersion)
        {
            // Convert the raw pointer to a TSharedPtr<IService> for compatibility with CoreServiceLocator
            TSharedPtr<IService> ServicePtr = MakeShareable((IService*)InService);
            FCachedServiceEntry Entry(ServicePtr, InVersion);
            Cache.Add(Key, Entry);
        }
    };

    /** Destructor */
    virtual ~FServiceLocator();
    
    //~ Begin IServiceLocator Interface
    virtual bool Initialize() override;
    virtual void Shutdown() override;
    virtual bool IsInitialized() const override;
    virtual bool RegisterService(void* InService, const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) override;
    virtual bool RegisterServiceByTypeName(const FString& ServiceTypeName, void* InService, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) override;
    virtual void* ResolveService(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) override;
    virtual bool UnregisterService(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) override;
    virtual bool HasService(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) const override;
    
    virtual bool RegisterServiceWithVersion(void* InService, const UClass* InInterfaceType, const FServiceVersion& InVersion, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) override;
    virtual void* ResolveServiceWithVersion(const UClass* InInterfaceType, FServiceVersion& OutVersion, const FServiceVersion* InMinVersion = nullptr, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) override;
    virtual bool DeclareDependency(const UClass* InDependentType, const UClass* InDependencyType, EServiceDependencyType InDependencyKind = EServiceDependencyType::Required) override;
    virtual bool ValidateDependencies(TArray<TPair<UClass*, UClass*>>& OutMissingDependencies) override;
    virtual EServiceHealthStatus GetServiceHealth(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) override;
    virtual bool RecoverService(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) override;
    virtual EServiceScope GetServiceScope(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) override;
    virtual TArray<UClass*> GetDependentServices(const UClass* InInterfaceType) override;
    virtual TArray<UClass*> GetServiceDependencies(const UClass* InInterfaceType) override;
    //~ End IServiceLocator Interface

    /**
     * Gets the singleton instance of the service locator
     * @return Reference to the service locator instance
     */
    static FServiceLocator& Get();
    
    /**
     * Registers a service provider with the service locator
     * @param InProvider Service provider to register
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return True if registration was successful
     */
    bool RegisterServiceProvider(TScriptInterface<IServiceProvider> InProvider, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE);
    
    /**
     * Unregisters a service provider from the service locator
     * @param InProvider Service provider to unregister
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return True if unregistration was successful
     */
    bool UnregisterServiceProvider(TScriptInterface<IServiceProvider> InProvider, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE);
    
    /**
     * Preregisters a fast-path for a specific service type
     * This can significantly improve resolution performance for frequently used services
     * @param InInterfaceType Interface type for the fast path
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return True if the fast path was registered successfully
     */
    bool RegisterFastPath(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE);
    
    /**
     * Gets a service context key for the given zone and region IDs
     * @param InZoneID Zone identifier
     * @param InRegionID Region identifier
     * @return Context key that can be used for service lookup
     */
    static FString GetServiceContextKey(int32 InZoneID, int32 InRegionID);
    
    /**
     * Optimistic thread-local cached service resolution
     * @param InInterfaceType Interface type to resolve
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return Resolved service instance or nullptr if not found
     */
    void* ResolveServiceCached(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE);
    
    /**
     * Fast direct service resolution without type checking
     * Use only when you know the exact type and context
     * @param TypeHash Hash of the interface type
     * @param InZoneID Zone identifier
     * @param InRegionID Region identifier
     * @return Resolved service instance or nullptr if not found
     */
    void* ResolveServiceDirect(uint32 TypeHash, int32 InZoneID, int32 InRegionID);
    
    /**
     * Invalidates the thread-local cache for all threads
     * Call this when services are registered or unregistered globally
     */
    void InvalidateGlobalCache();
    
    /**
     * Invalidates the thread-local cache for services in a specific context
     * @param InZoneID Zone identifier
     * @param InRegionID Region identifier
     */
    void InvalidateContextCache(int32 InZoneID, int32 InRegionID);
    
    /**
     * Gets all registered service types
     * @return Array of all registered service interface types
     */
    TArray<const UClass*> GetAllServiceTypes() const;
    
    /**
     * Gets all service instances for a specific type
     * @param InInterfaceType Interface type
     * @return Array of service entries for the specified type
     */
    TArray<FServiceEntry> GetAllServiceInstances(const UClass* InInterfaceType) const;

private:
    /** Private constructor to enforce singleton pattern */
    FServiceLocator();
    
    /**
     * Helper to get NUMA domain for the current thread
     * @return NUMA domain ID for the current thread
     */
    uint32 GetCurrentThreadNUMADomain() const;
    
    /**
     * Resolves the best matching service entry based on context
     * Implements hierarchical resolution (zone → region → global)
     * @param Entries Array of service entries to search
     * @param InZoneID Zone identifier for resolution context
     * @param InRegionID Region identifier for resolution context
     * @return Best matching service entry or nullptr if not found
     */
    const FServiceEntry* ResolveBestMatchingService(const TArray<FServiceEntry>& Entries, int32 InZoneID, int32 InRegionID) const;
    
    /**
     * Helper to invalidate a specific thread-local cache entry
     * @param InInterfaceType Interface type
     * @param InZoneID Zone identifier
     * @param InRegionID Region identifier
     */
    void InvalidateCacheEntry(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID);
    
    /**
     * Updates service version for a specific type and context
     * @param InInterfaceType Interface type
     * @param InZoneID Zone identifier
     * @param InRegionID Region identifier
     */
    void UpdateServiceVersion(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID);
    
    /**
     * Gets or creates a version counter for a service
     * @param InInterfaceType Interface type
     * @param InZoneID Zone identifier
     * @param InRegionID Region identifier
     * @return Pointer to the version counter
     */
    FWaitFreeCounter* GetServiceVersionCounter(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID);
    
    /** Map of service entries by interface type name */
    TMap<FName, TArray<FServiceEntry>> ServiceMap;
    
    /** Map of service fast-path entries by type hash */
    TMap<uint32, FFastPathEntry> FastPathMap;
    
    /** Map of service version counters */
    TMap<FServiceTypeKey, FWaitFreeCounter*> ServiceVersions;
    
    /** Map of service dependencies (dependent type name → dependencies) */
    TMap<FName, TArray<TPair<FName, EServiceDependencyType>>> ServiceDependencies;
    
    /** Reader-writer lock for service map */
    mutable FMiningReaderWriterLock ServiceMapLock;
    
    /** Lock for fast-path map */
    FNUMAOptimizedSpinLock FastPathLock;
    
    /** Lock for service version map */
    FMiningReaderWriterLock VersionMapLock;
    
    /** Lock for service dependencies map */
    mutable FMiningReaderWriterLock DependencyMapLock;
    
    /** Flag indicating whether the locator has been initialized */
    FThreadSafeBool bIsInitialized;
    
    /** Singleton instance */
    static FServiceLocator* Singleton;
    
    /** Flag indicating whether singleton has been initialized */
    static FThreadSafeBool bSingletonInitialized;
    
    /** NUMA topology information */
    FNUMATopology NUMATopology;
    
    /** Array of NUMA-local type caches */
    TArray<FNUMALocalTypeCache*> DomainTypeCaches;
    
    /** Service provider registrations */
    TArray<TScriptInterface<IServiceProvider>> ServiceProviders;
    
    /** Lock for service provider array */
    FMiningReaderWriterLock ProviderLock;
    
    /** Performance metrics */
    FThreadSafeCounter FastPathHits;
    FThreadSafeCounter CacheHits;
    FThreadSafeCounter StandardResolutionCount;
};
