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

FZoneManager::FZoneManager()
    : bIsInitialized(false)
{
    // Set the singleton instance
    check(Instance == nullptr);
    Instance = this;
}

FZoneManager::~FZoneManager()
{
    Shutdown();
    
    // Clear the singleton instance if it's us
    if (Instance == this)
    {
        Instance = nullptr;
    }
}

bool FZoneManager::Initialize()
{
    if (bIsInitialized)
    {
        return true;
    }
    
    bIsInitialized = true;
    return true;
}

void FZoneManager::Shutdown()
{
    if (!bIsInitialized)
    {
        return;
    }
    
    // Clean up all zones
    FScopeLock Lock(&ZoneLock);
    
    for (auto& Pair : Zones)
    {
        FZoneDescriptor* Zone = Pair.Value;
        
        // Clean up material version counters
        for (auto& MaterialPair : Zone->MaterialVersions)
        {
            delete MaterialPair.Value;
        }
        
        delete Zone;
    }
    
    Zones.Empty();
    ZonesByRegion.Empty();
    SpatialLookup.Empty();
    
    bIsInitialized = false;
}

bool FZoneManager::IsInitialized() const
{
    return bIsInitialized;
}

int32 FZoneManager::CreateZone(const FVector& Position, int32 RegionId)
{
    if (!bIsInitialized)
    {
        return INDEX_NONE;
    }
    
    FScopeLock Lock(&ZoneLock);
    
    // Create a new zone descriptor
    FZoneDescriptor* Zone = new FZoneDescriptor();
    Zone->ZoneId = NextZoneId.Increment();
    Zone->RegionId = RegionId;
    Zone->Position = Position;
    Zone->OwnerThreadId.Set(INDEX_NONE);
    Zone->OwnershipStatus = EZoneOwnershipStatus::None;
    
    // Add zone to maps
    Zones.Add(Zone->ZoneId, Zone);
    
    // Add to region map
    TSet<int32>& RegionZones = ZonesByRegion.FindOrAdd(RegionId);
    RegionZones.Add(Zone->ZoneId);
    
    // Add to spatial lookup
    AddZoneToSpatialLookup(Zone->ZoneId, Position);
    
    return Zone->ZoneId;
}

bool FZoneManager::RemoveZone(int32 ZoneId)
{
    if (!bIsInitialized || ZoneId == INDEX_NONE)
    {
        return false;
    }
    
    FScopeLock Lock(&ZoneLock);
    
    FZoneDescriptor* Zone = Zones.FindRef(ZoneId);
    if (!Zone)
    {
        return false;
    }
    
    // Remove from spatial lookup
    RemoveZoneFromSpatialLookup(ZoneId, Zone->Position);
    
    // Remove from region map
    TSet<int32>* RegionZones = ZonesByRegion.Find(Zone->RegionId);
    if (RegionZones)
    {
        RegionZones->Remove(ZoneId);
        if (RegionZones->Num() == 0)
        {
            ZonesByRegion.Remove(Zone->RegionId);
        }
    }
    
    // Clean up material version counters
    for (auto& MaterialPair : Zone->MaterialVersions)
    {
        delete MaterialPair.Value;
    }
    
    // Remove from zones map and delete
    Zones.Remove(ZoneId);
    delete Zone;
    
    return true;
}

FZoneDescriptor* FZoneManager::GetZone(int32 ZoneId)
{
    if (!bIsInitialized || ZoneId == INDEX_NONE)
    {
        return nullptr;
    }
    
    FScopeLock Lock(&ZoneLock);
    return Zones.FindRef(ZoneId);
}

FZoneDescriptor* FZoneManager::GetZoneAtPosition(const FVector& Position)
{
    if (!bIsInitialized)
    {
        return nullptr;
    }
    
    FScopeLock Lock(&ZoneLock);
    
    // Compute the spatial key for the position
    FIntVector SpatialKey = ComputeSpatialKey(Position);
    
    // Look up zones at this key
    TArray<int32>* ZoneIds = SpatialLookup.Find(SpatialKey);
    if (!ZoneIds || ZoneIds->Num() == 0)
    {
        return nullptr;
    }
    
    // Find the closest zone to the position
    float ClosestDistance = FLT_MAX;
    FZoneDescriptor* ClosestZone = nullptr;
    
    for (int32 ZoneId : *ZoneIds)
    {
        FZoneDescriptor* Zone = Zones.FindRef(ZoneId);
        if (Zone)
        {
            float Distance = FVector::Distance(Position, Zone->Position);
            if (Distance < ClosestDistance)
            {
                ClosestDistance = Distance;
                ClosestZone = Zone;
            }
        }
    }
    
    return ClosestZone;
}

bool FZoneManager::AcquireZoneOwnership(int32 ZoneId, int32 ThreadId, EZoneAccessMode AccessMode, uint32 TimeoutMs)
{
    if (!bIsInitialized || ZoneId == INDEX_NONE || ThreadId == INDEX_NONE)
    {
        return false;
    }
    
    FZoneDescriptor* Zone = GetZone(ZoneId);
    if (!Zone)
    {
        return false;
    }
    
    bool bAcquired = false;
    double StartTime = FPlatformTime::Seconds();
    uint32 TimeoutMicroseconds = TimeoutMs * 1000;
    
    while (!bAcquired)
    {
        // Try to acquire ownership based on access mode
        if (AccessMode == EZoneAccessMode::ReadOnly)
        {
            // For read-only access, we can allow multiple readers as long as there's no exclusive owner
            Zone->Lock.Lock();
            
            if (Zone->OwnershipStatus == EZoneOwnershipStatus::None ||
                Zone->OwnershipStatus == EZoneOwnershipStatus::Shared)
            {
                Zone->ReaderCount.Increment();
                Zone->OwnershipStatus = EZoneOwnershipStatus::Shared;
                bAcquired = true;
            }
            
            Zone->Lock.Unlock();
        }
        else if (AccessMode == EZoneAccessMode::Exclusive || AccessMode == EZoneAccessMode::ReadWrite)
        {
            // For exclusive or read-write access, we need exclusive ownership
            Zone->Lock.Lock();
            
            if (Zone->OwnershipStatus == EZoneOwnershipStatus::None)
            {
                Zone->OwnerThreadId.Set(ThreadId);
                Zone->OwnershipStatus = EZoneOwnershipStatus::Exclusive;
                bAcquired = true;
            }
            
            Zone->Lock.Unlock();
        }
        else if (AccessMode == EZoneAccessMode::MaterialOnly)
        {
            // For material-only access, we can allow shared access
            Zone->Lock.Lock();
            
            if (Zone->OwnershipStatus == EZoneOwnershipStatus::None ||
                Zone->OwnershipStatus == EZoneOwnershipStatus::Shared)
            {
                Zone->ReaderCount.Increment();
                Zone->OwnershipStatus = EZoneOwnershipStatus::Shared;
                bAcquired = true;
            }
            
            Zone->Lock.Unlock();
        }
        
        if (bAcquired)
        {
            break;
        }
        
        // Check for timeout
        if (TimeoutMs > 0)
        {
            double ElapsedMicroseconds = (FPlatformTime::Seconds() - StartTime) * 1000000.0;
            if (ElapsedMicroseconds >= TimeoutMicroseconds)
            {
                return false;
            }
        }
        
        // Sleep briefly to avoid spinning
        FPlatformProcess::Sleep(0.001f);
    }
    
    return bAcquired;
}

bool FZoneManager::ReleaseZoneOwnership(int32 ZoneId, int32 ThreadId)
{
    if (!bIsInitialized || ZoneId == INDEX_NONE)
    {
        return false;
    }
    
    FZoneDescriptor* Zone = GetZone(ZoneId);
    if (!Zone)
    {
        return false;
    }
    
    Zone->Lock.Lock();
    
    // Check if the thread owned the zone exclusively
    if (Zone->OwnershipStatus == EZoneOwnershipStatus::Exclusive)
    {
        if (Zone->OwnerThreadId.GetValue() == ThreadId)
        {
            Zone->OwnerThreadId.Set(INDEX_NONE);
            Zone->OwnershipStatus = EZoneOwnershipStatus::None;
            Zone->Lock.Unlock();
            return true;
        }
        else
        {
            // Trying to release a zone owned by another thread
            Zone->Lock.Unlock();
            return false;
        }
    }
    // Check if the thread was one of multiple readers
    else if (Zone->OwnershipStatus == EZoneOwnershipStatus::Shared)
    {
        int32 NewCount = Zone->ReaderCount.Decrement();
        
        // If this was the last reader, update the ownership status
        if (NewCount == 0)
        {
            Zone->OwnershipStatus = EZoneOwnershipStatus::None;
        }
        
        Zone->Lock.Unlock();
        return true;
    }
    
    // Zone wasn't owned
    Zone->Lock.Unlock();
    return false;
}

int32 FZoneManager::GetZoneOwner(int32 ZoneId) const
{
    if (!bIsInitialized || ZoneId == INDEX_NONE)
    {
        return INDEX_NONE;
    }
    
    FScopeLock Lock(&ZoneLock);
    
    FZoneDescriptor* const* ZonePtr = Zones.Find(ZoneId);
    if (!ZonePtr || !(*ZonePtr))
    {
        return INDEX_NONE;
    }
    
    return (*ZonePtr)->OwnerThreadId.GetValue();
}

TArray<int32> FZoneManager::GetZonesInRegion(int32 RegionId) const
{
    TArray<int32> Result;
    
    if (!bIsInitialized || RegionId == INDEX_NONE)
    {
        return Result;
    }
    
    FScopeLock Lock(&ZoneLock);
    
    const TSet<int32>* RegionZones = ZonesByRegion.Find(RegionId);
    if (RegionZones)
    {
        Result = RegionZones->Array();
    }
    
    return Result;
}

TArray<int32> FZoneManager::GetZonesInRadius(const FVector& Position, float Radius) const
{
    TArray<int32> Result;
    
    if (!bIsInitialized || Radius <= 0.0f)
    {
        return Result;
    }
    
    FScopeLock Lock(&ZoneLock);
    
    // Calculate the range of spatial keys to search
    int32 RadiusCells = FMath::CeilToInt(Radius / SpatialGridSize);
    FIntVector BaseKey = ComputeSpatialKey(Position);
    
    // Iterate through all potentially overlapping grid cells
    for (int32 Z = -RadiusCells; Z <= RadiusCells; ++Z)
    {
        for (int32 Y = -RadiusCells; Y <= RadiusCells; ++Y)
        {
            for (int32 X = -RadiusCells; X <= RadiusCells; ++X)
            {
                FIntVector Key = BaseKey + FIntVector(X, Y, Z);
                
                // Check for zones in this cell
                const TArray<int32>* ZoneIds = SpatialLookup.Find(Key);
                if (ZoneIds)
                {
                    for (int32 ZoneId : *ZoneIds)
                    {
                        FZoneDescriptor* const* ZonePtr = Zones.Find(ZoneId);
                        if (ZonePtr && *ZonePtr)
                        {
                            // Check if the zone is within the radius
                            FZoneDescriptor* Zone = *ZonePtr;
                            if (FVector::Distance(Position, Zone->Position) <= Radius)
                            {
                                Result.Add(ZoneId);
                            }
                        }
                    }
                }
            }
        }
    }
    
    return Result;
}

int32 FZoneManager::GetZoneCount() const
{
    FScopeLock Lock(&ZoneLock);
    return Zones.Num();
}

bool FZoneManager::UpdateZoneMaterials(int32 ZoneId, const TArray<int32>& MaterialIds)
{
    if (!bIsInitialized || ZoneId == INDEX_NONE)
    {
        return false;
    }
    
    FZoneDescriptor* Zone = GetZone(ZoneId);
    if (!Zone)
    {
        return false;
    }
    
    Zone->Lock.Lock();
    
    // Update the materials list
    Zone->MaterialIds = MaterialIds;
    
    // Create version counters for any new materials
    for (int32 MaterialId : MaterialIds)
    {
        GetOrCreateMaterialVersion(Zone, MaterialId);
    }
    
    // Increment zone version to indicate modification
    Zone->Version.Increment();
    
    Zone->Lock.Unlock();
    
    return true;
}

void FZoneManager::RecordZoneAccess(int32 ZoneId, int32 ThreadId, double AccessTimeMs, bool bWasModified, bool bHadConflict)
{
    if (!bIsInitialized || ZoneId == INDEX_NONE || ThreadId == INDEX_NONE)
    {
        return;
    }
    
    FZoneDescriptor* Zone = GetZone(ZoneId);
    if (!Zone)
    {
        return;
    }
    
    Zone->Lock.Lock();
    
    // Update access metrics
    Zone->Metrics.AccessCount++;
    
    // Update average access time using a weighted average
    if (Zone->Metrics.AccessCount <= 1)
    {
        Zone->Metrics.AverageAccessTimeMs = AccessTimeMs;
    }
    else
    {
        Zone->Metrics.AverageAccessTimeMs = 
            (Zone->Metrics.AverageAccessTimeMs * 0.9) + (AccessTimeMs * 0.1);
    }
    
    // Record conflicts
    if (bHadConflict)
    {
        Zone->Metrics.ConflictCount++;
    }
    
    // Update frequently modified flag
    if (bWasModified)
    {
        float ModificationRate = static_cast<float>(Zone->Metrics.AccessCount) / 
            static_cast<float>(Zone->Metrics.AccessCount);
            
        Zone->Metrics.bFrequentlyModified = (ModificationRate >= FrequentModificationThreshold);
    }
    
    // Update contention status
    UpdateZoneContentionStatus(Zone);
    
    Zone->Lock.Unlock();
}

FZoneMetrics FZoneManager::GetZoneMetrics(int32 ZoneId) const
{
    FZoneMetrics EmptyMetrics;
    
    if (!bIsInitialized || ZoneId == INDEX_NONE)
    {
        return EmptyMetrics;
    }
    
    FScopeLock Lock(&ZoneLock);
    
    FZoneDescriptor* const* ZonePtr = Zones.Find(ZoneId);
    if (!ZonePtr || !(*ZonePtr))
    {
        return EmptyMetrics;
    }
    
    return (*ZonePtr)->Metrics;
}

TArray<int32> FZoneManager::GetHighContentionZones() const
{
    TArray<int32> Result;
    
    if (!bIsInitialized)
    {
        return Result;
    }
    
    FScopeLock Lock(&ZoneLock);
    
    for (const auto& Pair : Zones)
    {
        if (Pair.Value && Pair.Value->Metrics.bHighContention)
        {
            Result.Add(Pair.Key);
        }
    }
    
    return Result;
}

uint32 FZoneManager::GetZoneVersion(int32 ZoneId) const
{
    if (!bIsInitialized || ZoneId == INDEX_NONE)
    {
        return 0;
    }
    
    FScopeLock Lock(&ZoneLock);
    
    FZoneDescriptor* const* ZonePtr = Zones.Find(ZoneId);
    if (!ZonePtr || !(*ZonePtr))
    {
        return 0;
    }
    
    return static_cast<uint32>((*ZonePtr)->Version.GetValue());
}

uint32 FZoneManager::GetMaterialVersion(int32 ZoneId, int32 MaterialId) const
{
    if (!bIsInitialized || ZoneId == INDEX_NONE || MaterialId == INDEX_NONE)
    {
        return 0;
    }
    
    FScopeLock Lock(&ZoneLock);
    
    FZoneDescriptor* const* ZonePtr = Zones.Find(ZoneId);
    if (!ZonePtr || !(*ZonePtr))
    {
        return 0;
    }
    
    FZoneDescriptor* Zone = *ZonePtr;
    
    FThreadSafeCounter* const* VersionCounter = Zone->MaterialVersions.Find(MaterialId);
    if (!VersionCounter || !(*VersionCounter))
    {
        return 0;
    }
    
    return static_cast<uint32>((*VersionCounter)->GetValue());
}

uint32 FZoneManager::IncrementZoneVersion(int32 ZoneId)
{
    if (!bIsInitialized || ZoneId == INDEX_NONE)
    {
        return 0;
    }
    
    FZoneDescriptor* Zone = GetZone(ZoneId);
    if (!Zone)
    {
        return 0;
    }
    
    int32 NewVersion = Zone->Version.Increment();
    return static_cast<uint32>(NewVersion);
}

uint32 FZoneManager::IncrementMaterialVersion(int32 ZoneId, int32 MaterialId)
{
    if (!bIsInitialized || ZoneId == INDEX_NONE || MaterialId == INDEX_NONE)
    {
        return 0;
    }
    
    FZoneDescriptor* Zone = GetZone(ZoneId);
    if (!Zone)
    {
        return 0;
    }
    
    FThreadSafeCounter* VersionCounter = GetOrCreateMaterialVersion(Zone, MaterialId);
    if (!VersionCounter)
    {
        return 0;
    }
    
    int32 NewVersion = VersionCounter->Increment();
    
    // Also increment the overall zone version
    Zone->Version.Increment();
    
    return static_cast<uint32>(NewVersion);
}

FZoneManager& FZoneManager::Get()
{
    check(Instance != nullptr);
    return *Instance;
}

FThreadSafeCounter* FZoneManager::GetOrCreateMaterialVersion(FZoneDescriptor* Zone, int32 MaterialId)
{
    if (!Zone)
    {
        return nullptr;
    }
    
    FThreadSafeCounter** VersionCounter = Zone->MaterialVersions.Find(MaterialId);
    
    if (VersionCounter)
    {
        return *VersionCounter;
    }
    else
    {
        FThreadSafeCounter* NewCounter = new FThreadSafeCounter(0);
        Zone->MaterialVersions.Add(MaterialId, NewCounter);
        return NewCounter;
    }
}

FIntVector FZoneManager::ComputeSpatialKey(const FVector& Position) const
{
    return FIntVector(
        FMath::FloorToInt(Position.X / SpatialGridSize),
        FMath::FloorToInt(Position.Y / SpatialGridSize),
        FMath::FloorToInt(Position.Z / SpatialGridSize)
    );
}

void FZoneManager::AddZoneToSpatialLookup(int32 ZoneId, const FVector& Position)
{
    FIntVector SpatialKey = ComputeSpatialKey(Position);
    
    TArray<int32>& ZonesAtLocation = SpatialLookup.FindOrAdd(SpatialKey);
    ZonesAtLocation.AddUnique(ZoneId);
}

void FZoneManager::RemoveZoneFromSpatialLookup(int32 ZoneId, const FVector& Position)
{
    FIntVector SpatialKey = ComputeSpatialKey(Position);
    
    TArray<int32>* ZonesAtLocation = SpatialLookup.Find(SpatialKey);
    if (ZonesAtLocation)
    {
        ZonesAtLocation->Remove(ZoneId);
        
        if (ZonesAtLocation->Num() == 0)
        {
            SpatialLookup.Remove(SpatialKey);
        }
    }
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