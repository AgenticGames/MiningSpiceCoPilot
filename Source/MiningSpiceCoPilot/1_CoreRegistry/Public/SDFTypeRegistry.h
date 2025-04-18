// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IRegistry.h"
#include "Containers/Map.h"
#include "Templates/SharedPointer.h"
#include "HAL/ThreadSafeBool.h"
#include "Misc/SpinLock.h"
#include "Interfaces/IServiceLocator.h"
#include "TypeVersionMigrationInfo.h"
#include "Interfaces/IMemoryManager.h"
#include "Interfaces/IPoolAllocator.h"
#include "SharedBufferManager.h"

/**
 * SDF field operation types for CSG operations
 */
enum class ESDFOperationType : uint8
{
    /** Union of two SDF fields */
    Union,
    
    /** Subtraction of two SDF fields */
    Subtraction,
    
    /** Intersection of two SDF fields */
    Intersection,
    
    /** Smooth union of two SDF fields */
    SmoothUnion,
    
    /** Smooth subtraction of two SDF fields */
    SmoothSubtraction,
    
    /** Smooth intersection of two SDF fields */
    SmoothIntersection,
    
    /** Custom operation type */
    Custom
};

/**
 * SDF field evaluation contexts
 */
enum class ESDFEvaluationContext : uint8
{
    /** Mining operations */
    Mining,
    
    /** Rendering and visualization */
    Rendering,
    
    /** Physics simulation */
    Physics,
    
    /** Game logic and gameplay */
    GameLogic
};

/**
 * Memory access patterns for SDF operations
 */
enum class ESDFMemoryAccessPattern : uint8
{
    Sequential,
    Strided,
    Random
};

/**
 * Optimization levels for SDF operations
 */
enum class ESDFOptimizationLevel : uint8
{
    Default,
    Aggressive,
    Conservative
};

/**
 * Cache locality hints for SDF operations
 */
enum class ESDFCacheLocality : uint8
{
    Low,
    Medium,
    High
};

/**
 * SIMD instruction sets
 */
enum class ESIMD_InstructionSet : uint8
{
    None,
    SSE2,
    AVX,
    AVX2,
    AVX512
};

/**
 * Precision modes for SDF operations
 */
enum class ESDFPrecisionMode : uint8
{
    HalfPrecision,
    SinglePrecision,
    DoublePrecision
};

/**
 * Memory layout types for SDF operations
 */
enum class ESDFMemoryLayout : uint8
{
    Sequential,
    Interleaved
};

/**
 * Field capabilities for SDF operations
 */
enum class ESDFFieldCapabilities : uint32
{
    None = 0,
    SupportsGPU = 1 << 0,
    SupportsThreading = 1 << 1,
    SupportsSIMD = 1 << 2,
    SupportsHotReload = 1 << 3,
    SupportsVersionedSerialization = 1 << 4,
    SupportsIncrementalUpdates = 1 << 5
};

/**
 * Additional properties for SDF field type operations
 */
struct MININGSPICECOPILOT_API FSDFFieldOperationProperties
{
    /** Preferred thread block size for parallel evaluation */
    uint32 PreferredThreadBlockSize;
    
    /** Whether operation can be vectorized using SIMD */
    bool bCanVectorize;
    
    /** Whether operation supports GPU acceleration */
    bool bSupportsGPU;
    
    /** Cached shader resource name for GPU implementation */
    FName GPUShaderName;
    
    /** Evaluation cost estimate (relative units) */
    float EvaluationCost;
    
    /** Memory access pattern */
    ESDFMemoryAccessPattern MemoryPattern;
    
    /** Optimization level for this operation */
    ESDFOptimizationLevel OptimizationLevel;
    
    /** Cache locality hint */
    ESDFCacheLocality CacheLocality;
    
    /** Default constructor */
    FSDFFieldOperationProperties()
        : PreferredThreadBlockSize(64)
        , bCanVectorize(false)
        , bSupportsGPU(false)
        , EvaluationCost(1.0f)
        , MemoryPattern(ESDFMemoryAccessPattern::Sequential)
        , OptimizationLevel(ESDFOptimizationLevel::Default)
        , CacheLocality(ESDFCacheLocality::Medium)
    {
    }
};

/**
 * Enhanced version of FSDFFieldTypeInfo with additional performance attributes
 */
struct MININGSPICECOPILOT_API FSDFFieldTypeInfo
{
    /** Unique ID for this field type */
    uint32 TypeId;
    
    /** Name of this field type */
    FName TypeName;
    
    /** Number of channels supported by this field */
    uint32 ChannelCount;
    
    /** Version of this field type's schema */
    uint32 SchemaVersion;
    
    /** Alignment requirements for field data */
    uint32 AlignmentRequirement;
    
    /** Whether this field type supports GPU evaluation */
    bool bSupportsGPU;
    
    /** Whether this field type supports multi-threaded evaluation */
    bool bSupportsThreading;
    
    /** Whether this field type supports SIMD operations */
    bool bSupportsSIMD;
    
    /** Required SIMD instruction set for optimized operations */
    ESIMD_InstructionSet RequiredInstructionSet;
    
    /** Precision mode for this field type */
    ESDFPrecisionMode PrecisionMode;
    
    /** Memory layout type for field data */
    ESDFMemoryLayout MemoryLayout;
    
    /** Memory access pattern for field evaluation */
    ESDFMemoryAccessPattern MemoryPattern;
    
    /** Whether this field supports hot-reload */
    bool bSupportsHotReload;
    
    /** Whether this field supports versioned serialization */
    bool bSupportsVersionedSerialization;
    
    /** Whether this field supports incremental updates */
    bool bSupportsIncrementalUpdates;
    
    /** Capabilities flags for this field type (bitwise combination of ESDFFieldCapabilities) */
    uint32 CapabilitiesFlags;
    
    /** Size of data for this field type in bytes */
    uint32 DataSize;
    
    /** Blueprint class for editor representation (if applicable) */
    TSoftClassPtr<UObject> BlueprintClassType;
    
    /** Default constructor */
    FSDFFieldTypeInfo()
        : TypeId(0)
        , ChannelCount(1)
        , SchemaVersion(1)
        , AlignmentRequirement(16)
        , bSupportsGPU(false)
        , bSupportsThreading(true)
        , bSupportsSIMD(false)
        , RequiredInstructionSet(ESIMD_InstructionSet::SSE2)
        , PrecisionMode(ESDFPrecisionMode::SinglePrecision)
        , MemoryLayout(ESDFMemoryLayout::Sequential)
        , MemoryPattern(ESDFMemoryAccessPattern::Sequential)
        , bSupportsHotReload(false)
        , bSupportsVersionedSerialization(true)
        , bSupportsIncrementalUpdates(false)
        , CapabilitiesFlags(0)
        , DataSize(0)
    {
    }
    
    /** Helper method to check if this field type has a specific capability */
    bool HasCapability(ESDFFieldCapabilities Capability) const
    {
        return (CapabilitiesFlags & static_cast<uint32>(Capability)) != 0;
    }
    
    /** Helper method to add a capability to this field type */
    void AddCapability(ESDFFieldCapabilities Capability)
    {
        CapabilitiesFlags |= static_cast<uint32>(Capability);
    }
};

/**
 * Enhanced version of FSDFOperationInfo with additional properties
 */
struct MININGSPICECOPILOT_API FSDFOperationInfo
{
    /** Unique ID for this operation */
    uint32 OperationId;
    
    /** Name of this operation */
    FName OperationName;
    
    /** Type of CSG operation */
    ESDFOperationType OperationType;
    
    /** Number of input fields required */
    uint32 InputCount;
    
    /** Whether operation supports variable smoothing */
    bool bSupportsSmoothing;
    
    /** Whether operation preserves field sign */
    bool bPreservesSign;
    
    /** Whether operation is commutative */
    bool bIsCommutative;
    
    /** Advanced operation properties */
    FSDFFieldOperationProperties Properties;
    
    /** Field evaluation context this operation supports */
    TArray<ESDFEvaluationContext> SupportedContexts;
    
    /** Default operation parameters */
    TMap<FName, FString> DefaultParameters;
    
    /** Blueprint function for editor visualization (if applicable) */
    TSoftObjectPtr<UFunction> BlueprintFunction;
    
    /** Default constructor */
    FSDFOperationInfo()
        : OperationId(0)
        , OperationType(ESDFOperationType::Union)
        , InputCount(2)
        , bSupportsSmoothing(false)
        , bPreservesSign(true)
        , bIsCommutative(true)
    {
        // By default, support all evaluation contexts
        SupportedContexts.Add(ESDFEvaluationContext::Mining);
        SupportedContexts.Add(ESDFEvaluationContext::Rendering);
        SupportedContexts.Add(ESDFEvaluationContext::Physics);
        SupportedContexts.Add(ESDFEvaluationContext::GameLogic);
    }
};

/**
 * Registry for SDF field types in the mining system
 * Handles field type registration, operation compatibility, and evaluation strategies
 */
class MININGSPICECOPILOT_API FSDFTypeRegistry : public IRegistry
{
public:
    /** Default constructor */
    FSDFTypeRegistry();
    
    /** Destructor */
    virtual ~FSDFTypeRegistry();
    
    //~ Begin IRegistry Interface
    virtual bool Initialize() override;
    virtual void Shutdown() override;
    virtual bool IsInitialized() const override;
    virtual FName GetRegistryName() const override;
    virtual uint32 GetSchemaVersion() const override;
    virtual bool Validate(TArray<FString>& OutErrors) const override;
    virtual void Clear() override;
    virtual bool SetTypeVersion(uint32 TypeId, uint32 NewVersion, bool bMigrateInstanceData = true) override;
    virtual uint32 GetTypeVersion(uint32 TypeId) const override;
    //~ End IRegistry Interface
    
    /**
     * Registers a new SDF field type
     * @param InTypeName Name of the field type
     * @param InChannelCount Number of channels supported
     * @param InAlignmentRequirement Memory alignment requirement (must be power of 2)
     * @param bInSupportsGPU Whether this field type supports GPU evaluation
     * @return Unique ID for the registered type, or 0 if registration failed
     */
    uint32 RegisterFieldType(
        const FName& InTypeName,
        uint32 InChannelCount,
        uint32 InAlignmentRequirement = 16,
        bool bInSupportsGPU = false);

    /**
     * Registers a new SDF operation
     * @param InOperationName Name of the operation
     * @param InOperationType Type of CSG operation
     * @param InInputCount Number of input fields required
     * @param bInSupportsSmoothing Whether operation supports smoothing
     * @return Unique ID for the registered operation, or 0 if registration failed
     */
    uint32 RegisterOperation(
        const FName& InOperationName,
        ESDFOperationType InOperationType,
        uint32 InInputCount,
        bool bInSupportsSmoothing = false);
        
    /**
     * Gets information about a registered field type
     * @param InTypeId Unique ID of the field type
     * @return Pointer to field type info, or nullptr if not found
     */
    const FSDFFieldTypeInfo* GetFieldTypeInfo(uint32 InTypeId) const;
    
    /**
     * Gets information about a registered field type by name
     * @param InTypeName Name of the field type
     * @return Pointer to field type info, or nullptr if not found
     */
    const FSDFFieldTypeInfo* GetFieldTypeInfoByName(const FName& InTypeName) const;
    
    /**
     * Gets information about a registered operation
     * @param InOperationId Unique ID of the operation
     * @return Pointer to operation info, or nullptr if not found
     */
    const FSDFOperationInfo* GetOperationInfo(uint32 InOperationId) const;
    
    /**
     * Gets information about a registered operation by name
     * @param InOperationName Name of the operation
     * @return Pointer to operation info, or nullptr if not found
     */
    const FSDFOperationInfo* GetOperationInfoByName(const FName& InOperationName) const;
    
    /**
     * Gets all registered field types
     * @return Array of all field type infos
     */
    TArray<FSDFFieldTypeInfo> GetAllFieldTypes() const;
    
    /**
     * Gets all registered operations
     * @return Array of all operation infos
     */
    TArray<FSDFOperationInfo> GetAllOperations() const;
    
    /**
     * Gets operations of a specific type
     * @param InOperationType The type of operations to retrieve
     * @return Array of matching operation infos
     */
    TArray<FSDFOperationInfo> GetOperationsByType(ESDFOperationType InOperationType) const;
    
    /**
     * Gets field types with a specific capability
     * @param InCapability The capability to filter by
     * @return Array of matching field type infos
     */
    TArray<FSDFFieldTypeInfo> GetFieldTypesWithCapability(ESDFFieldCapabilities InCapability) const;
    
    /**
     * Checks if a field type is registered
     * @param InTypeId Unique ID of the field type
     * @return True if the type is registered
     */
    bool IsFieldTypeRegistered(uint32 InTypeId) const;
    
    /**
     * Checks if a field type is registered by name
     * @param InTypeName Name of the field type
     * @return True if the type is registered
     */
    bool IsFieldTypeRegistered(const FName& InTypeName) const;
    
    /**
     * Checks if an operation is registered
     * @param InOperationId Unique ID of the operation
     * @return True if the operation is registered
     */
    bool IsOperationRegistered(uint32 InOperationId) const;
    
    /**
     * Checks if an operation type is compatible with GPU acceleration
     * @param InOperationType The operation type to check
     * @return True if the operation can be accelerated on GPU
     */
    bool IsOperationGPUCompatible(ESDFOperationType InOperationType) const;
    
    /**
     * Checks if an operation type is compatible with SIMD instructions
     * @param InOperationType The operation type to check
     * @return True if the operation can be vectorized with SIMD
     */
    bool IsOperationSIMDCompatible(ESDFOperationType InOperationType) const;
    
    /**
     * Gets the optimal thread block size for an operation type
     * @param InOperationType The operation type to check
     * @return Recommended thread block size for parallel processing
     */
    uint32 GetOptimalThreadBlockSize(ESDFOperationType InOperationType) const;
    
    /**
     * Detects and updates hardware capabilities flags
     */
    void DetectHardwareCapabilities();
    
    /**
     * Sets default properties for an operation based on its type
     * @param OpInfo The operation info to configure
     * @param InOperationType The type of operation
     */
    void SetDefaultOperationProperties(FSDFOperationInfo* OpInfo, ESDFOperationType InOperationType);
    
    /** Gets the singleton instance of the SDF type registry */
    static FSDFTypeRegistry& Get();
    
private:
    /** Generates a unique type ID for new field type registrations */
    uint32 GenerateUniqueTypeId();
    
    /** Generates a unique operation ID for new operation registrations */
    uint32 GenerateUniqueOperationId();
    
    /** Maps of registered field types by ID and name */
    TMap<uint32, TSharedRef<FSDFFieldTypeInfo>> FieldTypeMap;
    TMap<FName, uint32> FieldTypeNameMap;
    
    /** Maps of registered operations by ID and name */
    TMap<uint32, TSharedRef<FSDFOperationInfo>> OperationMap;
    TMap<FName, uint32> OperationNameMap;
    
    /** Map of type IDs to shared buffer managers for type-safe memory access */
    mutable TMap<uint32, TSharedPtr<FSharedBufferManager>> TypeBufferMap;
    
    /** Next available field type ID */
    uint32 NextTypeId;
    
    /** Next available operation ID */
    uint32 NextOperationId;
    
    /** Registry lock for thread safety */
    mutable FRWLock RegistryLock;
    
    /** Flag indicating if the registry has been initialized */
    FThreadSafeBool bIsInitialized;
    
    /** Schema version for this registry */
    uint32 SchemaVersion;
    
    /** Hardware capability flags */
    bool bHasSSE2;
    bool bHasAVX;
    bool bHasAVX2;
    bool bHasAVX512;
    bool bHasGPUSupport;
    
    /** Singleton instance */
    static FSDFTypeRegistry* Singleton;
    
    /** Flag indicating if singleton has been initialized */
    static FThreadSafeBool bSingletonInitialized;
};