// Copyright Epic Games, Inc. All Rights Reserved.

#include "MiningSpiceCoPilotGameMode.h"
#include "MiningSpiceCoPilotCharacter.h"
#include "UObject/ConstructorHelpers.h"
#include "Public/ServiceRegistryTestHarness.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

AMiningSpiceCoPilotGameMode::AMiningSpiceCoPilotGameMode()
	: Super()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnClassFinder(TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter"));
	DefaultPawnClass = PlayerPawnClassFinder.Class;
}

void AMiningSpiceCoPilotGameMode::RunServiceRegistryTests()
{
    // Log the start of the tests
    UE_LOG(LogTemp, Display, TEXT("Starting Service Registry Tests..."));
    
    // Get the player controller to show messages
    if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
    {
        PC->ClientMessage(TEXT("Running Service Registry Tests..."));
    }
    
    // Run the tests
    bool bSuccess = FServiceRegistryTestHarness::RunTests();
    
    // Log the results
    UE_LOG(LogTemp, Display, TEXT("Service Registry Tests %s"), bSuccess ? TEXT("PASSED") : TEXT("FAILED"));
    
    // Notify the player
    if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
    {
        PC->ClientMessage(FString::Printf(TEXT("Service Registry Tests: %s"), bSuccess ? TEXT("PASSED") : TEXT("FAILED")));
    }
}
