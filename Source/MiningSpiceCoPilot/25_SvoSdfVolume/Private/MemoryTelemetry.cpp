// MemoryTelemetry.cpp
// Implementation of memory usage tracking and analysis for SVO+SDF volume system

#include "../Public/MemoryTelemetry.h"
#include "../Public/SVOHybridVolume.h"
#include "../Public/OctreeNodeManager.h"
#include "../Public/MaterialSDFManager.h"
#include "../Public/NarrowBandAllocator.h"
#include "MiningSpiceCoPilot/2_MemoryManagement/Public/Interfaces/IMemoryTracker.h"
#include "Misc/ScopeLock.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFilemanager.h"

// Internal data structures
struct FMemoryTelemetry::FCategoryStats
{
    uint64 AllocatedBytes;
    uint64 PeakBytes;
    uint32 AllocationCount;
    uint32 DeallocationCount;
    
    FCategoryStats()
        : AllocatedBytes(0)
        , PeakBytes(0)
        , AllocationCount(0)
        , DeallocationCount(0)
    {}
};

struct FMemoryTelemetry::FMemorySnapshot
{
    double Timestamp;
    uint64 TotalMemoryUsage;
    uint64 OctreeStructureMemory;
    uint64 SDFFieldDataMemory;
    uint64 MaterialDataMemory;
    uint64 NetworkBufferMemory;
    uint64 CacheMemory;
    TMap<FString, uint64> CategoryMemory;
    TMap<uint8, uint64> MaterialMemory;
    TMap<int32, uint32> NodesByLevel;
    float FragmentationRatio;
    
    FMemorySnapshot()
        : Timestamp(0.0)
        , TotalMemoryUsage(0)
        , OctreeStructureMemory(0)
        , SDFFieldDataMemory(0)
        , MaterialDataMemory(0)
        , NetworkBufferMemory(0)
        , CacheMemory(0)
        , FragmentationRatio(0.0f)
    {}
};

FMemoryTelemetry::FMemoryTelemetry()
    : Volume(nullptr)
    , NodeManager(nullptr)
    , MaterialManager(nullptr)
    , NarrowBand(nullptr)
    , TotalAllocatedMemory(0)
    , MemoryBudget(UINT64_MAX)
{
    // Initialize with default categories
    CategoryStatistics.Add("Octree", FCategoryStats());
    CategoryStatistics.Add("SDF", FCategoryStats());
    CategoryStatistics.Add("Material", FCategoryStats());
    CategoryStatistics.Add("Network", FCategoryStats());
    CategoryStatistics.Add("Cache", FCategoryStats());
    CategoryStatistics.Add("Misc", FCategoryStats());
}

FMemoryTelemetry::~FMemoryTelemetry()
{
    // Clean up any resources
}

void FMemoryTelemetry::Initialize(USVOHybridVolume* InVolume, FOctreeNodeManager* InNodeManager, 
                                  FMaterialSDFManager* InMaterialManager, FNarrowBandAllocator* InNarrowBand)
{
    Volume = InVolume;
    NodeManager = InNodeManager;
    MaterialManager = InMaterialManager;
    NarrowBand = InNarrowBand;
    
    // Register with the memory tracking system
    IMemoryTracker* MemTracker = IMemoryTracker::Get();
    if (MemTracker)
    {
        MemTracker->RegisterComponent("SVOHybridVolume", this);
    }
    
    // Take an initial snapshot
    RecordMemorySnapshot("Initial");
}

void FMemoryTelemetry::TrackAllocation(uint64 Size, const FString& Category)
{
    FScopeLock Lock(&StatisticsLock);
    
    // Update category stats
    if (!CategoryStatistics.Contains(Category))
    {
        CategoryStatistics.Add(Category, FCategoryStats());
    }
    
    FCategoryStats& Stats = CategoryStatistics[Category];
    Stats.AllocatedBytes += Size;
    Stats.AllocationCount++;
    Stats.PeakBytes = FMath::Max(Stats.PeakBytes, Stats.AllocatedBytes);
    
    // Update total memory usage
    TotalAllocatedMemory.Add(Size);
}

void FMemoryTelemetry::TrackDeallocation(uint64 Size, const FString& Category)
{
    FScopeLock Lock(&StatisticsLock);
    
    if (CategoryStatistics.Contains(Category))
    {
        FCategoryStats& Stats = CategoryStatistics[Category];
        Stats.AllocatedBytes -= Size;
        Stats.DeallocationCount++;
    }
    
    // Update total memory usage
    TotalAllocatedMemory.Subtract(Size);
}

void FMemoryTelemetry::TrackResize(uint64 OldSize, uint64 NewSize, const FString& Category)
{
    FScopeLock Lock(&StatisticsLock);
    
    if (!CategoryStatistics.Contains(Category))
    {
        CategoryStatistics.Add(Category, FCategoryStats());
    }
    
    FCategoryStats& Stats = CategoryStatistics[Category];
    
    // Update bytes allocated/deallocated
    if (NewSize > OldSize)
    {
        Stats.AllocatedBytes += (NewSize - OldSize);
        TotalAllocatedMemory.Add(NewSize - OldSize);
    }
    else
    {
        Stats.AllocatedBytes -= (OldSize - NewSize);
        TotalAllocatedMemory.Subtract(OldSize - NewSize);
    }
    
    // Update peak
    Stats.PeakBytes = FMath::Max(Stats.PeakBytes, Stats.AllocatedBytes);
}

FMemoryTelemetry::FMemoryStats FMemoryTelemetry::GetMemoryStatistics() const
{
    FScopeLock Lock(&StatisticsLock);
    
    // Update statistics from components
    const_cast<FMemoryTelemetry*>(this)->UpdateNodeStatistics();
    const_cast<FMemoryTelemetry*>(this)->UpdateMaterialStatistics();
    const_cast<FMemoryTelemetry*>(this)->CalculateFragmentationRatio();
    
    FMemoryStats Stats;
    
    // Fill in the memory stats from our tracked categories
    Stats.TotalMemoryUsage = TotalAllocatedMemory.GetValue();
    
    if (CategoryStatistics.Contains("Octree"))
    {
        Stats.OctreeStructureMemory = CategoryStatistics["Octree"].AllocatedBytes;
    }
    
    if (CategoryStatistics.Contains("SDF"))
    {
        Stats.SDFFieldDataMemory = CategoryStatistics["SDF"].AllocatedBytes;
    }
    
    if (CategoryStatistics.Contains("Material"))
    {
        Stats.MaterialDataMemory = CategoryStatistics["Material"].AllocatedBytes;
    }
    
    if (CategoryStatistics.Contains("Network"))
    {
        Stats.NetworkBufferMemory = CategoryStatistics["Network"].AllocatedBytes;
    }
    
    if (CategoryStatistics.Contains("Cache"))
    {
        Stats.CacheMemory = CategoryStatistics["Cache"].AllocatedBytes;
    }
    
    // Get node stats from the octree manager
    if (NodeManager)
    {
        FOctreeNodeManager::FOctreeStats OctreeStats = NodeManager->GetStatistics();
        Stats.NodeCount = OctreeStats.TotalNodes;
        Stats.ActiveNodeCount = OctreeStats.TotalNodes - OctreeStats.EmptyNodes;
        Stats.NodesByLevel = OctreeStats.NodesByDepth;
    }
    
    // Get material stats
    if (MaterialManager)
    {
        Stats.MaterialChannelCount = MaterialManager->GetChannelCount();
        
        // Convert material memory map
        TMap<uint8, uint64> MaterialMemory = MaterialManager->GetMemoryByMaterial();
        Stats.MemoryByMaterial = MaterialMemory;
    }
    
    // Calculate fragmentation ratio
    Stats.FragmentationRatio = GetMemoryUtilizationRatio();
    
    return Stats;
}

uint64 FMemoryTelemetry::GetTotalMemoryUsage() const
{
    return TotalAllocatedMemory.GetValue();
}

uint64 FMemoryTelemetry::GetMemoryUsageByCategory(const FString& Category) const
{
    FScopeLock Lock(&StatisticsLock);
    
    if (CategoryStatistics.Contains(Category))
    {
        return CategoryStatistics[Category].AllocatedBytes;
    }
    
    return 0;
}

float FMemoryTelemetry::GetMemoryUtilizationRatio() const
{
    if (NarrowBand)
    {
        return NarrowBand->GetFragmentationRatio();
    }
    
    // If NarrowBand is not available, estimate from our own tracking
    uint64 Total = TotalAllocatedMemory.GetValue();
    uint64 Used = 0;
    
    for (const auto& Pair : CategoryStatistics)
    {
        Used += Pair.Value.AllocatedBytes;
    }
    
    return Total > 0 ? ((float)Used / Total) : 1.0f;
}

void FMemoryTelemetry::AnalyzeMemoryUsage()
{
    UpdateNodeStatistics();
    UpdateMaterialStatistics();
    CalculateFragmentationRatio();
    AnalyzeMemoryHotspots();
    GenerateOptimizationRecommendations();
}

TArray<FString> FMemoryTelemetry::GetOptimizationRecommendations() const
{
    // Return cached recommendations
    TArray<FString> Recommendations;
    
    // Check overall memory pressure
    uint64 Total = TotalAllocatedMemory.GetValue();
    float BudgetRatio = GetBudgetUtilizationRatio();
    
    if (BudgetRatio > 0.9f)
    {
        Recommendations.Add(FString::Printf(TEXT("Critical memory pressure: %.1f%% of budget used. Consider reducing material channel count or octree depth."), BudgetRatio * 100.0f));
    }
    else if (BudgetRatio > 0.75f)
    {
        Recommendations.Add(FString::Printf(TEXT("High memory usage: %.1f%% of budget used."), BudgetRatio * 100.0f));
    }
    
    // Check fragmentation
    float FragRatio = GetMemoryUtilizationRatio();
    if (FragRatio < 0.6f)
    {
        Recommendations.Add(FString::Printf(TEXT("High memory fragmentation detected (%.1f%% utilization). Consider running OptimizeMemoryUsage()."), FragRatio * 100.0f));
    }
    
    // Check octree balance if we have node stats
    if (NodeManager)
    {
        FOctreeNodeManager::FOctreeStats OctreeStats = NodeManager->GetStatistics();
        
        // Check for excessive depth
        if (OctreeStats.MaxDepth > 12)
        {
            Recommendations.Add(FString::Printf(TEXT("Octree maximum depth %d is very high. Consider limiting maximum subdivision depth."), OctreeStats.MaxDepth));
        }
        
        // Check leaf/interface node ratio
        float InterfaceRatio = OctreeStats.InterfaceNodes > 0 ? ((float)OctreeStats.InterfaceNodes / OctreeStats.TotalNodes) : 0.0f;
        if (InterfaceRatio < 0.1f)
        {
            Recommendations.Add(TEXT("Low interface node ratio. Consider increasing narrow band width to improve memory efficiency."));
        }
        else if (InterfaceRatio > 0.7f)
        {
            Recommendations.Add(TEXT("High interface node ratio. Volume may be too detailed for current octree settings."));
        }
    }
    
    // Material-specific recommendations
    if (MaterialManager)
    {
        // Check for inactive materials consuming memory
        TArray<uint8> ActiveMaterials = MaterialManager->GetActiveMaterials();
        uint32 ChannelCount = MaterialManager->GetChannelCount();
        
        if (ActiveMaterials.Num() < ChannelCount * 0.5f)
        {
            Recommendations.Add(FString::Printf(TEXT("Only %d out of %d material channels are active. Consider reducing channel count."), 
                                                ActiveMaterials.Num(), ChannelCount));
        }
    }
    
    return Recommendations;
}

TArray<TPair<FString, float>> FMemoryTelemetry::GetMemoryBreakdownByCategory() const
{
    TArray<TPair<FString, float>> Result;
    uint64 Total = TotalAllocatedMemory.GetValue();
    
    if (Total == 0)
    {
        return Result;
    }
    
    // Sort categories by memory usage
    TArray<TPair<FString, uint64>> SortedCategories;
    
    for (const auto& Pair : CategoryStatistics)
    {
        SortedCategories.Add(TPair<FString, uint64>(Pair.Key, Pair.Value.AllocatedBytes));
    }
    
    SortedCategories.Sort([](const TPair<FString, uint64>& A, const TPair<FString, uint64>& B) {
        return A.Value > B.Value; // Sort in descending order
    });
    
    // Convert to percentage
    for (const auto& Pair : SortedCategories)
    {
        float Percentage = (float)Pair.Value / Total * 100.0f;
        Result.Add(TPair<FString, float>(Pair.Key, Percentage));
    }
    
    return Result;
}

TArray<TPair<uint8, float>> FMemoryTelemetry::GetMemoryBreakdownByMaterial() const
{
    TArray<TPair<uint8, float>> Result;
    
    if (!MaterialManager)
    {
        return Result;
    }
    
    TMap<uint8, uint64> MaterialMemory = MaterialManager->GetMemoryByMaterial();
    uint64 TotalMaterialMemory = 0;
    
    // Calculate total material memory
    for (const auto& Pair : MaterialMemory)
    {
        TotalMaterialMemory += Pair.Value;
    }
    
    if (TotalMaterialMemory == 0)
    {
        return Result;
    }
    
    // Sort materials by memory usage
    TArray<TPair<uint8, uint64>> SortedMaterials;
    
    for (const auto& Pair : MaterialMemory)
    {
        SortedMaterials.Add(Pair);
    }
    
    SortedMaterials.Sort([](const TPair<uint8, uint64>& A, const TPair<uint8, uint64>& B) {
        return A.Value > B.Value; // Sort in descending order
    });
    
    // Convert to percentage
    for (const auto& Pair : SortedMaterials)
    {
        float Percentage = (float)Pair.Value / TotalMaterialMemory * 100.0f;
        Result.Add(TPair<uint8, float>(Pair.Key, Percentage));
    }
    
    return Result;
}

TArray<TPair<int32, float>> FMemoryTelemetry::GetMemoryBreakdownByOctreeLevel() const
{
    TArray<TPair<int32, float>> Result;
    
    if (!NodeManager)
    {
        return Result;
    }
    
    FOctreeNodeManager::FOctreeStats OctreeStats = NodeManager->GetStatistics();
    TMap<int32, uint32> NodesByDepth = OctreeStats.NodesByDepth;
    uint32 TotalNodes = OctreeStats.TotalNodes;
    
    if (TotalNodes == 0)
    {
        return Result;
    }
    
    // Sort depths by node count
    TArray<TPair<int32, uint32>> SortedDepths;
    
    for (const auto& Pair : NodesByDepth)
    {
        SortedDepths.Add(Pair);
    }
    
    SortedDepths.Sort([](const TPair<int32, uint32>& A, const TPair<int32, uint32>& B) {
        return A.Value > B.Value; // Sort in descending order
    });
    
    // Convert to percentage
    for (const auto& Pair : SortedDepths)
    {
        float Percentage = (float)Pair.Value / TotalNodes * 100.0f;
        Result.Add(TPair<int32, float>(Pair.Key, Percentage));
    }
    
    return Result;
}

void FMemoryTelemetry::LogMemoryStatistics(bool Detailed)
{
    FMemoryStats Stats = GetMemoryStatistics();
    
    UE_LOG(LogTemp, Log, TEXT("=== SVO+SDF Hybrid Volume Memory Report ==="));
    UE_LOG(LogTemp, Log, TEXT("Total Memory Usage: %s"), *FormatMemorySize(Stats.TotalMemoryUsage));
    UE_LOG(LogTemp, Log, TEXT("Octree Structure: %s"), *FormatMemorySize(Stats.OctreeStructureMemory));
    UE_LOG(LogTemp, Log, TEXT("SDF Field Data: %s"), *FormatMemorySize(Stats.SDFFieldDataMemory));
    UE_LOG(LogTemp, Log, TEXT("Material Data: %s"), *FormatMemorySize(Stats.MaterialDataMemory));
    UE_LOG(LogTemp, Log, TEXT("Network Buffer: %s"), *FormatMemorySize(Stats.NetworkBufferMemory));
    UE_LOG(LogTemp, Log, TEXT("Cache Memory: %s"), *FormatMemorySize(Stats.CacheMemory));
    UE_LOG(LogTemp, Log, TEXT("Nodes: %d total, %d active"), Stats.NodeCount, Stats.ActiveNodeCount);
    UE_LOG(LogTemp, Log, TEXT("Material Channels: %d"), Stats.MaterialChannelCount);
    UE_LOG(LogTemp, Log, TEXT("Fragmentation Ratio: %.2f%%"), Stats.FragmentationRatio * 100.0f);
    
    if (Detailed)
    {
        // Log memory by category
        UE_LOG(LogTemp, Log, TEXT("--- Memory by Category ---"));
        TArray<TPair<FString, float>> CategoryBreakdown = GetMemoryBreakdownByCategory();
        for (const auto& Pair : CategoryBreakdown)
        {
            UE_LOG(LogTemp, Log, TEXT("%s: %.1f%%"), *Pair.Key, Pair.Value);
        }
        
        // Log memory by material
        UE_LOG(LogTemp, Log, TEXT("--- Memory by Material ---"));
        TArray<TPair<uint8, float>> MaterialBreakdown = GetMemoryBreakdownByMaterial();
        for (const auto& Pair : MaterialBreakdown)
        {
            UE_LOG(LogTemp, Log, TEXT("Material %d: %.1f%%"), Pair.Key, Pair.Value);
        }
        
        // Log nodes by depth
        UE_LOG(LogTemp, Log, TEXT("--- Nodes by Depth ---"));
        for (const auto& Pair : Stats.NodesByLevel)
        {
            UE_LOG(LogTemp, Log, TEXT("Depth %d: %d nodes"), Pair.Key, Pair.Value);
        }
        
        // Log optimization recommendations
        UE_LOG(LogTemp, Log, TEXT("--- Optimization Recommendations ---"));
        TArray<FString> Recommendations = GetOptimizationRecommendations();
        for (const FString& Recommendation : Recommendations)
        {
            UE_LOG(LogTemp, Log, TEXT("* %s"), *Recommendation);
        }
    }
}

void FMemoryTelemetry::ExportMemoryReport(const FString& FilePath) const
{
    FMemoryStats Stats = GetMemoryStatistics();
    
    FString Report;
    Report += TEXT("SVO+SDF Hybrid Volume Memory Report\n");
    Report += TEXT("==============================\n\n");
    Report += FString::Printf(TEXT("Total Memory Usage: %s\n"), *FormatMemorySize(Stats.TotalMemoryUsage));
    Report += FString::Printf(TEXT("Octree Structure: %s\n"), *FormatMemorySize(Stats.OctreeStructureMemory));
    Report += FString::Printf(TEXT("SDF Field Data: %s\n"), *FormatMemorySize(Stats.SDFFieldDataMemory));
    Report += FString::Printf(TEXT("Material Data: %s\n"), *FormatMemorySize(Stats.MaterialDataMemory));
    Report += FString::Printf(TEXT("Network Buffer: %s\n"), *FormatMemorySize(Stats.NetworkBufferMemory));
    Report += FString::Printf(TEXT("Cache Memory: %s\n"), *FormatMemorySize(Stats.CacheMemory));
    Report += FString::Printf(TEXT("Nodes: %d total, %d active\n"), Stats.NodeCount, Stats.ActiveNodeCount);
    Report += FString::Printf(TEXT("Material Channels: %d\n"), Stats.MaterialChannelCount);
    Report += FString::Printf(TEXT("Fragmentation Ratio: %.2f%%\n\n"), Stats.FragmentationRatio * 100.0f);
    
    // Add memory by category
    Report += TEXT("Memory by Category\n");
    Report += TEXT("-----------------\n");
    TArray<TPair<FString, float>> CategoryBreakdown = GetMemoryBreakdownByCategory();
    for (const auto& Pair : CategoryBreakdown)
    {
        Report += FString::Printf(TEXT("%s: %.1f%%\n"), *Pair.Key, Pair.Value);
    }
    Report += TEXT("\n");
    
    // Add memory by material
    Report += TEXT("Memory by Material\n");
    Report += TEXT("----------------\n");
    TArray<TPair<uint8, float>> MaterialBreakdown = GetMemoryBreakdownByMaterial();
    for (const auto& Pair : MaterialBreakdown)
    {
        Report += FString::Printf(TEXT("Material %d: %.1f%%\n"), Pair.Key, Pair.Value);
    }
    Report += TEXT("\n");
    
    // Add nodes by depth
    Report += TEXT("Nodes by Depth\n");
    Report += TEXT("-------------\n");
    for (const auto& Pair : Stats.NodesByLevel)
    {
        Report += FString::Printf(TEXT("Depth %d: %d nodes\n"), Pair.Key, Pair.Value);
    }
    Report += TEXT("\n");
    
    // Add optimization recommendations
    Report += TEXT("Optimization Recommendations\n");
    Report += TEXT("--------------------------\n");
    TArray<FString> Recommendations = GetOptimizationRecommendations();
    for (const FString& Recommendation : Recommendations)
    {
        Report += FString::Printf(TEXT("* %s\n"), *Recommendation);
    }
    
    // Save to file
    FFileHelper::SaveStringToFile(Report, *FilePath);
}

void FMemoryTelemetry::RecordMemorySnapshot(const FString& SnapshotName)
{
    FMemoryStats Stats = GetMemoryStatistics();
    
    FMemorySnapshot Snapshot;
    Snapshot.Timestamp = FPlatformTime::Seconds();
    Snapshot.TotalMemoryUsage = Stats.TotalMemoryUsage;
    Snapshot.OctreeStructureMemory = Stats.OctreeStructureMemory;
    Snapshot.SDFFieldDataMemory = Stats.SDFFieldDataMemory;
    Snapshot.MaterialDataMemory = Stats.MaterialDataMemory;
    Snapshot.NetworkBufferMemory = Stats.NetworkBufferMemory;
    Snapshot.CacheMemory = Stats.CacheMemory;
    Snapshot.FragmentationRatio = Stats.FragmentationRatio;
    
    // Add category memory
    for (const auto& Pair : CategoryStatistics)
    {
        Snapshot.CategoryMemory.Add(Pair.Key, Pair.Value.AllocatedBytes);
    }
    
    // Add material memory
    Snapshot.MaterialMemory = Stats.MemoryByMaterial;
    
    // Add node depth distribution
    Snapshot.NodesByLevel = Stats.NodesByLevel;
    
    // Store the snapshot
    MemorySnapshots.Add(SnapshotName, Snapshot);
}

void FMemoryTelemetry::CompareSnapshots(const FString& BaseSnapshot, const FString& ComparisonSnapshot)
{
    if (!MemorySnapshots.Contains(BaseSnapshot) || !MemorySnapshots.Contains(ComparisonSnapshot))
    {
        UE_LOG(LogTemp, Warning, TEXT("Snapshot comparison failed: One or both snapshots not found"));
        return;
    }
    
    const FMemorySnapshot& Base = MemorySnapshots[BaseSnapshot];
    const FMemorySnapshot& Comparison = MemorySnapshots[ComparisonSnapshot];
    
    double TimeDiff = Comparison.Timestamp - Base.Timestamp;
    
    UE_LOG(LogTemp, Log, TEXT("=== Memory Snapshot Comparison ==="));
    UE_LOG(LogTemp, Log, TEXT("Base: %s, Comparison: %s, Time Difference: %.2f seconds"), *BaseSnapshot, *ComparisonSnapshot, TimeDiff);
    UE_LOG(LogTemp, Log, TEXT("Total Memory: %s -> %s (%.1f%% change)"),
           *FormatMemorySize(Base.TotalMemoryUsage),
           *FormatMemorySize(Comparison.TotalMemoryUsage),
           Base.TotalMemoryUsage > 0 ? ((float)(Comparison.TotalMemoryUsage - Base.TotalMemoryUsage) / Base.TotalMemoryUsage * 100.0f) : 0.0f);
    
    // Compare category memory
    UE_LOG(LogTemp, Log, TEXT("--- Memory by Category ---"));
    TSet<FString> AllCategories;
    
    for (const auto& Pair : Base.CategoryMemory)
    {
        AllCategories.Add(Pair.Key);
    }
    
    for (const auto& Pair : Comparison.CategoryMemory)
    {
        AllCategories.Add(Pair.Key);
    }
    
    for (const FString& Category : AllCategories)
    {
        uint64 BaseValue = Base.CategoryMemory.Contains(Category) ? Base.CategoryMemory[Category] : 0;
        uint64 CompValue = Comparison.CategoryMemory.Contains(Category) ? Comparison.CategoryMemory[Category] : 0;
        float PercentChange = BaseValue > 0 ? ((float)(CompValue - BaseValue) / BaseValue * 100.0f) : 0.0f;
        
        UE_LOG(LogTemp, Log, TEXT("%s: %s -> %s (%.1f%% change)"),
               *Category,
               *FormatMemorySize(BaseValue),
               *FormatMemorySize(CompValue),
               PercentChange);
    }
}

void FMemoryTelemetry::SetMemoryBudget(uint64 MaxMemory)
{
    MemoryBudget = MaxMemory;
}

void FMemoryTelemetry::SetMemoryBudgetByCategory(const FString& Category, uint64 MaxMemory)
{
    CategoryBudgets.Add(Category, MaxMemory);
}

bool FMemoryTelemetry::IsWithinBudget() const
{
    return GetTotalMemoryUsage() <= MemoryBudget;
}

float FMemoryTelemetry::GetBudgetUtilizationRatio() const
{
    return (float)GetTotalMemoryUsage() / MemoryBudget;
}

void FMemoryTelemetry::UpdateNodeStatistics()
{
    if (!NodeManager)
    {
        return;
    }
    
    FOctreeNodeManager::FOctreeStats OctreeStats = NodeManager->GetStatistics();
    
    // Update the octree category stats
    FCategoryStats& Stats = CategoryStatistics.FindOrAdd("Octree");
    Stats.AllocatedBytes = OctreeStats.TotalMemoryUsage;
    Stats.PeakBytes = FMath::Max(Stats.PeakBytes, Stats.AllocatedBytes);
}

void FMemoryTelemetry::UpdateMaterialStatistics()
{
    if (!MaterialManager)
    {
        return;
    }
    
    uint64 MaterialMemory = MaterialManager->GetTotalMemoryUsage();
    
    // Update the material category stats
    FCategoryStats& Stats = CategoryStatistics.FindOrAdd("Material");
    Stats.AllocatedBytes = MaterialMemory;
    Stats.PeakBytes = FMath::Max(Stats.PeakBytes, Stats.AllocatedBytes);
}

void FMemoryTelemetry::CalculateFragmentationRatio()
{
    // Most accurate fragmentation comes from NarrowBand allocator
    if (NarrowBand)
    {
        return; // NarrowBand will provide its own ratio
    }
    
    // Otherwise we'll estimate from our tracked allocations vs. actual used memory
}

void FMemoryTelemetry::AnalyzeMemoryHotspots()
{
    // Check for categories using excessive memory
    uint64 Total = TotalAllocatedMemory.GetValue();
    
    if (Total == 0)
    {
        return;
    }
    
    for (auto& Pair : CategoryStatistics)
    {
        float Percentage = (float)Pair.Value.AllocatedBytes / Total * 100.0f;
        if (Percentage > 50.0f)
        {
            UE_LOG(LogTemp, Warning, TEXT("Memory hotspot detected: %s using %.1f%% of total memory"), *Pair.Key, Percentage);
        }
    }
    
    // Check for specific materials using excessive memory
    if (MaterialManager)
    {
        TMap<uint8, uint64> MaterialMemory = MaterialManager->GetMemoryByMaterial();
        uint64 TotalMaterialMemory = 0;
        
        for (const auto& Pair : MaterialMemory)
        {
            TotalMaterialMemory += Pair.Value;
        }
        
        if (TotalMaterialMemory > 0)
        {
            for (const auto& Pair : MaterialMemory)
            {
                float Percentage = (float)Pair.Value / TotalMaterialMemory * 100.0f;
                if (Percentage > 30.0f)
                {
                    UE_LOG(LogTemp, Warning, TEXT("Material memory hotspot detected: Material %d using %.1f%% of material memory"),
                           Pair.Key, Percentage);
                }
            }
        }
    }
}

void FMemoryTelemetry::GenerateOptimizationRecommendations()
{
    // This is implemented in GetOptimizationRecommendations()
    // Here we could cache the recommendations if needed
}

FString FMemoryTelemetry::FormatMemorySize(uint64 Bytes) const
{
    const char* Units[] = {"B", "KB", "MB", "GB", "TB"};
    int UnitIndex = 0;
    double Size = Bytes;
    
    while (Size >= 1024.0 && UnitIndex < 4)
    {
        Size /= 1024.0;
        UnitIndex++;
    }
    
    return FString::Printf(TEXT("%.2f %s"), Size, ANSI_TO_TCHAR(Units[UnitIndex]));
}