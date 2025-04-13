// SVOPerformanceTracker.cpp
// Tracking performance metrics for the SVO system

#include "25_SvoSdfVolume/Public/SVOPerformanceTracker.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "GenericPlatform/GenericPlatformTime.h"
#include "Stats/Stats.h"
#include "Logging/LogMacros.h"
#include "4_Events/Public/EventBus.h"

FSVOPerformanceTracker::FSVOPerformanceTracker()
    : bTrackingEnabled(false)
    , bDetailedTracking(false)
    , TotalSampleCount(0)
    , CurrentFrameSamples(0)
    , SampleReportThreshold(100)
    , LastReportTime(0.0)
    , ReportIntervalSeconds(5.0)
{
    // Initialize performance categories
    for (int32 i = 0; i < (int32)EPerformanceCategory::Max; ++i)
    {
        PerformanceData[i].Category = (EPerformanceCategory)i;
        PerformanceData[i].TotalTime = 0.0;
        PerformanceData[i].MaxTime = 0.0;
        PerformanceData[i].SampleCount = 0;
        PerformanceData[i].TotalDataProcessed = 0;
    }

    // Register default performance category names
    CategoryNames.Add(EPerformanceCategory::DistanceFieldEvaluation, TEXT("Distance Field Evaluation"));
    CategoryNames.Add(EPerformanceCategory::MaterialInteraction, TEXT("Material Interaction"));
    CategoryNames.Add(EPerformanceCategory::TreeTraversal, TEXT("Octree Traversal"));
    CategoryNames.Add(EPerformanceCategory::FieldModification, TEXT("Field Modification"));
    CategoryNames.Add(EPerformanceCategory::Serialization, TEXT("Serialization"));
    CategoryNames.Add(EPerformanceCategory::NetworkSync, TEXT("Network Synchronization"));
    CategoryNames.Add(EPerformanceCategory::MemoryManagement, TEXT("Memory Management"));
    CategoryNames.Add(EPerformanceCategory::MaterialProcessing, TEXT("Material Processing"));

    // Initialize frame timing
    LastFrameTime = FPlatformTime::Seconds();
    AverageFrameTime = 0.0;
    FrameCount = 0;
    FrameTimeHistory.SetNum(FrameHistorySize);
}

FSVOPerformanceTracker::~FSVOPerformanceTracker()
{
    // Generate final report when destroyed
    if (bTrackingEnabled && TotalSampleCount > 0)
    {
        GeneratePerformanceReport();
    }
}

void FSVOPerformanceTracker::Initialize()
{
    // Check if tracking is enabled in config
    bool bTrackingEnabledInConfig = true; // Would come from config
    
    if (bTrackingEnabledInConfig)
    {
        EnableTracking(true);
    }
}

void FSVOPerformanceTracker::EnableTracking(bool bEnable)
{
    bTrackingEnabled = bEnable;
    
    if (bEnable)
    {
        // Reset tracking data when enabling
        ResetStats();
        LastReportTime = FPlatformTime::Seconds();
    }
    else
    {
        // Generate final report when disabling
        if (TotalSampleCount > 0)
        {
            GeneratePerformanceReport();
        }
    }
}

void FSVOPerformanceTracker::SetDetailedTracking(bool bDetailed)
{
    bDetailedTracking = bDetailed;
    
    // Clear existing detailed samples if disabling detailed tracking
    if (!bDetailed)
    {
        DetailedSamples.Empty();
    }
}

void FSVOPerformanceTracker::BeginSample(EPerformanceCategory Category)
{
    if (!bTrackingEnabled)
    {
        return;
    }
    
    // Get current time
    double CurrentTime = FPlatformTime::Seconds();
    
    // Get thread ID
    uint64 ThreadId = FPlatformTLS::GetCurrentThreadId();
    
    // Create a scope and start timing
    FSampleScope Scope;
    Scope.Category = Category;
    Scope.StartTime = CurrentTime;
    Scope.ThreadId = ThreadId;
    
    // Add to active scopes for this thread
    ThreadScopes.FindOrAdd(ThreadId).Add(Scope);
}

void FSVOPerformanceTracker::EndSample(EPerformanceCategory Category, uint64 DataSize)
{
    if (!bTrackingEnabled)
    {
        return;
    }
    
    // Get current time
    double CurrentTime = FPlatformTime::Seconds();
    
    // Get thread ID
    uint64 ThreadId = FPlatformTLS::GetCurrentThreadId();
    
    // Find matching scope for this thread
    TArray<FSampleScope>* ThreadScopeArray = ThreadScopes.Find(ThreadId);
    if (!ThreadScopeArray || ThreadScopeArray->Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("Performance tracker: EndSample called without matching BeginSample"));
        return;
    }
    
    // Assume the last scope is the one we're ending (nested scopes)
    int32 ScopeIndex = ThreadScopeArray->Num() - 1;
    FSampleScope& Scope = (*ThreadScopeArray)[ScopeIndex];
    
    // Verify category matches
    if (Scope.Category != Category)
    {
        UE_LOG(LogTemp, Warning, TEXT("Performance tracker: EndSample category mismatch"));
        return;
    }
    
    // Calculate timing
    double ElapsedTime = CurrentTime - Scope.StartTime;
    
    // Remove the scope
    ThreadScopeArray->RemoveAt(ScopeIndex);
    
    // Add timing data to the performance category
    FCategoryData& CategoryData = PerformanceData[(int32)Category];
    CategoryData.TotalTime += ElapsedTime;
    CategoryData.MaxTime = FMath::Max(CategoryData.MaxTime, ElapsedTime);
    CategoryData.SampleCount++;
    CategoryData.TotalDataProcessed += DataSize;
    
    // Store detailed sample if enabled
    if (bDetailedTracking)
    {
        FDetailedSample DetailedSample;
        DetailedSample.Category = Category;
        DetailedSample.StartTime = Scope.StartTime;
        DetailedSample.ElapsedTime = ElapsedTime;
        DetailedSample.ThreadId = ThreadId;
        DetailedSample.DataSize = DataSize;
        
        DetailedSamples.Add(DetailedSample);
    }
    
    // Update counters
    TotalSampleCount++;
    CurrentFrameSamples++;
    
    // Check if we should report
    if ((CurrentTime - LastReportTime > ReportIntervalSeconds) || 
        (CurrentFrameSamples >= SampleReportThreshold))
    {
        GeneratePerformanceReport();
        LastReportTime = CurrentTime;
        CurrentFrameSamples = 0;
    }
}

void FSVOPerformanceTracker::BeginFrame()
{
    if (!bTrackingEnabled)
    {
        return;
    }
    
    double CurrentTime = FPlatformTime::Seconds();
    
    // Calculate frame time
    double FrameTime = CurrentTime - LastFrameTime;
    LastFrameTime = CurrentTime;
    
    // Skip extreme values (likely due to debugging pauses)
    if (FrameTime > 1.0)
    {
        return;
    }
    
    // Update average frame time (with weighted averaging)
    if (FrameCount == 0)
    {
        AverageFrameTime = FrameTime;
    }
    else
    {
        // Apply more weight to recent frames (10% new frame, 90% history)
        AverageFrameTime = AverageFrameTime * 0.9 + FrameTime * 0.1;
    }
    
    // Store in history
    FrameTimeHistory[FrameCount % FrameHistorySize] = FrameTime;
    FrameCount++;
}

void FSVOPerformanceTracker::EndFrame()
{
    if (!bTrackingEnabled)
    {
        return;
    }
    
    // Check for and log any unclosed scopes
    CheckUnclosedScopes();
}

TArray<FSVOPerformanceTracker::FCategoryData> FSVOPerformanceTracker::GetPerformanceData() const
{
    TArray<FCategoryData> Result;
    
    for (int32 i = 0; i < (int32)EPerformanceCategory::Max; ++i)
    {
        Result.Add(PerformanceData[i]);
    }
    
    return Result;
}

double FSVOPerformanceTracker::GetAverageTime(EPerformanceCategory Category) const
{
    const FCategoryData& Data = PerformanceData[(int32)Category];
    
    if (Data.SampleCount > 0)
    {
        return Data.TotalTime / Data.SampleCount;
    }
    
    return 0.0;
}

double FSVOPerformanceTracker::GetMaxTime(EPerformanceCategory Category) const
{
    return PerformanceData[(int32)Category].MaxTime;
}

double FSVOPerformanceTracker::GetTotalTime(EPerformanceCategory Category) const
{
    return PerformanceData[(int32)Category].TotalTime;
}

uint32 FSVOPerformanceTracker::GetSampleCount(EPerformanceCategory Category) const
{
    return PerformanceData[(int32)Category].SampleCount;
}

double FSVOPerformanceTracker::GetProcessingThroughput(EPerformanceCategory Category) const
{
    const FCategoryData& Data = PerformanceData[(int32)Category];
    
    if (Data.TotalTime > 0.0 && Data.TotalDataProcessed > 0)
    {
        return Data.TotalDataProcessed / Data.TotalTime; // bytes per second
    }
    
    return 0.0;
}

double FSVOPerformanceTracker::GetAverageFrameTime() const
{
    return AverageFrameTime;
}

double FSVOPerformanceTracker::GetMedianFrameTime() const
{
    // Make a copy of frame times for sorting
    TArray<double> SortedFrameTimes;
    int32 ValidFrames = FMath::Min(FrameCount, FrameHistorySize);
    
    for (int32 i = 0; i < ValidFrames; ++i)
    {
        SortedFrameTimes.Add(FrameTimeHistory[i]);
    }
    
    if (SortedFrameTimes.Num() == 0)
    {
        return 0.0;
    }
    
    // Sort and get median
    SortedFrameTimes.Sort();
    int32 MiddleIndex = SortedFrameTimes.Num() / 2;
    
    if (SortedFrameTimes.Num() % 2 == 0)
    {
        // Even number - average the middle two
        return (SortedFrameTimes[MiddleIndex - 1] + SortedFrameTimes[MiddleIndex]) * 0.5;
    }
    else
    {
        // Odd number - return the middle value
        return SortedFrameTimes[MiddleIndex];
    }
}

FString FSVOPerformanceTracker::GetCategoryName(EPerformanceCategory Category) const
{
    const FString* Name = CategoryNames.Find(Category);
    if (Name)
    {
        return *Name;
    }
    
    return FString::Printf(TEXT("Category %d"), (int32)Category);
}

void FSVOPerformanceTracker::ResetStats()
{
    // Reset all performance data
    for (int32 i = 0; i < (int32)EPerformanceCategory::Max; ++i)
    {
        PerformanceData[i].TotalTime = 0.0;
        PerformanceData[i].MaxTime = 0.0;
        PerformanceData[i].SampleCount = 0;
        PerformanceData[i].TotalDataProcessed = 0;
    }
    
    // Reset counters
    TotalSampleCount = 0;
    CurrentFrameSamples = 0;
    LastReportTime = FPlatformTime::Seconds();
    
    // Reset frame timing
    AverageFrameTime = 0.0;
    FrameCount = 0;
    FrameTimeHistory.SetNum(FrameHistorySize);
    
    // Clear detailed samples
    DetailedSamples.Empty();
}

void FSVOPerformanceTracker::GeneratePerformanceReport()
{
    if (TotalSampleCount == 0)
    {
        return;
    }
    
    // Build a report string
    FString Report = TEXT("\n===== SVO Performance Report =====\n");
    Report += FString::Printf(TEXT("Total Samples: %d\n"), TotalSampleCount);
    Report += FString::Printf(TEXT("Average Frame Time: %.3f ms\n"), AverageFrameTime * 1000.0);
    Report += FString::Printf(TEXT("Median Frame Time: %.3f ms\n"), GetMedianFrameTime() * 1000.0);
    Report += TEXT("\nCategory Performance:\n");
    
    // Add data for each active category
    for (int32 i = 0; i < (int32)EPerformanceCategory::Max; ++i)
    {
        const FCategoryData& Data = PerformanceData[i];
        
        if (Data.SampleCount > 0)
        {
            FString CategoryName = GetCategoryName((EPerformanceCategory)i);
            double AverageTime = Data.TotalTime / Data.SampleCount;
            double Throughput = (Data.TotalDataProcessed > 0 && Data.TotalTime > 0.0) ? 
                                (Data.TotalDataProcessed / Data.TotalTime) : 0.0;
            
            Report += FString::Printf(TEXT("  %s:\n"), *CategoryName);
            Report += FString::Printf(TEXT("    Samples: %d\n"), Data.SampleCount);
            Report += FString::Printf(TEXT("    Avg Time: %.3f ms\n"), AverageTime * 1000.0);
            Report += FString::Printf(TEXT("    Max Time: %.3f ms\n"), Data.MaxTime * 1000.0);
            Report += FString::Printf(TEXT("    Total Time: %.3f s\n"), Data.TotalTime);
            
            if (Throughput > 0.0)
            {
                // Format throughput appropriately based on size
                if (Throughput > 1024.0 * 1024.0 * 1024.0)
                {
                    Report += FString::Printf(TEXT("    Throughput: %.2f GB/s\n"), Throughput / (1024.0 * 1024.0 * 1024.0));
                }
                else if (Throughput > 1024.0 * 1024.0)
                {
                    Report += FString::Printf(TEXT("    Throughput: %.2f MB/s\n"), Throughput / (1024.0 * 1024.0));
                }
                else if (Throughput > 1024.0)
                {
                    Report += FString::Printf(TEXT("    Throughput: %.2f KB/s\n"), Throughput / 1024.0);
                }
                else
                {
                    Report += FString::Printf(TEXT("    Throughput: %.2f bytes/s\n"), Throughput);
                }
            }
            
            Report += TEXT("\n");
        }
    }
    
    Report += TEXT("================================\n");
    
    // Log the report
    UE_LOG(LogTemp, Log, TEXT("%s"), *Report);
    
    // Send performance event if event system is available
    UEventBus* EventBus = UEventBus::Get();
    if (EventBus)
    {
        // Prepare event data
        TMap<FString, double> PerformanceMetrics;
        
        PerformanceMetrics.Add(TEXT("AverageFrameTime"), AverageFrameTime);
        PerformanceMetrics.Add(TEXT("MedianFrameTime"), GetMedianFrameTime());
        
        for (int32 i = 0; i < (int32)EPerformanceCategory::Max; ++i)
        {
            const FCategoryData& Data = PerformanceData[i];
            if (Data.SampleCount > 0)
            {
                FString CategoryName = GetCategoryName((EPerformanceCategory)i);
                FString AvgTimeKey = CategoryName + TEXT("_AvgTime");
                FString MaxTimeKey = CategoryName + TEXT("_MaxTime");
                
                PerformanceMetrics.Add(AvgTimeKey, Data.TotalTime / Data.SampleCount);
                PerformanceMetrics.Add(MaxTimeKey, Data.MaxTime);
            }
        }
        
        // Publish event
        EventBus->PublishEvent(TEXT("SVOPerformanceReport"), &PerformanceMetrics);
    }
}

void FSVOPerformanceTracker::CheckUnclosedScopes()
{
    double CurrentTime = FPlatformTime::Seconds();
    bool bFoundUnclosedScopes = false;
    
    // Check each thread's scope stack
    for (auto& ThreadScopePair : ThreadScopes)
    {
        uint64 ThreadId = ThreadScopePair.Key;
        TArray<FSampleScope>& ScopeStack = ThreadScopePair.Value;
        
        if (ScopeStack.Num() > 0)
        {
            bFoundUnclosedScopes = true;
            
            // Log warning for each unclosed scope
            for (const FSampleScope& Scope : ScopeStack)
            {
                double ElapsedTime = CurrentTime - Scope.StartTime;
                FString CategoryName = GetCategoryName(Scope.Category);
                
                UE_LOG(LogTemp, Warning, 
                       TEXT("Unclosed performance scope found: Category=%s, Thread=%llu, ElapsedTime=%.3fms"),
                       *CategoryName, ThreadId, ElapsedTime * 1000.0);
                
                // Force-close the scope by adding its time
                FCategoryData& CategoryData = PerformanceData[(int32)Scope.Category];
                CategoryData.TotalTime += ElapsedTime;
                CategoryData.MaxTime = FMath::Max(CategoryData.MaxTime, ElapsedTime);
                CategoryData.SampleCount++;
            }
            
            // Clear the stack for this thread
            ScopeStack.Empty();
        }
    }
    
    if (bFoundUnclosedScopes)
    {
        UE_LOG(LogTemp, Warning, TEXT("Performance tracker: Found and force-closed unclosed performance scopes"));
    }
}

void FSVOPerformanceTracker::ExportDetailedData(const FString& Filename)
{
    if (!bDetailedTracking || DetailedSamples.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("No detailed performance data to export"));
        return;
    }
    
    // Create CSV content
    FString CSVContent = TEXT("Category,StartTime,ElapsedTime,ThreadId,DataSize\n");
    
    for (const FDetailedSample& Sample : DetailedSamples)
    {
        FString CategoryName = GetCategoryName(Sample.Category);
        CSVContent += FString::Printf(TEXT("%s,%f,%f,%llu,%llu\n"),
                                     *CategoryName, Sample.StartTime, Sample.ElapsedTime,
                                     Sample.ThreadId, Sample.DataSize);
    }
    
    // Write to file
    if (FFileHelper::SaveStringToFile(CSVContent, *Filename))
    {
        UE_LOG(LogTemp, Log, TEXT("Exported detailed performance data to %s"), *Filename);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to export detailed performance data to %s"), *Filename);
    }
}