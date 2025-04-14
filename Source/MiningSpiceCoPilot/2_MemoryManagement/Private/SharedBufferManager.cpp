// Copyright Epic Games, Inc. All Rights Reserved.

#include "SharedBufferManager.h"
#include "HAL/PlatformMemory.h"
#include "Misc/ScopeLock.h"
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
    // Use a fallback value of 64 bytes if CacheLineSize isn't available
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
    // In a real implementation, this would upload buffer content to GPU
    // For this example, we just mark that there are no pending CPU changes
    
    // Only sync if there are pending CPU changes
    if (bHasPendingCPUChanges)
    {
        FScopeLock Lock(&BufferLock);
        bHasPendingCPUChanges = false;
        SyncToGPUCount.Increment();
    }
}

void FSharedBufferManager::SyncFromGPU()
{
    // In a real implementation, this would download buffer content from GPU
    // For this example, we just mark that there are no pending GPU changes
    
    // Only sync if there are pending GPU changes
    if (bHasPendingGPUChanges)
    {
        FScopeLock Lock(&BufferLock);
        bHasPendingGPUChanges = false;
        SyncFromGPUCount.Increment();
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
    
    return CachedStats;
}

void FSharedBufferManager::BumpVersion()
{
    VersionNumber.Increment();
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
    
    // Allocate new buffer
    const uint32 Alignment = FPlatformMemory::GetConstants().CacheLineSize;
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
    
    // Track mapping operation
    MapCount.Increment();
    CachedStats.bIsMapped = true;
    
    // Mark that CPU changes are pending if writable
    if (AccessMode == EBufferAccessMode::ReadWrite || AccessMode == EBufferAccessMode::Write)
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
    
    // Clear mapped data
    MappedData = nullptr;
    
    // Track unmapping operation
    UnmapCount.Increment();
    CachedStats.bIsMapped = false;
}

bool FSharedBufferManager::IsBufferMapped() const
{
    return MappedData != nullptr;
}
