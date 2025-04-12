// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IMemoryManager.h"
#include "IMemoryTracker.generated.h"

/**
 * Memory allocation tracker entry for detailed monitoring
 */
struct MININGSPICECOPILOT_API FMemoryAllocationInfo
{
    /** Pointer to the allocated memory */
    void* Ptr;
    
    /** Size of the allocation in bytes */
    uint64 SizeInBytes;
    
    /** Category for budget tracking */
    FName CategoryName;
    
    /** Name for the allocation (optional) */
    FName AllocationName;
    
    /** Time when the allocation was made */
    double TimeStamp;
    
    /** Call stack for the allocation if tracking enabled */
    TArray<FString> CallStack;
    
    /** Pool name if allocated from a pool, otherwise empty */
    FName PoolName;
    
    /** Alignment requirement for the allocation */
    uint32 Alignment;
    
    /** Associated UObject if available */
    TWeakObjectPtr<const UObject> AssociatedObject;
    
    /** Tags associated with this allocation */
    TArray<FName> Tags;
    
    /** Memory tier classification for hierarchical memory management */
    EMemoryTier MemoryTier;
    
    /** Access pattern hint for the allocation */
    EMemoryAccessPattern AccessPattern;
};

/**
 * Structure containing memory usage statistics
 */
struct MININGSPICECOPILOT_API FMemoryStats
{
    /** Total allocated memory in bytes */
    uint64 TotalAllocatedBytes;
    
    /** Total number of allocations */
    uint64 AllocationCount;
    
    /** Peak memory usage in bytes */
    uint64 PeakMemoryUsage;
    
    /** Available physical memory in bytes */
    uint64 AvailablePhysicalMemory;
    
    /** Total physical memory in bytes */
    uint64 TotalPhysicalMemory;
    
    /** Virtual memory usage in bytes */
    uint64 VirtualMemoryUsage;
    
    /** Map of memory usage by category */
    TMap<FName, uint64> UsageByCategory;
    
    /** Map of allocation counts by category */
    TMap<FName, uint64> AllocationCountByCategory;
    
    /** Map of memory budgets by category */
    TMap<FName, uint64> BudgetByCategory;
    
    /** Map of memory usage by tier */
    TMap<EMemoryTier, uint64> UsageByTier;
    
    /** Map of memory usage by access pattern */
    TMap<EMemoryAccessPattern, uint64> UsageByAccessPattern;
};

/**
 * Structure containing SVO+SDF specific memory metrics
 */
struct MININGSPICECOPILOT_API FSVOSDFMemoryMetrics
{
    /** Memory used by SVO octree nodes in bytes */
    uint64 SVONodeMemory;
    
    /** Memory used by SDF field data in bytes */
    uint64 SDFFieldMemory;
    
    /** Memory used by narrow-band high precision data in bytes */
    uint64 NarrowBandMemory;
    
    /** Memory used by material channel data in bytes */
    uint64 MaterialChannelMemory;
    
    /** Memory used by mesh data derived from SVO+SDF in bytes */
    uint64 MeshDataMemory;
    
    /** Number of active SVO nodes */
    uint32 ActiveSVONodeCount;
    
    /** Number of active SDF fields */
    uint32 ActiveSDFFieldCount;
    
    /** Map of memory usage by material type */
    TMap<FName, uint64> MemoryByMaterialType;
    
    /** Map of memory usage by region */
    TMap<FName, uint64> MemoryByRegion;
    
    /** Memory overhead for spatial data structures in bytes */
    uint64 StructureOverheadMemory;
    
    /** Compressed memory size in bytes */
    uint64 CompressedMemory;
    
    /** Uncompressed memory size in bytes */
    uint64 UncompressedMemory;
    
    /** Compression ratio (uncompressed / compressed) */
    float CompressionRatio;
};

/**
 * Base interface for memory trackers in the SVO+SDF mining architecture
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UMemoryTracker : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for memory tracking in the SVO+SDF mining architecture
 * Provides memory telemetry and monitoring functionality
 */
class MININGSPICECOPILOT_API IMemoryTracker
{
    GENERATED_BODY()

public:
    /**
     * Initializes the memory tracker and prepares it for use
     * @return True if initialization was successful
     */
    virtual bool Initialize() = 0;

    /**
     * Shuts down the memory tracker and cleans up resources
     */
    virtual void Shutdown() = 0;
    
    /**
     * Checks if the memory tracker has been initialized
     * @return True if initialized, false otherwise
     */
    virtual bool IsInitialized() const = 0;
    
    /**
     * Tracks a memory allocation with the system
     * @param Ptr Pointer to the allocated memory
     * @param SizeInBytes Size of the allocation in bytes
     * @param CategoryName Category for budget tracking
     * @param AllocationName Optional name for the allocation
     * @param RequestingObject Optional object for tracking
     * @return True if tracking was successful
     */
    virtual bool TrackAllocation(void* Ptr, uint64 SizeInBytes, const FName& CategoryName, 
        const FName& AllocationName = NAME_None, const UObject* RequestingObject = nullptr) = 0;
    
    /**
     * Untracks a memory allocation with the system
     * @param Ptr Pointer to the allocated memory
     * @return True if untracking was successful
     */
    virtual bool UntrackAllocation(void* Ptr) = 0;
    
    /**
     * Gets information about a tracked allocation
     * @param Ptr Pointer to the allocated memory
     * @return Allocation info or nullptr if not found
     */
    virtual const FMemoryAllocationInfo* GetAllocationInfo(void* Ptr) const = 0;
    
    /**
     * Gets current memory statistics
     * @return Structure containing memory statistics
     */
    virtual FMemoryStats GetMemoryStats() const = 0;
    
    /**
     * Gets SVO+SDF specific memory metrics
     * @return Structure containing SVO+SDF memory metrics
     */
    virtual FSVOSDFMemoryMetrics GetSVOSDFMemoryMetrics() const = 0;
    
    /**
     * Sets a memory budget for a specific category
     * @param CategoryName Name of the memory category
     * @param BudgetInBytes Budget in bytes
     */
    virtual void SetMemoryBudget(const FName& CategoryName, uint64 BudgetInBytes) = 0;
    
    /**
     * Gets the current memory budget for a specific category
     * @param CategoryName Name of the memory category
     * @return Budget in bytes, or 0 if category not found
     */
    virtual uint64 GetMemoryBudget(const FName& CategoryName) const = 0;
    
    /**
     * Gets the current memory usage for a specific category
     * @param CategoryName Name of the memory category
     * @return Usage in bytes, or 0 if category not found
     */
    virtual uint64 GetMemoryUsage(const FName& CategoryName) const = 0;
    
    /**
     * Sets a tracking tag for a memory allocation
     * @param Ptr Pointer to the allocated memory
     * @param Tag Tag to set
     * @return True if tag was successfully set
     */
    virtual bool SetAllocationTag(void* Ptr, const FName& Tag) = 0;
    
    /**
     * Sets the memory tier for an allocation
     * @param Ptr Pointer to the allocated memory
     * @param Tier Memory tier classification
     * @return True if tier was successfully set
     */
    virtual bool SetAllocationTier(void* Ptr, EMemoryTier Tier) = 0;
    
    /**
     * Sets the access pattern for an allocation
     * @param Ptr Pointer to the allocated memory
     * @param AccessPattern Memory access pattern
     * @return True if access pattern was successfully set
     */
    virtual bool SetAllocationAccessPattern(void* Ptr, EMemoryAccessPattern AccessPattern) = 0;
    
    /**
     * Enables or disables call stack tracking for allocations
     * @param bEnable Whether to enable call stack tracking
     * @param StackTraceDepth Depth of stack traces to capture
     */
    virtual void EnableCallStackTracking(bool bEnable, uint32 StackTraceDepth = 16) = 0;
    
    /**
     * Gets all allocations for a specific category
     * @param CategoryName Category name to filter by
     * @return Array of allocation infos
     */
    virtual TArray<FMemoryAllocationInfo> GetAllocationsByCategory(const FName& CategoryName) const = 0;
    
    /**
     * Gets all allocations with a specific tag
     * @param Tag Tag to filter by
     * @return Array of allocation infos
     */
    virtual TArray<FMemoryAllocationInfo> GetAllocationsByTag(const FName& Tag) const = 0;
    
    /**
     * Gets all allocations with a specific memory tier
     * @param Tier Memory tier to filter by
     * @return Array of allocation infos
     */
    virtual TArray<FMemoryAllocationInfo> GetAllocationsByTier(EMemoryTier Tier) const = 0;
    
    /**
     * Gets all allocations above a certain size
     * @param MinSizeInBytes Minimum size in bytes
     * @return Array of allocation infos
     */
    virtual TArray<FMemoryAllocationInfo> GetAllocationsBySize(uint64 MinSizeInBytes) const = 0;
    
    /**
     * Generates a memory report for debugging and analysis
     * @param bDetailed Whether to include detailed allocation information
     * @return String containing the memory report
     */
    virtual FString GenerateMemoryReport(bool bDetailed = false) const = 0;
    
    /**
     * Dumps memory statistics to the log
     * @param bDetailed Whether to include detailed allocation information
     */
    virtual void LogMemoryStatistics(bool bDetailed = false) const = 0;
    
    /**
     * Validates memory statistics for debugging
     * @param OutErrors Collection of errors found during validation
     * @return True if valid, false if errors were found
     */
    virtual bool ValidateStats(TArray<FString>& OutErrors) const = 0;
    
    /**
     * Resets memory tracking statistics
     * Note: This does not affect current allocations, only statistics
     */
    virtual void ResetStatistics() = 0;
};