// MaterialSDFManager.h
// Multi-channel SDF management for material-specific distance fields

#pragma once

#include "CoreMinimal.h"
#include "Math/Vector.h"
#include "Math/Box.h"
#include "Containers/Map.h"
#include "Containers/Array.h"
#include "Templates/Atomic.h"
#include "HAL/ThreadSafeCounter.h"

// Forward declarations
class USVOHybridVolume;
class FOctreeNodeManager;
class FNarrowBandAllocator;

/**
 * Multi-channel SDF management for material-specific distance fields
 * Manages independent distance functions for each material type
 * Supports boolean operations, material blending and efficient field updates
 */
class MININGSPICECOPILOT_API FMaterialSDFManager
{
public:
    // Constructor and destructor
    FMaterialSDFManager();
    ~FMaterialSDFManager();

    // Material SDF allocation state
    enum class EFieldState : uint8
    {
        Unallocated,    // Not yet allocated
        Empty,          // Contains no material
        Homogeneous,    // Filled with a single material
        Interface       // Contains material boundaries
    };
    
    // Field data structure for narrow band SDF
    struct FFieldData
    {
        uint32 NodeIndex;            // Associated octree node
        TArray<float> DistanceValues; // Distance field values
        FVector Origin;              // Local space origin
        float CellSize;              // Size of each cell
        uint32 Resolution;           // Resolution of the field (cells per side)
        EFieldState State;           // Current state of the field
        uint8 PrimaryMaterial;       // Primary material index
        bool bModified;              // Whether the field has been modified since last update
        uint64 VersionId;            // Version for network synchronization
        
        FFieldData()
            : NodeIndex(INDEX_NONE)
            , Origin(FVector::ZeroVector)
            , CellSize(0.0f)
            , Resolution(0)
            , State(EFieldState::Unallocated)
            , PrimaryMaterial(0)
            , bModified(false)
            , VersionId(0)
        {}
    };
    
    // Material field allocation options
    struct FFieldAllocationOptions
    {
        uint32 Resolution;           // Resolution in each dimension
        float CellSize;              // Size of each cell in world units
        bool bInitializeEmpty;       // Initialize as empty field
        uint8 DefaultMaterial;       // Default material to initialize with
        
        FFieldAllocationOptions()
            : Resolution(16)
            , CellSize(0.5f)
            , bInitializeEmpty(true)
            , DefaultMaterial(0)
        {}
    };
    
    // Material SDF statistics
    struct FMaterialStats
    {
        uint32 TotalFields;          // Total number of fields
        uint32 EmptyFields;          // Number of empty fields
        uint32 HomogeneousFields;    // Number of homogeneous fields
        uint32 InterfaceFields;      // Number of fields with interfaces
        uint64 TotalMemoryUsage;     // Total memory usage
        uint64 MemoryByMaterial[256]; // Memory usage by material index
        
        FMaterialStats()
        {
            TotalFields = 0;
            EmptyFields = 0;
            HomogeneousFields = 0;
            InterfaceFields = 0;
            TotalMemoryUsage = 0;
            FMemory::Memzero(MemoryByMaterial, sizeof(MemoryByMaterial));
        }
    };

    // Initialization
    void Initialize(USVOHybridVolume* InVolume, FOctreeNodeManager* InNodeManager);
    void SetNarrowBandAllocator(FNarrowBandAllocator* InAllocator);
    void SetMaterialChannelCount(uint32 MaterialCount);
    
    // Field creation and management
    uint32 CreateField(uint32 NodeIndex, const FFieldAllocationOptions& Options);
    void ReleaseField(uint32 FieldIndex);
    void UpdateFieldState(uint32 FieldIndex);
    void SetFieldResolution(uint32 FieldIndex, uint32 NewResolution);
    EFieldState GetFieldState(uint32 FieldIndex) const;
    
    // Field data access
    FFieldData* GetFieldData(uint32 FieldIndex);
    const FFieldData* GetFieldData(uint32 FieldIndex) const;
    uint32 GetFieldIndexForNode(uint32 NodeIndex) const;
    float GetDistanceValue(uint32 FieldIndex, const FVector& LocalPosition, uint8 MaterialIndex) const;
    void SetDistanceValue(uint32 FieldIndex, const FVector& LocalPosition, uint8 MaterialIndex, float Value);
    
    // Material operations
    void ApplyMaterialSphere(uint32 FieldIndex, const FVector& Center, float Radius, uint8 MaterialIndex, float Value);
    void UnionMaterial(uint32 FieldIndex, const FVector& Center, float Radius, uint8 MaterialIndex, float Strength = 1.0f);
    void SubtractMaterial(uint32 FieldIndex, const FVector& Center, float Radius, uint8 MaterialIndex, float Strength = 1.0f);
    void BlendMaterials(uint32 FieldIndex, const FVector& Center, float Radius, uint8 SourceMaterial, uint8 TargetMaterial, float BlendFactor);
    void ClearMaterial(uint32 FieldIndex, uint8 MaterialIndex);
    void FillWithMaterial(uint32 FieldIndex, uint8 MaterialIndex);
    
    // Field evaluation
    float EvaluateFieldAtPosition(const FVector& WorldPosition, uint8 MaterialIndex) const;
    TArray<float> EvaluateMultiChannelFieldAtPosition(const FVector& WorldPosition) const;
    FVector EvaluateGradientAtPosition(const FVector& WorldPosition, uint8 MaterialIndex) const;
    bool IsPositionInside(const FVector& WorldPosition, uint8 MaterialIndex) const;
    
    // Batch operations
    void EvaluateFieldBatch(const TArray<FVector>& Positions, uint8 MaterialIndex, TArray<float>& OutValues) const;
    void ApplyFieldOperation(const TArray<uint32>& FieldIndices, TFunction<void(uint32, FFieldData*)> OperationFunc);
    void PropagateFields(const TArray<uint32>& SourceFields, const TArray<uint32>& TargetFields);
    
    // Memory management
    void OptimizeMemoryUsage();
    void PrioritizeRegion(const FBox& Region, uint8 Priority);
    FMaterialStats GetMaterialStats() const;
    
    // Serialization
    void SerializeFieldData(uint32 FieldIndex, TArray<uint8>& OutData) const;
    bool DeserializeFieldData(uint32 FieldIndex, const TArray<uint8>& Data);
    void SerializeAllFields(TArray<uint8>& OutData) const;
    bool DeserializeAllFields(const TArray<uint8>& Data);
    
    // Network synchronization
    void RegisterFieldModification(uint32 FieldIndex, uint8 MaterialIndex);
    TArray<uint32> GetFieldsModifiedSince(uint64 BaseVersion) const;
    void MarkFieldAsModified(uint32 FieldIndex, uint64 VersionId);
    uint64 GetFieldVersion(uint32 FieldIndex) const;
    
private:
    // Internal data
    USVOHybridVolume* Volume;
    FOctreeNodeManager* NodeManager;
    FNarrowBandAllocator* NarrowBandAllocator;
    
    // Field storage
    TArray<FFieldData> Fields;
    TMap<uint32, uint32> NodeToFieldMap;
    uint32 MaterialChannelCount;
    
    // Memory tracking
    uint64 TotalMemoryUsage;
    
    // Network state tracking
    TAtomic<uint64> CurrentVersionCounter;
    TMap<uint64, TSet<uint32>> VersionFieldMap;
    
    // Field utility functions
    int32 GetFieldIndex(int32 X, int32 Y, int32 Z, int32 Resolution) const;
    FVector GetLocalPositionFromIndices(int32 X, int32 Y, int32 Z, float CellSize) const;
    void GetCellIndicesFromPosition(const FVector& LocalPos, float CellSize, int32 Resolution, 
                                   int32& OutX, int32& OutY, int32& OutZ) const;
    FVector CalculateGradient(uint32 FieldIndex, const FVector& LocalPosition, uint8 MaterialIndex) const;
    
    // Field operations
    void PropagateChanges(uint32 FieldIndex, const FBox& AffectedRegion);
    void UpdateAdjacentFields(uint32 FieldIndex);
    float TrilinearInterpolation(uint32 FieldIndex, const FVector& LocalPosition, uint8 MaterialIndex) const;
    
    // Memory management helpers
    void AllocateFieldMemory(FFieldData& Field, uint32 Resolution);
    void DeallocateFieldMemory(FFieldData& Field);
    void CompactFields();
    void ReleaseUnusedFields();
};
