// ServiceProvider.cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "ServiceProvider.h"
#include "Interfaces/IDependencyServiceLocator.h"
#include "Interfaces/IServiceMonitor.h"
#include "Logging/LogMacros.h"

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogServiceProvider, Log, All);

FServiceProvider::FServiceProvider(const FName& InProviderName)
    : ProviderName(InProviderName)
    , bIsInitialized(false)
{
    // Initialize empty
}

FServiceProvider::~FServiceProvider()
{
    // Ensure we're shut down
    if (bIsInitialized)
    {
        Shutdown();
    }
}

bool FServiceProvider::Initialize()
{
    FScopeLock Lock(&CriticalSection);
    
    if (bIsInitialized)
    {
        UE_LOG(LogServiceProvider, Warning, TEXT("ServiceProvider '%s' already initialized"), *ProviderName.ToString());
        return true;
    }
    
    UE_LOG(LogServiceProvider, Log, TEXT("Initializing ServiceProvider '%s'"), *ProviderName.ToString());
    bIsInitialized = true;
    
    return true;
}

void FServiceProvider::Shutdown()
{
    FScopeLock Lock(&CriticalSection);
    
    if (!bIsInitialized)
    {
        UE_LOG(LogServiceProvider, Warning, TEXT("ServiceProvider '%s' not initialized, cannot shutdown"), *ProviderName.ToString());
        return;
    }
    
    UE_LOG(LogServiceProvider, Log, TEXT("Shutting down ServiceProvider '%s'"), *ProviderName.ToString());
    
    // Unregister services
    UnregisterServices();
    
    // Clean up created services
    for (void* ServicePtr : CreatedServices)
    {
        if (ServicePtr)
        {
            // Note: Since we're just storing void*, we can't safely delete
            // This is why proper ownership semantics should be established
            // Derived classes should handle proper cleanup
            UE_LOG(LogServiceProvider, Warning, TEXT("Service created by provider '%s' cannot be safely deleted"),
                *ProviderName.ToString());
        }
    }
    
    CreatedServices.Empty();
    ServiceFactories.Empty();
    ServiceConfigurations.Empty();
    bIsInitialized = false;
}

bool FServiceProvider::IsInitialized() const
{
    return bIsInitialized;
}

FName FServiceProvider::GetProviderName() const
{
    return ProviderName;
}

bool FServiceProvider::RegisterServices()
{
    if (!bIsInitialized)
    {
        UE_LOG(LogServiceProvider, Error, TEXT("ServiceProvider '%s' not initialized, cannot register services"), *ProviderName.ToString());
        return false;
    }
    
    UE_LOG(LogServiceProvider, Log, TEXT("ServiceProvider '%s' registering services"), *ProviderName.ToString());
    
    // This is a base implementation that derived classes should override
    // We'll return success since there's nothing to do in the base class
    return true;
}

void FServiceProvider::UnregisterServices()
{
    if (!bIsInitialized)
    {
        UE_LOG(LogServiceProvider, Error, TEXT("ServiceProvider '%s' not initialized, cannot unregister services"), *ProviderName.ToString());
        return;
    }
    
    UE_LOG(LogServiceProvider, Log, TEXT("ServiceProvider '%s' unregistering services"), *ProviderName.ToString());
    
    // This is a base implementation that derived classes should override
    // Nothing to do in the base class
}

bool FServiceProvider::SupportsServiceType(const UClass* InInterfaceType) const
{
    FScopeLock Lock(&CriticalSection);
    
    if (!InInterfaceType)
    {
        return false;
    }
    
    return ServiceFactories.Contains(InInterfaceType);
}

TArray<const UClass*> FServiceProvider::GetSupportedServiceTypes() const
{
    FScopeLock Lock(&CriticalSection);
    
    TArray<const UClass*> Result;
    ServiceFactories.GetKeys(Result);
    
    return Result;
}

void* FServiceProvider::CreateServiceInstance(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID)
{
    if (!InInterfaceType)
    {
        UE_LOG(LogServiceProvider, Error, TEXT("Cannot create service with null interface type"));
        return nullptr;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    if (!bIsInitialized)
    {
        UE_LOG(LogServiceProvider, Error, TEXT("ServiceProvider '%s' not initialized, cannot create service instance for interface %s"),
            *ProviderName.ToString(), *InInterfaceType->GetName());
        return nullptr;
    }
    
    // Check if we support this service type
    TFunction<void*(int32, int32)>* Factory = ServiceFactories.Find(InInterfaceType);
    if (!Factory)
    {
        UE_LOG(LogServiceProvider, Warning, TEXT("ServiceProvider '%s' does not support service type %s"),
            *ProviderName.ToString(), *InInterfaceType->GetName());
        return nullptr;
    }
    
    // Create the service instance
    void* ServiceInstance = (*Factory)(InZoneID, InRegionID);
    if (ServiceInstance)
    {
        // Track the service for cleanup
        CreatedServices.Add(ServiceInstance);
        
        UE_LOG(LogServiceProvider, Log, TEXT("ServiceProvider '%s' created service instance of type %s for Zone %d Region %d"),
            *ProviderName.ToString(), *InInterfaceType->GetName(), InZoneID, InRegionID);
        
        // Register the service with the service locator
        IDependencyServiceLocator& ServiceLocator = IDependencyServiceLocator::Get();
        if (ServiceLocator.IsInitialized())
        {
            ServiceLocator.RegisterService(ServiceInstance, InInterfaceType, InZoneID, InRegionID);
        }
        
        // Register the service for health monitoring
        IServiceMonitor& ServiceMonitor = IServiceMonitor::Get();
        if (ServiceMonitor.IsInitialized())
        {
            // Use medium importance by default
            ServiceMonitor.RegisterServiceForMonitoring(InInterfaceType, 0.5f, InZoneID, InRegionID);
        }
    }
    else
    {
        UE_LOG(LogServiceProvider, Error, TEXT("ServiceProvider '%s' failed to create service instance of type %s for Zone %d Region %d"),
            *ProviderName.ToString(), *InInterfaceType->GetName(), InZoneID, InRegionID);
    }
    
    return ServiceInstance;
}

const void* FServiceProvider::GetServiceConfiguration(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID) const
{
    if (!InInterfaceType)
    {
        return nullptr;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    if (!bIsInitialized)
    {
        return nullptr;
    }
    
    // Check if we have configurations for this service type
    const TMap<TPair<int32, int32>, const void*>* Configurations = ServiceConfigurations.Find(InInterfaceType);
    if (!Configurations)
    {
        return nullptr;
    }
    
    // First try exact match
    TPair<int32, int32> ExactKey(InZoneID, InRegionID);
    if (const void* const* ExactConfig = Configurations->Find(ExactKey))
    {
        return *ExactConfig;
    }
    
    // For zone-specific configurations, try global configuration if zone-specific not found
    if (InZoneID != INDEX_NONE)
    {
        TPair<int32, int32> ZoneGlobalKey(INDEX_NONE, InRegionID);
        if (const void* const* ZoneGlobalConfig = Configurations->Find(ZoneGlobalKey))
        {
            return *ZoneGlobalConfig;
        }
    }
    
    // For region-specific configurations, try global configuration if region-specific not found
    if (InRegionID != INDEX_NONE)
    {
        TPair<int32, int32> RegionGlobalKey(InZoneID, INDEX_NONE);
        if (const void* const* RegionGlobalConfig = Configurations->Find(RegionGlobalKey))
        {
            return *RegionGlobalConfig;
        }
    }
    
    // Finally, try completely global configuration
    if (InZoneID != INDEX_NONE || InRegionID != INDEX_NONE)
    {
        TPair<int32, int32> GlobalKey(INDEX_NONE, INDEX_NONE);
        if (const void* const* GlobalConfig = Configurations->Find(GlobalKey))
        {
            return *GlobalConfig;
        }
    }
    
    // Configuration not found
    return nullptr;
}