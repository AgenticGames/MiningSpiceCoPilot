// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IEventDispatcher.generated.h"

// Forward declarations
class IEventPublisher;
class IEventSubscriber;
struct FEventData;

/**
 * Event dispatch modes for controlling the flow of event propagation
 */
enum class EEventDispatchMode : uint8
{
    /** Process events immediately on the calling thread */
    Immediate,
    
    /** Queue events and process them on the next tick */
    Deferred,
    
    /** Process high-priority events immediately and defer others */
    Hybrid
};

/**
 * Event dispatching options to control propagation behavior
 */
struct MININGSPICECOPILOT_API FEventDispatchOptions
{
    /** Dispatch mode for this event */
    EEventDispatchMode DispatchMode;
    
    /** Priority level for execution order */
    uint8 Priority;
    
    /** Whether to stop propagation after first successful handler */
    bool bStopAfterFirstHandler;
    
    /** Whether to use parallel dispatch for multiple subscribers */
    bool bAllowParallelDispatch;
    
    /** Maximum time allowed for event processing in milliseconds */
    uint32 MaxProcessingTimeMs;
    
    /** Default constructor */
    FEventDispatchOptions()
        : DispatchMode(EEventDispatchMode::Immediate)
        , Priority(128) // Medium priority
        , bStopAfterFirstHandler(false)
        , bAllowParallelDispatch(false)
        , MaxProcessingTimeMs(0) // No limit
    {
    }
};

/**
 * Event dispatch result containing handler responses
 */
struct MININGSPICECOPILOT_API FEventDispatchResult
{
    /** Whether any handler successfully processed the event */
    bool bHandled;
    
    /** Number of handlers that received the event */
    int32 HandlerCount;
    
    /** Number of handlers that successfully processed the event */
    int32 SuccessfulHandlerCount;
    
    /** Number of handlers that failed to process the event */
    int32 FailedHandlerCount;
    
    /** Time spent dispatching the event in milliseconds */
    float DispatchTimeMs;
    
    /** Whether the event processing was canceled */
    bool bWasCanceled;
    
    /** Default constructor */
    FEventDispatchResult()
        : bHandled(false)
        , HandlerCount(0)
        , SuccessfulHandlerCount(0)
        , FailedHandlerCount(0)
        , DispatchTimeMs(0.0f)
        , bWasCanceled(false)
    {
    }
};

/**
 * Base interface for event dispatchers in the SVO+SDF mining architecture
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UEventDispatcher : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for event dispatching in the SVO+SDF mining architecture
 * Provides central event dispatch capabilities with filtering and prioritization
 */
class MININGSPICECOPILOT_API IEventDispatcher
{
    GENERATED_BODY()

public:
    /**
     * Initializes the event dispatcher
     * @return True if initialization was successful
     */
    virtual bool Initialize() = 0;
    
    /**
     * Shuts down the event dispatcher and cleans up resources
     */
    virtual void Shutdown() = 0;
    
    /**
     * Checks if the event dispatcher has been initialized
     * @return True if initialized, false otherwise
     */
    virtual bool IsInitialized() const = 0;
    
    /**
     * Registers a publisher with this dispatcher
     * @param Publisher The publisher to register
     * @return True if registration was successful
     */
    virtual bool RegisterPublisher(IEventPublisher* Publisher) = 0;
    
    /**
     * Unregisters a publisher from this dispatcher
     * @param Publisher The publisher to unregister
     * @return True if unregistration was successful
     */
    virtual bool UnregisterPublisher(IEventPublisher* Publisher) = 0;
    
    /**
     * Registers a subscriber with this dispatcher
     * @param Subscriber The subscriber to register
     * @return True if registration was successful
     */
    virtual bool RegisterSubscriber(IEventSubscriber* Subscriber) = 0;
    
    /**
     * Unregisters a subscriber from this dispatcher
     * @param Subscriber The subscriber to unregister
     * @return True if unregistration was successful
     */
    virtual bool UnregisterSubscriber(IEventSubscriber* Subscriber) = 0;
    
    /**
     * Dispatches an event to appropriate subscribers
     * @param EventName Name of the event to dispatch
     * @param EventData Data associated with the event
     * @param Options Dispatch options
     * @return Result of the dispatch operation
     */
    virtual FEventDispatchResult DispatchEvent(const FName& EventName, const FEventData& EventData, const FEventDispatchOptions& Options = FEventDispatchOptions()) = 0;
    
    /**
     * Dispatches an event to a specific subscriber
     * @param EventName Name of the event to dispatch
     * @param EventData Data associated with the event
     * @param Subscriber The subscriber to dispatch to
     * @param Options Dispatch options
     * @return Result of the dispatch operation
     */
    virtual FEventDispatchResult DispatchEventToSubscriber(const FName& EventName, const FEventData& EventData, IEventSubscriber* Subscriber, const FEventDispatchOptions& Options = FEventDispatchOptions()) = 0;
    
    /**
     * Processes deferred events
     * @param MaxProcessingTimeMs Maximum processing time in milliseconds (0 for no limit)
     * @param MaxEventsToProcess Maximum number of events to process (0 for no limit)
     * @return Number of events processed
     */
    virtual int32 ProcessDeferredEvents(float MaxProcessingTimeMs = 0.0f, int32 MaxEventsToProcess = 0) = 0;
    
    /**
     * Gets the number of pending deferred events
     * @return Number of pending deferred events
     */
    virtual int32 GetDeferredEventCount() const = 0;
    
    /**
     * Gets a filtered list of subscribers for an event
     * @param EventName Name of the event
     * @param Category Optional event category filter
     * @return Array of subscribers
     */
    virtual TArray<IEventSubscriber*> GetSubscribersForEvent(const FName& EventName, const FName& Category = NAME_None) const = 0;
    
    /**
     * Gets a list of all registered subscribers
     * @return Array of all subscribers
     */
    virtual TArray<IEventSubscriber*> GetAllSubscribers() const = 0;
    
    /**
     * Gets a list of all registered publishers
     * @return Array of all publishers
     */
    virtual TArray<IEventPublisher*> GetAllPublishers() const = 0;
    
    /**
     * Checks if an event has any subscribers
     * @param EventName Name of the event
     * @return True if the event has subscribers
     */
    virtual bool HasSubscribersForEvent(const FName& EventName) const = 0;
    
    /**
     * Suspends event dispatching
     * Events will be queued until ResumeEventDispatching is called
     */
    virtual void SuspendEventDispatching() = 0;
    
    /**
     * Resumes event dispatching
     * @param bProcessQueuedEvents Whether to process events queued while suspended
     * @return Number of queued events processed
     */
    virtual int32 ResumeEventDispatching(bool bProcessQueuedEvents = true) = 0;
    
    /**
     * Checks if event dispatching is suspended
     * @return True if event dispatching is suspended
     */
    virtual bool IsEventDispatchingSuspended() const = 0;
    
    /**
     * Gets event dispatch statistics
     * @return Map of event names to dispatch count
     */
    virtual TMap<FName, int32> GetEventStats() const = 0;
    
    /**
     * Resets event statistics
     */
    virtual void ResetEventStats() = 0;
    
    /**
     * Gets the singleton instance
     * @return Reference to the event dispatcher
     */
    static IEventDispatcher& Get();
};
