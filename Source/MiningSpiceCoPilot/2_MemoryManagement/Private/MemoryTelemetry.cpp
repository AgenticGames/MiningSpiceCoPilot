// Copyright Epic Games, Inc. All Rights Reserved.

#include "MemoryTelemetry.h"
#include "HAL/PlatformMemory.h"
#include "Misc/ScopeLock.h"
#include "Interfaces/IPoolAllocator.h"

FMemoryTelemetry::FMemoryTelemetry()
    : bIsInitialized(false)
    , bCallStackTrackingEnabled(false)
    , StackTraceDepth(16)
    , LastStatsUpdateTime(0.0)
    , TotalAllocatedMemory(0)
    , PeakMemoryUsage(0)
    , TotalAllocationCount(0)
    , ActiveAllocationCount(0)
    , bStatsDirty(true)
    , bSVOSDFMetricsDirty(true)
{
}

FMemoryTelemetry::~FMemoryTelemetry()
{
    if (IsInitialized())
    {
        Shutdown();
    }
}

bool FMemoryTelemetry::Initialize()
{
    FScopeLock Lock(&AllocationLock);
    
    if (bIsInitialized)
    {
        return true;
    }
    
    // Initialize memory statistics
    UpdateMemoryStats();
    
    bIsInitialized = true;
    return true;
}

void FMemoryTelemetry::Shutdown()
{
    FScopeLock Lock(&AllocationLock);
    
    if (!bIsInitialized)
    {
        return;
    }
    
    // Clear all tracked allocations
    Allocations.Empty();
    MemoryUsageByCategory.Empty();
    AllocationCountByCategory.Empty();
    MemoryBudgets.Empty();
    
    bIsInitialized = false;
}

bool FMemoryTelemetry::IsInitialized() const
{
    return bIsInitialized;
}

bool FMemoryTelemetry::TrackAllocation(void* Ptr, uint64 SizeInBytes, const FName& CategoryName, 
    const FName& AllocationName, const UObject* RequestingObject)
{
    if (!bIsInitialized || !Ptr || SizeInBytes == 0)
    {
        return false;
    }
    
    FScopeLock Lock(&AllocationLock);
    
    // Create allocation info
    FMemoryAllocationInfo Info;
    Info.Ptr = Ptr;
    Info.SizeInBytes = SizeInBytes;
    Info.CategoryName = CategoryName;
    Info.AllocationName = AllocationName;
    Info.TimeStamp = FPlatformTime::Seconds();
    Info.AssociatedObject = RequestingObject;
    Info.MemoryTier = EMemoryTier::Warm; // Default to warm tier
    Info.AccessPattern = EMemoryAccessPattern::General; // Default to general
    
    // Capture call stack if enabled
    if (bCallStackTrackingEnabled)
    {
        CaptureCallStack(Info.CallStack);
    }
    
    // Add to tracking
    Allocations.Add(Ptr, Info);
    
    // Update statistics
    TotalAllocatedMemory += SizeInBytes;
    PeakMemoryUsage = FMath::Max(PeakMemoryUsage, TotalAllocatedMemory);
    TotalAllocationCount++;
    ActiveAllocationCount++;
    
    // Mark stats as dirty
    bStatsDirty = true;
    
    return true;
}

bool FMemoryTelemetry::UntrackAllocation(void* Ptr)
{
    if (!bIsInitialized || !Ptr)
    {
        return false;
    }
    
    FScopeLock Lock(&AllocationLock);
    
    // Find the allocation
    FMemoryAllocationInfo* Info = Allocations.Find(Ptr);
    if (!Info)
    {
        return false;
    }
    
    // Update statistics
    TotalAllocatedMemory -= Info->SizeInBytes;
    ActiveAllocationCount--;
    
    // Remove from tracking
    Allocations.Remove(Ptr);
    
    // Mark stats as dirty
    bStatsDirty = true;
    
    return true;
}

const FMemoryAllocationInfo* FMemoryTelemetry::GetAllocationInfo(void* Ptr) const
{
    if (!bIsInitialized || !Ptr)
    {
        return nullptr;
    }
    
    FScopeLock Lock(&AllocationLock);
    
    return Allocations.Find(Ptr);
}

bool FMemoryTelemetry::ValidateStats(TArray<FString>& OutErrors) const
{
    bool bValid = true;
    
    // Make sure all allocations are accounted for
    for (const auto& AllocationPair : Allocations)
    {
        if (!AllocationPair.Key)
        {
            OutErrors.Add(TEXT("Memory Telemetry: Found null allocation pointer in registry"));
            bValid = false;
            continue;
        }
        
        const FMemoryAllocationInfo& Info = AllocationPair.Value;
        
        if (Info.SizeInBytes == 0)
        {
            OutErrors.Add(FString::Printf(TEXT("Memory Telemetry: Zero-sized allocation found at %p"), AllocationPair.Key));
            bValid = false;
        }
        
        if (Info.CategoryName.IsNone())
        {
            OutErrors.Add(FString::Printf(TEXT("Memory Telemetry: Allocation at %p has no category"), AllocationPair.Key));
            bValid = false;
        }
    }
    
    // Validate category totals against actual allocations
    TMap<FName, uint64> CalculatedTotals;
    
    for (const auto& AllocationPair : Allocations)
    {
        const FMemoryAllocationInfo& Info = AllocationPair.Value;
        CalculatedTotals.FindOrAdd(Info.CategoryName) += Info.SizeInBytes;
    }
    
    // Check if totals match
    for (const auto& Category : CalculatedTotals)
    {
        const FName& CategoryName = Category.Key;
        const uint64 CalculatedSize = Category.Value;
        
        const uint64* StoredSize = MemoryUsageByCategory.Find(CategoryName);
        
        if (!StoredSize || *StoredSize != CalculatedSize)
        {
            OutErrors.Add(FString::Printf(TEXT("Memory Telemetry: Category '%s' size mismatch - Stored: %llu, Calculated: %llu"),
                *CategoryName.ToString(), StoredSize ? *StoredSize : 0, CalculatedSize));
            bValid = false;
        }
    }
    
    return bValid;
}

void FMemoryTelemetry::UpdateMemoryStats() const
{
    if (!bIsInitialized)
    {
        return;
    }
    
    FScopeLock Lock(&AllocationLock);
    
    // Only update if necessary
    if (!bStatsDirty)
    {
        return;
    }
    
    // Clear previous totals
    MemoryUsageByCategory.Empty();
    AllocationCountByCategory.Empty();
    MemoryUsageByTier.Empty();
    MemoryUsageByAccessPattern.Empty();
    
    // Calculate totals by category, tier, and access pattern
    for (const auto& AllocationPair : Allocations)
    {
        const FMemoryAllocationInfo& Info = AllocationPair.Value;
        
        // Update by category
        MemoryUsageByCategory.FindOrAdd(Info.CategoryName) += Info.SizeInBytes;
        AllocationCountByCategory.FindOrAdd(Info.CategoryName)++;
        
        // Update by tier
        MemoryUsageByTier.FindOrAdd(Info.MemoryTier) += Info.SizeInBytes;
        
        // Update by access pattern
        MemoryUsageByAccessPattern.FindOrAdd(Info.AccessPattern) += Info.SizeInBytes;
    }
    
    // Update platform memory stats
    FPlatformMemoryStats PlatformStats = FPlatformMemory::GetStats();
    
    // Update cached stats
    CachedMemoryStats.TotalAllocatedBytes = TotalAllocatedMemory;
    CachedMemoryStats.AllocationCount = ActiveAllocationCount;
    CachedMemoryStats.PeakMemoryUsage = PeakMemoryUsage;
    CachedMemoryStats.AvailablePhysicalMemory = PlatformStats.AvailablePhysical;
    CachedMemoryStats.TotalPhysicalMemory = PlatformStats.TotalPhysical;
    CachedMemoryStats.VirtualMemoryUsage = PlatformStats.UsedVirtual;
    
    CachedMemoryStats.UsageByCategory = MemoryUsageByCategory;
    
    // Fix the TMap type mismatch - convert the AllocationCountByCategory values to uint64
    for (const auto& Pair : AllocationCountByCategory)
    {
        const FName& CategoryName = Pair.Key;
        uint64 Count = static_cast<uint64>(Pair.Value); // Convert uint32 to uint64
        CachedMemoryStats.AllocationCountByCategory.Add(CategoryName, Count);
    }
    
    CachedMemoryStats.UsageByTier = MemoryUsageByTier;
    CachedMemoryStats.UsageByAccessPattern = MemoryUsageByAccessPattern;
    
    // Copy budgets to stats
    {
        FScopeLock BudgetScopeLock(&this->BudgetLock); // Renamed Lock to BudgetScopeLock to avoid variable hiding
        CachedMemoryStats.BudgetByCategory = MemoryBudgets;
    }
    
    // Update timestamp
    LastStatsUpdateTime = FPlatformTime::Seconds();
    
    // Mark as clean
    bStatsDirty = false;
}

FMemoryStats FMemoryTelemetry::GetMemoryStats() const
{
    if (bStatsDirty)
    {
        UpdateMemoryStats();
    }
    
    return CachedMemoryStats;
}

FSVOSDFMemoryMetrics FMemoryTelemetry::GetSVOSDFMemoryMetrics() const
{
    return CachedSVOSDFMetrics;
}

void FMemoryTelemetry::SetMemoryBudget(const FName& CategoryName, uint64 BudgetInBytes)
{
    FScopeLock Lock(&BudgetLock);
    MemoryBudgets.FindOrAdd(CategoryName) = BudgetInBytes;
    bStatsDirty = true;
}

uint64 FMemoryTelemetry::GetMemoryBudget(const FName& CategoryName) const
{
    FScopeLock Lock(&BudgetLock);
    const uint64* Budget = MemoryBudgets.Find(CategoryName);
    return Budget ? *Budget : 0;
}

uint64 FMemoryTelemetry::GetMemoryUsage(const FName& CategoryName) const
{
    if (bStatsDirty)
    {
        UpdateMemoryStats();
    }
    
    if (CategoryName.IsNone())
    {
        // Return total memory usage
        return TotalAllocatedMemory;
    }
    
    FScopeLock Lock(&AllocationLock);
    const uint64* Usage = MemoryUsageByCategory.Find(CategoryName);
    return Usage ? *Usage : 0;
}

void FMemoryTelemetry::CaptureCallStack(TArray<FString>& OutCallStack, uint32 SkipFrames) const
{
    // Implementation would capture call stack based on platform
    // This is a stub implementation for now
    OutCallStack.Empty();
}

void FMemoryTelemetry::ResetStatistics()
{
    FScopeLock Lock(&AllocationLock);
    
    // Don't reset Allocations map, just the statistics
    TotalAllocationCount = ActiveAllocationCount;
    TotalAllocatedMemory = 0;
    PeakMemoryUsage = 0;
    
    // Recalculate total allocated memory from current allocations
    for (const auto& Pair : Allocations)
    {
        TotalAllocatedMemory += Pair.Value.SizeInBytes;
    }
    
    PeakMemoryUsage = TotalAllocatedMemory;
    
    // Mark stats as dirty
    bStatsDirty = true;
}

bool FMemoryTelemetry::SetAllocationTag(void* Ptr, const FName& Tag)
{
    if (!bIsInitialized || !Ptr) return false;
    FScopeLock Lock(&AllocationLock);
    FMemoryAllocationInfo* Info = Allocations.Find(Ptr);
    if (!Info) return false;
    Info->AllocationName = Tag;
    bStatsDirty = true;
    return true;
}

bool FMemoryTelemetry::SetAllocationTier(void* Ptr, EMemoryTier Tier)
{
    if (!bIsInitialized || !Ptr) return false;
    FScopeLock Lock(&AllocationLock);
    FMemoryAllocationInfo* Info = Allocations.Find(Ptr);
    if (!Info) return false;
    Info->MemoryTier = Tier;
    bStatsDirty = true;
    return true;
}

bool FMemoryTelemetry::SetAllocationAccessPattern(void* Ptr, EMemoryAccessPattern Pattern)
{
    if (!bIsInitialized || !Ptr) return false;
    FScopeLock Lock(&AllocationLock);
    FMemoryAllocationInfo* Info = Allocations.Find(Ptr);
    if (!Info) return false;
    Info->AccessPattern = Pattern;
    bStatsDirty = true;
    return true;
}

void FMemoryTelemetry::EnableCallStackTracking(bool bEnable, uint32 Depth)
{
    FScopeLock Lock(&AllocationLock);
    bCallStackTrackingEnabled = bEnable;
    StackTraceDepth = Depth;
}

TArray<FMemoryAllocationInfo> FMemoryTelemetry::GetAllocationsByCategory(const FName& CategoryName) const
{
    TArray<FMemoryAllocationInfo> Result;
    if (!bIsInitialized || CategoryName.IsNone()) return Result;
    FScopeLock Lock(&AllocationLock);
    for (const auto& Pair : Allocations) {
        if (Pair.Value.CategoryName == CategoryName) Result.Add(Pair.Value);
    }
    return Result;
}

TArray<FMemoryAllocationInfo> FMemoryTelemetry::GetAllocationsByTag(const FName& Tag) const
{
    TArray<FMemoryAllocationInfo> Result;
    if (!bIsInitialized || Tag.IsNone()) return Result;
    FScopeLock Lock(&AllocationLock);
    for (const auto& Pair : Allocations) {
        if (Pair.Value.AllocationName == Tag) Result.Add(Pair.Value);
    }
    return Result;
}

TArray<FMemoryAllocationInfo> FMemoryTelemetry::GetAllocationsByTier(EMemoryTier Tier) const
{
    TArray<FMemoryAllocationInfo> Result;
    if (!bIsInitialized) return Result;
    FScopeLock Lock(&AllocationLock);
    for (const auto& Pair : Allocations) {
        if (Pair.Value.MemoryTier == Tier) Result.Add(Pair.Value);
    }
    return Result;
}

TArray<FMemoryAllocationInfo> FMemoryTelemetry::GetAllocationsBySize(uint64 MinSizeInBytes) const
{
    TArray<FMemoryAllocationInfo> Result;
    if (!bIsInitialized) return Result;
    FScopeLock Lock(&AllocationLock);
    for (const auto& Pair : Allocations) {
        if (Pair.Value.SizeInBytes >= MinSizeInBytes) Result.Add(Pair.Value);
    }
    return Result;
}

FString FMemoryTelemetry::GenerateMemoryReport(bool bDetailed) const
{
    FString Report;
    FScopeLock Lock(&AllocationLock);
    Report += FString::Printf(TEXT("Total Allocated: %llu bytes\nPeak Usage: %llu bytes\nActive Allocations: %u\n"),
        TotalAllocatedMemory, PeakMemoryUsage, ActiveAllocationCount);
    if (bDetailed) {
        for (const auto& Pair : Allocations) {
            Report += FString::Printf(TEXT("Ptr: %p Size: %llu Category: %s Name: %s Tier: %d\n"),
                Pair.Key, Pair.Value.SizeInBytes, *Pair.Value.CategoryName.ToString(), *Pair.Value.AllocationName.ToString(), (int32)Pair.Value.MemoryTier);
        }
    }
    return Report;
}

void FMemoryTelemetry::LogMemoryStatistics(bool bDetailed) const
{
    FString Report = GenerateMemoryReport(bDetailed);
    UE_LOG(LogTemp, Log, TEXT("MemoryTelemetry Report:\n%s"), *Report);
}

void FMemoryTelemetry::UpdateSVOSDFMetrics(const FSVOSDFMemoryMetrics& Metrics)
{
    FScopeLock Lock(&AllocationLock);
    CachedSVOSDFMetrics = Metrics;
    bSVOSDFMetricsDirty = false;
}

uint64 FMemoryTelemetry::GetPeakMemoryUsage() const
{
    FScopeLock Lock(&AllocationLock);
    return PeakMemoryUsage;
}

uint64 FMemoryTelemetry::GetAverageAllocationSize() const
{
    FScopeLock Lock(&AllocationLock);
    return ActiveAllocationCount > 0 ? TotalAllocatedMemory / ActiveAllocationCount : 0;
}

TMap<uint64, uint32> FMemoryTelemetry::GetAllocationSizeHistogram() const
{
    FScopeLock Lock(&AllocationLock);
    
    TMap<uint64, uint32> Histogram;
    
    // Define size buckets (powers of 2)
    const uint64 Buckets[] = {
        16, 32, 64, 128, 256, 512, 
        1024, 2048, 4096, 8192, 16384, 32768, 65536, 
        131072, 262144, 524288, 1048576, // 1MB
        2097152, 4194304, 8388608, 16777216, 33554432, 67108864, // Up to 64MB
        UINT64_MAX // Catch-all bucket for very large allocations
    };
    
    // Initialize buckets
    for (int32 i = 0; i < UE_ARRAY_COUNT(Buckets); ++i)
    {
        Histogram.Add(Buckets[i], 0);
    }
    
    // Count allocations by size
    for (const auto& Pair : Allocations)
    {
        const uint64 Size = Pair.Value.SizeInBytes;
        
        // Find appropriate bucket
        for (int32 i = 0; i < UE_ARRAY_COUNT(Buckets); ++i)
        {
            if (Size <= Buckets[i])
            {
                Histogram[Buckets[i]]++;
                break;
            }
        }
    }
    
    return Histogram;
}

TArray<TPair<double, uint64>> FMemoryTelemetry::GetMemoryUsageTimeline() const
{
    FScopeLock Lock(&AllocationLock);
    return MemoryTimeline;
}

TMap<int32, uint64> FMemoryTelemetry::GetMemoryUsageByNUMANode() const
{
    FScopeLock Lock(&AllocationLock);
    
    TMap<int32, uint64> NumaUsage;
    
    // Get NUMA node for each allocation
    for (const auto& Pair : Allocations)
    {
        const void* Ptr = Pair.Key;
        const uint64 Size = Pair.Value.SizeInBytes;
        
        // Use a default NUMA node value since GetPoolNumaNode is not available
        int32 NumaNode = 0; // Default to node 0
        
        // Add to the appropriate NUMA node bucket
        if (!NumaUsage.Contains(NumaNode))
        {
            NumaUsage.Add(NumaNode, 0);
        }
        NumaUsage[NumaNode] += Size;
    }
    
    return NumaUsage;
}

float FMemoryTelemetry::GetMemoryPressure() const
{
    FScopeLock Lock(&AllocationLock);
    
    // Get system memory stats
    FPlatformMemoryStats MemStats = FPlatformMemory::GetStats();
    
    // Calculate memory pressure as a ratio of used physical memory to total physical memory
    if (MemStats.TotalPhysical > 0)
    {
        const uint64 UsedPhysical = MemStats.TotalPhysical - MemStats.AvailablePhysical;
        return FMath::Clamp(static_cast<float>(UsedPhysical) / MemStats.TotalPhysical, 0.0f, 1.0f);
    }
    
    return 0.0f;
}

void FMemoryTelemetry::TakeMemorySnapshot(const FString& SnapshotName)
{
    FScopeLock Lock(&AllocationLock);
    
    // Ensure stats are up to date
    if (bStatsDirty)
    {
        UpdateMemoryStats();
    }
    
    // Store current memory stats in snapshot
    MemorySnapshots.Add(SnapshotName, CachedMemoryStats);
    
    // Add memory timeline entry
    const double CurrentTime = FPlatformTime::Seconds();
    MemoryTimeline.Add(TPair<double, uint64>(CurrentTime, TotalAllocatedMemory));
    
    // Prune timeline if it gets too large
    const int32 MaxTimelineEntries = 1000;
    if (MemoryTimeline.Num() > MaxTimelineEntries)
    {
        MemoryTimeline.RemoveAt(0, MemoryTimeline.Num() - MaxTimelineEntries);
    }
}

FString FMemoryTelemetry::CompareWithSnapshot(const FString& SnapshotName) const
{
    FScopeLock Lock(&AllocationLock);
    
    // Ensure stats are up to date
    if (bStatsDirty)
    {
        UpdateMemoryStats();
    }
    
    FString Result;
    
    // Find the snapshot
    const FMemoryStats* Snapshot = MemorySnapshots.Find(SnapshotName);
    if (!Snapshot)
    {
        return FString::Printf(TEXT("Snapshot '%s' not found"), *SnapshotName);
    }
    
    // Compare current stats with snapshot
    const int64 TotalDiff = static_cast<int64>(CachedMemoryStats.TotalAllocatedBytes) - static_cast<int64>(Snapshot->TotalAllocatedBytes);
    const int64 CountDiff = static_cast<int64>(CachedMemoryStats.AllocationCount) - static_cast<int64>(Snapshot->AllocationCount);
    
    Result += FString::Printf(TEXT("Memory comparison with snapshot '%s':\n"), *SnapshotName);
    Result += FString::Printf(TEXT("Total memory: %lld bytes (%s%lld bytes, %s%.2f%%)\n"),
        CachedMemoryStats.TotalAllocatedBytes,
        TotalDiff >= 0 ? TEXT("+") : TEXT(""),
        TotalDiff,
        TotalDiff >= 0 ? TEXT("+") : TEXT(""),
        Snapshot->TotalAllocatedBytes > 0 ? 
            100.0f * TotalDiff / static_cast<float>(Snapshot->TotalAllocatedBytes) : 0.0f);
    
    Result += FString::Printf(TEXT("Allocation count: %llu (%s%lld, %s%.2f%%)\n"),
        CachedMemoryStats.AllocationCount,
        CountDiff >= 0 ? TEXT("+") : TEXT(""),
        CountDiff,
        CountDiff >= 0 ? TEXT("+") : TEXT(""),
        Snapshot->AllocationCount > 0 ? 
            100.0f * CountDiff / static_cast<float>(Snapshot->AllocationCount) : 0.0f);
    
    // Compare by category
    Result += TEXT("\nBy category:\n");
    
    TSet<FName> AllCategories;
    
    // Collect all category names from both current and snapshot
    for (const auto& Pair : CachedMemoryStats.UsageByCategory)
    {
        AllCategories.Add(Pair.Key);
    }
    
    for (const auto& Pair : Snapshot->UsageByCategory)
    {
        AllCategories.Add(Pair.Key);
    }
    
    // Compare each category
    for (const FName& Category : AllCategories)
    {
        const uint64 CurrentUsage = CachedMemoryStats.UsageByCategory.Contains(Category) ?
            CachedMemoryStats.UsageByCategory[Category] : 0;
        
        const uint64 SnapshotUsage = Snapshot->UsageByCategory.Contains(Category) ?
            Snapshot->UsageByCategory[Category] : 0;
        
        const int64 Diff = static_cast<int64>(CurrentUsage) - static_cast<int64>(SnapshotUsage);
        
        if (Diff != 0 || (CurrentUsage > 0 && SnapshotUsage > 0))
        {
            Result += FString::Printf(TEXT("  %s: %llu bytes (%s%lld bytes, %s%.2f%%)\n"),
                *Category.ToString(),
                CurrentUsage,
                Diff >= 0 ? TEXT("+") : TEXT(""),
                Diff,
                Diff >= 0 ? TEXT("+") : TEXT(""),
                SnapshotUsage > 0 ? 100.0f * Diff / static_cast<float>(SnapshotUsage) : 0.0f);
        }
    }
    
    return Result;
}

float FMemoryTelemetry::CalculateFragmentation() const
{
    // Default fragmentation metric based on allocation pattern
    // This is a simplified version - a real implementation would use more sophisticated metrics
    
    // Count contiguous allocations
    uint32 ContiguousGroups = 0;
    uint32 TotalAllocations = 0;
    
    // Sort allocations by address for analysis
    struct FAllocationAddr
    {
        void* Ptr;
        uint64 Size;
        
        bool operator<(const FAllocationAddr& Other) const
        {
            return Ptr < Other.Ptr;
        }
    };
    
    TArray<FAllocationAddr> SortedAllocations;
    
    {
        FScopeLock Lock(&AllocationLock);
        
        for (const auto& Pair : Allocations)
        {
            FAllocationAddr Addr;
            Addr.Ptr = Pair.Key;
            Addr.Size = Pair.Value.SizeInBytes;
            SortedAllocations.Add(Addr);
        }
        
        TotalAllocations = SortedAllocations.Num();
    }
    
    // Early out if too few allocations
    if (TotalAllocations < 2)
    {
        return 0.0f;
    }
    
    // Sort by address
    SortedAllocations.Sort();
    
    // Analyze contiguity
    bool bInContiguousGroup = false;
    for (int32 i = 1; i < SortedAllocations.Num(); ++i)
    {
        const uint8* PrevEnd = static_cast<const uint8*>(SortedAllocations[i-1].Ptr) + SortedAllocations[i-1].Size;
        const uint8* CurrStart = static_cast<const uint8*>(SortedAllocations[i].Ptr);
        
        // Check if allocations are contiguous (with some tolerance for alignment)
        const ptrdiff_t Gap = CurrStart - PrevEnd;
        
        if (Gap <= 64) // Allow small gaps for alignment
        {
            if (!bInContiguousGroup)
            {
                bInContiguousGroup = true;
                ContiguousGroups++;
            }
        }
        else
        {
            bInContiguousGroup = false;
        }
    }
    
    // Calculate fragmentation as 1 - (contiguous groups / total allocations)
    // Perfect: All allocations in one contiguous group = 0% fragmentation
    // Worst: No contiguous allocations = 100% fragmentation
    if (ContiguousGroups > 0)
    {
        // Adjust by adding 1 to account for the first allocation in each group
        const float IdealGroups = 1.0f;
        const float ActualGroups = static_cast<float>(TotalAllocations) / (ContiguousGroups + 1);
        return 100.0f * (1.0f - (IdealGroups / ActualGroups));
    }
    
    return 100.0f; // Maximum fragmentation
}

bool FMemoryTelemetry::TrackPool(IPoolAllocator* Pool, const FName& Category)
{
    if (!bIsInitialized || !Pool)
    {
        return false;
    }
    
    FScopeLock Lock(&AllocationLock);
    
    // Get pool statistics
    FPoolStats PoolStats = Pool->GetStats();
    
    // Track the pool overall memory usage
    uint64 TotalPoolMemory = PoolStats.BlockSize * PoolStats.BlockCount + PoolStats.OverheadBytes;
    
    // Create a unique identifier for the pool
    void* PoolPtr = static_cast<void*>(Pool);
    FName PoolCategory = Category.IsNone() ? FName(TEXT("Memory_Pools")) : Category;
    FName AllocationName = Pool->GetPoolName();
    
    // Track the pool as a special allocation
    FMemoryAllocationInfo Info;
    Info.Ptr = PoolPtr;
    Info.SizeInBytes = TotalPoolMemory;
    Info.CategoryName = PoolCategory;
    Info.AllocationName = AllocationName;
    Info.TimeStamp = FPlatformTime::Seconds();
    Info.PoolName = AllocationName;
    Info.MemoryTier = EMemoryTier::Warm;
    Info.AccessPattern = Pool->GetAccessPattern();
    
    // Add to tracking
    Allocations.Add(PoolPtr, Info);
    
    // Update statistics
    TotalAllocatedMemory += TotalPoolMemory;
    PeakMemoryUsage = FMath::Max(PeakMemoryUsage, TotalAllocatedMemory);
    TotalAllocationCount++;
    ActiveAllocationCount++;
    
    // Mark stats as dirty
    bStatsDirty = true;
    
    return true;
}