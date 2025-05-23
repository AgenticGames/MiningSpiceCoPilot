// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "../../3_ThreadingTaskSystem/Public/TaskSystem/TaskTypes.h"
#include "../../3_ThreadingTaskSystem/Public/Interfaces/ITaskScheduler.h"
#include "IRegistry.generated.h"

// Using FTaskConfig from ITaskScheduler.h

// Use the enum definitions from TaskTypes.h instead of redefining them
// typedef ERegistryType TRegistryType;
// typedef ETypeCapabilities TTypeCapabilities;

/**
 * Type initialization stage for phased initialization
 */
enum class ETypeInitStage : uint8
{
	/** Memory allocation and basic setup */
	Allocation,
	
	/** Property initialization */
	Properties,
	
	/** Validation */
	Validation,
	
	/** Final initialization steps */
	Finalization
};

/**
 * Validation context for parallel validation
 */
struct MININGSPICECOPILOT_API FTypeValidationContext
{
	/** Validation errors */
	TArray<FString> Errors;
	
	/** Lock for thread-safe error collection */
	FCriticalSection ErrorLock;
	
	/** Adds a validation error with thread safety */
	void AddError(const FString& Error)
	{
		FScopeLock Lock(&ErrorLock);
		Errors.Add(Error);
	}
};

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
	
	/**
	 * Gets the registry type for this registry
	 * @return The registry type enum value
	 */
	virtual ERegistryType GetRegistryType() const = 0;
	
	/**
	 * Gets the capabilities of a specific type
	 * @param TypeId The ID of the type to query
	 * @return The basic capabilities of the type
	 */
	virtual ETypeCapabilities GetTypeCapabilities(uint32 TypeId) const = 0;
	
	/**
	 * Gets the extended capabilities of a specific type
	 * @param TypeId The ID of the type to query
	 * @return The extended capabilities of the type
	 */
	virtual ETypeCapabilitiesEx GetTypeCapabilitiesEx(uint32 TypeId) const = 0;
	
	/**
	 * Schedules a task that operates on a specific type
	 * @param TypeId The type ID
	 * @param TaskFunc The task function
	 * @param Config Optional task configuration
	 * @return The task ID
	 */
	virtual uint64 ScheduleTypeTask(uint32 TypeId, TFunction<void()> TaskFunc, const struct FTaskConfig& Config) = 0;
	
	/**
	 * Pre-initializes types before parallel initialization
	 * Performs setup necessary before full initialization
	 * @return True if pre-initialization was successful
	 */
	virtual bool PreInitializeTypes() = 0;
	
	/**
	 * Initializes types in parallel with dependency ordering
	 * @param bParallel Whether to initialize in parallel
	 * @return True if initialization was successful
	 */
	virtual bool ParallelInitializeTypes(bool bParallel = true) = 0;
	
	/**
	 * Performs post-initialization steps after parallel initialization
	 * @return True if post-initialization was successful
	 */
	virtual bool PostInitializeTypes() = 0;
	
	/**
	 * Gets dependencies for a specific type
	 * Used during parallel initialization to determine execution order
	 * @param TypeId The ID of the type to query
	 * @return Array of type IDs this type depends on
	 */
	virtual TArray<int32> GetTypeDependencies(uint32 TypeId) const = 0;
};
