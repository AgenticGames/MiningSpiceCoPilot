// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RenderGraphResources.h"
#include "RenderGraphBuilder.h"
#include "ShaderParameterStruct.h"
#include "RHICommandList.h"
#include "ShaderCore.h"
#include "PipelineStateCache.h"
#include "ShaderParameterUtils.h"
#include "ComputeShaderUtils.h"
#include "GlobalShader.h"
#include "Interface/SDFComputeKernelInterface.h"
#include "../../1_CoreRegistry/Public/SDFTypeRegistry.h"
#include "../../1_CoreRegistry/Public/MaterialRegistry.h"
#include "Containers/StaticArray.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"
#include "Async/ParallelFor.h"
#include "Containers/Queue.h"

/**
 * Enum for SDF compute kernel types
 */
enum class ESDFComputeKernelType : uint8
{
    // Basic kernel types
    Basic = 0,
    Optimized = 1,
    NarrowBand = 2,
    WideField = 3,
    
    // Mining tool specific kernels
    DrillTool = 10,
    ExplosiveTool = 11,
    PrecisionTool = 12,
    
    // Material interaction kernels
    MaterialBlending = 20,
    MaterialTransition = 21,
    
    // Advanced processing
    AdaptiveField = 30,
    DynamicResolution = 31,
    
    // Multiple operations
    KernelFusion = 40,
    BatchProcessing = 41,
    
    // Custom
    Custom = 100
};

/**
 * Enum for SDF compute kernel features
 */
enum class ESDFComputeKernelFeatures : uint32
{
    None = 0,
    SupportsGPU = 1 << 0,
    SupportsCPUFallback = 1 << 1,
    SupportsAsyncCompute = 1 << 2,
    SupportsMultiChannelSDF = 1 << 3,
    SupportsNarrowBand = 1 << 4,
    SupportsMaterialAwareness = 1 << 5,
    SupportsAdaptiveComputation = 1 << 6,
    SupportsKernelFusion = 1 << 7,
    SupportsVariablePrecision = 1 << 8,
    SupportsWavefrontOperations = 1 << 9,
    SupportsInteroperability = 1 << 10,
    SupportsDynamicLoadBalancing = 1 << 11,
    RequiresSpecializedHardware = 1 << 12
};

/**
 * Enum for compute kernel precision mode
 */
enum class EComputeKernelPrecision : uint8
{
    HalfPrecision = 0,
    SinglePrecision = 1,
    DoublePrecision = 2,
    MixedPrecision = 3,
    AdaptivePrecision = 4
};

/**
 * Kernel permutation parameters structure
 * Defines the variants of a shader that can be generated
 */
struct MININGSPICECOPILOT_API FKernelPermutationParameters
{
    // Operation type that this kernel handles
    ESDFOperationType OperationType;
    
    // Number of material channels supported
    uint32 MaterialChannelCount;
    
    // Kernel precision mode
    EComputeKernelPrecision PrecisionMode;
    
    // Whether kernel uses narrow band optimization
    bool bUseNarrowBand;
    
    // Whether kernel supports dynamic field resolution
    bool bSupportsDynamicResolution;
    
    // Whether kernel supports material-awareness
    bool bIsMaterialAware;
    
    // Whether kernel uses optimized memory access patterns
    bool bUsesOptimizedMemoryAccess;
    
    // Whether kernel supports wave intrinsics
    bool bSupportsWaveIntrinsics;
    
    // Whether kernel supports adaptive computation
    bool bSupportsAdaptiveComputation;
    
    // Whether kernel supports multiple operations fusion
    bool bSupportsKernelFusion;
    
    // Creates a unique hash for this permutation
    uint32 GetHash() const
    {
        uint32 Hash = static_cast<uint32>(OperationType);
        Hash = HashCombine(Hash, MaterialChannelCount);
        Hash = HashCombine(Hash, static_cast<uint32>(PrecisionMode));
        Hash = HashCombine(Hash, bUseNarrowBand ? 1u : 0u);
        Hash = HashCombine(Hash, bSupportsDynamicResolution ? 1u : 0u);
        Hash = HashCombine(Hash, bIsMaterialAware ? 1u : 0u);
        Hash = HashCombine(Hash, bUsesOptimizedMemoryAccess ? 1u : 0u);
        Hash = HashCombine(Hash, bSupportsWaveIntrinsics ? 1u : 0u);
        Hash = HashCombine(Hash, bSupportsAdaptiveComputation ? 1u : 0u);
        Hash = HashCombine(Hash, bSupportsKernelFusion ? 1u : 0u);
        return Hash;
    }
    
    // Default constructor
    FKernelPermutationParameters()
        : OperationType(ESDFOperationType::Union)
        , MaterialChannelCount(1)
        , PrecisionMode(EComputeKernelPrecision::SinglePrecision)
        , bUseNarrowBand(false)
        , bSupportsDynamicResolution(false)
        , bIsMaterialAware(false)
        , bUsesOptimizedMemoryAccess(false)
        , bSupportsWaveIntrinsics(false)
        , bSupportsAdaptiveComputation(false)
        , bSupportsKernelFusion(false)
    {
    }
    
    // Compare operators for container usage
    bool operator==(const FKernelPermutationParameters& Other) const
    {
        return OperationType == Other.OperationType &&
               MaterialChannelCount == Other.MaterialChannelCount &&
               PrecisionMode == Other.PrecisionMode &&
               bUseNarrowBand == Other.bUseNarrowBand &&
               bSupportsDynamicResolution == Other.bSupportsDynamicResolution &&
               bIsMaterialAware == Other.bIsMaterialAware &&
               bUsesOptimizedMemoryAccess == Other.bUsesOptimizedMemoryAccess &&
               bSupportsWaveIntrinsics == Other.bSupportsWaveIntrinsics &&
               bSupportsAdaptiveComputation == Other.bSupportsAdaptiveComputation &&
               bSupportsKernelFusion == Other.bSupportsKernelFusion;
    }
    
    bool operator!=(const FKernelPermutationParameters& Other) const
    {
        return !(*this == Other);
    }
};

// Forward declarations for shader classes
class FSDFComputeShaderBase;

/**
 * Compute shader cache entry structure
 */
struct MININGSPICECOPILOT_API FKernelCacheEntry
{
    // Permutation parameters that generated this shader
    FKernelPermutationParameters Permutation;
    
    // Global shader type for this entry
    TShaderRef<FSDFComputeShaderBase> ShaderRef;
    
    // When this entry was last used (for LRU cache management)
    double LastUseTime;
    
    // Number of times this kernel has been used
    uint32 UseCount;
    
    // Performance score for this kernel (lower is better)
    float PerformanceScore;
    
    // Constructor
    FKernelCacheEntry()
        : LastUseTime(0.0)
        , UseCount(0)
        , PerformanceScore(FLT_MAX)
    {
    }
};

/**
 * Structure describing a compute kernel
 */
struct MININGSPICECOPILOT_API FComputeKernelDesc
{
    // Kernel name
    FName KernelName;
    
    // Shader file path
    FString ShaderFilePath;
    
    // Shader entry point function name
    FString EntryPointName;
    
    // Kernel type
    ESDFComputeKernelType KernelType;
    
    // Kernel features
    ESDFComputeKernelFeatures Features;
    
    // Default permutation parameters
    FKernelPermutationParameters DefaultPermutation;
    
    // Supported operation types
    TArray<ESDFOperationType> SupportedOperations;
    
    // Supported material type IDs (empty means all materials)
    TArray<uint32> SupportedMaterialTypeIds;
    
    // Default thread group sizes
    FIntVector DefaultThreadGroupSize;
    
    // Constructor
    FComputeKernelDesc()
        : KernelName(NAME_None)
        , ShaderFilePath("")
        , EntryPointName("Main")
        , KernelType(ESDFComputeKernelType::Basic)
        , Features(ESDFComputeKernelFeatures::None)
        , DefaultThreadGroupSize(8, 8, 8)
    {
    }
    
    // Constructor with name
    FComputeKernelDesc(FName InKernelName)
        : KernelName(InKernelName)
        , ShaderFilePath("")
        , EntryPointName("Main")
        , KernelType(ESDFComputeKernelType::Basic)
        , Features(ESDFComputeKernelFeatures::None)
        , DefaultThreadGroupSize(8, 8, 8)
    {
    }
};

/**
 * Structure containing shader compilation information
 */
struct MININGSPICECOPILOT_API FShaderCompilationInfo
{
    // Kernel descriptor
    FComputeKernelDesc KernelDesc;
    
    // Permutation parameters
    FKernelPermutationParameters Permutation;
    
    // Shader platform to compile for
    EShaderPlatform ShaderPlatform;
    
    // Additional compiler defines
    TMap<FString, FString> AdditionalDefines;
    
    // Whether compilation is pending
    bool bIsPending;
    
    // Whether compilation succeeded
    bool bSucceeded;
    
    // Constructor
    FShaderCompilationInfo()
        : ShaderPlatform(EShaderPlatform::SP_PCD3D_SM5)
        , bIsPending(false)
        , bSucceeded(false)
    {
    }
};

/**
 * Material-specific kernel information
 */
struct MININGSPICECOPILOT_API FMaterialKernelInfo
{
    // Material type IDs this kernel is optimized for
    TArray<uint32> MaterialTypeIds;
    
    // Permutation parameters specific to this material combination
    FKernelPermutationParameters MaterialPermutation;
    
    // Kernel descriptor
    FComputeKernelDesc KernelDesc;
    
    // Constructor
    FMaterialKernelInfo()
    {
    }
    
    // Constructor with material type ID
    FMaterialKernelInfo(uint32 InMaterialTypeId)
    {
        MaterialTypeIds.Add(InMaterialTypeId);
    }
};

/**
 * Kernel fusion descriptor
 */
struct MININGSPICECOPILOT_API FKernelFusionDesc
{
    // Operations to be fused
    TArray<ESDFOperationType> Operations;
    
    // Kernel descriptor for the fused kernel
    FComputeKernelDesc FusedKernelDesc;
    
    // Whether this fusion is available
    bool bIsAvailable;
    
    // Performance score relative to separate kernels (>1.0 means better than separate)
    float PerformanceMultiplier;
    
    // Constructor
    FKernelFusionDesc()
        : bIsAvailable(false)
        , PerformanceMultiplier(1.0f)
    {
    }
};

/**
 * SDF Compute Kernel Manager class
 * Manages specialized compute shaders for SDF operations
 */
class MININGSPICECOPILOT_API FSDFComputeKernelManager
{
public:
    /** Constructor */
    FSDFComputeKernelManager();
    
    /** Destructor */
    ~FSDFComputeKernelManager();
    
    /** Initialize the kernel manager */
    bool Initialize();
    
    /** Shutdown the kernel manager */
    void Shutdown();
    
    /** Check if manager is initialized */
    bool IsInitialized() const { return bIsInitialized; }
    
    /**
     * Register a new compute kernel
     * @param InKernelDesc Kernel descriptor
     * @return True if registration succeeded
     */
    bool RegisterKernel(const FComputeKernelDesc& InKernelDesc);
    
    /**
     * Register material-specific kernel
     * @param InMaterialKernelInfo Material kernel info
     * @return True if registration succeeded
     */
    bool RegisterMaterialKernel(const FMaterialKernelInfo& InMaterialKernelInfo);
    
    /**
     * Register a kernel fusion
     * @param InFusionDesc Fusion descriptor
     * @return True if registration succeeded
     */
    bool RegisterKernelFusion(const FKernelFusionDesc& InFusionDesc);
    
    /**
     * Get a kernel for a specific operation type
     * @param InOperationType Operation type
     * @param OutKernelDesc Output kernel descriptor
     * @return True if a kernel was found
     */
    bool GetKernel(ESDFOperationType InOperationType, FComputeKernelDesc& OutKernelDesc) const;
    
    /**
     * Get a kernel for a specific operation type and material
     * @param InOperationType Operation type
     * @param InMaterialTypeId Material type ID
     * @param OutKernelDesc Output kernel descriptor
     * @return True if a kernel was found
     */
    bool GetMaterialKernel(ESDFOperationType InOperationType, uint32 InMaterialTypeId, FComputeKernelDesc& OutKernelDesc) const;
    
    /**
     * Get a fused kernel for a sequence of operations
     * @param InOperations Array of operations to be fused
     * @param OutKernelDesc Output kernel descriptor
     * @return True if a fused kernel was found
     */
    bool GetFusedKernel(const TArray<ESDFOperationType>& InOperations, FComputeKernelDesc& OutKernelDesc) const;
    
    /**
     * Get or compile a shader for a specific permutation
     * @param InKernelDesc Kernel descriptor
     * @param InPermutation Permutation parameters
     * @return Shader reference
     */
    TShaderRef<FSDFComputeShaderBase> GetOrCompileShader(const FComputeKernelDesc& InKernelDesc, const FKernelPermutationParameters& InPermutation);
    
    /**
     * Compile a shader for a specific permutation
     * @param InKernelDesc Kernel descriptor
     * @param InPermutation Permutation parameters
     * @param bAsync Whether to compile asynchronously
     * @return True if compilation was started successfully
     */
    bool CompileShader(const FComputeKernelDesc& InKernelDesc, const FKernelPermutationParameters& InPermutation, bool bAsync = false);
    
    /**
     * Get a cached shader
     * @param InKernelDesc Kernel descriptor
     * @param InPermutation Permutation parameters
     * @param OutShaderRef Output shader reference
     * @return True if the shader was found in cache
     */
    bool GetCachedShader(const FComputeKernelDesc& InKernelDesc, const FKernelPermutationParameters& InPermutation, TShaderRef<FSDFComputeShaderBase>& OutShaderRef);
    
    /**
     * Cache a shader
     * @param InKernelDesc Kernel descriptor
     * @param InPermutation Permutation parameters
     * @param InShaderRef Shader reference
     * @return True if the shader was cached successfully
     */
    bool CacheShader(const FComputeKernelDesc& InKernelDesc, const FKernelPermutationParameters& InPermutation, TShaderRef<FSDFComputeShaderBase> InShaderRef);
    
    /**
     * Generate a kernel permutation based on operation and material requirements
     * @param InOperationType Operation type
     * @param InMaterialTypeIds Material type IDs
     * @param OutPermutation Output permutation parameters
     * @return True if permutation was generated successfully
     */
    bool GenerateKernelPermutation(ESDFOperationType InOperationType, const TArray<uint32>& InMaterialTypeIds, FKernelPermutationParameters& OutPermutation) const;
    
    /**
     * Generate optimal thread group size for a kernel
     * @param InKernelDesc Kernel descriptor
     * @param InVolumeSize Size of the volume to process
     * @return Optimal thread group size
     */
    FIntVector GenerateOptimalThreadGroupSize(const FComputeKernelDesc& InKernelDesc, const FIntVector& InVolumeSize) const;
    
    /**
     * Set dynamic shader parameters for a kernel
     * @param InRDGBuilder RDG builder
     * @param InShaderRef Shader reference
     * @param InKernelDesc Kernel descriptor
     * @param InPermutation Permutation parameters
     * @param InParams Shader parameters
     * @return True if parameters were set successfully
     */
    template<typename ParamType>
    bool SetDynamicShaderParameters(FRDGBuilder& InRDGBuilder, TShaderRef<FSDFComputeShaderBase> InShaderRef, const FComputeKernelDesc& InKernelDesc, const FKernelPermutationParameters& InPermutation, ParamType* InParams) const;
    
    /**
     * Dispatch a compute shader
     * @param InRDGBuilder RDG builder
     * @param InShaderRef Shader reference
     * @param InKernelDesc Kernel descriptor
     * @param InPermutation Permutation parameters
     * @param InParams Shader parameters
     * @param InThreadGroupCount Thread group count
     * @return True if dispatch was successful
     */
    template<typename ParamType>
    bool DispatchComputeShader(FRDGBuilder& InRDGBuilder, TShaderRef<FSDFComputeShaderBase> InShaderRef, const FComputeKernelDesc& InKernelDesc, const FKernelPermutationParameters& InPermutation, ParamType* InParams, const FIntVector& InThreadGroupCount) const;
    
    /**
     * Update kernel performance metrics
     * @param InKernelDesc Kernel descriptor
     * @param InPermutation Permutation parameters
     * @param InExecutionTime Execution time in milliseconds
     */
    void UpdateKernelPerformanceMetrics(const FComputeKernelDesc& InKernelDesc, const FKernelPermutationParameters& InPermutation, float InExecutionTime);
    
    /**
     * Get best performing kernel for a specific operation
     * @param InOperationType Operation type
     * @param InMaterialTypeIds Material type IDs
     * @param OutKernelDesc Output kernel descriptor
     * @param OutPermutation Output permutation parameters
     * @return True if a kernel was found
     */
    bool GetBestPerformingKernel(ESDFOperationType InOperationType, const TArray<uint32>& InMaterialTypeIds, FComputeKernelDesc& OutKernelDesc, FKernelPermutationParameters& OutPermutation) const;
    
    /**
     * Get kernel descriptors for a specific operation type
     * @param InOperationType Operation type
     * @return Array of kernel descriptors
     */
    TArray<FComputeKernelDesc> GetKernelsForOperationType(ESDFOperationType InOperationType) const;
    
    /**
     * Get all registered kernels
     * @return Array of all kernel descriptors
     */
    TArray<FComputeKernelDesc> GetAllKernels() const;
    
    /**
     * Clean up unused shader cache entries
     * @param InMaxAge Maximum age in seconds
     * @return Number of entries cleaned up
     */
    int32 CleanupShaderCache(float InMaxAge = 300.0f);
    
    /**
     * Get singleton instance
     * @return Reference to singleton instance
     */
    static FSDFComputeKernelManager& Get();
    
    /**
     * Set the SDK type registry reference
     * @param InSdfTypeRegistry Reference to SDF type registry
     */
    void SetSDFTypeRegistry(FSDFTypeRegistry* InSdfTypeRegistry) { SdfTypeRegistry = InSdfTypeRegistry; }
    
    /**
     * Set the material registry reference
     * @param InMaterialRegistry Reference to material registry
     */
    void SetMaterialRegistry(FMaterialRegistry* InMaterialRegistry) { MaterialRegistry = InMaterialRegistry; }
    
private:
    /** Initialize default kernels */
    void InitializeDefaultKernels();
    
    /** Initialize material-specific kernels */
    void InitializeMaterialKernels();
    
    /** Initialize kernel fusion combinations */
    void InitializeKernelFusions();
    
    /** Get the kernel permutation hash */
    uint32 GetKernelPermutationHash(const FComputeKernelDesc& InKernelDesc, const FKernelPermutationParameters& InPermutation) const;
    
    /** Find the best kernel for material combination */
    bool FindBestMaterialKernel(const TArray<uint32>& InMaterialTypeIds, ESDFOperationType InOperationType, FComputeKernelDesc& OutKernelDesc) const;
    
    /** Check if a kernel supports a specific operation */
    bool DoesKernelSupportOperation(const FComputeKernelDesc& InKernelDesc, ESDFOperationType InOperationType) const;
    
    /** Check if a kernel supports a specific material */
    bool DoesKernelSupportMaterial(const FComputeKernelDesc& InKernelDesc, uint32 InMaterialTypeId) const;
    
    /** Generate shader compilation environment */
    void SetupShaderCompilationEnvironment(const FComputeKernelDesc& InKernelDesc, const FKernelPermutationParameters& InPermutation, FShaderCompilerEnvironment& OutEnvironment) const;
    
    /** Process pending shader compilations */
    void ProcessPendingShaderCompilations();
    
    /** Singleton instance */
    static FSDFComputeKernelManager* Singleton;
    
    /** Whether the manager is initialized */
    bool bIsInitialized;
    
    /** Map of kernel descriptors by name */
    TMap<FName, FComputeKernelDesc> KernelMap;
    
    /** Map of material-specific kernels */
    TMap<uint32, TArray<FMaterialKernelInfo>> MaterialKernelMap;
    
    /** Map of kernel fusions */
    TMap<FString, FKernelFusionDesc> KernelFusionMap;
    
    /** Map of operation type to kernel descriptors */
    TMap<ESDFOperationType, TArray<FName>> OperationToKernelMap;
    
    /** Shader cache */
    TMap<uint32, FKernelCacheEntry> ShaderCache;
    
    /** Pending shader compilations */
    TQueue<FShaderCompilationInfo> PendingCompilations;
    
    /** Thread lock for shader cache */
    mutable FCriticalSection ShaderCacheLock;
    
    /** Thread lock for kernel maps */
    mutable FCriticalSection KernelMapLock;
    
    /** SDF type registry reference */
    FSDFTypeRegistry* SdfTypeRegistry;
    
    /** Material registry reference */
    FMaterialRegistry* MaterialRegistry;
    
    /** Default thread group sizes for different operations */
    TMap<ESDFOperationType, FIntVector> DefaultThreadGroupSizes;
    
    /** Performance metrics for kernels (PermutationHash -> ExecutionTime) */
    TMap<uint32, float> KernelPerformanceMetrics;
};

// Template implementation for SetDynamicShaderParameters
template<typename ParamType>
bool FSDFComputeKernelManager::SetDynamicShaderParameters(FRDGBuilder& InRDGBuilder, TShaderRef<FSDFComputeShaderBase> InShaderRef, const FComputeKernelDesc& InKernelDesc, const FKernelPermutationParameters& InPermutation, ParamType* InParams) const
{
    // This is just a declaration - implementation will be in the cpp file
    return false;
}

// Template implementation for DispatchComputeShader
template<typename ParamType>
bool FSDFComputeKernelManager::DispatchComputeShader(FRDGBuilder& InRDGBuilder, TShaderRef<FSDFComputeShaderBase> InShaderRef, const FComputeKernelDesc& InKernelDesc, const FKernelPermutationParameters& InPermutation, ParamType* InParams, const FIntVector& InThreadGroupCount) const
{
    // This is just a declaration - implementation will be in the cpp file
    return false;
}
