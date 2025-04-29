// Copyright Epic Games, Inc. All Rights Reserved.

#include "../Public/SDFComputeKernelManager.h"
#include "../Public/Interface/SDFComputeKernelInterface.h"
#include "../../1_CoreRegistry/Public/SDFTypeRegistry.h"
#include "../../1_CoreRegistry/Public/MaterialRegistry.h"
#include "RenderCore.h"
#include "RenderGraphUtils.h"
#include "ScreenPass.h"
#include "ShaderCompiler.h"
#include "Misc/ScopeLock.h"
#include "Misc/CoreDelegates.h"
#include "Engine/Engine.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "ShaderCompilerCore.h"
#include "HAL/PlatformProcess.h"
#include "Async/ParallelFor.h"

// Initialize static members
FSDFComputeKernelManager* FSDFComputeKernelManager::Singleton = nullptr;

FSDFComputeKernelManager::FSDFComputeKernelManager()
    : bIsInitialized(false)
    , SdfTypeRegistry(nullptr)
    , MaterialRegistry(nullptr)
{
}

FSDFComputeKernelManager::~FSDFComputeKernelManager()
{
    Shutdown();
}

bool FSDFComputeKernelManager::Initialize()
{
    if (bIsInitialized)
    {
        return true;
    }

    // Check if we have valid registry references
    if (!SdfTypeRegistry)
    {
        SdfTypeRegistry = &FSDFTypeRegistry::Get();
    }

    if (!MaterialRegistry)
    {
        MaterialRegistry = &FMaterialRegistry::Get();
    }

    // Initialize default thread group sizes for different operations
    DefaultThreadGroupSizes.Add(ESDFOperationType::Union, FIntVector(8, 8, 4));
    DefaultThreadGroupSizes.Add(ESDFOperationType::Subtraction, FIntVector(8, 8, 4));
    DefaultThreadGroupSizes.Add(ESDFOperationType::Intersection, FIntVector(8, 8, 4));
    DefaultThreadGroupSizes.Add(ESDFOperationType::SmoothUnion, FIntVector(8, 8, 4));
    DefaultThreadGroupSizes.Add(ESDFOperationType::SmoothSubtraction, FIntVector(8, 8, 4));
    DefaultThreadGroupSizes.Add(ESDFOperationType::SmoothIntersection, FIntVector(8, 8, 4));
    DefaultThreadGroupSizes.Add(ESDFOperationType::Custom, FIntVector(8, 8, 4));
    DefaultThreadGroupSizes.Add(ESDFOperationType::Smoothing, FIntVector(8, 8, 4));
    DefaultThreadGroupSizes.Add(ESDFOperationType::Evaluation, FIntVector(8, 8, 4));
    DefaultThreadGroupSizes.Add(ESDFOperationType::Gradient, FIntVector(8, 8, 4));
    DefaultThreadGroupSizes.Add(ESDFOperationType::NarrowBandUpdate, FIntVector(8, 8, 4));
    DefaultThreadGroupSizes.Add(ESDFOperationType::MaterialTransition, FIntVector(8, 8, 4));
    DefaultThreadGroupSizes.Add(ESDFOperationType::VolumeRender, FIntVector(8, 8, 4));

    // Initialize default kernels
    InitializeDefaultKernels();

    // Initialize material-specific kernels
    InitializeMaterialKernels();

    // Initialize kernel fusion combinations
    InitializeKernelFusions();

    bIsInitialized = true;
    return true;
}

void FSDFComputeKernelManager::Shutdown()
{
    if (!bIsInitialized)
    {
        return;
    }

    // Clear kernel maps
    FScopeLock KernelLock(&KernelMapLock);
    KernelMap.Empty();
    MaterialKernelMap.Empty();
    KernelFusionMap.Empty();
    OperationToKernelMap.Empty();

    // Clear shader cache
    FScopeLock ShaderLock(&ShaderCacheLock);
    ShaderCache.Empty();
    KernelPerformanceMetrics.Empty();

    // Clear thread group sizes
    DefaultThreadGroupSizes.Empty();

    bIsInitialized = false;
}

FSDFComputeKernelManager& FSDFComputeKernelManager::Get()
{
    if (!Singleton)
    {
        Singleton = new FSDFComputeKernelManager();
    }
    return *Singleton;
}

void FSDFComputeKernelManager::InitializeDefaultKernels()
{
    // Basic Union operation
    {
        FComputeKernelDesc UnionKernel(FName("SDFUnion"));
        UnionKernel.ShaderFilePath = TEXT("/Engine/Private/MiningSystem/SDFOperations.usf");
        UnionKernel.EntryPointName = TEXT("SDFUnionCS");
        UnionKernel.KernelType = ESDFComputeKernelType::Basic;
        UnionKernel.Features = static_cast<ESDFComputeKernelFeatures>(
            static_cast<uint32>(ESDFComputeKernelFeatures::SupportsGPU) |
            static_cast<uint32>(ESDFComputeKernelFeatures::SupportsCPUFallback)
        );
        UnionKernel.DefaultThreadGroupSize = FIntVector(8, 8, 4);
        UnionKernel.SupportedOperations.Add(ESDFOperationType::Union);
        
        FKernelPermutationParameters& Permutation = UnionKernel.DefaultPermutation;
        Permutation.OperationType = ESDFOperationType::Union;
        Permutation.MaterialChannelCount = 1;
        Permutation.PrecisionMode = EComputeKernelPrecision::SinglePrecision;
        Permutation.bUseNarrowBand = false;
        
        RegisterKernel(UnionKernel);
    }

    // Basic Subtraction operation
    {
        FComputeKernelDesc SubtractionKernel(FName("SDFSubtraction"));
        SubtractionKernel.ShaderFilePath = TEXT("/Engine/Private/MiningSystem/SDFOperations.usf");
        SubtractionKernel.EntryPointName = TEXT("SDFSubtractionCS");
        SubtractionKernel.KernelType = ESDFComputeKernelType::Basic;
        SubtractionKernel.Features = static_cast<ESDFComputeKernelFeatures>(
            static_cast<uint32>(ESDFComputeKernelFeatures::SupportsGPU) |
            static_cast<uint32>(ESDFComputeKernelFeatures::SupportsCPUFallback)
        );
        SubtractionKernel.DefaultThreadGroupSize = FIntVector(8, 8, 4);
        SubtractionKernel.SupportedOperations.Add(ESDFOperationType::Subtraction);
        
        FKernelPermutationParameters& Permutation = SubtractionKernel.DefaultPermutation;
        Permutation.OperationType = ESDFOperationType::Subtraction;
        Permutation.MaterialChannelCount = 1;
        Permutation.PrecisionMode = EComputeKernelPrecision::SinglePrecision;
        Permutation.bUseNarrowBand = false;
        
        RegisterKernel(SubtractionKernel);
    }
    
    // Basic Intersection operation
    {
        FComputeKernelDesc IntersectionKernel(FName("SDFIntersection"));
        IntersectionKernel.ShaderFilePath = TEXT("/Engine/Private/MiningSystem/SDFOperations.usf");
        IntersectionKernel.EntryPointName = TEXT("SDFIntersectionCS");
        IntersectionKernel.KernelType = ESDFComputeKernelType::Basic;
        IntersectionKernel.Features = static_cast<ESDFComputeKernelFeatures>(
            static_cast<uint32>(ESDFComputeKernelFeatures::SupportsGPU) |
            static_cast<uint32>(ESDFComputeKernelFeatures::SupportsCPUFallback)
        );
        IntersectionKernel.DefaultThreadGroupSize = FIntVector(8, 8, 4);
        IntersectionKernel.SupportedOperations.Add(ESDFOperationType::Intersection);
        
        FKernelPermutationParameters& Permutation = IntersectionKernel.DefaultPermutation;
        Permutation.OperationType = ESDFOperationType::Intersection;
        Permutation.MaterialChannelCount = 1;
        Permutation.PrecisionMode = EComputeKernelPrecision::SinglePrecision;
        Permutation.bUseNarrowBand = false;
        
        RegisterKernel(IntersectionKernel);
    }

    // Narrow-band specialized Union operation
    {
        FComputeKernelDesc NarrowBandUnionKernel(FName("SDFNarrowBandUnion"));
        NarrowBandUnionKernel.ShaderFilePath = TEXT("/Engine/Private/MiningSystem/SDFOperations.usf");
        NarrowBandUnionKernel.EntryPointName = TEXT("SDFNarrowBandUnionCS");
        NarrowBandUnionKernel.KernelType = ESDFComputeKernelType::NarrowBand;
        NarrowBandUnionKernel.Features = static_cast<ESDFComputeKernelFeatures>(
            static_cast<uint32>(ESDFComputeKernelFeatures::SupportsGPU) |
            static_cast<uint32>(ESDFComputeKernelFeatures::SupportsNarrowBand)
        );
        NarrowBandUnionKernel.DefaultThreadGroupSize = FIntVector(8, 8, 4);
        NarrowBandUnionKernel.SupportedOperations.Add(ESDFOperationType::Union);
        
        FKernelPermutationParameters& Permutation = NarrowBandUnionKernel.DefaultPermutation;
        Permutation.OperationType = ESDFOperationType::Union;
        Permutation.MaterialChannelCount = 1;
        Permutation.PrecisionMode = EComputeKernelPrecision::SinglePrecision;
        Permutation.bUseNarrowBand = true;
        
        RegisterKernel(NarrowBandUnionKernel);
    }

    // Multi-channel SDF Union operation
    {
        FComputeKernelDesc MultiChannelUnionKernel(FName("SDFMultiChannelUnion"));
        MultiChannelUnionKernel.ShaderFilePath = TEXT("/Engine/Private/MiningSystem/SDFOperations.usf");
        MultiChannelUnionKernel.EntryPointName = TEXT("SDFMultiChannelUnionCS");
        MultiChannelUnionKernel.KernelType = ESDFComputeKernelType::Basic;
        MultiChannelUnionKernel.Features = static_cast<ESDFComputeKernelFeatures>(
            static_cast<uint32>(ESDFComputeKernelFeatures::SupportsGPU) |
            static_cast<uint32>(ESDFComputeKernelFeatures::SupportsMultiChannelSDF)
        );
        MultiChannelUnionKernel.DefaultThreadGroupSize = FIntVector(8, 8, 4);
        MultiChannelUnionKernel.SupportedOperations.Add(ESDFOperationType::Union);
        
        FKernelPermutationParameters& Permutation = MultiChannelUnionKernel.DefaultPermutation;
        Permutation.OperationType = ESDFOperationType::Union;
        Permutation.MaterialChannelCount = 4;
        Permutation.PrecisionMode = EComputeKernelPrecision::SinglePrecision;
        Permutation.bUseNarrowBand = false;
        
        RegisterKernel(MultiChannelUnionKernel);
    }

    // Drill tool specialized operation
    {
        FComputeKernelDesc DrillToolKernel(FName("SDFDrillTool"));
        DrillToolKernel.ShaderFilePath = TEXT("/Engine/Private/MiningSystem/SDFMiningTools.usf");
        DrillToolKernel.EntryPointName = TEXT("SDFDrillToolCS");
        DrillToolKernel.KernelType = ESDFComputeKernelType::DrillTool;
        DrillToolKernel.Features = static_cast<ESDFComputeKernelFeatures>(
            static_cast<uint32>(ESDFComputeKernelFeatures::SupportsGPU) |
            static_cast<uint32>(ESDFComputeKernelFeatures::SupportsMaterialAwareness)
        );
        DrillToolKernel.DefaultThreadGroupSize = FIntVector(8, 8, 4);
        DrillToolKernel.SupportedOperations.Add(ESDFOperationType::Subtraction);
        
        FKernelPermutationParameters& Permutation = DrillToolKernel.DefaultPermutation;
        Permutation.OperationType = ESDFOperationType::Subtraction;
        Permutation.MaterialChannelCount = 1;
        Permutation.PrecisionMode = EComputeKernelPrecision::SinglePrecision;
        Permutation.bUseNarrowBand = true;
        Permutation.bIsMaterialAware = true;
        
        RegisterKernel(DrillToolKernel);
    }

    // Explosive tool specialized operation
    {
        FComputeKernelDesc ExplosiveToolKernel(FName("SDFExplosiveTool"));
        ExplosiveToolKernel.ShaderFilePath = TEXT("/Engine/Private/MiningSystem/SDFMiningTools.usf");
        ExplosiveToolKernel.EntryPointName = TEXT("SDFExplosiveToolCS");
        ExplosiveToolKernel.KernelType = ESDFComputeKernelType::ExplosiveTool;
        ExplosiveToolKernel.Features = static_cast<ESDFComputeKernelFeatures>(
            static_cast<uint32>(ESDFComputeKernelFeatures::SupportsGPU) |
            static_cast<uint32>(ESDFComputeKernelFeatures::SupportsMaterialAwareness)
        );
        ExplosiveToolKernel.DefaultThreadGroupSize = FIntVector(8, 8, 4);
        ExplosiveToolKernel.SupportedOperations.Add(ESDFOperationType::Subtraction);
        
        FKernelPermutationParameters& Permutation = ExplosiveToolKernel.DefaultPermutation;
        Permutation.OperationType = ESDFOperationType::Subtraction;
        Permutation.MaterialChannelCount = 1;
        Permutation.PrecisionMode = EComputeKernelPrecision::SinglePrecision;
        Permutation.bUseNarrowBand = false;
        Permutation.bIsMaterialAware = true;
        
        RegisterKernel(ExplosiveToolKernel);
    }

    // Precision tool specialized operation
    {
        FComputeKernelDesc PrecisionToolKernel(FName("SDFPrecisionTool"));
        PrecisionToolKernel.ShaderFilePath = TEXT("/Engine/Private/MiningSystem/SDFMiningTools.usf");
        PrecisionToolKernel.EntryPointName = TEXT("SDFPrecisionToolCS");
        PrecisionToolKernel.KernelType = ESDFComputeKernelType::PrecisionTool;
        PrecisionToolKernel.Features = static_cast<ESDFComputeKernelFeatures>(
            static_cast<uint32>(ESDFComputeKernelFeatures::SupportsGPU) |
            static_cast<uint32>(ESDFComputeKernelFeatures::SupportsMaterialAwareness) |
            static_cast<uint32>(ESDFComputeKernelFeatures::SupportsNarrowBand)
        );
        PrecisionToolKernel.DefaultThreadGroupSize = FIntVector(8, 8, 4);
        PrecisionToolKernel.SupportedOperations.Add(ESDFOperationType::Subtraction);
        
        FKernelPermutationParameters& Permutation = PrecisionToolKernel.DefaultPermutation;
        Permutation.OperationType = ESDFOperationType::Subtraction;
        Permutation.MaterialChannelCount = 1;
        Permutation.PrecisionMode = EComputeKernelPrecision::SinglePrecision;
        Permutation.bUseNarrowBand = true;
        Permutation.bIsMaterialAware = true;
        
        RegisterKernel(PrecisionToolKernel);
    }
}

void FSDFComputeKernelManager::InitializeMaterialKernels()
{
    // Skip initialization if material registry is not available
    if (!MaterialRegistry)
    {
        return;
    }

    // Get all material types from the registry
    TArray<FMaterialTypeInfo> AllMaterialTypes = MaterialRegistry->GetAllMaterialTypes();

    for (const FMaterialTypeInfo& MaterialType : AllMaterialTypes)
    {
        // Skip non-mineable materials
        if (!MaterialType.bIsMineable)
        {
            continue;
        }

        // Create specialized kernels for mineable materials
        const uint32 MaterialTypeId = MaterialType.TypeId;

        // Mining subtraction kernel specialized for this material
        {
            FMaterialKernelInfo MaterialKernel(MaterialTypeId);
            
            MaterialKernel.KernelDesc = FComputeKernelDesc(FName(*FString::Printf(TEXT("SDFMaterial_%s_Subtraction"), *MaterialType.TypeName.ToString())));
            MaterialKernel.KernelDesc.ShaderFilePath = TEXT("/Engine/Private/MiningSystem/SDFMaterialOperations.usf");
            MaterialKernel.KernelDesc.EntryPointName = TEXT("SDFMaterialSubtractionCS");
            MaterialKernel.KernelDesc.KernelType = ESDFComputeKernelType::MaterialBlending;
            MaterialKernel.KernelDesc.Features = static_cast<ESDFComputeKernelFeatures>(
                static_cast<uint32>(ESDFComputeKernelFeatures::SupportsGPU) |
                static_cast<uint32>(ESDFComputeKernelFeatures::SupportsMaterialAwareness)
            );
            MaterialKernel.KernelDesc.DefaultThreadGroupSize = FIntVector(8, 8, 4);
            MaterialKernel.KernelDesc.SupportedOperations.Add(ESDFOperationType::Subtraction);
            MaterialKernel.KernelDesc.SupportedMaterialTypeIds.Add(MaterialTypeId);
            
            FKernelPermutationParameters& Permutation = MaterialKernel.MaterialPermutation;
            Permutation.OperationType = ESDFOperationType::Subtraction;
            Permutation.MaterialChannelCount = 1;
            Permutation.PrecisionMode = EComputeKernelPrecision::SinglePrecision;
            Permutation.bUseNarrowBand = false;
            Permutation.bIsMaterialAware = true;
            
            RegisterMaterialKernel(MaterialKernel);
        }

        // Material transition kernel for blending this material with others
        {
            FMaterialKernelInfo MaterialKernel(MaterialTypeId);
            
            MaterialKernel.KernelDesc = FComputeKernelDesc(FName(*FString::Printf(TEXT("SDFMaterial_%s_Transition"), *MaterialType.TypeName.ToString())));
            MaterialKernel.KernelDesc.ShaderFilePath = TEXT("/Engine/Private/MiningSystem/SDFMaterialOperations.usf");
            MaterialKernel.KernelDesc.EntryPointName = TEXT("SDFMaterialTransitionCS");
            MaterialKernel.KernelDesc.KernelType = ESDFComputeKernelType::MaterialTransition;
            MaterialKernel.KernelDesc.Features = static_cast<ESDFComputeKernelFeatures>(
                static_cast<uint32>(ESDFComputeKernelFeatures::SupportsGPU) |
                static_cast<uint32>(ESDFComputeKernelFeatures::SupportsMaterialAwareness) |
                static_cast<uint32>(ESDFComputeKernelFeatures::SupportsMultiChannelSDF)
            );
            MaterialKernel.KernelDesc.DefaultThreadGroupSize = FIntVector(8, 8, 4);
            MaterialKernel.KernelDesc.SupportedOperations.Add(ESDFOperationType::MaterialTransition);
            MaterialKernel.KernelDesc.SupportedMaterialTypeIds.Add(MaterialTypeId);
            
            FKernelPermutationParameters& Permutation = MaterialKernel.MaterialPermutation;
            Permutation.OperationType = ESDFOperationType::MaterialTransition;
            Permutation.MaterialChannelCount = MaterialType.ChannelCount > 0 ? MaterialType.ChannelCount : 1;
            Permutation.PrecisionMode = EComputeKernelPrecision::SinglePrecision;
            Permutation.bUseNarrowBand = false;
            Permutation.bIsMaterialAware = true;
            
            RegisterMaterialKernel(MaterialKernel);
        }
    }
}

void FSDFComputeKernelManager::InitializeKernelFusions()
{
    // Register some common operation fusions
    
    // Union + Smoothing fusion
    {
        FKernelFusionDesc FusionDesc;
        FusionDesc.Operations.Add(ESDFOperationType::Union);
        FusionDesc.Operations.Add(ESDFOperationType::Smoothing);
        
        FusionDesc.FusedKernelDesc = FComputeKernelDesc(FName("SDFFused_Union_Smoothing"));
        FusionDesc.FusedKernelDesc.ShaderFilePath = TEXT("/Engine/Private/MiningSystem/SDFFusedOperations.usf");
        FusionDesc.FusedKernelDesc.EntryPointName = TEXT("SDFFusedUnionSmoothingCS");
        FusionDesc.FusedKernelDesc.KernelType = ESDFComputeKernelType::KernelFusion;
        FusionDesc.FusedKernelDesc.Features = static_cast<ESDFComputeKernelFeatures>(
            static_cast<uint32>(ESDFComputeKernelFeatures::SupportsGPU) |
            static_cast<uint32>(ESDFComputeKernelFeatures::SupportsKernelFusion)
        );
        FusionDesc.FusedKernelDesc.DefaultThreadGroupSize = FIntVector(8, 8, 4);
        FusionDesc.FusedKernelDesc.SupportedOperations.Add(ESDFOperationType::Union);
        FusionDesc.FusedKernelDesc.SupportedOperations.Add(ESDFOperationType::Smoothing);
        
        FKernelPermutationParameters& Permutation = FusionDesc.FusedKernelDesc.DefaultPermutation;
        Permutation.OperationType = ESDFOperationType::Union; // Primary operation
        Permutation.MaterialChannelCount = 1;
        Permutation.PrecisionMode = EComputeKernelPrecision::SinglePrecision;
        Permutation.bUseNarrowBand = false;
        Permutation.bSupportsKernelFusion = true;
        
        FusionDesc.bIsAvailable = true;
        FusionDesc.PerformanceMultiplier = 1.8f; // Estimated performance gain
        
        RegisterKernelFusion(FusionDesc);
    }
    
    // Subtraction + MaterialTransition fusion
    {
        FKernelFusionDesc FusionDesc;
        FusionDesc.Operations.Add(ESDFOperationType::Subtraction);
        FusionDesc.Operations.Add(ESDFOperationType::MaterialTransition);
        
        FusionDesc.FusedKernelDesc = FComputeKernelDesc(FName("SDFFused_Subtraction_MaterialTransition"));
        FusionDesc.FusedKernelDesc.ShaderFilePath = TEXT("/Engine/Private/MiningSystem/SDFFusedOperations.usf");
        FusionDesc.FusedKernelDesc.EntryPointName = TEXT("SDFFusedSubtractionMaterialTransitionCS");
        FusionDesc.FusedKernelDesc.KernelType = ESDFComputeKernelType::KernelFusion;
        FusionDesc.FusedKernelDesc.Features = static_cast<ESDFComputeKernelFeatures>(
            static_cast<uint32>(ESDFComputeKernelFeatures::SupportsGPU) |
            static_cast<uint32>(ESDFComputeKernelFeatures::SupportsKernelFusion) |
            static_cast<uint32>(ESDFComputeKernelFeatures::SupportsMaterialAwareness) |
            static_cast<uint32>(ESDFComputeKernelFeatures::SupportsMultiChannelSDF)
        );
        FusionDesc.FusedKernelDesc.DefaultThreadGroupSize = FIntVector(8, 8, 4);
        FusionDesc.FusedKernelDesc.SupportedOperations.Add(ESDFOperationType::Subtraction);
        FusionDesc.FusedKernelDesc.SupportedOperations.Add(ESDFOperationType::MaterialTransition);
        
        FKernelPermutationParameters& Permutation = FusionDesc.FusedKernelDesc.DefaultPermutation;
        Permutation.OperationType = ESDFOperationType::Subtraction; // Primary operation
        Permutation.MaterialChannelCount = 2; // Support multiple channels
        Permutation.PrecisionMode = EComputeKernelPrecision::SinglePrecision;
        Permutation.bUseNarrowBand = false;
        Permutation.bIsMaterialAware = true;
        Permutation.bSupportsKernelFusion = true;
        
        FusionDesc.bIsAvailable = true;
        FusionDesc.PerformanceMultiplier = 2.0f; // Estimated performance gain
        
        RegisterKernelFusion(FusionDesc);
    }
}

bool FSDFComputeKernelManager::RegisterKernel(const FComputeKernelDesc& InKernelDesc)
{
    FScopeLock Lock(&KernelMapLock);
    
    // Check if kernel with this name already exists
    if (KernelMap.Contains(InKernelDesc.KernelName))
    {
        return false;
    }
    
    // Add to kernel map
    KernelMap.Add(InKernelDesc.KernelName, InKernelDesc);
    
    // Add to operation-to-kernel map
    for (ESDFOperationType Operation : InKernelDesc.SupportedOperations)
    {
        if (!OperationToKernelMap.Contains(Operation))
        {
            OperationToKernelMap.Add(Operation, TArray<FName>());
        }
        
        OperationToKernelMap[Operation].Add(InKernelDesc.KernelName);
    }
    
    return true;
}

bool FSDFComputeKernelManager::RegisterMaterialKernel(const FMaterialKernelInfo& InMaterialKernelInfo)
{
    FScopeLock Lock(&KernelMapLock);
    
    // Register the kernel descriptor
    if (!RegisterKernel(InMaterialKernelInfo.KernelDesc))
    {
        return false;
    }
    
    // Add to material kernel map
    for (uint32 MaterialTypeId : InMaterialKernelInfo.MaterialTypeIds)
    {
        if (!MaterialKernelMap.Contains(MaterialTypeId))
        {
            MaterialKernelMap.Add(MaterialTypeId, TArray<FMaterialKernelInfo>());
        }
        
        MaterialKernelMap[MaterialTypeId].Add(InMaterialKernelInfo);
    }
    
    return true;
}

bool FSDFComputeKernelManager::RegisterKernelFusion(const FKernelFusionDesc& InFusionDesc)
{
    FScopeLock Lock(&KernelMapLock);
    
    // Generate a key for the fusion
    FString FusionKey;
    for (ESDFOperationType Operation : InFusionDesc.Operations)
    {
        if (!FusionKey.IsEmpty())
        {
            FusionKey.Append(TEXT("_"));
        }
        FusionKey.Append(FString::FromInt(static_cast<int32>(Operation)));
    }
    
    // Check if fusion already exists
    if (KernelFusionMap.Contains(FusionKey))
    {
        return false;
    }
    
    // Register the fused kernel descriptor
    if (!RegisterKernel(InFusionDesc.FusedKernelDesc))
    {
        return false;
    }
    
    // Add to fusion map
    KernelFusionMap.Add(FusionKey, InFusionDesc);
    
    return true;
}

bool FSDFComputeKernelManager::GetKernel(ESDFOperationType InOperationType, FComputeKernelDesc& OutKernelDesc) const
{
    FScopeLock Lock(&KernelMapLock);
    
    // Check if we have kernels for this operation type
    if (!OperationToKernelMap.Contains(InOperationType) || OperationToKernelMap[InOperationType].Num() == 0)
    {
        return false;
    }
    
    // Get the first kernel that supports this operation
    const FName KernelName = OperationToKernelMap[InOperationType][0];
    if (!KernelMap.Contains(KernelName))
    {
        return false;
    }
    
    OutKernelDesc = KernelMap[KernelName];
    return true;
}

bool FSDFComputeKernelManager::GetMaterialKernel(ESDFOperationType InOperationType, uint32 InMaterialTypeId, FComputeKernelDesc& OutKernelDesc) const
{
    FScopeLock Lock(&KernelMapLock);
    
    // Check if we have specific kernels for this material
    if (MaterialKernelMap.Contains(InMaterialTypeId))
    {
        const TArray<FMaterialKernelInfo>& MaterialKernels = MaterialKernelMap[InMaterialTypeId];
        
        // Find a kernel that supports this operation type
        for (const FMaterialKernelInfo& MaterialKernel : MaterialKernels)
        {
            if (DoesKernelSupportOperation(MaterialKernel.KernelDesc, InOperationType))
            {
                OutKernelDesc = MaterialKernel.KernelDesc;
                return true;
            }
        }
    }
    
    // Fallback to general kernel for this operation
    return GetKernel(InOperationType, OutKernelDesc);
}

bool FSDFComputeKernelManager::GetFusedKernel(const TArray<ESDFOperationType>& InOperations, FComputeKernelDesc& OutKernelDesc) const
{
    FScopeLock Lock(&KernelMapLock);
    
    if (InOperations.Num() == 0)
    {
        return false;
    }
    
    // If only one operation, use regular kernel
    if (InOperations.Num() == 1)
    {
        return GetKernel(InOperations[0], OutKernelDesc);
    }
    
    // Generate key for the fusion
    FString FusionKey;
    for (ESDFOperationType Operation : InOperations)
    {
        if (!FusionKey.IsEmpty())
        {
            FusionKey.Append(TEXT("_"));
        }
        FusionKey.Append(FString::FromInt(static_cast<int32>(Operation)));
    }
    
    // Check if we have a fusion for this combination
    if (KernelFusionMap.Contains(FusionKey))
    {
        const FKernelFusionDesc& FusionDesc = KernelFusionMap[FusionKey];
        if (FusionDesc.bIsAvailable)
        {
            OutKernelDesc = FusionDesc.FusedKernelDesc;
            return true;
        }
    }
    
    // No fusion available
    return false;
}

bool FSDFComputeKernelManager::DoesKernelSupportOperation(const FComputeKernelDesc& InKernelDesc, ESDFOperationType InOperationType) const
{
    return InKernelDesc.SupportedOperations.Contains(InOperationType);
}

bool FSDFComputeKernelManager::DoesKernelSupportMaterial(const FComputeKernelDesc& InKernelDesc, uint32 InMaterialTypeId) const
{
    // If no specific materials are listed, kernel supports all materials
    if (InKernelDesc.SupportedMaterialTypeIds.Num() == 0)
    {
        return true;
    }
    
    return InKernelDesc.SupportedMaterialTypeIds.Contains(InMaterialTypeId);
}

bool FSDFComputeKernelManager::GenerateKernelPermutation(ESDFOperationType InOperationType, const TArray<uint32>& InMaterialTypeIds, FKernelPermutationParameters& OutPermutation) const
{
    // Start with default values
    OutPermutation = FKernelPermutationParameters();
    OutPermutation.OperationType = InOperationType;
    
    // If no material registry, use default permutation
    if (!MaterialRegistry)
    {
        return true;
    }
    
    // Determine if we need material awareness
    bool bIsMaterialAware = InMaterialTypeIds.Num() > 0;
    OutPermutation.bIsMaterialAware = bIsMaterialAware;
    
    // Set material channel count based on materials
    int32 MaxChannelCount = 1;
    
    for (uint32 MaterialTypeId : InMaterialTypeIds)
    {
        const FMaterialTypeInfo* MaterialInfo = MaterialRegistry->GetMaterialTypeInfo(MaterialTypeId);
        if (MaterialInfo)
        {
            // Use the maximum channel count from all materials
            MaxChannelCount = FMath::Max(MaxChannelCount, MaterialInfo->ChannelCount);
            
            // Check if material requires narrow band optimization
            if (MaterialInfo->HasCapability(EMaterialCapabilities::SupportsSpatialCoherence))
            {
                OutPermutation.bUseNarrowBand = true;
            }
            
            // Check if material supports vectorization
            if (MaterialInfo->HasCapability(EMaterialCapabilities::SupportsVectorization))
            {
                OutPermutation.bUsesOptimizedMemoryAccess = true;
            }
            
            // Check if material supports concurrent access
            if (MaterialInfo->HasCapability(EMaterialCapabilities::SupportsConcurrentAccess))
            {
                OutPermutation.bSupportsWaveIntrinsics = true;
            }
        }
    }
    
    OutPermutation.MaterialChannelCount = MaxChannelCount;
    
    // Set operation-specific options
    switch (InOperationType)
    {
        case ESDFOperationType::Union:
        case ESDFOperationType::Subtraction:
        case ESDFOperationType::Intersection:
            // These basic operations can use all optimizations
            break;
            
        case ESDFOperationType::SmoothUnion:
        case ESDFOperationType::SmoothSubtraction:
        case ESDFOperationType::SmoothIntersection:
            // Smooth operations benefit from higher precision
            OutPermutation.PrecisionMode = EComputeKernelPrecision::SinglePrecision;
            break;
            
        case ESDFOperationType::NarrowBandUpdate:
            // Always use narrow band for this operation
            OutPermutation.bUseNarrowBand = true;
            break;
            
        case ESDFOperationType::MaterialTransition:
            // Always material aware and use multiple channels
            OutPermutation.bIsMaterialAware = true;
            OutPermutation.MaterialChannelCount = FMath::Max(2, MaxChannelCount);
            break;
            
        default:
            break;
    }
    
    return true;
}

FIntVector FSDFComputeKernelManager::GenerateOptimalThreadGroupSize(const FComputeKernelDesc& InKernelDesc, const FIntVector& InVolumeSize) const
{
    // Start with the default thread group size for this kernel
    FIntVector ThreadGroupSize = InKernelDesc.DefaultThreadGroupSize;
    
    // If the operation has a default thread group size, use that
    if (InKernelDesc.SupportedOperations.Num() > 0)
    {
        ESDFOperationType Operation = InKernelDesc.SupportedOperations[0];
        if (DefaultThreadGroupSizes.Contains(Operation))
        {
            ThreadGroupSize = DefaultThreadGroupSizes[Operation];
        }
    }
    
    // Adjust based on volume size to avoid inefficient partial groups
    if (InVolumeSize.X > 0 && InVolumeSize.Y > 0 && InVolumeSize.Z > 0)
    {
        // Adjust X dimension
        for (int32 i = ThreadGroupSize.X; i > 1; i--)
        {
            if (InVolumeSize.X % i == 0)
            {
                ThreadGroupSize.X = i;
                break;
            }
        }
        
        // Adjust Y dimension
        for (int32 i = ThreadGroupSize.Y; i > 1; i--)
        {
            if (InVolumeSize.Y % i == 0)
            {
                ThreadGroupSize.Y = i;
                break;
            }
        }
        
        // Adjust Z dimension
        for (int32 i = ThreadGroupSize.Z; i > 1; i--)
        {
            if (InVolumeSize.Z % i == 0)
            {
                ThreadGroupSize.Z = i;
                break;
            }
        }
    }
    
    return ThreadGroupSize;
}

void FSDFComputeKernelManager::UpdateKernelPerformanceMetrics(const FComputeKernelDesc& InKernelDesc, const FKernelPermutationParameters& InPermutation, float InExecutionTime)
{
    FScopeLock Lock(&ShaderCacheLock);
    
    // Get hash for this permutation
    uint32 PermutationHash = GetKernelPermutationHash(InKernelDesc, InPermutation);
    
    // Update performance metrics
    if (KernelPerformanceMetrics.Contains(PermutationHash))
    {
        // Use exponential moving average to smooth out fluctuations
        const float Alpha = 0.2f;
        KernelPerformanceMetrics[PermutationHash] = KernelPerformanceMetrics[PermutationHash] * (1.0f - Alpha) + InExecutionTime * Alpha;
    }
    else
    {
        KernelPerformanceMetrics.Add(PermutationHash, InExecutionTime);
    }
    
    // Update cache entry if it exists
    if (ShaderCache.Contains(PermutationHash))
    {
        FKernelCacheEntry& CacheEntry = ShaderCache[PermutationHash];
        CacheEntry.PerformanceScore = KernelPerformanceMetrics[PermutationHash];
        CacheEntry.LastUseTime = FPlatformTime::Seconds();
        CacheEntry.UseCount++;
    }
}

bool FSDFComputeKernelManager::GetBestPerformingKernel(ESDFOperationType InOperationType, const TArray<uint32>& InMaterialTypeIds, FComputeKernelDesc& OutKernelDesc, FKernelPermutationParameters& OutPermutation) const
{
    FScopeLock KernelLock(&KernelMapLock);
    FScopeLock ShaderLock(&ShaderCacheLock);
    
    float BestPerformance = FLT_MAX;
    bool bFoundKernel = false;
    
    // First, check if we have material-specific kernels
    if (InMaterialTypeIds.Num() > 0)
    {
        // Try to find a specialized kernel for these materials
        if (FindBestMaterialKernel(InMaterialTypeIds, InOperationType, OutKernelDesc))
        {
            // Found a specialized kernel, generate permutation
            if (GenerateKernelPermutation(InOperationType, InMaterialTypeIds, OutPermutation))
            {
                return true;
            }
        }
    }
    
    // If no specialized kernel found, find the best generic kernel
    if (OperationToKernelMap.Contains(InOperationType))
    {
        for (const FName& KernelName : OperationToKernelMap[InOperationType])
        {
            if (KernelMap.Contains(KernelName))
            {
                const FComputeKernelDesc& KernelDesc = KernelMap[KernelName];
                
                // Generate permutation for this operation and materials
                FKernelPermutationParameters Permutation;
                if (!GenerateKernelPermutation(InOperationType, InMaterialTypeIds, Permutation))
                {
                    continue;
                }
                
                // Check performance metrics
                uint32 PermutationHash = GetKernelPermutationHash(KernelDesc, Permutation);
                float Performance = KernelPerformanceMetrics.Contains(PermutationHash) ? KernelPerformanceMetrics[PermutationHash] : FLT_MAX;
                
                // Update if this is the best performer so far
                if (Performance < BestPerformance)
                {
                    BestPerformance = Performance;
                    OutKernelDesc = KernelDesc;
                    OutPermutation = Permutation;
                    bFoundKernel = true;
                }
            }
        }
    }
    
    // If no kernel found at all, use the default permutation
    if (!bFoundKernel)
    {
        if (GetKernel(InOperationType, OutKernelDesc))
        {
            OutPermutation = OutKernelDesc.DefaultPermutation;
            bFoundKernel = true;
        }
    }
    
    return bFoundKernel;
}

bool FSDFComputeKernelManager::FindBestMaterialKernel(const TArray<uint32>& InMaterialTypeIds, ESDFOperationType InOperationType, FComputeKernelDesc& OutKernelDesc) const
{
    // If no materials specified, use generic kernel
    if (InMaterialTypeIds.Num() == 0)
    {
        return GetKernel(InOperationType, OutKernelDesc);
    }
    
    // First, check if we have a kernel that supports all specified materials
    TArray<FComputeKernelDesc> CandidateKernels;
    
    for (uint32 MaterialTypeId : InMaterialTypeIds)
    {
        if (MaterialKernelMap.Contains(MaterialTypeId))
        {
            for (const FMaterialKernelInfo& MaterialKernel : MaterialKernelMap[MaterialTypeId])
            {
                if (DoesKernelSupportOperation(MaterialKernel.KernelDesc, InOperationType))
                {
                    CandidateKernels.Add(MaterialKernel.KernelDesc);
                }
            }
        }
    }
    
    // If we found candidate kernels, find the best one
    if (CandidateKernels.Num() > 0)
    {
        float BestScore = -1.0f;
        int32 BestIndex = -1;
        
        // Score each kernel based on how many of our materials it supports
        for (int32 i = 0; i < CandidateKernels.Num(); ++i)
        {
            float Score = 0.0f;
            
            for (uint32 MaterialTypeId : InMaterialTypeIds)
            {
                if (DoesKernelSupportMaterial(CandidateKernels[i], MaterialTypeId))
                {
                    Score += 1.0f;
                }
            }
            
            // Normalize score
            Score /= InMaterialTypeIds.Num();
            
            if (Score > BestScore)
            {
                BestScore = Score;
                BestIndex = i;
            }
        }
        
        if (BestIndex >= 0)
        {
            OutKernelDesc = CandidateKernels[BestIndex];
            return true;
        }
    }
    
    // Fallback to generic kernel
    return GetKernel(InOperationType, OutKernelDesc);
}

uint32 FSDFComputeKernelManager::GetKernelPermutationHash(const FComputeKernelDesc& InKernelDesc, const FKernelPermutationParameters& InPermutation) const
{
    uint32 Hash = GetTypeHash(InKernelDesc.KernelName);
    Hash = HashCombine(Hash, InPermutation.GetHash());
    return Hash;
}

// Template implementations for parameter binding
template<typename ParamType>
bool FSDFComputeKernelManager::SetDynamicShaderParameters(FRDGBuilder& InRDGBuilder, TShaderRef<FSDFComputeShaderBase> InShaderRef, const FComputeKernelDesc& InKernelDesc, const FKernelPermutationParameters& InPermutation, ParamType* InParams) const
{
    // Set operation-specific parameters based on permutation
    InParams->OperationType = static_cast<int32>(InPermutation.OperationType);
    
    // Set narrow band parameters if enabled
    if (InPermutation.bUseNarrowBand)
    {
        InParams->UseNarrowBand = 1;
        InParams->NarrowBandThreshold = 2.0f; // Default narrow band threshold
    }
    else
    {
        InParams->UseNarrowBand = 0;
    }
    
    // Set multi-channel parameters if needed
    if (InPermutation.MaterialChannelCount > 1)
    {
        InParams->ChannelCount = InPermutation.MaterialChannelCount;
        InParams->PrimaryChannel = 0;
    }
    else
    {
        InParams->ChannelCount = 1;
        InParams->PrimaryChannel = 0;
    }
    
    return true;
}

// Template implementation for dispatch
template<typename ParamType>
bool FSDFComputeKernelManager::DispatchComputeShader(FRDGBuilder& InRDGBuilder, TShaderRef<FSDFComputeShaderBase> InShaderRef, const FComputeKernelDesc& InKernelDesc, const FKernelPermutationParameters& InPermutation, ParamType* InParams, const FIntVector& InThreadGroupCount) const
{
    FComputeShaderUtils::AddPass(
        InRDGBuilder,
        RDG_EVENT_NAME("SDF %s", *InKernelDesc.KernelName.ToString()),
        ComputeShaderUtils::EPassFlags::None,
        InShaderRef,
        InParams,
        InThreadGroupCount
    );
    
    return true;
}
