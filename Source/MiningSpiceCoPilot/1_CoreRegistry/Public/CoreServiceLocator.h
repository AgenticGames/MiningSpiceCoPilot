// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"
#include "Interfaces/IServiceLocator.h"
#include "Interfaces/IService.h"
#include "HAL/ThreadSafeBool.h"
#include "Containers/Map.h"
#include "Misc/SpinLock.h"
#include "UObject/ScriptInterface.h"
#include "HAL/CriticalSection.h"
#include "HAL/ThreadSafeCounter.h" // For atomic operations
#include "ThreadSafety.h"
#include "CommonServiceTypes.h"

/**
 * Structure for cached service entry with version information
 */
struct FCachedServiceEntry
{
    /** The cached service instance */
    TSharedPtr<IService> Service;
    
    /** The version at which this service was cached */
    uint32 Version;
    
    /** Default constructor */
    FCachedServiceEntry() : Version(0) {}
    
    /** Constructor with service and version */
    FCachedServiceEntry(TSharedPtr<IService> InService, uint32 InVersion)
        : Service(InService)
        , Version(InVersion)
    {}
};

/**
 * Core implementation of the service locator for the mining system
 * Provides service registration, resolution, and lifecycle management
 */
class MININGSPICECOPILOT_API FCoreServiceLocator : public IServiceLocator
{
public:
    /** Default constructor */
    FCoreServiceLocator();
    
    /** Destructor */
    virtual ~FCoreServiceLocator();
    
    // Make IServiceLocator a friend class to allow it to access our private static members
    friend class IServiceLocator;
    
    //~ Begin IServiceLocator Interface
    virtual bool Initialize() override;
    virtual void Shutdown() override;
    virtual bool IsInitialized() const override;
    virtual bool RegisterService(void* InService, const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) override;
    virtual void* ResolveService(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) override;
    virtual bool UnregisterService(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) override;
    virtual bool HasService(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) const override;
    virtual bool RegisterServiceByTypeName(const FString& ServiceTypeName, void* InService, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) override;
    
    // New methods from enhanced interface
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
     * Registers a service provider with the service locator
     * @param InProvider Service provider to register
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return True if registration was successful
     */
    bool RegisterServiceProvider(TScriptInterface<class IServiceProvider> InProvider, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE);
    
    /**
     * Unregisters a service provider from the service locator
     * @param InProvider Service provider to unregister
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return True if unregistration was successful
     */
    bool UnregisterServiceProvider(TScriptInterface<class IServiceProvider> InProvider, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE);
    
    /**
     * Gets all registered service types
     * @return Array of all registered service interface types
     */
    TArray<const UClass*> GetAllServiceTypes() const;
    
    /**
     * Gets a service context key for the given zone and region IDs
     * @param InZoneID Zone identifier
     * @param InRegionID Region identifier
     * @return Context key that can be used for service lookup
     */
    static FString GetServiceContextKey(int32 InZoneID, int32 InRegionID);
    
    /**
     * Gets the singleton instance of the service locator
     * @return Reference to the singleton instance
     */
    static FCoreServiceLocator& Get()
    {
        if (!bSingletonInitialized)
        {
            // Create thread-safe singleton using atomic operations
            FSpinLock InitializationLock;
            
            // Double-checked locking pattern with memory barriers
            if (!bSingletonInitialized)
            {
                FScopedSpinLock Lock(InitializationLock);
                
                if (!bSingletonInitialized)
                {
                    Singleton = new FCoreServiceLocator();
                    bSingletonInitialized.AtomicSet(true);
                    
                    // Auto-initialize
                    if (!Singleton->IsInitialized())
                    {
                        Singleton->Initialize();
                    }
                }
            }
        }
        
        check(Singleton);
        return *Singleton;
    }
    
    /**
     * Registers memory allocators from MemoryPoolManager with the service locator
     * This establishes critical integration between Core Registry and Memory Management systems
     */
    void RegisterMemoryAllocators();
    
    /**
     * Structure for fast-path type resolution
     * Used for direct access to frequently used services
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
            , UsageCount(0)
        {}
        
        /** Constructor with parameters */
        FFastPathEntry(void* InServiceInstance, uint32 InTypeHash, int32 InZoneID, int32 InRegionID)
            : ServiceInstance(InServiceInstance)
            , TypeHash(InTypeHash)
            , ZoneID(InZoneID)
            , RegionID(InRegionID)
            , UsageCount(0)
        {}
    };
    
    /**
     * Gets a fast-path entry for a specific type
     * This bypasses the standard service resolution for frequently accessed types
     * @param TypeHash Hash of the interface type for quick lookup
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return Fast-path entry containing the service instance, or nullptr if not found
     */
    FFastPathEntry* GetFastPathForType(uint32 TypeHash, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE);
    
    /**
     * Registers a fast-path for a specific type
     * @param InInterfaceType Interface type for the fast path
     * @param InServiceInstance Service instance for the fast path
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return True if the fast path was registered successfully
     */
    bool RegisterFastPath(const UClass* InInterfaceType, void* InServiceInstance, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE);
    
    /**
     * Thread-safe service resolution with optimistic read pattern and thread-local caching
     * @param ServiceName Name of the service to resolve
     * @return Shared pointer to the resolved service, or nullptr if not found
     */
    template<typename T>
    TSharedPtr<T> ResolveServiceOptimistic(const FName& ServiceName)
    {
        // Check thread-local cache first
        TMap<FName, FCachedServiceEntry>& ThreadCache = GetThreadLocalCache();
        if (ThreadCache.Contains(ServiceName))
        {
            FWaitFreeCounter* VersionCounter = ServiceVersions.FindRef(ServiceName);
            if (VersionCounter && ThreadCache[ServiceName].Version == VersionCounter->GetValue())
            {
                // Cache hit with current version
                return StaticCastSharedPtr<T>(ThreadCache[ServiceName].Service);
            }
        }
        
        // Cache miss or version mismatch, try optimistic read
        FWaitFreeCounter* VersionCounter = ServiceVersions.FindRef(ServiceName);
        if (VersionCounter)
        {
            uint32 InitialVersion = VersionCounter->GetValue();
            
            // Read service with shared lock (read-lock)
            TSharedPtr<IService> Service;
            {
                FScopedReadLock ReadLock(ServiceMapRWLock);
                // Find service entry
                TMap<FString, TArray<FServiceInstance>>* ContextMap = ServiceMap.Find(ServiceName);
                if (ContextMap && ContextMap->Num() > 0)
                {
                    // Get first available service instance for simplicity
                    // A more advanced implementation would check for zone/region specifics
                    for (auto& Pair : *ContextMap)
                    {
                        if (Pair.Value.Num() > 0 && Pair.Value[0].ServiceInstance)
                        {
                            Service = TSharedPtr<IService>((IService*)Pair.Value[0].ServiceInstance);
                            break;
                        }
                    }
                }
            }
            
            // Check if version is still valid
            if (Service.IsValid() && VersionCounter->GetValue() == InitialVersion)
            {
                // Update thread-local cache
                ThreadCache.Add(ServiceName, FCachedServiceEntry(Service, InitialVersion));
                return StaticCastSharedPtr<T>(Service);
            }
        }
        
        // Fallback to standard resolution
        void* ServicePtr = ResolveService(T::StaticClass());
        if (ServicePtr)
        {
            TSharedPtr<IService> Service = TSharedPtr<IService>((IService*)ServicePtr);
            
            // Add to version tracking if needed
            if (!ServiceVersions.Contains(ServiceName))
            {
                FScopedWriteLock WriteLock(ServiceMapRWLock);
                if (!ServiceVersions.Contains(ServiceName))
                {
                    ServiceVersions.Add(ServiceName, FThreadSafety::Get().CreateWaitFreeCounter(1));
                }
            }
            
            // Update thread-local cache
            FWaitFreeCounter* VersionCounter = ServiceVersions.FindRef(ServiceName);
            if (VersionCounter)
            {
                ThreadCache.Add(ServiceName, FCachedServiceEntry(Service, VersionCounter->GetValue()));
            }
            
            return StaticCastSharedPtr<T>(Service);
        }
        
        return nullptr;
    }

    /**
     * Updates the version counter for a service
     * This should be called whenever a service is modified, registered, or unregistered
     * @param ServiceName Name of the service
     */
    void UpdateServiceVersion(const FName& ServiceName)
    {
        FScopedWriteLock WriteLock(ServiceMapRWLock);
        
        FWaitFreeCounter* VersionCounter = ServiceVersions.FindRef(ServiceName);
        if (!VersionCounter)
        {
            VersionCounter = FThreadSafety::Get().CreateWaitFreeCounter(1);
            ServiceVersions.Add(ServiceName, VersionCounter);
        }
        else
        {
            VersionCounter->Increment();
        }
    }

private:
    /** Structure to hold a service instance and its context */
    struct FServiceInstance
    {
        /** The service instance */
        void* ServiceInstance;
        
        /** Zone ID this service is associated with (INDEX_NONE for global) */
        int32 ZoneID;
        
        /** Region ID this service is associated with (INDEX_NONE for global) */
        int32 RegionID;
        
        /** Priority of this service instance for resolution ordering */
        int32 Priority;
        
        /** Constructor */
        FServiceInstance()
            : ServiceInstance(nullptr)
            , ZoneID(INDEX_NONE)
            , RegionID(INDEX_NONE)
            , Priority(0)
        {}
        
        /** Constructor with parameters */
        FServiceInstance(void* InServiceInstance, int32 InZoneID, int32 InRegionID, int32 InPriority = 0)
            : ServiceInstance(InServiceInstance)
            , ZoneID(InZoneID)
            , RegionID(InRegionID)
            , Priority(InPriority)
        {}
    };
    
    /** 
     * Resolves the best matching service instance based on context
     * Implements the layered resolution strategy:
     * 1. Zone-specific services have highest priority
     * 2. Region-specific services have second highest priority
     * 3. Global services have lowest priority
     * 4. Within each group, higher Priority value wins
     */
    void* ResolveBestMatchingService(const TArray<FServiceInstance>& Instances, int32 InZoneID, int32 InRegionID) const;
    
    /** Map of service instances by interface type name and context key */
    TMap<FName, TMap<FString, TArray<FServiceInstance>>> ServiceMap;
    
    /** Map of service zone assignments */
    TMap<FName, int32> ServiceZoneMap;
    
    /** Map of service versions for optimistic access */
    TMap<FName, FWaitFreeCounter*> ServiceVersions;
    
    /** Map of registered service providers */
    TArray<TScriptInterface<class IServiceProvider>> ServiceProviders;
    
    /** Fast-path cache for frequently used types (indexed by type hash) */
    TMap<uint32, FFastPathEntry> FastPathCache;
    
    /** Lock for accessing the fast-path cache */
    FSpinLock FastPathLock;
    
    /** Reader-writer lock for thread-safe access to the service maps */
    mutable FMiningReaderWriterLock ServiceMapRWLock;
    
    /** Flag indicating whether the locator has been initialized */
    FThreadSafeBool bIsInitialized;
    
    /** Counter for tracking fast-path cache hits */
    FThreadSafeCounter FastPathHits;
    
    /** Counter for tracking standard resolution fallbacks */
    FThreadSafeCounter StandardResolutionCount;
    
    /** Singleton instance */
    static FCoreServiceLocator* Singleton;
    
    /** Flag indicating whether singleton has been initialized */
    static FThreadSafeBool bSingletonInitialized;
    
    /** Initialize a thread-local service cache for fast access */
    struct FThreadLocalServiceCache
    {
        /** Constructor */
        FThreadLocalServiceCache() 
        {
            // Initialize with reasonable capacity
            Cache.Reserve(16);
        }
        
        /** The cached services mapped by name */
        TMap<FName, FCachedServiceEntry> Cache;
        
        /** Gets the thread-local cache instance */
        static FThreadLocalServiceCache& Get() 
        {
            // C++11 thread_local guarantees initialization is thread-safe and happens only once per thread
            static thread_local FThreadLocalServiceCache Instance;
            return Instance;
        }
    };
    
    /** Helper method to access the thread-local cache */
    static TMap<FName, FCachedServiceEntry>& GetThreadLocalCache()
    {
        return FThreadLocalServiceCache::Get().Cache;
    }
    
    /** Helper method to invalidate thread-local cache */
    static void InvalidateThreadLocalCache()
    {
        FThreadLocalServiceCache::Get().Cache.Empty();
    }
    
    /** Map of service versions */
    TMap<FName, FServiceVersion> ServiceVersionsInfo;
    
    /** Map of service dependencies */
    TMap<FName, TArray<TPair<FName, EServiceDependencyType>>> ServiceDependencies;
    
    /** Map of service health status */
    TMap<FName, TMap<FString, EServiceHealthStatus>> ServiceHealthStatus;
    
    /** Map of service scopes */
    TMap<FName, EServiceScope> ServiceScopes;
};
