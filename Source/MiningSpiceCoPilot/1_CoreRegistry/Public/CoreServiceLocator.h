// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"
#include "Interfaces/IServiceLocator.h"
#include "HAL/ThreadSafeBool.h"
#include "Containers/Map.h"
#include "Misc/SpinLock.h"
#include "UObject/ScriptInterface.h"
#include "HAL/CriticalSection.h"
#include "HAL/ThreadSafeCounter.h" // For atomic operations

/**
 * Core implementation of the service locator for the mining system
 * Provides service registration, resolution, and lifecycle management
 */
class MININGSPICECOPILOT_API FCoreServiceLocator : public IServiceLocator
{
public:
    /** Constructor */
    FCoreServiceLocator();
    
    /** Destructor */
    virtual ~FCoreServiceLocator();
    
    //~ Begin IServiceLocator Interface
    virtual bool Initialize() override;
    virtual void Shutdown() override;
    virtual bool IsInitialized() const override;
    virtual bool RegisterService(void* InService, const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) override;
    virtual void* ResolveService(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) override;
    virtual bool UnregisterService(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) override;
    virtual bool HasService(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) const override;
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
    
    /** Singleton instance of the service locator */
    static FCoreServiceLocator* Singleton;
    
    /** Thread-safe initialization flag for the singleton */
    static FThreadSafeBool bSingletonInitialized;
    
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
    
    /** Map of registered service providers */
    TArray<TScriptInterface<class IServiceProvider>> ServiceProviders;
    
    /** Fast-path cache for frequently used types (indexed by type hash) */
    TMap<uint32, FFastPathEntry> FastPathCache;
    
    /** Lock for accessing the fast-path cache */
    FCriticalSection FastPathLock;
    
    /** Read-write lock for thread-safe access to the service maps */
    mutable FRWLock ServiceMapLock;
    
    /** Thread-safe flag indicating if the locator has been initialized */
    FThreadSafeBool bIsInitialized;
    
    /** Counter for tracking fast-path cache hits */
    FThreadSafeCounter FastPathHits;
    
    /** Counter for tracking standard resolution fallbacks */
    FThreadSafeCounter StandardResolutionCount;
};
