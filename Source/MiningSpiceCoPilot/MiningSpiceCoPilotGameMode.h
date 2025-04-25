// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MiningSpiceCoPilotGameMode.generated.h"

UCLASS(minimalapi)
class AMiningSpiceCoPilotGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMiningSpiceCoPilotGameMode();
    
    // Run the service registry tests from Blueprint or console command
    UFUNCTION(Exec, BlueprintCallable, Category = "Testing")
    void RunServiceRegistryTests();
};



