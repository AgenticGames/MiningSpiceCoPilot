// IRegistry.h
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IRegistry.generated.h"

/**
 * Base interface for type registry systems in the SVO+SDF hybrid mining architecture
 * Provides registration, lookup, and management functionality for various subsystems
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class URegistry : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for registry objects that handle type registration, lookup, and management
 * This is the base interface implemented by all registry types in the mining system
 */
class MININGSPICECOPILOT_API IRegistry
{
    GENERATED_BODY()

public:
    /**
     * Initialize the registry
     * @return True if successfully initialized
     */
    virtual bool Initialize() = 0;
    
    /**
     * Shutdown the registry and cleanup resources
     */
    virtual void Shutdown() = 0;
    
    /**
     * Check if the registry is initialized
     * @return True if initialized
     */
    virtual bool IsInitialized() const = 0;
    
    /**
     * Get the name of this registry
     * @return Registry name
     */
    virtual FName GetRegistryName() const = 0;
    
    /**
     * Get the schema version of this registry
     * @return Schema version number
     */
    virtual uint32 GetSchemaVersion() const = 0;
    
    /**
     * Validate the registry consistency
     * @param OutErrors Array to populate with error messages if validation fails
     * @return True if registry is valid
     */
    virtual bool Validate(TArray<FString>& OutErrors) const = 0;
    
    /**
     * Clear all registrations and reset the registry
     */
    virtual void Clear() = 0;
};
