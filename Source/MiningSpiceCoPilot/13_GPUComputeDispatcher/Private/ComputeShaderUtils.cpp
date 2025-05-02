#include "../Public/ComputeShaderUtils.h"
#include "../Public/GPUDispatcherLogging.h"
#include "GlobalShader.h"
#include "ShaderCore.h"

// Implementation of base shader type - first parameter empty since this is not a class template
IMPLEMENT_SHADER_TYPE(, FSDFComputeShaderBase, TEXT("/MiningSpiceCoPilot/Shaders/SDFOperations.usf"), TEXT("BaseOperation"), SF_Compute);

// Implementation of FComputeShaderType - first parameter empty since this is not a class template
IMPLEMENT_SHADER_TYPE(, FComputeShaderType, TEXT("/MiningSpiceCoPilot/Shaders/SDFOperations.usf"), TEXT("ComputeShaderMain"), SF_Compute);

// Define specialized shader classes to prevent redefinition errors
// We create distinct types by using typedefs so each shader gets a unique implementation
// Each shader gets a unique type and entry point

// Operation-specific shader implementations
// This prevents the "multiple function/variable redefinition" errors

// Generic SDF operation shader
class FGenericSDFOperation : public FSDFOperationShader 
{
public:
    DECLARE_SHADER_TYPE(FGenericSDFOperation, Global);
    FGenericSDFOperation() {}
    FGenericSDFOperation(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FSDFOperationShader(Initializer) {}
};
IMPLEMENT_SHADER_TYPE(, FGenericSDFOperation, TEXT("/MiningSpiceCoPilot/Shaders/SDFOperations.usf"), TEXT("GenericSDFOperation"), SF_Compute);

// Difference operation shader
class FDifferenceSDFOperation : public FSDFOperationShader 
{
public:
    DECLARE_SHADER_TYPE(FDifferenceSDFOperation, Global);
    FDifferenceSDFOperation() {}
    FDifferenceSDFOperation(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FSDFOperationShader(Initializer) {}
};
IMPLEMENT_SHADER_TYPE(, FDifferenceSDFOperation, TEXT("/MiningSpiceCoPilot/Shaders/SDFOperations.usf"), TEXT("DifferenceOperation"), SF_Compute);

// Intersection operation shader
class FIntersectionSDFOperation : public FSDFOperationShader 
{
public:
    DECLARE_SHADER_TYPE(FIntersectionSDFOperation, Global);
    FIntersectionSDFOperation() {}
    FIntersectionSDFOperation(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FSDFOperationShader(Initializer) {}
};
IMPLEMENT_SHADER_TYPE(, FIntersectionSDFOperation, TEXT("/MiningSpiceCoPilot/Shaders/SDFOperations.usf"), TEXT("IntersectionOperation"), SF_Compute);

// Smoothing operation shader
class FSmoothingSDFOperation : public FSDFOperationShader 
{
public:
    DECLARE_SHADER_TYPE(FSmoothingSDFOperation, Global);
    FSmoothingSDFOperation() {}
    FSmoothingSDFOperation(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FSDFOperationShader(Initializer) {}
};
IMPLEMENT_SHADER_TYPE(, FSmoothingSDFOperation, TEXT("/MiningSpiceCoPilot/Shaders/SDFOperations.usf"), TEXT("SmoothingOperation"), SF_Compute);

// Material blend operation shader
class FMaterialBlendSDFOperation : public FSDFOperationShader 
{
public:
    DECLARE_SHADER_TYPE(FMaterialBlendSDFOperation, Global);
    FMaterialBlendSDFOperation() {}
    FMaterialBlendSDFOperation(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FSDFOperationShader(Initializer) {}
};
IMPLEMENT_SHADER_TYPE(, FMaterialBlendSDFOperation, TEXT("/MiningSpiceCoPilot/Shaders/SDFOperations.usf"), TEXT("MaterialBlendOperation"), SF_Compute);