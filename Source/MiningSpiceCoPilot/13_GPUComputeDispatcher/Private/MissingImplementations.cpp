#include "../Public/GPUDispatcher.h"
#include "../Public/GPUDispatcherLogging.h"
#include "../Public/HardwareProfileManager.h"
#include "../Public/SDFComputeKernelManager.h"
#include "../Public/ZeroCopyResourceManager.h"

// Implementation of missing function in FGPUDispatcher
uint32 FGPUDispatcher::CalculateOperationDataSize(const FComputeOperation& Operation) const
{
    // Calculate approximate data size for metrics
    FVector Size = Operation.Bounds.GetSize();
    
    // Get the cell size from the HardwareProfileManager if available
    FVector CellSize(1.0f, 1.0f, 1.0f);
    if (HardwareProfileManager)
    {
        const FHardwareProfile& Profile = HardwareProfileManager->GetCurrentProfile();
        FSDFComputeKernel* Kernel = KernelManager->FindBestKernelForOperation(Operation.OperationType, Operation);
        if (Kernel)
        {
            CellSize = Kernel->CellSize;
        }
    }
    
    // Calculate number of voxels based on the bounds and cell size
    int32 VoxelsX = FMath::Max(1, FMath::CeilToInt(Size.X / CellSize.X));
    int32 VoxelsY = FMath::Max(1, FMath::CeilToInt(Size.Y / CellSize.Y));
    int32 VoxelsZ = FMath::Max(1, FMath::CeilToInt(Size.Z / CellSize.Z));
    uint32 VoxelCount = VoxelsX * VoxelsY * VoxelsZ;
    
    // If using narrow band optimization, we process fewer voxels
    if (Operation.bUseNarrowBand)
    {
        // In narrow band optimization, we typically only process 20-30% of the voxels
        VoxelCount = static_cast<uint32>(VoxelCount * 0.3f);
    }
    
    // Assume 4 bytes per voxel for SDF field (32-bit float)
    uint32 BytesPerVoxel = 4;
    
    // If high precision is required, use 8 bytes per voxel (64-bit double)
    if (Operation.bRequiresHighPrecision)
    {
        BytesPerVoxel = 8;
    }
    
    // If operation uses multiple material channels, multiply by channel count
    if (Operation.MaterialChannelId >= 0)
    {
        BytesPerVoxel *= 4; // Assume 4 channels on average for materials
    }
    
    return VoxelCount * BytesPerVoxel;
}

// Implementation of missing function in FGPUDispatcher
bool FGPUDispatcher::ProcessOnGPU(const FComputeOperation& Operation)
{
    // Get compute kernel for this operation
    FSDFComputeKernel* Kernel = KernelManager->FindBestKernelForOperation(Operation.OperationType, Operation);
    if (!Kernel)
    {
        GPU_DISPATCHER_LOG_WARNING("No suitable kernel found for operation %d", Operation.OperationType);
        return false;
    }
    
    // Prepare dispatch command to process the operation on GPU
    bool bSuccess = false;
    
    // Queue render command to execute the operation
    // Create local copies to avoid capturing 'this' and avoid reference capture problems
    FComputeOperation OpCopy = Operation;
    FSDFComputeKernel* KernelCopy = Kernel;
    FGPUDispatcher* Dispatcher = this;
    
    ENQUEUE_RENDER_COMMAND(ProcessComputeOperation)(
        [Dispatcher, OpCopy, KernelCopy, &bSuccess](FRHICommandListImmediate& RHICmdList)
        {
            FRDGBuilder GraphBuilder(RHICmdList);
            
            // Set up shader parameters
            FSDFOperationParameters* ShaderParams = GraphBuilder.AllocParameters<FSDFOperationParameters>();
            
            // Set operation parameters
            ShaderParams->OperationType = OpCopy.OperationType;
            ShaderParams->BoundsMin = FVector3f(OpCopy.Bounds.Min);
            ShaderParams->BoundsMax = FVector3f(OpCopy.Bounds.Max);
            ShaderParams->Strength = OpCopy.Strength;
            ShaderParams->BlendWeight = OpCopy.BlendWeight;
            ShaderParams->UseNarrowBand = OpCopy.bUseNarrowBand ? 1 : 0;
            ShaderParams->UseHighPrecision = OpCopy.bRequiresHighPrecision ? 1 : 0;
            
            // Calculate volume size based on bounds
            FVector VolumeSize = OpCopy.Bounds.GetSize();
            ShaderParams->VolumeSize = FVector3f(VolumeSize);
            
            // Set up dispatch parameters
            FIntVector VolumeRes(
                FMath::Max(1, FMath::CeilToInt(VolumeSize.X / KernelCopy->CellSize.X)),
                FMath::Max(1, FMath::CeilToInt(VolumeSize.Y / KernelCopy->CellSize.Y)),
                FMath::Max(1, FMath::CeilToInt(VolumeSize.Z / KernelCopy->CellSize.Z))
            );
            
            ShaderParams->VolumeWidth = VolumeRes.X;
            ShaderParams->VolumeHeight = VolumeRes.Y;
            ShaderParams->VolumeDepth = VolumeRes.Z;
            
            // Create dispatch parameters
            const FIntVector GroupSize(
                FMath::DivideAndRoundUp(VolumeRes.X, (int32)KernelCopy->ThreadGroupSizeX),
                FMath::DivideAndRoundUp(VolumeRes.Y, (int32)KernelCopy->ThreadGroupSizeY),
                FMath::DivideAndRoundUp(VolumeRes.Z, (int32)KernelCopy->ThreadGroupSizeZ)
            );
            
            // Get the appropriate shader based on operation type
            FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
            TShaderMapRef<FComputeShaderType> ComputeShader(ShaderMap);
            
            // Add compute pass using our updated utility class name
            FMiningSDFComputeShaderUtils::AddPass(
                GraphBuilder,
                TEXT("SDFOperation"),
                ComputeShader,
                ShaderParams,
                GroupSize
            );
            
            // Execute graph
            GraphBuilder.Execute();
            
            // Operation completed successfully
            bSuccess = true;
        });
    
    // Wait for command to complete
    FlushRenderingCommands();
    
    return bSuccess;
}