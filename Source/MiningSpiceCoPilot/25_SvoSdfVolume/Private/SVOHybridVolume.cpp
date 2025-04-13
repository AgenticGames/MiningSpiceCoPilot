// SVOHybridVolume.cpp
// Implementation of the hybrid volume representation combining sparse octree with multi-channel SDFs

#include "../Public/SVOHybridVolume.h"
#include "../Public/OctreeNodeManager.h"
#include "../Public/MaterialSDFManager.h"
#include "../Public/DistanceFieldEvaluator.h"
#include "../Public/MaterialInteractionModel.h"
#include "../Public/VolumeSerializer.h"
#include "../Public/NetworkVolumeCoordinator.h"
#include "../Public/NarrowBandAllocator.h"
#include "../Public/MemoryTelemetry.h"
#include "MiningSpiceCoPilot/2_MemoryManagement/Public/Interfaces/IMemoryTracker.h"
#include "MiningSpiceCoPilot/4_EventSystem/Public/EventBus.h"
#include "Net/UnrealNetwork.h"
#include "Engine/ActorChannel.h"
#include "Algo/Transform.h"

// For Core Registry integration
#include "MiningSpiceCoPilot/1_CoreRegistry/Public/ServiceLocator.h"
#include "MiningSpiceCoPilot/5_ConfigManagement/Public/ConfigManager.h"
#include "MiningSpiceCoPilot/3_ThreadingSystem/Public/TransactionManager.h"

USVOHybridVolume::USVOHybridVolume()
    : CurrentStateVersion(0)
{
    // Initialize member variables
    // Use the Service Locator to resolve dependencies as per System 1 integration requirements
    IServiceLocator* ServiceLocator = IServiceLocator::Get();
    
    // Get configuration manager
    IConfigManager* ConfigManager = nullptr;
    if (ServiceLocator)
    {
        ConfigManager = ServiceLocator->ResolveService<IConfigManager>();
    }
    
    // Default parameters (will be overridden by configuration if available)
    FIntVector DefaultDimensions(1024, 1024, 1024);
    float DefaultLeafNodeSize = 30.0f;  // 30cm leaf nodes
    uint8 DefaultMaxDepth = 8;
    uint32 DefaultMaterialCount = 32;
    
    // Apply configuration if available
    if (ConfigManager)
    {
        ConfigManager->GetValue("SVOHybridVolume.WorldDimensions", DefaultDimensions);
        ConfigManager->GetValue("SVOHybridVolume.LeafNodeSize", DefaultLeafNodeSize);
        ConfigManager->GetValue("SVOHybridVolume.MaxDepth", DefaultMaxDepth);
        ConfigManager->GetValue("SVOHybridVolume.MaterialCount", DefaultMaterialCount);
    }
}

USVOHybridVolume::~USVOHybridVolume()
{
    // Ensure proper cleanup - TUniquePtr will handle deletion
}

void USVOHybridVolume::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    
    // Setup properties for replication
    DOREPLIFETIME(USVOHybridVolume, CurrentStateVersion);
}

void USVOHybridVolume::Initialize(const FIntVector& WorldDimensions, float LeafNodeSize, uint8 MaxDepth)
{
    InitializeInternal(WorldDimensions, LeafNodeSize, MaxDepth);
}

void USVOHybridVolume::InitializeInternal(const FIntVector& WorldDimensions, float LeafNodeSize, uint8 MaxDepth)
{
    // Create Memory Telemetry first for tracking
    TUniquePtr<FMemoryTelemetry> MemoryTelemetryPtr = MakeUnique<FMemoryTelemetry>();
    
    // Initialize components in the correct order to ensure dependencies are met
    
    // 1. Narrow Band Allocator
    NarrowBandAllocator = MakeUnique<FNarrowBandAllocator>();
    NarrowBandAllocator->Initialize(1024 * 1024, 64); // 1MB blocks, 64 max materials
    
    // 2. Octree Node Manager
    OctreeManager = MakeUnique<FOctreeNodeManager>();
    OctreeManager->Initialize(WorldDimensions, LeafNodeSize, MaxDepth);
    
    // 3. Material SDF Manager
    MaterialManager = MakeUnique<FMaterialSDFManager>();
    MaterialManager->Initialize(32, FBox(FVector(-WorldDimensions) * LeafNodeSize, WorldDimensions * LeafNodeSize));
    MaterialManager->SetOctreeManager(OctreeManager.Get());
    MaterialManager->SetNarrowBandAllocator(NarrowBandAllocator.Get());
    
    // 4. Distance Field Evaluator
    FieldEvaluator = MakeUnique<FDistanceFieldEvaluator>();
    FieldEvaluator->Initialize(OctreeManager.Get(), MaterialManager.Get());
    
    // 5. Material Interaction Model
    InteractionModel = MakeUnique<FMaterialInteractionModel>();
    InteractionModel->Initialize(MaterialManager.Get(), OctreeManager.Get());
    
    // 6. Volume Serializer
    Serializer = MakeUnique<FVolumeSerializer>();
    Serializer->Initialize(this, OctreeManager.Get(), MaterialManager.Get());
    
    // 7. Network Volume Coordinator
    NetworkCoordinator = MakeUnique<FNetworkVolumeCoordinator>();
    NetworkCoordinator->Initialize(this, Serializer.Get());
    
    // Complete memory telemetry setup now that all components are created
    MemoryTelemetryPtr->Initialize(this, OctreeManager.Get(), MaterialManager.Get(), NarrowBandAllocator.Get());
    
    // Connect components to memory telemetry
    OctreeManager->SetMemoryTelemetry(MemoryTelemetryPtr.Get());
    NarrowBandAllocator->SetMemoryTelemetry(MemoryTelemetryPtr.Get());
    MaterialManager->SetMemoryTelemetry(MemoryTelemetryPtr.Get());
    
    // Initial volume version
    CurrentStateVersion = 1;
    
    // Register initial state as network version for synchronization baseline
    RegisterNetworkVersion(CurrentStateVersion);
    
    // Publish initialization event
    if (UEventBus* EventBus = UEventBus::Get())
    {
        // Create event context with volume information
        FVolumeInitializedEventContext Context;
        Context.Volume = this;
        Context.WorldDimensions = WorldDimensions;
        Context.LeafNodeSize = LeafNodeSize;
        Context.MaxDepth = MaxDepth;
        
        // Publish event
        EventBus->PublishEvent(EVolumeEvents::Initialized, &Context);
    }
}

void USVOHybridVolume::SetMaterialChannelCount(uint32 MaterialCount)
{
    if (MaterialManager.IsValid())
    {
        MaterialManager->SetChannelCount(MaterialCount);
    }
}

float USVOHybridVolume::EvaluateDistanceField(const FVector& WorldPosition, uint8 MaterialIndex) const
{
    if (FieldEvaluator.IsValid())
    {
        return FieldEvaluator->EvaluateDistanceField(WorldPosition, MaterialIndex);
    }
    return FLT_MAX;
}

TArray<float> USVOHybridVolume::EvaluateMultiChannelField(const FVector& WorldPosition) const
{
    if (FieldEvaluator.IsValid())
    {
        return FieldEvaluator->EvaluateMultiChannelField(WorldPosition);
    }
    return TArray<float>();
}

FVector USVOHybridVolume::EvaluateGradient(const FVector& WorldPosition, uint8 MaterialIndex) const
{
    if (FieldEvaluator.IsValid())
    {
        return FieldEvaluator->EvaluateGradient(WorldPosition, MaterialIndex);
    }
    return FVector::ZeroVector;
}

bool USVOHybridVolume::IsPositionInside(const FVector& WorldPosition, uint8 MaterialIndex) const
{
    if (FieldEvaluator.IsValid())
    {
        return FieldEvaluator->IsPositionInside(WorldPosition, MaterialIndex);
    }
    return false;
}

void USVOHybridVolume::UnionMaterial(const FVector& Position, float Radius, uint8 MaterialIndex, float Strength)
{
    // Validate the operation before proceeding
    if (!ValidateOperation(Position, Radius, MaterialIndex))
    {
        UE_LOG(LogTemp, Warning, TEXT("UnionMaterial operation failed validation"));
        return;
    }
    
    // Create a transaction if using the transaction system from System 3
    ITransactionManager* TransManager = nullptr;
    uint64 TransactionId = 0;
    
    if (IServiceLocator::Get())
    {
        TransManager = IServiceLocator::Get()->ResolveService<ITransactionManager>();
        if (TransManager)
        {
            TransactionId = TransManager->BeginTransaction(ETransactionConcurrency::Optimistic);
        }
    }
    
    // Calculate affected region for network tracking
    FBox AffectedRegion(Position - FVector(Radius), Position + FVector(Radius));
    
    // Request region modification from network coordinator
    if (NetworkCoordinator.IsValid())
    {
        auto Result = NetworkCoordinator->RequestRegionModification(AffectedRegion, MaterialIndex);
        if (Result != FNetworkVolumeCoordinator::ENetworkResult::Success)
        {
            UE_LOG(LogTemp, Warning, TEXT("Network coordinator denied UnionMaterial operation"));
            // Abort transaction if it was created
            if (TransManager && TransactionId != 0)
            {
                TransManager->AbortTransaction(TransactionId);
            }
            return;
        }
    }
    
    // Perform the operation
    if (MaterialManager.IsValid())
    {
        MaterialManager->UnionSphere(Position, Radius, MaterialIndex, Strength);
    }
    
    // Update field with new data
    ProcessFieldUpdate(Position, Radius, MaterialIndex);
    
    // Commit the transaction if it was created
    if (TransManager && TransactionId != 0)
    {
        TransManager->CommitTransaction(TransactionId);
    }
    
    // Send network delta if needed
    if (NetworkCoordinator.IsValid())
    {
        TArray<uint8> DeltaData = GenerateNetworkDelta(CurrentStateVersion - 1, CurrentStateVersion);
        NetworkCoordinator->SubmitRegionModification(AffectedRegion, MaterialIndex, DeltaData, CurrentStateVersion - 1);
    }
    
    // Publish event for other systems
    if (UEventBus* EventBus = UEventBus::Get())
    {
        FVolumeMaterialModifiedEventContext Context;
        Context.Volume = this;
        Context.Position = Position;
        Context.Radius = Radius;
        Context.MaterialIndex = MaterialIndex;
        Context.OperationType = EVolumeMaterialOperation::Union;
        Context.Strength = Strength;
        
        EventBus->PublishEvent(EVolumeEvents::MaterialModified, &Context);
    }
}

void USVOHybridVolume::SubtractMaterial(const FVector& Position, float Radius, uint8 MaterialIndex, float Strength)
{
    // Similar to UnionMaterial but with subtraction operation
    if (!ValidateOperation(Position, Radius, MaterialIndex))
    {
        UE_LOG(LogTemp, Warning, TEXT("SubtractMaterial operation failed validation"));
        return;
    }
    
    ITransactionManager* TransManager = nullptr;
    uint64 TransactionId = 0;
    
    if (IServiceLocator::Get())
    {
        TransManager = IServiceLocator::Get()->ResolveService<ITransactionManager>();
        if (TransManager)
        {
            TransactionId = TransManager->BeginTransaction(ETransactionConcurrency::Optimistic);
        }
    }
    
    // Calculate affected region
    FBox AffectedRegion(Position - FVector(Radius), Position + FVector(Radius));
    
    // Request network permission
    if (NetworkCoordinator.IsValid())
    {
        auto Result = NetworkCoordinator->RequestRegionModification(AffectedRegion, MaterialIndex);
        if (Result != FNetworkVolumeCoordinator::ENetworkResult::Success)
        {
            if (TransManager && TransactionId != 0)
            {
                TransManager->AbortTransaction(TransactionId);
            }
            return;
        }
    }
    
    // Perform the operation
    if (MaterialManager.IsValid())
    {
        MaterialManager->SubtractSphere(Position, Radius, MaterialIndex, Strength);
    }
    
    ProcessFieldUpdate(Position, Radius, MaterialIndex);
    
    if (TransManager && TransactionId != 0)
    {
        TransManager->CommitTransaction(TransactionId);
    }
    
    // Network sync
    if (NetworkCoordinator.IsValid())
    {
        TArray<uint8> DeltaData = GenerateNetworkDelta(CurrentStateVersion - 1, CurrentStateVersion);
        NetworkCoordinator->SubmitRegionModification(AffectedRegion, MaterialIndex, DeltaData, CurrentStateVersion - 1);
    }
    
    // Event notification
    if (UEventBus* EventBus = UEventBus::Get())
    {
        FVolumeMaterialModifiedEventContext Context;
        Context.Volume = this;
        Context.Position = Position;
        Context.Radius = Radius;
        Context.MaterialIndex = MaterialIndex;
        Context.OperationType = EVolumeMaterialOperation::Subtract;
        Context.Strength = Strength;
        
        EventBus->PublishEvent(EVolumeEvents::MaterialModified, &Context);
    }
}

void USVOHybridVolume::BlendMaterials(const FVector& Position, float Radius, uint8 SourceMaterial, uint8 TargetMaterial, float BlendFactor)
{
    // Validate the operation
    if (!ValidateOperation(Position, Radius, SourceMaterial) || !ValidateOperation(Position, Radius, TargetMaterial))
    {
        UE_LOG(LogTemp, Warning, TEXT("BlendMaterials operation failed validation"));
        return;
    }
    
    ITransactionManager* TransManager = nullptr;
    uint64 TransactionId = 0;
    
    if (IServiceLocator::Get())
    {
        TransManager = IServiceLocator::Get()->ResolveService<ITransactionManager>();
        if (TransManager)
        {
            TransactionId = TransManager->BeginTransaction(ETransactionConcurrency::Optimistic);
        }
    }
    
    // Calculate affected region
    FBox AffectedRegion(Position - FVector(Radius), Position + FVector(Radius));
    
    // Request network permission
    if (NetworkCoordinator.IsValid())
    {
        auto Result = NetworkCoordinator->RequestRegionModification(AffectedRegion, TargetMaterial);
        if (Result != FNetworkVolumeCoordinator::ENetworkResult::Success)
        {
            if (TransManager && TransactionId != 0)
            {
                TransManager->AbortTransaction(TransactionId);
            }
            return;
        }
    }
    
    // Perform the operation
    if (MaterialManager.IsValid())
    {
        MaterialManager->BlendField(AffectedRegion, SourceMaterial, TargetMaterial, BlendFactor);
    }
    
    // Update both materials
    ProcessFieldUpdate(Position, Radius, SourceMaterial);
    ProcessFieldUpdate(Position, Radius, TargetMaterial);
    
    if (TransManager && TransactionId != 0)
    {
        TransManager->CommitTransaction(TransactionId);
    }
    
    // Network sync
    if (NetworkCoordinator.IsValid())
    {
        TArray<uint8> DeltaData = GenerateNetworkDelta(CurrentStateVersion - 1, CurrentStateVersion);
        NetworkCoordinator->SubmitRegionModification(AffectedRegion, TargetMaterial, DeltaData, CurrentStateVersion - 1);
    }
    
    // Event notification
    if (UEventBus* EventBus = UEventBus::Get())
    {
        FVolumeMaterialBlendedEventContext Context;
        Context.Volume = this;
        Context.Position = Position;
        Context.Radius = Radius;
        Context.SourceMaterialIndex = SourceMaterial;
        Context.TargetMaterialIndex = TargetMaterial;
        Context.BlendFactor = BlendFactor;
        
        EventBus->PublishEvent(EVolumeEvents::MaterialsBlended, &Context);
    }
}

void USVOHybridVolume::RegisterNetworkVersion(uint64 VersionId)
{
    if (NetworkCoordinator.IsValid())
    {
        // Update our version tracker
        CurrentStateVersion = VersionId;
        
        // Notify any components that need to track version
        if (MaterialManager.IsValid())
        {
            for (uint8 MatIndex = 0; MatIndex < 32; ++MatIndex) // Assuming 32 material channels
            {
                if (MaterialManager->IsChannelActive(MatIndex))
                {
                    MaterialManager->RegisterFieldVersion(MatIndex, VersionId);
                }
            }
        }
    }
}

bool USVOHybridVolume::ValidateFieldModification(const FVector& Position, float Radius, uint8 MaterialIndex) const
{
    // Use network coordinator to validate the operation
    if (NetworkCoordinator.IsValid() && MaterialManager.IsValid())
    {
        // Check if the material channel is active
        if (!MaterialManager->IsChannelActive(MaterialIndex))
        {
            UE_LOG(LogTemp, Warning, TEXT("Material channel %d is not active"), MaterialIndex);
            return false;
        }
        
        // Create a box representing the affected region
        FBox AffectedRegion(Position - FVector(Radius), Position + FVector(Radius));
        
        // Check if the region is locked
        if (NetworkCoordinator->IsRegionLocked(AffectedRegion))
        {
            UE_LOG(LogTemp, Warning, TEXT("Region is locked for modification"));
            return false;
        }
        
        // Additional validation using material manager
        if (!MaterialManager->ValidateFieldOperation(Position, Radius, MaterialIndex))
        {
            UE_LOG(LogTemp, Warning, TEXT("Field operation validation failed by material manager"));
            return false;
        }
        
        return true;
    }
    
    return false;
}

void USVOHybridVolume::ApplyNetworkDelta(const TArray<uint8>& DeltaData, uint64 BaseVersion, uint64 TargetVersion)
{
    if (!Serializer.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot apply network delta: Serializer is invalid"));
        return;
    }
    
    // Verify that our current version matches the base version
    if (CurrentStateVersion != BaseVersion)
    {
        UE_LOG(LogTemp, Warning, TEXT("Version mismatch during ApplyNetworkDelta. Current: %llu, Base: %llu"), 
               CurrentStateVersion, BaseVersion);
        return;
    }
    
    // Apply the delta
    FMemoryReader Reader(DeltaData);
    Serializer->DeserializeVolumeDelta(Reader, BaseVersion);
    
    // Update our version
    CurrentStateVersion = TargetVersion;
    
    // Notify event listeners
    if (UEventBus* EventBus = UEventBus::Get())
    {
        FVolumeNetworkSyncEventContext Context;
        Context.Volume = this;
        Context.BaseVersion = BaseVersion;
        Context.NewVersion = TargetVersion;
        Context.DeltaSize = DeltaData.Num();
        
        EventBus->PublishEvent(EVolumeEvents::NetworkSynchronized, &Context);
    }
}

TArray<uint8> USVOHybridVolume::GenerateNetworkDelta(uint64 BaseVersion, uint64 CurrentVersion) const
{
    if (!Serializer.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot generate network delta: Serializer is invalid"));
        return TArray<uint8>();
    }
    
    // Use the serializer to generate the delta
    TArray<uint8> DeltaData;
    FMemoryWriter Writer(DeltaData);
    Serializer->SerializeVolumeDelta(Writer, BaseVersion, CurrentVersion);
    
    return DeltaData;
}

void USVOHybridVolume::OptimizeMemoryUsage()
{
    // Optimize each component to reduce memory usage
    if (OctreeManager.IsValid())
    {
        OctreeManager->OptimizeMemoryUsage();
    }
    
    if (MaterialManager.IsValid())
    {
        MaterialManager->OptimizeMemoryUsage();
    }
    
    if (NarrowBandAllocator.IsValid())
    {
        NarrowBandAllocator->CompactMemory();
        NarrowBandAllocator->ReleaseUnusedMemory();
    }
}

void USVOHybridVolume::PrioritizeRegion(const FBox& Region, uint8 Priority)
{
    // Set the priority for a region to control memory allocation and detail level
    if (OctreeManager.IsValid())
    {
        OctreeManager->PrioritizeRegion(Region, Priority);
    }
    
    if (MaterialManager.IsValid())
    {
        MaterialManager->PrioritizeRegion(Region, Priority);
    }
    
    if (NarrowBandAllocator.IsValid())
    {
        NarrowBandAllocator->PrioritizeRegion(Region, Priority);
    }
}

FMemoryStats USVOHybridVolume::GetMemoryStats() const
{
    // Create a memory stat structure
    FMemoryStats Stats;
    
    // Fill in memory usage data from various components
    if (OctreeManager.IsValid())
    {
        FOctreeNodeManager::FOctreeStats OctreeStats = OctreeManager->GetStatistics();
        Stats.OctreeMemoryUsage = OctreeStats.TotalMemoryUsage;
        Stats.TotalNodeCount = OctreeStats.TotalNodes;
        Stats.MaxDepth = OctreeStats.MaxDepth;
    }
    
    if (MaterialManager.IsValid())
    {
        Stats.MaterialDataMemoryUsage = MaterialManager->GetTotalMemoryUsage();
        TMap<uint8, uint64> MaterialMemory = MaterialManager->GetMemoryByMaterial();
        Stats.MaterialMemoryByType = MaterialMemory;
    }
    
    // Calculate totals
    Stats.TotalMemoryUsage = Stats.OctreeMemoryUsage + Stats.MaterialDataMemoryUsage + 
                             Stats.NetworkBufferMemory + Stats.CacheMemoryUsage +
                             Stats.MetadataMemoryUsage + Stats.MiscMemoryUsage;
    
    return Stats;
}

void USVOHybridVolume::SerializeState(FArchive& Ar)
{
    if (Serializer.IsValid())
    {
        Serializer->SerializeVolume(Ar, FVolumeSerializer::ESerializationFormat::Full);
    }
}

void USVOHybridVolume::SerializeStateDelta(FArchive& Ar, uint64 BaseVersion)
{
    if (Serializer.IsValid())
    {
        Serializer->SerializeVolumeDelta(Ar, BaseVersion, CurrentStateVersion);
    }
}

bool USVOHybridVolume::ValidateOperation(const FVector& Position, float Radius, uint8 MaterialIndex) const
{
    // Check if the volume has been initialized
    if (!OctreeManager.IsValid() || !MaterialManager.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("Volume not fully initialized for operation validation"));
        return false;
    }
    
    // Check if the material index is valid
    if (MaterialIndex >= MaterialManager->GetChannelCount())
    {
        UE_LOG(LogTemp, Warning, TEXT("Invalid material index %d, max is %d"), 
               MaterialIndex, MaterialManager->GetChannelCount() - 1);
        return false;
    }
    
    // Check if the position is within volume bounds
    FBox WorldBounds = FBox(
        FVector(-OctreeManager->GetWorldBounds().GetExtent()),
        FVector(OctreeManager->GetWorldBounds().GetExtent())
    );
    
    if (!WorldBounds.IsInsideOrOn(Position))
    {
        UE_LOG(LogTemp, Warning, TEXT("Position (%f, %f, %f) is outside volume bounds"),
               Position.X, Position.Y, Position.Z);
        return false;
    }
    
    // Check network permissions via field modification validation
    return ValidateFieldModification(Position, Radius, MaterialIndex);
}

void USVOHybridVolume::ProcessFieldUpdate(const FVector& Position, float Radius, uint8 MaterialIndex)
{
    // Increment state version
    CurrentStateVersion++;
    
    // Calculate affected region
    FBox AffectedRegion(Position - FVector(Radius), Position + FVector(Radius));
    
    // Update octree structure if needed
    if (OctreeManager.IsValid())
    {
        // Find nodes in the affected region
        TArray<uint32> AffectedNodes = OctreeManager->FindNodesInSphere(Position, Radius);
        
        // Update node types based on material info
        for (uint32 NodeIndex : AffectedNodes)
        {
            FOctreeNodeManager::FOctreeNode* Node = OctreeManager->GetNode(NodeIndex);
            if (Node)
            {
                // If node is close to the material boundary, mark as interface
                if (Node->Type != FOctreeNodeManager::ENodeType::Interface)
                {
                    // Check if the node contains material boundaries
                    bool bHasBoundary = false;
                    
                    // Simple check: if distance to node center is close to radius
                    float DistToCenter = (Node->Position - Position).Size();
                    if (FMath::Abs(DistToCenter - Radius) < Node->Size * 0.5f)
                    {
                        bHasBoundary = true;
                    }
                    
                    if (bHasBoundary)
                    {
                        OctreeManager->UpdateNodeType(NodeIndex, FOctreeNodeManager::ENodeType::Interface);
                        
                        // Subdivide interface nodes for better precision
                        OctreeManager->SubdivideNode(NodeIndex);
                    }
                }
            }
        }
    }
    
    // Register the new version with components
    RegisterNetworkVersion(CurrentStateVersion);
}