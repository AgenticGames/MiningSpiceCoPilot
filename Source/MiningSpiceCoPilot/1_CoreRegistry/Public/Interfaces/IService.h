// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

/**
 * Base interface for all service implementations in the mining system
 * Services provide functionality that can be accessed through the service locator
 */
class MININGSPICECOPILOT_API IService
{
public:
    /** Virtual destructor */
    virtual ~IService() {}
    
    /**
     * Initializes the service
     * @return True if initialization was successful
     */
    virtual bool Initialize() = 0;
    
    /**
     * Shuts down the service
     */
    virtual void Shutdown() = 0;
    
    /**
     * Checks if the service is initialized
     * @return True if the service is initialized
     */
    virtual bool IsInitialized() const = 0;
    
    /**
     * Gets the name of this service
     * @return Name of the service
     */
    virtual FName GetServiceName() const = 0;
    
    /**
     * Gets the priority of this service
     * Higher priority services are preferred in service resolution
     * @return Priority value
     */
    virtual int32 GetPriority() const = 0;
}; 