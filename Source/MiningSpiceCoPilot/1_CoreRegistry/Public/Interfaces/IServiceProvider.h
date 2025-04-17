// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
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
};