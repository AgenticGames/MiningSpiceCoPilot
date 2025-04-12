// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IEventPublisher.generated.h"

/**
 * Event priority for publishing and processing
 */
enum class EEventPriority : uint8
{
 /** Critical events that require immediate attention */
 Critical,
 
 /** High priority events */
 High,
 
 /** Normal priority events (default) */
 Normal,
 
 /** Low priority events */
 Low,
 
 /** Background events with minimal priority */
 Background
};

/**
 * Event visibility scope
 */
enum class EEventScope : uint8
{
 /** Local to the current region */
 Region,
 
 /** Global across all regions */
 Global,
 
 /** Only visible to system components */
 System,
 
 /** Only visible to debugging tools */
 Debug,
 
 /** Only distributed to network clients */
 Network
};

/**
 * Event delivery guarantee level
 */
enum class EEventDeliveryGuarantee : uint8
{
 /** Best effort delivery with no guarantees */
 BestEffort,
 
 /** Guaranteed delivery with potential delays */
 Guaranteed,
 
 /** Ordered delivery within the same channel */
 OrderedPerChannel,
 
 /** Strictly ordered delivery across all channels */
 StrictlyOrdered
};

/**
 * Event context containing metadata for event routing and processing
 */
struct MININGSPICECOPILOT_API FEventContext
{
 /** Unique identifier for the event */
 FGuid EventId;
 
 /** Name of the event type */
 FName EventType;
 
 /** Time when the event was published */
 double PublishTimeSeconds;
 
 /** Priority level for the event */
 EEventPriority Priority;
 
 /** Visibility scope for the event */
 EEventScope Scope;
 
 /** Delivery guarantee level for the event */
 EEventDeliveryGuarantee DeliveryGuarantee;
 
 /** Region ID associated with the event (INDEX_NONE for global) */
 int32 RegionId;
 
 /** Zone ID associated with the event (INDEX_NONE for region-wide) */
 int32 ZoneId;
 
 /** Channel ID for grouped event processing */
 int32 ChannelId;
 
 /** Optional correlation ID for tracking related events */
 FGuid CorrelationId;
 
 /** Publisher name for debugging and tracking */
 FString PublisherName;
 
 /** Whether this event can be cancelled by subscribers */
 bool bCancellable;
 
 /** Whether this event has been cancelled */
 bool bCancelled;
 
 /** Constructor with default values */
 FEventContext()
 : EventId(FGuid::NewGuid())
 , EventType(NAME_None)
 , PublishTimeSeconds(0.0)
 , Priority(EEventPriority::Normal)
 , Scope(EEventScope::Global)
 , DeliveryGuarantee(EEventDeliveryGuarantee::BestEffort)
 , RegionId(INDEX_NONE)
 , ZoneId(INDEX_NONE)
 , ChannelId(0)
 , PublisherName(TEXT("Unknown"))
 , bCancellable(false)
 , bCancelled(false)
 {
 }
};

/**
 * Base interface for event publishers in the SVO+SDF mining architecture
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UEventPublisher : public UInterface
{
 GENERATED_BODY()
};

/**
 * Interface for event publishing in the SVO+SDF mining architecture
 * Provides event creation and publishing capabilities for system components
 */
class MININGSPICECOPILOT_API IEventPublisher
{
 GENERATED_BODY()

public:
 /**
 * Initializes the event publisher
 * @return True if initialization was successful
 */
 virtual bool Initialize() = 0;
 
 /**
 * Shuts down the event publisher and cleans up resources
 */
 virtual void Shutdown() = 0;
 
 /**
 * Checks if the event publisher has been initialized
 * @return True if initialized, false otherwise
 */
 virtual bool IsInitialized() const = 0;
 
 /**
 * Publishes an event with the specified context
 * @param EventContext Context for the event
 * @param EventData Data for the event
 * @return True if the event was published successfully
 */
 virtual bool PublishEvent(const FEventContext& EventContext, const TSharedRef<FJsonObject>& EventData) = 0;
 
 /**
 * Publishes an event with automatic context creation
 * @param EventType Type of the event
 * @param EventData Data for the event
 * @param Priority Priority for the event
 * @param Scope Visibility scope for the event
 * @return True if the event was published successfully
 */
 virtual bool PublishEvent(const FName& EventType, const TSharedRef<FJsonObject>& EventData, 
 EEventPriority Priority = EEventPriority::Normal, EEventScope Scope = EEventScope::Global) = 0;
 
 /**
 * Publishes a cancellable event and waits for processing
 * @param EventType Type of the event
 * @param EventData Data for the event
 * @param OutCancelled Optional output parameter for cancellation status
 * @param Priority Priority for the event
 * @param Scope Visibility scope for the event
 * @return True if the event was published successfully
 */
 virtual bool PublishCancellableEvent(const FName& EventType, const TSharedRef<FJsonObject>& EventData, 
 bool* OutCancelled = nullptr, EEventPriority Priority = EEventPriority::Normal, 
 EEventScope Scope = EEventScope::Global) = 0;
 
 /**
 * Publishes an event specifically for a region
 * @param RegionId ID of the region for the event
 * @param EventType Type of the event
 * @param EventData Data for the event
 * @param Priority Priority for the event
 * @return True if the event was published successfully
 */
 virtual bool PublishRegionEvent(int32 RegionId, const FName& EventType, 
 const TSharedRef<FJsonObject>& EventData, EEventPriority Priority = EEventPriority::Normal) = 0;
 
 /**
 * Publishes an event specifically for a zone
 * @param RegionId ID of the region for the event
 * @param ZoneId ID of the zone for the event
 * @param EventType Type of the event
 * @param EventData Data for the event
 * @param Priority Priority for the event
 * @return True if the event was published successfully
 */
 virtual bool PublishZoneEvent(int32 RegionId, int32 ZoneId, const FName& EventType, 
 const TSharedRef<FJsonObject>& EventData, EEventPriority Priority = EEventPriority::Normal) = 0;
 
 /**
 * Publishes an event with a correlation ID to track related events
 * @param EventType Type of the event
 * @param EventData Data for the event
 * @param CorrelationId Correlation ID for tracking related events
 * @param Priority Priority for the event
 * @return True if the event was published successfully
 */
 virtual bool PublishCorrelatedEvent(const FName& EventType, const TSharedRef<FJsonObject>& EventData, 
 const FGuid& CorrelationId, EEventPriority Priority = EEventPriority::Normal) = 0;
 
 /**
 * Gets the name of this publisher
 * @return Publisher name
 */
 virtual FString GetPublisherName() const = 0;
 
 /**
 * Sets the name of this publisher
 * @param PublisherName Name to set
 */
 virtual void SetPublisherName(const FString& PublisherName) = 0;
 
 /**
 * Gets the singleton instance
 * @return Reference to the event publisher
 */
 static IEventPublisher& Get();
};
