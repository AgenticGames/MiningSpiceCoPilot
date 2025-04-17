// Copyright Epic Games, Inc. All Rights Reserved.

#include "ZoneManager.h"
#include "Misc/ScopeLock.h"
#include "HAL/PlatformTime.h"

// Initialize static instance to nullptr
FZoneManager* FZoneManager::Instance = nullptr;

// Size of the spatial lookup grid cells (should be at least the size of the largest zone)
static const float SpatialGridSize = 200.0f;

// Default timeout for zone acquisition in milliseconds
static const uint32 DefaultAcquisitionTimeoutMs = 5000;

// Threshold for marking a zone as high contention
static const uint32 HighContentionThreshold = 10;

// Threshold for marking a zone as frequently modified (as percentage of accesses)
static const float FrequentModificationThreshold = 0.5f;

/**
 * Constructor
 */
FZoneManager::FZoneManager()
    : bIsInitialized(false)
{
    // Store singleton instance
    Instance = this;
    
    // Initialize zone ID counter
    NextZoneId.Set(1); // Start IDs from 1
}

/**
 * Destructor
 */
FZoneManager::~FZoneManager()
{
    // Shutdown if still initialized
    if (bIsInitialized)
    {
        Shutdown();
    }
    
    // Clear singleton instance if it's this instance
    if (Instance == this)
    {
        Instance = nullptr;
    }
}

/**
 * Initializes the zone manager
 */
bool FZoneManager::Initialize()
{
    // Check if already initialized
    if (bIsInitialized)
    {
        return true;
    }
    
    // Initialize maps
    Zones.Empty();
    ZonesByRegion.Empty();
    SpatialLookup.Empty();
    ZoneSpatialKeys.Empty();
    
    // Mark as initialized
    bIsInitialized = true;
    
    UE_LOG(LogTemp, Log, TEXT("Zone Manager initialized"));
    
    return true;
}

/**
 * Shuts down the zone manager and cleans up resources
 */
void FZoneManager::Shutdown()
{
    // Check if initialized
    if (!bIsInitialized)
    {
        return;
    }
    
    // Clean up all zones
    {
        FScopeLock Lock(&ZoneLock);
        
        // Delete all zone descriptors
        for (auto& Pair : Zones)
        {
            FZoneDescriptor* Zone = Pair.Value;
            if (Zone)
            {
                // Clean up material version counters
                for (auto& MaterialPair : Zone->MaterialVersions)
                {
                    delete MaterialPair.Value;
                }
                
                delete Zone;
            }
        }
        
        // Clear maps
        Zones.Empty();
        ZonesByRegion.Empty();
        SpatialLookup.Empty();
        ZoneSpatialKeys.Empty();
    }
    
    // Mark as not initialized
    bIsInitialized = false;
    
    UE_LOG(LogTemp, Log, TEXT("Zone Manager shutdown"));
}

/**
 * Checks if the zone manager has been initialized
 */
bool FZoneManager::IsInitialized() const
{
    return bIsInitialized;
}

/**
 * Creates a zone at the specified position
 */
int32 FZoneManager::CreateZone(const FVector& Position, int32 RegionId)
{
    // Ensure we're initialized
    if (!bIsInitialized)
    {
        return INDEX_NONE;
    }
    
    // Generate a unique zone ID
    int32 ZoneId = NextZoneId.Increment();
    
    // Create a new zone descriptor
    FZoneDescriptor* Zone = new FZoneDescriptor();
    Zone->ZoneId = ZoneId;
    Zone->RegionId = RegionId;
    Zone->Position = Position;
    Zone->Dimensions = FVector(SpatialGridSize); // Default size
    Zone->OwnershipStatus = EZoneOwnershipStatus::None;
    
    // Add to zones map
    {
        FScopeLock Lock(&ZoneLock);
        
        // Add to main zones map
        Zones.Add(ZoneId, Zone);
        
        // Add to regions map
        TSet<int32>* RegionZones = ZonesByRegion.Find(RegionId);
        if (RegionZones)
        {
            RegionZones->Add(ZoneId);
        }
        else
        {
            TSet<int32> NewSet;
            NewSet.Add(ZoneId);
            ZonesByRegion.Add(RegionId, NewSet);
        }
        
        // Add to spatial lookup
        AddZoneToSpatialLookup(ZoneId, Position);
    }
    
    return ZoneId;
}

/**
 * Computes the spatial grid key for a position
 */
FIntVector FZoneManager::ComputeSpatialKey(const FVector& Position) const
{
    return FIntVector(
        FMath::FloorToInt(Position.X / SpatialGridSize),
        FMath::FloorToInt(Position.Y / SpatialGridSize),
        FMath::FloorToInt(Position.Z / SpatialGridSize)
    );
}

/**
 * Computes the spatial grid key for a position using TVector<double>
 */
UE::Math::TIntVector3<int> FZoneManager::ComputeSpatialKey(const UE::Math::TVector<double>& Position)
{
    return UE::Math::TIntVector3<int>(
        FMath::FloorToInt(Position.X / SpatialGridSize),
        FMath::FloorToInt(Position.Y / SpatialGridSize),
        FMath::FloorToInt(Position.Z / SpatialGridSize)
    );
}

/**
 * Adds a zone to the spatial lookup grid
 */
void FZoneManager::AddZoneToSpatialLookup(int32 ZoneId, const FVector& Position)
{
    // Compute spatial key
    FIntVector Key = ComputeSpatialKey(Position);
    
    // Add to spatial lookup
    TArray<int32>* ZonesAtKey = SpatialLookup.Find(Key);
    if (ZonesAtKey)
    {
        ZonesAtKey->Add(ZoneId);
    }
    else
    {
        TArray<int32> NewArray;
        NewArray.Add(ZoneId);
        SpatialLookup.Add(Key, NewArray);
    }
    
    // Store the key for this zone
    ZoneSpatialKeys.Add(ZoneId, Key);
}

/**
 * Removes a zone from the spatial lookup grid
 */
void FZoneManager::RemoveZoneFromSpatialLookup(int32 ZoneId, const FVector& Position)
{
    // Find the key for this zone
    FIntVector* KeyPtr = ZoneSpatialKeys.Find(ZoneId);
    if (!KeyPtr)
    {
        // Compute it if not found
        FIntVector Key = ComputeSpatialKey(Position);
        KeyPtr = &Key;
    }
    
    // Remove from spatial lookup
    TArray<int32>* ZonesAtKey = SpatialLookup.Find(*KeyPtr);
    if (ZonesAtKey)
    {
        ZonesAtKey->Remove(ZoneId);
        
        // Remove empty arrays
        if (ZonesAtKey->Num() == 0)
        {
            SpatialLookup.Remove(*KeyPtr);
        }
    }
    
    // Remove the key mapping
    ZoneSpatialKeys.Remove(ZoneId);
}

/**
 * Removes a zone from the system
 */
bool FZoneManager::RemoveZone(int32 ZoneId)
{
    // Ensure we're initialized
    if (!bIsInitialized)
    {
        return false;
    }
    
    FScopeLock Lock(&ZoneLock);
    
    // Find the zone
    FZoneDescriptor** ZonePtr = Zones.Find(ZoneId);
    if (!ZonePtr || !(*ZonePtr))
    {
        return false;
    }
    
    FZoneDescriptor* Zone = *ZonePtr;
    
    // Check if zone is currently in use
    if (Zone->OwnershipStatus != EZoneOwnershipStatus::None)
    {
        return false;
    }
    
    // Remove from regions map
    TSet<int32>* RegionZones = ZonesByRegion.Find(Zone->RegionId);
    if (RegionZones)
    {
        RegionZones->Remove(ZoneId);
        
        // Remove empty region entries
        if (RegionZones->Num() == 0)
        {
            ZonesByRegion.Remove(Zone->RegionId);
        }
    }
    
    // Remove from spatial lookup
    RemoveZoneFromSpatialLookup(ZoneId, Zone->Position);
    
    // Clean up material version counters
    for (auto& MaterialPair : Zone->MaterialVersions)
    {
        delete MaterialPair.Value;
    }
    
    // Delete the zone descriptor
    delete Zone;
    
    // Remove from zones map
    Zones.Remove(ZoneId);
    
    return true;
}

/**
 * Gets a zone by ID
 */
FZoneDescriptor* FZoneManager::GetZone(int32 ZoneId)
{
    // Ensure we're initialized
    if (!bIsInitialized)
    {
        return nullptr;
    }
    
    FScopeLock Lock(&ZoneLock);
    
    // Find the zone
    FZoneDescriptor** ZonePtr = Zones.Find(ZoneId);
    return ZonePtr ? *ZonePtr : nullptr;
}

/**
 * Gets a zone by world position
 */
FZoneDescriptor* FZoneManager::GetZoneAtPosition(const FVector& Position)
{
    // Ensure we're initialized
    if (!bIsInitialized)
    {
        return nullptr;
    }
    
    FScopeLock Lock(&ZoneLock);
    
    // Compute spatial key for the position
    FIntVector Key = ComputeSpatialKey(Position);
    
    // Look up zones at this position
    TArray<int32>* ZoneIds = SpatialLookup.Find(Key);
    if (!ZoneIds || ZoneIds->Num() == 0)
    {
        return nullptr;
    }
    
    // Find the closest zone to the position
    float ClosestDistSq = MAX_FLT;
    int32 ClosestZoneId = INDEX_NONE;
    
    for (int32 Id : *ZoneIds)
    {
        FZoneDescriptor** ZonePtr = Zones.Find(Id);
        if (ZonePtr && *ZonePtr)
        {
            FZoneDescriptor* Zone = *ZonePtr;
            
            // Calculate squared distance to zone center
            float DistSq = FVector::DistSquared(Zone->Position, Position);
            
            // Check if this is closer
            if (DistSq < ClosestDistSq)
            {
                ClosestDistSq = DistSq;
                ClosestZoneId = Id;
            }
        }
    }
    
    // Return the closest zone
    if (ClosestZoneId != INDEX_NONE)
    {
        return Zones[ClosestZoneId];
    }
    
    return nullptr;
}

/**
 * Acquires ownership of a zone
 */
bool FZoneManager::AcquireZoneOwnership(int32 ZoneId, int32 ThreadId, EZoneAccessMode AccessMode, uint32 TimeoutMs)
{
    // Ensure we're initialized
    if (!bIsInitialized)
    {
        return false;
    }
    
    // Use default timeout if none specified
    if (TimeoutMs == 0)
    {
        TimeoutMs = DefaultAcquisitionTimeoutMs;
    }
    
    // Get the zone
    FZoneDescriptor* Zone = GetZone(ZoneId);
    if (!Zone)
    {
        return false;
    }
    
    // Calculate timeout time
    double StartTime = FPlatformTime::Seconds();
    double EndTime = StartTime + (TimeoutMs / 1000.0);
    
    bool bAcquired = false;
    double WaitTime = 0.0;
    
    // Try to acquire ownership based on the access mode
    while (!bAcquired)
    {
        // Check if we've timed out
        double CurrentTime = FPlatformTime::Seconds();
        if (CurrentTime >= EndTime)
        {
            break;
        }
        
        // Attempt to acquire based on access mode
        switch (AccessMode)
        {
            case EZoneAccessMode::ReadOnly:
                // Read-only access: can share with other readers
                Zone->Lock.Lock();
                
                if (Zone->OwnershipStatus == EZoneOwnershipStatus::None || 
                    Zone->OwnershipStatus == EZoneOwnershipStatus::Shared)
                {
                    // Zone is available for shared access
                    Zone->OwnershipStatus = EZoneOwnershipStatus::Shared;
                    Zone->ReaderCount.Increment();
                    bAcquired = true;
                }
                else if (Zone->OwnershipStatus == EZoneOwnershipStatus::Exclusive && 
                         Zone->OwnerThreadId.GetValue() == ThreadId)
                {
                    // We already own it exclusively, so we can read too
                    bAcquired = true;
                }
                
                Zone->Lock.Unlock();
                break;
                
            case EZoneAccessMode::ReadWrite:
                // Read-write access: need exclusive ownership
                Zone->Lock.Lock();
                
                if (Zone->OwnershipStatus == EZoneOwnershipStatus::None)
                {
                    // Zone is available
                    Zone->OwnershipStatus = EZoneOwnershipStatus::Exclusive;
                    Zone->OwnerThreadId.Set(ThreadId);
                    bAcquired = true;
                }
                else if (Zone->OwnershipStatus == EZoneOwnershipStatus::Exclusive && 
                         Zone->OwnerThreadId.GetValue() == ThreadId)
                {
                    // We already own it
                    bAcquired = true;
                }
                
                Zone->Lock.Unlock();
                break;
                
            case EZoneAccessMode::Exclusive:
                // Exclusive access: need exclusive ownership, no readers
                Zone->Lock.Lock();
                
                if (Zone->OwnershipStatus == EZoneOwnershipStatus::None)
                {
                    // Zone is available
                    Zone->OwnershipStatus = EZoneOwnershipStatus::Exclusive;
                    Zone->OwnerThreadId.Set(ThreadId);
                    bAcquired = true;
                }
                else if (Zone->OwnershipStatus == EZoneOwnershipStatus::Exclusive && 
                         Zone->OwnerThreadId.GetValue() == ThreadId)
                {
                    // We already own it
                    bAcquired = true;
                }
                
                Zone->Lock.Unlock();
                break;
                
            case EZoneAccessMode::MaterialOnly:
                // Material-only access: less restrictive, can have multiple writers to different materials
                Zone->Lock.Lock();
                
                if (Zone->OwnershipStatus == EZoneOwnershipStatus::None || 
                    Zone->OwnershipStatus == EZoneOwnershipStatus::Shared)
                {
                    // Zone is available for material-level access
                    Zone->OwnershipStatus = EZoneOwnershipStatus::Shared;
                    Zone->ReaderCount.Increment();
                    bAcquired = true;
                }
                else if (Zone->OwnershipStatus == EZoneOwnershipStatus::Exclusive && 
                         Zone->OwnerThreadId.GetValue() == ThreadId)
                {
                    // We already own it exclusively
                    bAcquired = true;
                }
                
                Zone->Lock.Unlock();
                break;
        }
        
        // If not acquired, sleep briefly
        if (!bAcquired)
        {
            FPlatformProcess::Sleep(0.001f); // 1ms
            WaitTime += 0.001;
        }
    }
    
    // Record wait time and access in metrics
    if (bAcquired)
    {
        // Record zone access with zero processing time (will be updated on release)
        RecordZoneAccess(ZoneId, ThreadId, 0.0, AccessMode != EZoneAccessMode::ReadOnly, false);
    }
    
    return bAcquired;
}

/**
 * Releases ownership of a zone
 */
bool FZoneManager::ReleaseZoneOwnership(int32 ZoneId, int32 ThreadId)
{
    // Ensure we're initialized
    if (!bIsInitialized)
    {
        return false;
    }
    
    // Get the zone
    FZoneDescriptor* Zone = GetZone(ZoneId);
    if (!Zone)
    {
        return false;
    }
    
    bool bReleased = false;
    
    Zone->Lock.Lock();
    
    // Check current ownership status
    switch (Zone->OwnershipStatus)
    {
        case EZoneOwnershipStatus::Shared:
            // Shared ownership - decrement reader count
            if (Zone->ReaderCount.Decrement() == 0)
            {
                // No more readers
                Zone->OwnershipStatus = EZoneOwnershipStatus::None;
            }
            bReleased = true;
            break;
            
        case EZoneOwnershipStatus::Exclusive:
            // Exclusive ownership - check owner
            if (Zone->OwnerThreadId.GetValue() == ThreadId)
            {
                // We own it - release
                Zone->OwnerThreadId.Set(INDEX_NONE);
                Zone->OwnershipStatus = EZoneOwnershipStatus::None;
                bReleased = true;
            }
            break;
            
        case EZoneOwnershipStatus::None:
            // Zone not owned - nothing to release
            break;
            
        case EZoneOwnershipStatus::Transition:
            // Ownership in transition - should not happen
            // Log error and reset to safe state
            UE_LOG(LogTemp, Error, TEXT("Zone %d in transition state during release"), ZoneId);
            Zone->OwnerThreadId.Set(INDEX_NONE);
            Zone->OwnershipStatus = EZoneOwnershipStatus::None;
            Zone->ReaderCount.Set(0);
            bReleased = true;
            break;
    }
    
    Zone->Lock.Unlock();
    
    return bReleased;
}

/**
 * Gets the current owner thread of a zone
 */
int32 FZoneManager::GetZoneOwner(int32 ZoneId) const
{
    // Ensure we're initialized
    if (!bIsInitialized)
    {
        return INDEX_NONE;
    }
    
    FScopeLock Lock(&ZoneLock);
    
    // Find the zone
    const FZoneDescriptor* const* ZonePtr = Zones.Find(ZoneId);
    if (!ZonePtr || !(*ZonePtr))
    {
        return INDEX_NONE;
    }
    
    // Return owner thread ID
    return (*ZonePtr)->OwnerThreadId.GetValue();
}

/**
 * Gets the zone IDs within a region
 */
TArray<int32> FZoneManager::GetZonesInRegion(int32 RegionId) const
{
    TArray<int32> Result;
    
    // Ensure we're initialized
    if (!bIsInitialized)
    {
        return Result;
    }
    
    FScopeLock Lock(&ZoneLock);
    
    // Find zones for this region
    const TSet<int32>* ZoneSet = ZonesByRegion.Find(RegionId);
    if (ZoneSet)
    {
        Result.Reserve(ZoneSet->Num());
        for (int32 ZoneId : *ZoneSet)
        {
            Result.Add(ZoneId);
        }
    }
    
    return Result;
}

/**
 * Gets all zones within a radius from a position
 */
TArray<int32> FZoneManager::GetZonesInRadius(const FVector& Position, float Radius) const
{
    TArray<int32> Result;
    
    // Ensure we're initialized
    if (!bIsInitialized)
    {
        return Result;
    }
    
    FScopeLock Lock(&ZoneLock);
    
    // Calculate grid range based on radius
    FIntVector CenterKey = ComputeSpatialKey(Position);
    int32 GridRadius = FMath::CeilToInt(Radius / SpatialGridSize) + 1;
    
    // Check all grid cells in range
    for (int32 X = CenterKey.X - GridRadius; X <= CenterKey.X + GridRadius; X++)
    {
        for (int32 Y = CenterKey.Y - GridRadius; Y <= CenterKey.Y + GridRadius; Y++)
        {
            for (int32 Z = CenterKey.Z - GridRadius; Z <= CenterKey.Z + GridRadius; Z++)
            {
                FIntVector Key(X, Y, Z);
                
                // Get zones in this cell
                const TArray<int32>* ZonesInCell = SpatialLookup.Find(Key);
                if (ZonesInCell)
                {
                    for (int32 ZoneId : *ZonesInCell)
                    {
                        const FZoneDescriptor* const* ZonePtr = Zones.Find(ZoneId);
                        if (ZonePtr && *ZonePtr)
                        {
                            const FZoneDescriptor* Zone = *ZonePtr;
                            
                            // Check if zone center is within radius
                            float Distance = FVector::Dist(Zone->Position, Position);
                            if (Distance <= Radius)
                            {
                                Result.AddUnique(ZoneId);
                            }
                        }
                    }
                }
            }
        }
    }
    
    return Result;
}

/**
 * Gets the number of zones in the system
 */
int32 FZoneManager::GetZoneCount() const
{
    // Ensure we're initialized
    if (!bIsInitialized)
    {
        return 0;
    }
    
    FScopeLock Lock(&ZoneLock);
    return Zones.Num();
}

/**
 * Updates the materials present in a zone
 */
bool FZoneManager::UpdateZoneMaterials(int32 ZoneId, const TArray<int32>& MaterialIds)
{
    // Ensure we're initialized
    if (!bIsInitialized)
    {
        return false;
    }
    
    // Get the zone
    FZoneDescriptor* Zone = GetZone(ZoneId);
    if (!Zone)
    {
        return false;
    }
    
    FScopeLock Lock(&ZoneLock);
    
    // Update material IDs
    Zone->MaterialIds = MaterialIds;
    
    // Create version counters for any new materials
    for (int32 MaterialId : MaterialIds)
    {
        GetOrCreateMaterialVersion(Zone, MaterialId);
    }
    
    // Increment zone version to reflect material change
    Zone->Version.Increment();
    
    return true;
}

/**
 * Records zone access metrics for optimization
 */
void FZoneManager::RecordZoneAccess(int32 ZoneId, int32 ThreadId, double AccessTimeMs, bool bWasModified, bool bHadConflict)
{
    // Ensure we're initialized
    if (!bIsInitialized)
    {
        return;
    }
    
    // Get the zone
    FZoneDescriptor* Zone = GetZone(ZoneId);
    if (!Zone)
    {
        return;
    }
    
    // Update zone metrics with this access
    FScopeLock Lock(&ZoneLock);
    
    // Update access count
    Zone->Metrics.AccessCount++;
    
    // Update conflict count if there was a conflict
    if (bHadConflict)
    {
        Zone->Metrics.ConflictCount++;
    }
    
    // Update average access time
    if (AccessTimeMs > 0.0)
    {
        Zone->Metrics.AverageAccessTimeMs = 
            ((Zone->Metrics.AverageAccessTimeMs * (Zone->Metrics.AccessCount - 1)) + AccessTimeMs) / 
            Zone->Metrics.AccessCount;
    }
    
    // Track thread access
    static TMap<int32, TSet<int32>> ThreadAccessCounts;
    TSet<int32>* ThreadSet = ThreadAccessCounts.Find(ZoneId);
    if (!ThreadSet)
    {
        TSet<int32> NewSet;
        NewSet.Add(ThreadId);
        ThreadAccessCounts.Add(ZoneId, NewSet);
    }
    else
    {
        ThreadSet->Add(ThreadId);
    }
    
    // Update thread access count
    Zone->Metrics.ThreadAccessCount = ThreadSet ? ThreadSet->Num() : 1;
    
    // Update modification status
    if (bWasModified)
    {
        // Calculate modification frequency
        float ModificationFrequency = static_cast<float>(Zone->Metrics.ModificationCount + 1) / 
                                     static_cast<float>(Zone->Metrics.AccessCount);
        
        // Mark as frequently modified if above threshold
        if (ModificationFrequency >= FrequentModificationThreshold)
        {
            Zone->Metrics.bFrequentlyModified = true;
        }
        
        // Increment modification count
        Zone->Metrics.ModificationCount++;
    }
    
    // Update contention status
    if (Zone->Metrics.ConflictCount > HighContentionThreshold)
    {
        Zone->Metrics.bHighContention = true;
    }
    
    // Update access frequency (accesses per second)
    double CurrentTime = FPlatformTime::Seconds();
    static double LastFrequencyUpdateTime = CurrentTime;
    static TMap<int32, uint64> LastAccessCounts;
    
    // Update frequency every 5 seconds
    if (CurrentTime - LastFrequencyUpdateTime >= 5.0)
    {
        // Get previous access count
        uint64* LastCount = LastAccessCounts.Find(ZoneId);
        uint64 PreviousCount = LastCount ? *LastCount : 0;
        
        // Calculate frequency
        double TimeElapsed = CurrentTime - LastFrequencyUpdateTime;
        float Frequency = static_cast<float>(Zone->Metrics.AccessCount - PreviousCount) / static_cast<float>(TimeElapsed);
        
        // Update metrics
        Zone->Metrics.AccessFrequency = Frequency;
        
        // Store current count for next update
        LastAccessCounts.Add(ZoneId, Zone->Metrics.AccessCount);
        
        // Update last time if this is the first zone processed this cycle
        if (ZoneId == Zones.begin()->Key)
        {
            LastFrequencyUpdateTime = CurrentTime;
        }
    }
}

/**
 * Gets metrics for a zone
 */
FZoneMetrics FZoneManager::GetZoneMetrics(int32 ZoneId) const
{
    FZoneMetrics EmptyMetrics;
    
    // Ensure we're initialized
    if (!bIsInitialized)
    {
        return EmptyMetrics;
    }
    
    FScopeLock Lock(&ZoneLock);
    
    // Find the zone
    const FZoneDescriptor* const* ZonePtr = Zones.Find(ZoneId);
    if (!ZonePtr || !(*ZonePtr))
    {
        return EmptyMetrics;
    }
    
    // Return zone metrics
    return (*ZonePtr)->Metrics;
}

/**
 * Gets zones with high contention
 */
TArray<int32> FZoneManager::GetHighContentionZones() const
{
    TArray<int32> Result;
    
    // Ensure we're initialized
    if (!bIsInitialized)
    {
        return Result;
    }
    
    FScopeLock Lock(&ZoneLock);
    
    // Find zones with high contention
    for (const auto& Pair : Zones)
    {
        if (Pair.Value && Pair.Value->Metrics.bHighContention)
        {
            Result.Add(Pair.Key);
        }
    }
    
    return Result;
}

/**
 * Gets the current version of a zone
 */
uint32 FZoneManager::GetZoneVersion(int32 ZoneId) const
{
    // Ensure we're initialized
    if (!bIsInitialized)
    {
        return 0;
    }
    
    FScopeLock Lock(&ZoneLock);
    
    // Find the zone
    const FZoneDescriptor* const* ZonePtr = Zones.Find(ZoneId);
    if (!ZonePtr || !(*ZonePtr))
    {
        return 0;
    }
    
    // Return zone version
    return (*ZonePtr)->Version.GetValue();
}

/**
 * Gets the current version of a material in a zone
 */
uint32 FZoneManager::GetMaterialVersion(int32 ZoneId, int32 MaterialId) const
{
    // Ensure we're initialized
    if (!bIsInitialized)
    {
        return 0;
    }
    
    FScopeLock Lock(&ZoneLock);
    
    // Find the zone
    const FZoneDescriptor* const* ZonePtr = Zones.Find(ZoneId);
    if (!ZonePtr || !(*ZonePtr))
    {
        return 0;
    }
    
    const FZoneDescriptor* Zone = *ZonePtr;
    
    // Find material version counter
    const FThreadSafeCounter* const* VersionPtr = Zone->MaterialVersions.Find(MaterialId);
    if (!VersionPtr || !(*VersionPtr))
    {
        return 0;
    }
    
    // Return material version
    return (*VersionPtr)->GetValue();
}

/**
 * Increments the version of a zone
 */
uint32 FZoneManager::IncrementZoneVersion(int32 ZoneId)
{
    // Ensure we're initialized
    if (!bIsInitialized)
    {
        return 0;
    }
    
    // Get the zone
    FZoneDescriptor* Zone = GetZone(ZoneId);
    if (!Zone)
    {
        return 0;
    }
    
    // Increment and return new version
    return Zone->Version.Increment();
}

/**
 * Increments the version of a material in a zone
 */
uint32 FZoneManager::IncrementMaterialVersion(int32 ZoneId, int32 MaterialId)
{
    // Ensure we're initialized
    if (!bIsInitialized)
    {
        return 0;
    }
    
    // Get the zone
    FZoneDescriptor* Zone = GetZone(ZoneId);
    if (!Zone)
    {
        return 0;
    }
    
    FScopeLock Lock(&ZoneLock);
    
    // Get or create the material version counter
    FThreadSafeCounter* VersionCounter = GetOrCreateMaterialVersion(Zone, MaterialId);
    
    // Increment and return new version
    return VersionCounter->Increment();
}

/**
 * Gets or creates a material version counter for a zone
 */
FThreadSafeCounter* FZoneManager::GetOrCreateMaterialVersion(FZoneDescriptor* Zone, int32 MaterialId)
{
    // Find existing counter
    FThreadSafeCounter** CounterPtr = Zone->MaterialVersions.Find(MaterialId);
    if (CounterPtr)
    {
        return *CounterPtr;
    }
    
    // Create a new counter
    FThreadSafeCounter* NewCounter = new FThreadSafeCounter(1); // Start at version 1
    Zone->MaterialVersions.Add(MaterialId, NewCounter);
    return NewCounter;
}

/**
 * Calculates the probability of conflict for a zone based on metrics
 */
float FZoneManager::GetZoneConflictProbability(int32 ZoneId) const
{
    // Ensure we're initialized
    if (!bIsInitialized)
    {
        return 0.0f;
    }
    
    FScopeLock Lock(&ZoneLock);
    
    // Find the zone
    const FZoneDescriptor* const* ZonePtr = Zones.Find(ZoneId);
    if (!ZonePtr || !(*ZonePtr))
    {
        return 0.0f;
    }
    
    const FZoneDescriptor* Zone = *ZonePtr;
    
    // Calculate conflict probability
    if (Zone->Metrics.AccessCount == 0)
    {
        return 0.0f;
    }
    
    float ConflictProbability = static_cast<float>(Zone->Metrics.ConflictCount) / 
                               static_cast<float>(Zone->Metrics.AccessCount);
    
    // Adjust based on access frequency and thread count
    if (Zone->Metrics.AccessFrequency > 10.0f)
    {
        // High access frequency increases conflict probability
        ConflictProbability *= 1.5f;
    }
    
    if (Zone->Metrics.ThreadAccessCount > 4)
    {
        // More threads accessing increases conflict probability
        ConflictProbability *= (1.0f + (0.1f * (Zone->Metrics.ThreadAccessCount - 4)));
    }
    
    // Cap at 1.0
    return FMath::Min(ConflictProbability, 1.0f);
}

/**
 * Optimizes the zone layout for a region based on access patterns
 */
bool FZoneManager::OptimizeZoneLayout(int32 RegionId)
{
    // Ensure we're initialized
    if (!bIsInitialized)
    {
        return false;
    }
    
    // Get zones in the region
    TArray<int32> RegionZones = GetZonesInRegion(RegionId);
    if (RegionZones.Num() == 0)
    {
        return false;
    }
    
    bool bChangesApplied = false;
    
    // Identify high contention zones for splitting
    TArray<int32> HighContentionZones;
    TArray<int32> LowUsageZones;
    
    for (int32 ZoneId : RegionZones)
    {
        FZoneMetrics Metrics = GetZoneMetrics(ZoneId);
        
        if (Metrics.bHighContention)
        {
            HighContentionZones.Add(ZoneId);
        }
        else if (Metrics.AccessCount < 100 && !Metrics.bFrequentlyModified)
        {
            LowUsageZones.Add(ZoneId);
        }
    }
    
    // Split high contention zones
    for (int32 ZoneId : HighContentionZones)
    {
        if (SplitZone(ZoneId))
        {
            bChangesApplied = true;
        }
    }
    
    // Find pairs of adjacent low-usage zones for merging
    for (int32 i = 0; i < LowUsageZones.Num(); i++)
    {
        for (int32 j = i + 1; j < LowUsageZones.Num(); j++)
        {
            int32 Zone1 = LowUsageZones[i];
            int32 Zone2 = LowUsageZones[j];
            
            if (AreZonesAdjacent(Zone1, Zone2))
            {
                if (MergeZones(Zone1, Zone2))
                {
                    bChangesApplied = true;
                    
                    // Remove Zone2 from the list as it no longer exists
                    LowUsageZones.RemoveAt(j);
                    j--;
                    
                    // Break inner loop to avoid checking the merged zone against others in this iteration
                    break;
                }
            }
        }
    }
    
    // Reorganize materials in frequently modified zones
    for (int32 ZoneId : RegionZones)
    {
        FZoneMetrics Metrics = GetZoneMetrics(ZoneId);
        
        if (Metrics.bFrequentlyModified)
        {
            if (ReorganizeZoneMaterials(ZoneId))
            {
                bChangesApplied = true;
            }
        }
    }
    
    return bChangesApplied;
}

/**
 * Splits a high contention zone into smaller zones
 */
bool FZoneManager::SplitZone(int32 ZoneId)
{
    // Ensure we're initialized
    if (!bIsInitialized)
    {
        return false;
    }
    
    // Get the zone
    FZoneDescriptor* Zone = GetZone(ZoneId);
    if (!Zone)
    {
        return false;
    }
    
    // Check if zone has high contention
    if (!Zone->Metrics.bHighContention)
    {
        return false;
    }
    
    // Only split if zone is currently not in use
    if (Zone->OwnershipStatus != EZoneOwnershipStatus::None)
    {
        return false;
    }
    
    FScopeLock Lock(&ZoneLock);
    
    // Calculate dimensions for sub-zones
    FVector SubDimensions = Zone->Dimensions * 0.5f;
    
    // Create 8 sub-zones (octree division)
    TArray<int32> NewZoneIds;
    for (int32 X = 0; X < 2; X++)
    {
        for (int32 Y = 0; Y < 2; Y++)
        {
            for (int32 Z = 0; Z < 2; Z++)
            {
                // Calculate sub-zone position
                FVector Offset = FVector(
                    (X - 0.5f) * SubDimensions.X,
                    (Y - 0.5f) * SubDimensions.Y,
                    (Z - 0.5f) * SubDimensions.Z
                );
                FVector SubPosition = Zone->Position + Offset;
                
                // Create new zone
                int32 NewZoneId = CreateZone(SubPosition, Zone->RegionId);
                if (NewZoneId != INDEX_NONE)
                {
                    NewZoneIds.Add(NewZoneId);
                    
                    // Update dimensions
                    FZoneDescriptor* NewZone = GetZone(NewZoneId);
                    if (NewZone)
                    {
                        NewZone->Dimensions = SubDimensions;
                        
                        // Copy material IDs from parent zone
                        NewZone->MaterialIds = Zone->MaterialIds;
                        
                        // Create material version counters for the new zone
                        for (int32 MaterialId : NewZone->MaterialIds)
                        {
                            GetOrCreateMaterialVersion(NewZone, MaterialId);
                        }
                    }
                }
            }
        }
    }
    
    // Check if all sub-zones were created successfully
    if (NewZoneIds.Num() == 8)
    {
        // Remove the original zone
        RemoveZone(ZoneId);
        return true;
    }
    else
    {
        // Remove any partially created sub-zones
        for (int32 NewZoneId : NewZoneIds)
        {
            RemoveZone(NewZoneId);
        }
        return false;
    }
}

/**
 * Merges two adjacent zones with low usage
 */
bool FZoneManager::MergeZones(int32 ZoneId1, int32 ZoneId2)
{
    // Ensure we're initialized
    if (!bIsInitialized)
    {
        return false;
    }
    
    // Check if zones are adjacent
    if (!AreZonesAdjacent(ZoneId1, ZoneId2))
    {
        return false;
    }
    
    // Get the zones
    FZoneDescriptor* Zone1 = GetZone(ZoneId1);
    FZoneDescriptor* Zone2 = GetZone(ZoneId2);
    
    if (!Zone1 || !Zone2)
    {
        return false;
    }
    
    // Check if both zones have low usage
    if (Zone1->Metrics.bHighContention || Zone2->Metrics.bHighContention ||
        Zone1->Metrics.bFrequentlyModified || Zone2->Metrics.bFrequentlyModified)
    {
        return false;
    }
    
    // Only merge if both zones are currently not in use
    if (Zone1->OwnershipStatus != EZoneOwnershipStatus::None ||
        Zone2->OwnershipStatus != EZoneOwnershipStatus::None)
    {
        return false;
    }
    
    FScopeLock Lock(&ZoneLock);
    
    // Create new zone at the midpoint between the two zones
    FVector MergedPosition = (Zone1->Position + Zone2->Position) * 0.5f;
    
    // Calculate dimensions to encompass both zones
    FVector Min = FVector(
        FMath::Min(Zone1->Position.X - Zone1->Dimensions.X * 0.5f, Zone2->Position.X - Zone2->Dimensions.X * 0.5f),
        FMath::Min(Zone1->Position.Y - Zone1->Dimensions.Y * 0.5f, Zone2->Position.Y - Zone2->Dimensions.Y * 0.5f),
        FMath::Min(Zone1->Position.Z - Zone1->Dimensions.Z * 0.5f, Zone2->Position.Z - Zone2->Dimensions.Z * 0.5f)
    );
    
    FVector Max = FVector(
        FMath::Max(Zone1->Position.X + Zone1->Dimensions.X * 0.5f, Zone2->Position.X + Zone2->Dimensions.X * 0.5f),
        FMath::Max(Zone1->Position.Y + Zone1->Dimensions.Y * 0.5f, Zone2->Position.Y + Zone2->Dimensions.Y * 0.5f),
        FMath::Max(Zone1->Position.Z + Zone1->Dimensions.Z * 0.5f, Zone2->Position.Z + Zone2->Dimensions.Z * 0.5f)
    );
    
    FVector MergedDimensions = Max - Min;
    
    // Create the merged zone
    int32 MergedZoneId = CreateZone(MergedPosition, Zone1->RegionId);
    if (MergedZoneId == INDEX_NONE)
    {
        return false;
    }
    
    // Get the merged zone
    FZoneDescriptor* MergedZone = GetZone(MergedZoneId);
    if (!MergedZone)
    {
        return false;
    }
    
    // Set merged zone dimensions
    MergedZone->Dimensions = MergedDimensions;
    
    // Combine material IDs
    TSet<int32> CombinedMaterials;
    for (int32 MaterialId : Zone1->MaterialIds)
    {
        CombinedMaterials.Add(MaterialId);
    }
    for (int32 MaterialId : Zone2->MaterialIds)
    {
        CombinedMaterials.Add(MaterialId);
    }
    
    // Set combined materials
    MergedZone->MaterialIds.Empty();
    for (int32 MaterialId : CombinedMaterials)
    {
        MergedZone->MaterialIds.Add(MaterialId);
    }
    
    // Create material version counters for the merged zone
    for (int32 MaterialId : MergedZone->MaterialIds)
    {
        FThreadSafeCounter* VersionCounter = GetOrCreateMaterialVersion(MergedZone, MaterialId);
        
        // Set to maximum of the two zone's material versions
        uint32 Version1 = GetMaterialVersion(ZoneId1, MaterialId);
        uint32 Version2 = GetMaterialVersion(ZoneId2, MaterialId);
        uint32 MaxVersion = FMath::Max(Version1, Version2);
        
        if (MaxVersion > 1)
        {
            VersionCounter->Set(MaxVersion);
        }
    }
    
    // Remove the original zones
    bool bSuccess = RemoveZone(ZoneId1) && RemoveZone(ZoneId2);
    
    return bSuccess;
}

/**
 * Checks if two zones are adjacent in space
 */
bool FZoneManager::AreZonesAdjacent(int32 ZoneId1, int32 ZoneId2)
{
    // Ensure we're initialized
    if (!bIsInitialized)
    {
        return false;
    }
    
    // Get the zones
    FZoneDescriptor* Zone1 = GetZone(ZoneId1);
    FZoneDescriptor* Zone2 = GetZone(ZoneId2);
    
    if (!Zone1 || !Zone2)
    {
        return false;
    }
    
    // Calculate extents of each zone
    FVector Min1 = Zone1->Position - (Zone1->Dimensions * 0.5f);
    FVector Max1 = Zone1->Position + (Zone1->Dimensions * 0.5f);
    
    FVector Min2 = Zone2->Position - (Zone2->Dimensions * 0.5f);
    FVector Max2 = Zone2->Position + (Zone2->Dimensions * 0.5f);
    
    // Check if zones are adjacent (share a face, edge, or corner)
    bool bOverlapX = (Min1.X <= Max2.X) && (Min2.X <= Max1.X);
    bool bOverlapY = (Min1.Y <= Max2.Y) && (Min2.Y <= Max1.Y);
    bool bOverlapZ = (Min1.Z <= Max2.Z) && (Min2.Z <= Max1.Z);
    
    // Two zones are adjacent if they overlap in two dimensions and are adjacent in the third
    bool bTouchX = FMath::IsNearlyEqual(Min1.X, Max2.X, 0.1f) || FMath::IsNearlyEqual(Max1.X, Min2.X, 0.1f);
    bool bTouchY = FMath::IsNearlyEqual(Min1.Y, Max2.Y, 0.1f) || FMath::IsNearlyEqual(Max1.Y, Min2.Y, 0.1f);
    bool bTouchZ = FMath::IsNearlyEqual(Min1.Z, Max2.Z, 0.1f) || FMath::IsNearlyEqual(Max1.Z, Min2.Z, 0.1f);
    
    return (bOverlapY && bOverlapZ && bTouchX) ||
           (bOverlapX && bOverlapZ && bTouchY) ||
           (bOverlapX && bOverlapY && bTouchZ);
}

/**
 * Reorganizes materials within a fragmented zone
 */
bool FZoneManager::ReorganizeZoneMaterials(int32 ZoneId)
{
    // Ensure we're initialized
    if (!bIsInitialized)
    {
        return false;
    }
    
    // Get the zone
    FZoneDescriptor* Zone = GetZone(ZoneId);
    if (!Zone)
    {
        return false;
    }
    
    // Only reorganize if zone is currently not in use
    if (Zone->OwnershipStatus != EZoneOwnershipStatus::None)
    {
        return false;
    }
    
    FScopeLock Lock(&ZoneLock);
    
    // Identify most accessed material
    int32 MostAccessedMaterial = INDEX_NONE;
    uint64 HighestAccessCount = 0;
    
    // In a real implementation, we would have material-specific access counts
    // For now, we'll just increment the zone version to indicate a reorganization
    Zone->Version.Increment();
    
    // Flag that changes were made
    return true;
}

/**
 * Gets an instance of the zone manager
 */
FZoneManager& FZoneManager::Get()
{
    if (!Instance)
    {
        Instance = new FZoneManager();
        Instance->Initialize();
    }
    
    return *Instance;
}

void FZoneManager::UpdateZoneContentionStatus(FZoneDescriptor* Zone)
{
    if (!Zone)
    {
        return;
    }
    
    // Calculate contention rate
    float ContentionRate = 0.0f;
    if (Zone->Metrics.AccessCount > 0)
    {
        ContentionRate = static_cast<float>(Zone->Metrics.ConflictCount) / 
            static_cast<float>(Zone->Metrics.AccessCount);
    }
    
    // Update high contention flag
    Zone->Metrics.bHighContention = (ContentionRate > 0.1f) || 
        (Zone->Metrics.ConflictCount > HighContentionThreshold);
}