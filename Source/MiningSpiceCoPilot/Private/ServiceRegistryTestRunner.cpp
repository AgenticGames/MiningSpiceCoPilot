// Copyright Epic Games, Inc. All Rights Reserved.

#include "ServiceRegistryTestHarness.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Logging/LogMacros.h"
#include "Engine/Engine.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "WorkspaceMenuStructure.h"
#include "Framework/Docking/TabManager.h"

// Console command to run the service registry test harness
static FAutoConsoleCommand GRunServiceRegistryTests(
    TEXT("ServiceRegistry.RunTests"),
    TEXT("Runs a comprehensive test suite for the ServiceRegistry and Dependency system"),
    FConsoleCommandDelegate::CreateLambda([]()
    {
        // Log to console that tests are starting
        UE_LOG(LogTemp, Display, TEXT("Starting ServiceRegistry tests..."));
        
        // Run the tests
        bool bSuccess = FServiceRegistryTestHarness::RunTests();
        
        // Log results
        UE_LOG(LogTemp, Display, TEXT("ServiceRegistry tests %s"), bSuccess ? TEXT("PASSED") : TEXT("FAILED"));
        
        // Show notification in editor viewport if available
        if (GEngine && GIsEditor)
        {
            TSharedPtr<SNotificationItem> NotificationPtr;
            
            FNotificationInfo Info(FText::FromString(
                FString::Printf(TEXT("ServiceRegistry Tests: %s"), bSuccess ? TEXT("PASSED") : TEXT("FAILED"))));
                
            Info.bUseLargeFont = true;
            Info.bUseSuccessFailIcons = true;
            Info.bUseThrobber = false;
            Info.bFireAndForget = false;
            Info.ExpireDuration = 5.0f;
            Info.FadeOutDuration = 1.0f;
            Info.ButtonDetails.Add(FNotificationButtonInfo(
                FText::FromString(TEXT("View Log")),
                FText::FromString(TEXT("Opens the Output Log to view test results")),
                FSimpleDelegate::CreateLambda([]() {
                    // Simplified: Don't try to use FGlobalTabmanager since we may not have access to it
                    UE_LOG(LogTemp, Display, TEXT("View log button clicked - output log should be visible"));
                })));
                
            NotificationPtr = FSlateNotificationManager::Get().AddNotification(Info);
            NotificationPtr->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
        }
    })); 