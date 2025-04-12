// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Interfaces/Hibernation/IHibernationManager.h"
#include "IHibernationCache.generated.h"

/**
 * Region cache entry type
 */
enum class ECacheEntryType : uint8
{
    /** Full region with all data */
    Full,
    
    /** Essential data only for minimal memory footprint */
    Essential,
    
    /** Partially deserialized region for streaming */
    Partial,
    
    /** Compressed region with minimal memory footprint */
    Compressed,
    
    /** Reference-only entry with metadata */
    Reference
};

/**
 * Structure containing cache entry information
 */
struct MININGSPICECOPILOT_API FCacheEntryInfo
{
    /** ID of the cached region */
    int32 RegionId;
    
    /** Cache entry type */
    ECacheEntryType EntryType;
    
    /** Size of the cached data in bytes */
    uint64 CachedSizeBytes;
    
    /** Original size of the region data in bytes */
    uint64 OriginalSizeBytes;
    
    /** Timestamp when the entry was added to the cache */
    FDateTime CacheTimestamp;
    
    /** Last time the entry was accessed */
    FDateTime LastAccessTimestamp;
    
    /** Number of times the entry has been accessed */
    uint32 AccessCount;
    
    /** Priority level of the cache entry */
    EReactivationPriority Priority;
    
    /** Whether the entry is pinned in cache */
    bool bIsPinned;
    
    /** Compression tier used for this cache entry */
    ECompressionTier CompressionTier;
    
    /** Memory usage of the entry in bytes */
    uint64 MemoryUsageBytes;
    
    /** Estimated reactivation time in milliseconds */
    float EstimatedReactivationTimeMs;
    
    /** Whether the entry has been partially loaded */
    bool bIsPartiallyLoaded;
    
    /** Which components of the region are loaded (by name) */
    TArray<FName> LoadedComponents;
    
    /** Topological importance score (higher means more important) */
    float TopologicalImportance;
    
    /** Whether this entry contains mining modifications */
    bool bHasMiningModifications;
};

/**
 * Structure containing hibernation cache statistics
 */
struct MININGSPICECOPILOT_API FCacheStats
{
    /** Total number of entries in the cache */
    uint32 EntryCount;
    
    /** Total size of cached data in bytes */
    uint64 TotalCachedSizeBytes;
    
    /** Current memory usage in bytes */
    uint64 MemoryUsageBytes;
    
    /** Maximum capacity of the cache in bytes */
    uint64 MaxCapacityBytes;
    
    /** Cache hit count */
    uint64 HitCount;
    
    /** Cache miss count */
    uint64 MissCount;
    
    /** Cache hit rate (0-1) */
    float HitRate;
    
    /** Number of entries by type */
    TMap<ECacheEntryType, uint32> EntryCountByType;
    
    /** Memory usage by entry type in bytes */
    TMap<ECacheEntryType, uint64> MemoryUsageByType;
    
    /** Number of pinned entries */
    uint32 PinnedEntryCount;
    
    /** Memory usage by pinned entries in bytes */
    uint64 PinnedMemoryUsageBytes;
    
    /** Number of evictions performed */
    uint64 EvictionCount;
    
    /** Number of entries with mining modifications */
    uint32 ModifiedEntryCount;
    
    /** Average time spent in cache in seconds */
    float AverageCacheTimeSeconds;
    
    /** Peak memory usage in bytes */
    uint64 PeakMemoryUsageBytes;
    
    /** Average compression ratio for cached entries */
    float AverageCompressionRatio;
};

/**
 * Structure containing cache configuration parameters
 */
struct MININGSPICECOPILOT_API FCacheConfig
{
    /** Maximum memory budget for the cache in bytes */
    uint64 MaxMemoryBudgetBytes;
    
    /** Percentage of memory to reserve for priority entries (0-1) */
    float PriorityReservationPercent;
    
    /** Whether to automatically adjust cache size based on system memory */
    bool bAutoAdjustSize;
    
    /** Threshold for emergency cache eviction (0-1) */
    float EmergencyEvictionThreshold;
    
    /** Minimum time to keep entries in cache in seconds */
    float MinCacheTimeSeconds;
    
    /** Maximum number of entries regardless of memory usage */
    uint32 MaxEntryCount;
    
    /** Whether to use memory mapping for large regions */
    bool bUseMemoryMapping;
    
    /** Threshold size for memory mapping in bytes */
    uint64 MemoryMappingThresholdBytes;
    
    /** Whether to compress entries while in cache */
    bool bCompressInactiveEntries;
    
    /** Inactive time before compression in seconds */
    float InactiveCompressionTimeSeconds;
    
    /** Whether to maintain topology-based entry relationships */
    bool bMaintainTopologicalRelationships;
    
    /** Cache size adaptive factor based on available memory (0-1) */
    float AdaptiveSizeFactor;
    
    /** Constructor with defaults */
    FCacheConfig()
        : MaxMemoryBudgetBytes(256 * 1024 * 1024) // 256 MB
        , PriorityReservationPercent(0.25f)
        , bAutoAdjustSize(true)
        , EmergencyEvictionThreshold(0.95f)
        , MinCacheTimeSeconds(30.0f)
        , MaxEntryCount(100)
        , bUseMemoryMapping(true)
        , MemoryMappingThresholdBytes(16 * 1024 * 1024) // 16 MB
        , bCompressInactiveEntries(true)
        , InactiveCompressionTimeSeconds(60.0f)
        , bMaintainTopologicalRelationships(true)
        , AdaptiveSizeFactor(0.2f)
    {
    }
};

/**
 * Base interface for hibernation caches in the SVO+SDF mining architecture
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UHibernationCache : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for region hibernation cache in the SVO+SDF mining architecture
 * Provides efficient caching of hibernated regions with priority-based retention
 */
class MININGSPICECOPILOT_API IHibernationCache
{
    GENERATED_BODY()

public:
    /**
     * Initializes the hibernation cache and prepares it for use
     * @return True if initialization was successful
     */
    virtual bool Initialize() = 0;

    /**
     * Shuts down the hibernation cache and cleans up resources
     */
    virtual void Shutdown() = 0;
    
    /**
     * Checks if the hibernation cache has been initialized
     * @return True if initialized, false otherwise
     */
    virtual bool IsInitialized() const = 0;
    
    /**
     * Adds a region to the cache
     * @param RegionId The ID of the region to cache
     * @param RegionData Pointer to the region data
     * @param DataSize Size of the region data in bytes
     * @param EntryType Type of cache entry to create
     * @param Priority Priority level for cache retention
     * @return True if the region was successfully cached
     */
    virtual bool AddRegion(
        int32 RegionId,
        void* RegionData,
        uint64 DataSize,
        ECacheEntryType EntryType = ECacheEntryType::Full,
        EReactivationPriority Priority = EReactivationPriority::Normal) = 0;
    
    /**
     * Retrieves a region from the cache
     * @param RegionId The ID of the region to retrieve
     * @param OutRegionData Pointer to store the region data
     * @param OutDataSize Size of the retrieved region data in bytes
     * @return True if the region was found in cache
     */
    virtual bool GetRegion(
        int32 RegionId,
        void*& OutRegionData,
        uint64& OutDataSize) = 0;
    
    /**
     * Removes a region from the cache
     * @param RegionId The ID of the region to remove
     * @return True if the region was removed
     */
    virtual bool RemoveRegion(int32 RegionId) = 0;
    
    /**
     * Pins a region in the cache to prevent eviction
     * @param RegionId The ID of the region to pin
     * @param bPin Whether to pin or unpin the region
     * @return True if the pin state was changed
     */
    virtual bool PinRegion(int32 RegionId, bool bPin = true) = 0;
    
    /**
     * Checks if a region is in the cache
     * @param RegionId The ID of the region to check
     * @return True if the region is in the cache
     */
    virtual bool HasRegion(int32 RegionId) const = 0;
    
    /**
     * Gets information about a cached region
     * @param RegionId The ID of the region
     * @return Pointer to cache entry info, or nullptr if not found
     */
    virtual const FCacheEntryInfo* GetEntryInfo(int32 RegionId) const = 0;
    
    /**
     * Updates the priority of a cached region
     * @param RegionId The ID of the region
     * @param Priority New priority level
     * @return True if the priority was updated
     */
    virtual bool UpdatePriority(int32 RegionId, EReactivationPriority Priority) = 0;
    
    /**
     * Gets cache statistics
     * @return Structure containing cache statistics
     */
    virtual FCacheStats GetCacheStats() const = 0;
    
    /**
     * Sets the configuration for the cache
     * @param Config New cache configuration
     */
    virtual void SetConfig(const FCacheConfig& Config) = 0;
    
    /**
     * Gets the current cache configuration
     * @return Current cache configuration
     */
    virtual FCacheConfig GetConfig() const = 0;
    
    /**
     * Evicts entries to free up memory
     * @param BytesToFree Number of bytes to free
     * @param bEmergency Whether this is an emergency eviction
     * @return Number of bytes actually freed
     */
    virtual uint64 EvictEntries(uint64 BytesToFree, bool bEmergency = false) = 0;
    
    /**
     * Compresses a cached region to reduce memory footprint
     * @param RegionId The ID of the region to compress
     * @param CompressionTier Compression tier to use
     * @return True if compression was successful
     */
    virtual bool CompressEntry(int32 RegionId, ECompressionTier CompressionTier = ECompressionTier::Standard) = 0;
    
    /**
     * Preloads essential components for a region
     * @param RegionId The ID of the region
     * @param ComponentNames Array of component names to load
     * @return True if preloading was initiated
     */
    virtual bool PreloadEssentialComponents(int32 RegionId, const TArray<FName>& ComponentNames) = 0;
    
    /**
     * Sets the topological importance of a region
     * @param RegionId The ID of the region
     * @param Importance Importance score (higher means more important)
     * @return True if importance was set
     */
    virtual bool SetTopologicalImportance(int32 RegionId, float Importance) = 0;
    
    /**
     * Gets regions connected to a specific region
     * @param RegionId The ID of the region
     * @return Array of connected region IDs in cache
     */
    virtual TArray<int32> GetConnectedRegions(int32 RegionId) const = 0;
    
    /**
     * Establishes a connection between two regions
     * @param RegionId1 The ID of the first region
     * @param RegionId2 The ID of the second region
     * @param ConnectionStrength Connection strength (0-1)
     * @return True if connection was established
     */
    virtual bool ConnectRegions(int32 RegionId1, int32 RegionId2, float ConnectionStrength = 1.0f) = 0;
    
    /**
     * Gets the singleton instance of the hibernation cache
     * @return Reference to the hibernation cache instance
     */
    static IHibernationCache& Get();
};