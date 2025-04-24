// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IMaterialInteractionService.generated.h"

/**
 * Interface for material interaction services
 * Provides operations for handling interactions between different material types
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UMaterialInteractionService : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for material interactions
 * Implementations handle rules and behaviors for material type interactions
 */
class MININGSPICECOPILOT_API IMaterialInteractionService
{
    GENERATED_BODY()

public:
    /**
     * Gets the source material type identifier for this interaction
     * @return Source material type identifier
     */
    virtual uint32 GetSourceMaterialTypeId() const = 0;
    
    /**
     * Gets the target material type identifier for this interaction
     * @return Target material type identifier
     */
    virtual uint32 GetTargetMaterialTypeId() const = 0;
    
    /**
     * Checks if the two materials can interact
     * @return True if interaction is possible
     */
    virtual bool CanInteract() const = 0;
    
    /**
     * Gets the interaction strength factor
     * @return Strength multiplier for interactions
     */
    virtual float GetInteractionStrength() const = 0;
    
    /**
     * Gets the interaction result type
     * @param OutResultTypeId Optional output parameter for result material type
     * @return True if there is a transformation result
     */
    virtual bool GetInteractionResult(uint32& OutResultTypeId) const = 0;
    
    /**
     * Simulates an interaction between materials at specific volumes
     * @param InSourceVolume Volume of source material
     * @param InTargetVolume Volume of target material
     * @param OutResultVolume Output parameter for result volume
     * @param OutResultTypeId Output parameter for result material type
     * @return True if the simulation was successful
     */
    virtual bool SimulateInteraction(float InSourceVolume, float InTargetVolume, float& OutResultVolume, uint32& OutResultTypeId) const = 0;
    
    /**
     * Gets the interaction effect types supported by this service
     * @param OutEffectTypes Array to receive effect type identifiers
     * @return True if effect types were retrieved successfully
     */
    virtual bool GetSupportedEffectTypes(TArray<uint32>& OutEffectTypes) const = 0;
}; 