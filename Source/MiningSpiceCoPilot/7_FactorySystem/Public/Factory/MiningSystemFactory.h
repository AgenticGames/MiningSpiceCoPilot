// MiningSystemFactory.h
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IFactory.h"
#include "MiningSystemFactory.generated.h"

class IComponentPoolManager;
class IComponentBuilder;

/**
 * Core factory implementation for SVO+SDF mining system components
 * Provides creation services with proper type safety and configuration
 */
UCLASS()
class MININGSPICECOPILOT_API UMiningSystemFactory : public UObject, public IMiningFactory
{
    GENERATED_BODY()

public:
    UMiningSystemFactory();

    //~ Begin IMiningFactory Interface
    virtual bool Initialize() override;
    virtual void Shutdown() override;
    virtual bool IsInitialized() const override;
    virtual FName GetFactoryName() const override;
    virtual bool SupportsType(UClass* ComponentType) const override;
    virtual UObject* CreateComponent(UClass* ComponentType, const TMap<FName, FString>& Parameters = TMap<FName, FString>()) override;
    virtual TArray<UClass*> GetSupportedTypes() const override;
    virtual bool RegisterArchetype(UClass* ComponentType, UObject* Archetype) override;
    virtual bool HasPool(UClass* ComponentType) const override;
    virtual bool CreatePool(UClass* ComponentType, int32 InitialSize, int32 MaxSize, bool bEnablePooling = true) override;
    virtual bool ReturnToPool(UObject* Component) override;
    virtual int32 FlushPool(UClass* ComponentType) override;
    virtual bool GetPoolStats(UClass* ComponentType, int32& OutAvailable, int32& OutTotal) const override;
    //~ End IMiningFactory Interface

    /**
     * Register a component type to be created by this factory
     * @param ComponentType Component class to register
     * @return True if registration was successful
     */
    bool RegisterComponentType(UClass* ComponentType);

    /**
     * Register multiple component types at once
     * @param ComponentTypes Array of component classes to register
     * @return Number of successfully registered types
     */
    int32 RegisterComponentTypes(const TArray<UClass*>& ComponentTypes);

    /**
     * Unregister a component type from this factory
     * @param ComponentType Component class to unregister
     * @return True if successfully unregistered
     */
    bool UnregisterComponentType(UClass* ComponentType);

    /**
     * Creates a builder for configuring and creating a component
     * @param ComponentType Component class to create a builder for
     * @param UsePooling Whether to use component pooling when building
     * @return New component builder instance
     */
    TSharedPtr<IComponentBuilder> CreateBuilder(UClass* ComponentType, bool UsePooling = true);

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
     * Get singleton instance
     * @return Singleton factory instance
     */
    static UMiningSystemFactory* Get();

protected:
    /** Pool manager reference */
    UPROPERTY()
    TScriptInterface<IComponentPoolManager> PoolManager;

    /** Registered component types */
    UPROPERTY()
    TSet<UClass*> SupportedTypes;

    /** Component archetypes */
    UPROPERTY()
    TMap<UClass*, UObject*> Archetypes;

    /** Whether the factory is initialized */
    UPROPERTY()
    bool bIsInitialized;

    /** Factory name */
    UPROPERTY()
    FName FactoryName;

    /**
     * Create a new component instance
     * @param ComponentType Component type to create
     * @param Outer Outer object for component
     * @param Name Optional name for the component
     * @return New component instance
     */
    UObject* CreateComponentInstance(UClass* ComponentType, UObject* Outer, FName Name);

    /**
     * Configure a component with parameters
     * @param Component Component to configure
     * @param Parameters Parameter map with configuration values
     * @return True if configuration was successful
     */
    bool ConfigureComponent(UObject* Component, const TMap<FName, FString>& Parameters);
};