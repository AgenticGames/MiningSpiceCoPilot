// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IEventHandler.h"
#include "Interfaces/IEventSubscriber.h"
#include "StateChangeLogger.generated.h"

/**
 * Event log filter settings
 */
struct MININGSPICECOPILOT_API FEventLogFilter
{
    /** Types of events to include */
    TArray<FName> EventTypes;

    /** Priority range to include */
    EEventPriority MinPriority;
    EEventPriority MaxPriority;

    /** Regions to include (empty for all) */
    TArray<int32> RegionIds;

    /** Zones to include (empty for all) */
    TArray<int32> ZoneIds;

    /** Scopes to include */
    TArray<EEventScope> Scopes;

    /** Only include cancellable events */
    bool bOnlyCancellable;

    /** Only include cancelled events */
    bool bOnlyCancelled;

    /** Only include events with correlation ID */
    bool bOnlyCorrelated;

    /** Specific correlation ID to filter (if valid) */
    FGuid CorrelationId;

    /** Constructor with default values */
    FEventLogFilter()
        : MinPriority(EEventPriority::Background)
        , MaxPriority(EEventPriority::Critical)
        , bOnlyCancellable(false)
        , bOnlyCancelled(false)
        , bOnlyCorrelated(false)
    {
        Scopes = { EEventScope::Global, EEventScope::Region, EEventScope::System, EEventScope::Debug };
    }

    /** Helper to create filter for just one event type */
    static FEventLogFilter ForEventType(const FName& EventType)
    {
        FEventLogFilter Filter;
        Filter.EventTypes.Add(EventType);
        return Filter;
    }

    /** Helper to create filter for a specific region */
    static FEventLogFilter ForRegion(int32 RegionId)
    {
        FEventLogFilter Filter;
        Filter.RegionIds.Add(RegionId);
        Filter.Scopes = { EEventScope::Region, EEventScope::Global };
        return Filter;
    }

    /** Helper to create filter for high priority events */
    static FEventLogFilter ForHighPriorityOnly()
    {
        FEventLogFilter Filter;
        Filter.MinPriority = EEventPriority::High;
        Filter.MaxPriority = EEventPriority::Critical;
        return Filter;
    }

    /** Helper to create filter for correlated events */
    static FEventLogFilter ForCorrelation(const FGuid& CorrelationId)
    {
        FEventLogFilter Filter;
        Filter.bOnlyCorrelated = true;
        Filter.CorrelationId = CorrelationId;
        return Filter;
    }
};

/**
 * Logged event entry
 */
struct MININGSPICECOPILOT_API FLoggedEvent
{
    /** Event data */
    FEventData EventData;

    /** Time when the event was logged */
    double LogTimeSeconds;

    /** Custom tags for this event */
    TArray<FString> Tags;

    /** Reference ID for related events */
    FGuid ReferenceId;

    /** Constructor */
    FLoggedEvent(const FEventData& InEventData)
        : EventData(InEventData)
        , LogTimeSeconds(FPlatformTime::Seconds())
    {
    }
};

/**
 * Event statistics
 */
struct MININGSPICECOPILOT_API FEventStatistics
{
    /** Map of event types to counts */
    TMap<FName, int32> EventCounts;

    /** Map of regions to event counts */
    TMap<int32, int32> RegionEventCounts;

    /** Map of zones to event counts */
    TMap<TPair<int32, int32>, int32> ZoneEventCounts;

    /** Map of priorities to counts */
    TMap<EEventPriority, int32> PriorityCounts;

    /** Map of scopes to counts */
    TMap<EEventScope, int32> ScopeCounts;

    /** Map of correlation IDs to counts */
    TMap<FGuid, int32> CorrelationCounts;

    /** Count of cancelled events */
    int32 CancelledCount;

    /** Total count of events */
    int32 TotalEvents;

    /** Last time statistics were updated */
    double LastUpdateTimeSeconds;

    /** Constructor */
    FEventStatistics()
        : CancelledCount(0)
        , TotalEvents(0)
        , LastUpdateTimeSeconds(0)
    {
    }

    /** Updates statistics with a new event */
    void UpdateWithEvent(const FEventData& EventData);

    /** Clears all statistics */
    void Clear();
};

/**
 * Event sequence pattern
 */
struct MININGSPICECOPILOT_API FEventSequencePattern
{
    /** Sequence of event types */
    TArray<FName> EventSequence;

    /** Count of this pattern */
    int32 Count;

    /** Average time between events in ms */
    float AverageTimeMs;

    /** Constructor */
    FEventSequencePattern()
        : Count(0)
        , AverageTimeMs(0.0f)
    {
    }
};

/**
 * State Change Logger
 * Records and analyzes event history for debugging and pattern recognition
 */
UCLASS()
class MININGSPICECOPILOT_API UStateChangeLogger : public UObject, public IEventSubscriber
{
    GENERATED_BODY()

public:
    // Begin IEventSubscriber Interface
    virtual bool Initialize() override;
    virtual void Shutdown() override;
    virtual bool IsInitialized() const override;
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
    
    /**
     * Starts logging events
     * @param MaxEvents Maximum number of events to store (0 for unlimited, limited by memory)
     * @param CircularBuffer Whether to use a circular buffer when max is reached
     */
    void StartLogging(int32 MaxEvents = 10000, bool CircularBuffer = true);
    
    /**
     * Stops logging events
     */
    void StopLogging();
    
    /**
     * Checks if logging is active
     * @return True if logging is active
     */
    bool IsLoggingActive() const;
    
    /**
     * Adds an event tag for identification
     * @param EventId ID of the event to tag
     * @param Tag Tag to add
     */
    void AddEventTag(const FGuid& EventId, const FString& Tag);
    
    /**
     * Links events as part of a related sequence
     * @param EventIds Array of events to link
     * @param ReferenceId Optional reference ID (if empty, a new one will be generated)
     * @return Reference ID for the linked events
     */
    FGuid LinkEvents(const TArray<FGuid>& EventIds, const FGuid& ReferenceId = FGuid());
    
    /**
     * Gets recent events from the log
     * @param Count Number of events to retrieve
     * @param Filter Optional filter
     * @return Array of logged events
     */
    TArray<FLoggedEvent> GetRecentEvents(int32 Count, const FEventLogFilter& Filter = FEventLogFilter()) const;
    
    /**
     * Gets events within a time range
     * @param StartTime Start time in seconds
     * @param EndTime End time in seconds
     * @param Filter Optional filter
     * @return Array of logged events
     */
    TArray<FLoggedEvent> GetEventsInTimeRange(double StartTime, double EndTime, 
        const FEventLogFilter& Filter = FEventLogFilter()) const;
    
    /**
     * Gets events by reference ID
     * @param ReferenceId Reference ID to search for
     * @return Array of linked events
     */
    TArray<FLoggedEvent> GetEventsByReference(const FGuid& ReferenceId) const;
    
    /**
     * Gets event statistics for analysis
     * @param TimeRange Optional time range in seconds (0 for all time)
     * @param Filter Optional filter
     * @return Event statistics
     */
    FEventStatistics GetEventStatistics(double TimeRange = 0.0, const FEventLogFilter& Filter = FEventLogFilter()) const;
    
    /**
     * Analyzes event sequences to find patterns
     * @param SequenceLength Length of sequences to analyze
     * @param MinOccurrences Minimum number of occurrences to consider a pattern
     * @param MaxTimeGapMs Maximum time gap between events in ms
     * @return Array of detected patterns
     */
    TArray<FEventSequencePattern> AnalyzeEventPatterns(int32 SequenceLength = 3, int32 MinOccurrences = 2, float MaxTimeGapMs = 1000.0f) const;
    
    /**
     * Clears the event log
     */
    void ClearEventLog();
    
    /**
     * Gets the singleton instance
     */
    static UStateChangeLogger* Get();
    
private:
    /** Whether the logger is initialized */
    bool bIsInitialized;
    
    /** Whether logging is currently active */
    bool bLoggingActive;
    
    /** Maximum number of events to store */
    int32 MaxLoggedEvents;
    
    /** Whether to use a circular buffer */
    bool bUseCircularBuffer;
    
    /** Circular buffer index */
    int32 CircularBufferIndex;
    
    /** Name of this subscriber */
    FString Name;
    
    /** Critical section for thread safety */
    mutable FCriticalSection CriticalSection;
    
    /** Event log storage */
    TArray<FLoggedEvent> EventLog;
    
    /** Map of event IDs to log indices for fast lookup */
    TMap<FGuid, int32> EventIndexMap;
    
    /** Map of reference IDs to event IDs */
    TMultiMap<FGuid, FGuid> ReferenceMap;
    
    /** Event handler delegate */
    FEventHandlerDelegate LoggingDelegate;
    
    /** Map of subscription IDs */
    TMap<FName, FGuid> SubscriptionIds;
    
    /**
     * Handler for logging events
     * @param Context Event context
     * @param EventData Event data payload
     */
    void HandleEvent(const FEventContext& Context, const TSharedRef<FJsonObject>& EventData);
    
    /**
     * Logs an event
     * @param EventData Event to log
     * @return Index of the logged event
     */
    int32 LogEvent(const FEventData& EventData);
    
    /**
     * Applies an event filter
     * @param Event Event to check
     * @param Filter Filter to apply
     * @return True if the event matches the filter
     */
    bool ApplyFilter(const FLoggedEvent& Event, const FEventLogFilter& Filter) const;
};