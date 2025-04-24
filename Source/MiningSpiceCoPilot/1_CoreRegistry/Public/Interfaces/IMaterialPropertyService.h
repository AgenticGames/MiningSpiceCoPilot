// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IMaterialPropertyService.generated.h"

/**
 * Interface for material property services
 * Provides operations for managing material properties and attributes
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UMaterialPropertyService : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for material property management
 * Implementations handle property retrieval, modification and relationships
 */
class MININGSPICECOPILOT_API IMaterialPropertyService
{
    GENERATED_BODY()

public:
    /**
     * Gets the material type identifier this service manages
     * @return Material type identifier
     */
    virtual uint32 GetMaterialTypeId() const = 0;
    
    /**
     * Gets the property value by name
     * @param InPropertyName Name of the property to retrieve
     * @param OutValue Output string to receive the property value
     * @return True if the property was found and retrieved
     */
    virtual bool GetPropertyValue(const FName& InPropertyName, FString& OutValue) const = 0;
    
    /**
     * Sets the property value by name
     * @param InPropertyName Name of the property to set
     * @param InValue New value for the property
     * @return True if the property was set successfully
     */
    virtual bool SetPropertyValue(const FName& InPropertyName, const FString& InValue) = 0;
    
    /**
     * Checks if a property exists
     * @param InPropertyName Name of the property to check
     * @return True if the property exists
     */
    virtual bool HasProperty(const FName& InPropertyName) const = 0;
    
    /**
     * Gets all property names for this material type
     * @param OutPropertyNames Array to receive the property names
     * @return True if property names were retrieved successfully
     */
    virtual bool GetAllPropertyNames(TArray<FName>& OutPropertyNames) const = 0;
    
    /**
     * Gets the property data type
     * @param InPropertyName Name of the property
     * @param OutTypeName Output string to receive the type name
     * @return True if the property type was found
     */
    virtual bool GetPropertyType(const FName& InPropertyName, FName& OutTypeName) const = 0;
    
    /**
     * Gets the range constraints for numeric properties
     * @param InPropertyName Name of the property
     * @param OutMinValue Output parameter for minimum value
     * @param OutMaxValue Output parameter for maximum value
     * @return True if range constraints were retrieved
     */
    virtual bool GetPropertyRange(const FName& InPropertyName, double& OutMinValue, double& OutMaxValue) const = 0;
}; 