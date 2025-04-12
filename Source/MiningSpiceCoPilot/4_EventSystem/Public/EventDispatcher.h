// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IEventDispatcher.h"
#include "Interfaces/IEventPublisher.h"
#include "Interfaces/IEventSubscriber.h"
#include "Interfaces/IEventHandler.h"
#include "EventDispatcher.generated.h"

/**
 * Queued event data for deferred processing
 */
struct FQueuedEvent
{
    /** Name of the event */
    FName EventName;
    
    /** Event data */
    FEventData EventData;
    
    /** Dispatch options */
    FEventDispatchOptions Options;
    
    /** Time when the event was queued */
    double QueueTimeSeconds;
    
    /** Constructor */
    FQueuedEvent(const FName& InEventName, const FEventData& InEventData, 
        const FEventDispatchOptions& InOptions)
        : EventName(InEventName)
        , EventData(InEventData)
        , Options(InOptions)
        , QueueTimeSeconds(FPlatformTime::Seconds())
    {
    }
};

/**
 * Event dispatcher implementation for the SVO+SDF mining architecture
 * Provides central event routing and dispatch capabilities with filtering and prioritization
 */
UCLASS()
class MININGSPICECOPILOT_API UEventDispatcher : public UObject, public IEventDispatcher
{
    GENERATED_BODY()
    
public:
    // Begin IEventDispatcher Interface
    virtual bool Initialize() override;
    virtual void Shutdown() override;
    virtual bool IsInitialized() const override;
    virtual bool RegisterPublisher(IEventPublisher* Publisher) override;
    virtual bool UnregisterPublisher(IEventPublisher* Publisher) override;
    virtual bool RegisterSubscriber(IEventSubscriber* Subscriber) override;
    virtual bool UnregisterSubscriber(IEventSubscriber* Subscriber) override;
    virtual FEventDispatchResult DispatchEvent(const FName& EventName, const FEventData& EventData, 
        const FEventDispatchOptions& Options = FEventDispatchOptions()) override;
    virtual FEventDispatchResult DispatchEventToSubscriber(const FName& EventName, const FEventData& EventData, 
        IEventSubscriber* Subscriber, const FEventDispatchOptions& Options = FEventDispatchOptions()) override;
    virtual int32 ProcessDeferredEvents(float MaxProcessingTimeMs = 0.0f, int32 MaxEventsToProcess = 0) override;
    virtual int32 GetDeferredEventCount() const override;
    virtual TArray<IEventSubscriber*> GetSubscribersForEvent(const FName& EventName, const FName& Category = NAME_None) const override;
    virtual TArray<IEventSubscriber*> GetAllSubscribers() const override;
    virtual TArray<IEventPublisher*> GetAllPublishers() const override;
    virtual bool HasSubscribersForEvent(const FName& EventName) const override;
    virtual void SuspendEventDispatching() override;
    virtual int32 ResumeEventDispatching(bool bProcessQueuedEvents = true) override;
    virtual bool IsEventDispatchingSuspended() const override;
    virtual TMap<FName, int32> GetEventStats() const override;
    virtual void ResetEventStats() override;
    // End IEventDispatcher Interface
    
    /**
     * Gets the singleton instance
     */
    static UEventDispatcher* Get();
    
private:
    /** Whether the EventDispatcher has been initialized */
    bool bIsInitialized;
    
    /** Whether event dispatching is suspended */
    bool bDispatchingSuspended;
    
    /** Critical section for thread safety */
    mutable FCriticalSection CriticalSection;
    
    /** Registered publishers */
    TArray<IEventPublisher*> Publishers;
    
    /** Registered subscribers */
    TArray<IEventSubscriber*> Subscribers;
    
    /** Map of event names to subscribers for fast lookups */
    TMap<FName, TArray<IEventSubscriber*>> EventSubscriberMap;
    
    /** Queue of events pending processing */
    TArray<FQueuedEvent> PendingEvents;
    
    /** Statistics for event dispatch */
    TMap<FName, int32> EventStats;
    
    /**
     * Adds an event to the deferred processing queue
     * @param EventName Name of the event
     * @param EventData Event data
     * @param Options Dispatch options
     */
    void QueueEvent(const FName& EventName, const FEventData& EventData, const FEventDispatchOptions& Options);
    
    /**
     * Updates event statistics
     * @param EventName Name of the event
     * @param Result Dispatch result
     */
    void UpdateEventStats(const FName& EventName, const FEventDispatchResult& Result);
    
    /**
     * Gets subscribers for an event sorted by their handler priority
     * @param EventName Name of the event
     * @param Category Optional category filter
     * @return Sorted array of subscribers
     */
    TArray<IEventSubscriber*> GetSortedSubscribersForEvent(const FName& EventName, const FName& Category = NAME_None) const;
    
    /**
     * Dispatches an event in parallel to multiple subscribers
     * @param EventName Name of the event
     * @param EventData Event data
     * @param Subscribers Array of subscribers
     * @param Options Dispatch options
     * @return Dispatch result
     */
    FEventDispatchResult DispatchEventInParallel(const FName& EventName, const FEventData& EventData, 
        const TArray<IEventSubscriber*>& Subscribers, const FEventDispatchOptions& Options);
};