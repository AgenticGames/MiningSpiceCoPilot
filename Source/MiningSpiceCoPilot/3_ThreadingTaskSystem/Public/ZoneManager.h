// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeCounter.h"
#include "HAL/CriticalSection.h"

/**
 * Zone access mode for memory and concurrency optimization
 */
enum class EZoneAccessMode : uint8
{
    /** Read-only access to the zone */
    ReadOnly,
    
    /** Read-write access to the zone */
    ReadWrite,
    
    /** Exclusive access to the zone (blocks all other access) */
    Exclusive,
    
    /** Intent to access zone materials only */
    MaterialOnly
};

/**
 * Zone ownership status
 */
enum class EZoneOwnershipStatus : uint8
{
    /** Zone is not owned by any thread */
    None,
    
    /** Zone is owned exclusively by a single thread */
    Exclusive,
    
    /** Zone is shared by multiple readers */
    Shared,
    
    /** Zone ownership is in transition */
    Transition
};

/**
 * Zone performance metrics for optimization
 */
struct FZoneMetrics
{
    /** Number of accesses to this zone */
    uint64 AccessCount;
    
    /** Number of conflicts in this zone */
    uint64 ConflictCount;
    
    /** Number of modifications to this zone */
    uint64 ModificationCount;
    
    /** Average time spent in this zone in milliseconds */
    double AverageAccessTimeMs;
    
    /** Number of threads that frequently access this zone */
    int32 ThreadAccessCount;
    
    /** Most common material accessed in this zone */
    int32 MostAccessedMaterial;
    
    /** Access frequency (accesses per second) */
    float AccessFrequency;
    
    /** Whether this zone is frequently modified */
    bool bFrequentlyModified;
    
    /** Whether this zone has high contention */
    bool bHighContention;
    
    /** Constructor */
    FZoneMetrics()
        : AccessCount(0)
        , ConflictCount(0)
        , ModificationCount(0)
        , AverageAccessTimeMs(0.0)
        , ThreadAccessCount(0)
        , MostAccessedMaterial(INDEX_NONE)
        , AccessFrequency(0.0f)
        , bFrequentlyModified(false)
        , bHighContention(false)
    {
    }
};

/**
 * Zone descriptor with spatial and ownership information
 */
struct FZoneDescriptor
{
    /** Zone ID */
    int32 ZoneId;
    
    /** Region ID this zone belongs to */
    int32 RegionId;
    
    /** Zone position in world space */
    FVector Position;
    
    /** Zone dimensions */
    FVector Dimensions;
    
    /** Current owner thread ID or INDEX_NONE if unowned */
    FThreadSafeCounter OwnerThreadId;
    
    /** Number of readers currently accessing the zone */
    FThreadSafeCounter ReaderCount;
    
    /** Zone ownership status */
    EZoneOwnershipStatus OwnershipStatus;
    
    /** Materials present in this zone */
    TArray<int32> MaterialIds;
    
    /** Zone metrics for optimization */
    FZoneMetrics Metrics;
    
    /** Zone lock for synchronized access */
    FCriticalSection Lock;
    
    /** Zone version counter for optimistic concurrency */
    FThreadSafeCounter Version;
    
    /** Material version counters */
    TMap<int32, FThreadSafeCounter*> MaterialVersions;
    
    /** Constructor */
    FZoneDescriptor()
        : ZoneId(INDEX_NONE)
        , RegionId(INDEX_NONE)
        , Position(FVector::ZeroVector)
        , Dimensions(FVector(200.0f))
        , OwnershipStatus(EZoneOwnershipStatus::None)
    {
    }
};

/**
 * Zone manager for the Mining system
 * Manages zone grid partitioning and ownership tracking
 * for transaction coordination in the mining system
 */
class MININGSPICECOPILOT_API FZoneManager
{
public:
    /** Constructor */
    FZoneManager();
    
    /** Destructor */
    ~FZoneManager();
    
    /**
     * Initializes the zone manager
     * @return True if initialization was successful
     */
    bool Initialize();
    
    /**
     * Shuts down the zone manager and cleans up resources
     */
    void Shutdown();
    
    /**
     * Checks if the zone manager has been initialized
     * @return True if initialized, false otherwise
     */
    bool IsInitialized() const;
    
    /**
     * Creates a zone at the specified position
     * @param Position World position for the zone
     * @param RegionId Region this zone belongs to
     * @return Zone ID of the created zone
     */
    int32 CreateZone(const FVector& Position, int32 RegionId);
    
    /**
     * Removes a zone from the system
     * @param ZoneId ID of the zone to remove
     * @return True if the zone was successfully removed
     */
    bool RemoveZone(int32 ZoneId);
    
    /**
     * Gets a zone by ID
     * @param ZoneId ID of the zone to get
     * @return Zone descriptor or nullptr if not found
     */
    FZoneDescriptor* GetZone(int32 ZoneId);
    
    /**
     * Gets a zone by world position
     * @param Position World position to find zone at
     * @return Zone descriptor or nullptr if not found
     */
    FZoneDescriptor* GetZoneAtPosition(const FVector& Position);
    
    /**
     * Acquires ownership of a zone
     * @param ZoneId ID of the zone to acquire
     * @param ThreadId ID of the thread acquiring ownership
     * @param AccessMode Mode of access (read/write/exclusive)
     * @param TimeoutMs Maximum time to wait in milliseconds (0 for no wait)
     * @return True if ownership was acquired
     */
    bool AcquireZoneOwnership(int32 ZoneId, int32 ThreadId, EZoneAccessMode AccessMode, uint32 TimeoutMs = 0);
    
    /**
     * Releases ownership of a zone
     * @param ZoneId ID of the zone to release
     * @param ThreadId ID of the thread releasing ownership
     * @return True if ownership was released
     */
    bool ReleaseZoneOwnership(int32 ZoneId, int32 ThreadId);
    
    /**
     * Gets the current owner thread of a zone
     * @param ZoneId ID of the zone to check
     * @return Thread ID of the owner or INDEX_NONE if unowned
     */
    int32 GetZoneOwner(int32 ZoneId) const;
    
    /**
     * Gets the zone IDs within a region
     * @param RegionId ID of the region
     * @return Array of zone IDs in the region
     */
    TArray<int32> GetZonesInRegion(int32 RegionId) const;
    
    /**
     * Gets all zones within a radius from a position
     * @param Position Center position
     * @param Radius Radius to search within
     * @return Array of zone IDs within the radius
     */
    TArray<int32> GetZonesInRadius(const FVector& Position, float Radius) const;
    
    /**
     * Gets the number of zones in the system
     * @return Total zone count
     */
    int32 GetZoneCount() const;
    
    /**
     * Updates the materials present in a zone
     * @param ZoneId ID of the zone to update
     * @param MaterialIds Array of material IDs present in the zone
     * @return True if the update was successful
     */
    bool UpdateZoneMaterials(int32 ZoneId, const TArray<int32>& MaterialIds);
    
    /**
     * Records zone access metrics for optimization
     * @param ZoneId ID of the zone accessed
     * @param ThreadId ID of the thread that accessed the zone
     * @param AccessTimeMs Time spent accessing the zone in milliseconds
     * @param bWasModified Whether the zone was modified during access
     * @param bHadConflict Whether a conflict occurred during access
     */
    void RecordZoneAccess(int32 ZoneId, int32 ThreadId, double AccessTimeMs, bool bWasModified, bool bHadConflict);
    
    /**
     * Gets metrics for a zone
     * @param ZoneId ID of the zone to get metrics for
     * @return Zone metrics
     */
    FZoneMetrics GetZoneMetrics(int32 ZoneId) const;
    
    /**
     * Gets zones with high contention
     * @return Array of zone IDs with high contention
     */
    TArray<int32> GetHighContentionZones() const;
    
    /**
     * Gets the current version of a zone
     * @param ZoneId ID of the zone
     * @return Current zone version
     */
    uint32 GetZoneVersion(int32 ZoneId) const;
    
    /**
     * Gets the current version of a material in a zone
     * @param ZoneId ID of the zone
     * @param MaterialId ID of the material
     * @return Current material version
     */
    uint32 GetMaterialVersion(int32 ZoneId, int32 MaterialId) const;
    
    /**
     * Increments the version of a zone
     * @param ZoneId ID of the zone
     * @return New zone version
     */
    uint32 IncrementZoneVersion(int32 ZoneId);
    
    /**
     * Increments the version of a material in a zone
     * @param ZoneId ID of the zone
     * @param MaterialId ID of the material
     * @return New material version
     */
    uint32 IncrementMaterialVersion(int32 ZoneId, int32 MaterialId);
    
    /**
     * Calculates the probability of conflict for a zone based on metrics
     * @param ZoneId ID of the zone
     * @return Conflict probability between 0.0 and 1.0
     */
    float GetZoneConflictProbability(int32 ZoneId) const;
    
    /**
     * Optimizes the zone layout for a region based on access patterns
     * @param RegionId ID of the region to optimize
     * @return True if any changes were made
     */
    bool OptimizeZoneLayout(int32 RegionId);
    
    /**
     * Splits a high contention zone into smaller zones
     * @param ZoneId ID of the zone to split
     * @return True if the zone was successfully split
     */
    bool SplitZone(int32 ZoneId);
    
    /**
     * Merges two adjacent zones with low usage
     * @param ZoneId1 ID of the first zone
     * @param ZoneId2 ID of the second zone
     * @return True if the zones were successfully merged
     */
    bool MergeZones(int32 ZoneId1, int32 ZoneId2);
    
    /**
     * Checks if two zones are adjacent in space
     * @param ZoneId1 ID of the first zone
     * @param ZoneId2 ID of the second zone
     * @return True if the zones are adjacent
     */
    bool AreZonesAdjacent(int32 ZoneId1, int32 ZoneId2);
    
    /**
     * Reorganizes materials within a fragmented zone
     * @param ZoneId ID of the zone to reorganize
     * @return True if any changes were made
     */
    bool ReorganizeZoneMaterials(int32 ZoneId);

    /**
     * Gets an instance of the zone manager
     * @return Zone manager instance
     */
    static FZoneManager& Get();

private:
    /** Whether the zone manager has been initialized */
    bool bIsInitialized;
    
    /** Map of zones by ID */
    TMap<int32, FZoneDescriptor*> Zones;
    
    /** Map of zone sets by region ID */
    TMap<int32, TSet<int32>> ZonesByRegion;
    
    /** Spatial lookup grid for fast position-based queries */
    TMap<FIntVector, TArray<int32>> SpatialLookup;
    
    /** Map of spatial keys by zone ID for efficient removal */
    TMap<int32, FIntVector> ZoneSpatialKeys;
    
    /** Next zone ID */
    FThreadSafeCounter NextZoneId;
    
    /** Lock for zone data access */
    mutable FCriticalSection ZoneLock;
    
    /**
     * Computes the spatial grid key for a position
     * @param Position Position to compute key for
     * @return Spatial grid key
     */
    FIntVector ComputeSpatialKey(const FVector& Position);
    
    /**
     * Computes the spatial grid key for a position (const version)
     * @param Position Position to compute key for
     * @return Spatial grid key
     */
    FIntVector ComputeSpatialKey(const FVector& Position) const;
    
    /**
     * Adds a zone to the spatial lookup grid
     * @param ZoneId ID of the zone to add
     * @param Position Position of the zone
     */
    void AddZoneToSpatialLookup(int32 ZoneId, const FVector& Position);
    
    /**
     * Removes a zone from the spatial lookup grid
     * @param ZoneId ID of the zone to remove
     * @param Position Position of the zone
     */
    void RemoveZoneFromSpatialLookup(int32 ZoneId, const FVector& Position);
    
    /**
     * Gets or creates a material version counter for a zone
     * @param Zone Zone descriptor
     * @param MaterialId ID of the material
     * @return Material version counter
     */
    FThreadSafeCounter* GetOrCreateMaterialVersion(FZoneDescriptor* Zone, int32 MaterialId);
    
    /**
     * Updates the contention status for a zone based on metrics
     * @param Zone Zone descriptor to update
     */
    void UpdateZoneContentionStatus(FZoneDescriptor* Zone);
    
    /** Singleton instance */
    static FZoneManager* Instance;
};