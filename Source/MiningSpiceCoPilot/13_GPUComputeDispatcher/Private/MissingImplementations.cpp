#include "../Public/GPUDispatcher.h"
#include "../Public/GPUDispatcherLogging.h"
#include "../Public/HardwareProfileManager.h"
#include "../Public/SDFComputeKernelManager.h"
#include "../Public/ZeroCopyResourceManager.h"
#include "../Public/ComputeShaderUtils.h"
#include "../Public/SDFShaderParameters.h"
#include "GlobalShader.h"
#include "ShaderCore.h"
#include "RenderUtils.h"

// Helper function for FMiningSDFComputeUtils to add compute pass
// This implements the function declared in ComputeShaderUtils.h
void FMiningSDFComputeUtils::AddPass(
    FRDGBuilder& GraphBuilder,
    const TCHAR* PassName,
    FComputeShaderType* ComputeShader,
    FSDFOperationParameters* ShaderParams,
    const FIntVector& GroupSize)
{
    if (!ComputeShader || !ShaderParams)
    {
        UE_LOG(LogTemp, Warning, TEXT("Invalid parameters for compute pass %s"), PassName);
        return;
    }

    // Log computation for debugging
    UE_LOG(LogTemp, Verbose, TEXT("Adding compute pass: %s with thread groups [%d, %d, %d]"),
        PassName, GroupSize.X, GroupSize.Y, GroupSize.Z);
        
    // Use a simple implementation that doesn't rely on UE5's RDG system
    // This just simulates the add pass functionality without actually using RDG
    // Create a command list instance as we need to pass by reference
    FSimRHICommandList RHICmdList;
    ComputeShader->Dispatch(RHICmdList, GroupSize.X, GroupSize.Y, GroupSize.Z);
}

// Overload for compatibility with older code using TShaderMapRef
void FMiningSDFComputeUtils::AddPass(
    FRDGBuilder& GraphBuilder,
    const TCHAR* PassName,
    void* ComputeShader,
    FSDFOperationParameters* ShaderParams,
    const FIntVector& GroupSize)
{
    UE_LOG(LogTemp, Warning, TEXT("Using compatibility AddPass method for %s"), PassName);
    
    // We can't use the ComputeShader parameter directly since it's a different type
    // Instead, create a dummy shader just to log the dispatch
    FComputeShaderType DummyShader;
    
    // Call the main implementation with our dummy shader
    AddPass(GraphBuilder, PassName, &DummyShader, ShaderParams, GroupSize);
}

// These functions are already implemented in GPUDispatcher.cpp
// Removing them from this file to avoid duplicate symbol definitions

// uint32 FGPUDispatcher::CalculateOperationDataSize(const FComputeOperation& Operation) const
// {
//     // Implementation moved to GPUDispatcher.cpp
// }

// bool FGPUDispatcher::ProcessOnGPU(const FComputeOperation& Operation)
// {
//     // Implementation moved to GPUDispatcher.cpp
// }