#pragma once

#include "CoreMinimal.h"
#include "ComputeOperationTypes.h"
#include "SDFShaderParameters.h"
#include "../Private/SimplifiedShaderClasses.h"

// Forward declaration of UE5 shader classes to avoid including them directly
class FRDGBuilder;

// Forward include the SDFTypeRegistry.h to get the official enum definition
#include "../../1_CoreRegistry/Public/SDFTypeRegistry.h"

// Define our own FComputeShaderType class using SimShader classes
class MININGSPICECOPILOT_API FComputeShaderType : public FSimGlobalShader
{
public:
    // Default constructor and destructor
    FComputeShaderType() {}
    virtual ~FComputeShaderType() {}
    
    // Get the compute shader RHI
    virtual FSimRHIComputeShader* GetComputeShader() const { return nullptr; }
    
    // Dispatch the compute shader with the given thread groups
    virtual void Dispatch(FSimRHICommandList& RHICmdList, uint32 ThreadGroupCountX, uint32 ThreadGroupCountY, uint32 ThreadGroupCountZ)
    {
        UE_LOG(LogTemp, Verbose, TEXT("Dispatching compute shader: %s with thread groups [%d, %d, %d] (Simplified)"),
            *GetShaderName(), ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
    }
    
    // Get the thread group size for this shader
    virtual FIntVector GetThreadGroupSize() const { return FIntVector(8, 8, 1); }
    
    // Override shader name
    virtual FString GetShaderName() const override { return TEXT("ComputeShader"); }
    
    // Required shader permutation functions for UE shader system
    
    // Define permutation parameter structure to avoid conflicts
    struct FPermutationParameters 
    {
        bool bEnableHighPrecision;
        bool bEnableVectorization;
    };
    
    // Determines whether a specific permutation should be compiled
    static bool ShouldCompilePermutation(const FPermutationParameters& Parameters)
    {
        UE_LOG(LogTemp, Verbose, TEXT("ShouldCompilePermutation check (Simplified)"));
        return true; // Compile all permutations in simplified implementation
    }
    
    // Allows modification of the compilation environment for the shader
    static void ModifyCompilationEnvironment(const FPermutationParameters& Parameters, FSimShaderCompilerEnvironment& OutEnvironment)
    {
        UE_LOG(LogTemp, Verbose, TEXT("ModifyCompilationEnvironment (Simplified)"));
        
        // In a real shader, these would be used to define preprocessor macros
        if (Parameters.bEnableHighPrecision)
        {
            OutEnvironment.SetDefine(TEXT("HIGH_PRECISION"), 1);
        }
        
        if (Parameters.bEnableVectorization)
        {
            OutEnvironment.SetDefine(TEXT("ENABLE_VECTORIZATION"), 1);
        }
    }
    
    // Static initialization function called by the shader system
    static void InternalInitializeBases() 
    {
        UE_LOG(LogTemp, Verbose, TEXT("InternalInitializeBases (Simplified)"));
        // This would normally register the shader type with the shader system
    }
    
    // Static cleanup function called by the shader system
    static void InternalDestroy()
    {
        UE_LOG(LogTemp, Verbose, TEXT("InternalDestroy (Simplified)"));
        // This would normally unregister the shader type from the shader system
    }
};

// Helper functions to convert between different operation type enums
namespace SDFOperationTypeHelpers
{
    // Convert from shader operation to registry operation type
    inline ESDFOperationType ConvertToRegistryType(ESDFShaderOperation ShaderOp)
    {
        switch (ShaderOp)
        {
            case ESDFShaderOperation::Union:
                return ESDFOperationType::Union;
            case ESDFShaderOperation::Difference:
                return ESDFOperationType::Subtraction;
            case ESDFShaderOperation::Intersection:
                return ESDFOperationType::Intersection;
            case ESDFShaderOperation::Smoothing:
                return ESDFOperationType::SmoothUnion;
            case ESDFShaderOperation::MaterialBlend:
                return ESDFOperationType::Custom;
            default:
                return ESDFOperationType::Custom;
        }
    }

    // Convert from registry operation type to shader operation
    inline ESDFShaderOperation ConvertToShaderType(ESDFOperationType RegistryOp)
    {
        switch (RegistryOp)
        {
            case ESDFOperationType::Union:
                return ESDFShaderOperation::Union;
            case ESDFOperationType::Subtraction:
                return ESDFShaderOperation::Difference;
            case ESDFOperationType::Intersection:
                return ESDFShaderOperation::Intersection;
            case ESDFOperationType::SmoothUnion:
                return ESDFShaderOperation::Smoothing;
            case ESDFOperationType::SmoothSubtraction:
                return ESDFShaderOperation::Smoothing;
            case ESDFOperationType::SmoothIntersection:
                return ESDFShaderOperation::Smoothing;
            case ESDFOperationType::Custom:
                return ESDFShaderOperation::FieldOperation;
            default:
                return ESDFShaderOperation::Union;
        }
    }
}

/**
 * Simplified base class for SDF compute operations
 * Removed RHI dependencies for easier compilation
 */
class MININGSPICECOPILOT_API FSDFComputeShaderBase : public FComputeShaderType
{
public:
    FSDFComputeShaderBase() {}
    virtual ~FSDFComputeShaderBase() {}

    // Base operation properties
    virtual FString GetOperationName() const { return TEXT("Base"); }
    virtual bool IsHighPrecision() const { return false; }
    virtual bool SupportsVectorization() const { return false; }
    
    // Simplified operation execution without RHI
    virtual bool Execute(const FComputeOperation& Operation) 
    { 
        // Default implementation logs execution
        UE_LOG(LogTemp, Display, TEXT("Executing %s operation (simplified implementation)"), 
            *GetOperationName());
        return true;
    }
    
    // Operation cost estimation for scheduling
    virtual float EstimateOperationCost(const FComputeOperation& Operation) const
    {
        return 1.0f; // Base cost
    }
    
    // Override GetShaderName from FComputeShaderType
    virtual FString GetShaderName() const override { return GetOperationName(); }
    
    // Specialized permutation handling for SDF operations
    static bool ShouldCompilePermutation(const FPermutationParameters& Parameters)
    {
        UE_LOG(LogTemp, Verbose, TEXT("SDF ShouldCompilePermutation check (Simplified)"));
        return true; // Always compile base SDF shader permutation in simplified implementation
    }
    
    // Specialized compilation environment modification for SDF operations
    static void ModifyCompilationEnvironment(const FPermutationParameters& Parameters, FSimShaderCompilerEnvironment& OutEnvironment)
    {
        // Call parent implementation first
        FComputeShaderType::ModifyCompilationEnvironment(Parameters, OutEnvironment);
        
        // Add SDF-specific defines
        UE_LOG(LogTemp, Verbose, TEXT("SDF ModifyCompilationEnvironment (Simplified)"));
        
        OutEnvironment.SetDefine(TEXT("SDF_OPERATION"), 1);
    }
};

/**
 * Specialized operation for SDF unions
 */
class MININGSPICECOPILOT_API FUnionSDFOperation : public FSDFComputeShaderBase
{
public:
    FUnionSDFOperation() : FSDFComputeShaderBase() {}
    virtual ~FUnionSDFOperation() {}
    
    virtual FString GetOperationName() const override { return TEXT("Union"); }
    virtual bool SupportsVectorization() const override { return true; }
    
    virtual bool Execute(const FComputeOperation& Operation) override
    {
        // Simplified union operation
        UE_LOG(LogTemp, Display, TEXT("Executing Union SDF operation (simplified implementation)"));
        return true;
    }
};

/**
 * Specialized operation for SDF differences
 */
class MININGSPICECOPILOT_API FDifferenceSDFOperation : public FSDFComputeShaderBase
{
public:
    FDifferenceSDFOperation() : FSDFComputeShaderBase() {}
    virtual ~FDifferenceSDFOperation() {}
    
    virtual FString GetOperationName() const override { return TEXT("Difference"); }
    
    virtual bool Execute(const FComputeOperation& Operation) override
    {
        // Simplified difference operation
        UE_LOG(LogTemp, Display, TEXT("Executing Difference SDF operation (simplified implementation)"));
        return true;
    }
    
    virtual float EstimateOperationCost(const FComputeOperation& Operation) const override
    {
        // Difference operations are typically more expensive
        return 1.5f;
    }
};

/**
 * Specialized operation for SDF intersections
 */
class MININGSPICECOPILOT_API FIntersectionSDFOperation : public FSDFComputeShaderBase
{
public:
    FIntersectionSDFOperation() : FSDFComputeShaderBase() {}
    virtual ~FIntersectionSDFOperation() {}
    
    virtual FString GetOperationName() const override { return TEXT("Intersection"); }
    
    virtual bool Execute(const FComputeOperation& Operation) override
    {
        // Simplified intersection operation
        UE_LOG(LogTemp, Display, TEXT("Executing Intersection SDF operation (simplified implementation)"));
        return true;
    }
};

/**
 * Specialized operation for SDF smoothing
 */
class MININGSPICECOPILOT_API FSmoothingSDFOperation : public FSDFComputeShaderBase
{
public:
    FSmoothingSDFOperation() : FSDFComputeShaderBase() {}
    virtual ~FSmoothingSDFOperation() {}
    
    virtual FString GetOperationName() const override { return TEXT("Smoothing"); }
    virtual bool IsHighPrecision() const override { return true; }
    
    virtual bool Execute(const FComputeOperation& Operation) override
    {
        // Simplified smoothing operation
        UE_LOG(LogTemp, Display, TEXT("Executing Smoothing SDF operation (simplified implementation)"));
        return true;
    }
    
    virtual float EstimateOperationCost(const FComputeOperation& Operation) const override
    {
        // Smoothing is more complex than basic CSG
        return 2.0f;
    }
};

/**
 * Specialized operation for material blending
 */
class MININGSPICECOPILOT_API FMaterialBlendSDFOperation : public FSDFComputeShaderBase
{
public:
    FMaterialBlendSDFOperation() : FSDFComputeShaderBase() {}
    virtual ~FMaterialBlendSDFOperation() {}
    
    virtual FString GetOperationName() const override { return TEXT("MaterialBlend"); }
    virtual bool IsHighPrecision() const override { return true; }
    
    virtual bool Execute(const FComputeOperation& Operation) override
    {
        // Simplified material blend operation
        UE_LOG(LogTemp, Display, TEXT("Executing Material Blend SDF operation (simplified implementation)"));
        return true;
    }
    
    virtual float EstimateOperationCost(const FComputeOperation& Operation) const override
    {
        // Material blend operations can be expensive
        return 2.5f;
    }
};

/**
 * Factory class for creating SDF operations
 * Provides centralized creation and management of operation implementations
 */
class MININGSPICECOPILOT_API FSDFOperationFactory
{
public:
    static TSharedPtr<FSDFComputeShaderBase> CreateOperation(ESDFOperationType OperationType)
    {
        // Convert registry operation type to shader operation type
        ESDFShaderOperation ShaderOp = SDFOperationTypeHelpers::ConvertToShaderType(OperationType);
        
        // Create appropriate shader operation based on converted type
        switch (ShaderOp)
        {
            case ESDFShaderOperation::Union:
                return MakeShared<FUnionSDFOperation>();
            case ESDFShaderOperation::Difference:
                return MakeShared<FDifferenceSDFOperation>();
            case ESDFShaderOperation::Intersection:
                return MakeShared<FIntersectionSDFOperation>();
            case ESDFShaderOperation::Smoothing:
                return MakeShared<FSmoothingSDFOperation>();
            case ESDFShaderOperation::MaterialBlend:
                return MakeShared<FMaterialBlendSDFOperation>();
            default:
                // Default to union for unknown operations
                return MakeShared<FUnionSDFOperation>();
        }
    }
    
    // Overload for direct use with shader operation types
    static TSharedPtr<FSDFComputeShaderBase> CreateOperation(ESDFShaderOperation OperationType)
    {
        switch (OperationType)
        {
            case ESDFShaderOperation::Union:
                return MakeShared<FUnionSDFOperation>();
            case ESDFShaderOperation::Difference:
                return MakeShared<FDifferenceSDFOperation>();
            case ESDFShaderOperation::Intersection:
                return MakeShared<FIntersectionSDFOperation>();
            case ESDFShaderOperation::Smoothing:
                return MakeShared<FSmoothingSDFOperation>();
            case ESDFShaderOperation::MaterialBlend:
                return MakeShared<FMaterialBlendSDFOperation>();
            default:
                // Default to union for unknown operations
                return MakeShared<FUnionSDFOperation>();
        }
    }
};

/**
 * Utility class for SDF computation
 * Provides helper methods for operation execution and management
 */
class MININGSPICECOPILOT_API FMiningSDFComputeUtils
{
public:
    /**
     * Dispatches an SDF operation
     * @param Operation Operation to execute
     * @return True if operation was executed successfully
     */
    static bool DispatchOperation(const FComputeOperation& Operation)
    {
        // Create the appropriate operation handler 
        // The operation type in FComputeOperation is an int32, so we need to convert it properly
        TSharedPtr<FSDFComputeShaderBase> OperationHandler;
        
        // First try to interpret as ESDFOperationType (registry type)
        if (Operation.OperationType >= 0 && Operation.OperationType <= static_cast<int32>(ESDFOperationType::Custom))
        {
            // Create from registry type
            OperationHandler = FSDFOperationFactory::CreateOperation(
                static_cast<ESDFOperationType>(Operation.OperationType));
        }
        else
        {
            // Try as shader operation type
            OperationHandler = FSDFOperationFactory::CreateOperation(
                static_cast<ESDFShaderOperation>(Operation.OperationType));
        }
        
        if (!OperationHandler)
        {
            UE_LOG(LogTemp, Warning, TEXT("Unknown operation type: %d"), Operation.OperationType);
            return false;
        }
        
        // Execute the operation
        return OperationHandler->Execute(Operation);
    }
    
    /**
     * Calculates optimal thread group count based on dimensions
     * @param Dimensions Dimensions of the data to process
     * @param ThreadGroupSize Size of the thread group
     * @return Optimal thread group count
     */
    static FIntVector CalculateGroupCount(
        const FIntVector& Dimensions,
        const FIntVector& ThreadGroupSize)
    {
        FIntVector GroupCount;
        GroupCount.X = FMath::DivideAndRoundUp(Dimensions.X, ThreadGroupSize.X);
        GroupCount.Y = FMath::DivideAndRoundUp(Dimensions.Y, ThreadGroupSize.Y);
        GroupCount.Z = FMath::DivideAndRoundUp(Dimensions.Z, ThreadGroupSize.Z);
        return GroupCount;
    }
    
    /**
     * Calculates optimal dispatch size based on operation bounds
     * @param Bounds Bounds of the operation in world space
     * @param CellSize Size of each voxel cell
     * @param ThreadGroupSize Size of thread groups to use
     * @return Dispatch dimensions
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
    
    /**
     * Estimates the cost of an operation for scheduling
     * @param Operation Operation to estimate
     * @return Estimated cost factor
     */
    static float EstimateOperationCost(const FComputeOperation& Operation)
    {
        // Create the appropriate operation handler
        TSharedPtr<FSDFComputeShaderBase> OperationHandler = 
            FSDFOperationFactory::CreateOperation(static_cast<ESDFOperationType>(Operation.OperationType));
        
        if (!OperationHandler)
        {
            return 1.0f; // Default cost
        }
        
        // Get the operation's cost estimate
        return OperationHandler->EstimateOperationCost(Operation);
    }
    
    /**
     * Adds a compute pass to the render graph
     * @param GraphBuilder The render graph builder (Using UE5's FRDGBuilder type)
     * @param PassName The name of the pass
     * @param ComputeShader The compute shader to use
     * @param ShaderParams The shader parameters
     * @param GroupSize The dispatch group size
     */
    static void AddPass(
        FRDGBuilder& GraphBuilder,
        const TCHAR* PassName,
        FComputeShaderType* ComputeShader,
        FSDFOperationParameters* ShaderParams,
        const FIntVector& GroupSize);
        
    // For compatibility with older code using TShaderMapRef
    static void AddPass(
        FRDGBuilder& GraphBuilder,
        const TCHAR* PassName,
        void* ComputeShader,
        FSDFOperationParameters* ShaderParams,
        const FIntVector& GroupSize);
};

// Note: FComputeShaderType already defined at the top of this file (line 15)
// We don't need a second definition