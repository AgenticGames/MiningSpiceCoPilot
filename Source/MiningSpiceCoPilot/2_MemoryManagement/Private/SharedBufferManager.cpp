// Copyright Epic Games, Inc. All Rights Reserved.

#include "2_MemoryManagement/Public/SharedBufferManager.h"
#include "2_MemoryManagement/Public/ZeroCopyBuffer.h"
#include "2_MemoryManagement/Public/Interfaces/IMemoryManager.h"
#include "2_MemoryManagement/Public/Interfaces/IMemoryTracker.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMemory.h"
#include "Misc/ScopeLock.h"
#include "Containers/Map.h"
#include "Engine/Engine.h"

FSharedBufferManager::FSharedBufferManager(IMemoryManager* InMemoryManager)
    : MemoryManager(InMemoryManager)
    , bIsInitialized(false)
{
}

FSharedBufferManager::~FSharedBufferManager()
{
    Shutdown();
}

bool FSharedBufferManager::Initialize()
{
    if (bIsInitialized)
    {
        return true;
    }

    // Initialize buffer tracking containers
    BufferMap.Empty();
    BufferVersions.Empty();
    BufferOwnership.Empty();
    
    bIsInitialized = true;

    UE_LOG(LogTemp, Log, TEXT("SharedBufferManager: Initialized"));
    return true;
}

void FSharedBufferManager::Shutdown()
{
    if (!bIsInitialized)
    {
        return;
    }

    FScopeLock Lock(&BufferLock);

    // Release all managed buffers
    for (auto& Pair : BufferMap)
    {
        if (Pair.Value.IsValid())
        {
            Pair.Value->Shutdown();
        }
    }

    BufferMap.Empty();
    BufferVersions.Empty();
    BufferOwnership.Empty();
    bIsInitialized = false;

    UE_LOG(LogTemp, Log, TEXT("SharedBufferManager: Shutdown complete"));
}

bool FSharedBufferManager::IsInitialized() const
{
    return bIsInitialized;
}

IBufferProvider* FSharedBufferManager::CreateSharedBuffer(
    const FName& BufferName,
    uint64 SizeInBytes,
    EBufferUsage Usage,
    bool bGPUWritable)
{
    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Cannot create buffer '%s', manager not initialized"), *BufferName.ToString());
        return nullptr;
    }

    FScopeLock Lock(&BufferLock);

    // Check if buffer already exists
    if (BufferMap.Contains(BufferName))
    {
        UE_LOG(LogTemp, Warning, TEXT("SharedBufferManager: Buffer '%s' already exists, returning existing buffer"), *BufferName.ToString());
        return BufferMap[BufferName].Get();
    }

    // Create the appropriate buffer type based on usage
    TSharedPtr<IBufferProvider> Buffer;
    switch (Usage)
    {
        case EBufferUsage::ZeroCopy:
            Buffer = MakeShared<FZeroCopyBuffer>(BufferName, SizeInBytes, bGPUWritable);
            break;
        
        case EBufferUsage::StandardShared:
        default:
            // Standard buffer implementation
            Buffer = MakeShared<FZeroCopyBuffer>(BufferName, SizeInBytes, bGPUWritable);
            break;
    }

    // Initialize the buffer
    if (!Buffer->Initialize())
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Failed to initialize buffer '%s'"), *BufferName.ToString());
        return nullptr;
    }

    // Add to tracking maps
    BufferMap.Add(BufferName, Buffer);
    BufferVersions.Add(BufferName, 0);
    BufferOwnership.Add(BufferName, TPair<EBufferOwner, EBufferOwner>(EBufferOwner::None, EBufferOwner::None));

    // Track buffer creation if telemetry is available
    if (MemoryManager && MemoryManager->GetMemoryTracker())
    {
        MemoryManager->GetMemoryTracker()->TrackAllocation(
            Buffer->GetRawBuffer(),
            SizeInBytes,
            BufferName,
            FName("SharedBuffer"));
    }

    UE_LOG(LogTemp, Log, TEXT("SharedBufferManager: Created buffer '%s' with size %llu bytes"), *BufferName.ToString(), SizeInBytes);
    return Buffer.Get();
}

bool FSharedBufferManager::DestroySharedBuffer(const FName& BufferName)
{
    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Cannot destroy buffer '%s', manager not initialized"), *BufferName.ToString());
        return false;
    }

    FScopeLock Lock(&BufferLock);

    if (!BufferMap.Contains(BufferName))
    {
        UE_LOG(LogTemp, Warning, TEXT("SharedBufferManager: Buffer '%s' does not exist"), *BufferName.ToString());
        return false;
    }

    // Get buffer before removing it
    TSharedPtr<IBufferProvider> Buffer = BufferMap[BufferName];

    // Track buffer destruction if telemetry is available
    if (MemoryManager && MemoryManager->GetMemoryTracker())
    {
        MemoryManager->GetMemoryTracker()->TrackDeallocation(Buffer->GetRawBuffer());
    }

    // Shutdown the buffer
    Buffer->Shutdown();

    // Remove from tracking maps
    BufferMap.Remove(BufferName);
    BufferVersions.Remove(BufferName);
    BufferOwnership.Remove(BufferName);

    UE_LOG(LogTemp, Log, TEXT("SharedBufferManager: Destroyed buffer '%s'"), *BufferName.ToString());
    return true;
}

IBufferProvider* FSharedBufferManager::GetSharedBuffer(const FName& BufferName)
{
    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Cannot get buffer '%s', manager not initialized"), *BufferName.ToString());
        return nullptr;
    }

    FScopeLock Lock(&BufferLock);

    if (!BufferMap.Contains(BufferName))
    {
        UE_LOG(LogTemp, Warning, TEXT("SharedBufferManager: Buffer '%s' does not exist"), *BufferName.ToString());
        return nullptr;
    }

    return BufferMap[BufferName].Get();
}

bool FSharedBufferManager::ResizeSharedBuffer(const FName& BufferName, uint64 NewSizeInBytes)
{
    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Cannot resize buffer '%s', manager not initialized"), *BufferName.ToString());
        return false;
    }

    FScopeLock Lock(&BufferLock);

    if (!BufferMap.Contains(BufferName))
    {
        UE_LOG(LogTemp, Warning, TEXT("SharedBufferManager: Buffer '%s' does not exist"), *BufferName.ToString());
        return false;
    }

    TSharedPtr<IBufferProvider> Buffer = BufferMap[BufferName];
    uint64 OldSize = Buffer->GetBufferSize();

    // Resize the buffer
    bool bSuccess = Buffer->ResizeBuffer(NewSizeInBytes);
    if (!bSuccess)
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Failed to resize buffer '%s' to %llu bytes"), *BufferName.ToString(), NewSizeInBytes);
        return false;
    }

    // Track buffer resize if telemetry is available
    if (MemoryManager && MemoryManager->GetMemoryTracker())
    {
        void* OldPtr = Buffer->GetRawBuffer(); // This is already the new pointer, but needed for API
        void* NewPtr = Buffer->GetRawBuffer();
        MemoryManager->GetMemoryTracker()->TrackResize(OldPtr, NewPtr, NewSizeInBytes);
    }

    UE_LOG(LogTemp, Log, TEXT("SharedBufferManager: Resized buffer '%s' from %llu to %llu bytes"), 
        *BufferName.ToString(), OldSize, NewSizeInBytes);
    
    // Increment the version to indicate buffer has changed
    IncrementBufferVersion(BufferName);
    
    return true;
}

bool FSharedBufferManager::AcquireBufferOwnership(
    const FName& BufferName, 
    EBufferOwner RequestingOwner, 
    EBufferAccessMode AccessMode,
    bool bWaitForRelease)
{
    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Cannot acquire buffer '%s', manager not initialized"), *BufferName.ToString());
        return false;
    }

    FScopeLock Lock(&BufferLock);

    if (!BufferMap.Contains(BufferName))
    {
        UE_LOG(LogTemp, Warning, TEXT("SharedBufferManager: Buffer '%s' does not exist"), *BufferName.ToString());
        return false;
    }

    // Get current ownership
    TPair<EBufferOwner, EBufferOwner>& Ownership = BufferOwnership[BufferName];
    
    // Check if ownership can be granted
    bool bCanAcquire = false;
    
    switch (AccessMode)
    {
        case EBufferAccessMode::ReadOnly:
            // Read-only access can be shared
            bCanAcquire = (Ownership.Key == EBufferOwner::None || Ownership.Value == EBufferOwner::None ||
                          Ownership.Value == RequestingOwner || 
                          (Ownership.Key != EBufferOwner::None && Ownership.Value == EBufferOwner::None));
            break;
            
        case EBufferAccessMode::ReadWrite:
            // Read-write access requires exclusive ownership
            bCanAcquire = (Ownership.Key == EBufferOwner::None && Ownership.Value == EBufferOwner::None);
            break;
    }
    
    // If we can't acquire now but need to wait
    if (!bCanAcquire && bWaitForRelease)
    {
        // In a real implementation, this would use a condition variable or semaphore
        // For simplicity, we'll use a simple polling approach
        const float MaxWaitTimeSeconds = 5.0f;
        const float SleepIntervalSeconds = 0.01f;
        float ElapsedTime = 0.0f;
        
        Lock.Unlock(); // Release lock during wait
        
        while (ElapsedTime < MaxWaitTimeSeconds)
        {
            FPlatformProcess::Sleep(SleepIntervalSeconds);
            ElapsedTime += SleepIntervalSeconds;
            
            Lock.Lock();
            
            // Re-check ownership
            Ownership = BufferOwnership[BufferName];
            switch (AccessMode)
            {
                case EBufferAccessMode::ReadOnly:
                    bCanAcquire = (Ownership.Key == EBufferOwner::None || Ownership.Value == EBufferOwner::None ||
                                  Ownership.Value == RequestingOwner || 
                                  (Ownership.Key != EBufferOwner::None && Ownership.Value == EBufferOwner::None));
                    break;
                    
                case EBufferAccessMode::ReadWrite:
                    bCanAcquire = (Ownership.Key == EBufferOwner::None && Ownership.Value == EBufferOwner::None);
                    break;
            }
            
            if (bCanAcquire)
            {
                break;
            }
            
            Lock.Unlock();
        }
        
        if (!Lock.IsLocked())
        {
            Lock.Lock();
        }
        
        if (!bCanAcquire)
        {
            UE_LOG(LogTemp, Warning, TEXT("SharedBufferManager: Timed out waiting for buffer '%s' ownership"), 
                *BufferName.ToString());
            return false;
        }
    }
    
    // If we can't acquire, fail
    if (!bCanAcquire)
    {
        UE_LOG(LogTemp, Warning, TEXT("SharedBufferManager: Cannot acquire buffer '%s' ownership, currently owned by %d/%d"), 
            *BufferName.ToString(), (int)Ownership.Key, (int)Ownership.Value);
        return false;
    }
    
    // Update ownership based on access mode
    switch (AccessMode)
    {
        case EBufferAccessMode::ReadOnly:
            if (Ownership.Key == EBufferOwner::None)
            {
                Ownership.Key = RequestingOwner;
            }
            else if (Ownership.Value == EBufferOwner::None)
            {
                Ownership.Value = RequestingOwner;
            }
            break;
            
        case EBufferAccessMode::ReadWrite:
            // Exclusive ownership
            Ownership.Key = RequestingOwner;
            Ownership.Value = RequestingOwner;
            break;
    }
    
    return true;
}

bool FSharedBufferManager::ReleaseBufferOwnership(const FName& BufferName, EBufferOwner ReleasingOwner)
{
    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Cannot release buffer '%s', manager not initialized"), *BufferName.ToString());
        return false;
    }

    FScopeLock Lock(&BufferLock);

    if (!BufferMap.Contains(BufferName))
    {
        UE_LOG(LogTemp, Warning, TEXT("SharedBufferManager: Buffer '%s' does not exist"), *BufferName.ToString());
        return false;
    }
    
    // Get current ownership
    TPair<EBufferOwner, EBufferOwner>& Ownership = BufferOwnership[BufferName];
    
    // Check if the releasing owner actually owns the buffer
    if (Ownership.Key != ReleasingOwner && Ownership.Value != ReleasingOwner)
    {
        UE_LOG(LogTemp, Warning, TEXT("SharedBufferManager: Buffer '%s' not owned by %d, current owners: %d/%d"), 
            *BufferName.ToString(), (int)ReleasingOwner, (int)Ownership.Key, (int)Ownership.Value);
        return false;
    }
    
    // Release ownership
    if (Ownership.Key == ReleasingOwner)
    {
        Ownership.Key = EBufferOwner::None;
    }
    
    if (Ownership.Value == ReleasingOwner)
    {
        Ownership.Value = EBufferOwner::None;
    }
    
    return true;
}

uint32 FSharedBufferManager::GetBufferVersion(const FName& BufferName) const
{
    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Cannot get buffer version '%s', manager not initialized"), *BufferName.ToString());
        return 0;
    }

    FScopeLock Lock(&BufferLock);

    if (!BufferVersions.Contains(BufferName))
    {
        UE_LOG(LogTemp, Warning, TEXT("SharedBufferManager: Buffer '%s' does not exist"), *BufferName.ToString());
        return 0;
    }

    return BufferVersions[BufferName];
}

void FSharedBufferManager::IncrementBufferVersion(const FName& BufferName)
{
    if (!bIsInitialized || !BufferVersions.Contains(BufferName))
    {
        return;
    }

    BufferVersions[BufferName]++;
    UE_LOG(LogTemp, Verbose, TEXT("SharedBufferManager: Buffer '%s' version incremented to %d"), 
        *BufferName.ToString(), BufferVersions[BufferName]);
}

bool FSharedBufferManager::MapBuffer(
    const FName& BufferName,
    void*& OutData,
    EBufferAccessFlags AccessFlags,
    uint32& OutVersion)
{
    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Cannot map buffer '%s', manager not initialized"), *BufferName.ToString());
        return false;
    }

    IBufferProvider* Buffer = GetSharedBuffer(BufferName);
    if (!Buffer)
    {
        return false;
    }

    OutData = Buffer->MapBuffer(AccessFlags);
    if (!OutData)
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Failed to map buffer '%s'"), *BufferName.ToString());
        return false;
    }

    FScopeLock Lock(&BufferLock);
    OutVersion = BufferVersions[BufferName];
    
    return true;
}

bool FSharedBufferManager::UnmapBuffer(const FName& BufferName, bool bVersionIncrement)
{
    if (!bIsInitialized)
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Cannot unmap buffer '%s', manager not initialized"), *BufferName.ToString());
        return false;
    }

    IBufferProvider* Buffer = GetSharedBuffer(BufferName);
    if (!Buffer)
    {
        return false;
    }

    bool bResult = Buffer->UnmapBuffer();
    if (!bResult)
    {
        UE_LOG(LogTemp, Error, TEXT("SharedBufferManager: Failed to unmap buffer '%s'"), *BufferName.ToString());
        return false;
    }

    if (bVersionIncrement)
    {
        FScopeLock Lock(&BufferLock);
        IncrementBufferVersion(BufferName);
    }
    
    return true;
}

TArray<FName> FSharedBufferManager::GetBufferNames() const
{
    TArray<FName> Results;
    
    if (!bIsInitialized)
    {
        return Results;
    }
    
    FScopeLock Lock(&BufferLock);
    BufferMap.GetKeys(Results);
    
    return Results;
}

void FSharedBufferManager::GetBufferStats(TMap<FName, FBufferStats>& OutStats) const
{
    OutStats.Empty();
    
    if (!bIsInitialized)
    {
        return;
    }
    
    FScopeLock Lock(&BufferLock);
    
    for (const auto& Pair : BufferMap)
    {
        if (Pair.Value.IsValid())
        {
            OutStats.Add(Pair.Key, Pair.Value->GetStats());
        }
    }
}

EBufferOwner FSharedBufferManager::GetBufferOwner(const FName& BufferName, bool bSecondaryOwner) const
{
    if (!bIsInitialized || !BufferOwnership.Contains(BufferName))
    {
        return EBufferOwner::None;
    }
    
    FScopeLock Lock(&BufferLock);
    const TPair<EBufferOwner, EBufferOwner>& Ownership = BufferOwnership[BufferName];
    
    return bSecondaryOwner ? Ownership.Value : Ownership.Key;
}
