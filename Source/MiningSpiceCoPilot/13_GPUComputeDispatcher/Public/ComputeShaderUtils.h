#pragma once

#include "CoreMinimal.h"
#include "RenderGraphBuilder.h"
#include "ShaderParameterStruct.h"
#include "RHICommandList.h"
#include "ShaderCore.h"
#include "PipelineStateCache.h"
#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterUtils.h"
#include "ShaderCompilerCore.h"
#include "RenderGraphResources.h"
#include "ComputeOperationTypes.h"
#include "SDFShaderParameters.h"
#include "RHIDefinitions.h"
#include "RHIStaticStates.h"
#include "PipelineStateCache.h"
#include "RHICore.h"
#include "RenderCore.h"

/**
 * Base class for all SDF compute shaders
 * Handles shader parameter binding and resource access
 */
class MININGSPICECOPILOT_API FSDFComputeShaderBase : public FGlobalShader
{
    DECLARE_TYPE_LAYOUT(FSDFComputeShaderBase, NonVirtual);
    DECLARE_SHADER_TYPE(FSDFComputeShaderBase, Global);

public:
    FSDFComputeShaderBase() {}

    FSDFComputeShaderBase(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
        : FGlobalShader(Initializer)
    {
        // Bind shader parameters
        // Note: The parameter map is inherited from FShader via FGlobalShader
    }
    
    // Add virtual destructor since this class has virtual functions
    virtual ~FSDFComputeShaderBase() {}

    // UE5.5 compatible shader permutation compilation checking
    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
    }

    // UE5.5 compatible compilation environment modification
    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
    {
        FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
        OutEnvironment.SetDefine(TEXT("COMPUTE_SHADER"), 1);
    }

    virtual void SetParameters(FRHIComputeCommandList& RHICmdList, FRHIComputeShader* ShaderRHI, const FSDFOperationParameters& Parameters) 
    {
        // Create uniform buffer for the parameters
        FRHIUniformBuffer* ParametersUB = FSDFOperationParameters::CreateUniformBuffer(Parameters, EUniformBufferUsage::UniformBuffer_SingleDraw);
        
        // In UE5.5, we use FRHIComputeShaderParameters instead of FRHIBatchedShaderParameters
        FRHIComputeShaderParameters ShaderParams;
        
        // Set parameters using the new API
        ShaderParams.AddParameter(GetUniformBufferParameter<FSDFOperationParameters>(), ParametersUB);
        
        // Set all parameters at once
        RHICmdList.SetComputeShaderParameters(ShaderRHI, ShaderParams);
    }

    virtual void UnbindBuffers(FRHIComputeCommandList& RHICmdList, FRHIComputeShader* ShaderRHI) 
    {
        // Unbind UAVs to avoid resource conflicts
    }
};

/**
 * Specialized compute shader for SDF operations
 * Implements specific SDF operations like union, difference, etc.
 */
class MININGSPICECOPILOT_API FSDFOperationShader : public FSDFComputeShaderBase
{
    DECLARE_TYPE_LAYOUT(FSDFOperationShader, NonVirtual);
    DECLARE_SHADER_TYPE(FSDFOperationShader, Global);

public:
    FSDFOperationShader() {}

    FSDFOperationShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
        : FSDFComputeShaderBase(Initializer)
    {
        // Operation-specific initialization can go here
    }
    
    // Add virtual destructor since this class inherits from a class with virtual functions
    virtual ~FSDFOperationShader() {}

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
    }

    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
    {
        FSDFComputeShaderBase::ModifyCompilationEnvironment(Parameters, OutEnvironment);
        OutEnvironment.SetDefine(TEXT("SDF_OPERATIONS"), 1);
    }
};

/**
 * Generic compute shader type that can be used for any operation
 * Uses parameter buffer to determine what operation to execute
 */
class MININGSPICECOPILOT_API FComputeShaderType : public FGlobalShader
{
    DECLARE_TYPE_LAYOUT(FComputeShaderType, NonVirtual);
    DECLARE_SHADER_TYPE(FComputeShaderType, Global);

public:
    FComputeShaderType() {}

    FComputeShaderType(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
        : FGlobalShader(Initializer)
    {
        // Bind parameters
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

    template<typename TParameters>
    void SetParameters(FRHICommandList& RHICmdList, FRHIComputeShader* ShaderRHI, const TParameters& Parameters)
    {
        // Create uniform buffer for the parameters
        FRHIUniformBuffer* ParametersUB = TParameters::CreateUniformBuffer(Parameters, EUniformBufferUsage::UniformBuffer_SingleDraw);
        
        // In UE5.5, we use FRHIComputeShaderParameters
        FRHIComputeShaderParameters ShaderParams;
        
        // Set parameters using the new API
        ShaderParams.AddParameter(GetUniformBufferParameter<TParameters>(), ParametersUB);
        
        // Set all parameters at once
        RHICmdList.SetComputeShaderParameters(ShaderRHI, ShaderParams);
    }

    void UnbindBuffers(FRHICommandList& RHICmdList, FRHIComputeShader* ShaderRHI)
    {
        // Unbind UAVs
    }
};

/**
 * Utility class for working with compute shaders in the GPU dispatch system
 * Provides helpers for shader dispatch, parameter management, and execution
 */
// Changed class name to avoid potential conflicts with existing UE5 classes
class MININGSPICECOPILOT_API FMiningSDFComputeShaderUtils
{
public:
    /**
     * Adds a compute pass to the render graph
     * @param GraphBuilder Graph builder for creating passes
     * @param PassName Name of the pass for debugging
     * @param ComputeShader Shader to execute
     * @param ShaderParams Shader parameters to set
     * @param DispatchDim Dispatch dimensions (num groups in X, Y, Z)
     */
    template<typename TShaderClass, typename TParams>
    static void AddPass(
        FRDGBuilder& GraphBuilder,
        const TCHAR* PassName,
        TShaderMapRef<TShaderClass> ComputeShader,
        TParams* ShaderParams,
        FIntVector DispatchDim)
    {
        // Create a proper FRDGEventName for the pass
        FRDGEventName EventName(PassName);
        
        // Create the compute pass - UE5.5 compatible AddPass with explicit event name
        GraphBuilder.AddPass(
            EventName,  // Use the event name directly (no LPassName variable)
            ShaderParams,
            ERDGPassFlags::Compute,
            [ComputeShader, ShaderParams, DispatchDim](FRHIComputeCommandList& RHICmdList)
            {
                FMiningSDFComputeShaderUtils::DispatchComputeShader(RHICmdList, ComputeShader, *ShaderParams, DispatchDim);
            });
    }

    /**
     * Dispatches a compute shader directly on a command list
     * @param RHICmdList Command list to use for dispatch
     * @param ComputeShader Shader to execute
     * @param ShaderParams Shader parameters to set
     * @param DispatchDim Dispatch dimensions (num groups in X, Y, Z)
     */
    template<typename TShaderClass, typename TParams>
    static void DispatchComputeShader(
        FRHIComputeCommandList& RHICmdList,
        TShaderMapRef<TShaderClass> ComputeShader,
        const TParams& ShaderParams,
        FIntVector DispatchDim)
    {
        // Get compute shader RHI
        FRHIComputeShader* ShaderRHI = ComputeShader.GetComputeShader();
        
        // Create pipeline state
        FComputePipelineState* PipelineState = GetOrCreateComputePipelineState(RHICmdList, ShaderRHI);
        
        // Set the pipeline state
        RHICmdList.SetComputePipelineState(PipelineState);

        // Set shader parameters
        ComputeShader->SetParameters(RHICmdList, ShaderRHI, ShaderParams);

        // Dispatch the compute shader
        RHICmdList.DispatchComputeShader(DispatchDim.X, DispatchDim.Y, DispatchDim.Z);

        // Unbind resources
        ComputeShader->UnbindBuffers(RHICmdList, ShaderRHI);
    }

    /**
     * Gets or creates a compute pipeline state for a shader
     * @param RHICmdList Command list
     * @param ShaderRHI Compute shader RHI
     * @return Compute pipeline state
     */
    static FComputePipelineState* GetOrCreateComputePipelineState(
        FRHIComputeCommandList& RHICmdList, 
        FRHIComputeShader* ShaderRHI)
    {
        // Create the pipeline initializer with UE5.5 compatible API
        FComputePipelineStateInitializer PipelineInitializer;
        PipelineInitializer.ComputeShader = ShaderRHI;
        
        // Get or create the pipeline state using UE5.5 compatible API
        return PipelineStateCache::GetAndOrCreateComputePipelineState(RHICmdList, PipelineInitializer);
    }

    /**
     * Calculates optimal thread group count based on dimensions
     * @param DimensionX X dimension of the data to process
     * @param DimensionY Y dimension of the data to process
     * @param DimensionZ Z dimension of the data to process
     * @param ThreadGroupSizeX X dimension of the thread group size
     * @param ThreadGroupSizeY Y dimension of the thread group size
     * @param ThreadGroupSizeZ Z dimension of the thread group size
     * @return FIntVector containing the number of thread groups in each dimension
     */
    static FIntVector CalculateGroupCount(
        int32 DimensionX, int32 DimensionY, int32 DimensionZ,
        int32 ThreadGroupSizeX, int32 ThreadGroupSizeY, int32 ThreadGroupSizeZ)
    {
        FIntVector GroupCount;
        GroupCount.X = FMath::DivideAndRoundUp(DimensionX, ThreadGroupSizeX);
        GroupCount.Y = FMath::DivideAndRoundUp(DimensionY, ThreadGroupSizeY);
        GroupCount.Z = FMath::DivideAndRoundUp(DimensionZ, ThreadGroupSizeZ);
        return GroupCount;
    }

    /**
     * Calculates optimal dispatch size based on operation bounds
     * @param Bounds Bounds of the operation in world space
     * @param CellSize Size of each voxel cell
     * @param ThreadGroupSize Size of thread groups to use
     * @return FDispatchParameters containing dispatch information
     */
    static FDispatchParameters CalculateDispatchFromBounds(
        const FBox& Bounds, 
        const FVector& CellSize,
        const FIntVector& ThreadGroupSize)
    {
        // Calculate dimensions in voxels
        FVector Dimensions = Bounds.GetSize() / CellSize;
        int32 SizeX = FMath::Max<int32>(1, FMath::CeilToInt(Dimensions.X));
        int32 SizeY = FMath::Max<int32>(1, FMath::CeilToInt(Dimensions.Y));
        int32 SizeZ = FMath::Max<int32>(1, FMath::CeilToInt(Dimensions.Z));
        
        // Create dispatch parameters
        FDispatchParameters Params;
        Params.ThreadGroupSizeX = ThreadGroupSize.X;
        Params.ThreadGroupSizeY = ThreadGroupSize.Y;
        Params.ThreadGroupSizeZ = ThreadGroupSize.Z;
        Params.SizeX = SizeX;
        Params.SizeY = SizeY;
        Params.SizeZ = SizeZ;
        
        return Params;
    }
};