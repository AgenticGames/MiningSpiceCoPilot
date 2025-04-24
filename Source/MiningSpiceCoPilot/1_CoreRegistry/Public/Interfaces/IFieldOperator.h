// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IFieldOperator.generated.h"

// Forward declarations
// struct FBox; - Removing, already defined in CoreMinimal.h

/**
 * Interface for field operations on voxel data
 * Provides operations for manipulating signed distance fields and volume data
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UFieldOperator : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for field operators
 * Implementations handle procedural operations on volumetric data
 */
class MININGSPICECOPILOT_API IFieldOperator
{
    GENERATED_BODY()

public:
    /**
     * Get the operator type identifier
     * @return Unique identifier for this operator type
     */
    virtual uint32 GetOperatorType() const = 0;
    
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
     * Reset the field operator state
     * @return True if the reset was successful
     */
    virtual bool Reset() = 0;
    
    /**
     * Gets the region identifier this operator is responsible for
     * @return Region identifier
     */
    virtual int32 GetRegionId() const = 0;
    
    /**
     * Check if this operator can handle cross-region operations
     * @return True if cross-region operations are supported
     */
    virtual bool SupportsGlobalCoordination() const = 0;
}; 