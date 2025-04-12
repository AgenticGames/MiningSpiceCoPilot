// MemoryTelemetry.h
// Memory usage tracking and analysis for SVO+SDF volume system

#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeCounter64.h"
#include "Misc/ScopeLock.h"

// Forward declarations
class USVOHybridVolume;
class FOctreeNodeManager;
class FMaterialSDFManager;
class FNarrowBandAllocator;

/**
 * Memory usage tracking and analysis for SVO+SDF volume system
 * Provides detailed memory statistics, optimization recommendations,
 * and usage tracking for different volume components
 */
class MININGSPICECOPILOT_API FMemoryTelemetry
{
public:
    FMemoryTelemetry();
    ~FMemoryTelemetry();

    // Memory usage statistics structure
    struct FMemoryStats
    {
        uint64 TotalMemoryUsage;
        uint64 OctreeStructureMemory;
        uint64 SDFFieldDataMemory;
        uint64 MaterialDataMemory;
        uint64 NetworkBufferMemory;
        uint64 CacheMemory;
        float FragmentationRatio;
        uint32 NodeCount;
        uint32 ActiveNodeCount;
        uint32 MaterialChannelCount;
        TMap<uint8, uint64> MemoryByMaterial;
        TMap<int32, uint32> NodesByLevel;
    };

    // Initialization
    void Initialize(USVOHybridVolume* InVolume, FOctreeNodeManager* InNodeManager, 
                   FMaterialSDFManager* InMaterialManager, FNarrowBandAllocator* InNarrowBand);
    
    // Memory tracking
    void TrackAllocation(uint64 Size, const FString& Category);
    void TrackDeallocation(uint64 Size, const FString& Category);
    void TrackResize(uint64 OldSize, uint64 NewSize, const FString& Category);
    
    // Memory statistics
    FMemoryStats GetMemoryStatistics() const;
    uint64 GetTotalMemoryUsage() const;
    uint64 GetMemoryUsageByCategory(const FString& Category) const;
    float GetMemoryUtilizationRatio() const;
    
    // Memory analysis
    void AnalyzeMemoryUsage();
    TArray<FString> GetOptimizationRecommendations() const;
    TArray<TPair<FString, float>> GetMemoryBreakdownByCategory() const;
    TArray<TPair<uint8, float>> GetMemoryBreakdownByMaterial() const;
    TArray<TPair<int32, float>> GetMemoryBreakdownByOctreeLevel() const;
    
    // Logging and reporting
    void LogMemoryStatistics(bool Detailed = false);
    void ExportMemoryReport(const FString& FilePath) const;
    void RecordMemorySnapshot(const FString& SnapshotName);
    void CompareSnapshots(const FString& BaseSnapshot, const FString& ComparisonSnapshot);
    
    // Memory budget management
    void SetMemoryBudget(uint64 MaxMemory);
    void SetMemoryBudgetByCategory(const FString& Category, uint64 MaxMemory);
    bool IsWithinBudget() const;
    float GetBudgetUtilizationRatio() const;

private:
    // Internal data structures
    struct FCategoryStats;
    struct FMemorySnapshot;
    
    // Implementation details
    USVOHybridVolume* Volume;
    FOctreeNodeManager* NodeManager;
    FMaterialSDFManager* MaterialManager;
    FNarrowBandAllocator* NarrowBand;
    TMap<FString, FCategoryStats> CategoryStatistics;
    TMap<FString, FMemorySnapshot> MemorySnapshots;
    FThreadSafeCounter64 TotalAllocatedMemory;
    uint64 MemoryBudget;
    TMap<FString, uint64> CategoryBudgets;
    FCriticalSection StatisticsLock;
    
    // Helper methods
    void UpdateNodeStatistics();
    void UpdateMaterialStatistics();
    void CalculateFragmentationRatio();
    void AnalyzeMemoryHotspots();
    void GenerateOptimizationRecommendations();
    FString FormatMemorySize(uint64 Bytes) const;
};