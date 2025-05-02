#include "../Public/HardwareProfileManager.h"
#include "../Public/GPUDispatcherLogging.h"
#include "../../3_ThreadingTaskSystem/Public/NumaHelpers.h"

#include "Misc/ConfigCacheIni.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"
#include "GenericPlatform/GenericPlatformMisc.h"

FHardwareProfileManager::FHardwareProfileManager()
    : bProfilesLoaded(false)
    , ComputeUnits(0)
    , TotalVRAM(0)
    , GPUVendor(EGPUVendor::Unknown)
    , bSupportsRayTracing(false)
    , bSupportsAsyncCompute(false)
    , bSupportsWaveIntrinsics(false)
    , WavefrontSize(32)
    , SharedMemoryBytes(32768)
    , PreferredNumaNode(0)
{
    // Initialize optimal block sizes with defaults
    OptimalBlockSizes.Add(0, 8); // Union
    OptimalBlockSizes.Add(1, 8); // Difference
    OptimalBlockSizes.Add(2, 8); // Intersection
    OptimalBlockSizes.Add(3, 16); // Smoothing (larger for better coherence)
    OptimalBlockSizes.Add(4, 8); // Gradient
    OptimalBlockSizes.Add(5, 8); // Evaluation
    OptimalBlockSizes.Add(6, 16); // MaterialBlend (larger for better coherence)
    OptimalBlockSizes.Add(7, 8); // Erosion
    OptimalBlockSizes.Add(8, 8); // Dilation
    OptimalBlockSizes.Add(9, 16); // ChannelTransfer (larger for better coherence)
    OptimalBlockSizes.Add(10, 8); // FieldOperation
}

FHardwareProfileManager::~FHardwareProfileManager()
{
    SaveProfiles();
}

bool FHardwareProfileManager::DetectHardwareCapabilities()
{
    GPU_DISPATCHER_SCOPED_TIMER(DetectHardwareCapabilities);
    
    // Clear previous data
    SupportedExtensions.Empty();
    
    // Detect GPU specs
    DetectGPUSpecs();
    
    // Detect memory limits
    DetectMemoryLimits();
    
    // Detect shader support
    DetectShaderSupport();
    
    // Detect NUMA topology
    DetectNumaTopology();
    
    // Create compatible profile
    CurrentProfile.bSupportsRayTracing = bSupportsRayTracing;
    CurrentProfile.bSupportsAsyncCompute = bSupportsAsyncCompute;
    CurrentProfile.ComputeUnits = ComputeUnits;
    CurrentProfile.MaxWorkgroupSize = FMath::Min(1024u, ComputeUnits * 32); // Estimate based on CUs
    CurrentProfile.WavefrontSize = WavefrontSize;
    CurrentProfile.bSupportsWaveIntrinsics = bSupportsWaveIntrinsics;
    CurrentProfile.SharedMemoryBytes = SharedMemoryBytes;
    CurrentProfile.VendorId = GPUVendor;
    CurrentProfile.DeviceName = GPUName;
    
    // Try to load a matching profile
    bool bProfileLoaded = LoadProfileForHardware(GPUName, GPUVendor);
    if (!bProfileLoaded)
    {
        // If no matching profile, create a new one
        GPU_DISPATCHER_LOG_DEBUG("No matching hardware profile found, creating new profile");
        
        // Run benchmark to optimize parameters
        RunBenchmark();
        
        // Save the new profile
        SaveProfiles();
    }
    
    GPU_DISPATCHER_LOG_DEBUG("Hardware capabilities detected: %s, %d CUs, %llu MB VRAM", 
        *GPUName, ComputeUnits, TotalVRAM / (1024 * 1024));
    GPU_DISPATCHER_LOG_DEBUG("Ray Tracing: %s, Async Compute: %s, Wave Intrinsics: %s",
        bSupportsRayTracing ? TEXT("Yes") : TEXT("No"),
        bSupportsAsyncCompute ? TEXT("Yes") : TEXT("No"),
        bSupportsWaveIntrinsics ? TEXT("Yes") : TEXT("No"));
    
    return true;
}

const FHardwareProfile& FHardwareProfileManager::GetCurrentProfile() const
{
    return CurrentProfile;
}

bool FHardwareProfileManager::LoadProfileForHardware(const FString& DeviceName, EGPUVendor VendorId)
{
    // First try to load profiles if not already loaded
    if (!bProfilesLoaded)
    {
        // Load all known profiles
        TArray<FString> ProfileFiles;
        IFileManager::Get().FindFiles(ProfileFiles, *(GetProfilePath() + TEXT("*.json")), true, false);
        
        for (const FString& ProfileFile : ProfileFiles)
        {
            FHardwareProfile Profile;
            if (LoadProfileFromFile(ProfileFile, Profile))
            {
                KnownProfiles.Add(Profile.DeviceName, Profile);
            }
        }
        
        bProfilesLoaded = true;
    }
    
    // Try to find exact match
    if (FHardwareProfile* ExactProfile = KnownProfiles.Find(DeviceName))
    {
        CurrentProfile = *ExactProfile;
        GPU_DISPATCHER_LOG_DEBUG("Loaded exact matching profile for %s", *DeviceName);
        return true;
    }
    
    // Try to find a similar profile (same vendor, similar specs)
    for (const auto& ProfilePair : KnownProfiles)
    {
        const FHardwareProfile& Profile = ProfilePair.Value;
        
        // Check vendor ID
        if (Profile.VendorId == VendorId)
        {
            // Check compute units within 25% difference
            float CURatio = (float)ComputeUnits / (float)Profile.ComputeUnits;
            if (CURatio >= 0.75f && CURatio <= 1.33f)
            {
                // Similar enough, use this profile with some adjustments
                CurrentProfile = Profile;
                
                // Adjust for compute unit differences
                CurrentProfile.ComputeUnits = ComputeUnits;
                CurrentProfile.DeviceName = DeviceName;
                
                // Scale block sizes based on compute unit ratio
                for (auto& BlockSizePair : OptimalBlockSizes)
                {
                    BlockSizePair.Value = FMath::Max(8u, (uint32)(BlockSizePair.Value * CURatio));
                }
                
                GPU_DISPATCHER_LOG_DEBUG("Loaded similar profile from %s for %s (CU ratio: %.2f)",
                    *Profile.DeviceName, *DeviceName, CURatio);
                return true;
            }
        }
    }
    
    // No matching profile found
    return false;
}

bool FHardwareProfileManager::CreateCustomProfile(const FHardwareProfile& Profile)
{
    // Store the profile
    KnownProfiles.Add(Profile.DeviceName, Profile);
    
    // Save to file
    return SaveProfileToFile(Profile.DeviceName, Profile);
}

bool FHardwareProfileManager::SaveProfiles()
{
    bool bAllSaved = true;
    
    // Save current profile if it exists
    if (!CurrentProfile.DeviceName.IsEmpty())
    {
        bAllSaved &= SaveProfileToFile(CurrentProfile.DeviceName, CurrentProfile);
    }
    
    // Save all known profiles
    for (const auto& ProfilePair : KnownProfiles)
    {
        bAllSaved &= SaveProfileToFile(ProfilePair.Key, ProfilePair.Value);
    }
    
    return bAllSaved;
}

bool FHardwareProfileManager::LoadProfile(const FHardwareProfile& Profile)
{
    // Update current profile
    CurrentProfile = Profile;
    
    // Update optimal block sizes based on the profile
    // This would normally be stored within the profile
    
    // Add to known profiles
    KnownProfiles.FindOrAdd(Profile.DeviceName) = Profile;
    
    return true;
}

uint32 FHardwareProfileManager::GetOptimalBlockSizeForOperation(int32 OpType)
{
    // Get optimal block size for this operation type
    uint32* BlockSize = OptimalBlockSizes.Find(OpType);
    if (BlockSize)
    {
        return *BlockSize;
    }
    
    // Default to 8x8x1 if not found
    return 8;
}

bool FHardwareProfileManager::ShouldUseAsyncCompute() const
{
    return CurrentProfile.bSupportsAsyncCompute;
}

bool FHardwareProfileManager::SupportsRayTracing() const
{
    return CurrentProfile.bSupportsRayTracing;
}

int32 FHardwareProfileManager::GetGPUPreferredNumaNode() const
{
    return PreferredNumaNode;
}

bool FHardwareProfileManager::RunBenchmark()
{
    GPU_DISPATCHER_LOG_DEBUG("Running benchmark to optimize parameters...");
    
    // In a real implementation, this would run various SDF operations
    // to find optimal block sizes and other parameters
    
    // For the purpose of this implementation, we'll use some heuristics
    
    // Adjust block sizes based on compute units
    // More compute units = larger block sizes for better parallelism
    float ScaleFactor = FMath::Clamp((float)ComputeUnits / 16.0f, 0.5f, 2.0f);
    
    for (auto& BlockSizePair : OptimalBlockSizes)
    {
        // Scale based on compute units but keep within reasonable bounds
        // and ensure it's a multiple of WavefrontSize / 4 for better occupancy
        uint32 OptimalSize = FMath::Max(8u, (uint32)(8.0f * ScaleFactor));
        OptimalSize = FMath::Min(32u, OptimalSize);
        
        // Round to multiple of 4 or 8 depending on operation type
        uint32 Alignment = (BlockSizePair.Key == 3 || BlockSizePair.Key == 6) ? 8 : 4;
        OptimalSize = (OptimalSize + Alignment - 1) & ~(Alignment - 1);
        
        BlockSizePair.Value = OptimalSize;
    }
    
    // Optimize for specific operation types based on vendor
    switch (GPUVendor)
    {
        case EGPUVendor::AMD:
            // AMD prefers larger block sizes for compute-heavy operations
            OptimalBlockSizes[3] = 16; // Smoothing
            OptimalBlockSizes[6] = 16; // MaterialBlend
            break;
            
        case EGPUVendor::NVIDIA:
            // NVIDIA often works better with smaller block sizes but more blocks
            OptimalBlockSizes[3] = 8; // Smoothing
            OptimalBlockSizes[6] = 8; // MaterialBlend
            break;
            
        case EGPUVendor::Intel:
            // Intel integrated GPUs need smaller block sizes
            for (auto& BlockSizePair : OptimalBlockSizes)
            {
                BlockSizePair.Value = FMath::Min(BlockSizePair.Value, 8u);
            }
            break;
            
        default:
            break;
    }
    
    GPU_DISPATCHER_LOG_DEBUG("Benchmark complete, parameters optimized");
    
    // Now calculate other optimized parameters
    CalculateOptimalParameters();
    
    return true;
}

void FHardwareProfileManager::CalculateOptimalParameters()
{
    // Set operation compatibility based on hardware capabilities
    
    // Operations that require wave intrinsics
    bool bSupportsWaveOps = CurrentProfile.bSupportsWaveIntrinsics;
    
    // Operations that benefit from async compute
    bool bSupportsAsync = CurrentProfile.bSupportsAsyncCompute;
    
    for (int32 i = 0; i <= 10; ++i)
    {
        // Most operations are compatible with most GPUs
        AsyncCompatibleOperations.Add(i, bSupportsAsync);
        
        // Estimate complexity rating for each operation type
        switch (i)
        {
            case 0: // Union - simple
                OperationComplexityRatings.Add(i, 1.0f);
                break;
            case 1: // Difference - simple
                OperationComplexityRatings.Add(i, 1.0f);
                break;
            case 2: // Intersection - simple
                OperationComplexityRatings.Add(i, 1.0f);
                break;
            case 3: // Smoothing - complex
                OperationComplexityRatings.Add(i, 3.0f);
                break;
            case 4: // Gradient - moderately complex
                OperationComplexityRatings.Add(i, 2.0f);
                break;
            case 5: // Evaluation - simple
                OperationComplexityRatings.Add(i, 1.0f);
                break;
            case 6: // MaterialBlend - complex
                OperationComplexityRatings.Add(i, 3.0f);
                break;
            case 7: // Erosion - moderately complex
                OperationComplexityRatings.Add(i, 2.0f);
                break;
            case 8: // Dilation - moderately complex
                OperationComplexityRatings.Add(i, 2.0f);
                break;
            case 9: // ChannelTransfer - simple
                OperationComplexityRatings.Add(i, 1.0f);
                break;
            case 10: // FieldOperation - depends on the specific operation
                OperationComplexityRatings.Add(i, 2.0f);
                break;
        }
    }
}

void FHardwareProfileManager::DetectGPUSpecs()
{
    GPU_DISPATCHER_LOG_DEBUG("Using simplified GPU detection without RHI");

    // Set default values for simplified implementation
    GPUName = TEXT("High-End GPU");
    ComputeUnits = 32;  // Reasonable default
    GPUVendor = EGPUVendor::NVIDIA;  // Default to NVIDIA
    WavefrontSize = 32;
    
    // Use configuration settings if available
    FString ConfigVendor;
    if (GConfig->GetString(TEXT("GPUDispatcher"), TEXT("GPUVendor"), ConfigVendor, GEngineIni))
    {
        if (ConfigVendor.Equals(TEXT("NVIDIA"), ESearchCase::IgnoreCase))
        {
            GPUVendor = EGPUVendor::NVIDIA;
            WavefrontSize = 32;
            GPUName = TEXT("NVIDIA GPU");
        }
        else if (ConfigVendor.Equals(TEXT("AMD"), ESearchCase::IgnoreCase))
        {
            GPUVendor = EGPUVendor::AMD;
            WavefrontSize = 64;
            GPUName = TEXT("AMD GPU");
        }
        else if (ConfigVendor.Equals(TEXT("Intel"), ESearchCase::IgnoreCase))
        {
            GPUVendor = EGPUVendor::Intel;
            WavefrontSize = 16;
            GPUName = TEXT("Intel GPU");
        }
    }
    
    // Try to get compute units from config
    int32 ConfigComputeUnits = 0;
    if (GConfig->GetInt(TEXT("GPUDispatcher"), TEXT("ComputeUnits"), ConfigComputeUnits, GEngineIni) && ConfigComputeUnits > 0)
    {
        ComputeUnits = ConfigComputeUnits;
    }
    
    // Set capabilities based on vendor (simplified without RHI)
    if (GPUVendor == EGPUVendor::NVIDIA)
    {
        SharedMemoryBytes = 48 * 1024; // 48KB
        bSupportsRayTracing = true;
        bSupportsAsyncCompute = true;
        bSupportsWaveIntrinsics = true;
    }
    else if (GPUVendor == EGPUVendor::AMD)
    {
        SharedMemoryBytes = 64 * 1024; // 64KB
        bSupportsRayTracing = true;
        bSupportsAsyncCompute = true;
        bSupportsWaveIntrinsics = true;
    }
    else if (GPUVendor == EGPUVendor::Intel)
    {
        SharedMemoryBytes = 32 * 1024; // 32KB
        bSupportsRayTracing = false;
        bSupportsAsyncCompute = false;
        bSupportsWaveIntrinsics = false;
    }
    else
    {
        // Default capabilities for unknown GPUs
        SharedMemoryBytes = 32 * 1024; // 32KB
        bSupportsRayTracing = false;
        bSupportsAsyncCompute = false;
        bSupportsWaveIntrinsics = false;
    }
    
    GPU_DISPATCHER_LOG_DEBUG("Simplified GPU detection complete - Vendor: %s, CUs: %d", 
        *FString(GPUVendor == EGPUVendor::NVIDIA ? "NVIDIA" : 
                  GPUVendor == EGPUVendor::AMD ? "AMD" : 
                  GPUVendor == EGPUVendor::Intel ? "Intel" : "Unknown"),
        ComputeUnits);
}

void FHardwareProfileManager::DetectMemoryLimits()
{
    GPU_DISPATCHER_LOG_DEBUG("Using simplified memory detection without RHI");
    
    // Default to conservative values
    TotalVRAM = 4ULL * 1024 * 1024 * 1024; // 4GB
    
    // Try to get VRAM from config
    int32 ConfigVRAMGB = 0;
    if (GConfig->GetInt(TEXT("GPUDispatcher"), TEXT("VRAM_GB"), ConfigVRAMGB, GEngineIni) && ConfigVRAMGB > 0)
    {
        TotalVRAM = static_cast<uint64>(ConfigVRAMGB) * 1024 * 1024 * 1024;
    }
    else
    {
        // Estimate based on vendor
        if (GPUVendor == EGPUVendor::NVIDIA)
        {
            TotalVRAM = 8ULL * 1024 * 1024 * 1024; // 8GB
        }
        else if (GPUVendor == EGPUVendor::AMD)
        {
            TotalVRAM = 8ULL * 1024 * 1024 * 1024; // 8GB
        }
        else if (GPUVendor == EGPUVendor::Intel)
        {
            TotalVRAM = 4ULL * 1024 * 1024 * 1024; // 4GB
        }
    }
    
    GPU_DISPATCHER_LOG_DEBUG("Simplified memory detection complete - Total VRAM: %llu GB", 
        TotalVRAM / (1024 * 1024 * 1024));
}

void FHardwareProfileManager::DetectShaderSupport()
{
    GPU_DISPATCHER_LOG_DEBUG("Using simplified shader support detection without RHI");
    
    // Clear and populate with simplified common formats based on vendor
    SupportedExtensions.Empty();
    
    // Add common shader formats for all platforms
    SupportedExtensions.Add(TEXT("SF_VULKAN_SM5"));
    SupportedExtensions.Add(TEXT("SF_GLSL_430"));
    
    // Add vendor-specific shader formats
    if (GPUVendor == EGPUVendor::NVIDIA)
    {
        SupportedExtensions.Add(TEXT("SF_VULKAN_SM6"));
        SupportedExtensions.Add(TEXT("SF_GLSL_460"));
        SupportedExtensions.Add(TEXT("SF_GLSL_SPIRV"));
    }
    else if (GPUVendor == EGPUVendor::AMD)
    {
        SupportedExtensions.Add(TEXT("SF_VULKAN_SM6"));
        SupportedExtensions.Add(TEXT("SF_GLSL_460"));
    }
    else if (GPUVendor == EGPUVendor::Intel)
    {
        // More conservative set for Intel
        SupportedExtensions.Add(TEXT("SF_GLSL_430"));
    }
    
    // Add platform specific formats
    #if PLATFORM_WINDOWS
        SupportedExtensions.Add(TEXT("SF_HLSL_SM5"));
        if (GPUVendor == EGPUVendor::NVIDIA || GPUVendor == EGPUVendor::AMD)
        {
            SupportedExtensions.Add(TEXT("SF_HLSL_SM6"));
        }
    #elif PLATFORM_MAC
        SupportedExtensions.Add(TEXT("SF_METAL_SM5"));
        SupportedExtensions.Add(TEXT("SF_METAL_SM5_NOTESS"));
        SupportedExtensions.Add(TEXT("SF_METAL_MRT"));
    #elif PLATFORM_ANDROID
        SupportedExtensions.Add(TEXT("SF_VULKAN_ES31_ANDROID"));
    #endif
    
    // Log supported formats
    FString SupportedFormatsString = FString::Join(SupportedExtensions, TEXT(", "));
    GPU_DISPATCHER_LOG_VERBOSE("Simplified shader format detection: %s", *SupportedFormatsString);
}

void FHardwareProfileManager::DetectNumaTopology()
{
#if WITH_ENGINE
    // Try to detect the NUMA node closest to the GPU
    // This is highly platform-specific, so we'll use simple heuristics
    
    int32 NumNodes = FPlatformMisc::NumberOfCoresIncludingHyperthreads();
    if (NumNodes <= 1)
    {
        // Single NUMA domain, use node 0
        PreferredNumaNode = 0;
        return;
    }
    
    // On systems with PCIe GPUs, they're often closest to the first NUMA node
    // This is a simplification, as the actual topology depends on motherboard layout
    PreferredNumaNode = 0;
    
    // For integrated GPUs, we'd check the specific CPU it's part of
    if (GPUVendor == EGPUVendor::Intel && !GPUName.Contains(TEXT("Arc")))
    {
        // Use the last NUMA node for integrated GPUs
        // This is a heuristic and may not be accurate for all systems
        PreferredNumaNode = NumNodes - 1;
    }
    
    // For more accurate NUMA node detection, platform-specific code would be needed
    // This might involve checking PCIe bus topology or vendor-specific tools
#endif
}

FString FHardwareProfileManager::GetProfilePath() const
{
    return FPaths::ProjectSavedDir() / TEXT("GPUProfiles/");
}

bool FHardwareProfileManager::SaveProfileToFile(const FString& ProfileName, const FHardwareProfile& Profile)
{
    // Create directory if it doesn't exist
    IFileManager::Get().MakeDirectory(*GetProfilePath(), true);
    
    // Create a JSON object for the profile
    TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
    
    // Add profile properties
    JsonObject->SetStringField("DeviceName", Profile.DeviceName);
    JsonObject->SetNumberField("VendorId", (int32)Profile.VendorId);
    JsonObject->SetBoolField("SupportsRayTracing", Profile.bSupportsRayTracing);
    JsonObject->SetBoolField("SupportsAsyncCompute", Profile.bSupportsAsyncCompute);
    JsonObject->SetNumberField("ComputeUnits", Profile.ComputeUnits);
    JsonObject->SetNumberField("MaxWorkgroupSize", Profile.MaxWorkgroupSize);
    JsonObject->SetNumberField("WavefrontSize", Profile.WavefrontSize);
    JsonObject->SetBoolField("SupportsWaveIntrinsics", Profile.bSupportsWaveIntrinsics);
    JsonObject->SetNumberField("SharedMemoryBytes", Profile.SharedMemoryBytes);
    JsonObject->SetNumberField("L1CacheSizeKB", Profile.L1CacheSizeKB);
    JsonObject->SetNumberField("L2CacheSizeKB", Profile.L2CacheSizeKB);
    JsonObject->SetNumberField("ComputeToPipelineRatio", Profile.ComputeToPipelineRatio);
    
    // Add block sizes
    TSharedPtr<FJsonObject> BlockSizesObject = MakeShared<FJsonObject>();
    for (const auto& BlockSizePair : OptimalBlockSizes)
    {
        BlockSizesObject->SetNumberField(FString::FromInt(BlockSizePair.Key), BlockSizePair.Value);
    }
    JsonObject->SetObjectField("BlockSizes", BlockSizesObject);
    
    // Add async compatibility
    TSharedPtr<FJsonObject> AsyncCompatObject = MakeShared<FJsonObject>();
    for (const auto& AsyncPair : AsyncCompatibleOperations)
    {
        AsyncCompatObject->SetBoolField(FString::FromInt(AsyncPair.Key), AsyncPair.Value);
    }
    JsonObject->SetObjectField("AsyncCompatibility", AsyncCompatObject);
    
    // Serialize to string
    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
    
    // Save to file
    FString FilePath = GetProfilePath() / (FString::Format(TEXT("{0}.json"), { *ProfileName }));
    return FFileHelper::SaveStringToFile(OutputString, *FilePath);
}

bool FHardwareProfileManager::LoadProfileFromFile(const FString& ProfileName, FHardwareProfile& OutProfile)
{
    // Build file path
    FString FilePath = GetProfilePath() / ProfileName;
    if (!FilePath.EndsWith(".json"))
    {
        FilePath += ".json";
    }
    
    // Read file content
    FString FileContent;
    if (!FFileHelper::LoadFileToString(FileContent, *FilePath))
    {
        GPU_DISPATCHER_LOG_WARNING("Failed to load profile from %s", *FilePath);
        return false;
    }
    
    // Parse JSON
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileContent);
    if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
    {
        GPU_DISPATCHER_LOG_WARNING("Failed to parse profile JSON from %s", *FilePath);
        return false;
    }
    
    // Extract profile properties with updated API for UE5.5
    OutProfile.DeviceName = JsonObject->GetStringField(TEXT("DeviceName"));
    OutProfile.VendorId = (EGPUVendor)(int32)JsonObject->GetNumberField(TEXT("VendorId"));
    OutProfile.bSupportsRayTracing = JsonObject->GetBoolField(TEXT("SupportsRayTracing"));
    OutProfile.bSupportsAsyncCompute = JsonObject->GetBoolField(TEXT("SupportsAsyncCompute"));
    OutProfile.ComputeUnits = (uint32)JsonObject->GetNumberField(TEXT("ComputeUnits"));
    OutProfile.MaxWorkgroupSize = (uint32)JsonObject->GetNumberField(TEXT("MaxWorkgroupSize"));
    OutProfile.WavefrontSize = (uint32)JsonObject->GetNumberField(TEXT("WavefrontSize"));
    OutProfile.bSupportsWaveIntrinsics = JsonObject->GetBoolField(TEXT("SupportsWaveIntrinsics"));
    OutProfile.SharedMemoryBytes = (uint32)JsonObject->GetNumberField(TEXT("SharedMemoryBytes"));
    OutProfile.L1CacheSizeKB = (uint32)JsonObject->GetNumberField(TEXT("L1CacheSizeKB"));
    OutProfile.L2CacheSizeKB = (uint32)JsonObject->GetNumberField(TEXT("L2CacheSizeKB"));
    OutProfile.ComputeToPipelineRatio = (float)JsonObject->GetNumberField(TEXT("ComputeToPipelineRatio"));
    
    // Extract block sizes with updated API for UE5.5
    const TSharedPtr<FJsonObject>* BlockSizesObject;
    if (JsonObject->TryGetObjectField(TEXT("BlockSizes"), BlockSizesObject))
    {
        for (const auto& BlockSizePair : (*BlockSizesObject)->Values)
        {
            int32 OperationType = FCString::Atoi(*BlockSizePair.Key);
            uint32 BlockSize = (uint32)BlockSizePair.Value->AsNumber();
            OptimalBlockSizes.Add(OperationType, BlockSize);
        }
    }
    
    // Extract async compatibility with updated API for UE5.5
    const TSharedPtr<FJsonObject>* AsyncCompatObject;
    if (JsonObject->TryGetObjectField(TEXT("AsyncCompatibility"), AsyncCompatObject))
    {
        for (const auto& AsyncPair : (*AsyncCompatObject)->Values)
        {
            int32 OperationType = FCString::Atoi(*AsyncPair.Key);
            bool bCompatible = AsyncPair.Value->AsBool();
            AsyncCompatibleOperations.Add(OperationType, bCompatible);
        }
    }
    
    return true;
}