// ServiceProvider.h
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IDependencyServiceProvider.h"
#include "HAL/CriticalSection.h"
#include "Containers/Map.h"
#include "Containers/Set.h"

/**
 * Base implementation of IDependencyServiceProvider interface
 * Abstract base class with common functionality for service providers
 */
class FServiceProvider : public IDependencyServiceProvider
{
public:
    /** Constructor */
    FServiceProvider(const FName& InProviderName);
    
    /** Destructor */
    virtual ~FServiceProvider();

    //~ Begin IDependencyServiceProvider Interface
    virtual bool Initialize() override;
    virtual void Shutdown() override;
    virtual bool IsInitialized() const override;
    virtual FName GetProviderName() const override;
    virtual bool RegisterServices() override;
    virtual void UnregisterServices() override;
    virtual bool SupportsServiceType(const UClass* InInterfaceType) const override;
    virtual TArray<const UClass*> GetSupportedServiceTypes() const override;
    virtual void* CreateServiceInstance(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) override;
    virtual const void* GetServiceConfiguration(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) const override;
    //~ End IDependencyServiceProvider Interface

protected:
    /** Name of this provider */
    FName ProviderName;
    
    /** Flag indicating if the provider is initialized */
    bool bIsInitialized;
    
    /** Map of supported service types to factory functions */
    TMap<const UClass*, TFunction<void*(int32, int32)>> ServiceFactories;
    
    /** Map of service configurations */
    TMap<const UClass*, TMap<TPair<int32, int32>, const void*>> ServiceConfigurations;
    
    /** Set of created services (for cleanup) */
    TSet<void*> CreatedServices;
    
    /** Critical section for thread safety */
    mutable FCriticalSection CriticalSection;
    
    /**
     * Register a service factory function
     * @param InInterfaceType Interface type class for the service
     * @param InFactoryFunc Factory function that creates service instances
     */
    template<typename ServiceType>
    void RegisterServiceFactory(const UClass* InInterfaceType, TFunction<ServiceType*(int32, int32)> InFactoryFunc)
    {
        ServiceFactories.Add(InInterfaceType, [InFactoryFunc](int32 ZoneID, int32 RegionID) -> void* {
            return static_cast<void*>(InFactoryFunc(ZoneID, RegionID));
        });
    }
    
    /**
     * Register service configuration
     * @param InInterfaceType Interface type class for the service
     * @param InConfig Configuration object
     * @param InZoneID Optional zone ID for zone-specific configuration
     * @param InRegionID Optional region ID for region-specific configuration
     */
    template<typename ConfigType>
    void RegisterServiceConfiguration(const UClass* InInterfaceType, const ConfigType* InConfig, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE)
    {
        if (!ServiceConfigurations.Contains(InInterfaceType))
        {
            ServiceConfigurations.Add(InInterfaceType, TMap<TPair<int32, int32>, const void*>());
        }
        
        TPair<int32, int32> Key(InZoneID, InRegionID);
        ServiceConfigurations[InInterfaceType].Add(Key, InConfig);
    }
};