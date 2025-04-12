// IComponentBuilder.h
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IComponentBuilder.generated.h"

/**
 * Base interface for component builder in the SVO+SDF mining architecture
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UComponentBuilder : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for building component instances with a fluent API
 * Provides configuration chaining for mining system components
 */
class MININGSPICECOPILOT_API IComponentBuilder
{
    GENERATED_BODY()

public:
    /**
     * Set a property value on the component being built
     * @param PropertyName Name of the property to set
     * @param Value String representation of the value to set
     * @return Builder instance for chaining
     */
    virtual IComponentBuilder* SetProperty(const FName& PropertyName, const FString& Value) = 0;
    
    /**
     * Set a numeric property on the component being built
     * @param PropertyName Name of the property to set
     * @param Value Numeric value to set
     * @return Builder instance for chaining
     */
    virtual IComponentBuilder* SetNumericProperty(const FName& PropertyName, double Value) = 0;
    
    /**
     * Set a boolean property on the component being built
     * @param PropertyName Name of the property to set
     * @param Value Boolean value to set
     * @return Builder instance for chaining
     */
    virtual IComponentBuilder* SetBoolProperty(const FName& PropertyName, bool Value) = 0;
    
    /**
     * Set a vector property on the component being built
     * @param PropertyName Name of the property to set
     * @param Value Vector value to set
     * @return Builder instance for chaining
     */
    virtual IComponentBuilder* SetVectorProperty(const FName& PropertyName, const FVector& Value) = 0;
    
    /**
     * Set a rotation property on the component being built
     * @param PropertyName Name of the property to set
     * @param Value Rotation value to set
     * @return Builder instance for chaining
     */
    virtual IComponentBuilder* SetRotatorProperty(const FName& PropertyName, const FRotator& Value) = 0;
    
    /**
     * Set an object reference property on the component being built
     * @param PropertyName Name of the property to set
     * @param Value Object reference to set
     * @return Builder instance for chaining
     */
    virtual IComponentBuilder* SetObjectProperty(const FName& PropertyName, UObject* Value) = 0;
    
    /**
     * Apply a predefined configuration to the component being built
     * @param ConfigName Name of the configuration to apply
     * @return Builder instance for chaining
     */
    virtual IComponentBuilder* ApplyConfiguration(const FName& ConfigName) = 0;
    
    /**
     * Configure the component based on its spatial location
     * @param Location World location for context-specific configuration
     * @param RegionId Region ID for region-specific configuration
     * @param ZoneId Zone ID for zone-specific configuration
     * @return Builder instance for chaining
     */
    virtual IComponentBuilder* ConfigureForLocation(const FVector& Location, int32 RegionId = INDEX_NONE, int32 ZoneId = INDEX_NONE) = 0;
    
    /**
     * Configure the component for a specific material type
     * @param MaterialTypeId Type ID of the material to configure for
     * @return Builder instance for chaining
     */
    virtual IComponentBuilder* ConfigureForMaterial(uint32 MaterialTypeId) = 0;
    
    /**
     * Add a child component to the component being built
     * @param ChildComponentType Type of child component to add
     * @param AttachmentSocket Optional socket name for attachment
     * @return Builder instance for the child component
     */
    virtual IComponentBuilder* AddChildComponent(UClass* ChildComponentType, const FName& AttachmentSocket = NAME_None) = 0;
    
    /**
     * Return to the parent builder when configuring a child component
     * @return Builder instance for the parent component
     */
    virtual IComponentBuilder* FinishChild() = 0;
    
    /**
     * Register a completion callback to be invoked after the component is built
     * @param Callback Function to call when the component is built
     * @return Builder instance for chaining
     */
    virtual IComponentBuilder* OnComplete(TFunction<void(UObject*)> Callback) = 0;
    
    /**
     * Build the component with the configured properties
     * @return The built component instance
     */
    virtual UObject* Build() = 0;
    
    /**
     * Template helper for type-safe component building
     * @return The built component instance with proper type
     */
    template<typename ComponentType>
    ComponentType* Build()
    {
        return Cast<ComponentType>(Build());
    }
    
    /**
     * Build the component and register it with the provided outer object
     * @param Outer Outer object to register the component with
     * @return The built component instance
     */
    virtual UObject* BuildWithOuter(UObject* Outer) = 0;
    
    /**
     * Template helper for type-safe component building with outer
     * @param Outer Outer object to register the component with
     * @return The built component instance with proper type
     */
    template<typename ComponentType>
    ComponentType* BuildWithOuter(UObject* Outer)
    {
        return Cast<ComponentType>(BuildWithOuter(Outer));
    }
    
    /**
     * Create a new component builder for the specified component type
     * @param ComponentType Type of component to build
     * @param UsePooling Whether to use component pooling when building
     * @return New component builder instance
     */
    static TSharedPtr<IComponentBuilder> CreateBuilder(UClass* ComponentType, bool UsePooling = true);
    
    /**
     * Template helper for type-safe builder creation
     * @param UsePooling Whether to use component pooling when building
     * @return New component builder instance with proper type
     */
    template<typename ComponentType>
    static TSharedPtr<IComponentBuilder> CreateBuilder(bool UsePooling = true)
    {
        return CreateBuilder(ComponentType::StaticClass(), UsePooling);
    }
};
