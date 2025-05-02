#pragma once

#include "CoreMinimal.h"
#include "../Public/GPUDispatcherLogging.h"

/**
 * Simplified shader base classes to avoid RHI dependencies
 * These are designed to allow for compilation without requiring
 * the full Unreal Engine RHI system.
 * 
 * IMPORTANT: Uses 'Sim' prefix to avoid conflicts with actual UE5 shader classes
 */

/**
 * Forward declarations for simplified RHI types
 */
class FSimRHICommandList;
class FSimRHIComputeShader;

/**
 * Simplified version of shader base class
 */
class FSimShader
{
public:
    FSimShader() {}
    virtual ~FSimShader() {}

    virtual FString GetShaderName() const { return TEXT("BaseShader"); }
};

/**
 * Simplified version of global shader class
 * This provides the minimal interface needed for the GPU dispatcher system
 * without requiring full RHI dependencies.
 */
class FSimGlobalShader : public FSimShader
{
public:
    FSimGlobalShader() {}
    virtual ~FSimGlobalShader() {}

    // Minimal interface for shader binding
    virtual void SetParameters(
        FSimRHICommandList& RHICmdList, 
        FSimRHIComputeShader* ShaderRHI, 
        const void* UniformBufferData) 
    {
        UE_LOG(LogTemp, Verbose, TEXT("Setting parameters for shader: %s (Simplified implementation)"), 
            *GetShaderName());
    }

    // Unbind resources after use
    virtual void UnbindResources(FSimRHICommandList& RHICmdList, FSimRHIComputeShader* ShaderRHI)
    {
        UE_LOG(LogTemp, Verbose, TEXT("Unbinding resources for shader: %s (Simplified implementation)"), 
            *GetShaderName());
    }

    virtual bool IsSupported(const FString& ShaderPlatform)
    {
        return true;
    }
};

/**
 * Dummy implementation of RHI compute shader
 */
class FSimRHIComputeShader
{
public:
    virtual ~FSimRHIComputeShader() {}
};

/**
 * Dummy implementation of RHI command list
 */
class FSimRHICommandList
{
public:
    virtual ~FSimRHICommandList() {}
};

/**
 * Simplified shader compiler environment
 * Normally used to set shader compilation flags and defines
 */
class FSimShaderCompilerEnvironment
{
public:
    FSimShaderCompilerEnvironment() {}
    virtual ~FSimShaderCompilerEnvironment() {}
    
    // Set a preprocessor define for shader compilation
    void SetDefine(const TCHAR* Name, int32 Value)
    {
        UE_LOG(LogTemp, Verbose, TEXT("Setting shader define: %s = %d (Simplified)"), 
            Name, Value);
    }
    
    // Set a preprocessor define for shader compilation
    void SetDefine(const TCHAR* Name, float Value)
    {
        UE_LOG(LogTemp, Verbose, TEXT("Setting shader define: %s = %f (Simplified)"), 
            Name, Value);
    }
    
    // Set a preprocessor define for shader compilation
    void SetDefine(const TCHAR* Name, const TCHAR* Value)
    {
        UE_LOG(LogTemp, Verbose, TEXT("Setting shader define: %s = %s (Simplified)"), 
            Name, Value);
    }
};