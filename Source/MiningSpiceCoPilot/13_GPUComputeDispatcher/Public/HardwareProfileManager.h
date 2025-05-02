#pragma once

#include "CoreMinimal.h"
#include "ComputeOperationTypes.h"

/**
 * Hardware capability detection and profile management
 * Detects GPU capabilities and creates optimal profiles for SDF operations
 * Provides device-specific optimization strategies
 */
class MININGSPICECOPILOT_API FHardwareProfileManager
{
public:
    FHardwareProfileManager();
    ~FHardwareProfileManager();
    
    // Hardware detection
    bool DetectHardwareCapabilities();
    const FHardwareProfile& GetCurrentProfile() const;
    
    // Profile selection and management
    bool LoadProfileForHardware(const FString& DeviceName, EGPUVendor VendorId);
    bool CreateCustomProfile(const FHardwareProfile& Profile);
    bool SaveProfiles();
    bool LoadProfile(const FHardwareProfile& Profile);
    
    // Profile-based optimizations
    uint32 GetOptimalBlockSizeForOperation(int32 OpType);
    bool ShouldUseAsyncCompute() const;
    bool SupportsRayTracing() const;
    
    // NUMA/Hardware topology
    int32 GetGPUPreferredNumaNode() const;
    
    // Benchmarking
    bool RunBenchmark();
    void CalculateOptimalParameters();
    
private:
    // Member variables
    TMap<FString, FHardwareProfile> KnownProfiles;
    FHardwareProfile CurrentProfile;
    bool bProfilesLoaded;
    TArray<FString> SupportedExtensions;
    
    // Hardware information
    uint32 ComputeUnits;
    uint64 TotalVRAM;
    FString GPUName;
    EGPUVendor GPUVendor;
    bool bSupportsRayTracing;
    bool bSupportsAsyncCompute;
    bool bSupportsWaveIntrinsics;
    uint32 WavefrontSize;
    uint32 SharedMemoryBytes;
    int32 PreferredNumaNode;
    
    // Detection helpers
    void DetectGPUSpecs();
    void DetectMemoryLimits();
    void DetectShaderSupport();
    void DetectNumaTopology();
    
    // Profile management
    FString GetProfilePath() const;
    bool SaveProfileToFile(const FString& ProfileName, const FHardwareProfile& Profile);
    bool LoadProfileFromFile(const FString& ProfileName, FHardwareProfile& OutProfile);
    
    // Optimization lookup tables
    TMap<int32, uint32> OptimalBlockSizes;
    TMap<int32, bool> AsyncCompatibleOperations;
    TMap<int32, float> OperationComplexityRatings;
};