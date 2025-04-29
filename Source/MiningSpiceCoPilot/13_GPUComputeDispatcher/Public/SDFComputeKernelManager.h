#pragma once

#include "CoreMinimal.h"
#include "ShaderCore.h"
#include "RHICommandList.h"
#include "ShaderParameterStruct.h"
#include "13_GPUComputeDispatcher/Public/ComputeOperationTypes.h"

class FShaderPermutationManager;

/** SDF Compute kernel for dispatching specialized compute shaders */
struct FSDFComputeKernel
{
    FString KernelName;
    int32 OperationType = 0;
    FString PermutationName;
    uint32 OptimizationFlags = 0;
    TSharedPtr<FRHIComputeShader> CompiledShader;
    bool bIsCompiled = false;
    bool bIsCached = false;
    FString ShaderFileName;
    TArray<uint8> ParameterData;
    uint32 ThreadGroupSizeX = 8;
    uint32 ThreadGroupSizeY = 8;
    uint32 ThreadGroupSizeZ = 1;
    uint32 UsageCount = 0;
    double LastUsedTime = 0.0;
};

/** Shader compiler parameters structure */
struct FShaderCompilerParameters
{
    FString ShaderName;
    FString EntryPoint = "Main";
    FString ShaderPath;
    FString PermutationDefines;
    uint32 CompileFlags = 0;
    TArray<FString> IncludePaths;
    bool bDebugInfo = false;
};

/**
 * Specialized compute shader management for SDF operations
 * Manages kernels for different operations and materials
 * Handles shader compilation, caching, and fusion for optimal performance
 */
class MININGSPICECOPILOT_API FSDFComputeKernelManager
{
public:
    FSDFComputeKernelManager();
    ~FSDFComputeKernelManager();
    
    // Kernel registration and management
    bool RegisterKernel(int32 OpType, const FSDFComputeKernel& Kernel);
    bool RegisterKernelPermutation(int32 OpType, const FString& PermutationName, const FShaderVariant& Variant);
    FSDFComputeKernel* FindBestKernelForOperation(int32 OpType, const FComputeOperation& Operation);
    
    // Kernel fusion
    bool FuseKernels(const TArray<FComputeOperation>& OperationChain, FSDFComputeKernel& OutFusedKernel);
    
    // Shader compilation and caching
    bool PrecompileCommonKernels();
    void PurgeUnusedKernels();
    bool UpdateShaderForTypeVersion(uint32 TypeId, uint32 NewVersion);
    uint64 GetTotalShaderMemoryUsage() const;
    
    // Specialized kernels for mining operations
    FSDFComputeKernel* GetDrillOperationKernel(float Radius, bool bSmooth);
    FSDFComputeKernel* GetExplosiveOperationKernel(float Radius, float Falloff);
    FSDFComputeKernel* GetPrecisionToolKernel(const FBox& Bounds, bool bHighPrecision);
    
    // Material channel registration
    bool RegisterMaterialChannels(uint32 MaterialTypeId, int32 ChannelId, uint32 ChannelCount);
    
private:
    // Shader management
    bool CompileShader(FSDFComputeKernel& Kernel);
    bool LoadCachedShader(FSDFComputeKernel& Kernel);
    bool SaveShaderToCache(const FSDFComputeKernel& Kernel);
    FString GetShaderCachePath(const FString& ShaderName, const FString& PermutationName) const;
    bool OptimizeForSIMD(FSDFComputeKernel& Kernel);
    
    // Shader permutation system
    TSharedPtr<FShaderPermutationManager> PermutationManager;
    bool SetupShaderHotReloading();
    void OnShaderFormatChanged();
    
    // Member variables
    TMap<int32, TArray<FSDFComputeKernel>> Kernels;
    TMap<FString, TSharedPtr<FRHIComputeShader>> CompiledShaders;
    TMap<uint32, uint32> KernelUsageCounts;
    FString ShaderCachePath;
    FCriticalSection KernelLock;
    
    // Material channel mapping
    TMap<uint32, int32> MaterialChannelMap;
    TMap<uint32, uint32> MaterialChannelCounts;
};