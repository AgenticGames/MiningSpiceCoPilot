// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IMaterialFieldOperator.generated.h"

/**
 * Interface for material field operations
 * Provides operations for manipulating material fields and properties
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UMaterialFieldOperator : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for material field operators
 * Implementations handle procedural operations on material data
 */
class MININGSPICECOPILOT_API IMaterialFieldOperator
{
    GENERATED_BODY()

public:
    /**
     * Gets the material type identifier this operator works with
     * @return Material type identifier
     */
    virtual uint32 GetMaterialTypeId() const = 0;
    
    /**
     * Gets the channel identifier this operator works with
     * @return Channel identifier
     */
    virtual int32 GetChannelId() const = 0;
    
    /**
     * Apply the field operation within a bounded region
     * @param InBounds The region bounds to operate within
     * @param InStrength Operation strength/intensity
     * @return True if the operation was applied successfully
     */
    virtual bool ApplyOperation(const FBox& InBounds, float InStrength = 1.0f) = 0;
    
    /**
     * Apply the field operation at a specific point
     * @param InLocation The center point of the operation
     * @param InRadius The radius of effect
     * @param InStrength Operation strength/intensity
     * @return True if the operation was applied successfully
     */
    virtual bool ApplyPointOperation(const FVector& InLocation, float InRadius, float InStrength = 1.0f) = 0;
    
    /**
     * Query the field value at a specific point
     * @param InLocation The point to query
     * @param OutValue The resulting field value
     * @return True if the query was successful
     */
    virtual bool QueryFieldValue(const FVector& InLocation, float& OutValue) const = 0;
    
    /**
     * Gets the expected value range for this field
     * @param OutMinValue Output parameter for minimum value
     * @param OutMaxValue Output parameter for maximum value
     * @return True if range was retrieved successfully
     */
    virtual bool GetValueRange(float& OutMinValue, float& OutMaxValue) const = 0;
    
    /**
     * Gets the default value for this field
     * @return Default field value
     */
    virtual float GetDefaultValue() const = 0;
    
    /**
     * Gets the field operation capabilities
     * @param OutSupportsBlending Whether the field supports blending
     * @param OutSupportsLayering Whether the field supports layering
     * @return True if capabilities were retrieved successfully
     */
    virtual bool GetCapabilities(bool& OutSupportsBlending, bool& OutSupportsLayering) const = 0;
}; 