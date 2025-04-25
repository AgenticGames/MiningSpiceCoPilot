// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RHIResources.h"
#include "RHIDefinitions.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMemory.h"
#include "Containers/Map.h"
#include "Containers/Array.h"
#include "Containers/StaticArray.h"
#include "Templates/SharedPointer.h"
#include "HAL/ThreadSafeBool.h"
#include "Serialization/Archive.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "Math/RandomStream.h"
#include "../../3_ThreadingTaskSystem/Public/ThreadSafety.h"
#include "../../1_CoreRegistry/Public/Interfaces/IService.h"

// Forward declarations
class FServiceLocator;

/** Enum representing different GPU vendors */
UENUM()
enum class EGPUVendor : uint8
{
    Unknown,
    NVIDIA,
    AMD,
    Intel,
    Apple,
    ARM,
    ImgTec,
    Qualcomm,
    Other
};

/** Enum representing different SDF operation types */
UENUM()
enum class ESDFOperationType : uint8
{
    Union,              // CSG Union operation
    Subtraction,        // CSG Subtraction operation
    Intersection,       // CSG Intersection operation
    Smoothing,          // Smoothing operation
    Evaluation,         // Field evaluation
    Gradient,           // Gradient calculation
    NarrowBandUpdate,   // Narrow-band update
    MaterialTransition, // Material transition operations
    VolumeRender,       // Volume rendering
    Custom              // Custom operations
};

/** Enum representing different memory strategies */
UENUM()
enum class EMemoryStrategy : uint8
{
    Unified,       // Unified memory (shared between CPU/GPU)
    Dedicated,     // Dedicated GPU memory
    Staged,        // Staged through system memory
    Tiled,         // Tiled memory access pattern
    Adaptive       // Adaptive based on operation
};

/** Enum representing compute shader precision */
UENUM()
enum class EComputePrecision : uint8
{
    Full,    // Full precision (typically FP32)
    Half,    // Half precision (FP16)
    Mixed,   // Mixed precision (varies by operation)
    Variable // Variable precision based on distance
};

/** Structure containing performance metrics for SDF operations */
struct FSDFOperationMetrics
{
    /** Average execution time in milliseconds */
    float AverageExecutionTimeMs;
    
    /** Minimum execution time in milliseconds */
    float MinExecutionTimeMs;
    
    /** Maximum execution time in milliseconds */
    float MaxExecutionTimeMs;
    
    /** Number of samples */
    int32 SampleCount;
    
    /** Standard deviation of execution time */
    float StdDeviation;
    
    /** Last update time */
    FDateTime LastUpdateTime;
    
    /** Constructor with default values */
    FSDFOperationMetrics()
        : AverageExecutionTimeMs(0.0f)
        , MinExecutionTimeMs(FLT_MAX)
        , MaxExecutionTimeMs(0.0f)
        , SampleCount(0)
        , StdDeviation(0.0f)
        , LastUpdateTime(FDateTime::Now())
    {}
    
    /** Update metrics with a new sample */
    void UpdateWithSample(float ExecutionTimeMs);
    
    /** Reset metrics */
    void Reset();
    
    /** Get a weighted average that prioritizes recent samples */
    float GetWeightedAverage() const;
};

/** Structure containing optimization parameters for a specific SDF operation type */
struct FSDFOperationProfile
{
    /** Operation type */
    ESDFOperationType OperationType;
    
    /** Optimal work group size in X dimension */
    int32 WorkGroupSizeX;
    
    /** Optimal work group size in Y dimension */
    int32 WorkGroupSizeY;
    
    /** Optimal work group size in Z dimension */
    int32 WorkGroupSizeZ;
    
    /** CPU fallback threshold in milliseconds per operation */
    float CPUFallbackThresholdMs;
    
    /** Memory strategy for this operation */
    EMemoryStrategy MemoryStrategy;
    
    /** Compute precision for this operation */
    EComputePrecision Precision;
    
    /** Whether to use narrow-band optimization */
    bool bUseNarrowBand;
    
    /** Narrow band threshold (distance from surface) */
    float NarrowBandThreshold;
    
    /** Whether to prioritize this operation for async compute */
    bool bPrioritizeForAsyncCompute;
    
    /** Performance metrics for this operation */
    FSDFOperationMetrics Metrics;
    
    /** Custom parameters for specific operations */
    TMap<FName, float> CustomParameters;
    
    /** Constructor with default values */
    FSDFOperationProfile()
        : OperationType(ESDFOperationType::Union)
        , WorkGroupSizeX(8)
        , WorkGroupSizeY(8)
        , WorkGroupSizeZ(4)
        , CPUFallbackThresholdMs(50.0f)
        , MemoryStrategy(EMemoryStrategy::Adaptive)
        , Precision(EComputePrecision::Full)
        , bUseNarrowBand(true)
        , NarrowBandThreshold(5.0f)
        , bPrioritizeForAsyncCompute(false)
    {}
    
    /** Constructor with operation type */
    FSDFOperationProfile(ESDFOperationType InOperationType)
        : OperationType(InOperationType)
        , WorkGroupSizeX(8)
        , WorkGroupSizeY(8)
        , WorkGroupSizeZ(4)
        , CPUFallbackThresholdMs(50.0f)
        , MemoryStrategy(EMemoryStrategy::Adaptive)
        , Precision(EComputePrecision::Full)
        , bUseNarrowBand(true)
        , NarrowBandThreshold(5.0f)
        , bPrioritizeForAsyncCompute(false)
    {}
    
    /** Serialize profile to archive */
    void Serialize(FArchive& Ar);
};

/** Structure containing GPU hardware capability information */
struct FGPUCapabilityInfo
{
    /** GPU vendor */
    EGPUVendor Vendor;
    
    /** GPU name */
    FString DeviceName;
    
    /** GPU driver version */
    FString DriverVersion;
    
    /** Total GPU memory in MB */
    int32 TotalMemoryMB;
    
    /** Shader model version */
    float ShaderModelVersion;
    
    /** Maximum compute shader workgroup size in X dimension */
    int32 MaxWorkGroupSizeX;
    
    /** Maximum compute shader workgroup size in Y dimension */
    int32 MaxWorkGroupSizeY;
    
    /** Maximum compute shader workgroup size in Z dimension */
    int32 MaxWorkGroupSizeZ;
    
    /** Whether the GPU supports compute shaders */
    bool bSupportsComputeShaders;
    
    /** Whether the GPU supports async compute */
    bool bSupportsAsyncCompute;
    
    /** Whether the GPU supports compute shader wave operations */
    bool bSupportsWaveOperations;
    
    /** Whether the GPU supports FP16 compute */
    bool bSupportsHalfPrecision;
    
    /** Whether the GPU supports shared memory */
    bool bSupportsSharedMemory;
    
    /** Whether the GPU supports unified memory */
    bool bSupportsUnifiedMemory;
    
    /** Maximum shared memory size per workgroup */
    int32 MaxSharedMemoryBytes;
    
    /** Platform-specific capabilities */
    TMap<FName, FString> PlatformSpecificCapabilities;
    
    /** Constructor with default values */
    FGPUCapabilityInfo()
        : Vendor(EGPUVendor::Unknown)
        , TotalMemoryMB(0)
        , ShaderModelVersion(0.0f)
        , MaxWorkGroupSizeX(0)
        , MaxWorkGroupSizeY(0)
        , MaxWorkGroupSizeZ(0)
        , bSupportsComputeShaders(false)
        , bSupportsAsyncCompute(false)
        , bSupportsWaveOperations(false)
        , bSupportsHalfPrecision(false)
        , bSupportsSharedMemory(false)
        , bSupportsUnifiedMemory(false)
        , MaxSharedMemoryBytes(0)
    {}
    
    /** Serialize capability info to archive */
    void Serialize(FArchive& Ar);
};

/** Structure representing a complete hardware profile */
struct FHardwareProfile
{
    /** Profile name */
    FString ProfileName;
    
    /** Profile ID */
    FGuid ProfileId;
    
    /** GPU capability information */
    FGPUCapabilityInfo GPUInfo;
    
    /** Creation date/time */
    FDateTime CreationTime;
    
    /** Last update date/time */
    FDateTime LastUpdateTime;
    
    /** SDF operation profiles */
    TMap<ESDFOperationType, FSDFOperationProfile> OperationProfiles;
    
    /** Profile version */
    int32 Version;
    
    /** Whether this is a custom profile */
    bool bIsCustomProfile;
    
    /** Whether this profile is auto-generated */
    bool bIsAutoGenerated;
    
    /** Constructor with default values */
    FHardwareProfile()
        : ProfileId(FGuid::NewGuid())
        , CreationTime(FDateTime::Now())
        , LastUpdateTime(FDateTime::Now())
        , Version(1)
        , bIsCustomProfile(false)
        , bIsAutoGenerated(true)
    {}
    
    /** Serialize profile to archive */
    void Serialize(FArchive& Ar);
};

/** Benchmark result for an SDF operation */
struct FBenchmarkResult
{
    /** Operation type */
    ESDFOperationType OperationType;
    
    /** Work group size tested */
    FIntVector WorkGroupSize;
    
    /** Memory strategy tested */
    EMemoryStrategy MemoryStrategy;
    
    /** Compute precision tested */
    EComputePrecision Precision;
    
    /** Average execution time in milliseconds */
    float AverageExecutionTimeMs;
    
    /** Standard deviation of execution time */
    float StdDeviation;
    
    /** Number of iterations run */
    int32 IterationCount;
    
    /** Benchmark dataset size (number of voxels) */
    int32 DatasetSize;
    
    /** Constructor with default values */
    FBenchmarkResult()
        : OperationType(ESDFOperationType::Union)
        , WorkGroupSize(8, 8, 4)
        , MemoryStrategy(EMemoryStrategy::Adaptive)
        , Precision(EComputePrecision::Full)
        , AverageExecutionTimeMs(0.0f)
        , StdDeviation(0.0f)
        , IterationCount(0)
        , DatasetSize(0)
    {}
};

/**
 * Hardware profile manager for GPU compute operations
 * Handles hardware capability detection, profiling, and optimization selection
 */
class MININGSPICECOPILOT_API FHardwareProfileManager : public IService
{
public:
    /** Constructor */
    FHardwareProfileManager();
    
    /** Destructor */
    virtual ~FHardwareProfileManager();
    
    /** Initialize the profile manager */
    virtual bool Initialize() override;
    
    /** Shutdown the profile manager */
    virtual void Shutdown() override;
    
    /** IService interface implementation */
    virtual bool IsInitialized() const override;
    virtual FName GetServiceName() const override;
    virtual int32 GetPriority() const override;
    
    /** Get the current hardware profile */
    const FHardwareProfile& GetCurrentProfile() const;
    
    /** Get optimal work group size for an operation */
    FIntVector GetOptimalWorkGroupSize(ESDFOperationType OperationType) const;
    
    /** Get optimal memory strategy for an operation */
    EMemoryStrategy GetOptimalMemoryStrategy(ESDFOperationType OperationType) const;
    
    /** Get optimal compute precision for an operation */
    EComputePrecision GetOptimalPrecision(ESDFOperationType OperationType) const;
    
    /** Get CPU fallback threshold for an operation */
    float GetCPUFallbackThreshold(ESDFOperationType OperationType) const;
    
    /** Check if an operation should use narrow band optimization */
    bool ShouldUseNarrowBand(ESDFOperationType OperationType) const;
    
    /** Get narrow band threshold for an operation */
    float GetNarrowBandThreshold(ESDFOperationType OperationType) const;
    
    /** Check if an operation should use async compute */
    bool ShouldUseAsyncCompute(ESDFOperationType OperationType) const;
    
    /** Get a custom parameter value for an operation */
    float GetCustomParameter(ESDFOperationType OperationType, const FName& ParameterName, float DefaultValue = 0.0f) const;
    
    /** Get the GPU capability information */
    const FGPUCapabilityInfo& GetGPUCapabilityInfo() const;
    
    /** Record performance for an SDF operation */
    void RecordOperationPerformance(ESDFOperationType OperationType, float ExecutionTimeMs);
    
    /** Get performance metrics for an SDF operation */
    const FSDFOperationMetrics& GetOperationMetrics(ESDFOperationType OperationType) const;
    
    /** Run benchmark for a specific operation */
    TArray<FBenchmarkResult> RunBenchmark(ESDFOperationType OperationType, int32 IterationCount = 10, bool bFullParameterSpace = false);
    
    /** Run comprehensive benchmarks for all operations */
    void RunComprehensiveBenchmarks(int32 IterationCount = 10);
    
    /** Save the current profile to disk */
    bool SaveProfile(const FString& ProfileName = TEXT(""));
    
    /** Load a profile from disk */
    bool LoadProfile(const FString& ProfileName);
    
    /** Generate a profile based on current hardware detection */
    void GenerateProfileForCurrentHardware();
    
    /** Refine profile based on accumulated performance metrics */
    void RefineProfile();
    
    /** Reset all performance metrics */
    void ResetPerformanceMetrics();
    
    /** Create a custom profile */
    FHardwareProfile CreateCustomProfile(const FString& ProfileName);
    
    /** Get a list of available profiles */
    TArray<FString> GetAvailableProfiles() const;
    
    /** Get the singleton instance */
    static FHardwareProfileManager& Get();
    
    /** Register with the service locator */
    void RegisterWithServiceLocator();
    
private:
    /** Detect current GPU capabilities */
    void DetectGPUCapabilities();
    
    /** Create default operation profiles */
    void CreateDefaultProfiles();
    
    /** Detect NVIDIA GPU capabilities */
    void DetectNVIDIACapabilities(FGPUCapabilityInfo& CapabilityInfo);
    
    /** Detect AMD GPU capabilities */
    void DetectAMDCapabilities(FGPUCapabilityInfo& CapabilityInfo);
    
    /** Detect Intel GPU capabilities */
    void DetectIntelCapabilities(FGPUCapabilityInfo& CapabilityInfo);
    
    /** Apply vendor-specific optimizations */
    void ApplyVendorSpecificOptimizations(FHardwareProfile& Profile);
    
    /** Create an optimized profile for NVIDIA GPUs */
    void CreateNVIDIAOptimizedProfile(FHardwareProfile& Profile);
    
    /** Create an optimized profile for AMD GPUs */
    void CreateAMDOptimizedProfile(FHardwareProfile& Profile);
    
    /** Create an optimized profile for Intel GPUs */
    void CreateIntelOptimizedProfile(FHardwareProfile& Profile);
    
    /** Update profile with benchmark results */
    void UpdateProfileWithBenchmarkResults(const TArray<FBenchmarkResult>& Results);
    
    /** Get optimal work group sizes based on GPU capabilities */
    FIntVector CalculateOptimalWorkGroupSize(const FGPUCapabilityInfo& GPUInfo, ESDFOperationType OperationType);
    
    /** Determine optimal memory strategy based on hardware capabilities */
    EMemoryStrategy DetermineOptimalMemoryStrategy(const FGPUCapabilityInfo& GPUInfo, ESDFOperationType OperationType);
    
    /** Get the full path for a profile file */
    FString GetProfileFilePath(const FString& ProfileName) const;
    
    /** Run a benchmark for specific parameters */
    FBenchmarkResult BenchmarkWithParameters(ESDFOperationType OperationType, const FIntVector& WorkGroupSize, 
                                            EMemoryStrategy MemoryStrategy, EComputePrecision Precision, 
                                            int32 IterationCount, int32 DatasetSize);
    
    /** Get a thread-safe copy of the current profile */
    FHardwareProfile GetProfileCopy() const;
    
    /** Apply profile updates in a thread-safe manner */
    void ApplyProfileUpdates(const FHardwareProfile& UpdatedProfile);
    
    /** Current hardware profile */
    FHardwareProfile CurrentProfile;
    
    /** Fallback default profile */
    FHardwareProfile DefaultProfile;
    
    /** Lock for thread-safe profile access */
    mutable FMiningReaderWriterLock ProfileLock;
    
    /** Flag indicating whether the manager has been initialized */
    FThreadSafeBool bIsInitialized;
    
    /** Path to profiles directory */
    FString ProfilesDirectory;
    
    /** Static instance for singleton pattern */
    static FHardwareProfileManager* Instance;
};
