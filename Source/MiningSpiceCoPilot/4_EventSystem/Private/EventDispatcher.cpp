// Copyright Epic Games, Inc. All Rights Reserved.

#include "EventDispatcher.h"
#include "Async/ParallelFor.h"
#include "HAL/PlatformTime.h"

UEventDispatcher* UEventDispatcher::Get()
{
    static UEventDispatcher* Singleton = nullptr;
    if (!Singleton)
    {
        Singleton = NewObject<UEventDispatcher>();
        Singleton->Initialize();
    }
    return Singleton;
}

bool UEventDispatcher::Initialize()
{
    if (bIsInitialized)
    {
        return true;
    }
    
    bIsInitialized = true;
    bDispatchingSuspended = false;
    
    return true;
}

void UEventDispatcher::Shutdown()
{
    if (!bIsInitialized)
    {
        return;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    // Clear collections
    Publishers.Empty();
    Subscribers.Empty();
    EventSubscriberMap.Empty();
    PendingEvents.Empty();
    EventStats.Empty();
    
    bIsInitialized = false;
}

bool UEventDispatcher::IsInitialized() const
{
    return bIsInitialized;
}

bool UEventDispatcher::RegisterPublisher(IEventPublisher* Publisher)
{
    if (!bIsInitialized || !Publisher)
    {
        return false;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    if (!Publishers.Contains(Publisher))
    {
        Publishers.Add(Publisher);
        return true;
    }
    
    return false;
}

bool UEventDispatcher::UnregisterPublisher(IEventPublisher* Publisher)
{
    if (!bIsInitialized || !Publisher)
    {
        return false;
    }
    
    FScopeLock Lock(&CriticalSection);
    return Publishers.Remove(Publisher) > 0;
}

bool UEventDispatcher::RegisterSubscriber(IEventSubscriber* Subscriber)
{
    if (!bIsInitialized || !Subscriber)
    {
        return false;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    if (!Subscribers.Contains(Subscriber))
    {
        Subscribers.Add(Subscriber);
        return true;
    }
    
    return false;
}

bool UEventDispatcher::UnregisterSubscriber(IEventSubscriber* Subscriber)
{
    if (!bIsInitialized || !Subscriber)
    {
        return false;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    // Remove from the main list
    if (Subscribers.Remove(Subscriber) > 0)
    {
        // Also remove from event mapping
        for (auto& EventSubscribers : EventSubscriberMap)
        {
            EventSubscribers.Value.Remove(Subscriber);
        }
        
        return true;
    }
    
    return false;
}

FEventDispatchResult UEventDispatcher::DispatchEvent(const FName& EventName, const FEventData& EventData, 
    const FEventDispatchOptions& Options)
{
    if (!bIsInitialized)
    {
        return FEventDispatchResult();
    }
    
    // If dispatching is suspended, queue the event
    if (bDispatchingSuspended)
    {
        QueueEvent(EventName, EventData, Options);
        FEventDispatchResult DeferredResult;
        DeferredResult.bHandled = true; // Mark as handled since we queued it
        return DeferredResult;
    }
    
    // If using deferred mode, queue the event
    if (Options.DispatchMode == EEventDispatchMode::Deferred ||
        (Options.DispatchMode == EEventDispatchMode::Hybrid && 
         EventData.Context.Priority < EEventPriority::High))
    {
        QueueEvent(EventName, EventData, Options);
        FEventDispatchResult DeferredResult;
        DeferredResult.bHandled = true; // Mark as handled since we queued it
        return DeferredResult;
    }
    
    // Otherwise, dispatch immediately
    FScopeLock Lock(&CriticalSection);
    
    double StartTimeMs = FPlatformTime::Seconds() * 1000.0;
    FEventDispatchResult Result;
    Result.bWasCanceled = EventData.Context.bCancelled;
    
    // Get subscribers for this event
    TArray<IEventSubscriber*> EventSubscribers = GetSortedSubscribersForEvent(EventName);
    Result.HandlerCount = EventSubscribers.Num();
    
    if (EventSubscribers.Num() == 0)
    {
        // No subscribers for this event
        Result.DispatchTimeMs = (FPlatformTime::Seconds() * 1000.0) - StartTimeMs;
        return Result;
    }
    
    // Check if we should use parallel dispatch
    if (Options.bAllowParallelDispatch && EventSubscribers.Num() > 1)
    {
        Result = DispatchEventInParallel(EventName, EventData, EventSubscribers, Options);
    }
    else
    {
        // Sequential dispatch
        for (IEventSubscriber* Subscriber : EventSubscribers)
        {
            // Check for specific handling for cancellable events
            if (EventData.Context.bCancellable && EventData.Context.bCancelled)
            {
                Result.bWasCanceled = true;
                break;
            }
            
            // Dispatch to this subscriber
            FEventDispatchResult SubResult = DispatchEventToSubscriber(EventName, EventData, Subscriber, Options);
            
            // Update counts
            if (SubResult.bHandled)
            {
                Result.SuccessfulHandlerCount++;
                Result.bHandled = true;
            }
            else
            {
                Result.FailedHandlerCount++;
            }
            
            // Check if we should stop after first handler
            if (Options.bStopAfterFirstHandler && SubResult.bHandled)
            {
                break;
            }
        }
    }
    
    // Update timing
    Result.DispatchTimeMs = (FPlatformTime::Seconds() * 1000.0) - StartTimeMs;
    
    // Update stats
    UpdateEventStats(EventName, Result);
    
    return Result;
}

FEventDispatchResult UEventDispatcher::DispatchEventToSubscriber(const FName& EventName, const FEventData& EventData, 
    IEventSubscriber* Subscriber, const FEventDispatchOptions& Options)
{
    FEventDispatchResult Result;
    
    if (!bIsInitialized || !Subscriber)
    {
        return Result;
    }
    
    // Check if we have a valid subscriber
    if (!Subscriber->IsInitialized())
    {
        return Result;
    }
    
    double StartTimeMs = FPlatformTime::Seconds() * 1000.0;
    
    // Find subscriptions for this event type
    TArray<FSubscriptionInfo> Subscriptions = Subscriber->GetAllSubscriptions();
    TArray<FGuid> MatchingSubscriptions;
    
    // Find matching subscriptions by event type
    for (const FSubscriptionInfo& Info : Subscriptions)
    {
        if (Info.EventType == EventName)
        {
            MatchingSubscriptions.Add(Info.SubscriptionId);
        }
    }
    
    Result.HandlerCount = MatchingSubscriptions.Num();
    
    // Invoke the subscription handlers
    // (Note: This relies on the subscriber's implementation of event delivery)
    if (MatchingSubscriptions.Num() > 0)
    {
        // The subscriber implementation handles actual dispatch to handlers
        Result.bHandled = true;
    }
    
    Result.DispatchTimeMs = (FPlatformTime::Seconds() * 1000.0) - StartTimeMs;
    return Result;
}

FEventDispatchResult UEventDispatcher::DispatchEventInParallel(const FName& EventName, const FEventData& EventData, 
    const TArray<IEventSubscriber*>& Subscribers, const FEventDispatchOptions& Options)
{
    FEventDispatchResult Result;
    Result.HandlerCount = Subscribers.Num();
    
    // We need an array to track successful handlers across threads
    TArray<bool> SuccessFlags;
    SuccessFlags.AddZeroed(Subscribers.Num());
    
    // Process in parallel
    ParallelFor(Subscribers.Num(), [&](int32 Index)
    {
        IEventSubscriber* Subscriber = Subscribers[Index];
        
        if (Subscriber && Subscriber->IsInitialized())
        {
            FEventDispatchResult SubResult = DispatchEventToSubscriber(
                EventName, EventData, Subscriber, Options);
            
            SuccessFlags[Index] = SubResult.bHandled;
        }
    });
    
    // Gather results
    for (bool bSuccess : SuccessFlags)
    {
        if (bSuccess)
        {
            Result.SuccessfulHandlerCount++;
            Result.bHandled = true;
        }
        else
        {
            Result.FailedHandlerCount++;
        }
    }
    
    return Result;
}

int32 UEventDispatcher::ProcessDeferredEvents(float MaxProcessingTimeMs, int32 MaxEventsToProcess)
{
    if (!bIsInitialized || bDispatchingSuspended)
    {
        return 0;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    if (PendingEvents.Num() == 0)
    {
        return 0;
    }
    
    double StartTimeMs = FPlatformTime::Seconds() * 1000.0;
    int32 ProcessedCount = 0;
    
    // Sort events by priority
    PendingEvents.Sort([](const FQueuedEvent& A, const FQueuedEvent& B) {
        return A.Options.Priority > B.Options.Priority;
    });
    
    TArray<FQueuedEvent> RemainingEvents;
    
    for (int32 i = 0; i < PendingEvents.Num(); ++i)
    {
        // Check time constraints
        if (MaxProcessingTimeMs > 0 && 
            ((FPlatformTime::Seconds() * 1000.0) - StartTimeMs) >= MaxProcessingTimeMs)
        {
            // Add remaining events back to the queue
            for (int32 j = i; j < PendingEvents.Num(); ++j)
            {
                RemainingEvents.Add(PendingEvents[j]);
            }
            break;
        }
        
        // Check count constraints
        if (MaxEventsToProcess > 0 && ProcessedCount >= MaxEventsToProcess)
        {
            // Add remaining events back to the queue
            for (int32 j = i; j < PendingEvents.Num(); ++j)
            {
                RemainingEvents.Add(PendingEvents[j]);
            }
            break;
        }
        
        // Process this event
        const FQueuedEvent& Event = PendingEvents[i];
        
        // Don't hold the lock during dispatch
        CriticalSection.Unlock();
        
        // Dispatch the event (note: this will acquire the lock internally)
        DispatchEvent(Event.EventName, Event.EventData, Event.Options);
        
        // Re-acquire the lock
        CriticalSection.Lock();
        
        ProcessedCount++;
    }
    
    // Replace the queue with remaining events
    PendingEvents = RemainingEvents;
    
    return ProcessedCount;
}

int32 UEventDispatcher::GetDeferredEventCount() const
{
    if (!bIsInitialized)
    {
        return 0;
    }
    
    FScopeLock Lock(&CriticalSection);
    return PendingEvents.Num();
}

TArray<IEventSubscriber*> UEventDispatcher::GetSubscribersForEvent(const FName& EventName, const FName& Category) const
{
    if (!bIsInitialized)
    {
        return TArray<IEventSubscriber*>();
    }
    
    FScopeLock Lock(&CriticalSection);
    
    if (EventSubscriberMap.Contains(EventName))
    {
        return EventSubscriberMap[EventName];
    }
    
    return TArray<IEventSubscriber*>();
}

TArray<IEventSubscriber*> UEventDispatcher::GetSortedSubscribersForEvent(const FName& EventName, const FName& Category) const
{
    TArray<IEventSubscriber*> Result = GetSubscribersForEvent(EventName, Category);
    
    // Sort by handler priority (determined by examining subscriptions)
    Result.Sort([EventName](const IEventSubscriber* A, const IEventSubscriber* B) {
        // This is a simplification - in a real implementation, we would examine
        // the actual subscription options for the subscribers to determine priority
        return true; // Placeholder for actual priority comparison
    });
    
    return Result;
}

TArray<IEventSubscriber*> UEventDispatcher::GetAllSubscribers() const
{
    if (!bIsInitialized)
    {
        return TArray<IEventSubscriber*>();
    }
    
    FScopeLock Lock(&CriticalSection);
    return Subscribers;
}

TArray<IEventPublisher*> UEventDispatcher::GetAllPublishers() const
{
    if (!bIsInitialized)
    {
        return TArray<IEventPublisher*>();
    }
    
    FScopeLock Lock(&CriticalSection);
    return Publishers;
}

bool UEventDispatcher::HasSubscribersForEvent(const FName& EventName) const
{
    if (!bIsInitialized)
    {
        return false;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    if (EventSubscriberMap.Contains(EventName))
    {
        return EventSubscriberMap[EventName].Num() > 0;
    }
    
    return false;
}

void UEventDispatcher::SuspendEventDispatching()
{
    if (!bIsInitialized)
    {
        return;
    }
    
    FScopeLock Lock(&CriticalSection);
    bDispatchingSuspended = true;
}

int32 UEventDispatcher::ResumeEventDispatching(bool bProcessQueuedEvents)
{
    if (!bIsInitialized)
    {
        return 0;
    }
    
    FScopeLock Lock(&CriticalSection);
    bDispatchingSuspended = false;
    
    if (bProcessQueuedEvents)
    {
        int32 QueuedEventCount = PendingEvents.Num();
        
        // Don't hold the lock during processing
        CriticalSection.Unlock();
        
        int32 ProcessedCount = ProcessDeferredEvents();
        
        // Re-acquire the lock
        CriticalSection.Lock();
        
        return ProcessedCount;
    }
    
    return 0;
}

bool UEventDispatcher::IsEventDispatchingSuspended() const
{
    return bDispatchingSuspended;
}

TMap<FName, int32> UEventDispatcher::GetEventStats() const
{
    if (!bIsInitialized)
    {
        return TMap<FName, int32>();
    }
    
    FScopeLock Lock(&CriticalSection);
    return EventStats;
}

void UEventDispatcher::ResetEventStats()
{
    if (!bIsInitialized)
    {
        return;
    }
    
    FScopeLock Lock(&CriticalSection);
    EventStats.Empty();
}

void UEventDispatcher::QueueEvent(const FName& EventName, const FEventData& EventData, 
    const FEventDispatchOptions& Options)
{
    FScopeLock Lock(&CriticalSection);
    PendingEvents.Emplace(EventName, EventData, Options);
}

void UEventDispatcher::UpdateEventStats(const FName& EventName, const FEventDispatchResult& Result)
{
    if (!EventStats.Contains(EventName))
    {
        EventStats.Add(EventName, 0);
    }
    
    EventStats[EventName]++;
}