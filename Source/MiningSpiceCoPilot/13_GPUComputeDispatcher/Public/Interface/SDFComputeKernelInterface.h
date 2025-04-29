// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RHICommandList.h"
#include "ShaderCore.h"
#include "../../1_CoreRegistry/Public/SDFTypeRegistry.h"

/**
 * Base class for all SDF compute shaders
 */
class MININGSPICECOPILOT_API FSDFComputeShaderBase : public FGlobalShader
{
    DECLARE_SHADER_TYPE(FSDFComputeShaderBase, Global);

public:
    FSDFComputeShaderBase() {}

    FSDFComputeShaderBase(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
        : FGlobalShader(Initializer)
    {
    }

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
    }

    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
    {
        FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
        OutEnvironment.SetDefine(TEXT("COMPUTE_SHADER"), 1);
    }

    virtual void SetParameters(
        FRHICommandList& RHICmdList,
        FRHIComputeShader* ComputeShader
    ) const = 0;
};

/**
 * Interface for SDF operation compute shaders
 */
class MININGSPICECOPILOT_API ISDFOperationComputeShader
{
public:
    virtual ~ISDFOperationComputeShader() {}

    /**
     * Set operation-specific parameters
     * @param OperationType Type of SDF operation
     * @param MaterialTypeIds Material type IDs involved in the operation
     * @param bNarrowBand Whether to use narrow band optimization
     */
    virtual void SetOperationParameters(
        ESDFOperationType OperationType,
        const TArray<uint32>& MaterialTypeIds,
        bool bNarrowBand
    ) = 0;

    /**
     * Validate shader compatibility with operation
     * @param OperationType Operation type to validate
     * @return True if this shader supports the operation
     */
    virtual bool SupportsOperation(ESDFOperationType OperationType) const = 0;

    /**
     * Get required thread group size for this shader
     * @param VolumeSize Size of the volume to process
     * @return Optimal thread group size
     */
    virtual FIntVector GetOptimalThreadGroupSize(const FIntVector& VolumeSize) const = 0;
};

/**
 * Interface for material-aware compute shaders
 */
class MININGSPICECOPILOT_API IMaterialAwareComputeShader
{
public:
    virtual ~IMaterialAwareComputeShader() {}

    /**
     * Set material-specific parameters
     * @param MaterialTypeIds Material type IDs involved in the operation
     * @param MaterialProperties Material properties to consider
     */
    virtual void SetMaterialParameters(
        const TArray<uint32>& MaterialTypeIds,
        const TMap<FName, FString>& MaterialProperties
    ) = 0;

    /**
     * Check if this shader supports the specified materials
     * @param MaterialTypeIds Material type IDs to check
     * @return True if this shader supports all specified materials
     */
    virtual bool SupportsMaterials(const TArray<uint32>& MaterialTypeIds) const = 0;

    /**
     * Get preferred parameters for specific materials
     * @param MaterialTypeIds Material type IDs
     * @return Map of parameter names to values
     */
    virtual TMap<FName, FString> GetPreferredParameters(const TArray<uint32>& MaterialTypeIds) const = 0;
};

/**
 * Interface for kernels that support fusion of multiple operations
 */
class MININGSPICECOPILOT_API IKernelFusionComputeShader
{
public:
    virtual ~IKernelFusionComputeShader() {}

    /**
     * Set parameters for fused operations
     * @param Operations List of operations to fuse
     * @param OperationParams Parameters for each operation
     */
    virtual void SetFusionParameters(
        const TArray<ESDFOperationType>& Operations,
        const TArray<TMap<FName, FString>>& OperationParams
    ) = 0;

    /**
     * Check if this shader supports fusion of the specified operations
     * @param Operations Operations to check
     * @return True if fusion is supported
     */
    virtual bool SupportsFusion(const TArray<ESDFOperationType>& Operations) const = 0;

    /**
     * Get performance multiplier for fusion
     * @param Operations Operations to be fused
     * @return Estimated performance multiplier (>1.0 means better than separate)
     */
    virtual float GetFusionPerformanceMultiplier(const TArray<ESDFOperationType>& Operations) const = 0;
};

/**
 * Common parameters for SDF compute shaders
 */
BEGIN_SHADER_PARAMETER_STRUCT(FSDFComputeShaderParameters, )
    // Input SDF field buffer
    SHADER_PARAMETER_SRV(StructuredBuffer<float>, InputField)
    
    // Output SDF field buffer (for write operations)
    SHADER_PARAMETER_UAV(RWStructuredBuffer<float>, OutputField)
    
    // Field dimensions
    SHADER_PARAMETER(FIntVector, FieldDimensions)
    
    // Voxel size
    SHADER_PARAMETER(float, VoxelSize)
    
    // Operation-specific parameters
    SHADER_PARAMETER(int32, OperationType)
    
    // Material-specific parameters
    SHADER_PARAMETER(int32, MaterialTypeId)
    SHADER_PARAMETER(int32, SecondaryMaterialTypeId)
    
    // Optional narrow band parameters
    SHADER_PARAMETER(float, NarrowBandThreshold)
    SHADER_PARAMETER(int32, UseNarrowBand)
    
    // Optional multi-channel parameters
    SHADER_PARAMETER(int32, ChannelCount)
    SHADER_PARAMETER(int32, PrimaryChannel)
    
    // Optional smoothing parameters
    SHADER_PARAMETER(float, SmoothingFactor)
    SHADER_PARAMETER(int32, UseSmoothing)
    
    // Optional region of interest
    SHADER_PARAMETER(FIntVector, RegionMin)
    SHADER_PARAMETER(FIntVector, RegionMax)
    
    // Optional mask buffer for conditional operations
    SHADER_PARAMETER_SRV(StructuredBuffer<int>, MaskBuffer)
    SHADER_PARAMETER(int32, UseMask)
    
    // Optional material properties buffer
    SHADER_PARAMETER_SRV(StructuredBuffer<float>, MaterialPropertiesBuffer)
    SHADER_PARAMETER(int32, UseMaterialProperties)
END_SHADER_PARAMETER_STRUCT()

/**
 * Basic SDF union operation compute shader implementation
 */
class MININGSPICECOPILOT_API FSDFUnionComputeShader : public FSDFComputeShaderBase, public ISDFOperationComputeShader
{
    DECLARE_SHADER_TYPE(FSDFUnionComputeShader, Global);
    SHADER_USE_PARAMETER_STRUCT(FSDFUnionComputeShader, FSDFComputeShaderBase);

    using FParameters = FSDFComputeShaderParameters;

public:
    FSDFUnionComputeShader() {}

    FSDFUnionComputeShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
        : FSDFComputeShaderBase(Initializer)
    {
        Parameters.Bind(Initializer.ParameterMap);
    }

    // FSDFComputeShaderBase interface
    virtual void SetParameters(
        FRHICommandList& RHICmdList,
        FRHIComputeShader* ComputeShader
    ) const override;

    // ISDFOperationComputeShader interface
    virtual void SetOperationParameters(
        ESDFOperationType OperationType,
        const TArray<uint32>& MaterialTypeIds,
        bool bNarrowBand
    ) override;

    virtual bool SupportsOperation(ESDFOperationType OperationType) const override;

    virtual FIntVector GetOptimalThreadGroupSize(const FIntVector& VolumeSize) const override;

private:
    LAYOUT_FIELD(FShaderParameter, Parameters);
};

/**
 * Factory method to create a compute shader based on operation type
 */
TShaderRef<FSDFComputeShaderBase> CreateSDFComputeShader(
    ESDFOperationType OperationType,
    const TArray<uint32>& MaterialTypeIds,
    bool bNarrowBand = false,
    bool bMaterialAware = false,
    bool bSupportFusion = false
); 