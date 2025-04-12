// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeCounter.h"
#include "Misc/SpinLock.h"

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
    FSpinLock Lock;
    
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
     * Records an access to a zone for metrics
     * @param ZoneId ID of the zone accessed
     * @param ThreadId ID of the thread that accessed the zone
     * @param AccessTimeMs Time spent accessing the zone in milliseconds
     * @param bWasModified Whether the zone was modified
     * @param bHadConflict Whether there was a conflict during access
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
     * Gets the version of a zone
     * @param ZoneId ID of the zone to check
     * @return Current version of the zone
     */
    uint32 GetZoneVersion(int32 ZoneId) const;
    
    /**
     * Gets the version of a material within a zone
     * @param ZoneId ID of the zone to check
     * @param MaterialId ID of the material to check
     * @return Current version of the material in the zone
     */
    uint32 GetMaterialVersion(int32 ZoneId, int32 MaterialId) const;
    
    /**
     * Increments the version of a zone
     * @param ZoneId ID of the zone to update
     * @return New version of the zone
     */
    uint32 IncrementZoneVersion(int32 ZoneId);
    
    /**
     * Increments the version of a material within a zone
     * @param ZoneId ID of the zone to update
     * @param MaterialId ID of the material to update
     * @return New version of the material
     */
    uint32 IncrementMaterialVersion(int32 ZoneId, int32 MaterialId);
    
    /**
     * Gets the singleton instance
     * @return Reference to the zone manager
     */
    static FZoneManager& Get();

private:
    /** Whether the zone manager has been initialized */
    bool bIsInitialized;
    
    /** Map of zones by ID */
    TMap<int32, FZoneDescriptor*> Zones;
    
    /** Map of zones by region ID */
    TMap<int32, TSet<int32>> ZonesByRegion;
    
    /** Spatial lookup structure for efficiently finding zones by position */
    TMap<FIntVector, TArray<int32>> SpatialLookup;
    
    /** Lock for zone map access */
    mutable FCriticalSection ZoneLock;
    
    /** Next zone ID */
    FThreadSafeCounter NextZoneId;
    
    /** Gets or creates a material version counter */
    FThreadSafeCounter* GetOrCreateMaterialVersion(FZoneDescriptor* Zone, int32 MaterialId);
    
    /** Computes the spatial key for a position */
    FIntVector ComputeSpatialKey(const FVector& Position) const;
    
    /** Adds a zone to the spatial lookup */
    void AddZoneToSpatialLookup(int32 ZoneId, const FVector& Position);
    
    /** Removes a zone from the spatial lookup */
    void RemoveZoneFromSpatialLookup(int32 ZoneId, const FVector& Position);
    
    /** Updates zone contention status based on metrics */
    void UpdateZoneContentionStatus(FZoneDescriptor* Zone);

    /** Singleton instance */
    static FZoneManager* Instance;
};