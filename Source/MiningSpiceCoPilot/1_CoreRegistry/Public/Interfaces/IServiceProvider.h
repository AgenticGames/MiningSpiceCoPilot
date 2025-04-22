// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "../CommonServiceTypes.h"
#include "IServiceProvider.generated.h"

/**
 * Base interface for service providers in the SVO+SDF mining architecture
 * Provides the ability to register and manage service implementations
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UServiceProvider : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for service providers in the SVO+SDF mining architecture
 * Allows systems to provide services to the service locator
 */
class MININGSPICECOPILOT_API IServiceProvider
{
    GENERATED_BODY()

public:
    /**
     * Gets the service interfaces provided by this provider
     * @return Array of interface class types this provider supports
     */
    virtual TArray<TSubclassOf<UInterface>> GetProvidedServices() const = 0;
    
    /**
     * Registers all services with the provided service locator
     * @param InServiceLocator Service locator to register services with
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return True if registration was successful
     */
    virtual bool RegisterServices(class IServiceLocator* InServiceLocator, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) = 0;
    
    /**
     * Unregisters all services from the provided service locator
     * @param InServiceLocator Service locator to unregister services from
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return True if unregistration was successful
     */
    virtual bool UnregisterServices(class IServiceLocator* InServiceLocator, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) = 0;
    
    /**
     * Initializes all services provided by this provider
     * @return True if initialization was successful
     */
    virtual bool InitializeServices() = 0;
    
    /**
     * Shuts down all services provided by this provider
     */
    virtual void ShutdownServices() = 0;
    
    /**
     * Gets the name of this service provider for debugging
     * @return Provider name
     */
    virtual FName GetProviderName() const = 0;
    
    /**
     * Gets dependencies for services provided by this provider
     * @return Array of service dependencies
     */
    virtual TArray<FServiceDependency> GetServiceDependencies() const = 0;
    
    /**
     * Handles a specific lifecycle phase for services
     * @param Phase Current lifecycle phase
     * @return True if the phase was handled successfully
     */
    virtual bool HandleLifecyclePhase(EServiceLifecyclePhase Phase) = 0;
    
    /**
     * Gets the scope of services provided by this provider
     * @return Service scope
     */
    virtual EServiceScope GetServiceScope() const = 0;
    
    /**
     * Gets the health status for services provided by this provider
     * @return Health information for the services
     */
    virtual FServiceHealth GetServiceHealth() const = 0;
    
    /**
     * Attempts to recover services in a failed state
     * @return True if recovery was successful
     */
    virtual bool RecoverServices() = 0;
    
    /**
     * Gets the configuration for services provided by this provider
     * @return Service configuration
     */
    virtual FServiceConfig GetServiceConfig() const = 0;
    
    /**
     * Updates the configuration for services provided by this provider
     * @param InConfig New service configuration
     * @return True if configuration was successfully applied
     */
    virtual bool UpdateServiceConfig(const FServiceConfig& InConfig) = 0;
    
    /**
     * Validates that all service dependencies are available
     * @param InServiceLocator Service locator to validate dependencies against
     * @param OutMissingDependencies Output array of missing required dependencies
     * @return True if all required dependencies are available
     */
    virtual bool ValidateServiceDependencies(class IServiceLocator* InServiceLocator, TArray<FServiceDependency>& OutMissingDependencies) = 0;
    
    /**
     * Gets services that depend on services provided by this provider
     * @param InServiceLocator Service locator to check dependencies against
     * @return Array of dependent service interface types
     */
    virtual TArray<TSubclassOf<UInterface>> GetDependentServices(class IServiceLocator* InServiceLocator) = 0;
};