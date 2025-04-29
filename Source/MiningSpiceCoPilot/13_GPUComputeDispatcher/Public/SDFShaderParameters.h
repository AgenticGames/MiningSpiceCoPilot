#pragma once

#include "CoreMinimal.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphResources.h"

// Define SDF operation types to match the enums in the SDF system
enum class ESDFShaderOperation : uint32
{
    Union = 0,
    Difference = 1,
    Intersection = 2,
    Smoothing = 3,
    Gradient = 4,
    Evaluation = 5,
    MaterialBlend = 6,
    Erosion = 7,
    Dilation = 8,
    ChannelTransfer = 9,
    FieldOperation = 10
};

/**
 * Compute shader parameter struct for SDF operations
 * Compatible with Unreal's RDG system
 */
BEGIN_SHADER_PARAMETER_STRUCT(FSDFOperationParameters, )
    // Input/Output buffers
    SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, OutputField)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, InputField1)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, InputField2)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, MaterialField)
    
    // Operation parameters
    SHADER_PARAMETER(uint32, OperationType)
    SHADER_PARAMETER(FVector3f, VolumeSize)
    SHADER_PARAMETER(FVector3f, VolumeOrigin)
    SHADER_PARAMETER(FVector3f, CellSize)
    SHADER_PARAMETER(FVector3f, BoundsMin)
    SHADER_PARAMETER(FVector3f, BoundsMax)
    SHADER_PARAMETER(uint32, VolumeWidth)
    SHADER_PARAMETER(uint32, VolumeHeight)
    SHADER_PARAMETER(uint32, VolumeDepth)
    
    // Operation-specific parameters
    SHADER_PARAMETER(float, Strength)
    SHADER_PARAMETER(float, BlendWeight)
    SHADER_PARAMETER(float, SmoothingRadius)
    SHADER_PARAMETER(uint32, MaterialChannelId)
    SHADER_PARAMETER(uint32, ChannelCount)
    SHADER_PARAMETER(uint32, Flags)
    SHADER_PARAMETER(FMatrix44f, Transform)
    
    // Narrow-band parameters
    SHADER_PARAMETER(float, NarrowBandThreshold)
    SHADER_PARAMETER(uint32, UseNarrowBand)
    SHADER_PARAMETER(uint32, UseHighPrecision)
END_SHADER_PARAMETER_STRUCT()

/**
 * Specialized compute shader parameter struct for drill operations
 */
BEGIN_SHADER_PARAMETER_STRUCT(FDrillOperationParameters, )
    // Input/Output buffers
    SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, OutputField)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, InputField)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, MaterialField)
    
    // Drill parameters
    SHADER_PARAMETER(FVector3f, DrillOrigin)
    SHADER_PARAMETER(FVector3f, DrillDirection)
    SHADER_PARAMETER(float, DrillRadius)
    SHADER_PARAMETER(float, DrillLength)
    SHADER_PARAMETER(float, Smoothing)
    SHADER_PARAMETER(float, Strength)
    
    // Volume parameters
    SHADER_PARAMETER(FVector3f, VolumeSize)
    SHADER_PARAMETER(FVector3f, VolumeOrigin)
    SHADER_PARAMETER(FVector3f, CellSize)
    SHADER_PARAMETER(FVector3f, BoundsMin)
    SHADER_PARAMETER(FVector3f, BoundsMax)
    SHADER_PARAMETER(uint32, VolumeWidth)
    SHADER_PARAMETER(uint32, VolumeHeight)
    SHADER_PARAMETER(uint32, VolumeDepth)
    
    // Material parameters
    SHADER_PARAMETER(uint32, MaterialChannelId)
    SHADER_PARAMETER(uint32, ChannelCount)
    SHADER_PARAMETER(uint32, Flags)
END_SHADER_PARAMETER_STRUCT()

/**
 * Specialized compute shader parameter struct for explosive operations
 */
BEGIN_SHADER_PARAMETER_STRUCT(FExplosiveOperationParameters, )
    // Input/Output buffers
    SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, OutputField)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, InputField)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, MaterialField)
    
    // Explosive parameters
    SHADER_PARAMETER(FVector3f, ExplosionCenter)
    SHADER_PARAMETER(float, ExplosionRadius)
    SHADER_PARAMETER(float, FalloffRadius)
    SHADER_PARAMETER(float, Strength)
    SHADER_PARAMETER(uint32, NoiseEnabled)
    SHADER_PARAMETER(uint32, NoiseOctaves)
    SHADER_PARAMETER(float, NoiseFrequency)
    SHADER_PARAMETER(float, NoiseAmplitude)
    
    // Volume parameters
    SHADER_PARAMETER(FVector3f, VolumeSize)
    SHADER_PARAMETER(FVector3f, VolumeOrigin)
    SHADER_PARAMETER(FVector3f, CellSize)
    SHADER_PARAMETER(FVector3f, BoundsMin)
    SHADER_PARAMETER(FVector3f, BoundsMax)
    SHADER_PARAMETER(uint32, VolumeWidth)
    SHADER_PARAMETER(uint32, VolumeHeight)
    SHADER_PARAMETER(uint32, VolumeDepth)
    
    // Material parameters
    SHADER_PARAMETER(uint32, MaterialChannelId)
    SHADER_PARAMETER(uint32, ChannelCount)
    SHADER_PARAMETER(uint32, Flags)
END_SHADER_PARAMETER_STRUCT()

/**
 * Specialized compute shader parameter struct for precision tool operations
 */
BEGIN_SHADER_PARAMETER_STRUCT(FPrecisionToolParameters, )
    // Input/Output buffers
    SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, OutputField)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, InputField)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, MaterialField)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, ToolShapeField)
    
    // Tool parameters
    SHADER_PARAMETER(FMatrix44f, ToolTransform)
    SHADER_PARAMETER(float, Strength)
    SHADER_PARAMETER(float, BlendWeight)
    SHADER_PARAMETER(uint32, ToolTypeId)
    
    // Volume parameters
    SHADER_PARAMETER(FVector3f, VolumeSize)
    SHADER_PARAMETER(FVector3f, VolumeOrigin)
    SHADER_PARAMETER(FVector3f, CellSize)
    SHADER_PARAMETER(FVector3f, BoundsMin)
    SHADER_PARAMETER(FVector3f, BoundsMax)
    SHADER_PARAMETER(uint32, VolumeWidth)
    SHADER_PARAMETER(uint32, VolumeHeight)
    SHADER_PARAMETER(uint32, VolumeDepth)
    
    // Material parameters
    SHADER_PARAMETER(uint32, MaterialChannelId)
    SHADER_PARAMETER(uint32, ChannelCount)
    SHADER_PARAMETER(uint32, Flags)
    SHADER_PARAMETER(uint32, UseHighPrecision)
END_SHADER_PARAMETER_STRUCT()