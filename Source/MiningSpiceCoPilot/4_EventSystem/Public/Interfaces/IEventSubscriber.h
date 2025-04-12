// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include " CoreMinimal.h\
#include \UObject/Interface.h\
#include \Event/Public/Interfaces/IEventPublisher.h\
#include \IEventSubscriber.generated.h\

/**
 * Event handler callback signature
 */
DECLARE_DELEGATE_TwoParams(FEventHandlerDelegate, const FEventContext&, const TSharedRef<FJsonObject>&);

/**
 * Subscription options for event handlers
 */
struct MININGSPICECOPILOT_API FSubscriptionOptions
{
 /** Minimum priority level to receive events */
 EEventPriority MinPriorityLevel;
 
 /** Maximum priority level to receive events */
 EEventPriority MaxPriorityLevel;
 
 /** Visibility scopes to receive events from */
 TArray<EEventScope> Scopes;
 
 /** Region ID filter (INDEX_NONE for all regions) */
 int32 RegionIdFilter;
 
 /** Zone ID filter (INDEX_NONE for all zones) */
 int32 ZoneIdFilter;
 
 /** Channel ID filter (0 for all channels) */
 int32 ChannelIdFilter;
 
 /** Whether to receive events in the thread they were published (true) or in processing thread (false) */
 bool bReceiveInPublisherThread;
 
 /** Handler priority for execution order (higher executes first) */
 int32 HandlerPriority;
 
 /** Whether this subscription is temporary and should be automatically removed after processing */
 bool bTemporary;
 
 /** Maximum number of events to process before auto-removing (0 for no limit) */
 int32 MaxEvents;
 
 /** Constructor with default values */
 FSubscriptionOptions()
 : MinPriorityLevel(EEventPriority::Background)
 , MaxPriorityLevel(EEventPriority::Critical)
 , RegionIdFilter(INDEX_NONE)
 , ZoneIdFilter(INDEX_NONE)
 , ChannelIdFilter(0)
 , bReceiveInPublisherThread(false)
 , HandlerPriority(0)
 , bTemporary(false)
 , MaxEvents(0)
 {
 Scopes = { EEventScope::Global, EEventScope::Region, EEventScope::System, EEventScope::Debug, EEventScope::Network };
 }
 
 /** Helper to create options for region-specific events */
 static FSubscriptionOptions ForRegion(int32 RegionId)
 {
 FSubscriptionOptions Options;
 Options.RegionIdFilter = RegionId;
 Options.Scopes = { EEventScope::Region, EEventScope::Global };
 return Options;
 }
 
 /** Helper to create options for zone-specific events */
 static FSubscriptionOptions ForZone(int32 RegionId, int32 ZoneId)
 {
 FSubscriptionOptions Options;
 Options.RegionIdFilter = RegionId;
 Options.ZoneIdFilter = ZoneId;
 Options.Scopes = { EEventScope::Region, EEventScope::Global };
 return Options;
 }
 
 /** Helper to create options for high priority events */
 static FSubscriptionOptions HighPriorityOnly()
 {
 FSubscriptionOptions Options;
 Options.MinPriorityLevel = EEventPriority::High;
 Options.MaxPriorityLevel = EEventPriority::Critical;
 return Options;
 }
};

/**
 * Subscription tracking information
 */
struct MININGSPICECOPILOT_API FSubscriptionInfo
{
 /** Unique identifier for this subscription */
 FGuid SubscriptionId;
 
 /** Event type this subscription is for */
 FName EventType;
 
 /** Options for this subscription */
 FSubscriptionOptions Options;
 
 /** Subscriber name for debugging */
 FString SubscriberName;
 
 /** Total number of events received by this subscription */
 int32 EventsReceived;
 
 /** Total number of events processed by this subscription */
 int32 EventsProcessed;
 
 /** Last time an event was received */
 double LastEventTimeSeconds;
 
 /** Constructor */
 FSubscriptionInfo()
 : SubscriptionId(FGuid::NewGuid())
 , EventType(NAME_None)
 , SubscriberName(TEXT(\Unknown\))
 , EventsReceived(0)
 , EventsProcessed(0)
 , LastEventTimeSeconds(0.0)
 {
 }
};

/**
 * Base interface for event subscribers in the SVO+SDF mining architecture
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UEventSubscriber : public UInterface
{
 GENERATED_BODY()
};

/**
 * Interface for event subscribers in the SVO+SDF mining architecture
 * Provides event subscription and handling capabilities for system components
 */
class MININGSPICECOPILOT_API IEventSubscriber
{
 GENERATED_BODY()

public:
 /**
 * Initializes the event subscriber
 * @return True if initialization was successful
 */
 virtual bool Initialize() = 0;
 
 /**
 * Shuts down the event subscriber and cleans up resources
 */
 virtual void Shutdown() = 0;
 
 /**
 * Checks if the event subscriber has been initialized
 * @return True if initialized, false otherwise
 */
 virtual bool IsInitialized() const = 0;
 
 /**
 * Subscribes to an event type with the specified handler and options
 * @param EventType Type of event to subscribe to
 * @param Handler Delegate to call when the event is received
 * @param Options Subscription options
 * @return Subscription ID for managing the subscription
 */
 virtual FGuid SubscribeToEvent(const FName& EventType, const FEventHandlerDelegate& Handler, 
 const FSubscriptionOptions& Options = FSubscriptionOptions()) = 0;
 
 /**
 * Subscribes to multiple event types with the same handler and options
 * @param EventTypes Array of event types to subscribe to
 * @param Handler Delegate to call when any of the events are received
 * @param Options Subscription options
 * @return Map of event types to subscription IDs
 */
 virtual TMap<FName, FGuid> SubscribeToEvents(const TArray<FName>& EventTypes, 
 const FEventHandlerDelegate& Handler, const FSubscriptionOptions& Options = FSubscriptionOptions()) = 0;
 
 /**
 * Subscribes to all events within a namespace
 * @param Namespace Namespace prefix for events to subscribe to
 * @param Handler Delegate to call when matching events are received
 * @param Options Subscription options
 * @return Subscription ID for managing the subscription
 */
 virtual FGuid SubscribeToNamespace(const FString& Namespace, const FEventHandlerDelegate& Handler,
 const FSubscriptionOptions& Options = FSubscriptionOptions()) = 0;
 
 /**
 * Subscribes to events for a specific correlation ID
 * @param CorrelationId Correlation ID to subscribe to
 * @param Handler Delegate to call when matching events are received
 * @param Options Subscription options
 * @return Subscription ID for managing the subscription
 */
 virtual FGuid SubscribeToCorrelation(const FGuid& CorrelationId, const FEventHandlerDelegate& Handler,
 const FSubscriptionOptions& Options = FSubscriptionOptions()) = 0;
 
 /**
 * Unsubscribes from an event using the subscription ID
 * @param SubscriptionId ID of the subscription to remove
 * @return True if the subscription was found and removed
 */
 virtual bool Unsubscribe(const FGuid& SubscriptionId) = 0;
 
 /**
 * Unsubscribes from all events of a specific type
 * @param EventType Type of events to unsubscribe from
 * @return Number of subscriptions removed
 */
 virtual int32 UnsubscribeAll(const FName& EventType) = 0;
 
 /**
 * Unsubscribes from all events
 * @return Number of subscriptions removed
 */
 virtual int32 UnsubscribeAll() = 0;
 
 /**
 * Gets information about a subscription
 * @param SubscriptionId ID of the subscription to get info for
 * @return Subscription information or nullptr if not found
 */
 virtual const FSubscriptionInfo* GetSubscriptionInfo(const FGuid& SubscriptionId) const = 0;
 
 /**
 * Gets all active subscriptions for this subscriber
 * @return Array of subscription information
 */
 virtual TArray<FSubscriptionInfo> GetAllSubscriptions() const = 0;
 
 /**
 * Gets the number of active subscriptions
 * @return Number of active subscriptions
 */
 virtual int32 GetSubscriptionCount() const = 0;
 
 /**
 * Gets the name of this subscriber
 * @return Subscriber name
 */
 virtual FString GetSubscriberName() const = 0;
 
 /**
 * Sets the name of this subscriber
 * @param SubscriberName Name to set
 */
 virtual void SetSubscriberName(const FString& SubscriberName) = 0;
 
 /**
 * Gets the singleton instance
 * @return Reference to the event subscriber
 */
 static IEventSubscriber& Get();
};
