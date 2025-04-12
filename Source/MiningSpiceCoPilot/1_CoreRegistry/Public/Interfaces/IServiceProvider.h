// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IServiceProvider.generated.h"

/**
 * Base interface for service providers in the SVO+SDF mining architecture
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UServiceProvider : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface that all service providers must implement to register with the service locator
 * Provides lifecycle management and dependency resolution
 */
class MININGSPICECOPILOT_API IServiceProvider
{
    GENERATED_BODY()

public:
    /**
     * Initialize the service provider
     * Called by service locator after registration
     * @return True if successfully initialized
     */
    virtual bool Initialize() = 0;
    
    /**
     * Shutdown the service provider
     * Called by service locator before unregistration
     */
    virtual void Shutdown() = 0;
    
    /**
     * Check if the service provider is initialized
     * @return True if initialized
     */
    virtual bool IsInitialized() const = 0;

    /**
     * Get service dependencies that must be registered first
     * @param OutDependencies Array to receive dependency interface types
     */
    virtual void GetDependencies(TArray<UClass*>& OutDependencies) const = 0;
};