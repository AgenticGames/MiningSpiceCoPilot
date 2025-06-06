// Copyright Epic Games, Inc. All Rights Reserved.

#include "SharedBufferManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ScopeLock.h"
#include "HAL/PlatformTime.h"
#include "RHI.h"

FSharedBufferManager::FSharedBufferManager(const FName& InName, uint64 InSizeInBytes, bool InGPUWritable)
    : Name(InName)
    , SizeInBytes(InSizeInBytes)
    , RawData(nullptr)
    , MappedData(nullptr)
    , CurrentAccessMode(EBufferAccessMode::ReadWrite)
    , bInitialized(false)
    , bHasPendingCPUChanges(false)
    , bHasPendingGPUChanges(false)
    , UsageHint(EBufferUsage::General)
    , bGPUWritable(InGPUWritable)
    , Priority(EBufferPriority::Medium)
    , MappedZoneName(NAME_None)
{
    // Initialize version to 1
    VersionNumber.Set(1);
    
    // Initialize reference count to 1
    RefCount.Set(1);
}

FSharedBufferManager::~FSharedBufferManager()
{
    // Ensure we're shut down properly
    if (bInitialized)
    {
        Shutdown();
    }
}

bool FSharedBufferManager::Initialize()
{
    // Guard against multiple initialization
    if (bInitialized)
    {
        return true;
    }

    // Validate inputs
    if (SizeInBytes == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Cannot initialize buffer '%s' with zero size"), *Name.ToString());
        return false;
    }

    // Allocate memory with platform-specific alignment
    // Use a fixed value of 64 bytes for cache line alignment as a fallback
    const uint32 Alignment = 64;
    RawData = FMemory::Malloc(SizeInBytes, Alignment);
    
    if (!RawData)
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Failed to allocate %llu bytes for buffer '%s'"), 
            SizeInBytes, *Name.ToString());
        return false;
    }

    // Zero out the memory
    FMemory::Memzero(RawData, SizeInBytes);

    // Initialize cached statistics
    CachedStats.BufferName = Name;
    CachedStats.SizeInBytes = SizeInBytes;
    CachedStats.ReferenceCount = RefCount.GetValue();
    CachedStats.bIsMapped = false;
    CachedStats.bIsZeroCopy = false;
    CachedStats.bIsGPUWritable = bGPUWritable;
    CachedStats.VersionNumber = VersionNumber.GetValue();
    CachedStats.UsageHint = UsageHint;
    CachedStats.MapCount = 0;
    CachedStats.UnmapCount = 0;

    bInitialized = true;
    
    UE_LOG(LogTemp, Log, TEXT("SharedBufferManager: Initialized buffer '%s' (%llu bytes)"), *Name.ToString(), SizeInBytes);
    return true;
}

void FSharedBufferManager::Shutdown()
{
    // Guard against multiple shutdown
    if (!bInitialized)
    {
        // Already shut down
        return;
    }
    
    bInitialized = false;

    // Make sure buffer is unmapped
    if (MappedData)
    {
        UnmapBuffer();
    }

    // Wait for any pending GPU operations
    WaitForGPU();

    // Clear all zones
    {
        FScopeLock ZoneLocker(&ZoneLock);
        Zones.Empty();
    }

    // Free the memory
    if (RawData)
    {
        FMemory::Free(RawData);
        RawData = nullptr;
    }

    UE_LOG(LogTemp, Log, TEXT("SharedBufferManager: Shut down buffer '%s'"), *Name.ToString());
}

bool FSharedBufferManager::IsInitialized() const
{
    return bInitialized;
}

FName FSharedBufferManager::GetBufferName() const
{
    return Name;
}

uint64 FSharedBufferManager::GetSizeInBytes() const
{
    return SizeInBytes;
}

void* FSharedBufferManager::GetRawBuffer() const
{
    return RawData;
}

void* FSharedBufferManager::Map(EBufferAccessMode AccessMode)
{
    return MapBuffer(AccessMode);
}

bool FSharedBufferManager::Unmap()
{
    UnmapBuffer();
    return true;
}

bool FSharedBufferManager::IsMapped() const
{
    return IsBufferMapped();
}

void FSharedBufferManager::SyncToGPU()
{
    // Only sync if there are pending CPU changes
    if (bHasPendingCPUChanges)
    {
        FScopeLock Lock(&BufferLock);
        bHasPendingCPUChanges = false;
        SyncToGPUCount.Increment();
        
        // In a real implementation with a GPU resource, 
        // this would update the GPU buffer with CPU memory content
    }
}

void FSharedBufferManager::SyncFromGPU()
{
    // Only sync if there are pending GPU changes
    if (bHasPendingGPUChanges)
    {
        FScopeLock Lock(&BufferLock);
        bHasPendingGPUChanges = false;
        SyncFromGPUCount.Increment();
        
        // In a real implementation with a GPU resource,
        // this would update the CPU memory with GPU buffer content
    }
}

bool FSharedBufferManager::IsGPUBufferValid() const
{
    // SharedBufferManager doesn't maintain a GPU buffer directly
    // This would be handled by a graphics system that uses this buffer
    return bInitialized;
}

void FSharedBufferManager::AddRef()
{
    RefCount.Increment();
    CachedStats.ReferenceCount = RefCount.GetValue();
}

uint32 FSharedBufferManager::Release()
{
    int32 NewRefCount = RefCount.Decrement();
    CachedStats.ReferenceCount = NewRefCount;
    
    // If no more references, we could auto-delete or return to a pool
    if (NewRefCount <= 0)
    {
        UE_LOG(LogTemp, Verbose, TEXT("SharedBufferManager: Buffer '%s' reference count is now zero"), 
            *Name.ToString());
    }
    
    return FMath::Max(0, NewRefCount);
}

int32 FSharedBufferManager::GetRefCount() const
{
    return RefCount.GetValue();
}

uint64 FSharedBufferManager::GetVersionNumber() const
{
    return VersionNumber.GetValue();
}

void FSharedBufferManager::SetUsageHint(EBufferUsage InUsage)
{
    FScopeLock Lock(&BufferLock);
    UsageHint = InUsage;
    CachedStats.UsageHint = InUsage;
}

EBufferUsage FSharedBufferManager::GetUsageHint() const
{
    return UsageHint;
}

FBufferStats FSharedBufferManager::GetStats() const
{
    FScopeLock Lock(&BufferLock);
    
    // Update dynamic parts of the stats
    CachedStats.VersionNumber = VersionNumber.GetValue();
    CachedStats.ReferenceCount = RefCount.GetValue();
    CachedStats.bIsMapped = MappedData != nullptr;
    CachedStats.MapCount = MapCount.GetValue();
    CachedStats.UnmapCount = UnmapCount.GetValue();
    CachedStats.LastAccessMode = CurrentAccessMode;
    
    return CachedStats;
}

void FSharedBufferManager::BumpVersion()
{
    VersionNumber.Increment();
    CachedStats.VersionNumber = VersionNumber.GetValue();
}

void FSharedBufferManager::BumpZoneVersion(FName ZoneName)
{
    FScopeLock ZoneLocker(&ZoneLock);
    FBufferZone* Zone = FindZone(ZoneName);
    if (Zone)
    {
        // Increment both zone version and global buffer version
        Zone->VersionNumber++;
        BumpVersion();
    }
}

bool FSharedBufferManager::Resize(uint64 NewSizeInBytes, bool bPreserveContent)
{
    FScopeLock Lock(&BufferLock);
    
    if (!bInitialized)
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Cannot resize uninitialized buffer '%s'"), *Name.ToString());
        return false;
    }
    
    if (NewSizeInBytes == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Cannot resize buffer '%s' to zero size"), *Name.ToString());
        return false;
    }
    
    // Don't resize if already mapped
    if (IsMapped())
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Cannot resize mapped buffer '%s'"), *Name.ToString());
        return false;
    }
    
    // If size matches, no need to resize
    if (NewSizeInBytes == SizeInBytes)
    {
        return true;
    }
    
    // Check if we can't preserve zones
    if (bPreserveContent && (NewSizeInBytes < SizeInBytes) && !Zones.IsEmpty())
    {
        // Verify all zones still fit
        FScopeLock ZoneLocker(&ZoneLock);
        for (const auto& Pair : Zones)
        {
            const FBufferZone& Zone = Pair.Value;
            if (Zone.OffsetInBytes + Zone.SizeInBytes > NewSizeInBytes)
            {
                UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Cannot resize buffer '%s' to %llu bytes; zone '%s' would be out of bounds"),
                    *Name.ToString(), NewSizeInBytes, *Zone.ZoneName.ToString());
                return false;
            }
        }
    }
    
    // Allocate new buffer with proper alignment
    const uint32 Alignment = 64; // Use fixed value for cache line size
    void* NewRawData = FMemory::Malloc(NewSizeInBytes, Alignment);
    
    if (!NewRawData)
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Failed to allocate %llu bytes for resized buffer '%s'"), 
            NewSizeInBytes, *Name.ToString());
        return false;
    }
    
    // Copy data if requested
    if (bPreserveContent && RawData)
    {
        uint64 CopySize = FMath::Min(SizeInBytes, NewSizeInBytes);
        FMemory::Memcpy(NewRawData, RawData, CopySize);
        
        // Zero out any additional bytes
        if (NewSizeInBytes > SizeInBytes)
        {
            uint8* ExtraBytes = static_cast<uint8*>(NewRawData) + SizeInBytes;
            FMemory::Memzero(ExtraBytes, NewSizeInBytes - SizeInBytes);
        }
    }
    else
    {
        // Zero out the new buffer
        FMemory::Memzero(NewRawData, NewSizeInBytes);
    }
    
    // Free the old buffer
    if (RawData)
    {
        FMemory::Free(RawData);
    }
    
    // Update to use the new buffer
    RawData = NewRawData;
    SizeInBytes = NewSizeInBytes;
    CachedStats.SizeInBytes = SizeInBytes;
    
    // Increment version
    BumpVersion();
    
    // Mark that CPU changes are pending
    bHasPendingCPUChanges = true;
    
    UE_LOG(LogTemp, Log, TEXT("SharedBufferManager: Resized buffer '%s' to %llu bytes"), *Name.ToString(), SizeInBytes);
    return true;
}

bool FSharedBufferManager::SupportsZeroCopy() const
{
    return false; // SharedBufferManager doesn't support zero copy
}

bool FSharedBufferManager::IsGPUWritable() const
{
    return bGPUWritable;
}

void* FSharedBufferManager::GetGPUResource() const
{
    return nullptr; // SharedBufferManager doesn't maintain a GPU resource
}

bool FSharedBufferManager::Validate(TArray<FString>& OutErrors) const
{
    if (!bInitialized)
    {
        OutErrors.Add(FString::Printf(TEXT("Buffer '%s' is not initialized"), *Name.ToString()));
        return false;
    }
    
    if (!RawData)
    {
        OutErrors.Add(FString::Printf(TEXT("Buffer '%s' has null raw data pointer"), *Name.ToString()));
        return false;
    }
    
    if (SizeInBytes == 0)
    {
        OutErrors.Add(FString::Printf(TEXT("Buffer '%s' has zero size"), *Name.ToString()));
        return false;
    }
    
    if (IsMapped() && !MappedData)
    {
        OutErrors.Add(FString::Printf(TEXT("Buffer '%s' is marked as mapped but has null mapped pointer"), *Name.ToString()));
        return false;
    }
    
    // Validate zones
    {
        FScopeLock ZoneLocker(&ZoneLock);
        for (const auto& Pair : Zones)
        {
            const FBufferZone& Zone = Pair.Value;
            
            // Check zone bounds
            if (Zone.OffsetInBytes + Zone.SizeInBytes > SizeInBytes)
            {
                OutErrors.Add(FString::Printf(TEXT("Buffer '%s' zone '%s' extends beyond buffer bounds"), 
                    *Name.ToString(), *Zone.ZoneName.ToString()));
                return false;
            }
        }
    }
    
    return true;
}

void* FSharedBufferManager::MapBuffer(EBufferAccessMode AccessMode)
{
    FScopeLock Lock(&BufferLock);
    
    if (!bInitialized || !RawData)
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Cannot map uninitialized buffer '%s'"), *Name.ToString());
        return nullptr;
    }
    
    // If already mapped, just return the mapped data
    if (MappedData)
    {
        // Update access mode if more permissive
        if (AccessMode > CurrentAccessMode)
        {
            CurrentAccessMode = AccessMode;
        }
        
        return MappedData;
    }
    
    // Store access mode
    CurrentAccessMode = AccessMode;
    
    // Set mapped data pointer to the raw data
    MappedData = RawData;
    MappedZoneName = NAME_None; // Entire buffer is mapped
    
    // Track mapping operation
    MapCount.Increment();
    CachedStats.bIsMapped = true;
    CachedStats.LastAccessMode = AccessMode;
    
    // Record this access for telemetry
    RecordAccess(AccessMode);
    
    // If reading, make sure we have the latest data from GPU
    if (AccessMode == EBufferAccessMode::ReadOnly || AccessMode == EBufferAccessMode::ReadWrite)
    {
        if (bHasPendingGPUChanges)
        {
            SyncFromGPU();
        }
    }
    
    // Mark that CPU changes are pending if writable
    if (AccessMode == EBufferAccessMode::WriteOnly || AccessMode == EBufferAccessMode::ReadWrite)
    {
        bHasPendingCPUChanges = true;
    }
    
    return MappedData;
}

void FSharedBufferManager::UnmapBuffer()
{
    FScopeLock Lock(&BufferLock);
    
    if (!MappedData)
    {
        // Not mapped, nothing to do
        return;
    }
    
    // If mapped for writing, bump version
    if (CurrentAccessMode == EBufferAccessMode::WriteOnly || CurrentAccessMode == EBufferAccessMode::ReadWrite)
    {
        if (MappedZoneName == NAME_None)
        {
            // Entire buffer was mapped for writing, bump global version
            BumpVersion();
        }
        else
        {
            // Only a zone was mapped for writing, bump zone version
            BumpZoneVersion(MappedZoneName);
        }
    }
    
    // Clear mapped data
    MappedData = nullptr;
    MappedZoneName = NAME_None;
    
    // Track unmapping operation
    UnmapCount.Increment();
    CachedStats.bIsMapped = false;
}

bool FSharedBufferManager::IsBufferMapped() const
{
    return MappedData != nullptr;
}

bool FSharedBufferManager::Write(const void* Data, uint64 DataSize, uint64 OffsetInBytes)
{
    // Lock for thread safety
    FScopeLock Lock(&BufferLock);
    
    // Validate buffer is initialized
    if (!bInitialized || !RawData)
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Cannot write to uninitialized buffer '%s'"), *Name.ToString());
        return false;
    }
    
    // Validate input parameters
    if (!Data || DataSize == 0)
    {
        return false;
    }
    
    // Validate offset and size
    if (OffsetInBytes + DataSize > SizeInBytes)
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Write extends beyond buffer '%s' bounds"), *Name.ToString());
        return false;
    }
    
    // Calculate destination address
    uint8* Dest = static_cast<uint8*>(RawData) + OffsetInBytes;
    
    // Copy data
    FMemory::Memcpy(Dest, Data, DataSize);
    
    // Mark that CPU changes are pending
    bHasPendingCPUChanges = true;
    
    // Record write access for telemetry
    RecordAccess(EBufferAccessMode::WriteOnly);
    
    // Find affected zones
    {
        FScopeLock ZoneLocker(&ZoneLock);
        bool bFoundAffectedZone = false;
        
        for (auto& Pair : Zones)
        {
            FBufferZone& Zone = Pair.Value;
            uint64 ZoneEnd = Zone.OffsetInBytes + Zone.SizeInBytes;
            uint64 WriteEnd = OffsetInBytes + DataSize;
            
            // Check if write overlaps this zone
            if (OffsetInBytes < ZoneEnd && WriteEnd > Zone.OffsetInBytes)
            {
                // Write affects this zone, bump its version
                Zone.VersionNumber++;
                bFoundAffectedZone = true;
            }
        }
        
        // If we modified any zones, also bump global version
        if (bFoundAffectedZone)
        {
            BumpVersion();
        }
    }
    
    return true;
}

bool FSharedBufferManager::Read(void* OutData, uint64 DataSize, uint64 OffsetInBytes) const
{
    // Lock for thread safety
    FScopeLock Lock(&BufferLock);
    
    // Validate buffer is initialized
    if (!bInitialized || !RawData)
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Cannot read from uninitialized buffer '%s'"), *Name.ToString());
        return false;
    }
    
    // Validate input parameters
    if (!OutData || DataSize == 0)
    {
        return false;
    }
    
    // Validate offset and size
    if (OffsetInBytes + DataSize > SizeInBytes)
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Read extends beyond buffer '%s' bounds"), *Name.ToString());
        return false;
    }
    
    // Sync from GPU if needed to ensure we have most recent data
    if (bHasPendingGPUChanges)
    {
        const_cast<FSharedBufferManager*>(this)->SyncFromGPU();
    }
    
    // Calculate source address
    const uint8* Source = static_cast<const uint8*>(RawData) + OffsetInBytes;
    
    // Copy data
    FMemory::Memcpy(OutData, Source, DataSize);
    
    // Record read access for telemetry
    const_cast<FSharedBufferManager*>(this)->RecordAccess(EBufferAccessMode::ReadOnly);
    
    return true;
}

void FSharedBufferManager::SetPriority(EBufferPriority InPriority)
{
    FScopeLock Lock(&BufferLock);
    Priority = InPriority;
}

EBufferPriority FSharedBufferManager::GetPriority() const
{
    return Priority;
}

bool FSharedBufferManager::CreateZone(FName ZoneName, uint64 OffsetInBytes, uint64 ZoneSizeInBytes, EBufferPriority ZonePriority)
{
    if (!bInitialized)
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Cannot create zone in uninitialized buffer '%s'"), *Name.ToString());
        return false;
    }
    
    if (ZoneName == NAME_None)
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Cannot create zone with NAME_None in buffer '%s'"), *Name.ToString());
        return false;
    }
    
    // Validate bounds
    if (OffsetInBytes + ZoneSizeInBytes > SizeInBytes)
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Zone '%s' extends beyond buffer '%s' bounds"), 
            *ZoneName.ToString(), *Name.ToString());
        return false;
    }
    
    FScopeLock ZoneLocker(&ZoneLock);
    
    // Check if zone already exists
    if (Zones.Contains(ZoneName))
    {
        UE_LOG(LogTemp, Warning, TEXT("SharedBufferManager: Zone '%s' already exists in buffer '%s'"), 
            *ZoneName.ToString(), *Name.ToString());
        return false;
    }
    
    // Create the zone
    FBufferZone NewZone(ZoneName, OffsetInBytes, ZoneSizeInBytes, ZonePriority);
    Zones.Add(ZoneName, NewZone);
    
    UE_LOG(LogTemp, Verbose, TEXT("SharedBufferManager: Created zone '%s' in buffer '%s' (offset: %llu, size: %llu)"), 
        *ZoneName.ToString(), *Name.ToString(), OffsetInBytes, ZoneSizeInBytes);
    
    return true;
}

bool FSharedBufferManager::RemoveZone(FName ZoneName)
{
    if (!bInitialized)
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Cannot remove zone from uninitialized buffer '%s'"), *Name.ToString());
        return false;
    }
    
    FScopeLock ZoneLocker(&ZoneLock);
    
    // Check if the zone is mapped
    if (MappedZoneName == ZoneName)
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Cannot remove mapped zone '%s' from buffer '%s'"), 
            *ZoneName.ToString(), *Name.ToString());
        return false;
    }
    
    // Remove the zone
    int32 NumRemoved = Zones.Remove(ZoneName);
    
    if (NumRemoved == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("SharedBufferManager: Zone '%s' not found in buffer '%s'"), 
            *ZoneName.ToString(), *Name.ToString());
        return false;
    }
    
    UE_LOG(LogTemp, Verbose, TEXT("SharedBufferManager: Removed zone '%s' from buffer '%s'"), 
        *ZoneName.ToString(), *Name.ToString());
    
    return true;
}

void* FSharedBufferManager::MapZone(FName ZoneName, EBufferAccessMode AccessMode)
{
    if (!bInitialized || !RawData)
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Cannot map zone in uninitialized buffer '%s'"), *Name.ToString());
        return nullptr;
    }
    
    // If the entire buffer is already mapped, return error
    if (MappedData && MappedZoneName == NAME_None)
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Cannot map zone '%s' in buffer '%s' because entire buffer is already mapped"), 
            *ZoneName.ToString(), *Name.ToString());
        return nullptr;
    }
    
    // If any zone is already mapped, return error
    if (MappedData)
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Cannot map zone '%s' in buffer '%s' because zone '%s' is already mapped"), 
            *ZoneName.ToString(), *Name.ToString(), *MappedZoneName.ToString());
        return nullptr;
    }
    
    // Find the zone
    FScopeLock ZoneLocker(&ZoneLock);
    FBufferZone* Zone = FindZone(ZoneName);
    
    if (!Zone)
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Zone '%s' not found in buffer '%s'"), 
            *ZoneName.ToString(), *Name.ToString());
        return nullptr;
    }
    
    // Check if the zone is already locked
    if (Zone->bIsLocked)
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Zone '%s' in buffer '%s' is already locked"), 
            *ZoneName.ToString(), *Name.ToString());
        return nullptr;
    }
    
    // Lock the zone
    Zone->bIsLocked = true;
    
    // Calculate zone address
    uint8* ZoneAddress = static_cast<uint8*>(RawData) + Zone->OffsetInBytes;
    
    // If reading, make sure we have the latest data from GPU
    if (AccessMode == EBufferAccessMode::ReadOnly || AccessMode == EBufferAccessMode::ReadWrite)
    {
        if (bHasPendingGPUChanges)
        {
            SyncFromGPU();
        }
    }
    
    // Store mapping information
    MappedData = ZoneAddress;
    MappedZoneName = ZoneName;
    CurrentAccessMode = AccessMode;
    
    // Track mapping operation
    MapCount.Increment();
    CachedStats.bIsMapped = true;
    CachedStats.LastAccessMode = AccessMode;
    
    // Record this access for telemetry
    RecordAccess(AccessMode);
    
    // If writing, mark that CPU changes will be pending
    if (AccessMode == EBufferAccessMode::WriteOnly || AccessMode == EBufferAccessMode::ReadWrite)
    {
        bHasPendingCPUChanges = true;
    }
    
    return MappedData;
}

bool FSharedBufferManager::UnmapZone(FName ZoneName)
{
    if (!bInitialized)
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Cannot unmap zone from uninitialized buffer '%s'"), *Name.ToString());
        return false;
    }
    
    // If nothing is mapped, nothing to do
    if (!MappedData)
    {
        return true;
    }
    
    // Check if the right zone is mapped
    if (MappedZoneName != ZoneName)
    {
        if (MappedZoneName == NAME_None)
        {
            UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Cannot unmap zone '%s' from buffer '%s' because entire buffer is mapped"), 
                *ZoneName.ToString(), *Name.ToString());
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Cannot unmap zone '%s' from buffer '%s' because zone '%s' is mapped"), 
                *ZoneName.ToString(), *Name.ToString(), *MappedZoneName.ToString());
        }
        return false;
    }
    
    // Find the zone
    FScopeLock ZoneLocker(&ZoneLock);
    FBufferZone* Zone = FindZone(ZoneName);
    
    if (!Zone)
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Zone '%s' not found in buffer '%s' during unmap"), 
            *ZoneName.ToString(), *Name.ToString());
        return false;
    }
    
    // If mapped for writing, bump zone version
    if (CurrentAccessMode == EBufferAccessMode::WriteOnly || CurrentAccessMode == EBufferAccessMode::ReadWrite)
    {
        Zone->VersionNumber++;
        BumpVersion();
    }
    
    // Unlock the zone
    Zone->bIsLocked = false;
    
    // Clear mapped data
    MappedData = nullptr;
    MappedZoneName = NAME_None;
    
    // Track unmapping operation
    UnmapCount.Increment();
    CachedStats.bIsMapped = false;
    
    return true;
}

bool FSharedBufferManager::IsZoneMapped(FName ZoneName) const
{
    return MappedZoneName == ZoneName && MappedData != nullptr;
}

uint64 FSharedBufferManager::GetZoneVersion(FName ZoneName) const
{
    FScopeLock ZoneLocker(&ZoneLock);
    const FBufferZone* Zone = FindZone(ZoneName);
    return Zone ? Zone->VersionNumber : 0;
}

void* FSharedBufferManager::GetZoneBuffer(FName ZoneName) const
{
    if (!bInitialized || !RawData)
    {
        return nullptr;
    }
    
    FScopeLock ZoneLocker(&ZoneLock);
    const FBufferZone* Zone = FindZone(ZoneName);
    
    if (!Zone)
    {
        return nullptr;
    }
    
    return static_cast<uint8*>(RawData) + Zone->OffsetInBytes;
}

uint64 FSharedBufferManager::GetZoneSize(FName ZoneName) const
{
    FScopeLock ZoneLocker(&ZoneLock);
    const FBufferZone* Zone = FindZone(ZoneName);
    return Zone ? Zone->SizeInBytes : 0;
}

void FSharedBufferManager::WaitForGPU()
{
    // In a real implementation, this would wait for any pending
    // GPU operations on this buffer to complete
    
    // For now, just mark that there are no pending GPU changes
    bHasPendingGPUChanges = false;
}

void FSharedBufferManager::Synchronize(bool bToGPU)
{
    if (bToGPU)
    {
        SyncToGPU();
    }
    else
    {
        SyncFromGPU();
    }
}

bool FSharedBufferManager::SynchronizeZone(FName ZoneName, bool bToGPU)
{
    if (!bInitialized)
    {
        return false;
    }
    
    FScopeLock ZoneLocker(&ZoneLock);
    FBufferZone* Zone = FindZone(ZoneName);
    
    if (!Zone)
    {
        return false;
    }
    
    // In a real implementation, we would only sync the specific zone's memory
    // For now, we just update the flags
    
    if (bToGPU)
    {
        bHasPendingCPUChanges = false;
        SyncToGPUCount.Increment();
    }
    else
    {
        bHasPendingGPUChanges = false;
        SyncFromGPUCount.Increment();
    }
    
    return true;
}

FBufferZone* FSharedBufferManager::FindZone(FName ZoneName)
{
    return Zones.Find(ZoneName);
}

const FBufferZone* FSharedBufferManager::FindZone(FName ZoneName) const
{
    return Zones.Find(ZoneName);
}

void FSharedBufferManager::RecordAccess(EBufferAccessMode AccessMode)
{
    // Create a record of this access
    FBufferAccessRecord Record(
        FPlatformTime::Seconds(),
        FPlatformTLS::GetCurrentThreadId(),
        AccessMode
    );
    
    // Add to history
    AccessHistory.Enqueue(Record);
    
    // TQueue doesn't have a Num() function, so we need to track size manually
    // We'll keep a static counter to limit the queue size
    static int32 QueueSize = 0;
    QueueSize++;
    
    // Keep the history at a reasonable size
    const int32 MaxHistorySize = 16;
    if (QueueSize > MaxHistorySize)
    {
        FBufferAccessRecord Unused;
        if (AccessHistory.Dequeue(Unused))
        {
            QueueSize--;
        }
    }
}

TSharedPtr<FSharedBufferManager> FSharedBufferManager::CreateTypedBuffer(
    const FName& TypeName,
    uint32 TypeId,
    uint32 DataSize,
    uint32 AlignmentRequirement,
    bool bSupportsGPU,
    uint32 MemoryLayout,
    uint32 FieldCapabilities,
    uint64 ElementCount)
{
    // Create a buffer name based on the field type
    FName BufferName = FName(*FString::Printf(TEXT("TypedBuffer_%s_%u"), *TypeName.ToString(), TypeId));
    
    // Calculate the total size needed for the buffer
    // Ensure proper alignment by rounding up to the required alignment
    uint64 ElementSizeAligned = ((DataSize + AlignmentRequirement - 1) / AlignmentRequirement) * AlignmentRequirement;
    uint64 TotalSize = ElementSizeAligned * ElementCount;
    
    // Create the shared buffer manager with the appropriate settings
    TSharedPtr<FSharedBufferManager> Buffer = MakeShared<FSharedBufferManager>(BufferName, TotalSize, bSupportsGPU);
    
    // Initialize the buffer
    if (!Buffer->Initialize())
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Failed to initialize typed buffer for type '%s'"), *TypeName.ToString());
        return nullptr;
    }
    
    // Set buffer usage hint based on memory layout
    // This uses the passed memory layout parameter (from ESDFMemoryLayout or similar)
    switch (MemoryLayout)
    {
        case 0: // Sequential layout (assumed to be 0)
            Buffer->SetUsageHint(EBufferUsage::Sequential);
            break;
        case 1: // Interleaved layout (assumed to be 1)
            Buffer->SetUsageHint(EBufferUsage::Random);
            break;
        default:
            Buffer->SetUsageHint(EBufferUsage::General);
            break;
    }
    
    // Create a primary zone for the entire buffer
    Buffer->CreateZone(FName(TEXT("PrimaryZone")), 0, TotalSize, EBufferPriority::Medium);
    
    // Set up memory barriers based on capabilities
    // For now, just log the setup - a real implementation would set up appropriate memory barriers
    UE_LOG(LogTemp, Log, TEXT("SharedBufferManager: Created typed buffer for type '%s' with %llu elements (%llu bytes, alignment %u)"),
        *TypeName.ToString(), ElementCount, TotalSize, AlignmentRequirement);
    
    return Buffer;
}
