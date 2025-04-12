// IDependencyServiceProvider.h
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IDependencyServiceProvider.generated.h"

/**
 * Base interface for service providers in the SVO+SDF mining architecture
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UDependencyServiceProvider : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for specialized service providers in the SVO+SDF mining architecture
 * Provides domain-specific service registration and resolution
 */
class MININGSPICECOPILOT_API IDependencyServiceProvider
{
    GENERATED_BODY()

public:
    /**
     * Initialize the service provider
     * @return True if successfully initialized
     */
    virtual bool Initialize() = 0;
    
    /**
     * Shutdown the service provider and cleanup resources
     */
    virtual void Shutdown() = 0;
    
    /**
     * Check if the service provider is initialized
     * @return True if initialized
     */
    virtual bool IsInitialized() const = 0;
    
    /**
     * Get the name of this service provider
     * @return Name of the service provider
     */
    virtual FName GetProviderName() const = 0;
    
    /**
     * Register domain-specific services with the global service locator
     * @return True if all services were registered successfully
     */
    virtual bool RegisterServices() = 0;
    
    /**
     * Unregister all services provided by this provider
     */
    virtual void UnregisterServices() = 0;
    
    /**
     * Check if this provider supports a specific service type
     * @param InInterfaceType Interface type to check
     * @return True if the service type is supported
     */
    virtual bool SupportsServiceType(const UClass* InInterfaceType) const = 0;
    
    /**
     * Template helper for type-safe service type checking
     * @return True if the service type is supported
     */
    template<typename ServiceType>
    bool SupportsServiceType() const
    {
        return SupportsServiceType(ServiceType::StaticClass());
    }
    
    /**
     * Get all service types supported by this provider
     * @return Array of supported service types
     */
    virtual TArray<const UClass*> GetSupportedServiceTypes() const = 0;
    
    /**
     * Create a service instance of the specified type
     * @param InInterfaceType Interface type to create
     * @param InZoneID Optional zone ID for zone-specific services
     * @param InRegionID Optional region ID for region-specific services
     * @return Pointer to the created service or nullptr on failure
     */
    virtual void* CreateServiceInstance(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) = 0;
    
    /**
     * Template helper for type-safe service creation
     * @param InZoneID Optional zone ID for zone-specific services
     * @param InRegionID Optional region ID for region-specific services
     * @return Typed pointer to the created service or nullptr on failure
     */
    template<typename ServiceType>
    ServiceType* CreateServiceInstance(int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE)
    {
        return static_cast<ServiceType*>(CreateServiceInstance(ServiceType::StaticClass(), InZoneID, InRegionID));
    }
    
    /**
     * Get context-specific configuration for a service type
     * @param InInterfaceType Interface type to get configuration for
     * @param InZoneID Optional zone ID for zone-specific configuration
     * @param InRegionID Optional region ID for region-specific configuration
     * @return Configuration object or nullptr if not available
     */
    virtual const void* GetServiceConfiguration(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) const = 0;
    
    /**
     * Template helper for type-safe configuration access
     * @param InZoneID Optional zone ID for zone-specific configuration
     * @param InRegionID Optional region ID for region-specific configuration
     * @return Typed configuration object or nullptr if not available
     */
    template<typename ConfigType>
    const ConfigType* GetServiceConfiguration(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) const
    {
        return static_cast<const ConfigType*>(GetServiceConfiguration(InInterfaceType, InZoneID, InRegionID));
    }
};