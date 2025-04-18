// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IRegistry.generated.h"

/**
 * Base interface for all registry types in the SVO+SDF mining architecture
 * Provides common functionality for type registration and management
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class URegistry : public UInterface
{
	GENERATED_BODY()
};

/**
 * Interface for type registries in the SVO+SDF mining architecture
 * Defines core functionality for type registration, lookup, and management
 */
class MININGSPICECOPILOT_API IRegistry
{
	GENERATED_BODY()

public:
	/**
	 * Initializes the registry and prepares it for use
	 * @return True if initialization was successful
	 */
	virtual bool Initialize() = 0;

	/**
	 * Shuts down the registry and cleans up resources
	 */
	virtual void Shutdown() = 0;
	
	/**
	 * Checks if the registry has been initialized
	 * @return True if initialized, false otherwise
	 */
	virtual bool IsInitialized() const = 0;
	
	/**
	 * Gets the name of this registry
	 * @return The name of the registry
	 */
	virtual FName GetRegistryName() const = 0;
	
	/**
	 * Gets the version of this registry's schema
	 * @return The schema version
	 */
	virtual uint32 GetSchemaVersion() const = 0;
	
	/**
	 * Validates that the registry is in a consistent state
	 * @param OutErrors Collection of errors found during validation
	 * @return True if valid, false if errors were found
	 */
	virtual bool Validate(TArray<FString>& OutErrors) const = 0;
	
	/**
	 * Clears all registrations and resets the registry to its initial state
	 */
	virtual void Clear() = 0;
	
	/**
	 * Sets the version for a specific type and handles memory migration
	 * Integrates with MemoryPoolManager::UpdateTypeVersion for memory state management
	 * 
	 * @param TypeId The ID of the type to update
	 * @param NewVersion The new version for the type
	 * @param bMigrateInstanceData Whether to migrate existing instance data to the new version
	 * @return True if the version was successfully updated
	 */
	virtual bool SetTypeVersion(uint32 TypeId, uint32 NewVersion, bool bMigrateInstanceData = true) = 0;
	
	/**
	 * Gets the current version of a specific type
	 * 
	 * @param TypeId The ID of the type to query
	 * @return The current version of the type, or 0 if not found
	 */
	virtual uint32 GetTypeVersion(uint32 TypeId) const = 0;
};
