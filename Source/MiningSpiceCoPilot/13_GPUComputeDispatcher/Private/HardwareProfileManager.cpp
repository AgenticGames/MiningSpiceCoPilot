// Copyright Epic Games, Inc. All Rights Reserved.

#include "../Public/HardwareProfileManager.h"
#include "../../6_ServiceRegistryandDependency/Public/ServiceLocator.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformProcess.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Misc/Paths.h"
#include "GenericPlatform/GenericPlatformDriver.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "RHI.h"
#include "RenderGraphBuilder.h"
#include "ShaderParameterUtils.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/MessageDialog.h"
#include "Logging/LogMacros.h"

// Static instance initialization
FHardwareProfileManager* FHardwareProfileManager::Instance = nullptr;

// Create a dedicated log category for GPU profiling
DEFINE_LOG_CATEGORY_STATIC(LogGPUProfiler, Log, All);

// Helper functions for hardware detection
namespace HardwareProfilerHelpers
{
    // Detect GPU vendor from device name
    EGPUVendor DetectVendorFromName(const FString& DeviceName)
    {
        FString LowerDeviceName = DeviceName.ToLower();
        
        if (LowerDeviceName.Contains(TEXT("nvidia")) || LowerDeviceName.Contains(TEXT("geforce")) || LowerDeviceName.Contains(TEXT("quadro")))
        {
            return EGPUVendor::NVIDIA;
        }
        else if (LowerDeviceName.Contains(TEXT("amd")) || LowerDeviceName.Contains(TEXT("radeon")) || LowerDeviceName.Contains(TEXT("vega")))
        {
            return EGPUVendor::AMD;
        }
        else if (LowerDeviceName.Contains(TEXT("intel")) || LowerDeviceName.Contains(TEXT("iris")))
        {
            return EGPUVendor::Intel;
        }
        else if (LowerDeviceName.Contains(TEXT("apple")) || LowerDeviceName.Contains(TEXT("m1")) || LowerDeviceName.Contains(TEXT("m2")))
        {
            return EGPUVendor::Apple;
        }
        else if (LowerDeviceName.Contains(TEXT("arm")) || LowerDeviceName.Contains(TEXT("mali")))
        {
            return EGPUVendor::ARM;
        }
        else if (LowerDeviceName.Contains(TEXT("powervr")))
        {
            return EGPUVendor::ImgTec;
        }
        else if (LowerDeviceName.Contains(TEXT("adreno")))
        {
            return EGPUVendor::Qualcomm;
        }
        
        return EGPUVendor::Unknown;
    }
    
    // Parse NVIDIA driver version
    FString ParseNVIDIADriverVersion(const FString& VersionString)
    {
        // Example format: "471.11"
        return VersionString;
    }
    
    // Parse AMD driver version
    FString ParseAMDDriverVersion(const FString& VersionString)
    {
        // Example format: "21.5.2"
        return VersionString;
    }
    
    // Convert a memory size in bytes to MB
    int32 BytesToMB(uint64 Bytes)
    {
        return static_cast<int32>(Bytes / (1024 * 1024));
    }
    
    // Get a safe string from potentially unsafe string
    FString GetSafeString(const TCHAR* UnsafeString)
    {
        return UnsafeString ? UnsafeString : TEXT("Unknown");
    }
}

// Implementation of FSDFOperationMetrics methods
void FSDFOperationMetrics::UpdateWithSample(float ExecutionTimeMs)
{
    // Update min/max times
    MinExecutionTimeMs = FMath::Min(MinExecutionTimeMs, ExecutionTimeMs);
    MaxExecutionTimeMs = FMath::Max(MaxExecutionTimeMs, ExecutionTimeMs);
    
    // Update sample count and average
    SampleCount++;
    
    // Calculate running average and standard deviation
    float PreviousAverage = AverageExecutionTimeMs;
    AverageExecutionTimeMs = PreviousAverage + (ExecutionTimeMs - PreviousAverage) / SampleCount;
    
    // Update standard deviation using Welford's online algorithm
    if (SampleCount > 1)
    {
        float Delta = ExecutionTimeMs - PreviousAverage;
        float Delta2 = ExecutionTimeMs - AverageExecutionTimeMs;
        StdDeviation = ((SampleCount - 1) * StdDeviation + Delta * Delta2) / SampleCount;
    }
    
    // Update timestamp
    LastUpdateTime = FDateTime::Now();
}

void FSDFOperationMetrics::Reset()
{
    AverageExecutionTimeMs = 0.0f;
    MinExecutionTimeMs = FLT_MAX;
    MaxExecutionTimeMs = 0.0f;
    SampleCount = 0;
    StdDeviation = 0.0f;
    LastUpdateTime = FDateTime::Now();
}

float FSDFOperationMetrics::GetWeightedAverage() const
{
    // If we have very few samples, just return the regular average
    if (SampleCount < 5)
    {
        return AverageExecutionTimeMs;
    }
    
    // For established metrics, return a value that's slightly biased toward
    // the maximum to provide conservative performance estimates
    return AverageExecutionTimeMs + (StdDeviation * 0.5f);
}

// Serialization for FSDFOperationProfile
void FSDFOperationProfile::Serialize(FArchive& Ar)
{
    Ar << *reinterpret_cast<uint8*>(&OperationType);
    Ar << WorkGroupSizeX;
    Ar << WorkGroupSizeY;
    Ar << WorkGroupSizeZ;
    Ar << CPUFallbackThresholdMs;
    Ar << *reinterpret_cast<uint8*>(&MemoryStrategy);
    Ar << *reinterpret_cast<uint8*>(&Precision);
    Ar << bUseNarrowBand;
    Ar << NarrowBandThreshold;
    Ar << bPrioritizeForAsyncCompute;
    
    // Serialize metrics data
    Ar << Metrics.AverageExecutionTimeMs;
    Ar << Metrics.MinExecutionTimeMs;
    Ar << Metrics.MaxExecutionTimeMs;
    Ar << Metrics.SampleCount;
    Ar << Metrics.StdDeviation;
    
    if (Ar.IsLoading())
    {
        int64 TimeStamp;
        Ar << TimeStamp;
        Metrics.LastUpdateTime = FDateTime::FromUnixTimestamp(TimeStamp);
    }
    else if (Ar.IsSaving())
    {
        int64 TimeStamp = Metrics.LastUpdateTime.ToUnixTimestamp();
        Ar << TimeStamp;
    }
    
    // Serialize custom parameters
    if (Ar.IsLoading())
    {
        int32 NumParams;
        Ar << NumParams;
        CustomParameters.Empty(NumParams);
        
        for (int32 i = 0; i < NumParams; ++i)
        {
            FName ParamName;
            float ParamValue;
            Ar << ParamName;
            Ar << ParamValue;
            CustomParameters.Add(ParamName, ParamValue);
        }
    }
    else if (Ar.IsSaving())
    {
        int32 NumParams = CustomParameters.Num();
        Ar << NumParams;
        
        for (const auto& Pair : CustomParameters)
        {
            FName ParamName = Pair.Key;
            float ParamValue = Pair.Value;
            Ar << ParamName;
            Ar << ParamValue;
        }
    }
}

// Serialization for FGPUCapabilityInfo
void FGPUCapabilityInfo::Serialize(FArchive& Ar)
{
    Ar << *reinterpret_cast<uint8*>(&Vendor);
    Ar << DeviceName;
    Ar << DriverVersion;
    Ar << TotalMemoryMB;
    Ar << ShaderModelVersion;
    Ar << MaxWorkGroupSizeX;
    Ar << MaxWorkGroupSizeY;
    Ar << MaxWorkGroupSizeZ;
    Ar << bSupportsComputeShaders;
    Ar << bSupportsAsyncCompute;
    Ar << bSupportsWaveOperations;
    Ar << bSupportsHalfPrecision;
    Ar << bSupportsSharedMemory;
    Ar << bSupportsUnifiedMemory;
    Ar << MaxSharedMemoryBytes;
    
    // Serialize platform-specific capabilities
    if (Ar.IsLoading())
    {
        int32 NumCapabilities;
        Ar << NumCapabilities;
        PlatformSpecificCapabilities.Empty(NumCapabilities);
        
        for (int32 i = 0; i < NumCapabilities; ++i)
        {
            FName CapName;
            FString CapValue;
            Ar << CapName;
            Ar << CapValue;
            PlatformSpecificCapabilities.Add(CapName, CapValue);
        }
    }
    else if (Ar.IsSaving())
    {
        int32 NumCapabilities = PlatformSpecificCapabilities.Num();
        Ar << NumCapabilities;
        
        for (const auto& Pair : PlatformSpecificCapabilities)
        {
            FName CapName = Pair.Key;
            FString CapValue = Pair.Value;
            Ar << CapName;
            Ar << CapValue;
        }
    }
}

// Serialization for FHardwareProfile
void FHardwareProfile::Serialize(FArchive& Ar)
{
    Ar << ProfileName;
    
    // Handle GUID serialization
    if (Ar.IsLoading())
    {
        FString GuidString;
        Ar << GuidString;
        FGuid::Parse(GuidString, ProfileId);
    }
    else if (Ar.IsSaving())
    {
        FString GuidString = ProfileId.ToString();
        Ar << GuidString;
    }
    
    // Serialize GPU info
    GPUInfo.Serialize(Ar);
    
    // Serialize creation time
    if (Ar.IsLoading())
    {
        int64 CreationTimeStamp;
        Ar << CreationTimeStamp;
        CreationTime = FDateTime::FromUnixTimestamp(CreationTimeStamp);
        
        int64 UpdateTimeStamp;
        Ar << UpdateTimeStamp;
        LastUpdateTime = FDateTime::FromUnixTimestamp(UpdateTimeStamp);
    }
    else if (Ar.IsSaving())
    {
        int64 CreationTimeStamp = CreationTime.ToUnixTimestamp();
        Ar << CreationTimeStamp;
        
        int64 UpdateTimeStamp = LastUpdateTime.ToUnixTimestamp();
        Ar << UpdateTimeStamp;
    }
    
    // Serialize operation profiles
    if (Ar.IsLoading())
    {
        int32 NumProfiles;
        Ar << NumProfiles;
        OperationProfiles.Empty(NumProfiles);
        
        for (int32 i = 0; i < NumProfiles; ++i)
        {
            uint8 OpTypeRaw;
            Ar << OpTypeRaw;
            ESDFOperationType OpType = static_cast<ESDFOperationType>(OpTypeRaw);
            
            FSDFOperationProfile Profile(OpType);
            Profile.Serialize(Ar);
            OperationProfiles.Add(OpType, Profile);
        }
    }
    else if (Ar.IsSaving())
    {
        int32 NumProfiles = OperationProfiles.Num();
        Ar << NumProfiles;
        
        for (const auto& Pair : OperationProfiles)
        {
            uint8 OpTypeRaw = static_cast<uint8>(Pair.Key);
            Ar << OpTypeRaw;
            
            FSDFOperationProfile Profile = Pair.Value;
            Profile.Serialize(Ar);
        }
    }
    
    Ar << Version;
    Ar << bIsCustomProfile;
    Ar << bIsAutoGenerated;
}

// Implementation of FHardwareProfileManager
FHardwareProfileManager::FHardwareProfileManager()
    : bIsInitialized(false)
{
    // Set a default profile name
    CurrentProfile.ProfileName = TEXT("DefaultProfile");
    
    // Set profiles directory
    ProfilesDirectory = FPaths::ProjectSavedDir() / TEXT("GPUProfiles");
}

FHardwareProfileManager::~FHardwareProfileManager()
{
    Shutdown();
}

bool FHardwareProfileManager::Initialize()
{
    if (bIsInitialized)
    {
        return true;
    }
    
    UE_LOG(LogGPUProfiler, Log, TEXT("Initializing Hardware Profile Manager"));
    
    // Create profiles directory if it doesn't exist
    if (!FPaths::DirectoryExists(ProfilesDirectory))
    {
        IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
        PlatformFile.CreateDirectoryTree(*ProfilesDirectory);
    }
    
    // Detect hardware capabilities
    DetectGPUCapabilities();
    
    // Create default profiles for common operations
    CreateDefaultProfiles();
    
    // Apply vendor-specific optimizations
    ApplyVendorSpecificOptimizations(CurrentProfile);
    
    // Save the generated profile
    SaveProfile(TEXT("AutoGenerated"));
    
    bIsInitialized = true;
    UE_LOG(LogGPUProfiler, Log, TEXT("Hardware Profile Manager initialized successfully"));
    
    return true;
}

void FHardwareProfileManager::Shutdown()
{
    if (!bIsInitialized)
    {
        return;
    }
    
    UE_LOG(LogGPUProfiler, Log, TEXT("Shutting down Hardware Profile Manager"));
    
    // Save current profile with final metrics before shutdown
    SaveProfile(CurrentProfile.ProfileName);
    
    bIsInitialized = false;
}

const FHardwareProfile& FHardwareProfileManager::GetCurrentProfile() const
{
    FScopedReadLock ReadLock(ProfileLock);
    return CurrentProfile;
}

FIntVector FHardwareProfileManager::GetOptimalWorkGroupSize(ESDFOperationType OperationType) const
{
    FScopedReadLock ReadLock(ProfileLock);
    
    const FSDFOperationProfile* Profile = CurrentProfile.OperationProfiles.Find(OperationType);
    if (Profile)
    {
        return FIntVector(Profile->WorkGroupSizeX, Profile->WorkGroupSizeY, Profile->WorkGroupSizeZ);
    }
    
    // Return default work group size if no profile exists for this operation
    return FIntVector(8, 8, 4);
}

EMemoryStrategy FHardwareProfileManager::GetOptimalMemoryStrategy(ESDFOperationType OperationType) const
{
    FScopedReadLock ReadLock(ProfileLock);
    
    const FSDFOperationProfile* Profile = CurrentProfile.OperationProfiles.Find(OperationType);
    if (Profile)
    {
        return Profile->MemoryStrategy;
    }
    
    // Return default memory strategy if no profile exists for this operation
    return EMemoryStrategy::Adaptive;
}

EComputePrecision FHardwareProfileManager::GetOptimalPrecision(ESDFOperationType OperationType) const
{
    FScopedReadLock ReadLock(ProfileLock);
    
    const FSDFOperationProfile* Profile = CurrentProfile.OperationProfiles.Find(OperationType);
    if (Profile)
    {
        return Profile->Precision;
    }
    
    // Return default precision if no profile exists for this operation
    return EComputePrecision::Full;
}

float FHardwareProfileManager::GetCPUFallbackThreshold(ESDFOperationType OperationType) const
{
    FScopedReadLock ReadLock(ProfileLock);
    
    const FSDFOperationProfile* Profile = CurrentProfile.OperationProfiles.Find(OperationType);
    if (Profile)
    {
        return Profile->CPUFallbackThresholdMs;
    }
    
    // Return default threshold if no profile exists for this operation
    return 50.0f;
}

bool FHardwareProfileManager::ShouldUseNarrowBand(ESDFOperationType OperationType) const
{
    FScopedReadLock ReadLock(ProfileLock);
    
    const FSDFOperationProfile* Profile = CurrentProfile.OperationProfiles.Find(OperationType);
    if (Profile)
    {
        return Profile->bUseNarrowBand;
    }
    
    // Return default setting if no profile exists for this operation
    return true;
}

float FHardwareProfileManager::GetNarrowBandThreshold(ESDFOperationType OperationType) const
{
    FScopedReadLock ReadLock(ProfileLock);
    
    const FSDFOperationProfile* Profile = CurrentProfile.OperationProfiles.Find(OperationType);
    if (Profile)
    {
        return Profile->NarrowBandThreshold;
    }
    
    // Return default threshold if no profile exists for this operation
    return 5.0f;
}

bool FHardwareProfileManager::ShouldUseAsyncCompute(ESDFOperationType OperationType) const
{
    FScopedReadLock ReadLock(ProfileLock);
    
    // If GPU doesn't support async compute, always return false
    if (!CurrentProfile.GPUInfo.bSupportsAsyncCompute)
    {
        return false;
    }
    
    const FSDFOperationProfile* Profile = CurrentProfile.OperationProfiles.Find(OperationType);
    if (Profile)
    {
        return Profile->bPrioritizeForAsyncCompute;
    }
    
    // Return default setting if no profile exists for this operation
    return false;
}

float FHardwareProfileManager::GetCustomParameter(ESDFOperationType OperationType, const FName& ParameterName, float DefaultValue) const
{
    FScopedReadLock ReadLock(ProfileLock);
    
    const FSDFOperationProfile* Profile = CurrentProfile.OperationProfiles.Find(OperationType);
    if (Profile)
    {
        const float* ParamValue = Profile->CustomParameters.Find(ParameterName);
        if (ParamValue)
        {
            return *ParamValue;
        }
    }
    
    // Return default value if parameter not found
    return DefaultValue;
}

const FGPUCapabilityInfo& FHardwareProfileManager::GetGPUCapabilityInfo() const
{
    FScopedReadLock ReadLock(ProfileLock);
    return CurrentProfile.GPUInfo;
}

void FHardwareProfileManager::RecordOperationPerformance(ESDFOperationType OperationType, float ExecutionTimeMs)
{
    FScopedWriteLock WriteLock(ProfileLock);
    
    FSDFOperationProfile* Profile = CurrentProfile.OperationProfiles.Find(OperationType);
    if (Profile)
    {
        Profile->Metrics.UpdateWithSample(ExecutionTimeMs);
        CurrentProfile.LastUpdateTime = FDateTime::Now();
    }
    else
    {
        // Create a new profile if it doesn't exist
        FSDFOperationProfile NewProfile(OperationType);
        NewProfile.Metrics.UpdateWithSample(ExecutionTimeMs);
        CurrentProfile.OperationProfiles.Add(OperationType, NewProfile);
        CurrentProfile.LastUpdateTime = FDateTime::Now();
    }
}

const FSDFOperationMetrics& FHardwareProfileManager::GetOperationMetrics(ESDFOperationType OperationType) const
{
    FScopedReadLock ReadLock(ProfileLock);
    
    static FSDFOperationMetrics EmptyMetrics;
    
    const FSDFOperationProfile* Profile = CurrentProfile.OperationProfiles.Find(OperationType);
    if (Profile)
    {
        return Profile->Metrics;
    }
    
    // Return empty metrics if no profile exists
    return EmptyMetrics;
}

void FHardwareProfileManager::DetectGPUCapabilities()
{
    UE_LOG(LogGPUProfiler, Log, TEXT("Detecting GPU capabilities"));
    
    FScopedWriteLock WriteLock(ProfileLock);
    
    FGPUCapabilityInfo& CapabilityInfo = CurrentProfile.GPUInfo;
    
    // Get basic RHI information
    // In UE 5.5, we need to use the shader platform functionality to check capabilities
    CapabilityInfo.bSupportsComputeShaders = IsFeatureLevelSupported(GMaxRHIShaderPlatform, ERHIFeatureLevel::SM5);
    
    // Get GPU name
    if (GDynamicRHI)
    {
        CapabilityInfo.DeviceName = GDynamicRHI->GetName();
        
        // Use GRHIAdapterName for adapter name
        if (!GRHIAdapterName.IsEmpty())
        {
            CapabilityInfo.DeviceName = GRHIAdapterName;
        }
    }
    else
    {
        CapabilityInfo.DeviceName = TEXT("Unknown GPU");
    }
    
    // Detect vendor from device name
    CapabilityInfo.Vendor = HardwareProfilerHelpers::DetectVendorFromName(CapabilityInfo.DeviceName);
    
    // Get shader model version using the correct signature
    CapabilityInfo.ShaderModelVersion = GetMaxSupportedFeatureLevel(GMaxRHIShaderPlatform) >= ERHIFeatureLevel::SM5 ? 5.0f : 4.0f;
    
    // Set work group size limits - these are conservative defaults that will be refined
    // in vendor-specific detection functions
    CapabilityInfo.MaxWorkGroupSizeX = 1024;
    CapabilityInfo.MaxWorkGroupSizeY = 1024;
    CapabilityInfo.MaxWorkGroupSizeZ = 64;
    
    // Determine async compute support
    CapabilityInfo.bSupportsAsyncCompute = GSupportsParallelRenderingTasksWithSeparateRHIThread;
    
    // Conservative estimate for shared memory
    CapabilityInfo.bSupportsSharedMemory = true;
    CapabilityInfo.MaxSharedMemoryBytes = 16384; // 16KB conservative default
    
    // Detect more specific capabilities based on vendor
    switch (CapabilityInfo.Vendor)
    {
        case EGPUVendor::NVIDIA:
            DetectNVIDIACapabilities(CapabilityInfo);
            break;
            
        case EGPUVendor::AMD:
            DetectAMDCapabilities(CapabilityInfo);
            break;
            
        case EGPUVendor::Intel:
            DetectIntelCapabilities(CapabilityInfo);
            break;
            
        default:
            // Use conservative defaults for unknown vendors
            break;
    }
    
    // Update capabilities based on feature level
    ERHIFeatureLevel::Type FeatureLevel = GetMaxSupportedFeatureLevel(GMaxRHIShaderPlatform);
    if (FeatureLevel >= ERHIFeatureLevel::SM5)
    {
        CapabilityInfo.bSupportsWaveOperations = true;
    }
    
    // Log detected capabilities
    UE_LOG(LogGPUProfiler, Log, TEXT("GPU Detected: %s"), *CapabilityInfo.DeviceName);
    UE_LOG(LogGPUProfiler, Log, TEXT("Vendor: %s"), *UEnum::GetValueAsString(CapabilityInfo.Vendor));
    UE_LOG(LogGPUProfiler, Log, TEXT("Compute Shader Support: %s"), CapabilityInfo.bSupportsComputeShaders ? TEXT("Yes") : TEXT("No"));
    UE_LOG(LogGPUProfiler, Log, TEXT("Async Compute Support: %s"), CapabilityInfo.bSupportsAsyncCompute ? TEXT("Yes") : TEXT("No"));
    UE_LOG(LogGPUProfiler, Log, TEXT("Shader Model: %.1f"), CapabilityInfo.ShaderModelVersion);
    
    CurrentProfile.LastUpdateTime = FDateTime::Now();
}

void FHardwareProfileManager::DetectNVIDIACapabilities(FGPUCapabilityInfo& CapabilityInfo)
{
    // NVIDIA specific detection
    UE_LOG(LogGPUProfiler, Log, TEXT("Detecting NVIDIA-specific capabilities"));
    
    // Extract driver version from device name (if possible)
    FString DeviceName = CapabilityInfo.DeviceName;
    int32 VersionStart = DeviceName.Find(TEXT("("));
    int32 VersionEnd = DeviceName.Find(TEXT(")"));
    
    if (VersionStart != INDEX_NONE && VersionEnd != INDEX_NONE && VersionEnd > VersionStart)
    {
        FString VersionString = DeviceName.Mid(VersionStart + 1, VersionEnd - VersionStart - 1);
        CapabilityInfo.DriverVersion = HardwareProfilerHelpers::ParseNVIDIADriverVersion(VersionString);
    }
    
    // Handle workgroup size for NVIDIA
    CapabilityInfo.MaxWorkGroupSizeX = 1024;
    CapabilityInfo.MaxWorkGroupSizeY = 1024;
    CapabilityInfo.MaxWorkGroupSizeZ = 64;
    
    // NVIDIA GPUs typically support half-precision since Pascal
    CapabilityInfo.bSupportsHalfPrecision = true;
    
    // NVIDIA shared memory size varies by architecture
    // Use a conservative estimate based on general capabilities
    CapabilityInfo.MaxSharedMemoryBytes = 48 * 1024; // 48KB (typical for newer NVIDIA GPUs)
    
    // Detect GPU family from name
    if (DeviceName.Contains(TEXT("RTX")))
    {
        // Add RTX-specific capabilities
        CapabilityInfo.PlatformSpecificCapabilities.Add(TEXT("RTX"), TEXT("True"));
        CapabilityInfo.PlatformSpecificCapabilities.Add(TEXT("TensorCores"), TEXT("True"));
        
        // Increase shared memory for RTX cards
        CapabilityInfo.MaxSharedMemoryBytes = 64 * 1024; // 64KB
        
        // Tensor cores can benefit from half-precision
        CapabilityInfo.PlatformSpecificCapabilities.Add(TEXT("PreferHalfPrecision"), TEXT("True"));
    }
    else if (DeviceName.Contains(TEXT("GTX 16")))
    {
        // Turing architecture without RT cores
        CapabilityInfo.PlatformSpecificCapabilities.Add(TEXT("TuringWithoutRT"), TEXT("True"));
    }
    else if (DeviceName.Contains(TEXT("GTX 10")))
    {
        // Pascal architecture
        CapabilityInfo.PlatformSpecificCapabilities.Add(TEXT("Pascal"), TEXT("True"));
    }
}

void FHardwareProfileManager::DetectAMDCapabilities(FGPUCapabilityInfo& CapabilityInfo)
{
    // AMD specific detection
    UE_LOG(LogGPUProfiler, Log, TEXT("Detecting AMD-specific capabilities"));
    
    // Extract driver version from device name (if possible)
    FString DeviceName = CapabilityInfo.DeviceName;
    int32 VersionStart = DeviceName.Find(TEXT("("));
    int32 VersionEnd = DeviceName.Find(TEXT(")"));
    
    if (VersionStart != INDEX_NONE && VersionEnd != INDEX_NONE && VersionEnd > VersionStart)
    {
        FString VersionString = DeviceName.Mid(VersionStart + 1, VersionEnd - VersionStart - 1);
        CapabilityInfo.DriverVersion = HardwareProfilerHelpers::ParseAMDDriverVersion(VersionString);
    }
    
    // AMD workgroup sizes are typically different from NVIDIA
    CapabilityInfo.MaxWorkGroupSizeX = 1024;
    CapabilityInfo.MaxWorkGroupSizeY = 1024;
    CapabilityInfo.MaxWorkGroupSizeZ = 64;
    
    // Detect if this is an RDNA architecture
    bool bIsRDNA = DeviceName.Contains(TEXT("RX 6")) || DeviceName.Contains(TEXT("RX 7")) || 
                   DeviceName.Contains(TEXT("RDNA"));
                   
    // AMD GPUs since RDNA support half-precision well
    CapabilityInfo.bSupportsHalfPrecision = bIsRDNA;
    
    // AMD shared memory size
    CapabilityInfo.MaxSharedMemoryBytes = 32 * 1024; // 32KB (typical for AMD GPUs)
    
    if (bIsRDNA)
    {
        // RDNA-specific capabilities
        CapabilityInfo.PlatformSpecificCapabilities.Add(TEXT("RDNA"), TEXT("True"));
        CapabilityInfo.MaxSharedMemoryBytes = 64 * 1024; // 64KB for newer AMD GPUs
    }
    else if (DeviceName.Contains(TEXT("Vega")))
    {
        // Vega-specific capabilities
        CapabilityInfo.PlatformSpecificCapabilities.Add(TEXT("Vega"), TEXT("True"));
    }
}

void FHardwareProfileManager::DetectIntelCapabilities(FGPUCapabilityInfo& CapabilityInfo)
{
    // Intel specific detection
    UE_LOG(LogGPUProfiler, Log, TEXT("Detecting Intel-specific capabilities"));
    
    // Intel integrated GPUs typically have limited resources
    CapabilityInfo.MaxWorkGroupSizeX = 512;
    CapabilityInfo.MaxWorkGroupSizeY = 512;
    CapabilityInfo.MaxWorkGroupSizeZ = 64;
    
    // Intel shared memory size is typically lower
    CapabilityInfo.MaxSharedMemoryBytes = 16 * 1024; // 16KB for most Intel GPUs
    
    // Check for Intel Xe architecture (newer GPUs)
    bool bIsXe = CapabilityInfo.DeviceName.Contains(TEXT("Xe")) || 
                CapabilityInfo.DeviceName.Contains(TEXT("Arc"));
                
    // Intel GPUs since Xe support half-precision
    CapabilityInfo.bSupportsHalfPrecision = bIsXe;
    
    if (bIsXe)
    {
        // Xe-specific capabilities
        CapabilityInfo.PlatformSpecificCapabilities.Add(TEXT("Xe"), TEXT("True"));
        CapabilityInfo.MaxSharedMemoryBytes = 32 * 1024; // 32KB for Intel Xe
        
        // Xe GPUs have better workgroup size support
        CapabilityInfo.MaxWorkGroupSizeX = 1024;
        CapabilityInfo.MaxWorkGroupSizeY = 1024;
    }
    
    // Intel GPUs traditionally benefit from unified memory
    CapabilityInfo.bSupportsUnifiedMemory = true;
}

void FHardwareProfileManager::CreateDefaultProfiles()
{
    UE_LOG(LogGPUProfiler, Log, TEXT("Creating default operation profiles"));
    
    FScopedWriteLock WriteLock(ProfileLock);
    
    // Create profiles for each SDF operation type
    
    // Union operation profile
    FSDFOperationProfile UnionProfile(ESDFOperationType::Union);
    UnionProfile.WorkGroupSizeX = 8;
    UnionProfile.WorkGroupSizeY = 8;
    UnionProfile.WorkGroupSizeZ = 4;
    UnionProfile.CPUFallbackThresholdMs = 50.0f;
    UnionProfile.MemoryStrategy = EMemoryStrategy::Adaptive;
    UnionProfile.Precision = EComputePrecision::Full;
    UnionProfile.bUseNarrowBand = true;
    UnionProfile.NarrowBandThreshold = 5.0f;
    UnionProfile.bPrioritizeForAsyncCompute = false;
    CurrentProfile.OperationProfiles.Add(ESDFOperationType::Union, UnionProfile);
    
    // Subtraction operation profile
    FSDFOperationProfile SubtractionProfile(ESDFOperationType::Subtraction);
    SubtractionProfile.WorkGroupSizeX = 8;
    SubtractionProfile.WorkGroupSizeY = 8;
    SubtractionProfile.WorkGroupSizeZ = 4;
    SubtractionProfile.CPUFallbackThresholdMs = 50.0f;
    SubtractionProfile.MemoryStrategy = EMemoryStrategy::Adaptive;
    SubtractionProfile.Precision = EComputePrecision::Full;
    SubtractionProfile.bUseNarrowBand = true;
    SubtractionProfile.NarrowBandThreshold = 5.0f;
    SubtractionProfile.bPrioritizeForAsyncCompute = false;
    CurrentProfile.OperationProfiles.Add(ESDFOperationType::Subtraction, SubtractionProfile);
    
    // Intersection operation profile
    FSDFOperationProfile IntersectionProfile(ESDFOperationType::Intersection);
    IntersectionProfile.WorkGroupSizeX = 8;
    IntersectionProfile.WorkGroupSizeY = 8;
    IntersectionProfile.WorkGroupSizeZ = 4;
    IntersectionProfile.CPUFallbackThresholdMs = 50.0f;
    IntersectionProfile.MemoryStrategy = EMemoryStrategy::Adaptive;
    IntersectionProfile.Precision = EComputePrecision::Full;
    IntersectionProfile.bUseNarrowBand = true;
    IntersectionProfile.NarrowBandThreshold = 5.0f;
    IntersectionProfile.bPrioritizeForAsyncCompute = false;
    CurrentProfile.OperationProfiles.Add(ESDFOperationType::Intersection, IntersectionProfile);
    
    // Smoothing operation profile
    FSDFOperationProfile SmoothingProfile(ESDFOperationType::Smoothing);
    SmoothingProfile.WorkGroupSizeX = 8;
    SmoothingProfile.WorkGroupSizeY = 8;
    SmoothingProfile.WorkGroupSizeZ = 4;
    SmoothingProfile.CPUFallbackThresholdMs = 75.0f; // Smoothing is more complex
    SmoothingProfile.MemoryStrategy = EMemoryStrategy::Staged;
    SmoothingProfile.Precision = EComputePrecision::Full;
    SmoothingProfile.bUseNarrowBand = true;
    SmoothingProfile.NarrowBandThreshold = 8.0f; // Wider band for smoothing
    SmoothingProfile.bPrioritizeForAsyncCompute = true; // Good candidate for async
    CurrentProfile.OperationProfiles.Add(ESDFOperationType::Smoothing, SmoothingProfile);
    
    // Evaluation operation profile
    FSDFOperationProfile EvaluationProfile(ESDFOperationType::Evaluation);
    EvaluationProfile.WorkGroupSizeX = 16;
    EvaluationProfile.WorkGroupSizeY = 16;
    EvaluationProfile.WorkGroupSizeZ = 4;
    EvaluationProfile.CPUFallbackThresholdMs = 30.0f;
    EvaluationProfile.MemoryStrategy = EMemoryStrategy::Dedicated;
    EvaluationProfile.Precision = EComputePrecision::Mixed;
    EvaluationProfile.bUseNarrowBand = false; // Full field evaluation
    EvaluationProfile.bPrioritizeForAsyncCompute = false;
    CurrentProfile.OperationProfiles.Add(ESDFOperationType::Evaluation, EvaluationProfile);
    
    // Gradient operation profile
    FSDFOperationProfile GradientProfile(ESDFOperationType::Gradient);
    GradientProfile.WorkGroupSizeX = 8;
    GradientProfile.WorkGroupSizeY = 8;
    GradientProfile.WorkGroupSizeZ = 4;
    GradientProfile.CPUFallbackThresholdMs = 60.0f;
    GradientProfile.MemoryStrategy = EMemoryStrategy::Dedicated;
    GradientProfile.Precision = EComputePrecision::Full; // Gradients need precision
    GradientProfile.bUseNarrowBand = true;
    GradientProfile.NarrowBandThreshold = 4.0f;
    GradientProfile.bPrioritizeForAsyncCompute = true;
    CurrentProfile.OperationProfiles.Add(ESDFOperationType::Gradient, GradientProfile);
    
    // Narrow band update profile
    FSDFOperationProfile NarrowBandProfile(ESDFOperationType::NarrowBandUpdate);
    NarrowBandProfile.WorkGroupSizeX = 8;
    NarrowBandProfile.WorkGroupSizeY = 8;
    NarrowBandProfile.WorkGroupSizeZ = 4;
    NarrowBandProfile.CPUFallbackThresholdMs = 40.0f;
    NarrowBandProfile.MemoryStrategy = EMemoryStrategy::Dedicated;
    NarrowBandProfile.Precision = EComputePrecision::Full;
    NarrowBandProfile.bUseNarrowBand = true; // Obviously
    NarrowBandProfile.NarrowBandThreshold = 5.0f;
    NarrowBandProfile.bPrioritizeForAsyncCompute = true;
    CurrentProfile.OperationProfiles.Add(ESDFOperationType::NarrowBandUpdate, NarrowBandProfile);
    
    // Material transition profile
    FSDFOperationProfile MaterialProfile(ESDFOperationType::MaterialTransition);
    MaterialProfile.WorkGroupSizeX = 8;
    MaterialProfile.WorkGroupSizeY = 8;
    MaterialProfile.WorkGroupSizeZ = 4;
    MaterialProfile.CPUFallbackThresholdMs = 70.0f;
    MaterialProfile.MemoryStrategy = EMemoryStrategy::Staged;
    MaterialProfile.Precision = EComputePrecision::Full;
    MaterialProfile.bUseNarrowBand = true;
    MaterialProfile.NarrowBandThreshold = 6.0f;
    MaterialProfile.bPrioritizeForAsyncCompute = false;
    CurrentProfile.OperationProfiles.Add(ESDFOperationType::MaterialTransition, MaterialProfile);
    
    // Volume render profile
    FSDFOperationProfile VolumeRenderProfile(ESDFOperationType::VolumeRender);
    VolumeRenderProfile.WorkGroupSizeX = 16;
    VolumeRenderProfile.WorkGroupSizeY = 16;
    VolumeRenderProfile.WorkGroupSizeZ = 1; // 2D dispatch for rendering
    VolumeRenderProfile.CPUFallbackThresholdMs = 100.0f; // Higher threshold for visualization
    VolumeRenderProfile.MemoryStrategy = EMemoryStrategy::Dedicated;
    VolumeRenderProfile.Precision = EComputePrecision::Mixed;
    VolumeRenderProfile.bUseNarrowBand = false; // Full field for rendering
    VolumeRenderProfile.bPrioritizeForAsyncCompute = false; // Usually on graphics queue
    CurrentProfile.OperationProfiles.Add(ESDFOperationType::VolumeRender, VolumeRenderProfile);
    
    // Custom operation profile template
    FSDFOperationProfile CustomProfile(ESDFOperationType::Custom);
    CustomProfile.WorkGroupSizeX = 8;
    CustomProfile.WorkGroupSizeY = 8;
    CustomProfile.WorkGroupSizeZ = 4;
    CustomProfile.CPUFallbackThresholdMs = 50.0f;
    CustomProfile.MemoryStrategy = EMemoryStrategy::Adaptive;
    CustomProfile.Precision = EComputePrecision::Full;
    CustomProfile.bUseNarrowBand = true;
    CustomProfile.NarrowBandThreshold = 5.0f;
    CustomProfile.bPrioritizeForAsyncCompute = false;
    CurrentProfile.OperationProfiles.Add(ESDFOperationType::Custom, CustomProfile);
    
    UE_LOG(LogGPUProfiler, Log, TEXT("Created default profiles for %d operation types"), CurrentProfile.OperationProfiles.Num());
}

void FHardwareProfileManager::ApplyVendorSpecificOptimizations(FHardwareProfile& Profile)
{
    UE_LOG(LogGPUProfiler, Log, TEXT("Applying vendor-specific optimizations"));
    
    // Apply optimizations based on GPU vendor
    switch (Profile.GPUInfo.Vendor)
    {
        case EGPUVendor::NVIDIA:
            CreateNVIDIAOptimizedProfile(Profile);
            break;
            
        case EGPUVendor::AMD:
            CreateAMDOptimizedProfile(Profile);
            break;
            
        case EGPUVendor::Intel:
            CreateIntelOptimizedProfile(Profile);
            break;
            
        default:
            // No specific optimizations for other vendors
            break;
    }
    
    // Apply common optimizations based on capabilities
    
    // If async compute is not supported, disable it for all operations
    if (!Profile.GPUInfo.bSupportsAsyncCompute)
    {
        for (auto& Pair : Profile.OperationProfiles)
        {
            Pair.Value.bPrioritizeForAsyncCompute = false;
        }
    }
    
    // If half precision is supported, use it for appropriate operations
    if (Profile.GPUInfo.bSupportsHalfPrecision)
    {
        // Volume rendering can benefit from half precision
        FSDFOperationProfile* VolumeRenderProfile = Profile.OperationProfiles.Find(ESDFOperationType::VolumeRender);
        if (VolumeRenderProfile)
        {
            VolumeRenderProfile->Precision = EComputePrecision::Half;
        }
        
        // Evaluation can use half precision at a distance from the surface
        FSDFOperationProfile* EvaluationProfile = Profile.OperationProfiles.Find(ESDFOperationType::Evaluation);
        if (EvaluationProfile)
        {
            EvaluationProfile->Precision = EComputePrecision::Variable;
        }
    }
    
    // If unified memory is supported, use it for appropriate operations
    if (Profile.GPUInfo.bSupportsUnifiedMemory)
    {
        for (auto& Pair : Profile.OperationProfiles)
        {
            // Switch to unified memory for operations that benefit from CPU access
            if (Pair.Key == ESDFOperationType::NarrowBandUpdate ||
                Pair.Key == ESDFOperationType::MaterialTransition)
            {
                Pair.Value.MemoryStrategy = EMemoryStrategy::Unified;
            }
        }
    }
    
    UE_LOG(LogGPUProfiler, Log, TEXT("Applied vendor-specific optimizations for %s"), *UEnum::GetValueAsString(Profile.GPUInfo.Vendor));
}

void FHardwareProfileManager::CreateNVIDIAOptimizedProfile(FHardwareProfile& Profile)
{
    // NVIDIA specific optimizations
    UE_LOG(LogGPUProfiler, Log, TEXT("Applying NVIDIA-specific optimizations"));
    
    // NVIDIA GPUs typically work best with these work group sizes
    for (auto& Pair : Profile.OperationProfiles)
    {
        // Basic operations (Union, Subtraction, Intersection) work well with 8x8x4
        if (Pair.Key <= ESDFOperationType::Intersection)
        {
            Pair.Value.WorkGroupSizeX = 8;
            Pair.Value.WorkGroupSizeY = 8;
            Pair.Value.WorkGroupSizeZ = 4;
        }
        // Evaluation and Volume Rendering work better with larger tiles
        else if (Pair.Key == ESDFOperationType::Evaluation || Pair.Key == ESDFOperationType::VolumeRender)
        {
            Pair.Value.WorkGroupSizeX = 16;
            Pair.Value.WorkGroupSizeY = 16;
            Pair.Value.WorkGroupSizeZ = 2;
        }
        
        // NVIDIA GPUs tend to prefer dedicated memory for most operations
        Pair.Value.MemoryStrategy = EMemoryStrategy::Dedicated;
    }
    
    // Check for RTX capabilities
    const FString* RTXSupport = Profile.GPUInfo.PlatformSpecificCapabilities.Find(TEXT("RTX"));
    if (RTXSupport && (*RTXSupport == TEXT("True")))
    {
        // For RTX GPUs, enable async compute for more operations
        FSDFOperationProfile* GradientProfile = Profile.OperationProfiles.Find(ESDFOperationType::Gradient);
        if (GradientProfile)
        {
            GradientProfile->bPrioritizeForAsyncCompute = true;
        }
        
        FSDFOperationProfile* NarrowBandProfile = Profile.OperationProfiles.Find(ESDFOperationType::NarrowBandUpdate);
        if (NarrowBandProfile)
        {
            NarrowBandProfile->bPrioritizeForAsyncCompute = true;
        }
        
        // RTX GPUs can handle larger work groups
        for (auto& Pair : Profile.OperationProfiles)
        {
            // Increase work group sizes for computation-heavy operations
            if (Pair.Key == ESDFOperationType::Smoothing ||
                Pair.Key == ESDFOperationType::Gradient)
            {
                Pair.Value.WorkGroupSizeX = 16;
                Pair.Value.WorkGroupSizeY = 8;
                Pair.Value.WorkGroupSizeZ = 4;
            }
        }
    }
}

void FHardwareProfileManager::CreateAMDOptimizedProfile(FHardwareProfile& Profile)
{
    // AMD specific optimizations
    UE_LOG(LogGPUProfiler, Log, TEXT("Applying AMD-specific optimizations"));
    
    // AMD GPUs typically work best with different work group sizes than NVIDIA
    for (auto& Pair : Profile.OperationProfiles)
    {
        // AMD GPUs often prefer wider but flatter workgroups
        Pair.Value.WorkGroupSizeX = 16;
        Pair.Value.WorkGroupSizeY = 16;
        Pair.Value.WorkGroupSizeZ = 2;
        
        // AMD GPUs often benefit from staged memory for complex operations
        if (Pair.Key == ESDFOperationType::Smoothing ||
            Pair.Key == ESDFOperationType::MaterialTransition)
        {
            Pair.Value.MemoryStrategy = EMemoryStrategy::Staged;
        }
    }
    
    // Check for RDNA capabilities
    const FString* RDNASupport = Profile.GPUInfo.PlatformSpecificCapabilities.Find(TEXT("RDNA"));
    if (RDNASupport && (*RDNASupport == TEXT("True")))
    {
        // RDNA architecture benefits from these optimizations
        for (auto& Pair : Profile.OperationProfiles)
        {
            // RDNA has improved compute capabilities
            if (Pair.Key == ESDFOperationType::Evaluation ||
                Pair.Key == ESDFOperationType::Gradient)
            {
                Pair.Value.WorkGroupSizeX = 32;
                Pair.Value.WorkGroupSizeY = 8;
                Pair.Value.WorkGroupSizeZ = 2;
            }
            
            // RDNA has better async compute performance
            if (Pair.Key == ESDFOperationType::NarrowBandUpdate ||
                Pair.Key == ESDFOperationType::Gradient)
            {
                Pair.Value.bPrioritizeForAsyncCompute = true;
            }
        }
    }
}

void FHardwareProfileManager::CreateIntelOptimizedProfile(FHardwareProfile& Profile)
{
    // Intel specific optimizations
    UE_LOG(LogGPUProfiler, Log, TEXT("Applying Intel-specific optimizations"));
    
    // Intel GPUs typically work best with smaller work groups
    for (auto& Pair : Profile.OperationProfiles)
    {
        // Intel integrated GPUs benefit from smaller work groups
        Pair.Value.WorkGroupSizeX = 8;
        Pair.Value.WorkGroupSizeY = 8;
        Pair.Value.WorkGroupSizeZ = 2;
        
        // Intel GPUs benefit from unified memory
        Pair.Value.MemoryStrategy = EMemoryStrategy::Unified;
        
        // Intel GPUs typically need higher CPU fallback thresholds
        Pair.Value.CPUFallbackThresholdMs *= 0.7f; // 30% lower threshold
    }
    
    // Check for Xe capabilities
    const FString* XeSupport = Profile.GPUInfo.PlatformSpecificCapabilities.Find(TEXT("Xe"));
    if (XeSupport && (*XeSupport == TEXT("True")))
    {
        // Xe architecture has much better compute capabilities
        for (auto& Pair : Profile.OperationProfiles)
        {
            // Newer Intel GPUs can handle larger work groups
            Pair.Value.WorkGroupSizeX = 16;
            Pair.Value.WorkGroupSizeY = 8;
            Pair.Value.WorkGroupSizeZ = 2;
            
            // Adjust CPU fallback threshold back up for Xe
            Pair.Value.CPUFallbackThresholdMs *= 1.4f; // Undo the reduction and add a bit more
        }
    }
}

FHardwareProfile FHardwareProfileManager::GetProfileCopy() const
{
    FScopedReadLock ReadLock(ProfileLock);
    return CurrentProfile;
}

void FHardwareProfileManager::ApplyProfileUpdates(const FHardwareProfile& UpdatedProfile)
{
    FScopedWriteLock WriteLock(ProfileLock);
    CurrentProfile = UpdatedProfile;
    CurrentProfile.LastUpdateTime = FDateTime::Now();
    CurrentProfile.Version++;
}

void FHardwareProfileManager::RefineProfile()
{
    UE_LOG(LogGPUProfiler, Log, TEXT("Refining hardware profile based on performance metrics"));
    
    FHardwareProfile UpdatedProfile = GetProfileCopy();
    bool bProfileChanged = false;
    
    // Iterate through all operation profiles
    for (auto& Pair : UpdatedProfile.OperationProfiles)
    {
        FSDFOperationProfile& Profile = Pair.Value;
        
        // Skip profiles with insufficient data
        if (Profile.Metrics.SampleCount < 10)
        {
            continue;
        }
        
        // Adjust work group size based on performance
        if (Profile.Metrics.AverageExecutionTimeMs > Profile.CPUFallbackThresholdMs * 0.8f)
        {
            // If close to the CPU fallback threshold, try smaller work groups
            Profile.WorkGroupSizeX = FMath::Max(4, Profile.WorkGroupSizeX / 2);
            Profile.WorkGroupSizeY = FMath::Max(4, Profile.WorkGroupSizeY / 2);
            bProfileChanged = true;
            
            UE_LOG(LogGPUProfiler, Log, TEXT("Reduced work group size for %s operation"), *UEnum::GetValueAsString(Pair.Key));
        }
        
        // Adjust memory strategy if performance is poor
        if (Profile.Metrics.StdDeviation > Profile.Metrics.AverageExecutionTimeMs * 0.5f)
        {
            // High variability might indicate memory strategy issues
            if (Profile.MemoryStrategy == EMemoryStrategy::Dedicated)
            {
                Profile.MemoryStrategy = EMemoryStrategy::Staged;
                bProfileChanged = true;
                
                UE_LOG(LogGPUProfiler, Log, TEXT("Changed memory strategy for %s operation to Staged"), *UEnum::GetValueAsString(Pair.Key));
            }
            else if (Profile.MemoryStrategy == EMemoryStrategy::Staged)
            {
                Profile.MemoryStrategy = EMemoryStrategy::Unified;
                bProfileChanged = true;
                
                UE_LOG(LogGPUProfiler, Log, TEXT("Changed memory strategy for %s operation to Unified"), *UEnum::GetValueAsString(Pair.Key));
            }
        }
        
        // Adjust narrow band optimization if needed
        if (Profile.bUseNarrowBand && Profile.Metrics.AverageExecutionTimeMs > 30.0f)
        {
            // If performance is still poor with narrow band, adjust the threshold
            Profile.NarrowBandThreshold = FMath::Max(2.0f, Profile.NarrowBandThreshold - 1.0f);
            bProfileChanged = true;
            
            UE_LOG(LogGPUProfiler, Log, TEXT("Reduced narrow band threshold for %s operation to %.1f"), 
                   *UEnum::GetValueAsString(Pair.Key), Profile.NarrowBandThreshold);
        }
        
        // Update CPU fallback threshold based on real-world data
        float NewThreshold = Profile.Metrics.GetWeightedAverage() * 1.5f; // 50% headroom
        if (FMath::Abs(NewThreshold - Profile.CPUFallbackThresholdMs) > 10.0f)
        {
            Profile.CPUFallbackThresholdMs = FMath::Clamp(NewThreshold, 10.0f, 200.0f);
            bProfileChanged = true;
            
            UE_LOG(LogGPUProfiler, Log, TEXT("Updated CPU fallback threshold for %s operation to %.1f ms"), 
                   *UEnum::GetValueAsString(Pair.Key), Profile.CPUFallbackThresholdMs);
        }
    }
    
    if (bProfileChanged)
    {
        // Apply the updated profile
        ApplyProfileUpdates(UpdatedProfile);
        
        // Save the refined profile
        SaveProfile(TEXT("Refined_") + FDateTime::Now().ToString());
        
        UE_LOG(LogGPUProfiler, Log, TEXT("Profile refinement complete. Profile version: %d"), CurrentProfile.Version);
    }
    else
    {
        UE_LOG(LogGPUProfiler, Log, TEXT("No profile changes were necessary during refinement."));
    }
}

FString FHardwareProfileManager::GetProfileFilePath(const FString& ProfileName) const
{
    FString SafeProfileName = ProfileName;
    
    // Replace invalid filename characters
    for (int32 i = 0; i < SafeProfileName.Len(); ++i)
    {
        TCHAR& C = SafeProfileName[i];
        if (C == TEXT('/') || C == TEXT('\\') || C == TEXT(':') || C == TEXT('*') || 
            C == TEXT('?') || C == TEXT('"') || C == TEXT('<') || C == TEXT('>') || C == TEXT('|'))
        {
            C = TEXT('_');
        }
    }
    
    // If no profile name is provided, use the current profile name
    if (SafeProfileName.IsEmpty())
    {
        FScopedReadLock ReadLock(ProfileLock);
        SafeProfileName = CurrentProfile.ProfileName;
    }
    
    return FPaths::Combine(ProfilesDirectory, SafeProfileName + TEXT(".gpuprofile"));
}

bool FHardwareProfileManager::SaveProfile(const FString& ProfileName)
{
    FScopedReadLock ReadLock(ProfileLock);
    
    // Update profile name if needed
    FString TargetProfileName = ProfileName;
    if (TargetProfileName.IsEmpty())
    {
        TargetProfileName = CurrentProfile.ProfileName;
    }
    else
    {
        // We need to modify the profile, so release the read lock and acquire a write lock
        FScopedWriteLock WriteLock(ProfileLock);
        CurrentProfile.ProfileName = TargetProfileName;
    }
    
    // Get the file path
    FString FilePath = GetProfileFilePath(TargetProfileName);
    
    // Create a memory buffer for serialization
    TArray<uint8> ProfileData;
    FMemoryWriter MemWriter(ProfileData);
    
    // Serialize the profile
    CurrentProfile.Serialize(MemWriter);
    
    // Save to file
    bool bSaveResult = FFileHelper::SaveArrayToFile(ProfileData, *FilePath);
    
    if (bSaveResult)
    {
        UE_LOG(LogGPUProfiler, Log, TEXT("Successfully saved profile '%s' to '%s'"), *TargetProfileName, *FilePath);
    }
    else
    {
        UE_LOG(LogGPUProfiler, Error, TEXT("Failed to save profile '%s' to '%s'"), *TargetProfileName, *FilePath);
    }
    
    return bSaveResult;
}

bool FHardwareProfileManager::LoadProfile(const FString& ProfileName)
{
    // Get the file path
    FString FilePath = GetProfileFilePath(ProfileName);
    
    // Check if the file exists
    if (!FPaths::FileExists(FilePath))
    {
        UE_LOG(LogGPUProfiler, Error, TEXT("Profile file not found: %s"), *FilePath);
        return false;
    }
    
    // Load file into memory
    TArray<uint8> ProfileData;
    if (!FFileHelper::LoadFileToArray(ProfileData, *FilePath))
    {
        UE_LOG(LogGPUProfiler, Error, TEXT("Failed to load profile file: %s"), *FilePath);
        return false;
    }
    
    // Create a memory reader for deserialization
    FMemoryReader MemReader(ProfileData);
    
    // Create a temporary profile
    FHardwareProfile LoadedProfile;
    
    // Deserialize
    LoadedProfile.Serialize(MemReader);
    
    // Validate profile
    if (LoadedProfile.ProfileName.IsEmpty() || LoadedProfile.ProfileId.IsValid() == false)
    {
        UE_LOG(LogGPUProfiler, Error, TEXT("Loaded profile is invalid or corrupted: %s"), *FilePath);
        return false;
    }
    
    // Apply the loaded profile
    FScopedWriteLock WriteLock(ProfileLock);
    CurrentProfile = LoadedProfile;
    
    UE_LOG(LogGPUProfiler, Log, TEXT("Successfully loaded profile '%s' from '%s'"), *LoadedProfile.ProfileName, *FilePath);
    
    return true;
}

TArray<FString> FHardwareProfileManager::GetAvailableProfiles() const
{
    TArray<FString> ProfileNames;
    
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    
    // Create the directory if it doesn't exist
    if (!PlatformFile.DirectoryExists(*ProfilesDirectory))
    {
        PlatformFile.CreateDirectoryTree(*ProfilesDirectory);
        return ProfileNames; // Return empty array if directory was just created
    }
    
    // Enumerate files in the profiles directory
    class FProfileFileVisitor : public IPlatformFile::FDirectoryVisitor
    {
    public:
        TArray<FString>& ProfileNames;
        
        FProfileFileVisitor(TArray<FString>& InProfileNames)
            : ProfileNames(InProfileNames)
        {
        }
        
        virtual bool Visit(const TCHAR* FilenameOrDirectory, bool bIsDirectory) override
        {
            if (!bIsDirectory)
            {
                FString Filename = FPaths::GetBaseFilename(FilenameOrDirectory);
                FString Extension = FPaths::GetExtension(FilenameOrDirectory);
                
                if (Extension.Equals(TEXT("gpuprofile"), ESearchCase::IgnoreCase))
                {
                    ProfileNames.Add(Filename);
                }
            }
            return true; // Continue iterating
        }
    };
    
    FProfileFileVisitor FileVisitor(ProfileNames);
    PlatformFile.IterateDirectory(*ProfilesDirectory, FileVisitor);
    
    return ProfileNames;
}

FHardwareProfileManager& FHardwareProfileManager::Get()
{
    if (Instance == nullptr)
    {
        Instance = new FHardwareProfileManager();
        // Make sure to initialize the instance
        if (!Instance->Initialize())
        {
            UE_LOG(LogGPUProfiler, Error, TEXT("Failed to initialize HardwareProfileManager singleton"));
        }
    }
    
    return *Instance;
}

void FHardwareProfileManager::RegisterWithServiceLocator()
{
    // Register this service with the service locator
    FServiceLocator& ServiceLocator = FServiceLocator::Get();
    
    // Assuming IHardwareProfileManager interface exists and is derived from IService
    // Check if we need to cast to the specific interface type
    ServiceLocator.RegisterService(this, nullptr); // nullptr for interface type means using the object's type
    
    UE_LOG(LogGPUProfiler, Log, TEXT("HardwareProfileManager registered with ServiceLocator"));
}

void FHardwareProfileManager::GenerateProfileForCurrentHardware()
{
    UE_LOG(LogGPUProfiler, Log, TEXT("Generating hardware-specific profile"));
    
    // Reset the current profile
    FScopedWriteLock WriteLock(ProfileLock);
    
    // Generate a new GUID for this profile
    CurrentProfile.ProfileId = FGuid::NewGuid();
    CurrentProfile.ProfileName = FString::Printf(TEXT("Auto_%s_%s"), 
                                                *UEnum::GetValueAsString(CurrentProfile.GPUInfo.Vendor),
                                                *FDateTime::Now().ToString());
    
    CurrentProfile.CreationTime = FDateTime::Now();
    CurrentProfile.LastUpdateTime = FDateTime::Now();
    CurrentProfile.Version = 1;
    CurrentProfile.bIsCustomProfile = false;
    CurrentProfile.bIsAutoGenerated = true;
    
    // Clear existing operation profiles
    CurrentProfile.OperationProfiles.Empty();
    
    // Re-detect GPU capabilities
    DetectGPUCapabilities();
    
    // Create default operation profiles
    CreateDefaultProfiles();
    
    // Apply vendor-specific optimizations
    ApplyVendorSpecificOptimizations(CurrentProfile);
    
    // Save the generated profile
    SaveProfile();
    
    UE_LOG(LogGPUProfiler, Log, TEXT("Generated hardware-specific profile: %s"), *CurrentProfile.ProfileName);
}

void FHardwareProfileManager::ResetPerformanceMetrics()
{
    FScopedWriteLock WriteLock(ProfileLock);
    
    for (auto& Pair : CurrentProfile.OperationProfiles)
    {
        Pair.Value.Metrics.Reset();
    }
    
    UE_LOG(LogGPUProfiler, Log, TEXT("Reset all performance metrics"));
}

FHardwareProfile FHardwareProfileManager::CreateCustomProfile(const FString& ProfileName)
{
    FScopedReadLock ReadLock(ProfileLock);
    
    // Create a copy of the current profile
    FHardwareProfile CustomProfile = CurrentProfile;
    
    // Update profile information
    CustomProfile.ProfileId = FGuid::NewGuid();
    CustomProfile.ProfileName = ProfileName.IsEmpty() ? TEXT("CustomProfile") : ProfileName;
    CustomProfile.CreationTime = FDateTime::Now();
    CustomProfile.LastUpdateTime = FDateTime::Now();
    CustomProfile.Version = 1;
    CustomProfile.bIsCustomProfile = true;
    CustomProfile.bIsAutoGenerated = false;
    
    // Reset performance metrics in the custom profile
    for (auto& Pair : CustomProfile.OperationProfiles)
    {
        Pair.Value.Metrics.Reset();
    }
    
    return CustomProfile;
}

TArray<FBenchmarkResult> FHardwareProfileManager::RunBenchmark(ESDFOperationType OperationType, int32 IterationCount, bool bFullParameterSpace)
{
    UE_LOG(LogGPUProfiler, Log, TEXT("Running benchmark for operation: %s"), *UEnum::GetValueAsString(OperationType));
    
    TArray<FBenchmarkResult> Results;
    
    // If full parameter space is requested, we'll try different combinations of parameters
    if (bFullParameterSpace)
    {
        // Define parameter space to search
        TArray<FIntVector> WorkGroupSizes = {
            FIntVector(4, 4, 4),
            FIntVector(8, 8, 4),
            FIntVector(16, 16, 2),
            FIntVector(32, 8, 2),
            FIntVector(16, 8, 4),
            FIntVector(32, 4, 4)
        };
        
        TArray<EMemoryStrategy> MemoryStrategies = {
            EMemoryStrategy::Dedicated,
            EMemoryStrategy::Staged,
            EMemoryStrategy::Unified,
            EMemoryStrategy::Tiled
        };
        
        TArray<EComputePrecision> Precisions = {
            EComputePrecision::Full,
            EComputePrecision::Half,
            EComputePrecision::Mixed
        };
        
        // Create a progress dialog for lengthy benchmark
        FScopedSlowTask BenchmarkProgress(WorkGroupSizes.Num() * MemoryStrategies.Num() * Precisions.Num(), 
                                     NSLOCTEXT("HardwareProfileManager", "RunningBenchmarks", "Running SDF Benchmarks..."));
        BenchmarkProgress.MakeDialog();
        
        // Run benchmarks for each combination of parameters
        for (const FIntVector& WorkGroupSize : WorkGroupSizes)
        {
            for (EMemoryStrategy MemoryStrategy : MemoryStrategies)
            {
                for (EComputePrecision Precision : Precisions)
                {
                    if (BenchmarkProgress.ShouldCancel())
                    {
                        UE_LOG(LogGPUProfiler, Warning, TEXT("Benchmark canceled by user"));
                        return Results;
                    }
                    
                    BenchmarkProgress.EnterProgressFrame(1, FText::Format(
                        NSLOCTEXT("HardwareProfileManager", "BenchmarkingOperation", "Benchmarking {0} with {1}x{2}x{3}"),
                        FText::FromString(UEnum::GetValueAsString(OperationType)),
                        FText::AsNumber(WorkGroupSize.X),
                        FText::AsNumber(WorkGroupSize.Y),
                        FText::AsNumber(WorkGroupSize.Z)
                    ));
                    
                    // Determine dataset size based on operation type
                    int32 DatasetSize = 0;
                    switch (OperationType)
                    {
                        case ESDFOperationType::Union:
                        case ESDFOperationType::Subtraction:
                        case ESDFOperationType::Intersection:
                            DatasetSize = 128; // 128³ voxels for CSG operations
                            break;
                            
                        case ESDFOperationType::Smoothing:
                            DatasetSize = 64; // 64³ voxels for smoothing (more computationally intensive)
                            break;
                            
                        case ESDFOperationType::Evaluation:
                        case ESDFOperationType::Gradient:
                            DatasetSize = 256; // 256³ voxels for evaluations (less intensive per voxel)
                            break;
                            
                        case ESDFOperationType::NarrowBandUpdate:
                            DatasetSize = 128; // 128³ voxels for narrow band updates
                            break;
                            
                        case ESDFOperationType::MaterialTransition:
                            DatasetSize = 96; // 96³ voxels for material transitions
                            break;
                            
                        case ESDFOperationType::VolumeRender:
                            DatasetSize = 256; // 256³ voxels for volume rendering
                            break;
                            
                        default:
                            DatasetSize = 128; // Default size
                            break;
                    }
                    
                    // Run the benchmark with these parameters
                    FBenchmarkResult Result = BenchmarkWithParameters(
                        OperationType,
                        WorkGroupSize,
                        MemoryStrategy,
                        Precision,
                        IterationCount,
                        DatasetSize
                    );
                    
                    Results.Add(Result);
                    
                    // Log the result
                    UE_LOG(LogGPUProfiler, Log, TEXT("Benchmark result for %s - WorkGroup: [%d,%d,%d], Strategy: %s, Precision: %s - Time: %.2f ms"),
                           *UEnum::GetValueAsString(OperationType),
                           WorkGroupSize.X, WorkGroupSize.Y, WorkGroupSize.Z,
                           *UEnum::GetValueAsString(MemoryStrategy),
                           *UEnum::GetValueAsString(Precision),
                           Result.AverageExecutionTimeMs);
                }
            }
        }
    }
    else
    {
        // For quick benchmarking, just use the current profile settings
        FScopedReadLock ReadLock(ProfileLock);
        
        const FSDFOperationProfile* Profile = CurrentProfile.OperationProfiles.Find(OperationType);
        if (!Profile)
        {
            UE_LOG(LogGPUProfiler, Error, TEXT("No profile found for operation type: %s"), *UEnum::GetValueAsString(OperationType));
            return Results;
        }
        
        FIntVector WorkGroupSize(Profile->WorkGroupSizeX, Profile->WorkGroupSizeY, Profile->WorkGroupSizeZ);
        EMemoryStrategy MemoryStrategy = Profile->MemoryStrategy;
        EComputePrecision Precision = Profile->Precision;
        
        // Determine dataset size based on operation type
        int32 DatasetSize = 128; // Default
        
        // Create a progress dialog for benchmark
        FScopedSlowTask BenchmarkProgress(1, NSLOCTEXT("HardwareProfileManager", "RunningBenchmark", "Running SDF Benchmark..."));
        BenchmarkProgress.MakeDialog();
        BenchmarkProgress.EnterProgressFrame(1);
        
        // Run the benchmark with current parameters
        FBenchmarkResult Result = BenchmarkWithParameters(
            OperationType,
            WorkGroupSize,
            MemoryStrategy,
            Precision,
            IterationCount,
            DatasetSize
        );
        
        Results.Add(Result);
        
        // Log the result
        UE_LOG(LogGPUProfiler, Log, TEXT("Benchmark result for %s - WorkGroup: [%d,%d,%d], Strategy: %s, Precision: %s - Time: %.2f ms"),
               *UEnum::GetValueAsString(OperationType),
               WorkGroupSize.X, WorkGroupSize.Y, WorkGroupSize.Z,
               *UEnum::GetValueAsString(MemoryStrategy),
               *UEnum::GetValueAsString(Precision),
               Result.AverageExecutionTimeMs);
    }
    
    // Update profile with benchmark results
    UpdateProfileWithBenchmarkResults(Results);
    
    return Results;
}

void FHardwareProfileManager::RunComprehensiveBenchmarks(int32 IterationCount)
{
    UE_LOG(LogGPUProfiler, Log, TEXT("Running comprehensive benchmarks for all operation types"));
    
    // Confirm with user since this is a time-consuming operation
    EAppReturnType::Type Response = FMessageDialog::Open(EAppMsgType::YesNo,
        FText::FromString(TEXT("Comprehensive benchmarks may take several minutes to complete. Continue?")));
    
    if (Response != EAppReturnType::Yes)
    {
        UE_LOG(LogGPUProfiler, Log, TEXT("Comprehensive benchmarks canceled by user"));
        return;
    }
    
    // Create a progress dialog for the entire benchmark process
    FScopedSlowTask OverallProgress(10, NSLOCTEXT("HardwareProfileManager", "ComprehensiveBenchmarks", "Running Comprehensive SDF Benchmarks..."));
    OverallProgress.MakeDialog();
    
    // Run benchmarks for each operation type
    TArray<ESDFOperationType> OperationTypes = {
        ESDFOperationType::Union,
        ESDFOperationType::Subtraction,
        ESDFOperationType::Intersection,
        ESDFOperationType::Smoothing,
        ESDFOperationType::Evaluation,
        ESDFOperationType::Gradient,
        ESDFOperationType::NarrowBandUpdate,
        ESDFOperationType::MaterialTransition,
        ESDFOperationType::VolumeRender,
        ESDFOperationType::Custom
    };
    
    for (ESDFOperationType OperationType : OperationTypes)
    {
        OverallProgress.EnterProgressFrame(1, FText::Format(
            NSLOCTEXT("HardwareProfileManager", "BenchmarkingOperationType", "Benchmarking {0}..."),
            FText::FromString(UEnum::GetValueAsString(OperationType))
        ));
        
        // Run benchmark with current profile settings (not full parameter space)
        RunBenchmark(OperationType, IterationCount, false);
    }
    
    // Save the updated profile
    SaveProfile(CurrentProfile.ProfileName + TEXT("_Benchmarked"));
    
    UE_LOG(LogGPUProfiler, Log, TEXT("Comprehensive benchmarks completed successfully"));
}

FBenchmarkResult FHardwareProfileManager::BenchmarkWithParameters(
    ESDFOperationType OperationType,
    const FIntVector& WorkGroupSize,
    EMemoryStrategy MemoryStrategy,
    EComputePrecision Precision,
    int32 IterationCount,
    int32 DatasetSize)
{
    // Create the result structure
    FBenchmarkResult Result;
    Result.OperationType = OperationType;
    Result.WorkGroupSize = WorkGroupSize;
    Result.MemoryStrategy = MemoryStrategy;
    Result.Precision = Precision;
    Result.IterationCount = IterationCount;
    Result.DatasetSize = DatasetSize;
    
    // This would typically involve dispatching actual compute shaders and measuring performance
    // Since we can't run the actual SDF operations in this context, we'll simulate the benchmark
    
    // For the purpose of this implementation, we'll use a deterministic pseudo-benchmark based on parameters
    // In a real implementation, this would dispatch actual compute shaders and measure performance
    
    // Create a random stream with seed based on parameters for deterministic results
    FRandomStream Random(
        (uint32)OperationType * 73 +
        WorkGroupSize.X * 17 + WorkGroupSize.Y * 31 + WorkGroupSize.Z * 47 +
        (uint32)MemoryStrategy * 61 + (uint32)Precision * 89 +
        DatasetSize * 101
    );
    
    // Simulate performance of different combinations
    float BaselineMs = 0.0f;
    
    // Base time by operation type
    switch (OperationType)
    {
        case ESDFOperationType::Union:
            BaselineMs = 2.5f;
            break;
        case ESDFOperationType::Subtraction:
            BaselineMs = 2.7f;
            break;
        case ESDFOperationType::Intersection:
            BaselineMs = 2.3f;
            break;
        case ESDFOperationType::Smoothing:
            BaselineMs = 8.0f;
            break;
        case ESDFOperationType::Evaluation:
            BaselineMs = 1.8f;
            break;
        case ESDFOperationType::Gradient:
            BaselineMs = 6.5f;
            break;
        case ESDFOperationType::NarrowBandUpdate:
            BaselineMs = 3.2f;
            break;
        case ESDFOperationType::MaterialTransition:
            BaselineMs = 7.8f;
            break;
        case ESDFOperationType::VolumeRender:
            BaselineMs = 4.5f;
            break;
        default:
            BaselineMs = 5.0f;
            break;
    }
    
    // Adjust by workgroup size efficiency
    // Optimal sizes depend on the hardware and operation
    float WorkGroupEfficiency = 1.0f;
    
    // This simulates that different workgroup sizes are optimal for different operations
    const int32 TotalThreads = WorkGroupSize.X * WorkGroupSize.Y * WorkGroupSize.Z;
    
    // Simulate most GPUs prefer workgroup sizes that are multiples of 32 or 64
    if (TotalThreads % 64 == 0)
    {
        WorkGroupEfficiency = 0.85f; // Better efficiency
    }
    else if (TotalThreads % 32 == 0)
    {
        WorkGroupEfficiency = 0.9f; // Good efficiency
    }
    else if (TotalThreads < 64)
    {
        WorkGroupEfficiency = 1.5f; // Poor efficiency for very small workgroups
    }
    else if (TotalThreads > 512)
    {
        WorkGroupEfficiency = 1.3f; // Poor efficiency for very large workgroups
    }
    
    // Adjust by memory strategy
    float MemoryStrategyFactor = 1.0f;
    switch (MemoryStrategy)
    {
        case EMemoryStrategy::Dedicated:
            MemoryStrategyFactor = 0.9f; // Generally fastest for pure GPU operations
            break;
        case EMemoryStrategy::Unified:
            MemoryStrategyFactor = 1.1f; // Slightly slower but better for CPU readback
            break;
        case EMemoryStrategy::Staged:
            MemoryStrategyFactor = 1.05f; // Good compromise
            break;
        case EMemoryStrategy::Tiled:
            MemoryStrategyFactor = 0.95f; // Good for large datasets
            break;
        default:
            MemoryStrategyFactor = 1.0f;
            break;
    }
    
    // Adjust by precision
    float PrecisionFactor = 1.0f;
    switch (Precision)
    {
        case EComputePrecision::Full:
            PrecisionFactor = 1.0f; // Baseline
            break;
        case EComputePrecision::Half:
            PrecisionFactor = 0.7f; // Faster but less precise
            break;
        case EComputePrecision::Mixed:
            PrecisionFactor = 0.85f; // Good compromise
            break;
        case EComputePrecision::Variable:
            PrecisionFactor = 0.8f; // Good for adaptive precision
            break;
        default:
            PrecisionFactor = 1.0f;
            break;
    }
    
    // Scale by dataset size (cubic relationship)
    const float DatasetScale = FMath::Pow(static_cast<float>(DatasetSize) / 128.0f, 3.0f);
    
    // Calculate final simulated time
    float SimulatedTimeMs = BaselineMs * WorkGroupEfficiency * MemoryStrategyFactor * PrecisionFactor * DatasetScale;
    
    // Add some random variation
    TArray<float> RunTimes;
    float TotalTime = 0.0f;
    
    for (int32 i = 0; i < IterationCount; ++i)
    {
        // Add up to 20% random variation
        float Variation = 1.0f + (Random.FRandRange(-0.2f, 0.2f));
        float IterationTime = SimulatedTimeMs * Variation;
        RunTimes.Add(IterationTime);
        TotalTime += IterationTime;
    }
    
    // Calculate average and standard deviation
    Result.AverageExecutionTimeMs = TotalTime / static_cast<float>(IterationCount);
    
    // Calculate standard deviation
    float SumSquaredDifferences = 0.0f;
    for (float Time : RunTimes)
    {
        float Difference = Time - Result.AverageExecutionTimeMs;
        SumSquaredDifferences += Difference * Difference;
    }
    Result.StdDeviation = FMath::Sqrt(SumSquaredDifferences / static_cast<float>(IterationCount));
    
    return Result;
}

void FHardwareProfileManager::UpdateProfileWithBenchmarkResults(const TArray<FBenchmarkResult>& Results)
{
    if (Results.Num() == 0)
    {
        UE_LOG(LogGPUProfiler, Warning, TEXT("No benchmark results to update profile with"));
        return;
    }
    
    UE_LOG(LogGPUProfiler, Log, TEXT("Updating profile with %d benchmark results"), Results.Num());
    
    FHardwareProfile UpdatedProfile = GetProfileCopy();
    bool bProfileChanged = false;
    
    // Group results by operation type
    TMap<ESDFOperationType, TArray<FBenchmarkResult>> ResultsByOperation;
    
    for (const FBenchmarkResult& Result : Results)
    {
        ResultsByOperation.FindOrAdd(Result.OperationType).Add(Result);
    }
    
    // Process each operation type
    for (const auto& Pair : ResultsByOperation)
    {
        ESDFOperationType OperationType = Pair.Key;
        const TArray<FBenchmarkResult>& OperationResults = Pair.Value;
        
        // Find the best result (lowest average execution time)
        const FBenchmarkResult* BestResult = nullptr;
        float BestTime = FLT_MAX;
        
        for (const FBenchmarkResult& Result : OperationResults)
        {
            // Consider the standard deviation as well for stability
            float AdjustedTime = Result.AverageExecutionTimeMs + (Result.StdDeviation * 0.2f);
            
            if (AdjustedTime < BestTime)
            {
                BestTime = AdjustedTime;
                BestResult = &Result;
            }
        }
        
        // Update the profile with the best parameters
        if (BestResult)
        {
            FSDFOperationProfile& Profile = UpdatedProfile.OperationProfiles.FindOrAdd(OperationType);
            
            // Update work group size
            Profile.WorkGroupSizeX = BestResult->WorkGroupSize.X;
            Profile.WorkGroupSizeY = BestResult->WorkGroupSize.Y;
            Profile.WorkGroupSizeZ = BestResult->WorkGroupSize.Z;
            
            // Update memory strategy
            Profile.MemoryStrategy = BestResult->MemoryStrategy;
            
            // Update precision
            Profile.Precision = BestResult->Precision;
            
            // Update CPU fallback threshold based on benchmark results
            Profile.CPUFallbackThresholdMs = BestResult->AverageExecutionTimeMs * 2.0f; // Double the time as fallback threshold
            
            bProfileChanged = true;
            
            UE_LOG(LogGPUProfiler, Log, TEXT("Updated profile for %s - Work Group: [%d,%d,%d], Memory: %s, Precision: %s, Time: %.2f ms"),
                   *UEnum::GetValueAsString(OperationType),
                   Profile.WorkGroupSizeX, Profile.WorkGroupSizeY, Profile.WorkGroupSizeZ,
                   *UEnum::GetValueAsString(Profile.MemoryStrategy),
                   *UEnum::GetValueAsString(Profile.Precision),
                   BestResult->AverageExecutionTimeMs);
        }
    }
    
    if (bProfileChanged)
    {
        // Apply the updated profile
        UpdatedProfile.LastUpdateTime = FDateTime::Now();
        UpdatedProfile.Version++;
        
        ApplyProfileUpdates(UpdatedProfile);
        
        UE_LOG(LogGPUProfiler, Log, TEXT("Profile updated with benchmark results. New version: %d"), UpdatedProfile.Version);
    }
}

// IService interface implementation
bool FHardwareProfileManager::IsInitialized() const
{
    return bIsInitialized;
}

FName FHardwareProfileManager::GetServiceName() const
{
    return FName("HardwareProfileManager");
}

int32 FHardwareProfileManager::GetPriority() const
{
    return 100; // Adjust priority as needed for the service hierarchy
}
