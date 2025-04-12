// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateChangeLogger.h"
#include "EventBus.h"
#include "HAL/PlatformTime.h"
#include "Algo/Sort.h"

void FEventStatistics::UpdateWithEvent(const FEventData& EventData)
{
    TotalEvents++;
    LastUpdateTimeSeconds = FPlatformTime::Seconds();
    
    const FEventContext& Context = EventData.Context;
    
    // Update event type count
    if (!EventCounts.Contains(Context.EventType))
    {
        EventCounts.Add(Context.EventType, 0);
    }
    EventCounts[Context.EventType]++;
    
    // Update region count if applicable
    if (Context.RegionId != INDEX_NONE)
    {
        if (!RegionEventCounts.Contains(Context.RegionId))
        {
            RegionEventCounts.Add(Context.RegionId, 0);
        }
        RegionEventCounts[Context.RegionId]++;
        
        // Update zone count if applicable
        if (Context.ZoneId != INDEX_NONE)
        {
            TPair<int32, int32> RegionZonePair(Context.RegionId, Context.ZoneId);
            if (!ZoneEventCounts.Contains(RegionZonePair))
            {
                ZoneEventCounts.Add(RegionZonePair, 0);
            }
            ZoneEventCounts[RegionZonePair]++;
        }
    }
    
    // Update priority count
    if (!PriorityCounts.Contains(Context.Priority))
    {
        PriorityCounts.Add(Context.Priority, 0);
    }
    PriorityCounts[Context.Priority]++;
    
    // Update scope count
    if (!ScopeCounts.Contains(Context.Scope))
    {
        ScopeCounts.Add(Context.Scope, 0);
    }
    ScopeCounts[Context.Scope]++;
    
    // Update correlation count if applicable
    if (Context.CorrelationId.IsValid())
    {
        if (!CorrelationCounts.Contains(Context.CorrelationId))
        {
            CorrelationCounts.Add(Context.CorrelationId, 0);
        }
        CorrelationCounts[Context.CorrelationId]++;
    }
    
    // Update cancelled count
    if (Context.bCancelled)
    {
        CancelledCount++;
    }
}

void FEventStatistics::Clear()
{
    EventCounts.Empty();
    RegionEventCounts.Empty();
    ZoneEventCounts.Empty();
    PriorityCounts.Empty();
    ScopeCounts.Empty();
    CorrelationCounts.Empty();
    CancelledCount = 0;
    TotalEvents = 0;
    LastUpdateTimeSeconds = FPlatformTime::Seconds();
}

UStateChangeLogger* UStateChangeLogger::Get()
{
    static UStateChangeLogger* Singleton = nullptr;
    if (!Singleton)
    {
        Singleton = NewObject<UStateChangeLogger>();
        Singleton->Initialize();
    }
    return Singleton;
}

bool UStateChangeLogger::Initialize()
{
    if (bIsInitialized)
    {
        return true;
    }
    
    Name = TEXT("StateChangeLogger");
    bIsInitialized = true;
    bLoggingActive = false;
    MaxLoggedEvents = 10000;
    bUseCircularBuffer = true;
    CircularBufferIndex = 0;
    
    // Create event handling delegate
    LoggingDelegate.BindUObject(this, &UStateChangeLogger::HandleEvent);
    
    return true;
}

void UStateChangeLogger::Shutdown()
{
    if (!bIsInitialized)
    {
        return;
    }
    
    StopLogging();
    
    FScopeLock Lock(&CriticalSection);
    
    // Clear collections
    EventLog.Empty();
    EventIndexMap.Empty();
    ReferenceMap.Empty();
    SubscriptionIds.Empty();
    
    bIsInitialized = false;
}

bool UStateChangeLogger::IsInitialized() const
{
    return bIsInitialized;
}

FString UStateChangeLogger::GetSubscriberName() const
{
    return Name;
}

void UStateChangeLogger::SetSubscriberName(const FString& SubscriberName)
{
    Name = SubscriberName;
}

FGuid UStateChangeLogger::SubscribeToEvent(const FName& EventType, const FEventHandlerDelegate& Handler, 
    const FSubscriptionOptions& Options)
{
    // StateChangeLogger doesn't actually handle subscription management itself
    // Instead, we forward to the EventBus singleton
    UEventBus* Bus = UEventBus::Get();
    if (Bus && Bus->IsInitialized())
    {
        return Bus->SubscribeToEvent(EventType, Handler, Options);
    }
    
    return FGuid();
}

TMap<FName, FGuid> UStateChangeLogger::SubscribeToEvents(const TArray<FName>& EventTypes, 
    const FEventHandlerDelegate& Handler, const FSubscriptionOptions& Options)
{
    UEventBus* Bus = UEventBus::Get();
    if (Bus && Bus->IsInitialized())
    {
        return Bus->SubscribeToEvents(EventTypes, Handler, Options);
    }
    
    return TMap<FName, FGuid>();
}

FGuid UStateChangeLogger::SubscribeToNamespace(const FString& Namespace, const FEventHandlerDelegate& Handler,
    const FSubscriptionOptions& Options)
{
    UEventBus* Bus = UEventBus::Get();
    if (Bus && Bus->IsInitialized())
    {
        return Bus->SubscribeToNamespace(Namespace, Handler, Options);
    }
    
    return FGuid();
}

FGuid UStateChangeLogger::SubscribeToCorrelation(const FGuid& CorrelationId, const FEventHandlerDelegate& Handler,
    const FSubscriptionOptions& Options)
{
    UEventBus* Bus = UEventBus::Get();
    if (Bus && Bus->IsInitialized())
    {
        return Bus->SubscribeToCorrelation(CorrelationId, Handler, Options);
    }
    
    return FGuid();
}

bool UStateChangeLogger::Unsubscribe(const FGuid& SubscriptionId)
{
    UEventBus* Bus = UEventBus::Get();
    if (Bus && Bus->IsInitialized())
    {
        return Bus->Unsubscribe(SubscriptionId);
    }
    
    return false;
}

int32 UStateChangeLogger::UnsubscribeAll(const FName& EventType)
{
    UEventBus* Bus = UEventBus::Get();
    if (Bus && Bus->IsInitialized())
    {
        return Bus->UnsubscribeAll(EventType);
    }
    
    return 0;
}

int32 UStateChangeLogger::UnsubscribeAll()
{
    UEventBus* Bus = UEventBus::Get();
    if (Bus && Bus->IsInitialized())
    {
        return Bus->UnsubscribeAll();
    }
    
    return 0;
}

const FSubscriptionInfo* UStateChangeLogger::GetSubscriptionInfo(const FGuid& SubscriptionId) const
{
    UEventBus* Bus = UEventBus::Get();
    if (Bus && Bus->IsInitialized())
    {
        return Bus->GetSubscriptionInfo(SubscriptionId);
    }
    
    return nullptr;
}

TArray<FSubscriptionInfo> UStateChangeLogger::GetAllSubscriptions() const
{
    UEventBus* Bus = UEventBus::Get();
    if (Bus && Bus->IsInitialized())
    {
        return Bus->GetAllSubscriptions();
    }
    
    return TArray<FSubscriptionInfo>();
}

int32 UStateChangeLogger::GetSubscriptionCount() const
{
    UEventBus* Bus = UEventBus::Get();
    if (Bus && Bus->IsInitialized())
    {
        return Bus->GetSubscriptionCount();
    }
    
    return 0;
}

void UStateChangeLogger::StartLogging(int32 MaxEvents, bool CircularBuffer)
{
    if (!bIsInitialized || bLoggingActive)
    {
        return;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    MaxLoggedEvents = MaxEvents > 0 ? MaxEvents : 10000;
    bUseCircularBuffer = CircularBuffer;
    CircularBufferIndex = 0;
    
    // Create a subscription to all events
    FSubscriptionOptions Options;
    Options.MinPriorityLevel = EEventPriority::Background;
    Options.MaxPriorityLevel = EEventPriority::Critical;
    Options.bReceiveInPublisherThread = false; // Process in background
    
    // Subscribe to all events (by using a namespace prefix of empty string)
    FGuid SubscriptionId = SubscribeToNamespace(TEXT(""), LoggingDelegate, Options);
    if (SubscriptionId.IsValid())
    {
        SubscriptionIds.Add(NAME_None, SubscriptionId);
        bLoggingActive = true;
    }
}

void UStateChangeLogger::StopLogging()
{
    if (!bIsInitialized || !bLoggingActive)
    {
        return;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    // Unsubscribe from all events
    for (const auto& Entry : SubscriptionIds)
    {
        Unsubscribe(Entry.Value);
    }
    
    SubscriptionIds.Empty();
    bLoggingActive = false;
}

bool UStateChangeLogger::IsLoggingActive() const
{
    return bLoggingActive;
}

void UStateChangeLogger::AddEventTag(const FGuid& EventId, const FString& Tag)
{
    if (!bIsInitialized || !EventId.IsValid() || Tag.IsEmpty())
    {
        return;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    if (EventIndexMap.Contains(EventId))
    {
        int32 Index = EventIndexMap[EventId];
        if (EventLog.IsValidIndex(Index))
        {
            EventLog[Index].Tags.AddUnique(Tag);
        }
    }
}

FGuid UStateChangeLogger::LinkEvents(const TArray<FGuid>& EventIds, const FGuid& ReferenceId)
{
    if (!bIsInitialized || EventIds.Num() == 0)
    {
        return FGuid();
    }
    
    FScopeLock Lock(&CriticalSection);
    
    // Generate a reference ID if not provided
    FGuid ActualReferenceId = ReferenceId.IsValid() ? ReferenceId : FGuid::NewGuid();
    
    for (const FGuid& EventId : EventIds)
    {
        if (EventId.IsValid() && EventIndexMap.Contains(EventId))
        {
            int32 Index = EventIndexMap[EventId];
            if (EventLog.IsValidIndex(Index))
            {
                // Set reference ID in the event
                EventLog[Index].ReferenceId = ActualReferenceId;
                
                // Add to reference map
                ReferenceMap.Add(ActualReferenceId, EventId);
            }
        }
    }
    
    return ActualReferenceId;
}

TArray<FLoggedEvent> UStateChangeLogger::GetRecentEvents(int32 Count, const FEventLogFilter& Filter) const
{
    TArray<FLoggedEvent> Result;
    
    if (!bIsInitialized || Count <= 0)
    {
        return Result;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    // Start from the newest events
    for (int32 i = EventLog.Num() - 1; i >= 0; --i)
    {
        const FLoggedEvent& Event = EventLog[i];
        
        if (ApplyFilter(Event, Filter))
        {
            Result.Add(Event);
            
            if (Result.Num() >= Count)
            {
                break;
            }
        }
    }
    
    return Result;
}

TArray<FLoggedEvent> UStateChangeLogger::GetEventsInTimeRange(double StartTime, double EndTime, 
    const FEventLogFilter& Filter) const
{
    TArray<FLoggedEvent> Result;
    
    if (!bIsInitialized || StartTime >= EndTime)
    {
        return Result;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    for (const FLoggedEvent& Event : EventLog)
    {
        if (Event.LogTimeSeconds >= StartTime && Event.LogTimeSeconds <= EndTime && 
            ApplyFilter(Event, Filter))
        {
            Result.Add(Event);
        }
    }
    
    // Sort by time (oldest first)
    Algo::Sort(Result, [](const FLoggedEvent& A, const FLoggedEvent& B) {
        return A.LogTimeSeconds < B.LogTimeSeconds;
    });
    
    return Result;
}

TArray<FLoggedEvent> UStateChangeLogger::GetEventsByReference(const FGuid& ReferenceId) const
{
    TArray<FLoggedEvent> Result;
    
    if (!bIsInitialized || !ReferenceId.IsValid())
    {
        return Result;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    TArray<FGuid> EventIds;
    ReferenceMap.MultiFind(ReferenceId, EventIds);
    
    for (const FGuid& EventId : EventIds)
    {
        if (EventIndexMap.Contains(EventId))
        {
            int32 Index = EventIndexMap[EventId];
            if (EventLog.IsValidIndex(Index))
            {
                Result.Add(EventLog[Index]);
            }
        }
    }
    
    // Sort by time (oldest first)
    Algo::Sort(Result, [](const FLoggedEvent& A, const FLoggedEvent& B) {
        return A.LogTimeSeconds < B.LogTimeSeconds;
    });
    
    return Result;
}

FEventStatistics UStateChangeLogger::GetEventStatistics(double TimeRange, const FEventLogFilter& Filter) const
{
    FEventStatistics Result;
    
    if (!bIsInitialized)
    {
        return Result;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    double CurrentTime = FPlatformTime::Seconds();
    double StartTime = TimeRange > 0 ? CurrentTime - TimeRange : 0;
    
    for (const FLoggedEvent& Event : EventLog)
    {
        if ((TimeRange <= 0 || Event.LogTimeSeconds >= StartTime) && 
            ApplyFilter(Event, Filter))
        {
            Result.UpdateWithEvent(Event.EventData);
        }
    }
    
    return Result;
}

TArray<FEventSequencePattern> UStateChangeLogger::AnalyzeEventPatterns(
    int32 SequenceLength, int32 MinOccurrences, float MaxTimeGapMs) const
{
    TArray<FEventSequencePattern> Result;
    
    if (!bIsInitialized || SequenceLength < 2 || EventLog.Num() < SequenceLength)
    {
        return Result;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    // This is a simplified implementation of sequence pattern detection
    // In a real implementation, we would use more sophisticated algorithms
    
    // Get a sorted copy of all events
    TArray<FLoggedEvent> SortedEvents = EventLog;
    Algo::Sort(SortedEvents, [](const FLoggedEvent& A, const FLoggedEvent& B) {
        return A.LogTimeSeconds < B.LogTimeSeconds;
    });
    
    // Map to track patterns
    TMap<FString, FEventSequencePattern> PatternMap;
    
    // Slide a window of size SequenceLength through the events
    for (int32 i = 0; i <= SortedEvents.Num() - SequenceLength; ++i)
    {
        // Check if the time gap between first and last event is within limits
        double StartTime = SortedEvents[i].LogTimeSeconds;
        double EndTime = SortedEvents[i + SequenceLength - 1].LogTimeSeconds;
        
        if ((EndTime - StartTime) * 1000.0 <= MaxTimeGapMs)
        {
            // Build the sequence signature
            FString Signature;
            TArray<FName> EventTypes;
            float TotalTimeMs = 0.0f;
            
            for (int32 j = 0; j < SequenceLength; ++j)
            {
                const FLoggedEvent& Event = SortedEvents[i + j];
                EventTypes.Add(Event.EventData.Context.EventType);
                
                if (j > 0)
                {
                    TotalTimeMs += (float)((Event.LogTimeSeconds - SortedEvents[i + j - 1].LogTimeSeconds) * 1000.0);
                }
                
                Signature += Event.EventData.Context.EventType.ToString();
                if (j < SequenceLength - 1)
                {
                    Signature += TEXT(">");
                }
            }
            
            // Update pattern in map
            if (!PatternMap.Contains(Signature))
            {
                FEventSequencePattern Pattern;
                Pattern.EventSequence = EventTypes;
                Pattern.Count = 1;
                Pattern.AverageTimeMs = TotalTimeMs / (SequenceLength - 1);
                PatternMap.Add(Signature, Pattern);
            }
            else
            {
                FEventSequencePattern& Pattern = PatternMap[Signature];
                Pattern.Count++;
                
                // Update average time
                Pattern.AverageTimeMs = ((Pattern.AverageTimeMs * (Pattern.Count - 1)) + 
                    (TotalTimeMs / (SequenceLength - 1))) / Pattern.Count;
            }
        }
    }
    
    // Filter by minimum occurrences and add to result
    for (const auto& Entry : PatternMap)
    {
        if (Entry.Value.Count >= MinOccurrences)
        {
            Result.Add(Entry.Value);
        }
    }
    
    // Sort by count (most frequent first)
    Algo::Sort(Result, [](const FEventSequencePattern& A, const FEventSequencePattern& B) {
        return A.Count > B.Count;
    });
    
    return Result;
}

void UStateChangeLogger::ClearEventLog()
{
    if (!bIsInitialized)
    {
        return;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    EventLog.Empty();
    EventIndexMap.Empty();
    ReferenceMap.Empty();
    CircularBufferIndex = 0;
}

void UStateChangeLogger::HandleEvent(const FEventContext& Context, const TSharedRef<FJsonObject>& EventData)
{
    if (!bIsInitialized || !bLoggingActive)
    {
        return;
    }
    
    // Create an event data wrapper and log it
    FEventData Data(Context, EventData);
    LogEvent(Data);
}

int32 UStateChangeLogger::LogEvent(const FEventData& EventData)
{
    FScopeLock Lock(&CriticalSection);
    
    int32 Index = -1;
    
    if (MaxLoggedEvents > 0 && EventLog.Num() >= MaxLoggedEvents)
    {
        if (bUseCircularBuffer)
        {
            // Replace old event in circular buffer
            Index = CircularBufferIndex;
            
            // Remove old mappings
            if (EventLog.IsValidIndex(Index))
            {
                const FGuid& OldEventId = EventLog[Index].EventData.Context.EventId;
                EventIndexMap.Remove(OldEventId);
                
                // Also remove from reference map
                TArray<FGuid> ReferenceIds;
                ReferenceMap.GetKeys(OldEventId, ReferenceIds);
                for (const FGuid& RefId : ReferenceIds)
                {
                    ReferenceMap.Remove(RefId, OldEventId);
                }
            }
            
            // Update or add new event
            if (EventLog.IsValidIndex(Index))
            {
                EventLog[Index] = FLoggedEvent(EventData);
            }
            else
            {
                Index = EventLog.Add(FLoggedEvent(EventData));
            }
            
            // Add new mapping
            EventIndexMap.Add(EventData.Context.EventId, Index);
            
            // Update circular buffer index
            CircularBufferIndex = (CircularBufferIndex + 1) % MaxLoggedEvents;
        }
        else
        {
            // Ignore new events when buffer is full
            return -1;
        }
    }
    else
    {
        // Add new event to the end
        Index = EventLog.Add(FLoggedEvent(EventData));
        
        // Add mapping
        EventIndexMap.Add(EventData.Context.EventId, Index);
    }
    
    return Index;
}

bool UStateChangeLogger::ApplyFilter(const FLoggedEvent& Event, const FEventLogFilter& Filter) const
{
    const FEventContext& Context = Event.EventData.Context;
    
    // Check event type filter
    if (Filter.EventTypes.Num() > 0 && !Filter.EventTypes.Contains(Context.EventType))
    {
        return false;
    }
    
    // Check priority range
    if (Context.Priority < Filter.MinPriority || Context.Priority > Filter.MaxPriority)
    {
        return false;
    }
    
    // Check region filter
    if (Filter.RegionIds.Num() > 0 && Context.RegionId != INDEX_NONE && 
        !Filter.RegionIds.Contains(Context.RegionId))
    {
        return false;
    }
    
    // Check zone filter
    if (Filter.ZoneIds.Num() > 0 && Context.ZoneId != INDEX_NONE && 
        !Filter.ZoneIds.Contains(Context.ZoneId))
    {
        return false;
    }
    
    // Check scope filter
    if (Filter.Scopes.Num() > 0 && !Filter.Scopes.Contains(Context.Scope))
    {
        return false;
    }
    
    // Check cancellable filter
    if (Filter.bOnlyCancellable && !Context.bCancellable)
    {
        return false;
    }
    
    // Check cancelled filter
    if (Filter.bOnlyCancelled && !Context.bCancelled)
    {
        return false;
    }
    
    // Check correlation filter
    if (Filter.bOnlyCorrelated && !Context.CorrelationId.IsValid())
    {
        return false;
    }
    
    // Check specific correlation ID
    if (Filter.bOnlyCorrelated && Filter.CorrelationId.IsValid() && 
        Context.CorrelationId != Filter.CorrelationId)
    {
        return false;
    }
    
    return true;
}