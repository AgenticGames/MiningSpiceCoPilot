// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "CoreServiceLocator.generated.h"

/**
 * Interface for the core service locator
 * This is the main registry for core services in the Mining system
 */
UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UCoreServiceLocator : public UInterface
{
    GENERATED_BODY()
};

/**
 * Core service locator interface implementation
 */
class MININGSPICECOPILOT_API ICoreServiceLocator
{
    GENERATED_BODY()
public:
    /**
     * Registers a service with the locator
     * @param ServiceName - Name of the service
     * @param ServiceInstance - Instance of the service
     * @return True if registration was successful
     */
    virtual bool RegisterService(const FName& ServiceName, TScriptInterface<UObject> ServiceInstance) = 0;
    
    /**
     * Unregisters a service from the locator
     * @param ServiceName - Name of the service to unregister
     * @return True if unregistration was successful
     */
    virtual bool UnregisterService(const FName& ServiceName) = 0;
    
    /**
     * Retrieves a service from the locator
     * @param ServiceName - Name of the service to retrieve
     * @return The requested service instance or nullptr if not found
     */
    virtual TScriptInterface<UObject> GetService(const FName& ServiceName) = 0;
    
    /**
     * Checks if a service exists in the locator
     * @param ServiceName - Name of the service to check
     * @return True if the service exists
     */
    virtual bool HasService(const FName& ServiceName) const = 0;
    
    /**
     * Gets all registered service names
     * @return Array of service names
     */
    virtual TArray<FName> GetAllServiceNames() const = 0;
    
    /**
     * Gets the singleton instance of the service locator
     * @return Reference to the service locator interface
     */
    static ICoreServiceLocator& Get();
};
