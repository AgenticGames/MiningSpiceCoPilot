// Copyright Epic Games, Inc. All Rights Reserved.

#include "2_MemoryManagement/Public/SharedBufferManager.h"
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
    bool bWasInitialized = false;
    if (bInitialized.AtomicSet(true, bWasInitialized))
    {
        // Already initialized
        return true;
    }

    // Validate inputs
    if (SizeInBytes == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Cannot initialize buffer '%s' with zero size"), *Name.ToString());
        bInitialized = false;
        return false;
    }

    // Allocate memory aligned to cache line size for optimal performance
    const uint32 Alignment = FPlatformMemory::GetConstants().CacheLineSize;
    RawData = FMemory::Malloc(SizeInBytes, Alignment);
    
    if (!RawData)
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Failed to allocate %llu bytes for buffer '%s'"), 
            SizeInBytes, *Name.ToString());
        bInitialized = false;
        return false;
    }

    // Zero out the memory
    FMemory::Memzero(RawData, SizeInBytes);

    // Initialize cached statistics
    CachedStats.BufferName = Name;
    CachedStats.SizeInBytes = SizeInBytes;
    CachedStats.ReferenceCount = RefCount.GetValue();
    CachedStats.bIsMapped = false;
    CachedStats.bIsZeroCopy = false; // SharedBufferManager doesn't use zero-copy (that's handled by ZeroCopyBuffer)
    CachedStats.bIsGPUWritable = bGPUWritable;
    CachedStats.VersionNumber = VersionNumber.GetValue();
    CachedStats.UsageHint = UsageHint;

    UE_LOG(LogTemp, Log, TEXT("SharedBufferManager: Initialized buffer '%s' (%llu bytes)"), *Name.ToString(), SizeInBytes);
    return true;
}

void FSharedBufferManager::Shutdown()
{
    // Guard against multiple shutdown
    bool bWasInitialized = true;
    if (!bInitialized.AtomicSet(false, bWasInitialized) || !bWasInitialized)
    {
        // Already shut down
        return;
    }

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

FName FSharedBufferManager::GetName() const
{
    return Name;
}

uint64 FSharedBufferManager::GetBufferSize() const
{
    return SizeInBytes;
}

void* FSharedBufferManager::GetRawBuffer() const
{
    return RawData;
}

void* FSharedBufferManager::MapBuffer(EBufferAccessMode AccessMode)
{
    FScopeLock Lock(&BufferLock);
    
    // Check if initialized
    if (!bInitialized || !RawData)
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Cannot map uninitialized buffer '%s'"), *Name.ToString());
        return nullptr;
    }

    // If already mapped, return existing mapping
    if (MappedData)
    {
        return MappedData;
    }

    // If we're reading from the buffer and there are pending GPU changes, sync from GPU first
    if ((AccessMode == EBufferAccessMode::ReadOnly || AccessMode == EBufferAccessMode::ReadWrite) &&
        bHasPendingGPUChanges)
    {
        SyncFromGPU();
    }

    // Record mapping
    CurrentAccessMode = AccessMode;
    MappedData = RawData;
    MapCount.Increment();
    CachedStats.bIsMapped = true;
    CachedStats.LastAccessMode = AccessMode;

    return MappedData;
}

void FSharedBufferManager::UnmapBuffer()
{
    FScopeLock Lock(&BufferLock);
    
    // Check if we have an active mapping
    if (!MappedData)
    {
        return;
    }

    // If buffer was mapped for writing, mark as having pending CPU changes
    if (CurrentAccessMode == EBufferAccessMode::WriteOnly || CurrentAccessMode == EBufferAccessMode::ReadWrite)
    {
        bHasPendingCPUChanges = true;
        
        // Increment version number to indicate content has changed
        BumpVersion();
    }

    // Clear mapping
    MappedData = nullptr;
    UnmapCount.Increment();
    CachedStats.bIsMapped = false;
}

bool FSharedBufferManager::IsBufferMapped() const
{
    return MappedData != nullptr;
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

int32 FSharedBufferManager::AddRef()
{
    int32 NewRefCount = RefCount.Increment();
    CachedStats.ReferenceCount = NewRefCount;
    return NewRefCount;
}

int32 FSharedBufferManager::Release()
{
    int32 NewRefCount = RefCount.Decrement();
    CachedStats.ReferenceCount = NewRefCount;
    
    // If no more references, we could auto-delete or return to a pool
    if (NewRefCount <= 0)
    {
        UE_LOG(LogTemp, Verbose, TEXT("SharedBufferManager: Buffer '%s' reference count is now zero"), 
            *Name.ToString());
    }
    
    return NewRefCount;
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
