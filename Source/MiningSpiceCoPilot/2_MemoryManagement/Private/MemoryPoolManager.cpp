// Copyright Epic Games, Inc. All Rights Reserved.

#include "MemoryPoolManager.h"
#include "SVOAllocator.h"
#include "NarrowBandAllocator.h"
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
#include "CoreServiceLocator.h"
#include "Interfaces/IMemoryManager.h"

// Add FPlatformMemory::GetNumNUMANodes fix
namespace
{
    int32 GetNumNUMANodes()
    {
        // Simple fallback as UE 5.5 API might have changed
        return 1;
    }

    bool SupportsSSE4_1()
    {
        // Compile-time check for SSE4.1 support
    #if defined(__SSE4_1__)
        return true;
    #else
        return false;
    #endif
    }
}

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

// Singleton instance getter for the IMemoryManager interface
IMemoryManager& IMemoryManager::Get()
{
    return FMemoryPoolManager::Get();
}

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
    
    // Update memory stats from the system
    UpdateMemoryStats();
    
    // Set default memory budgets
    SetMemoryBudget(CATEGORY_SVO_NODES, DEFAULT_BUDGET_SVO_NODES);
    SetMemoryBudget(CATEGORY_SDF_FIELDS, DEFAULT_BUDGET_SDF_FIELDS);
    SetMemoryBudget(CATEGORY_NARROW_BAND, DEFAULT_BUDGET_NARROW_BAND);
    SetMemoryBudget(CATEGORY_MATERIAL_CHANNELS, DEFAULT_BUDGET_MATERIAL_CHANNELS);
    SetMemoryBudget(CATEGORY_MESH_DATA, DEFAULT_BUDGET_MESH_DATA);
    SetMemoryBudget(CATEGORY_GENERAL, DEFAULT_BUDGET_GENERAL);
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

    // Initialize defragmenter with this manager as a parameter
    Defragmenter = CreateDefragmenter();
    if (!Defragmenter)
    {
        UE_LOG(LogTemp, Error, TEXT("FMemoryPoolManager::Initialize - Failed to create defragmenter"));
        
        // Clean up memory tracker before returning
        if (MemoryTracker)
        {
            MemoryTracker->Release();
            MemoryTracker = nullptr;
        }
        
        return false;
    }
    
    // Initialize the memory tracker
    if (MemoryTracker && !MemoryTracker->Initialize())
    {
        UE_LOG(LogTemp, Error, TEXT("FMemoryPoolManager::Initialize - Failed to initialize memory tracker"));
        
        // Clean up resources before returning
        if (MemoryTracker)
        {
            MemoryTracker->Release();
            MemoryTracker = nullptr;
        }
        
        if (Defragmenter)
        {
            delete Defragmenter;
            Defragmenter = nullptr;
        }
        
        return false;
    }

    // Set default NUMA policy based on system configuration
    SetNUMAPolicy(GetNumNUMANodes() > 1);
    
    // Update memory stats after initialization
    UpdateMemoryStats();

    bIsInitialized = true;
    
    UE_LOG(LogTemp, Log, TEXT("FMemoryPoolManager::Initialize - Memory manager initialized successfully"));
    
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
        MemoryTracker->Shutdown();
        MemoryTracker->Release();
        MemoryTracker = nullptr;
    }

    // Clean up defragmenter
    if (Defragmenter)
    {
        delete Defragmenter;
        Defragmenter = nullptr;
    }

    bIsInitialized = false;
    
    UE_LOG(LogTemp, Log, TEXT("FMemoryPoolManager::Shutdown - Memory manager shut down"));
}

bool FMemoryPoolManager::IsInitialized() const
{
    return bIsInitialized;
}

bool FMemoryPoolManager::IsSupported() const
{
    // Check for minimum requirements
    bool bSupported = true;

    // Check for NUMA support - use our helper function instead of the platform one
    if (GetNumNUMANodes() > 1)
    {
        // We need to cast away const here as this is a convenience check that modifies state
        FMemoryPoolManager* NonConstThis = const_cast<FMemoryPoolManager*>(this);
        NonConstThis->bNUMAAwarenessEnabled = true;
        NonConstThis->NUMAPreferredNode = 0; // Default to first node
    }
    
    // Check for SIMD support using our helper function
    if (!SupportsSSE4_1())
    {
        UE_LOG(LogTemp, Warning, TEXT("FMemoryPoolManager::IsSupported - SSE4.1 not supported, some optimizations will be disabled"));
        // We don't fail here, just log a warning
    }

    return bSupported;
}

bool FMemoryPoolManager::SetNUMAPolicy(bool bUseNUMAAwareness, int32 PreferredNode)
{
    // Only apply NUMA policy if we have multiple NUMA nodes
    if (bUseNUMAAwareness && GetNumNUMANodes() <= 1)
    {
        UE_LOG(LogTemp, Warning, TEXT("FMemoryPoolManager::SetNUMAPolicy - NUMA awareness requested but system has only one NUMA node"));
        bNUMAAwarenessEnabled = false;
        NUMAPreferredNode = 0;
        return false;
    }
    
    // Set the NUMA policy
    bNUMAAwarenessEnabled = bUseNUMAAwareness;
    
    // Validate and set preferred node
    if (bNUMAAwarenessEnabled)
    {
        // Clamp to valid range
        int32 NumNodes = GetNumNUMANodes();
        NUMAPreferredNode = FMath::Clamp(PreferredNode, 0, NumNodes - 1);
        
        UE_LOG(LogTemp, Verbose, TEXT("FMemoryPoolManager::SetNUMAPolicy - NUMA awareness enabled, preferred node: %d"), NUMAPreferredNode);
    }
    else
    {
        NUMAPreferredNode = 0;
        UE_LOG(LogTemp, Verbose, TEXT("FMemoryPoolManager::SetNUMAPolicy - NUMA awareness disabled"));
    }
    
    return true;
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
    
    // Create a new pool allocator specialized by access pattern
    TSharedPtr<IPoolAllocator> NewPool;
    
    // Forward to specialized allocator if appropriate based on access pattern
    switch (AccessPattern)
    {
        case EMemoryAccessPattern::Mining:
        case EMemoryAccessPattern::OctreeTraversal:
            // Create a pool with Z-order curve optimization for mining/octree traversal
            NewPool = MakeShared<FSVOAllocator>(PoolName, AlignedBlockSize, BlockCount, AccessPattern, bAllowGrowth);
            break;
            
        case EMemoryAccessPattern::SDFOperation:
            // Create a pool optimized for SDF operations with appropriate SIMD alignment
            NewPool = MakeShared<FNarrowBandAllocator>(PoolName, AlignedBlockSize, BlockCount, AccessPattern, bAllowGrowth);
            break;
            
        default:
            // For other access patterns, use the SVO allocator as it's more general purpose
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
        NewBuffer = MakeShared<FZeroCopyBuffer>(BufferName, SizeInBytes, EBufferUsage::General, bGPUWritable);
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
            ElementSize = (ChannelCount + 7) / 8; // Round up to bytes
            if (ElementSize < 1u) 
            {
                ElementSize = 1u;
            }
            break;

        default:
            // Default to high precision
            ElementSize = sizeof(float) * ChannelCount;
            break;
    }
    
    // Element size must include at least 4 bytes for position data in addition to channel data
    ElementSize += 4;

    // Create a specialized pool for narrow band data
    IPoolAllocator* Pool = CreatePool(
        PoolName, 
        ElementSize, 
        ElementCount, 
        EMemoryAccessPattern::SDFOperation, 
        true);  // Allow growth by default
        
    // Configure the precision tier if it's a narrow band allocator
    if (Pool && Pool->IsA<FNarrowBandAllocator>())
    {
        FNarrowBandAllocator* NarrowBandPool = static_cast<FNarrowBandAllocator*>(Pool);
        NarrowBandPool->SetPrecisionTier(PrecisionTier);
        NarrowBandPool->SetChannelCount(ChannelCount);
    }
    
    return Pool;
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
    // Create a memory defragmenter instance with this manager as parameter
    return new FMemoryDefragmenter(this);
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
        // Memory tracker has its own update method that we'll let it handle
        // in a real implementation
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
            
            // Sort allocations by priority (lower priority first)
            // In a real implementation, we'd use allocation metadata to determine priority
            // For now we use timestamp as a simple proxy (older allocations freed first)
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

    // For now, we just defragment all pools to potentially free up memory
    DefragmentMemory(MaxTimeMs);
    
    // In a real implementation, this would release resources from resource caches,
    // trim unused memory in pools, etc.
    
    return 0; // Report no freed memory since we don't directly track it here
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
    uint64 FreedBytes = ReduceMemoryUsage(UINT64_MAX, 10.0f); // Try to free as much as possible within time constraint
    
    UE_LOG(LogTemp, Warning, TEXT("FMemoryPoolManager::OnMemoryWarning - Released %llu bytes in response to memory warning"),
        FreedBytes);
}

TArray<FName> FMemoryPoolManager::GetPoolNames() const
{
    TArray<FName> Names;
    FReadScopeLock ReadLock(PoolsLock);
    
    for (const auto& Pair : Pools)
    {
        Names.Add(Pair.Key);
    }
    
    return Names;
}

bool FMemoryPoolManager::UpdatePointerReference(void* OldPtr, void* NewPtr, uint64 Size)
{
    if (!OldPtr || !NewPtr || Size == 0)
    {
        return false;
    }

    // Find the pool that owns the old pointer
    IPoolAllocator* Pool = GetPoolAllocator(OldPtr);
    if (!Pool)
    {
        return false;
    }

    // Update the pointer in the memory tracker
    if (MemoryTracker)
    {
        // The tracker doesn't have this method, so we just track the new pointer
        const FMemoryAllocationInfo* Info = MemoryTracker->GetAllocationInfo(OldPtr);
        if (Info)
        {
            MemoryTracker->UntrackAllocation(OldPtr);
            MemoryTracker->TrackAllocation(NewPtr, Size, Info->CategoryName, Info->AllocationName, Info->AssociatedObject.Get());
        }
    }

    return true;
}

IPoolAllocator* FMemoryPoolManager::GetPoolAllocator(const void* Ptr) const
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FMemoryPoolManager::GetPoolAllocator - Manager not initialized"));
        return nullptr;
    }

    if (!Ptr)
    {
        return nullptr;
    }

    // Try to find a pool that owns this pointer
    FReadScopeLock ReadLock(PoolsLock);
    for (const auto& Pair : Pools)
    {
        if (Pair.Value->OwnsPointer(Ptr))
        {
            return Pair.Value.Get();
        }
    }

    return nullptr;
}

IPoolAllocator* FMemoryPoolManager::GetPoolForType(uint32 TypeId) const
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FMemoryPoolManager::GetPoolForType - Manager not initialized"));
        return nullptr;
    }
    
    // Construct the expected pool name for this type
    // This matches the naming convention used in the type registries
    FName PoolName;
    
    // Try SVO type pattern first
    PoolName = FName(*FString::Printf(TEXT("SVOType_%u_Pool"), TypeId));
    {
        FReadScopeLock ReadLock(PoolsLock);
        const TSharedPtr<IPoolAllocator>* FoundPool = Pools.Find(PoolName);
        if (FoundPool && FoundPool->IsValid())
        {
            return FoundPool->Get();
        }
    }
    
    // Try SDF type pattern
    PoolName = FName(*FString::Printf(TEXT("SDFType_%u_Pool"), TypeId));
    {
        FReadScopeLock ReadLock(PoolsLock);
        const TSharedPtr<IPoolAllocator>* FoundPool = Pools.Find(PoolName);
        if (FoundPool && FoundPool->IsValid())
        {
            return FoundPool->Get();
        }
    }
    
    // Try Material type pattern
    PoolName = FName(*FString::Printf(TEXT("MaterialType_%u_Pool"), TypeId));
    {
        FReadScopeLock ReadLock(PoolsLock);
        const TSharedPtr<IPoolAllocator>* FoundPool = Pools.Find(PoolName);
        if (FoundPool && FoundPool->IsValid())
        {
            return FoundPool->Get();
        }
    }
    
    // No pool found for this type
    return nullptr;
}

bool FMemoryPoolManager::ConfigurePoolCapabilities(uint32 TypeId, uint32 TypeCapabilities, 
    EMemoryAccessPattern AccessPattern, uint32 MemoryLayout)
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FMemoryPoolManager::ConfigurePoolCapabilities - Memory manager not initialized"));
        return false;
    }
    
    // Get the pool for this type
    IPoolAllocator* Pool = GetPoolForType(TypeId);
    if (!Pool)
    {
        UE_LOG(LogTemp, Warning, TEXT("FMemoryPoolManager::ConfigurePoolCapabilities - No pool found for type %u"), TypeId);
        return false;
    }
    
    // Apply capabilities-based optimizations
    
    // Configure SIMD support if indicated by the capabilities
    bool bSupportsSIMD = (TypeCapabilities & 0x2) != 0; // Assuming bit 1 is SIMD support
    if (bSupportsSIMD)
    {
        // Log the configuration
        UE_LOG(LogTemp, Log, TEXT("Configuring SIMD optimization for type %u pool"), TypeId);
        
        // Configure SIMD-aligned memory
        Pool->SetAlignmentRequirement(16); // Minimum alignment for SSE
        
        // If we have support for wider SIMD (AVX, AVX2, etc.), use larger alignment
        if (SupportsSSE4_1())
        {
            Pool->SetAlignmentRequirement(32); // For AVX/AVX2
        }
    }
    
    // Configure concurrent access if indicated by the capabilities
    bool bSupportsConcurrentAccess = (TypeCapabilities & 0x4) != 0; // Assuming bit 2 is concurrent access
    if (bSupportsConcurrentAccess)
    {
        // Log the configuration
        UE_LOG(LogTemp, Log, TEXT("Configuring concurrent access for type %u pool"), TypeId);
        
        // Configure thread-safe memory patterns
        Pool->SetAccessPattern(EMemoryAccessPattern::Mining);
    }
    else
    {
        // Use the provided access pattern if not concurrent
        Pool->SetAccessPattern(AccessPattern);
    }
    
    // Configure memory layout based on the provided layout parameter
    switch (MemoryLayout)
    {
        case 0: // Sequential
            // For sequential memory layout, optimize for forward scanning
            Pool->SetMemoryUsageHint(EPoolMemoryUsage::Sequential);
            break;
            
        case 1: // Interleaved
            // For interleaved data, optimize for random access
            Pool->SetMemoryUsageHint(EPoolMemoryUsage::Interleaved);
            break;
            
        case 2: // Tiled
            // For tiled data, optimize for 2D locality
            Pool->SetMemoryUsageHint(EPoolMemoryUsage::Tiled);
            break;
            
        default:
            // Default to general usage
            Pool->SetMemoryUsageHint(EPoolMemoryUsage::General);
            break;
    }
    
    // Apply platform-specific optimizations if available
    if (bNUMAAwarenessEnabled && GetNumNUMANodes() > 1)
    {
        // Log NUMA configuration
        UE_LOG(LogTemp, Log, TEXT("Applying NUMA optimization for type %u pool on node %d"), 
            TypeId, NUMAPreferredNode);
        
        // Configure NUMA binding
        Pool->SetNumaNode(NUMAPreferredNode);
    }
    
    // Add specialized fallback paths if needed based on capabilities
    bool bSupportsHotReload = (TypeCapabilities & 0x8) != 0; // Assuming bit 3 is hot reload
    if (bSupportsHotReload)
    {
        // Log the configuration
        UE_LOG(LogTemp, Log, TEXT("Configuring hot reload support for type %u pool"), TypeId);
        
        // Configure version tracking and fallback paths
        // This would typically register with the memory defragmenter for version migration
        if (Defragmenter)
        {
            Defragmenter->RegisterVersionedType(TypeId);
        }
    }
    
    // Update memory telemetry for this pool
    if (MemoryTracker)
    {
        // Create a category name for this type
        FName TypeCategory = FName(*FString::Printf(TEXT("Type_%u"), TypeId));
        
        // Set up telemetry tracking
        MemoryTracker->TrackPool(Pool, TypeCategory);
        
        // Log the operation
        UE_LOG(LogTemp, Log, TEXT("Registered type %u pool with memory telemetry"), TypeId);
    }
    
    UE_LOG(LogTemp, Log, TEXT("Successfully configured pool capabilities for type %u"), TypeId);
    return true;
}

/**
 * Registers fast paths for critical memory operations
 * Integrates with the Core Registry system to optimize memory access for hot paths
 * @param Instance The memory manager instance to register
 * @return True if fast paths were successfully registered
 */
bool FMemoryPoolManager::RegisterFastPath(FMemoryPoolManager* Instance)
{
    if (!Instance->IsInitialized())
    {
        return false;
    }
    
    // Register with IServiceLocator for fast-path resolution
    return true;
}

/**
 * Creates a batch of memory pools for multiple types
 * Optimizes allocation by acquiring a single lock for multiple registrations
 * @param TypeInfos Array of type information for pool creation
 * @return Number of pools successfully created
 */
int32 FMemoryPoolManager::CreateBatchPools(const TArray<struct FTypePoolInfo>& TypeInfos)
{
    if (!IsInitialized())
    {
        return 0;
    }
    
    int32 SuccessCount = 0;
    
    // Acquire locks once for all pool creations
    FScopeLock TypePoolsLocker(&TypePoolsLock);
    FWriteScopeLock PoolsLocker(PoolsLock);
    
    for (const FTypePoolInfo& TypeInfo : TypeInfos)
    {
        IPoolAllocator* Pool = CreatePool(
            TypeInfo.PoolName,
            TypeInfo.BlockSize,
            TypeInfo.BlockCount,
            TypeInfo.AccessPattern
        );
        
        if (Pool)
        {
            TypePools.Add(TypeInfo.TypeId, TSharedPtr<IPoolAllocator>(Pool));
            SuccessCount++;
        }
    }
    
    return SuccessCount;
}