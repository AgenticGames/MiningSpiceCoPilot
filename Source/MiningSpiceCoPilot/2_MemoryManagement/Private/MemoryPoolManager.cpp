// Copyright Epic Games, Inc. All Rights Reserved.

#include "MemoryPoolManager.h"
#include "SVOAllocator.h"
#include "MemoryNarrowBandAllocator.h"
#include "SharedBufferManager.h"
#include "ZeroCopyBuffer.h"
#include "MemoryTelemetry.h"
#include "MemoryDefragmenter.h"
#include "CompressionUtility.h"
#include "HAL/PlatformMemory.h"
#include "HAL/LowLevelMemTracker.h"
#include "HAL/MallocBinned.h"
#include "HAL/MallocBinned2.h"
#include "HAL/MallocAnsi.h"
#include "Misc/CoreDelegates.h"
#include "Templates/UniquePtr.h"

// Initialize static members
FMemoryPoolManager* FMemoryPoolManager::ManagerInstance = nullptr;
FCriticalSection FMemoryPoolManager::SingletonLock;

// Memory category definitions for tracking
static const FName CATEGORY_SVO_NODES(TEXT("SVONodes"));
static const FName CATEGORY_SDF_FIELDS(TEXT("SDFFields"));
static const FName CATEGORY_NARROW_BAND(TEXT("NarrowBand"));
static const FName CATEGORY_MATERIAL_CHANNELS(TEXT("MaterialChannels"));
static const FName CATEGORY_MESH_DATA(TEXT("MeshData"));
static const FName CATEGORY_GENERAL(TEXT("General"));

// Default memory budgets - these can be adjusted through configuration
static constexpr uint64 DEFAULT_BUDGET_SVO_NODES = 256 * 1024 * 1024;       // 256 MB
static constexpr uint64 DEFAULT_BUDGET_SDF_FIELDS = 512 * 1024 * 1024;      // 512 MB
static constexpr uint64 DEFAULT_BUDGET_NARROW_BAND = 128 * 1024 * 1024;     // 128 MB
static constexpr uint64 DEFAULT_BUDGET_MATERIAL_CHANNELS = 256 * 1024 * 1024; // 256 MB
static constexpr uint64 DEFAULT_BUDGET_MESH_DATA = 128 * 1024 * 1024;       // 128 MB
static constexpr uint64 DEFAULT_BUDGET_GENERAL = 64 * 1024 * 1024;          // 64 MB

// Memory pressure thresholds
static constexpr float MEMORY_PRESSURE_THRESHOLD = 0.15f;                   // 15% free memory
static constexpr float MEMORY_CRITICAL_THRESHOLD = 0.05f;                   // 5% free memory

FMemoryPoolManager::FMemoryPoolManager()
    : MemoryTracker(nullptr)
    , Defragmenter(nullptr)
    , bIsInitialized(false)
    , bNUMAAwarenessEnabled(false)
    , NUMAPreferredNode(0)
{
    // Initialize counter values
    MaxMemoryLimit.Set(UINT64_MAX); // Default to no limit
    AvailablePhysicalMemory.Set(0);
}

FMemoryPoolManager::~FMemoryPoolManager()
{
    if (IsInitialized())
    {
        Shutdown();
    }
}

bool FMemoryPoolManager::Initialize()
{
    // Guard against multiple initialization
    if (bIsInitialized)
    {
        return true;
    }

    // Register for memory warnings
    FCoreDelegates::GetMemoryTrimDelegate().AddRaw(this, &FMemoryPoolManager::OnMemoryWarning);

    // Check platform capabilities
    if (!IsSupported())
    {
        UE_LOG(LogTemp, Warning, TEXT("FMemoryPoolManager::Initialize - Some features may be disabled due to platform limitations"));
    }

    // Initialize memory tracker
    MemoryTracker = CreateMemoryTracker();
    if (!MemoryTracker)
    {
        UE_LOG(LogTemp, Error, TEXT("FMemoryPoolManager::Initialize - Failed to create memory tracker"));
        return false;
    }

    // Initialize defragmenter
    Defragmenter = new FMemoryDefragmenter();
    if (!Defragmenter)
    {
        UE_LOG(LogTemp, Error, TEXT("FMemoryPoolManager::Initialize - Failed to create defragmenter"));
        return false;
    }

    bIsInitialized = true;
    return true;
}

void FMemoryPoolManager::Shutdown()
{
    if (!bIsInitialized)
    {
        return;
    }

    // Unregister from memory warnings
    FCoreDelegates::GetMemoryTrimDelegate().RemoveAll(this);

    // Release all pools
    {
        FWriteScopeLock WriteLock(PoolsLock);
        Pools.Empty();
    }

    // Release all buffers
    {
        FWriteScopeLock WriteLock(BuffersLock);
        Buffers.Empty();
    }

    // Clean up memory tracker
    if (MemoryTracker)
    {
        delete MemoryTracker;
        MemoryTracker = nullptr;
    }

    // Clean up defragmenter
    if (Defragmenter)
    {
        delete Defragmenter;
        Defragmenter = nullptr;
    }

    bIsInitialized = false;
}

bool FMemoryPoolManager::IsInitialized() const
{
    return bIsInitialized;
}

bool FMemoryPoolManager::IsSupported() const
{
    // Check for minimum requirements
    bool bSupported = true;

    // Check for NUMA support
    if (FPlatformMemory::GetNumNUMANodes() > 1)
    {
        bNUMAAwarenessEnabled = true;
        NUMAPreferredNode = 0; // Default to first node
    }
    
    // Check for SIMD support
    if (!FPlatformMath::SupportsSSE4_1())
    {
        UE_LOG(LogTemp, Warning, TEXT("FMemoryPoolManager::IsSupported - SSE4.1 not supported, some optimizations will be disabled"));
        // We don't fail here, just log a warning
    }

    return bSupported;
}

IPoolAllocator* FMemoryPoolManager::CreatePool(
    const FName& PoolName, 
    uint32 BlockSize, 
    uint32 BlockCount, 
    EMemoryAccessPattern AccessPattern,
    bool bAllowGrowth)
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FMemoryPoolManager::CreatePool - Manager not initialized"));
        return nullptr;
    }

    if (PoolName.IsNone())
    {
        UE_LOG(LogTemp, Error, TEXT("FMemoryPoolManager::CreatePool - Invalid pool name"));
        return nullptr;
    }

    if (BlockSize == 0 || BlockCount == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("FMemoryPoolManager::CreatePool - Invalid pool parameters: BlockSize=%u, BlockCount=%u"),
            BlockSize, BlockCount);
        return nullptr;
    }

    // Check if the pool already exists
    {
        FReadScopeLock ReadLock(PoolsLock);
        const TSharedPtr<IPoolAllocator>* ExistingPool = Pools.Find(PoolName);
        if (ExistingPool && ExistingPool->IsValid())
        {
            UE_LOG(LogTemp, Warning, TEXT("FMemoryPoolManager::CreatePool - Pool '%s' already exists"),
                *PoolName.ToString());
            return ExistingPool->Get();
        }
    }

    // Ensure block size is properly aligned (at least 8-byte aligned)
    const uint32 AlignedBlockSize = Align(BlockSize, 8);
    
    // Create a new generic pool allocator
    TSharedPtr<IPoolAllocator> NewPool;
    
    // Forward to specialized allocator if appropriate based on access pattern
    switch (AccessPattern)
    {
        case EMemoryAccessPattern::Mining:
            // Create a pool with Z-order curve optimization for mining
            // For now we're using a generic pool, but we'll replace this with SVOAllocator later
            NewPool = MakeShared<FSVOAllocator>(PoolName, AlignedBlockSize, BlockCount, AccessPattern, bAllowGrowth);
            break;
            
        case EMemoryAccessPattern::SDFOperation:
            // Create a pool optimized for SDF operations with appropriate SIMD alignment
            // For now we're using a generic pool, but we'll replace this with NarrowBandAllocator later
            NewPool = MakeShared<FNarrowBandAllocator>(PoolName, AlignedBlockSize, BlockCount, AccessPattern, bAllowGrowth);
            break;
            
        default:
            // For generic pools we'll also use SVOAllocator for now as a placeholder
            // This will be replaced with a proper generic pool allocator implementation
            NewPool = MakeShared<FSVOAllocator>(PoolName, AlignedBlockSize, BlockCount, AccessPattern, bAllowGrowth);
            break;
    }
    
    // Initialize the pool
    if (!NewPool->Initialize())
    {
        UE_LOG(LogTemp, Error, TEXT("FMemoryPoolManager::CreatePool - Failed to initialize pool '%s'"),
            *PoolName.ToString());
        return nullptr;
    }
    
    // Register the pool
    {
        FWriteScopeLock WriteLock(PoolsLock);
        Pools.Add(PoolName, NewPool);
    }
    
    UE_LOG(LogTemp, Log, TEXT("FMemoryPoolManager::CreatePool - Created pool '%s' (BlockSize=%u, BlockCount=%u)"),
        *PoolName.ToString(), AlignedBlockSize, BlockCount);
        
    return NewPool.Get();
}

IPoolAllocator* FMemoryPoolManager::GetPool(const FName& PoolName) const
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FMemoryPoolManager::GetPool - Manager not initialized"));
        return nullptr;
    }

    if (PoolName.IsNone())
    {
        UE_LOG(LogTemp, Error, TEXT("FMemoryPoolManager::GetPool - Invalid pool name"));
        return nullptr;
    }

    // Find the pool in the registry
    FReadScopeLock ReadLock(PoolsLock);
    const TSharedPtr<IPoolAllocator>* FoundPool = Pools.Find(PoolName);
    if (FoundPool && FoundPool->IsValid())
    {
        return FoundPool->Get();
    }

    UE_LOG(LogTemp, Warning, TEXT("FMemoryPoolManager::GetPool - Pool '%s' not found"),
        *PoolName.ToString());
    return nullptr;
}

IBufferProvider* FMemoryPoolManager::CreateBuffer(
    const FName& BufferName, 
    uint64 SizeInBytes, 
    bool bZeroCopy,
    bool bGPUWritable)
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FMemoryPoolManager::CreateBuffer - Manager not initialized"));
        return nullptr;
    }

    if (BufferName.IsNone())
    {
        UE_LOG(LogTemp, Error, TEXT("FMemoryPoolManager::CreateBuffer - Invalid buffer name"));
        return nullptr;
    }

    if (SizeInBytes == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("FMemoryPoolManager::CreateBuffer - Invalid buffer size: %llu"), 
            SizeInBytes);
        return nullptr;
    }

    // Check if the buffer already exists
    {
        FReadScopeLock ReadLock(BuffersLock);
        const TSharedPtr<IBufferProvider>* ExistingBuffer = Buffers.Find(BufferName);
        if (ExistingBuffer && ExistingBuffer->IsValid())
        {
            UE_LOG(LogTemp, Warning, TEXT("FMemoryPoolManager::CreateBuffer - Buffer '%s' already exists"),
                *BufferName.ToString());
            return ExistingBuffer->Get();
        }
    }

    // Create a new buffer (use zero-copy if requested and supported)
    TSharedPtr<IBufferProvider> NewBuffer;
    
    if (bZeroCopy)
    {
        NewBuffer = MakeShared<FZeroCopyBuffer>(BufferName, SizeInBytes, bGPUWritable);
    }
    else
    {
        NewBuffer = MakeShared<FSharedBufferManager>(BufferName, SizeInBytes, bGPUWritable);
    }
    
    // Initialize the buffer
    if (!NewBuffer->Initialize())
    {
        UE_LOG(LogTemp, Error, TEXT("FMemoryPoolManager::CreateBuffer - Failed to initialize buffer '%s'"),
            *BufferName.ToString());
        return nullptr;
    }
    
    // Register the buffer
    {
        FWriteScopeLock WriteLock(BuffersLock);
        Buffers.Add(BufferName, NewBuffer);
    }
    
    UE_LOG(LogTemp, Log, TEXT("FMemoryPoolManager::CreateBuffer - Created buffer '%s' (Size=%llu, ZeroCopy=%s, GPUWritable=%s)"),
        *BufferName.ToString(), SizeInBytes, bZeroCopy ? TEXT("true") : TEXT("false"), bGPUWritable ? TEXT("true") : TEXT("false"));
        
    return NewBuffer.Get();
}

IBufferProvider* FMemoryPoolManager::GetBuffer(const FName& BufferName) const
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FMemoryPoolManager::GetBuffer - Manager not initialized"));
        return nullptr;
    }

    if (BufferName.IsNone())
    {
        UE_LOG(LogTemp, Error, TEXT("FMemoryPoolManager::GetBuffer - Invalid buffer name"));
        return nullptr;
    }

    // Find the buffer in the registry
    FReadScopeLock ReadLock(BuffersLock);
    const TSharedPtr<IBufferProvider>* FoundBuffer = Buffers.Find(BufferName);
    if (FoundBuffer && FoundBuffer->IsValid())
    {
        return FoundBuffer->Get();
    }

    UE_LOG(LogTemp, Warning, TEXT("FMemoryPoolManager::GetBuffer - Buffer '%s' not found"),
        *BufferName.ToString());
    return nullptr;
}

IMemoryTracker* FMemoryPoolManager::GetMemoryTracker() const
{
    return MemoryTracker;
}

bool FMemoryPoolManager::DefragmentMemory(float MaxTimeMs, EMemoryPriority Priority)
{
    if (!IsInitialized() || !Defragmenter)
    {
        UE_LOG(LogTemp, Error, TEXT("FMemoryPoolManager::DefragmentMemory - Manager not initialized or defragmenter not available"));
        return false;
    }

    // Update memory stats before defragmentation
    UpdateMemoryStats();
    
    // Defragmentation has different strategies based on priority threshold
    bool bResult = false;
    
    // Defragment pools starting with the highest priority ones
    FReadScopeLock ReadLock(PoolsLock);
    
    // Get all pools sorted by priority (this is a non-strict ordering)
    TArray<IPoolAllocator*> PoolsByPriority;
    for (const auto& PoolPair : Pools)
    {
        if (PoolPair.Value.IsValid())
        {
            PoolsByPriority.Add(PoolPair.Value.Get());
        }
    }

    // Basic tracking metrics for logging
    int32 PoolsDefragmented = 0;
    double TotalTimeMs = 0.0;
    
    // Timer to stay within the time budget
    double StartTime = FPlatformTime::Seconds();
    double EndTime = StartTime + MaxTimeMs / 1000.0;
    
    // Process pools until we run out of time
    for (IPoolAllocator* Pool : PoolsByPriority)
    {
        // Skip invalid pools
        if (!Pool)
        {
            continue;
        }
        
        // Calculate remaining time
        double CurrentTime = FPlatformTime::Seconds();
        double RemainingTime = EndTime - CurrentTime;
        
        // If we're out of time, break
        if (RemainingTime <= 0.0)
        {
            break;
        }
        
        // Perform defragmentation
        if (Pool->Defragment(static_cast<float>(RemainingTime * 1000.0)))
        {
            PoolsDefragmented++;
            bResult = true;
        }
        
        // Update timing
        TotalTimeMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
    }
    
    UE_LOG(LogTemp, Verbose, TEXT("FMemoryPoolManager::DefragmentMemory - Defragmented %d pools in %.2f ms"),
        PoolsDefragmented, TotalTimeMs);
        
    return bResult;
}

void* FMemoryPoolManager::Allocate(uint64 SizeInBytes, uint32 Alignment)
{
    // Validate alignment (must be power of 2)
    check(FMath::IsPowerOfTwo(Alignment));
    check(Alignment >= 1);
    
    // Allocate memory from the system allocator
    void* Memory = FMemory::Malloc(SizeInBytes, Alignment);
    
    // Track the allocation if memory tracker is available
    if (Memory && MemoryTracker && IsInitialized())
    {
        MemoryTracker->TrackAllocation(Memory, SizeInBytes, CATEGORY_GENERAL);
    }
    
    return Memory;
}

void FMemoryPoolManager::Free(void* Ptr)
{
    if (!Ptr)
    {
        return;
    }
    
    // Untrack the allocation if memory tracker is available
    if (MemoryTracker && IsInitialized())
    {
        MemoryTracker->UntrackAllocation(Ptr);
    }
    
    // Free the memory
    FMemory::Free(Ptr);
}

void FMemoryPoolManager::SetMemoryBudget(const FName& CategoryName, uint64 BudgetInBytes)
{
    if (CategoryName.IsNone())
    {
        UE_LOG(LogTemp, Error, TEXT("FMemoryPoolManager::SetMemoryBudget - Invalid category name"));
        return;
    }

    // Update the budget
    {
        FWriteScopeLock WriteLock(BudgetsLock);
        MemoryBudgets.FindOrAdd(CategoryName) = BudgetInBytes;
    }

    // Update the tracker if available
    if (MemoryTracker && IsInitialized())
    {
        MemoryTracker->SetMemoryBudget(CategoryName, BudgetInBytes);
    }
    
    UE_LOG(LogTemp, Verbose, TEXT("FMemoryPoolManager::SetMemoryBudget - Set budget for '%s' to %llu bytes"),
        *CategoryName.ToString(), BudgetInBytes);
}

uint64 FMemoryPoolManager::GetMemoryBudget(const FName& CategoryName) const
{
    if (CategoryName.IsNone())
    {
        UE_LOG(LogTemp, Error, TEXT("FMemoryPoolManager::GetMemoryBudget - Invalid category name"));
        return 0;
    }

    // Get the budget from our local cache
    {
        FReadScopeLock ReadLock(BudgetsLock);
        const uint64* Budget = MemoryBudgets.Find(CategoryName);
        if (Budget)
        {
            return *Budget;
        }
    }

    return 0;
}

uint64 FMemoryPoolManager::GetMemoryUsage(const FName& CategoryName) const
{
    if (!IsInitialized() || !MemoryTracker)
    {
        return 0;
    }

    return MemoryTracker->GetMemoryUsage(CategoryName);
}

void FMemoryPoolManager::RegisterAllocation(void* Ptr, uint64 SizeInBytes, const FName& CategoryName, const FName& AllocationName)
{
    if (!IsInitialized() || !MemoryTracker)
    {
        return;
    }

    if (!Ptr || SizeInBytes == 0 || CategoryName.IsNone())
    {
        UE_LOG(LogTemp, Error, TEXT("FMemoryPoolManager::RegisterAllocation - Invalid parameters"));
        return;
    }

    MemoryTracker->TrackAllocation(Ptr, SizeInBytes, CategoryName, AllocationName);
}

void FMemoryPoolManager::UnregisterAllocation(void* Ptr, const FName& CategoryName)
{
    if (!IsInitialized() || !MemoryTracker || !Ptr)
    {
        return;
    }

    MemoryTracker->UntrackAllocation(Ptr);
}

IMemoryManager& FMemoryPoolManager::Get()
{
    // Thread-safe singleton initialization
    if (!ManagerInstance)
    {
        // Critical section to prevent race condition
        FScopeLock Lock(&SingletonLock);
        
        // Double-check that the instance is still null
        if (!ManagerInstance)
        {
            ManagerInstance = new FMemoryPoolManager();
            ManagerInstance->Initialize();
        }
    }
    
    check(ManagerInstance);
    return *ManagerInstance;
}

IPoolAllocator* FMemoryPoolManager::CreateSVONodePool(
    const FName& PoolName,
    uint32 NodeSize,
    uint32 NodeCount)
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FMemoryPoolManager::CreateSVONodePool - Manager not initialized"));
        return nullptr;
    }

    // Create SVO-specific allocator for octree nodes
    // This is just a thin wrapper around CreatePool for now, but will be specialized for SVO needs
    return CreatePool(
        PoolName, 
        NodeSize, 
        NodeCount, 
        EMemoryAccessPattern::OctreeTraversal, 
        true);  // Allow growth by default
}

IPoolAllocator* FMemoryPoolManager::CreateNarrowBandPool(
    const FName& PoolName,
    EMemoryTier PrecisionTier,
    uint32 ChannelCount,
    uint32 ElementCount)
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FMemoryPoolManager::CreateNarrowBandPool - Manager not initialized"));
        return nullptr;
    }

    if (ChannelCount == 0 || ElementCount == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("FMemoryPoolManager::CreateNarrowBandPool - Invalid parameters: ChannelCount=%u, ElementCount=%u"),
            ChannelCount, ElementCount);
        return nullptr;
    }

    // Calculate block size based on precision tier and channel count
    uint32 ElementSize = 0;
    switch (PrecisionTier)
    {
        case EMemoryTier::Hot:
            // High precision - 4 bytes per channel value
            ElementSize = sizeof(float) * ChannelCount; 
            break;

        case EMemoryTier::Warm:
            // Medium precision - 2 bytes per channel value
            ElementSize = sizeof(uint16) * ChannelCount;
            break;

        case EMemoryTier::Cold:
            // Low precision - 1 byte per channel value
            ElementSize = sizeof(uint8) * ChannelCount;
            break;

        case EMemoryTier::Archive:
            // Archive tier - compressed format with header
            ElementSize = FMath::Max(1u, (ChannelCount + 7) / 8); // Round up to bytes
            break;

        default:
            // Default to high precision
            ElementSize = sizeof(float) * ChannelCount;
            break;
    }

    // Element size must include at least 4 bytes for position data in addition to channel data
    ElementSize += 4; 

    // Create a specialized pool for narrow band data
    return CreatePool(
        PoolName, 
        ElementSize, 
        ElementCount, 
        EMemoryAccessPattern::SDFOperation, 
        true);  // Allow growth by default
}

FMemoryStats FMemoryPoolManager::GetDetailedMemoryStats() const
{
    if (!IsInitialized() || !MemoryTracker)
    {
        return FMemoryStats();
    }

    return MemoryTracker->GetMemoryStats();
}

FSVOSDFMemoryMetrics FMemoryPoolManager::GetSVOSDFMemoryMetrics() const
{
    if (!IsInitialized() || !MemoryTracker)
    {
        return FSVOSDFMemoryMetrics();
    }

    return MemoryTracker->GetSVOSDFMemoryMetrics();
}

uint64 FMemoryPoolManager::ReduceMemoryUsage(uint64 TargetReductionBytes, float MaxTimeMs)
{
    if (!IsInitialized())
    {
        return 0;
    }

    uint64 TotalFreed = 0;

    // First try to enforce budgets at the cacheable level
    TotalFreed += EnforceBudgets(EMemoryPriority::Cacheable);
    
    // If we need more, release unused resources
    if (TotalFreed < TargetReductionBytes)
    {
        TotalFreed += ReleaseUnusedResources(MaxTimeMs * 0.5f);
    }
    
    // If we still need more, enforce budgets at the low level
    if (TotalFreed < TargetReductionBytes)
    {
        TotalFreed += EnforceBudgets(EMemoryPriority::Low);
    }
    
    // If we're in severe memory pressure, shrink pools
    if (TotalFreed < TargetReductionBytes && IsUnderMemoryPressure())
    {
        // Adjust pool sizes based on memory pressure
        AdjustPoolSizes();
        
        // Note: We don't know exactly how much memory this frees,
        // but we update our tracker metrics
    }
    
    // Update memory stats after reduction
    UpdateMemoryStats();
    
    return TotalFreed;
}

bool FMemoryPoolManager::IsUnderMemoryPressure(uint64* OutAvailableBytes) const
{
    // Update memory stats
    if (IsInitialized())
    {
        const_cast<FMemoryPoolManager*>(this)->UpdateMemoryStats();
    }
    
    // Get current memory stats
    const uint64 AvailableMemory = AvailablePhysicalMemory.GetValue();
    const FMemoryStats Stats = GetDetailedMemoryStats();
    
    // Calculate thresholds
    const uint64 TotalMemory = Stats.TotalPhysicalMemory;
    const uint64 PressureThreshold = static_cast<uint64>(TotalMemory * MEMORY_PRESSURE_THRESHOLD);
    
    // Provide available bytes if requested
    if (OutAvailableBytes)
    {
        *OutAvailableBytes = AvailableMemory;
    }
    
    // Check if available memory is below the pressure threshold
    return AvailableMemory < PressureThreshold;
}

void FMemoryPoolManager::SetMaxMemoryLimit(uint64 MaxMemoryBytes)
{
    if (MaxMemoryBytes == 0)
    {
        // A value of 0 indicates no limit
        MaxMemoryLimit.Set(UINT64_MAX);
        UE_LOG(LogTemp, Log, TEXT("FMemoryPoolManager::SetMaxMemoryLimit - Memory limit disabled"));
    }
    else
    {
        MaxMemoryLimit.Set(MaxMemoryBytes);
        UE_LOG(LogTemp, Log, TEXT("FMemoryPoolManager::SetMaxMemoryLimit - Memory limit set to %llu bytes"), MaxMemoryBytes);
    }
}

IMemoryTracker* FMemoryPoolManager::CreateMemoryTracker()
{
    // Create a memory tracker instance
    return new FMemoryTelemetry();
}

FMemoryDefragmenter* FMemoryPoolManager::CreateDefragmenter()
{
    // Create a memory defragmenter instance
    return new FMemoryDefragmenter();
}

void FMemoryPoolManager::UpdateMemoryStats()
{
    if (!IsInitialized())
    {
        return;
    }
    
    // Get platform memory stats
    FPlatformMemoryStats MemoryStats = FPlatformMemory::GetStats();
    
    // Update available memory counter
    AvailablePhysicalMemory.Set(MemoryStats.AvailablePhysical);
    
    // Update memory tracker if available
    if (MemoryTracker)
    {
        // Update our memory usage stats (tracker has its own more detailed stats)
    }
}

void FMemoryPoolManager::AdjustPoolSizes()
{
    if (!IsInitialized())
    {
        return;
    }

    // Get all pools
    FReadScopeLock ReadLock(PoolsLock);
    
    for (const auto& PoolPair : Pools)
    {
        if (!PoolPair.Value.IsValid())
        {
            continue;
        }
        
        IPoolAllocator* Pool = PoolPair.Value.Get();
        
        // Get current pool statistics
        FPoolStats PoolStats = Pool->GetStats();
        
        // Calculate free percentage
        float FreePercentage = 0.0f;
        if (PoolStats.BlockCount > 0)
        {
            FreePercentage = static_cast<float>(PoolStats.FreeBlocks) / static_cast<float>(PoolStats.BlockCount);
        }
        
        // If the pool has significant free blocks (>25%), try to shrink it
        if (FreePercentage > 0.25f)
        {
            // Calculate how many blocks to remove (up to half of free blocks)
            uint32 BlocksToRemove = static_cast<uint32>(PoolStats.FreeBlocks * 0.5f);
            
            // Shrink the pool
            uint32 BlocksRemoved = Pool->Shrink(BlocksToRemove);
            
            if (BlocksRemoved > 0)
            {
                UE_LOG(LogTemp, Verbose, TEXT("FMemoryPoolManager::AdjustPoolSizes - Shrunk pool '%s' by %u blocks"),
                    *PoolStats.PoolName.ToString(), BlocksRemoved);
            }
        }
    }
}

uint64 FMemoryPoolManager::EnforceBudgets(EMemoryPriority PriorityThreshold)
{
    if (!IsInitialized() || !MemoryTracker)
    {
        return 0;
    }
    
    uint64 TotalFreed = 0;
    FMemoryStats Stats = GetDetailedMemoryStats();
    
    // Get budget information
    FReadScopeLock ReadLock(BudgetsLock);
    
    // Check each category against its budget
    for (const auto& Usage : Stats.UsageByCategory)
    {
        const FName& CategoryName = Usage.Key;
        const uint64 CurrentUsage = Usage.Value;
        
        // Find budget for this category
        const uint64* BudgetPtr = MemoryBudgets.Find(CategoryName);
        if (!BudgetPtr)
        {
            continue;
        }
        
        const uint64 Budget = *BudgetPtr;
        
        // Check if usage exceeds budget
        if (CurrentUsage > Budget)
        {
            // Calculate how much we need to free
            const uint64 OverBudget = CurrentUsage - Budget;
            
            // Log the over-budget condition
            UE_LOG(LogTemp, Verbose, TEXT("FMemoryPoolManager::EnforceBudgets - Category '%s' is over budget: %llu/%llu"),
                *CategoryName.ToString(), CurrentUsage, Budget);
            
            // Get allocations for this category
            TArray<FMemoryAllocationInfo> Allocations = MemoryTracker->GetAllocationsByCategory(CategoryName);
            
            // Sort allocations by priority (we only have allocation metadata, not actual priority)
            // In a real implementation, we'd store priorities with allocations
            // For now, let's use time as a proxy for priority (older allocations freed first)
            Allocations.Sort([](const FMemoryAllocationInfo& A, const FMemoryAllocationInfo& B) {
                return A.TimeStamp < B.TimeStamp;
            });
            
            // Start freeing allocations until we're under budget or run out of allocations
            uint64 Freed = 0;
            for (const FMemoryAllocationInfo& Allocation : Allocations)
            {
                // Skip memory we don't directly control (e.g., UObject memory)
                if (!Allocation.Ptr || Allocation.AssociatedObject.IsValid())
                {
                    continue;
                }
                
                // Track how much we're freeing
                Freed += Allocation.SizeInBytes;
                TotalFreed += Allocation.SizeInBytes;
                
                // Free the memory
                Free(Allocation.Ptr);
                
                // Check if we've freed enough
                if (Freed >= OverBudget)
                {
                    break;
                }
            }
            
            UE_LOG(LogTemp, Verbose, TEXT("FMemoryPoolManager::EnforceBudgets - Freed %llu bytes from category '%s'"),
                Freed, *CategoryName.ToString());
        }
    }
    
    return TotalFreed;
}

uint64 FMemoryPoolManager::ReleaseUnusedResources(float MaxTimeMs)
{
    if (!IsInitialized())
    {
        return 0;
    }

    // This is a placeholder for actual resource release logic
    // In a real implementation, this would work with resource caches to free memory
    
    // For now, we just defragment all pools to free up memory
    DefragmentMemory(MaxTimeMs);
    
    // Real implementation would track and return actual bytes freed
    return 0;
}

void FMemoryPoolManager::OnMemoryWarning()
{
    if (!bIsInitialized)
    {
        return;
    }

    // Log memory state
    UE_LOG(LogTemp, Warning, TEXT("FMemoryPoolManager::OnMemoryWarning - Memory warning received, current usage: %llu bytes"),
        GetTotalMemoryUsage());

    // Attempt to reduce memory usage
    EnforceBudgets(EMemoryPriority::Low);
    ReleaseUnusedResources(5.0f); // Give it up to 5ms to free resources
}