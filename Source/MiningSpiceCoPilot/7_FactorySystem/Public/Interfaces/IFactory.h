// IFactory.h
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IFactory.generated.h"

/**
 * Base interface for factory in the SVO+SDF mining architecture
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UMiningFactory : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for SVO+SDF component factories
 * Provides creation and management of mining system components with proper configuration
 */
class MININGSPICECOPILOT_API IMiningFactory
{
    GENERATED_BODY()

public:
    /**
     * Initialize the factory
     * @return True if successfully initialized
     */
    virtual bool Initialize() = 0;
    
    /**
     * Shutdown the factory and cleanup resources
     */
    virtual void Shutdown() = 0;
    
    /**
     * Check if the factory is initialized
     * @return True if initialized
     */
    virtual bool IsInitialized() const = 0;
    
    /**
     * Get the name of this factory
     * @return Name of the factory
     */
    virtual FName GetFactoryName() const = 0;
    
    /**
     * Check if this factory supports creating the specified type
     * @param ComponentType Component class to check for support
     * @return True if the factory can create this component type
     */
    virtual bool SupportsType(UClass* ComponentType) const = 0;
    
    /**
     * Create a component instance
     * @param ComponentType Component class to create
     * @param Parameters Optional parameters for component creation
     * @return New component instance or nullptr if creation failed
     */
    virtual UObject* CreateComponent(UClass* ComponentType, const TMap<FName, FString>& Parameters = TMap<FName, FString>()) = 0;
    
    /**
     * Template helper for type-safe component creation
     * @param Parameters Optional parameters for component creation
     * @return New typed component instance or nullptr if creation failed
     */
    template<typename ComponentType>
    ComponentType* CreateComponent(const TMap<FName, FString>& Parameters = TMap<FName, FString>())
    {
        return Cast<ComponentType>(CreateComponent(ComponentType::StaticClass(), Parameters));
    }
    
    /**
     * Get all component types supported by this factory
     * @return Array of supported component types
     */
    virtual TArray<UClass*> GetSupportedTypes() const = 0;
    
    /**
     * Register a component archetype to use as a template for creation
     * @param ComponentType Component class type
     * @param Archetype Template object for this component type
     * @return True if registration was successful
     */
    virtual bool RegisterArchetype(UClass* ComponentType, UObject* Archetype) = 0;
    
    /**
     * Check if the factory has a component instance pool
     * @param ComponentType Component class to check
     * @return True if pooling is enabled for this component type
     */
    virtual bool HasPool(UClass* ComponentType) const = 0;
    
    /**
     * Create and configure a component pool
     * @param ComponentType Component class to create a pool for
     * @param InitialSize Initial number of instances to pre-create in the pool
     * @param MaxSize Maximum number of instances the pool can contain
     * @param bEnablePooling Whether pooling should be enabled for this component type
     * @return True if pool was created successfully
     */
    virtual bool CreatePool(UClass* ComponentType, int32 InitialSize, int32 MaxSize, bool bEnablePooling = true) = 0;
    
    /**
     * Return a component to the pool for reuse
     * @param Component Component to return to the pool
     * @return True if the component was successfully returned to the pool
     */
    virtual bool ReturnToPool(UObject* Component) = 0;
    
    /**
     * Flush a component pool, destroying all pooled instances
     * @param ComponentType Component class type to flush
     * @return Number of instances that were flushed
     */
    virtual int32 FlushPool(UClass* ComponentType) = 0;
    
    /**
     * Get pool statistics for a component type
     * @param ComponentType Component class type to get statistics for
     * @param OutAvailable Number of available instances in the pool
     * @param OutTotal Total number of instances created by this pool
     * @return True if statistics were retrieved successfully
     */
    virtual bool GetPoolStats(UClass* ComponentType, int32& OutAvailable, int32& OutTotal) const = 0;
    
    /**
     * Get the singleton instance of the factory registry
     * @return Reference to the factory registry instance
     */
    static IMiningFactory& Get();
};
