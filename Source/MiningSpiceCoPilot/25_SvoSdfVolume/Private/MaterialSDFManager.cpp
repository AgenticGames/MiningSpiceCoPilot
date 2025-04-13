// MaterialSDFManager.cpp
// Implementation of multi-channel SDF management for material-specific distance fields

#include "25_SvoSdfVolume/Public/MaterialSDFManager.h"
#include "25_SvoSdfVolume/Public/NarrowBandAllocator.h"
#include "SVOHybridVolume.h"
#include "OctreeNodeManager.h"
#include "Math/Vector.h"
#include "Math/Box.h"
#include "Containers/Map.h"
#include "Containers/Array.h"
#include "Templates/Atomic.h"
#include "HAL/ThreadSafeCounter.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Containers/StaticArray.h"
#include "Math/UnrealMathSSE.h"

// Access to core systems
#include "Core/Registry/TypeRegistry.h"
#include "Core/Memory/MemoryManager.h"
#include "Core/Threading/TaskScheduler.h"
#include "Core/Events/EventBus.h"
#include "Core/Config/ConfigManager.h"
#include "Core/Services/ServiceLocator.h"
#include "Core/Factory/ComponentFactory.h"

FMaterialSDFManager::FMaterialSDFManager()
    : Volume(nullptr)
    , NodeManager(nullptr)
    , NarrowBandAllocator(nullptr)
    , MaterialChannelCount(8) // Default to 8 channels
    , TotalMemoryUsage(0)
    , CurrentVersionCounter(0)
{
    // Register with service locator
    IServiceLocator::Get().RegisterService<FMaterialSDFManager>(this);
    
    // Configure default memory limits from config
    IConfigManager& ConfigManager = IServiceLocator::Get().ResolveService<IConfigManager>();
    uint32 DefaultMaxFields = ConfigManager.GetValue<uint32>("SVO.Material.MaxFields", 10000);
    uint32 DefaultMaxMemoryMB = ConfigManager.GetValue<uint32>("SVO.Material.MaxMemoryMB", 256);
    
    // Reserve space for fields based on config
    Fields.Reserve(DefaultMaxFields);
}

FMaterialSDFManager::~FMaterialSDFManager()
{
    // Unregister from service locator
    IServiceLocator::Get().UnregisterService<FMaterialSDFManager>(this);
    
    // Release all fields
    for (auto& Field : Fields)
    {
        if (Field.State != EFieldState::Unallocated)
        {
            DeallocateFieldMemory(Field);
        }
    }
    
    // Log final statistics
    FMaterialStats Stats = GetMaterialStats();
    UE_LOG(LogSVO, Log, TEXT("MaterialSDFManager shutdown - Total fields: %u, Memory usage: %llu MB"),
           Stats.TotalFields, Stats.TotalMemoryUsage / (1024 * 1024));
}

void FMaterialSDFManager::Initialize(USVOHybridVolume* InVolume, FOctreeNodeManager* InNodeManager)
{
    Volume = InVolume;
    NodeManager = InNodeManager;
    
    // Register material types with the core registry
    FSDFTypeRegistry& SDFRegistry = FSDFTypeRegistry::Get();
    for (uint32 i = 0; i < MaterialChannelCount; ++i)
    {
        SDFRegistry.RegisterFieldType(i, FString::Printf(TEXT("Material%d"), i));
    }
    
    // Subscribe to relevant events
    UEventBus::Get()->SubscribeToEvent<FMaterialModifiedEvent>(
        [this](const FMaterialModifiedEvent& Event)
        {
            RegisterFieldModification(Event.FieldIndex, Event.MaterialIndex);
        });
    
    // Log initialization
    UE_LOG(LogSVO, Log, TEXT("MaterialSDFManager initialized with %u material channels"), MaterialChannelCount);
}

void FMaterialSDFManager::SetNarrowBandAllocator(FNarrowBandAllocator* InAllocator)
{
    NarrowBandAllocator = InAllocator;
}

void FMaterialSDFManager::SetMaterialChannelCount(uint32 MaterialCount)
{
    MaterialChannelCount = FMath::Min(MaterialCount, 256u); // Limit to 256 materials (uint8)
}

uint32 FMaterialSDFManager::CreateField(uint32 NodeIndex, const FFieldAllocationOptions& Options)
{
    // Check if field already exists for this node
    if (NodeToFieldMap.Contains(NodeIndex))
    {
        return NodeToFieldMap[NodeIndex];
    }
    
    // Create new field
    FFieldData NewField;
    NewField.NodeIndex = NodeIndex;
    NewField.Origin = NodeManager->GetNodeOrigin(NodeIndex);
    NewField.CellSize = Options.CellSize;
    NewField.Resolution = Options.Resolution;
    NewField.State = Options.bInitializeEmpty ? EFieldState::Empty : EFieldState::Homogeneous;
    NewField.PrimaryMaterial = Options.DefaultMaterial;
    NewField.bModified = true;
    NewField.VersionId = CurrentVersionCounter.IncrementExchange();
    
    // Allocate memory for the field
    AllocateFieldMemory(NewField, Options.Resolution);
    
    // Initialize field values
    if (!Options.bInitializeEmpty)
    {
        // Fill with default material
        for (int32 i = 0; i < NewField.DistanceValues.Num(); ++i)
        {
            // For default material, inside = negative distance
            NewField.DistanceValues[i] = (i % MaterialChannelCount == Options.DefaultMaterial) ? -1.0f : 1.0f;
        }
    }
    else
    {
        // Initialize as empty (all distances positive)
        for (int32 i = 0; i < NewField.DistanceValues.Num(); ++i)
        {
            NewField.DistanceValues[i] = 1.0f;
        }
    }
    
    // Add to collection
    uint32 FieldIndex = Fields.Add(NewField);
    NodeToFieldMap.Add(NodeIndex, FieldIndex);
    
    // Track memory
    IMemoryManager::Get().RegisterAllocation(
        EMemoryPurpose::TerrainVolume,
        NewField.DistanceValues.GetAllocatedSize(),
        FString::Printf(TEXT("SDFField_%u"), FieldIndex)
    );
    
    // Report field creation via event
    UEventBus::Get()->PublishEvent(FFieldCreatedEvent(NodeIndex, FieldIndex));
    
    return FieldIndex;
}

void FMaterialSDFManager::ReleaseField(uint32 FieldIndex)
{
    if (!Fields.IsValidIndex(FieldIndex))
    {
        UE_LOG(LogSVO, Warning, TEXT("Attempting to release invalid field index: %u"), FieldIndex);
        return;
    }
    
    FFieldData& Field = Fields[FieldIndex];
    
    // Update maps
    NodeToFieldMap.Remove(Field.NodeIndex);
    
    // Free memory
    DeallocateFieldMemory(Field);
    
    // Mark as unallocated
    Field.State = EFieldState::Unallocated;
    Field.NodeIndex = INDEX_NONE;
    
    // Report field release via event
    UEventBus::Get()->PublishEvent(FFieldReleasedEvent(FieldIndex));
}

void FMaterialSDFManager::UpdateFieldState(uint32 FieldIndex)
{
    if (!Fields.IsValidIndex(FieldIndex))
    {
        return;
    }
    
    FFieldData& Field = Fields[FieldIndex];
    
    // Count materials present
    TSet<uint8> FoundMaterials;
    bool bHasInterface = false;
    
    // Sample key points in the field to determine state
    int32 Resolution = Field.Resolution;
    int32 Step = FMath::Max(1, Resolution / 4);
    
    for (int32 z = 0; z < Resolution; z += Step)
    {
        for (int32 y = 0; y < Resolution; y += Step)
        {
            for (int32 x = 0; x < Resolution; x += Step)
            {
                int32 BaseIndex = GetFieldIndex(x, y, z, Resolution) * MaterialChannelCount;
                
                // Find material with minimum distance (inside material)
                uint8 MinMaterial = 0;
                float MinDist = Field.DistanceValues[BaseIndex];
                
                for (uint32 m = 1; m < MaterialChannelCount; ++m)
                {
                    float Dist = Field.DistanceValues[BaseIndex + m];
                    if (Dist < MinDist)
                    {
                        MinDist = Dist;
                        MinMaterial = m;
                    }
                }
                
                // If inside any material (negative distance), add to found materials
                if (MinDist < 0)
                {
                    FoundMaterials.Add(MinMaterial);
                }
                
                // Check adjacent points to detect interfaces
                if (x < Resolution - Step && y < Resolution - Step && z < Resolution - Step)
                {
                    int32 AdjacentIndex = GetFieldIndex(x + Step, y, z, Resolution) * MaterialChannelCount;
                    uint8 AdjacentMaterial = 0;
                    float AdjacentMinDist = Field.DistanceValues[AdjacentIndex];
                    
                    for (uint32 m = 1; m < MaterialChannelCount; ++m)
                    {
                        float Dist = Field.DistanceValues[AdjacentIndex + m];
                        if (Dist < AdjacentMinDist)
                        {
                            AdjacentMinDist = Dist;
                            AdjacentMaterial = m;
                        }
                    }
                    
                    if (MinMaterial != AdjacentMaterial && 
                        (MinDist < 0 || AdjacentMinDist < 0))
                    {
                        bHasInterface = true;
                        break;
                    }
                }
                
                if (bHasInterface)
                {
                    break;
                }
            }
            
            if (bHasInterface)
            {
                break;
            }
        }
        
        if (bHasInterface)
        {
            break;
        }
    }
    
    // Update field state
    EFieldState OldState = Field.State;
    if (FoundMaterials.Num() == 0)
    {
        Field.State = EFieldState::Empty;
    }
    else if (FoundMaterials.Num() == 1 && !bHasInterface)
    {
        Field.State = EFieldState::Homogeneous;
        Field.PrimaryMaterial = FoundMaterials.Array()[0];
    }
    else
    {
        Field.State = EFieldState::Interface;
    }
    
    // Report state change if needed
    if (OldState != Field.State)
    {
        Field.bModified = true;
        Field.VersionId = CurrentVersionCounter.IncrementExchange();
        
        UEventBus::Get()->PublishEvent(FFieldStateChangedEvent(
            FieldIndex, 
            Field.NodeIndex,
            OldState,
            Field.State
        ));
    }
}

void FMaterialSDFManager::SetFieldResolution(uint32 FieldIndex, uint32 NewResolution)
{
    if (!Fields.IsValidIndex(FieldIndex))
    {
        return;
    }
    
    FFieldData& Field = Fields[FieldIndex];
    if (Field.Resolution == NewResolution)
    {
        return;
    }
    
    // Store old values
    TArray<float> OldValues = MoveTemp(Field.DistanceValues);
    uint32 OldResolution = Field.Resolution;
    
    // Allocate new storage
    Field.Resolution = NewResolution;
    AllocateFieldMemory(Field, NewResolution);
    
    // Resample field (trilinear interpolation)
    float ScaleFactor = static_cast<float>(OldResolution - 1) / static_cast<float>(NewResolution - 1);
    
    // Use task system for parallel resampling
    ITaskScheduler& TaskScheduler = IServiceLocator::Get().ResolveService<ITaskScheduler>();
    
    TaskScheduler.ParallelFor(
        TEXT("ResampleSDF"),
        0, NewResolution,
        [&](int32 z)
        {
            for (int32 y = 0; y < NewResolution; ++y)
            {
                for (int32 x = 0; x < NewResolution; ++x)
                {
                    // Convert to old resolution space
                    float OldX = x * ScaleFactor;
                    float OldY = y * ScaleFactor;
                    float OldZ = z * ScaleFactor;
                    
                    // Get surrounding indices
                    int32 X0 = FMath::FloorToInt(OldX);
                    int32 Y0 = FMath::FloorToInt(OldY);
                    int32 Z0 = FMath::FloorToInt(OldZ);
                    
                    int32 X1 = FMath::Min(X0 + 1, OldResolution - 1);
                    int32 Y1 = FMath::Min(Y0 + 1, OldResolution - 1);
                    int32 Z1 = FMath::Min(Z0 + 1, OldResolution - 1);
                    
                    // Calculate interpolation factors
                    float FracX = OldX - X0;
                    float FracY = OldY - Y0;
                    float FracZ = OldZ - Z0;
                    
                    // For each material channel
                    for (uint32 m = 0; m < MaterialChannelCount; ++m)
                    {
                        // Get sample values at corners
                        float V000 = OldValues[GetFieldIndex(X0, Y0, Z0, OldResolution) * MaterialChannelCount + m];
                        float V001 = OldValues[GetFieldIndex(X0, Y0, Z1, OldResolution) * MaterialChannelCount + m];
                        float V010 = OldValues[GetFieldIndex(X0, Y1, Z0, OldResolution) * MaterialChannelCount + m];
                        float V011 = OldValues[GetFieldIndex(X0, Y1, Z1, OldResolution) * MaterialChannelCount + m];
                        float V100 = OldValues[GetFieldIndex(X1, Y0, Z0, OldResolution) * MaterialChannelCount + m];
                        float V101 = OldValues[GetFieldIndex(X1, Y0, Z1, OldResolution) * MaterialChannelCount + m];
                        float V110 = OldValues[GetFieldIndex(X1, Y1, Z0, OldResolution) * MaterialChannelCount + m];
                        float V111 = OldValues[GetFieldIndex(X1, Y1, Z1, OldResolution) * MaterialChannelCount + m];
                        
                        // Trilinear interpolation
                        float V00 = FMath::Lerp(V000, V100, FracX);
                        float V01 = FMath::Lerp(V001, V101, FracX);
                        float V10 = FMath::Lerp(V010, V110, FracX);
                        float V11 = FMath::Lerp(V011, V111, FracX);
                        
                        float V0 = FMath::Lerp(V00, V10, FracY);
                        float V1 = FMath::Lerp(V01, V11, FracY);
                        
                        float V = FMath::Lerp(V0, V1, FracZ);
                        
                        // Store resampled value
                        Field.DistanceValues[GetFieldIndex(x, y, z, NewResolution) * MaterialChannelCount + m] = V;
                    }
                }
            }
        }
    );
    
    // Mark as modified
    Field.bModified = true;
    Field.VersionId = CurrentVersionCounter.IncrementExchange();
    
    // Report resolution change via event
    UEventBus::Get()->PublishEvent(FFieldResolutionChangedEvent(
        FieldIndex, 
        Field.NodeIndex,
        OldResolution,
        NewResolution
    ));
}

FMaterialSDFManager::EFieldState FMaterialSDFManager::GetFieldState(uint32 FieldIndex) const
{
    if (!Fields.IsValidIndex(FieldIndex))
    {
        return EFieldState::Unallocated;
    }
    
    return Fields[FieldIndex].State;
}

FMaterialSDFManager::FFieldData* FMaterialSDFManager::GetFieldData(uint32 FieldIndex)
{
    if (!Fields.IsValidIndex(FieldIndex))
    {
        return nullptr;
    }
    
    return &Fields[FieldIndex];
}

const FMaterialSDFManager::FFieldData* FMaterialSDFManager::GetFieldData(uint32 FieldIndex) const
{
    if (!Fields.IsValidIndex(FieldIndex))
    {
        return nullptr;
    }
    
    return &Fields[FieldIndex];
}

uint32 FMaterialSDFManager::GetFieldIndexForNode(uint32 NodeIndex) const
{
    const uint32* FoundIndex = NodeToFieldMap.Find(NodeIndex);
    if (FoundIndex)
    {
        return *FoundIndex;
    }
    
    return INDEX_NONE;
}

float FMaterialSDFManager::GetDistanceValue(uint32 FieldIndex, const FVector& LocalPosition, uint8 MaterialIndex) const
{
    if (!Fields.IsValidIndex(FieldIndex) || MaterialIndex >= MaterialChannelCount)
    {
        return 1.0f; // Outside by default
    }
    
    const FFieldData& Field = Fields[FieldIndex];
    if (Field.State == EFieldState::Unallocated)
    {
        return 1.0f;
    }
    
    // For homogeneous fields, check if we're in the primary material
    if (Field.State == EFieldState::Homogeneous)
    {
        return (MaterialIndex == Field.PrimaryMaterial) ? -1.0f : 1.0f;
    }
    
    // For empty fields, always outside
    if (Field.State == EFieldState::Empty)
    {
        return 1.0f;
    }
    
    // Get cell indices
    int32 X, Y, Z;
    GetCellIndicesFromPosition(LocalPosition, Field.CellSize, Field.Resolution, X, Y, Z);
    
    // Clamp to field bounds
    X = FMath::Clamp(X, 0, (int32)Field.Resolution - 1);
    Y = FMath::Clamp(Y, 0, (int32)Field.Resolution - 1);
    Z = FMath::Clamp(Z, 0, (int32)Field.Resolution - 1);
    
    // Calculate index
    int32 Index = GetFieldIndex(X, Y, Z, Field.Resolution) * MaterialChannelCount + MaterialIndex;
    return Field.DistanceValues[Index];
}

void FMaterialSDFManager::SetDistanceValue(uint32 FieldIndex, const FVector& LocalPosition, uint8 MaterialIndex, float Value)
{
    if (!Fields.IsValidIndex(FieldIndex) || MaterialIndex >= MaterialChannelCount)
    {
        return;
    }
    
    FFieldData& Field = Fields[FieldIndex];
    if (Field.State == EFieldState::Unallocated)
    {
        return;
    }
    
    // Get cell indices
    int32 X, Y, Z;
    GetCellIndicesFromPosition(LocalPosition, Field.CellSize, Field.Resolution, X, Y, Z);
    
    // Clamp to field bounds
    X = FMath::Clamp(X, 0, (int32)Field.Resolution - 1);
    Y = FMath::Clamp(Y, 0, (int32)Field.Resolution - 1);
    Z = FMath::Clamp(Z, 0, (int32)Field.Resolution - 1);
    
    // Calculate index
    int32 Index = GetFieldIndex(X, Y, Z, Field.Resolution) * MaterialChannelCount + MaterialIndex;
    
    // Apply the new value
    if (Field.DistanceValues[Index] != Value)
    {
        Field.DistanceValues[Index] = Value;
        Field.bModified = true;
    }
    
    // Start transaction for field modification
    ITransactionManager& TransactionMgr = IServiceLocator::Get().ResolveService<ITransactionManager>();
    uint64 TransId = TransactionMgr.BeginTransaction(ETransactionConcurrency::Optimistic);
    
    // Perform the modification
    Field.DistanceValues[Index] = Value;
    Field.bModified = true;
    
    // Register the modification with proper version
    RegisterFieldModification(FieldIndex, MaterialIndex);
    
    // Commit transaction
    TransactionMgr.CommitTransaction(TransId);
}

void FMaterialSDFManager::ApplyMaterialSphere(uint32 FieldIndex, const FVector& Center, float Radius, uint8 MaterialIndex, float Value)
{
    if (!Fields.IsValidIndex(FieldIndex) || MaterialIndex >= MaterialChannelCount)
    {
        return;
    }
    
    FFieldData& Field = Fields[FieldIndex];
    if (Field.State == EFieldState::Unallocated)
    {
        return;
    }
    
    // Start transaction for field modification
    ITransactionManager& TransactionMgr = IServiceLocator::Get().ResolveService<ITransactionManager>();
    uint64 TransId = TransactionMgr.BeginTransaction(ETransactionConcurrency::Optimistic);
    
    // Calculate local center
    FVector LocalCenter = Center - Field.Origin;
    
    // Determine affected region in field space
    FVector MinCorner = (LocalCenter - FVector(Radius)) / Field.CellSize;
    FVector MaxCorner = (LocalCenter + FVector(Radius)) / Field.CellSize;
    
    int32 MinX = FMath::Max(0, FMath::FloorToInt(MinCorner.X));
    int32 MinY = FMath::Max(0, FMath::FloorToInt(MinCorner.Y));
    int32 MinZ = FMath::Max(0, FMath::FloorToInt(MinCorner.Z));
    
    int32 MaxX = FMath::Min((int32)Field.Resolution - 1, FMath::CeilToInt(MaxCorner.X));
    int32 MaxY = FMath::Min((int32)Field.Resolution - 1, FMath::CeilToInt(MaxCorner.Y));
    int32 MaxZ = FMath::Min((int32)Field.Resolution - 1, FMath::CeilToInt(MaxCorner.Z));
    
    // Use task system for parallel processing
    ITaskScheduler& TaskScheduler = IServiceLocator::Get().ResolveService<ITaskScheduler>();
    
    TaskScheduler.ParallelFor(
        TEXT("ApplyMaterialSphere"),
        MinZ, MaxZ + 1,
        [&](int32 Z)
        {
            for (int32 Y = MinY; Y <= MaxY; ++Y)
            {
                for (int32 X = MinX; X <= MaxX; ++X)
                {
                    // Calculate position in world space
                    FVector LocalPos = GetLocalPositionFromIndices(X, Y, Z, Field.CellSize);
                    float Distance = (LocalPos - LocalCenter).Size();
                    
                    // Calculate signed distance to sphere
                    float SignedDistance = Distance - Radius;
                    
                    // Apply the material value if inside the sphere's influence
                    if (SignedDistance <= 0)
                    {
                        int32 Index = GetFieldIndex(X, Y, Z, Field.Resolution) * MaterialChannelCount + MaterialIndex;
                        Field.DistanceValues[Index] = Value;
                        Field.bModified = true;
                    }
                }
            }
        }
    );
    
    // Mark field as modified
    RegisterFieldModification(FieldIndex, MaterialIndex);
    
    // Update field state
    UpdateFieldState(FieldIndex);
    
    // Commit transaction
    TransactionMgr.CommitTransaction(TransId);
    
    // Create affected region for propagation
    FBox AffectedRegion(
        Field.Origin + FVector(MinX, MinY, MinZ) * Field.CellSize,
        Field.Origin + FVector(MaxX, MaxY, MaxZ) * Field.CellSize
    );
    
    // Propagate changes to adjacent fields
    PropagateChanges(FieldIndex, AffectedRegion);
}

void FMaterialSDFManager::UnionMaterial(uint32 FieldIndex, const FVector& Center, float Radius, uint8 MaterialIndex, float Strength)
{
    if (!Fields.IsValidIndex(FieldIndex) || MaterialIndex >= MaterialChannelCount)
    {
        return;
    }
    
    FFieldData& Field = Fields[FieldIndex];
    if (Field.State == EFieldState::Unallocated)
    {
        return;
    }
    
    // Start transaction
    ITransactionManager& TransactionMgr = IServiceLocator::Get().ResolveService<ITransactionManager>();
    uint64 TransId = TransactionMgr.BeginTransaction(ETransactionConcurrency::Optimistic);
    
    // Calculate local center
    FVector LocalCenter = Center - Field.Origin;
    
    // Determine affected region
    FVector MinCorner = (LocalCenter - FVector(Radius)) / Field.CellSize;
    FVector MaxCorner = (LocalCenter + FVector(Radius)) / Field.CellSize;
    
    int32 MinX = FMath::Max(0, FMath::FloorToInt(MinCorner.X));
    int32 MinY = FMath::Max(0, FMath::FloorToInt(MinCorner.Y));
    int32 MinZ = FMath::Max(0, FMath::FloorToInt(MinCorner.Z));
    
    int32 MaxX = FMath::Min((int32)Field.Resolution - 1, FMath::CeilToInt(MaxCorner.X));
    int32 MaxY = FMath::Min((int32)Field.Resolution - 1, FMath::CeilToInt(MaxCorner.Y));
    int32 MaxZ = FMath::Min((int32)Field.Resolution - 1, FMath::CeilToInt(MaxCorner.Z));
    
    // Use task system for parallel processing
    ITaskScheduler& TaskScheduler = IServiceLocator::Get().ResolveService<ITaskScheduler>();
    
    TaskScheduler.ParallelFor(
        TEXT("UnionMaterial"),
        MinZ, MaxZ + 1,
        [&](int32 Z)
        {
            for (int32 Y = MinY; Y <= MaxY; ++Y)
            {
                for (int32 X = MinX; X <= MaxX; ++X)
                {
                    // Calculate position in world space
                    FVector LocalPos = GetLocalPositionFromIndices(X, Y, Z, Field.CellSize);
                    float Distance = (LocalPos - LocalCenter).Size();
                    
                    // Calculate signed distance to sphere
                    float SignedDistance = (Distance - Radius) * Strength;
                    
                    // Apply union operation (min)
                    int32 Index = GetFieldIndex(X, Y, Z, Field.Resolution) * MaterialChannelCount + MaterialIndex;
                    Field.DistanceValues[Index] = FMath::Min(Field.DistanceValues[Index], SignedDistance);
                    Field.bModified = true;
                }
            }
        }
    );
    
    // Mark field as modified
    RegisterFieldModification(FieldIndex, MaterialIndex);
    
    // Update field state
    UpdateFieldState(FieldIndex);
    
    // Commit transaction
    TransactionMgr.CommitTransaction(TransId);
    
    // Create affected region for propagation
    FBox AffectedRegion(
        Field.Origin + FVector(MinX, MinY, MinZ) * Field.CellSize,
        Field.Origin + FVector(MaxX, MaxY, MaxZ) * Field.CellSize
    );
    
    // Propagate changes to adjacent fields
    PropagateChanges(FieldIndex, AffectedRegion);
}

void FMaterialSDFManager::SubtractMaterial(uint32 FieldIndex, const FVector& Center, float Radius, uint8 MaterialIndex, float Strength)
{
    if (!Fields.IsValidIndex(FieldIndex) || MaterialIndex >= MaterialChannelCount)
    {
        return;
    }
    
    FFieldData& Field = Fields[FieldIndex];
    if (Field.State == EFieldState::Unallocated)
    {
        return;
    }
    
    // Start transaction
    ITransactionManager& TransactionMgr = IServiceLocator::Get().ResolveService<ITransactionManager>();
    uint64 TransId = TransactionMgr.BeginTransaction(ETransactionConcurrency::Optimistic);
    
    // Calculate local center
    FVector LocalCenter = Center - Field.Origin;
    
    // Determine affected region
    FVector MinCorner = (LocalCenter - FVector(Radius)) / Field.CellSize;
    FVector MaxCorner = (LocalCenter + FVector(Radius)) / Field.CellSize;
    
    int32 MinX = FMath::Max(0, FMath::FloorToInt(MinCorner.X));
    int32 MinY = FMath::Max(0, FMath::FloorToInt(MinCorner.Y));
    int32 MinZ = FMath::Max(0, FMath::FloorToInt(MinCorner.Z));
    
    int32 MaxX = FMath::Min((int32)Field.Resolution - 1, FMath::CeilToInt(MaxCorner.X));
    int32 MaxY = FMath::Min((int32)Field.Resolution - 1, FMath::CeilToInt(MaxCorner.Y));
    int32 MaxZ = FMath::Min((int32)Field.Resolution - 1, FMath::CeilToInt(MaxCorner.Z));
    
    // Use task system for parallel processing
    ITaskScheduler& TaskScheduler = IServiceLocator::Get().ResolveService<ITaskScheduler>();
    
    TaskScheduler.ParallelFor(
        TEXT("SubtractMaterial"),
        MinZ, MaxZ + 1,
        [&](int32 Z)
        {
            for (int32 Y = MinY; Y <= MaxY; ++Y)
            {
                for (int32 X = MinX; X <= MaxX; ++X)
                {
                    // Calculate position in world space
                    FVector LocalPos = GetLocalPositionFromIndices(X, Y, Z, Field.CellSize);
                    float Distance = (LocalPos - LocalCenter).Size();
                    
                    // Calculate signed distance to sphere (negative for inside sphere)
                    float SignedDistance = (Radius - Distance) * Strength;
                    
                    // Apply subtraction operation (max)
                    int32 Index = GetFieldIndex(X, Y, Z, Field.Resolution) * MaterialChannelCount + MaterialIndex;
                    Field.DistanceValues[Index] = FMath::Max(Field.DistanceValues[Index], SignedDistance);
                    Field.bModified = true;
                }
            }
        }
    );
    
    // Mark field as modified
    RegisterFieldModification(FieldIndex, MaterialIndex);
    
    // Update field state
    UpdateFieldState(FieldIndex);
    
    // Commit transaction
    TransactionMgr.CommitTransaction(TransId);
    
    // Create affected region for propagation
    FBox AffectedRegion(
        Field.Origin + FVector(MinX, MinY, MinZ) * Field.CellSize,
        Field.Origin + FVector(MaxX, MaxY, MaxZ) * Field.CellSize
    );
    
    // Propagate changes to adjacent fields
    PropagateChanges(FieldIndex, AffectedRegion);
}

void FMaterialSDFManager::BlendMaterials(uint32 FieldIndex, const FVector& Center, float Radius, uint8 SourceMaterial, uint8 TargetMaterial, float BlendFactor)
{
    if (!Fields.IsValidIndex(FieldIndex) || 
        SourceMaterial >= MaterialChannelCount ||
        TargetMaterial >= MaterialChannelCount)
    {
        return;
    }
    
    FFieldData& Field = Fields[FieldIndex];
    if (Field.State == EFieldState::Unallocated)
    {
        return;
    }
    
    // Start transaction
    ITransactionManager& TransactionMgr = IServiceLocator::Get().ResolveService<ITransactionManager>();
    uint64 TransId = TransactionMgr.BeginTransaction(ETransactionConcurrency::Optimistic);
    
    // Calculate local center
    FVector LocalCenter = Center - Field.Origin;
    
    // Determine affected region
    FVector MinCorner = (LocalCenter - FVector(Radius)) / Field.CellSize;
    FVector MaxCorner = (LocalCenter + FVector(Radius)) / Field.CellSize;
    
    int32 MinX = FMath::Max(0, FMath::FloorToInt(MinCorner.X));
    int32 MinY = FMath::Max(0, FMath::FloorToInt(MinCorner.Y));
    int32 MinZ = FMath::Max(0, FMath::FloorToInt(MinCorner.Z));
    
    int32 MaxX = FMath::Min((int32)Field.Resolution - 1, FMath::CeilToInt(MaxCorner.X));
    int32 MaxY = FMath::Min((int32)Field.Resolution - 1, FMath::CeilToInt(MaxCorner.Y));
    int32 MaxZ = FMath::Min((int32)Field.Resolution - 1, FMath::CeilToInt(MaxCorner.Z));
    
    // Use task system for parallel processing
    ITaskScheduler& TaskScheduler = IServiceLocator::Get().ResolveService<ITaskScheduler>();
    
    TaskScheduler.ParallelFor(
        TEXT("BlendMaterials"),
        MinZ, MaxZ + 1,
        [&](int32 Z)
        {
            for (int32 Y = MinY; Y <= MaxY; ++Y)
            {
                for (int32 X = MinX; X <= MaxX; ++X)
                {
                    // Calculate position in world space
                    FVector LocalPos = GetLocalPositionFromIndices(X, Y, Z, Field.CellSize);
                    float Distance = (LocalPos - LocalCenter).Size();
                    
                    // Calculate blend weight based on distance (1.0 at center, 0.0 at radius)
                    float BlendWeight = FMath::Max(0.0f, 1.0f - Distance / Radius) * BlendFactor;
                    
                    if (BlendWeight > 0.0f)
                    {
                        int32 BaseIndex = GetFieldIndex(X, Y, Z, Field.Resolution) * MaterialChannelCount;
                        
                        // Get current values
                        float SourceValue = Field.DistanceValues[BaseIndex + SourceMaterial];
                        float TargetValue = Field.DistanceValues[BaseIndex + TargetMaterial];
                        
                        // Blend values
                        float BlendedSource = FMath::Lerp(SourceValue, TargetValue, BlendWeight);
                        float BlendedTarget = FMath::Lerp(TargetValue, SourceValue, BlendWeight);
                        
                        // Apply blended values
                        Field.DistanceValues[BaseIndex + SourceMaterial] = BlendedSource;
                        Field.DistanceValues[BaseIndex + TargetMaterial] = BlendedTarget;
                        Field.bModified = true;
                    }
                }
            }
        }
    );
    
    // Mark fields as modified
    RegisterFieldModification(FieldIndex, SourceMaterial);
    RegisterFieldModification(FieldIndex, TargetMaterial);
    
    // Update field state
    UpdateFieldState(FieldIndex);
    
    // Commit transaction
    TransactionMgr.CommitTransaction(TransId);
    
    // Create affected region for propagation
    FBox AffectedRegion(
        Field.Origin + FVector(MinX, MinY, MinZ) * Field.CellSize,
        Field.Origin + FVector(MaxX, MaxY, MaxZ) * Field.CellSize
    );
    
    // Propagate changes to adjacent fields
    PropagateChanges(FieldIndex, AffectedRegion);
}

void FMaterialSDFManager::ClearMaterial(uint32 FieldIndex, uint8 MaterialIndex)
{
    if (!Fields.IsValidIndex(FieldIndex) || MaterialIndex >= MaterialChannelCount)
    {
        return;
    }
    
    FFieldData& Field = Fields[FieldIndex];
    if (Field.State == EFieldState::Unallocated)
    {
        return;
    }
    
    // Start transaction
    ITransactionManager& TransactionMgr = IServiceLocator::Get().ResolveService<ITransactionManager>();
    uint64 TransId = TransactionMgr.BeginTransaction(ETransactionConcurrency::Optimistic);
    
    // Use task system for parallel processing
    ITaskScheduler& TaskScheduler = IServiceLocator::Get().ResolveService<ITaskScheduler>();
    
    TaskScheduler.ParallelFor(
        TEXT("ClearMaterial"),
        0, Field.Resolution,
        [&](int32 Z)
        {
            for (int32 Y = 0; Y < Field.Resolution; ++Y)
            {
                for (int32 X = 0; X < Field.Resolution; ++X)
                {
                    int32 Index = GetFieldIndex(X, Y, Z, Field.Resolution) * MaterialChannelCount + MaterialIndex;
                    Field.DistanceValues[Index] = 1.0f; // Outside value
                }
            }
        }
    );
    
    // Mark field as modified
    RegisterFieldModification(FieldIndex, MaterialIndex);
    
    // Update field state
    UpdateFieldState(FieldIndex);
    
    // Commit transaction
    TransactionMgr.CommitTransaction(TransId);
}

void FMaterialSDFManager::FillWithMaterial(uint32 FieldIndex, uint8 MaterialIndex)
{
    if (!Fields.IsValidIndex(FieldIndex) || MaterialIndex >= MaterialChannelCount)
    {
        return;
    }
    
    FFieldData& Field = Fields[FieldIndex];
    if (Field.State == EFieldState::Unallocated)
    {
        return;
    }
    
    // Start transaction
    ITransactionManager& TransactionMgr = IServiceLocator::Get().ResolveService<ITransactionManager>();
    uint64 TransId = TransactionMgr.BeginTransaction(ETransactionConcurrency::Optimistic);
    
    // Use task system for parallel processing
    ITaskScheduler& TaskScheduler = IServiceLocator::Get().ResolveService<ITaskScheduler>();
    
    TaskScheduler.ParallelFor(
        TEXT("FillWithMaterial"),
        0, Field.Resolution,
        [&](int32 Z)
        {
            for (int32 Y = 0; Y < Field.Resolution; ++Y)
            {
                for (int32 X = 0; X < Field.Resolution; ++X)
                {
                    int32 BaseIndex = GetFieldIndex(X, Y, Z, Field.Resolution) * MaterialChannelCount;
                    
                    // Set all materials to outside except target material
                    for (uint32 m = 0; m < MaterialChannelCount; ++m)
                    {
                        if (m == MaterialIndex)
                        {
                            Field.DistanceValues[BaseIndex + m] = -1.0f; // Inside
                        }
                        else
                        {
                            Field.DistanceValues[BaseIndex + m] = 1.0f; // Outside
                        }
                    }
                }
            }
        }
    );
    
    // Mark all materials as modified since we changed all channels
    for (uint32 m = 0; m < MaterialChannelCount; ++m)
    {
        RegisterFieldModification(FieldIndex, m);
    }
    
    // Set field state directly
    Field.State = EFieldState::Homogeneous;
    Field.PrimaryMaterial = MaterialIndex;
    
    // Commit transaction
    TransactionMgr.CommitTransaction(TransId);
    
    // Propagate changes to adjacent fields
    FBox AffectedRegion(Field.Origin, Field.Origin + FVector(Field.Resolution * Field.CellSize));
    PropagateChanges(FieldIndex, AffectedRegion);
}

float FMaterialSDFManager::EvaluateFieldAtPosition(const FVector& WorldPosition, uint8 MaterialIndex) const
{
    if (MaterialIndex >= MaterialChannelCount)
    {
        return 1.0f;
    }
    
    // Find the node containing this position
    uint32 NodeIndex = NodeManager->FindNodeContainingPoint(WorldPosition);
    if (NodeIndex == INDEX_NONE)
    {
        return 1.0f;
    }
    
    // Get field for the node
    uint32 FieldIndex = GetFieldIndexForNode(NodeIndex);
    if (FieldIndex == INDEX_NONE)
    {
        return 1.0f;
    }
    
    const FFieldData& Field = Fields[FieldIndex];
    
    // Handle non-interface fields quickly
    if (Field.State == EFieldState::Empty)
    {
        return 1.0f;
    }
    else if (Field.State == EFieldState::Homogeneous)
    {
        return (MaterialIndex == Field.PrimaryMaterial) ? -1.0f : 1.0f;
    }
    
    // Calculate local position
    FVector LocalPos = WorldPosition - Field.Origin;
    
    // Use trilinear interpolation for smooth values
    return TrilinearInterpolation(FieldIndex, LocalPos, MaterialIndex);
}

TArray<float> FMaterialSDFManager::EvaluateMultiChannelFieldAtPosition(const FVector& WorldPosition) const
{
    TArray<float> Results;
    Results.AddZeroed(MaterialChannelCount);
    
    // Find the node containing this position
    uint32 NodeIndex = NodeManager->FindNodeContainingPoint(WorldPosition);
    if (NodeIndex == INDEX_NONE)
    {
        // Outside all materials
        for (uint32 i = 0; i < MaterialChannelCount; ++i)
        {
            Results[i] = 1.0f;
        }
        return Results;
    }
    
    // Get field for the node
    uint32 FieldIndex = GetFieldIndexForNode(NodeIndex);
    if (FieldIndex == INDEX_NONE)
    {
        // Outside all materials
        for (uint32 i = 0; i < MaterialChannelCount; ++i)
        {
            Results[i] = 1.0f;
        }
        return Results;
    }
    
    const FFieldData& Field = Fields[FieldIndex];
    
    // Handle non-interface fields quickly
    if (Field.State == EFieldState::Empty)
    {
        // Outside all materials
        for (uint32 i = 0; i < MaterialChannelCount; ++i)
        {
            Results[i] = 1.0f;
        }
    }
    else if (Field.State == EFieldState::Homogeneous)
    {
        // Inside primary material, outside others
        for (uint32 i = 0; i < MaterialChannelCount; ++i)
        {
            Results[i] = (i == Field.PrimaryMaterial) ? -1.0f : 1.0f;
        }
    }
    else
    {
        // Interface field, evaluate for each material
        FVector LocalPos = WorldPosition - Field.Origin;
        
        for (uint32 i = 0; i < MaterialChannelCount; ++i)
        {
            Results[i] = TrilinearInterpolation(FieldIndex, LocalPos, i);
        }
    }
    
    return Results;
}

FVector FMaterialSDFManager::EvaluateGradientAtPosition(const FVector& WorldPosition, uint8 MaterialIndex) const
{
    if (MaterialIndex >= MaterialChannelCount)
    {
        return FVector::ZeroVector;
    }
    
    // Find the node containing this position
    uint32 NodeIndex = NodeManager->FindNodeContainingPoint(WorldPosition);
    if (NodeIndex == INDEX_NONE)
    {
        return FVector::ZeroVector;
    }
    
    // Get field for the node
    uint32 FieldIndex = GetFieldIndexForNode(NodeIndex);
    if (FieldIndex == INDEX_NONE)
    {
        return FVector::ZeroVector;
    }
    
    // Calculate local position
    const FFieldData& Field = Fields[FieldIndex];
    FVector LocalPos = WorldPosition - Field.Origin;
    
    // Calculate gradient
    return CalculateGradient(FieldIndex, LocalPos, MaterialIndex);
}

bool FMaterialSDFManager::IsPositionInside(const FVector& WorldPosition, uint8 MaterialIndex) const
{
    float Distance = EvaluateFieldAtPosition(WorldPosition, MaterialIndex);
    return Distance < 0.0f;
}

void FMaterialSDFManager::EvaluateFieldBatch(const TArray<FVector>& Positions, uint8 MaterialIndex, TArray<float>& OutValues) const
{
    OutValues.SetNumZeroed(Positions.Num());
    
    if (MaterialIndex >= MaterialChannelCount)
    {
        // Fill with outside values
        for (int32 i = 0; i < Positions.Num(); ++i)
        {
            OutValues[i] = 1.0f;
        }
        return;
    }
    
    // Use task system for parallel processing
    ITaskScheduler& TaskScheduler = IServiceLocator::Get().ResolveService<ITaskScheduler>();
    
    TaskScheduler.ParallelFor(
        TEXT("EvaluateFieldBatch"),
        0, Positions.Num(),
        [&](int32 i)
        {
            OutValues[i] = EvaluateFieldAtPosition(Positions[i], MaterialIndex);
        }
    );
}

void FMaterialSDFManager::ApplyFieldOperation(const TArray<uint32>& FieldIndices, TFunction<void(uint32, FFieldData*)> OperationFunc)
{
    // Start batch transaction
    ITransactionManager& TransactionMgr = IServiceLocator::Get().ResolveService<ITransactionManager>();
    uint64 TransId = TransactionMgr.BeginTransaction(ETransactionConcurrency::Optimistic);
    
    // Process each field
    for (uint32 FieldIndex : FieldIndices)
    {
        if (Fields.IsValidIndex(FieldIndex))
        {
            FFieldData* Field = &Fields[FieldIndex];
            
            // Apply the operation
            OperationFunc(FieldIndex, Field);
            
            // Update field state
            UpdateFieldState(FieldIndex);
        }
    }
    
    // Commit transaction
    TransactionMgr.CommitTransaction(TransId);
}

void FMaterialSDFManager::PropagateFields(const TArray<uint32>& SourceFields, const TArray<uint32>& TargetFields)
{
    // Start batch transaction
    ITransactionManager& TransactionMgr = IServiceLocator::Get().ResolveService<ITransactionManager>();
    uint64 TransId = TransactionMgr.BeginTransaction(ETransactionConcurrency::Optimistic);
    
    // Process each pair
    for (uint32 SrcIdx = 0; SrcIdx < SourceFields.Num() && SrcIdx < TargetFields.Num(); ++SrcIdx)
    {
        uint32 SourceFieldIdx = SourceFields[SrcIdx];
        uint32 TargetFieldIdx = TargetFields[SrcIdx];
        
        if (!Fields.IsValidIndex(SourceFieldIdx) || !Fields.IsValidIndex(TargetFieldIdx))
        {
            continue;
        }
        
        const FFieldData& SourceField = Fields[SourceFieldIdx];
        FFieldData& TargetField = Fields[TargetFieldIdx];
        
        // Calculate offset between fields
        FVector Offset = TargetField.Origin - SourceField.Origin;
        
        // Calculate scale factor between fields
        float ScaleFactor = TargetField.CellSize / SourceField.CellSize;
        
        // Use task system for parallel processing
        ITaskScheduler& TaskScheduler = IServiceLocator::Get().ResolveService<ITaskScheduler>();
        
        TaskScheduler.ParallelFor(
            TEXT("PropagateFields"),
            0, TargetField.Resolution,
            [&](int32 Z)
            {
                for (int32 Y = 0; Y < TargetField.Resolution; ++Y)
                {
                    for (int32 X = 0; X < TargetField.Resolution; ++X)
                    {
                        // Calculate target position in world space
                        FVector TargetLocalPos = GetLocalPositionFromIndices(X, Y, Z, TargetField.CellSize);
                        
                        // Calculate corresponding position in source field
                        FVector SourceLocalPos = (TargetLocalPos + Offset) / ScaleFactor;
                        
                        // Get source field indices
                        int32 SrcX, SrcY, SrcZ;
                        GetCellIndicesFromPosition(SourceLocalPos, SourceField.CellSize, SourceField.Resolution, SrcX, SrcY, SrcZ);
                        
                        // Check if within bounds of source field
                        if (SrcX >= 0 && SrcX < SourceField.Resolution &&
                            SrcY >= 0 && SrcY < SourceField.Resolution &&
                            SrcZ >= 0 && SrcZ < SourceField.Resolution)
                        {
                            // Copy value for each material
                            int32 TargetIdx = GetFieldIndex(X, Y, Z, TargetField.Resolution) * MaterialChannelCount;
                            int32 SourceIdx = GetFieldIndex(SrcX, SrcY, SrcZ, SourceField.Resolution) * MaterialChannelCount;
                            
                            for (uint32 m = 0; m < MaterialChannelCount; ++m)
                            {
                                TargetField.DistanceValues[TargetIdx + m] = SourceField.DistanceValues[SourceIdx + m];
                            }
                        }
                    }
                }
            }
        );
        
        // Mark target field as modified
        TargetField.bModified = true;
        TargetField.VersionId = CurrentVersionCounter.IncrementExchange();
        
        // Register modification for all materials
        for (uint32 m = 0; m < MaterialChannelCount; ++m)
        {
            RegisterFieldModification(TargetFieldIdx, m);
        }
        
        // Update target field state
        UpdateFieldState(TargetFieldIdx);
    }
    
    // Commit transaction
    TransactionMgr.CommitTransaction(TransId);
}

void FMaterialSDFManager::OptimizeMemoryUsage()
{
    // First compact fields
    CompactFields();
    
    // Then release unused fields
    ReleaseUnusedFields();
}

void FMaterialSDFManager::PrioritizeRegion(const FBox& Region, uint8 Priority)
{
    // Find all nodes in the region
    TArray<uint32> NodesInRegion;
    NodeManager->FindNodesInRegion(Region, NodesInRegion);
    
    // Prioritize these nodes
    for (uint32 NodeIndex : NodesInRegion)
    {
        uint32 FieldIndex = GetFieldIndexForNode(NodeIndex);
        if (FieldIndex != INDEX_NONE)
        {
            // Prioritize the field by updating its resolution based on importance
            FFieldData& Field = Fields[FieldIndex];
            
            // Higher priority = higher resolution
            uint32 TargetResolution = FMath::Max(8u, 8u + Priority * 4);
            
            if (Field.Resolution < TargetResolution)
            {
                SetFieldResolution(FieldIndex, TargetResolution);
            }
        }
    }
}

FMaterialSDFManager::FMaterialStats FMaterialSDFManager::GetMaterialStats() const
{
    FMaterialStats Stats;
    
    // Count fields by type
    for (const FFieldData& Field : Fields)
    {
        if (Field.State != EFieldState::Unallocated)
        {
            Stats.TotalFields++;
            
            uint64 FieldMemory = Field.DistanceValues.GetAllocatedSize();
            Stats.TotalMemoryUsage += FieldMemory;
            
            switch (Field.State)
            {
                case EFieldState::Empty:
                    Stats.EmptyFields++;
                    break;
                    
                case EFieldState::Homogeneous:
                    Stats.HomogeneousFields++;
                    Stats.MemoryByMaterial[Field.PrimaryMaterial] += FieldMemory;
                    break;
                    
                case EFieldState::Interface:
                    Stats.InterfaceFields++;
                    
                    // Count memory by material for interface fields (approximate split)
                    for (uint32 m = 0; m < MaterialChannelCount; ++m)
                    {
                        Stats.MemoryByMaterial[m] += FieldMemory / MaterialChannelCount;
                    }
                    break;
                    
                default:
                    break;
            }
        }
    }
    
    return Stats;
}

void FMaterialSDFManager::SerializeFieldData(uint32 FieldIndex, TArray<uint8>& OutData) const
{
    if (!Fields.IsValidIndex(FieldIndex))
    {
        return;
    }
    
    const FFieldData& Field = Fields[FieldIndex];
    FMemoryWriter Writer(OutData);
    
    // Write header
    Writer << Field.NodeIndex;
    Writer << Field.Origin;
    Writer << Field.CellSize;
    Writer << Field.Resolution;
    uint8 StateValue = static_cast<uint8>(Field.State);
    Writer << StateValue;
    Writer << Field.PrimaryMaterial;
    Writer << Field.VersionId;
    
    // Compress and write field values if allocated
    if (Field.State != EFieldState::Unallocated)
    {
        uint32 ValuesSize = Field.DistanceValues.Num();
        Writer << ValuesSize;
        
        // Write raw values for now (compression would be added here)
        Writer.Serialize(const_cast<float*>(Field.DistanceValues.GetData()), ValuesSize * sizeof(float));
    }
    else
    {
        uint32 ValuesSize = 0;
        Writer << ValuesSize;
    }
}

bool FMaterialSDFManager::DeserializeFieldData(uint32 FieldIndex, const TArray<uint8>& Data)
{
    if (FieldIndex == INDEX_NONE)
    {
        // Create a new field
        FieldIndex = Fields.Add(FFieldData());
    }
    else if (!Fields.IsValidIndex(FieldIndex))
    {
        return false;
    }
    
    FFieldData& Field = Fields[FieldIndex];
    FMemoryReader Reader(Data);
    
    // Read header
    Reader << Field.NodeIndex;
    Reader << Field.Origin;
    Reader << Field.CellSize;
    Reader << Field.Resolution;
    uint8 StateValue;
    Reader << StateValue;
    Field.State = static_cast<EFieldState>(StateValue);
    Reader << Field.PrimaryMaterial;
    Reader << Field.VersionId;
    
    // Read field values
    uint32 ValuesSize;
    Reader << ValuesSize;
    
    if (ValuesSize > 0)
    {
        // Allocate memory
        Field.DistanceValues.SetNumUninitialized(ValuesSize);
        
        // Read raw values (decompression would be added here)
        Reader.Serialize(Field.DistanceValues.GetData(), ValuesSize * sizeof(float));
    }
    
    // Update node map
    if (Field.NodeIndex != INDEX_NONE)
    {
        NodeToFieldMap.Add(Field.NodeIndex, FieldIndex);
    }
    
    Field.bModified = false;
    
    return true;
}

void FMaterialSDFManager::SerializeAllFields(TArray<uint8>& OutData) const
{
    FMemoryWriter Writer(OutData);
    
    // Write number of fields
    uint32 FieldCount = 0;
    for (const FFieldData& Field : Fields)
    {
        if (Field.State != EFieldState::Unallocated)
        {
            FieldCount++;
        }
    }
    Writer << FieldCount;
    Writer << MaterialChannelCount;
    Writer << CurrentVersionCounter.Load();
    
    // Write each allocated field
    for (uint32 i = 0; i < Fields.Num(); ++i)
    {
        if (Fields[i].State != EFieldState::Unallocated)
        {
            Writer << i;
            TArray<uint8> FieldData;
            SerializeFieldData(i, FieldData);
            
            uint32 DataSize = FieldData.Num();
            Writer << DataSize;
            Writer.Serialize(FieldData.GetData(), DataSize);
        }
    }
}

bool FMaterialSDFManager::DeserializeAllFields(const TArray<uint8>& Data)
{
    FMemoryReader Reader(Data);
    
    // Clear existing fields
    Fields.Empty();
    NodeToFieldMap.Empty();
    
    // Read number of fields
    uint32 FieldCount;
    Reader >> FieldCount;
    Reader >> MaterialChannelCount;
    uint64 VersionCount;
    Reader >> VersionCount;
    
    // Setup fields array
    Fields.AddDefaulted(FieldCount);
    
    // Read each field
    for (uint32 i = 0; i < FieldCount; ++i)
    {
        uint32 FieldIndex;
        Reader >> FieldIndex;
        
        uint32 DataSize;
        Reader >> DataSize;
        
        TArray<uint8> FieldData;
        FieldData.SetNumUninitialized(DataSize);
        Reader.Serialize(FieldData.GetData(), DataSize);
        
        DeserializeFieldData(FieldIndex, FieldData);
    }
    
    // Set version counter
    CurrentVersionCounter.Store(VersionCount);
    
    return true;
}

void FMaterialSDFManager::RegisterFieldModification(uint32 FieldIndex, uint8 MaterialIndex)
{
    if (Fields.IsValidIndex(FieldIndex))
    {
        FFieldData& Field = Fields[FieldIndex];
        uint64 OldVersion = Field.VersionId;
        Field.VersionId = CurrentVersionCounter.IncrementExchange();
        Field.bModified = true;
        
        // Add to version map
        if (!VersionFieldMap.Contains(Field.VersionId))
        {
            VersionFieldMap.Add(Field.VersionId, TSet<uint32>());
        }
        VersionFieldMap[Field.VersionId].Add(FieldIndex);
        
        // Publish event for the modification
        UEventBus::Get()->PublishEvent(FFieldModifiedEvent(
            FieldIndex,
            Field.NodeIndex,
            MaterialIndex,
            OldVersion,
            Field.VersionId
        ));
    }
}

TArray<uint32> FMaterialSDFManager::GetFieldsModifiedSince(uint64 BaseVersion) const
{
    TArray<uint32> ModifiedFields;
    
    for (const auto& VersionPair : VersionFieldMap)
    {
        if (VersionPair.Key > BaseVersion)
        {
            ModifiedFields.Append(VersionPair.Value.Array());
        }
    }
    
    return ModifiedFields;
}

void FMaterialSDFManager::MarkFieldAsModified(uint32 FieldIndex, uint64 VersionId)
{
    if (Fields.IsValidIndex(FieldIndex))
    {
        FFieldData& Field = Fields[FieldIndex];
        uint64 OldVersion = Field.VersionId;
        Field.VersionId = VersionId;
        Field.bModified = true;
        
        // Add to version map
        if (!VersionFieldMap.Contains(VersionId))
        {
            VersionFieldMap.Add(VersionId, TSet<uint32>());
        }
        VersionFieldMap[VersionId].Add(FieldIndex);
        
        // Update global version counter if needed
        if (VersionId > CurrentVersionCounter.Load())
        {
            CurrentVersionCounter.Store(VersionId);
        }
        
        // Publish event for the version change
        UEventBus::Get()->PublishEvent(FFieldVersionChangedEvent(
            FieldIndex,
            OldVersion,
            Field.VersionId
        ));
    }
}

uint64 FMaterialSDFManager::GetFieldVersion(uint32 FieldIndex) const
{
    if (Fields.IsValidIndex(FieldIndex))
    {
        return Fields[FieldIndex].VersionId;
    }
    
    return 0;
}

int32 FMaterialSDFManager::GetFieldIndex(int32 X, int32 Y, int32 Z, int32 Resolution) const
{
    // Use Morton order (Z-order curve) for better cache coherence
    return ZOrderCurve::MortonEncode(X, Y, Z) % (Resolution * Resolution * Resolution);
}

FVector FMaterialSDFManager::GetLocalPositionFromIndices(int32 X, int32 Y, int32 Z, float CellSize) const
{
    return FVector(
        X * CellSize + CellSize * 0.5f,
        Y * CellSize + CellSize * 0.5f,
        Z * CellSize + CellSize * 0.5f
    );
}