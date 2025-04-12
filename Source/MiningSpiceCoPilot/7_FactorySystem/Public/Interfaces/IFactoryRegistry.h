// IFactoryRegistry.h
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IFactoryRegistry.generated.h"

// Forward declarations
class IFactory;
class IComponentBuilder;

/**
 * Base interface for factory registry in the SVO+SDF mining architecture
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UFactoryRegistry : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for managing multiple specialized factories in the SVO+SDF mining architecture
 * Provides centralized factory registration and resolution
 */
class MININGSPICECOPILOT_API IFactoryRegistry
{
    GENERATED_BODY()

public:
    /**
     * Initialize the factory registry
     * @return True if successfully initialized
     */
    virtual bool Initialize() = 0;
    
    /**
     * Shutdown the factory registry and cleanup resources
     */
    virtual void Shutdown() = 0;
    
    /**
     * Check if the factory registry is initialized
     * @return True if initialized
     */
    virtual bool IsInitialized() const = 0;
    
    /**
     * Register a factory with the registry
     * @param Factory Factory to register
     * @return True if registration was successful
     */
    virtual bool RegisterFactory(IFactory* Factory) = 0;
    
    /**
     * Unregister a factory from the registry
     * @param FactoryName Name of the factory to unregister
     * @return True if the factory was unregistered
     */
    virtual bool UnregisterFactory(const FName& FactoryName) = 0;
    
    /**
     * Find a factory by name
     * @param FactoryName Name of the factory to find
     * @return Factory instance or nullptr if not found
     */
    virtual IFactory* FindFactory(const FName& FactoryName) const = 0;
    
    /**
     * Get all registered factories
     * @return Array of registered factories
     */
    virtual TArray<IFactory*> GetAllFactories() const = 0;
    
    /**
     * Find a factory that can create the specified component type
     * @param ComponentType Component class to find a factory for
     * @return Factory that can create the component or nullptr if none found
     */
    virtual IFactory* FindFactoryForType(UClass* ComponentType) const = 0;
    
    /**
     * Create a component instance using the appropriate factory
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
     * Create a component instance using a specific factory
     * @param FactoryName Name of the factory to use
     * @param ComponentType Component class to create
     * @param Parameters Optional parameters for component creation
     * @return New component instance or nullptr if creation failed
     */
    virtual UObject* CreateComponentWithFactory(const FName& FactoryName, UClass* ComponentType, const TMap<FName, FString>& Parameters = TMap<FName, FString>()) = 0;
    
    /**
     * Template helper for type-safe component creation with a specific factory
     * @param FactoryName Name of the factory to use
     * @param Parameters Optional parameters for component creation
     * @return New typed component instance or nullptr if creation failed
     */
    template<typename ComponentType>
    ComponentType* CreateComponentWithFactory(const FName& FactoryName, const TMap<FName, FString>& Parameters = TMap<FName, FString>())
    {
        return Cast<ComponentType>(CreateComponentWithFactory(FactoryName, ComponentType::StaticClass(), Parameters));
    }
    
    /**
     * Create a component builder for the specified component type
     * @param ComponentType Component class to create a builder for
     * @param UsePooling Whether to use component pooling when building
     * @return New component builder instance
     */
    virtual TSharedPtr<IComponentBuilder> CreateBuilder(UClass* ComponentType, bool UsePooling = true) = 0;
    
    /**
     * Template helper for type-safe builder creation
     * @param UsePooling Whether to use component pooling when building
     * @return New component builder instance with proper type
     */
    template<typename ComponentType>
    TSharedPtr<IComponentBuilder> CreateBuilder(bool UsePooling = true)
    {
        return CreateBuilder(ComponentType::StaticClass(), UsePooling);
    }
    
    /**
     * Return a component to its factory's pool
     * @param Component Component to return to the pool
     * @return True if the component was successfully returned to the pool
     */
    virtual bool ReturnToPool(UObject* Component) = 0;
    
    /**
     * Configure global pooling parameters for all factories
     * @param bEnableGlobalPooling Whether pooling should be enabled globally
     * @param DefaultMaxPoolSize Default maximum size for component pools
     */
    virtual void ConfigurePooling(bool bEnableGlobalPooling, int32 DefaultMaxPoolSize) = 0;
    
    /**
     * Get the singleton instance of the factory registry
     * @return Reference to the factory registry instance
     */
    static IFactoryRegistry& Get();
};
