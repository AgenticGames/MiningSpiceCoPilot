// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Json.h"
#include "Interfaces/IEventPublisher.h"
#include "IEventHandler.generated.h"

/**
 * Event data container for passing events between components
 */
struct MININGSPICECOPILOT_API FEventData
{
    /** Event context with metadata */
    FEventContext Context;
    
    /** Event payload data */
    TSharedRef<FJsonObject> Payload;
    
    /** Constructor with context and payload */
    FEventData(const FEventContext& InContext, const TSharedRef<FJsonObject>& InPayload)
        : Context(InContext)
        , Payload(InPayload)
    {
    }
    
    /** Default constructor */
    FEventData()
        : Payload(MakeShared<FJsonObject>())
    {
    }
};

/**
 * Base interface for event handlers in the SVO+SDF mining architecture
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UEventHandler : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for event handlers in the SVO+SDF mining architecture
 * Provides context-specific event handling capabilities
 */
class MININGSPICECOPILOT_API IEventHandler
{
    GENERATED_BODY()

public:
    /**
     * Handles an event
     * @param Context Event context
     * @param EventData Event payload
     * @return True if the event was handled successfully
     */
    virtual bool HandleEvent(const FEventContext& Context, const TSharedRef<FJsonObject>& EventData) = 0;
    
    /**
     * Checks if this handler can handle a specific event type
     * @param EventType Type of event to check
     * @return True if this handler can handle the event
     */
    virtual bool CanHandleEventType(const FName& EventType) const = 0;
    
    /**
     * Gets the name of this handler
     * @return Handler name
     */
    virtual FString GetHandlerName() const = 0;
};