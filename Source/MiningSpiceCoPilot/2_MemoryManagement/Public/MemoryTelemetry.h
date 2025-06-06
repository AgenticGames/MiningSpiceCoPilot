// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IMemoryTracker.h"
#include "HAL/CriticalSection.h"
#include "HAL/ThreadSafeBool.h"
#include "Containers/Map.h"

/**
 * Memory telemetry implementation for tracking memory usage in the SVO+SDF architecture
 * Provides detailed analytics about memory allocation patterns, particularly for SVO nodes and SDF data
 */
class MININGSPICECOPILOT_API FMemoryTelemetry : public IMemoryTracker
{
public:
    /** Constructor */
    FMemoryTelemetry();

    /** Destructor */
    virtual ~FMemoryTelemetry();

    //~ Begin IMemoryTracker Interface
    virtual bool Initialize() override;
    virtual void Shutdown() override;
    virtual bool IsInitialized() const override;
    
    virtual bool TrackAllocation(void* Ptr, uint64 SizeInBytes, const FName& CategoryName, 
        const FName& AllocationName = NAME_None, const UObject* RequestingObject = nullptr) override;
    
    virtual bool UntrackAllocation(void* Ptr) override;
    
    virtual const FMemoryAllocationInfo* GetAllocationInfo(void* Ptr) const override;
    
    virtual FMemoryStats GetMemoryStats() const override;
    
    virtual FSVOSDFMemoryMetrics GetSVOSDFMemoryMetrics() const override;
    
    virtual void SetMemoryBudget(const FName& CategoryName, uint64 BudgetInBytes) override;
    
    virtual uint64 GetMemoryBudget(const FName& CategoryName) const override;
    
    virtual uint64 GetMemoryUsage(const FName& CategoryName) const override;
    
    virtual bool SetAllocationTag(void* Ptr, const FName& Tag) override;
    
    virtual bool SetAllocationTier(void* Ptr, EMemoryTier Tier) override;
    
    virtual bool SetAllocationAccessPattern(void* Ptr, EMemoryAccessPattern AccessPattern) override;
    
    virtual void EnableCallStackTracking(bool bEnable, uint32 StackTraceDepth = 16) override;
    
    virtual TArray<FMemoryAllocationInfo> GetAllocationsByCategory(const FName& CategoryName) const override;
    
    virtual TArray<FMemoryAllocationInfo> GetAllocationsByTag(const FName& Tag) const override;
    
    virtual TArray<FMemoryAllocationInfo> GetAllocationsByTier(EMemoryTier Tier) const override;
    
    virtual TArray<FMemoryAllocationInfo> GetAllocationsBySize(uint64 MinSizeInBytes) const override;
    
    virtual FString GenerateMemoryReport(bool bDetailed = false) const override;
    
    virtual void LogMemoryStatistics(bool bDetailed = false) const override;
    
    virtual bool ValidateStats(TArray<FString>& OutErrors) const override;

    virtual void ResetStatistics() override;

    // Add this method to allow safe release of the memory tracker
    virtual void Release() override
    {
        // Shut down the tracker before releasing
        if (IsInitialized())
        {
            Shutdown();
        }
        
        // Safe self-deletion
        delete this;
    }
    
    /**
     * Track a memory pool for telemetry
     * @param Pool Pool to track
     * @param Category Category for the pool in telemetry
     * @return True if the pool was successfully tracked
     */
    virtual bool TrackPool(IPoolAllocator* Pool, const FName& Category) override;
    //~ End IMemoryTracker Interface
    
    /**
     * Updates SVO+SDF specific metrics
     * @param Metrics Updated metrics
     */
    void UpdateSVOSDFMetrics(const FSVOSDFMemoryMetrics& Metrics);
    
    /**
     * Tracks peak memory usage over time
     * @return Peak memory usage
     */
    uint64 GetPeakMemoryUsage() const;
    
    /**
     * Gets average memory allocation size
     * @return Average allocation size in bytes
     */
    uint64 GetAverageAllocationSize() const;
    
    /**
     * Gets a histogram of allocation sizes
     * @return Map of size bucket to allocation count
     */
    TMap<uint64, uint32> GetAllocationSizeHistogram() const;
    
    /**
     * Gets memory usage over time
     * @return Array of memory usage samples over time
     */
    TArray<TPair<double, uint64>> GetMemoryUsageTimeline() const;
    
    /**
     * Gets memory usage by NUMA node (if NUMA is enabled)
     * @return Map of NUMA node to memory usage
     */
    TMap<int32, uint64> GetMemoryUsageByNUMANode() const;
    
    /**
     * Gets memory pressure metric (0-1 range where 1 is highest pressure)
     * @return Memory pressure value
     */
    float GetMemoryPressure() const;
    
    /**
     * Takes a memory snapshot for later comparison
     * @param SnapshotName Name for this snapshot
     */
    void TakeMemorySnapshot(const FString& SnapshotName);
    
    /**
     * Compares current state with a named snapshot
     * @param SnapshotName Name of snapshot to compare with
     * @return String containing the comparison report
     */
    FString CompareWithSnapshot(const FString& SnapshotName) const;
    
private:
    /**
     * Updates memory statistics
     */
    void UpdateMemoryStats() const;
    
    /**
     * Captures the current call stack
     * @param OutCallStack Array to receive the call stack
     * @param SkipFrames Number of stack frames to skip
     */
    void CaptureCallStack(TArray<FString>& OutCallStack, uint32 SkipFrames = 2) const;
    
    /**
     * Calculates fragmentation metrics
     * @return Fragmentation percentage (0-100)
     */
    float CalculateFragmentation() const;
    
    /** Whether the tracker has been initialized */
    FThreadSafeBool bIsInitialized;
    
    /** Whether call stack tracking is enabled */
    FThreadSafeBool bCallStackTrackingEnabled;
    
    /** Depth of stack traces to capture */
    uint32 StackTraceDepth;
    
    /** Map of allocations by pointer */
    TMap<void*, FMemoryAllocationInfo> Allocations;
    
    /** Map of memory budgets by category */
    TMap<FName, uint64> MemoryBudgets;
    
    /** Map of memory usage by category */
    mutable TMap<FName, uint64> MemoryUsageByCategory;
    
    /** Map of allocation counts by category */
    mutable TMap<FName, uint32> AllocationCountByCategory;
    
    /** Map of memory usage by tier */
    mutable TMap<EMemoryTier, uint64> MemoryUsageByTier;
    
    /** Map of memory usage by access pattern */
    mutable TMap<EMemoryAccessPattern, uint64> MemoryUsageByAccessPattern;
    
    /** Lock for synchronizing access to allocation data */
    mutable FCriticalSection AllocationLock;
    
    /** Lock for synchronizing access to budget data */
    mutable FCriticalSection BudgetLock;
    
    /** Cached memory statistics */
    mutable FMemoryStats CachedMemoryStats;
    
    /** Cached SVO+SDF metrics */
    mutable FSVOSDFMemoryMetrics CachedSVOSDFMetrics;
    
    /** Time when statistics were last updated */
    mutable double LastStatsUpdateTime;
    
    /** Total allocated memory */
    mutable uint64 TotalAllocatedMemory;
    
    /** Peak memory usage */
    mutable uint64 PeakMemoryUsage;
    
    /** Total number of allocations */
    mutable uint32 TotalAllocationCount;
    
    /** Total number of active allocations */
    mutable uint32 ActiveAllocationCount;
    
    /** Memory snapshots for comparison */
    TMap<FString, FMemoryStats> MemorySnapshots;
    
    /** Memory timeline samples (time, memory usage) */
    TArray<TPair<double, uint64>> MemoryTimeline;
    
    /** Whether telemetry stats need updating */
    mutable bool bStatsDirty;
    
    /** Whether SVO+SDF metrics need updating */
    mutable bool bSVOSDFMetricsDirty;
};
