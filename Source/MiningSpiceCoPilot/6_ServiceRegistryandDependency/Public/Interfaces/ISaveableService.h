// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ISaveableService.generated.h"

/**
 * Interface for services that can save and restore their state
 * Used by the service manager for state preservation during service restarts
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class USaveableService : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for services that can save and restore their state
 * Used by the service manager for state preservation during service restarts
 */
class MININGSPICECOPILOT_API ISaveableService
{
    GENERATED_BODY()

public:
    /**
     * Save the service state to a binary array
     * @param OutState Array to receive the serialized state
     * @return True if state was successfully saved
     */
    virtual bool SaveState(TArray<uint8>& OutState) = 0;
    
    /**
     * Restore the service state from a binary array
     * @param InState Array containing the serialized state
     * @return True if state was successfully restored
     */
    virtual bool RestoreState(const TArray<uint8>& InState) = 0;
}; 