// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IEventPublisher.h"
#include "Interfaces/IEventSubscriber.h"
#include "Interfaces/IEventHandler.h"
#include "Interfaces/IEventDispatcher.h"
#include "EventBus.generated.h"

/**
 * Event subscription record
 */
struct MININGSPICECOPILOT_API FSubscriptionRecord
{
    /** Unique ID for this subscription */
    FGuid SubscriptionId;
    
    /** Type of event subscribed to */
    FName EventType;
    
    /** Subscriber handling the events */
    TWeakInterfacePtr<IEventSubscriber> Subscriber;
    
    /** Callback delegate for handling events */
    FEventHandlerDelegate HandlerDelegate;
    
    /** Subscription options */
    FSubscriptionOptions Options;
    
    /** Namespace pattern for wildcard subscriptions */
    FString NamespacePattern;
    
    /** Number of events processed by this subscription */
    int32 EventsProcessed;

    /** Whether this is a namespace subscription */
    bool bIsNamespaceSubscription;

    /** Whether this is a correlation subscription */
    bool bIsCorrelationSubscription;
    
    /** Correlation ID for specific correlation subscriptions */
    FGuid CorrelationId;
    
    FSubscriptionRecord()
        : SubscriptionId(FGuid::NewGuid())
        , EventType(NAME_None)
        , EventsProcessed(0)
        , bIsNamespaceSubscription(false)
        , bIsCorrelationSubscription(false)
    {
    }
};

/**
 * EventBus implementation
 * Central event system for SVO+SDF mining architecture that manages event publishing and subscriptions
 */
UCLASS()
class MININGSPICECOPILOT_API UEventBus : public UObject, public IEventPublisher, public IEventSubscriber
{
    GENERATED_BODY()
    
public:
    // Begin IEventPublisher Interface
    virtual bool Initialize() override;
    virtual void Shutdown() override;
    virtual bool IsInitialized() const override;
    virtual bool PublishEvent(const FEventContext& EventContext, const TSharedRef<FJsonObject>& EventData) override;
    virtual bool PublishEvent(const FName& EventType, const TSharedRef<FJsonObject>& EventData,
        EEventPriority Priority = EEventPriority::Normal, EEventScope Scope = EEventScope::Global) override;
    virtual bool PublishCancellableEvent(const FName& EventType, const TSharedRef<FJsonObject>& EventData,
        bool* OutCancelled = nullptr, EEventPriority Priority = EEventPriority::Normal, 
        EEventScope Scope = EEventScope::Global) override;
    virtual bool PublishRegionEvent(int32 RegionId, const FName& EventType, 
        const TSharedRef<FJsonObject>& EventData, EEventPriority Priority = EEventPriority::Normal) override;
    virtual bool PublishZoneEvent(int32 RegionId, int32 ZoneId, const FName& EventType, 
        const TSharedRef<FJsonObject>& EventData, EEventPriority Priority = EEventPriority::Normal) override;
    virtual bool PublishCorrelatedEvent(const FName& EventType, const TSharedRef<FJsonObject>& EventData,
        const FGuid& CorrelationId, EEventPriority Priority = EEventPriority::Normal) override;
    virtual FString GetPublisherName() const override;
    virtual void SetPublisherName(const FString& PublisherName) override;
    // End IEventPublisher Interface
    
    // Begin IEventSubscriber Interface
    virtual FGuid SubscribeToEvent(const FName& EventType, const FEventHandlerDelegate& Handler, 
        const FSubscriptionOptions& Options = FSubscriptionOptions()) override;
    virtual TMap<FName, FGuid> SubscribeToEvents(const TArray<FName>& EventTypes, 
        const FEventHandlerDelegate& Handler, const FSubscriptionOptions& Options = FSubscriptionOptions()) override;
    virtual FGuid SubscribeToNamespace(const FString& Namespace, const FEventHandlerDelegate& Handler,
        const FSubscriptionOptions& Options = FSubscriptionOptions()) override;
    virtual FGuid SubscribeToCorrelation(const FGuid& CorrelationId, const FEventHandlerDelegate& Handler,
        const FSubscriptionOptions& Options = FSubscriptionOptions()) override;
    virtual bool Unsubscribe(const FGuid& SubscriptionId) override;
    virtual int32 UnsubscribeAll(const FName& EventType) override;
    virtual int32 UnsubscribeAll() override;
    virtual const FSubscriptionInfo* GetSubscriptionInfo(const FGuid& SubscriptionId) const override;
    virtual TArray<FSubscriptionInfo> GetAllSubscriptions() const override;
    virtual int32 GetSubscriptionCount() const override;
    virtual FString GetSubscriberName() const override;
    virtual void SetSubscriberName(const FString& SubscriberName) override;
    // End IEventSubscriber Interface
    
    // EventBus-specific methods
    
    /**
     * Creates a new context for publishing events
     * @param EventType Type of the event
     * @param Priority Priority for the event
     * @param Scope Visibility scope for the event
     * @return Newly created context
     */
    FEventContext CreateEventContext(const FName& EventType, EEventPriority Priority = EEventPriority::Normal, EEventScope Scope = EEventScope::Global) const;
    
    /**
     * Processes all pending events in the queue
     * @param MaxEventsToProcess Maximum number of events to process in this call (0 for all pending events)
     * @param MaxTimeMs Maximum time to spend processing events in milliseconds (0 for no limit)
     * @return Number of events processed
     */
    int32 ProcessPendingEvents(int32 MaxEventsToProcess = 0, float MaxTimeMs = 0.0f);
    
    /**
     * Records an event for debugging and replay purposes
     * @param EventData Event to record
     * @param MaxRecordedEvents Maximum number of events to keep in history (0 for unlimited)
     */
    void RecordEventForDebug(const FEventData& EventData, int32 MaxRecordedEvents = 1000);
    
    /**
     * Gets recently recorded events for debugging
     * @param MaxEvents Maximum number of recent events to return
     * @param EventTypeFilter Optional filter for specific event types
     * @return Array of recorded events
     */
    TArray<FEventData> GetRecentEvents(int32 MaxEvents = 100, const FName& EventTypeFilter = NAME_None) const;
    
    /**
     * Clears the event history
     */
    void ClearEventHistory();
    
    /**
     * Gets event statistics
     * @return Map of event types to counts
     */
    TMap<FName, int32> GetEventStats() const;
    
    /**
     * Gets the singleton instance
     */
    static UEventBus* Get();
    
private:
    /** Whether the EventBus has been initialized */
    bool bIsInitialized;
    
    /** Name of this publisher/subscriber for debugging */
    FString Name;
    
    /** Critical section for thread safety */
    mutable FCriticalSection CriticalSection;
    
    /** Map of subscription IDs to subscription records */
    TMap<FGuid, FSubscriptionRecord> SubscriptionRecords;
    
    /** Map of event types to subscriptions for fast lookups */
    TMap<FName, TArray<FGuid>> EventTypeToSubscriptions;
    
    /** Namespace pattern subscriptions */
    TArray<FGuid> NamespaceSubscriptions;
    
    /** Correlation ID subscriptions */
    TMap<FGuid, TArray<FGuid>> CorrelationSubscriptions;
    
    /** Subscription information cache for external queries */
    mutable TMap<FGuid, FSubscriptionInfo> SubscriptionInfoCache;
    
    /** Map of subscription IDs to info objects */
    mutable TMap<FGuid, FSubscriptionInfo> SubscriptionInfoMap;
    
    /** Queue of events pending processing */
    TQueue<FEventData> PendingEvents;
    
    /** History of recent events for debugging */
    TArray<FEventData> EventHistory;
    
    /** Statistics for event types */
    TMap<FName, int32> EventStatistics;
    
    /** Event dispatcher for handling delivery */
    TWeakInterfacePtr<IEventDispatcher> EventDispatcher;
    
    /** Publisher for events from this bus */
    TWeakInterfacePtr<IEventPublisher> Publisher;
    
    /** Subscriber for events from this bus */
    TWeakInterfacePtr<IEventSubscriber> Subscriber;
    
    /**
     * Internal event handler method
     * @param EventContext Event context
     * @param EventData Event data
     */
    void HandleEventInternal(const FEventContext& EventContext, const TSharedRef<FJsonObject>& EventData);
    
    /**
     * Updates subscription statistics
     * @param SubscriptionId ID of the subscription to update
     * @param bEventHandled Whether the event was handled successfully
     */
    void UpdateSubscriptionStats(const FGuid& SubscriptionId, bool bEventHandled);
    
    /**
     * Checks if a subscription has expired based on its options
     * @param Subscription Subscription to check
     * @return True if the subscription should be removed
     */
    bool HasSubscriptionExpired(const FSubscriptionRecord& Subscription) const;
    
    /**
     * Creates and caches subscription info for a record
     * @param SubscriptionId ID of the subscription
     * @param Record Subscription record
     * @return Subscription info object
     */
    const FSubscriptionInfo& CreateSubscriptionInfo(const FGuid& SubscriptionId, const FSubscriptionRecord& Record) const;
    
    /**
     * Dispatches an event to all matching subscriptions
     * @param EventContext Event context
     * @param EventData Event data payload
     * @return Number of subscriptions that received the event
     */
    int32 DispatchToMatchingSubscriptions(const FEventContext& EventContext, const TSharedRef<FJsonObject>& EventData);
    
    /**
     * Checks if a subscription matches an event
     * @param Subscription Subscription to check
     * @param Context Event context
     * @return True if the subscription should receive the event
     */
    bool DoesSubscriptionMatchEvent(const FSubscriptionRecord& Subscription, const FEventContext& Context) const;
    
    /**
     * Updates event statistics
     * @param EventType Type of the event to update
     */
    void UpdateEventStats(const FName& EventType);
};