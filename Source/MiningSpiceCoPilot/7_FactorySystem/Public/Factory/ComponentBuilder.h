// ComponentBuilder.h
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ComponentBuilder.generated.h"

class IComponentPoolManager;

// Define the IComponentBuilder interface directly
UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UComponentBuilder : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for component builders using the fluent builder pattern
 */
class MININGSPICECOPILOT_API IComponentBuilder
{
    GENERATED_BODY()

public:
    /** Set a string property on the component */
    virtual IComponentBuilder* SetProperty(const FName& PropertyName, const FString& Value) = 0;
    
    /** Set a numeric property on the component */
    virtual IComponentBuilder* SetNumericProperty(const FName& PropertyName, double Value) = 0;
    
    /** Set a boolean property on the component */
    virtual IComponentBuilder* SetBoolProperty(const FName& PropertyName, bool Value) = 0;
    
    /** Set a vector property on the component */
    virtual IComponentBuilder* SetVectorProperty(const FName& PropertyName, const FVector& Value) = 0;
    
    /** Set a rotator property on the component */
    virtual IComponentBuilder* SetRotatorProperty(const FName& PropertyName, const FRotator& Value) = 0;
    
    /** Set an object property on the component */
    virtual IComponentBuilder* SetObjectProperty(const FName& PropertyName, UObject* Value) = 0;
    
    /** Apply a named configuration to the component */
    virtual IComponentBuilder* ApplyConfiguration(const FName& ConfigName) = 0;
    
    /** Configure the component for a specific spatial location */
    virtual IComponentBuilder* ConfigureForLocation(const FVector& Location, int32 RegionId = INDEX_NONE, int32 ZoneId = INDEX_NONE) = 0;
    
    /** Configure the component for a specific material type */
    virtual IComponentBuilder* ConfigureForMaterial(uint32 MaterialTypeId) = 0;
    
    /** Add a child component to the component being built */
    virtual IComponentBuilder* AddChildComponent(UClass* ChildComponentType, const FName& AttachmentSocket = NAME_None) = 0;
    
    /** Finish configuring the current child component and return to the parent */
    virtual IComponentBuilder* FinishChild() = 0;
    
    /** Register a callback to be called when the component is fully built */
    virtual IComponentBuilder* OnComplete(TFunction<void(UObject*)> Callback) = 0;
    
    /** Build and return the configured component */
    virtual UObject* Build() = 0;
    
    /** Build the component with a specific outer object */
    virtual UObject* BuildWithOuter(UObject* Outer) = 0;
};

/**
 * Implements the fluent builder pattern for component creation
 * Provides a chainable API for configuring component properties
 */
UCLASS()
class MININGSPICECOPILOT_API UComponentBuilderImpl : public UObject, public IComponentBuilder
{
    GENERATED_BODY()

public:
    UComponentBuilderImpl();

    //~ Begin IComponentBuilder Interface
    virtual IComponentBuilder* SetProperty(const FName& PropertyName, const FString& Value) override;
    virtual IComponentBuilder* SetNumericProperty(const FName& PropertyName, double Value) override;
    virtual IComponentBuilder* SetBoolProperty(const FName& PropertyName, bool Value) override;
    virtual IComponentBuilder* SetVectorProperty(const FName& PropertyName, const FVector& Value) override;
    virtual IComponentBuilder* SetRotatorProperty(const FName& PropertyName, const FRotator& Value) override;
    virtual IComponentBuilder* SetObjectProperty(const FName& PropertyName, UObject* Value) override;
    virtual IComponentBuilder* ApplyConfiguration(const FName& ConfigName) override;
    virtual IComponentBuilder* ConfigureForLocation(const FVector& Location, int32 RegionId = INDEX_NONE, int32 ZoneId = INDEX_NONE) override;
    virtual IComponentBuilder* ConfigureForMaterial(uint32 MaterialTypeId) override;
    virtual IComponentBuilder* AddChildComponent(UClass* ChildComponentType, const FName& AttachmentSocket = NAME_None) override;
    virtual IComponentBuilder* FinishChild() override;
    virtual IComponentBuilder* OnComplete(TFunction<void(UObject*)> Callback) override;
    virtual UObject* Build() override;
    virtual UObject* BuildWithOuter(UObject* Outer) override;
    //~ End IComponentBuilder Interface

    /**
     * Initialize the builder with a component type
     * @param InComponentType Component class to build
     * @param InUsePooling Whether to use component pooling
     * @return True if initialization was successful
     */
    bool Initialize(UClass* InComponentType, bool InUsePooling);

    /**
     * Set the pool manager to use for pooled components
     * @param InPoolManager Pool manager instance
     */
    void SetPoolManager(TScriptInterface<IComponentPoolManager> InPoolManager);

    /**
     * Create a builder with the specified component type
     * @param ComponentType Component class to build
     * @param UsePooling Whether to use component pooling
     * @return New builder instance
     */
    static TSharedPtr<UComponentBuilderImpl> CreateBuilder(UClass* ComponentType, bool UsePooling = true);

protected:
    /** Component type to build */
    UPROPERTY()
    UClass* ComponentType;

    /** Property values to apply */
    UPROPERTY()
    TMap<FName, FString> PropertyValues;

    /** Numeric property values to apply */
    UPROPERTY()
    TMap<FName, double> NumericValues;

    /** Boolean property values to apply */
    UPROPERTY()
    TMap<FName, bool> BoolValues;

    /** Vector property values to apply */
    UPROPERTY()
    TMap<FName, FVector> VectorValues;

    /** Rotator property values to apply */
    UPROPERTY()
    TMap<FName, FRotator> RotatorValues;

    /** Object reference property values to apply */
    UPROPERTY()
    TMap<FName, UObject*> ObjectValues;

    /** Whether to use pooling when building the component */
    UPROPERTY()
    bool bUsePooling;

    /** Reference to the parent builder if this is a child builder */
    TSharedPtr<UComponentBuilderImpl> ParentBuilder;

    /** Child builders for nested components */
    TArray<TSharedPtr<UComponentBuilderImpl>> ChildBuilders;

    /** Attachment socket names for child components */
    TMap<TSharedPtr<UComponentBuilderImpl>, FName> ChildAttachmentSockets;

    /** Pool manager reference */
    UPROPERTY()
    TScriptInterface<IComponentPoolManager> PoolManager;

    /** Completion callback */
    TFunction<void(UObject*)> CompletionCallback;

    /** Location for spatial configuration */
    UPROPERTY()
    FVector SpatialLocation;

    /** Region ID for spatial configuration */
    UPROPERTY()
    int32 RegionId;

    /** Zone ID for spatial configuration */
    UPROPERTY()
    int32 ZoneId;

    /** Material type ID for material configuration */
    UPROPERTY()
    uint32 MaterialTypeId;

    /** Applied configuration names */
    UPROPERTY()
    TArray<FName> AppliedConfigurations;

    /**
     * Create a component instance
     * @param Outer Optional outer object
     * @return New component instance
     */
    UObject* CreateInstance(UObject* Outer = nullptr);

    /**
     * Apply all configured properties to the component
     * @param Component Component to configure
     * @return True if successful
     */
    bool ApplyProperties(UObject* Component);

    /**
     * Create and attach child components
     * @param Parent Parent component
     * @return True if successful
     */
    bool CreateChildComponents(UObject* Parent);

    /**
     * Apply a named configuration from the configuration system
     * @param Component Component to configure
     * @param ConfigName Configuration name to apply
     * @return True if successful
     */
    bool ApplyNamedConfiguration(UObject* Component, const FName& ConfigName);

    /**
     * Apply spatial configuration
     * @param Component Component to configure
     * @return True if successful
     */
    bool ApplySpatialConfiguration(UObject* Component);

    /**
     * Apply material configuration
     * @param Component Component to configure
     * @return True if successful
     */
    bool ApplyMaterialConfiguration(UObject* Component);
};