// Copyright Epic Games, Inc. All Rights Reserved.

#include "EventBus.h"
#include "Interfaces/IEventDispatcher.h"
#include "JsonObjectConverter.h"
#include "HAL/PlatformTime.h"

UEventBus* UEventBus::Get()
{
    static UEventBus* Singleton = nullptr;
    if (!Singleton)
    {
        Singleton = NewObject<UEventBus>();
        Singleton->Initialize();
    }
    return Singleton;
}

bool UEventBus::Initialize()
{
    if (bIsInitialized)
    {
        return true;
    }

    Name = TEXT("EventBus");
    bIsInitialized = true;
    
    // Self register
    Publisher = this;
    Subscriber = this;
    
    return true;
}

void UEventBus::Shutdown()
{
    if (!bIsInitialized)
    {
        return;
    }

    FScopeLock Lock(&CriticalSection);
    
    // Clear all subscriptions
    UnsubscribeAll();
    
    // Clear event history
    EventHistory.Empty();
    
    // Clear stats
    EventStatistics.Empty();
    
    // Clear pending events
    FEventData DummyEvent(FEventContext(), MakeShared<FJsonObject>());
    while (!PendingEvents.IsEmpty())
    {
        PendingEvents.Dequeue(DummyEvent);
    }
    
    // Reset pointers
    EventDispatcher = nullptr;
    Publisher = nullptr;
    Subscriber = nullptr;
    
    bIsInitialized = false;
}

bool UEventBus::IsInitialized() const
{
    return bIsInitialized;
}

FString UEventBus::GetPublisherName() const
{
    return Name;
}

void UEventBus::SetPublisherName(const FString& PublisherName)
{
    Name = PublisherName;
}

FString UEventBus::GetSubscriberName() const
{
    return Name;
}

void UEventBus::SetSubscriberName(const FString& SubscriberName)
{
    Name = SubscriberName;
}

bool UEventBus::PublishEvent(const FEventContext& EventContext, const TSharedRef<FJsonObject>& EventData)
{
    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Warning, TEXT("EventBus: Attempting to publish event when not initialized"));
        return false;
    }
    
    // Copy and update the context with the current time
    FEventContext Context = EventContext;
    Context.PublishTimeSeconds = FPlatformTime::Seconds();
    Context.PublisherName = Name;
    
    // Update event statistics
    UpdateEventStats(Context.EventType);
    
    // Record event for debug if needed
    RecordEventForDebug(FEventData(Context, EventData));
    
    // If we have a dispatcher, use it
    if (EventDispatcher.IsValid())
    {
        FEventDispatchOptions Options;
        Options.DispatchMode = Context.Priority >= EEventPriority::High ? 
            EEventDispatchMode::Immediate : EEventDispatchMode::Deferred;
        
        return EventDispatcher->DispatchEvent(Context.EventType, FEventData(Context, EventData), Options).bHandled;
    }
    
    // Otherwise dispatch directly
    return DispatchToMatchingSubscriptions(Context, EventData) > 0;
}

bool UEventBus::PublishEvent(const FName& EventType, const TSharedRef<FJsonObject>& EventData, 
    EEventPriority Priority, EEventScope Scope)
{
    FEventContext Context = CreateEventContext(EventType, Priority, Scope);
    return PublishEvent(Context, EventData);
}

bool UEventBus::PublishCancellableEvent(const FName& EventType, const TSharedRef<FJsonObject>& EventData, 
    bool* OutCancelled, EEventPriority Priority, EEventScope Scope)
{
    FEventContext Context = CreateEventContext(EventType, Priority, Scope);
    Context.bCancellable = true;
    
    bool Result = PublishEvent(Context, EventData);
    
    if (OutCancelled)
    {
        *OutCancelled = Context.bCancelled;
    }
    
    return Result;
}

bool UEventBus::PublishRegionEvent(int32 RegionId, const FName& EventType, 
    const TSharedRef<FJsonObject>& EventData, EEventPriority Priority)
{
    FEventContext Context = CreateEventContext(EventType, Priority, EEventScope::Region);
    Context.RegionId = RegionId;
    return PublishEvent(Context, EventData);
}

bool UEventBus::PublishZoneEvent(int32 RegionId, int32 ZoneId, const FName& EventType, 
    const TSharedRef<FJsonObject>& EventData, EEventPriority Priority)
{
    FEventContext Context = CreateEventContext(EventType, Priority, EEventScope::Region);
    Context.RegionId = RegionId;
    Context.ZoneId = ZoneId;
    return PublishEvent(Context, EventData);
}

bool UEventBus::PublishCorrelatedEvent(const FName& EventType, const TSharedRef<FJsonObject>& EventData, 
    const FGuid& CorrelationId, EEventPriority Priority)
{
    FEventContext Context = CreateEventContext(EventType, Priority);
    Context.CorrelationId = CorrelationId;
    return PublishEvent(Context, EventData);
}

FEventContext UEventBus::CreateEventContext(const FName& EventType, 
    EEventPriority Priority, EEventScope Scope) const
{
    FEventContext Context;
    Context.EventType = EventType;
    Context.Priority = Priority;
    Context.Scope = Scope;
    Context.PublisherName = Name;
    return Context;
}

int32 UEventBus::ProcessPendingEvents(int32 MaxEventsToProcess, float MaxTimeMs)
{
    if (!bIsInitialized)
    {
        return 0;
    }
    
    int32 ProcessedCount = 0;
    double StartTimeMs = FPlatformTime::Seconds() * 1000.0;
    
    FEventData Event(FEventContext(), MakeShared<FJsonObject>());
    
    while (!PendingEvents.IsEmpty() && 
          (MaxEventsToProcess == 0 || ProcessedCount < MaxEventsToProcess) &&
          (MaxTimeMs == 0 || ((FPlatformTime::Seconds() * 1000.0) - StartTimeMs) < MaxTimeMs))
    {
        if (PendingEvents.Dequeue(Event))
        {
            DispatchToMatchingSubscriptions(Event.Context, Event.Payload);
            ProcessedCount++;
        }
    }
    
    return ProcessedCount;
}

FGuid UEventBus::SubscribeToEvent(const FName& EventType, const FEventHandlerDelegate& Handler, 
    const FSubscriptionOptions& Options)
{
    if (!bIsInitialized || !Handler.IsBound())
    {
        return FGuid();
    }
    
    FScopeLock Lock(&CriticalSection);
    
    FSubscriptionRecord Record;
    Record.SubscriptionId = FGuid::NewGuid();
    Record.EventType = EventType;
    Record.HandlerDelegate = Handler;
    Record.Options = Options;
    Record.Subscriber = this;
    
    SubscriptionRecords.Add(Record.SubscriptionId, Record);
    
    // Add to type lookup map for faster event routing
    if (!EventTypeToSubscriptions.Contains(EventType))
    {
        EventTypeToSubscriptions.Add(EventType, TArray<FGuid>());
    }
    
    EventTypeToSubscriptions[EventType].Add(Record.SubscriptionId);
    
    // Create and cache subscription info
    CreateSubscriptionInfo(Record.SubscriptionId, Record);
    
    return Record.SubscriptionId;
}

TMap<FName, FGuid> UEventBus::SubscribeToEvents(const TArray<FName>& EventTypes, 
    const FEventHandlerDelegate& Handler, const FSubscriptionOptions& Options)
{
    TMap<FName, FGuid> Result;
    
    if (!bIsInitialized || !Handler.IsBound() || EventTypes.Num() == 0)
    {
        return Result;
    }
    
    for (const FName& EventType : EventTypes)
    {
        FGuid SubscriptionId = SubscribeToEvent(EventType, Handler, Options);
        if (SubscriptionId.IsValid())
        {
            Result.Add(EventType, SubscriptionId);
        }
    }
    
    return Result;
}

FGuid UEventBus::SubscribeToNamespace(const FString& Namespace, const FEventHandlerDelegate& Handler,
    const FSubscriptionOptions& Options)
{
    if (!bIsInitialized || !Handler.IsBound() || Namespace.IsEmpty())
    {
        return FGuid();
    }
    
    FScopeLock Lock(&CriticalSection);
    
    FSubscriptionRecord Record;
    Record.SubscriptionId = FGuid::NewGuid();
    Record.EventType = NAME_None; // No specific event type
    Record.HandlerDelegate = Handler;
    Record.Options = Options;
    Record.Subscriber = this;
    Record.NamespacePattern = Namespace;
    Record.bIsNamespaceSubscription = true;
    
    SubscriptionRecords.Add(Record.SubscriptionId, Record);
    NamespaceSubscriptions.Add(Record.SubscriptionId);
    
    // Create and cache subscription info
    CreateSubscriptionInfo(Record.SubscriptionId, Record);
    
    return Record.SubscriptionId;
}

FGuid UEventBus::SubscribeToCorrelation(const FGuid& CorrelationId, const FEventHandlerDelegate& Handler,
    const FSubscriptionOptions& Options)
{
    if (!bIsInitialized || !Handler.IsBound() || !CorrelationId.IsValid())
    {
        return FGuid();
    }
    
    FScopeLock Lock(&CriticalSection);
    
    FSubscriptionRecord Record;
    Record.SubscriptionId = FGuid::NewGuid();
    Record.EventType = NAME_None; // No specific event type
    Record.HandlerDelegate = Handler;
    Record.Options = Options;
    Record.Subscriber = this;
    Record.bIsCorrelationSubscription = true;
    Record.CorrelationId = CorrelationId;
    
    SubscriptionRecords.Add(Record.SubscriptionId, Record);
    
    if (!CorrelationSubscriptions.Contains(CorrelationId))
    {
        CorrelationSubscriptions.Add(CorrelationId, TArray<FGuid>());
    }
    
    CorrelationSubscriptions[CorrelationId].Add(Record.SubscriptionId);
    
    // Create and cache subscription info
    CreateSubscriptionInfo(Record.SubscriptionId, Record);
    
    return Record.SubscriptionId;
}

bool UEventBus::Unsubscribe(const FGuid& SubscriptionId)
{
    if (!bIsInitialized || !SubscriptionId.IsValid())
    {
        return false;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    if (!SubscriptionRecords.Contains(SubscriptionId))
    {
        return false;
    }
    
    FSubscriptionRecord& Record = SubscriptionRecords[SubscriptionId];
    
    if (Record.bIsNamespaceSubscription)
    {
        NamespaceSubscriptions.Remove(SubscriptionId);
    }
    else if (Record.bIsCorrelationSubscription)
    {
        if (CorrelationSubscriptions.Contains(Record.CorrelationId))
        {
            CorrelationSubscriptions[Record.CorrelationId].Remove(SubscriptionId);
            
            // Clean up empty correlation arrays
            if (CorrelationSubscriptions[Record.CorrelationId].Num() == 0)
            {
                CorrelationSubscriptions.Remove(Record.CorrelationId);
            }
        }
    }
    else
    {
        // Remove from type-based lookup
        if (EventTypeToSubscriptions.Contains(Record.EventType))
        {
            EventTypeToSubscriptions[Record.EventType].Remove(SubscriptionId);
            
            // Clean up empty event type arrays
            if (EventTypeToSubscriptions[Record.EventType].Num() == 0)
            {
                EventTypeToSubscriptions.Remove(Record.EventType);
            }
        }
    }
    
    // Remove from records and cache
    SubscriptionRecords.Remove(SubscriptionId);
    SubscriptionInfoCache.Remove(SubscriptionId);
    SubscriptionInfoMap.Remove(SubscriptionId);
    
    return true;
}

int32 UEventBus::UnsubscribeAll(const FName& EventType)
{
    if (!bIsInitialized)
    {
        return 0;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    int32 Count = 0;
    
    if (EventTypeToSubscriptions.Contains(EventType))
    {
        TArray<FGuid> SubscriptionIds = EventTypeToSubscriptions[EventType];
        
        for (const FGuid& SubscriptionId : SubscriptionIds)
        {
            if (Unsubscribe(SubscriptionId))
            {
                Count++;
            }
        }
    }
    
    return Count;
}

int32 UEventBus::UnsubscribeAll()
{
    if (!bIsInitialized)
    {
        return 0;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    int32 Count = SubscriptionRecords.Num();
    
    // Clear all collections
    SubscriptionRecords.Empty();
    EventTypeToSubscriptions.Empty();
    NamespaceSubscriptions.Empty();
    CorrelationSubscriptions.Empty();
    SubscriptionInfoCache.Empty();
    SubscriptionInfoMap.Empty();
    
    return Count;
}

const FSubscriptionInfo* UEventBus::GetSubscriptionInfo(const FGuid& SubscriptionId) const
{
    if (!bIsInitialized || !SubscriptionId.IsValid())
    {
        return nullptr;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    if (!SubscriptionRecords.Contains(SubscriptionId))
    {
        return nullptr;
    }
    
    const FSubscriptionRecord& Record = SubscriptionRecords[SubscriptionId];
    return &CreateSubscriptionInfo(SubscriptionId, Record);
}

TArray<FSubscriptionInfo> UEventBus::GetAllSubscriptions() const
{
    TArray<FSubscriptionInfo> Result;
    
    if (!bIsInitialized)
    {
        return Result;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    Result.Reserve(SubscriptionRecords.Num());
    
    for (const auto& Pair : SubscriptionRecords)
    {
        Result.Add(CreateSubscriptionInfo(Pair.Key, Pair.Value));
    }
    
    return Result;
}

int32 UEventBus::GetSubscriptionCount() const
{
    if (!bIsInitialized)
    {
        return 0;
    }
    
    FScopeLock Lock(&CriticalSection);
    return SubscriptionRecords.Num();
}

void UEventBus::RecordEventForDebug(const FEventData& EventData, int32 MaxRecordedEvents)
{
    if (!bIsInitialized || MaxRecordedEvents <= 0)
    {
        return;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    // Add to the front for most recent first
    EventHistory.Insert(EventData, 0);
    
    // Trim if needed
    if (EventHistory.Num() > MaxRecordedEvents)
    {
        EventHistory.RemoveAt(MaxRecordedEvents, EventHistory.Num() - MaxRecordedEvents);
    }
}

TArray<FEventData> UEventBus::GetRecentEvents(int32 MaxEvents, const FName& EventTypeFilter) const
{
    TArray<FEventData> Result;
    
    if (!bIsInitialized)
    {
        return Result;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    if (EventTypeFilter == NAME_None)
    {
        // Just return most recent events up to the limit
        int32 Count = FMath::Min(MaxEvents, EventHistory.Num());
        Result.Append(EventHistory.GetData(), Count);
    }
    else
    {
        // Filter by event type
        for (const FEventData& Event : EventHistory)
        {
            if (Event.Context.EventType == EventTypeFilter)
            {
                Result.Add(Event);
                
                if (Result.Num() >= MaxEvents)
                {
                    break;
                }
            }
        }
    }
    
    return Result;
}

void UEventBus::ClearEventHistory()
{
    if (!bIsInitialized)
    {
        return;
    }
    
    FScopeLock Lock(&CriticalSection);
    EventHistory.Empty();
}

TMap<FName, int32> UEventBus::GetEventStats() const
{
    if (!bIsInitialized)
    {
        return TMap<FName, int32>();
    }
    
    FScopeLock Lock(&CriticalSection);
    return EventStatistics;
}

int32 UEventBus::DispatchToMatchingSubscriptions(const FEventContext& EventContext, const TSharedRef<FJsonObject>& EventData)
{
    if (!bIsInitialized)
    {
        return 0;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    int32 MatchCount = 0;
    TArray<FGuid> MatchingSubscriptions;
    
    // Check direct event type subscriptions first (fastest path)
    if (EventTypeToSubscriptions.Contains(EventContext.EventType))
    {
        for (const FGuid& SubscriptionId : EventTypeToSubscriptions[EventContext.EventType])
        {
            if (SubscriptionRecords.Contains(SubscriptionId))
            {
                const FSubscriptionRecord& Record = SubscriptionRecords[SubscriptionId];
                
                if (DoesSubscriptionMatchEvent(Record, EventContext))
                {
                    MatchingSubscriptions.Add(SubscriptionId);
                }
            }
        }
    }
    
    // Check namespace subscriptions (slower path)
    FString EventTypeStr = EventContext.EventType.ToString();
    
    for (const FGuid& SubscriptionId : NamespaceSubscriptions)
    {
        if (SubscriptionRecords.Contains(SubscriptionId))
        {
            const FSubscriptionRecord& Record = SubscriptionRecords[SubscriptionId];
            
            if (EventTypeStr.StartsWith(Record.NamespacePattern) && 
                DoesSubscriptionMatchEvent(Record, EventContext))
            {
                MatchingSubscriptions.Add(SubscriptionId);
            }
        }
    }
    
    // Check correlation subscriptions
    if (EventContext.CorrelationId.IsValid() && CorrelationSubscriptions.Contains(EventContext.CorrelationId))
    {
        for (const FGuid& SubscriptionId : CorrelationSubscriptions[EventContext.CorrelationId])
        {
            if (SubscriptionRecords.Contains(SubscriptionId))
            {
                const FSubscriptionRecord& Record = SubscriptionRecords[SubscriptionId];
                
                if (DoesSubscriptionMatchEvent(Record, EventContext))
                {
                    MatchingSubscriptions.Add(SubscriptionId);
                }
            }
        }
    }
    
    // Now dispatch to all matching subscriptions
    TArray<FGuid> ExpiredSubscriptions;
    
    for (const FGuid& SubscriptionId : MatchingSubscriptions)
    {
        if (SubscriptionRecords.Contains(SubscriptionId))
        {
            FSubscriptionRecord& Record = SubscriptionRecords[SubscriptionId];
            
            // Execute the handler
            if (Record.HandlerDelegate.IsBound())
            {
                Record.HandlerDelegate.Execute(EventContext, EventData);
                Record.EventsProcessed++;
                MatchCount++;
                
                // Update subscription info if it exists in cache
                if (SubscriptionInfoMap.Contains(SubscriptionId))
                {
                    FSubscriptionInfo& Info = SubscriptionInfoMap[SubscriptionId];
                    Info.EventsProcessed = Record.EventsProcessed;
                    Info.LastEventTimeSeconds = EventContext.PublishTimeSeconds;
                }
                
                // Check for expiration
                if (HasSubscriptionExpired(Record))
                {
                    ExpiredSubscriptions.Add(SubscriptionId);
                }
            }
        }
    }
    
    // Clean up any expired subscriptions
    for (const FGuid& SubscriptionId : ExpiredSubscriptions)
    {
        Unsubscribe(SubscriptionId);
    }
    
    return MatchCount;
}

bool UEventBus::DoesSubscriptionMatchEvent(const FSubscriptionRecord& Subscription, const FEventContext& Context) const
{
    // Check priority range
    if (Context.Priority < Subscription.Options.MinPriorityLevel || 
        Context.Priority > Subscription.Options.MaxPriorityLevel)
    {
        return false;
    }
    
    // Check scope
    if (!Subscription.Options.Scopes.Contains(Context.Scope))
    {
        return false;
    }
    
    // Check region filter
    if (Subscription.Options.RegionIdFilter != INDEX_NONE && 
        Context.RegionId != INDEX_NONE && 
        Subscription.Options.RegionIdFilter != Context.RegionId)
    {
        return false;
    }
    
    // Check zone filter
    if (Subscription.Options.ZoneIdFilter != INDEX_NONE && 
        Context.ZoneId != INDEX_NONE && 
        Subscription.Options.ZoneIdFilter != Context.ZoneId)
    {
        return false;
    }
    
    // Check channel filter
    if (Subscription.Options.ChannelIdFilter != 0 && 
        Context.ChannelId != 0 && 
        Subscription.Options.ChannelIdFilter != Context.ChannelId)
    {
        return false;
    }
    
    return true;
}

bool UEventBus::HasSubscriptionExpired(const FSubscriptionRecord& Subscription) const
{
    // Check if it's a temporary subscription
    if (Subscription.Options.bTemporary)
    {
        // Check if it has a max event count
        if (Subscription.Options.MaxEvents > 0 && 
            Subscription.EventsProcessed >= Subscription.Options.MaxEvents)
        {
            return true;
        }
    }
    
    return false;
}

void UEventBus::UpdateEventStats(const FName& EventType)
{
    if (!EventStatistics.Contains(EventType))
    {
        EventStatistics.Add(EventType, 0);
    }
    
    EventStatistics[EventType]++;
}

const FSubscriptionInfo& UEventBus::CreateSubscriptionInfo(const FGuid& SubscriptionId, const FSubscriptionRecord& Record) const
{
    // Check if we already have cached info
    if (!SubscriptionInfoMap.Contains(SubscriptionId))
    {
        FSubscriptionInfo Info;
        Info.SubscriptionId = SubscriptionId;
        Info.EventType = Record.EventType;
        Info.Options = Record.Options;
        Info.SubscriberName = Name;
        Info.EventsReceived = 0;
        Info.EventsProcessed = Record.EventsProcessed;
        Info.LastEventTimeSeconds = 0.0;
        
        SubscriptionInfoMap.Add(SubscriptionId, Info);
    }
    
    return SubscriptionInfoMap[SubscriptionId];
}

void UEventBus::HandleEventInternal(const FEventContext& EventContext, const TSharedRef<FJsonObject>& EventData)
{
    // Not using this direct handler in this implementation, as we use DispatchToMatchingSubscriptions instead
}