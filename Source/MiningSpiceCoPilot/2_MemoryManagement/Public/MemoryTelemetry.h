// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "2_MemoryManagement/Public/Interfaces/IMemoryTracker.h"
#include "HAL/ThreadSafeBool.h"
#include "Containers/CircularQueue.h"
#include "Stats/Stats.h"

// Forward declarations
class IMemoryManager;
class IPoolAllocator;
class IBufferProvider;
class FJsonObject;

/**
 * Memory usage statistics for visualization and analysis
 */
struct FMemoryUsageStats
{
    /** Total allocated memory in bytes */
    uint64 TotalAllocatedBytes;
    
    /** Total free memory in bytes */
    uint64 TotalFreeBytes;
    
    /** Peak memory usage in bytes */
    uint64 PeakMemoryUsage;
    
    /** Total number of allocations */
    uint32 AllocationCount;
    
    /** Total number of deallocations */
    uint32 DeallocationCount;
    
    /** Fragmentation percentage (0-100) */
    float FragmentationPercentage;
    
    /** Memory tracking timestamp */
    double Timestamp;
    
    /** Default constructor */
    FMemoryUsageStats()
        : TotalAllocatedBytes(0)
        , TotalFreeBytes(0)
        , PeakMemoryUsage(0)
        , AllocationCount(0)
        , DeallocationCount(0)
        , FragmentationPercentage(0.0f)
        , Timestamp(0.0)
    {
    }
    
    /** Reset all statistics */
    void Reset()
    {
        TotalAllocatedBytes = 0;
        TotalFreeBytes = 0;
        PeakMemoryUsage = 0;
        AllocationCount = 0;
        DeallocationCount = 0;
        FragmentationPercentage = 0.0f;
        Timestamp = 0.0;
    }
};

/** Allocation tracking by category */
struct FAllocationCategory
{
    /** Name of the category */
    FName CategoryName;
    
    /** Total memory allocated for this category */
    uint64 AllocatedBytes;
    
    /** Number of allocations in this category */
    uint32 AllocationCount;
    
    /** Peak memory usage for this category */
    uint64 PeakAllocatedBytes;
    
    /** Default constructor */
    FAllocationCategory()
        : CategoryName(NAME_None)
        , AllocatedBytes(0)
        , AllocationCount(0)
        , PeakAllocatedBytes(0)
    {
    }
    
    /** Constructor with category name */
    explicit FAllocationCategory(const FName& InCategoryName)
        : CategoryName(InCategoryName)
        , AllocatedBytes(0)
        , AllocationCount(0)
        , PeakAllocatedBytes(0)
    {
    }
};

/**
 * Memory allocation event for tracking and visualization
 */
struct FMemoryEvent
{
    /** Event types */
    enum class EEventType : uint8
    {
        Allocation,
        Deallocation,
        Resize,
        Defragmentation,
        PoolCreation,
        PoolDestruction,
        SystemPause,
        SystemResume
    };
    
    /** Type of memory event */
    EEventType Type;
    
    /** Memory address */
    void* Address;
    
    /** Size in bytes */
    uint64 Size;
    
    /** Previous size in bytes (for resize events) */
    uint64 PreviousSize;
    
    /** Allocation tag */
    FName Tag;
    
    /** Category of the allocation */
    FName Category;
    
    /** Timestamp of the event */
    double Timestamp;
    
    /** Thread ID that triggered the event */
    uint32 ThreadId;
    
    /** Call stack at the time of the event (if enabled) */
    TArray<FString> CallStack;
    
    /** Default constructor */
    FMemoryEvent()
        : Type(EEventType::Allocation)
        , Address(nullptr)
        , Size(0)
        , PreviousSize(0)
        , Tag(NAME_None)
        , Category(NAME_None)
        , Timestamp(0.0)
        , ThreadId(0)
    {
    }
};

/**
 * Memory telemetry system for tracking and analyzing memory usage
 * Provides detailed allocation tracking and visualization
 */
class MININGSPICECOPILOT_API FMemoryTelemetry : public IMemoryTracker
{
public:
    /**
     * Constructor
     * @param InMemoryManager Memory manager reference
     */
    explicit FMemoryTelemetry(IMemoryManager* InMemoryManager);
    
    /** Destructor */
    virtual ~FMemoryTelemetry();
    
    //~ Begin IMemoryTracker Interface
    virtual bool Initialize() override;
    virtual void Shutdown() override;
    virtual bool IsInitialized() const override;
    virtual void TrackAllocation(void* Ptr, uint64 Size, const FName& Tag, const FName& Category) override;
    virtual void TrackDeallocation(void* Ptr) override;
    virtual void TrackResize(void* OldPtr, void* NewPtr, uint64 NewSize) override;
    virtual void GetMemoryStats(FMemoryStatsDesc& OutStats) const override;
    virtual bool EnableCallStackTracking(bool bEnable) override;
    virtual bool IsCallStackTrackingEnabled() const override;
    virtual void SetMemoryBudget(uint64 BudgetInBytes) override;
    virtual uint64 GetMemoryBudget() const override;
    virtual float GetMemoryUsagePercentage() const override;
    virtual void ResetStats() override;
    virtual bool GenerateMemoryReport(const FString& ReportFilePath) const override;
    virtual bool SetCategoryBudget(const FName& Category, uint64 BudgetInBytes) override;
    virtual uint64 GetCategoryBudget(const FName& Category) const override;
    virtual float GetCategoryUsagePercentage(const FName& Category) const override;
    //~ End IMemoryTracker Interface
    
    /**
     * Track a pool creation event
     * @param Pool The pool that was created
     */
    void TrackPoolCreation(const IPoolAllocator* Pool);
    
    /**
     * Track a pool destruction event
     * @param Pool The pool that was destroyed
     */
    void TrackPoolDestruction(const IPoolAllocator* Pool);
    
    /**
     * Track a buffer creation event
     * @param Buffer The buffer that was created
     */
    void TrackBufferCreation(const IBufferProvider* Buffer);
    
    /**
     * Track a buffer destruction event
     * @param Buffer The buffer that was destroyed
     */
    void TrackBufferDestruction(const IBufferProvider* Buffer);
    
    /**
     * Track a defragmentation event
     * @param PoolName Name of the defragmented pool
     * @param BytesMoved Number of bytes moved during defragmentation
     * @param AllocationsMoved Number of allocations moved
     * @param Duration Duration of the defragmentation operation in seconds
     */
    void TrackDefragmentation(const FName& PoolName, uint64 BytesMoved, uint32 AllocationsMoved, double Duration);
    
    /**
     * Track a system pause event
     */
    void TrackSystemPause();
    
    /**
     * Track a system resume event
     */
    void TrackSystemResume();
    
    /**
     * Set the maximum number of memory events to keep in history
     * @param MaxEvents Maximum number of events
     */
    void SetMaxEventHistory(uint32 MaxEvents);
    
    /**
     * Get the current memory usage statistics
     * @param OutStats Statistics structure to fill
     */
    void GetMemoryUsageStats(FMemoryUsageStats& OutStats) const;
    
    /**
     * Get memory allocation events within a time range
     * @param StartTime Start time in seconds
     * @param EndTime End time in seconds
     * @param OutEvents Array of events to fill
     * @param MaxEvents Maximum number of events to return
     * @return Number of events returned
     */
    uint32 GetMemoryEvents(
        double StartTime,
        double EndTime,
        TArray<FMemoryEvent>& OutEvents,
        uint32 MaxEvents = UINT32_MAX) const;
    
    /**
     * Get memory usage over time at specific intervals
     * @param StartTime Start time in seconds
     * @param EndTime End time in seconds
     * @param IntervalSeconds Interval between samples in seconds
     * @param OutTimePoints Array of time points to fill
     * @param OutMemoryUsage Array of memory usage values to fill (in bytes)
     * @return Number of samples returned
     */
    uint32 GetMemoryUsageOverTime(
        double StartTime,
        double EndTime,
        double IntervalSeconds,
        TArray<double>& OutTimePoints,
        TArray<uint64>& OutMemoryUsage) const;
    
    /**
     * Get per-category allocation information
     * @param OutCategories Array of category information to fill
     */
    void GetAllocationCategories(TArray<FAllocationCategory>& OutCategories) const;
    
    /**
     * Get allocation hotspots (most memory usage by call stack)
     * @param OutHotspots Map of call stacks to allocation sizes
     * @param MaxHotspots Maximum number of hotspots to return
     * @return Number of hotspots returned
     */
    uint32 GetAllocationHotspots(
        TMap<FString, uint64>& OutHotspots,
        uint32 MaxHotspots = 10) const;
    
    /**
     * Save memory visualization data to a file for external analysis
     * @param FilePath Path to save the file
     * @return True if the file was saved successfully
     */
    bool SaveVisualizationData(const FString& FilePath) const;
    
    /**
     * Set whether to track memory leaks
     * @param bTrack Whether to track leaks
     */
    void SetTrackLeaks(bool bTrack);
    
    /**
     * Check for memory leaks
     * @param OutLeaks Array of leak information to fill
     * @return Number of leaks detected
     */
    uint32 CheckForLeaks(TArray<TPair<void*, FMemoryEvent>>& OutLeaks) const;
    
    /**
     * Enable or disable monitoring of a specific memory category
     * @param Category Category name
     * @param bEnable Whether to enable monitoring
     */
    void SetCategoryMonitoring(const FName& Category, bool bEnable);
    
    /**
     * Check if a category is being monitored
     * @param Category Category name
     * @return True if the category is being monitored
     */
    bool IsCategoryMonitored(const FName& Category) const;
    
    /**
     * Set threshold for memory pressure warnings
     * @param UsagePercentage Usage percentage threshold (0-100)
     */
    void SetMemoryPressureThreshold(float UsagePercentage);
    
    /**
     * Check for memory pressure
     * @return Current memory pressure (0-100)
     */
    float GetMemoryPressure() const;

protected:
    /** Memory manager reference */
    IMemoryManager* MemoryManager;
    
    /** Whether the telemetry system is initialized */
    FThreadSafeBool bIsInitialized;
    
    /** Current memory usage statistics */
    FMemoryUsageStats CurrentStats;
    
    /** Map of addresses to allocation events for tracking */
    TMap<void*, FMemoryEvent> AllocatedAddresses;
    
    /** History of memory events for analysis */
    TCircularQueue<FMemoryEvent> EventHistory;
    
    /** Maximum number of events to keep in history */
    uint32 MaxEventHistory;
    
    /** Map of categories to allocation information */
    TMap<FName, FAllocationCategory> Categories;
    
    /** Map of call stacks to allocation information */
    TMap<FString, uint64> CallStackAllocations;
    
    /** Critical section for thread safety */
    mutable FCriticalSection TelemetryLock;
    
    /** Memory budget in bytes */
    uint64 MemoryBudget;
    
    /** Map of category budgets */
    TMap<FName, uint64> CategoryBudgets;
    
    /** Set of monitored categories */
    TSet<FName> MonitoredCategories;
    
    /** Whether call stack tracking is enabled */
    bool bCallStackTrackingEnabled;
    
    /** Whether leak tracking is enabled */
    bool bTrackLeaks;
    
    /** Memory pressure threshold percentage (0-100) */
    float MemoryPressureThreshold;
    
    /** Stats data for integration with UE stats system */
    mutable FDelegateHandle MemoryStatDelegateHandle;

private:
    /**
     * Get the current timestamp
     * @return Current time in seconds
     */
    double GetTimestamp() const;
    
    /**
     * Get the current call stack
     * @param OutCallStack Array to fill with call stack information
     * @param SkipFrames Number of frames to skip
     */
    void GetCallStack(TArray<FString>& OutCallStack, uint32 SkipFrames = 2) const;
    
    /**
     * Update peak memory usage statistics
     * @param AllocatedBytes Current allocated bytes
     */
    void UpdatePeakMemoryUsage(uint64 AllocatedBytes);
    
    /**
     * Convert memory events to JSON for visualization
     * @param StartTime Start time for events
     * @param EndTime End time for events
     * @return JSON object with memory event data
     */
    TSharedPtr<FJsonObject> ConvertEventsToJson(double StartTime, double EndTime) const;
    
    /** Register with the UE stats system */
    void RegisterWithStatsSystem();
    
    /** Unregister from the UE stats system */
    void UnregisterFromStatsSystem();
    
    /**
     * Check if a memory category is valid
     * @param Category Category to check
     * @return True if the category is valid
     */
    bool IsValidCategory(const FName& Category) const;
};
