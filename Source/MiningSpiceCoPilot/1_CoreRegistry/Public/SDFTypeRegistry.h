// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "../../3_ThreadingTaskSystem/Public/ParallelExecutor.h"
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
#include "ThreadSafety.h"
#include "../../3_ThreadingTaskSystem/Public/TaskScheduler.h"
#include "../../3_ThreadingTaskSystem/Public/AsyncTaskTypes.h"

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
    
    /** Whether this field type uses optimized memory access patterns */
    bool bOptimizedAccess;
    
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
        , bOptimizedAccess(false)
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
 * Shared buffer for SDF field data
 * Provides thread-safe access to field data with version tracking
 */
class MININGSPICECOPILOT_API FSharedFieldBuffer
{
public:
    /** Constructor */
    FSharedFieldBuffer(uint32 InCapacity, uint32 InTypeId)
        : Capacity(InCapacity)
        , TypeId(InTypeId)
        , Version(1)
        , RefCount(0)
    {
        Data = FMemory::Malloc(Capacity);
    }
    
    /** Destructor */
    ~FSharedFieldBuffer()
    {
        if (Data)
        {
            FMemory::Free(Data);
            Data = nullptr;
        }
    }
    
    /** Gets a pointer to the buffer data */
    void* GetData() const
    {
        return Data;
    }
    
    /** Gets the buffer capacity */
    uint32 GetCapacity() const
    {
        return Capacity;
    }
    
    /** Gets the buffer version */
    uint32 GetVersion() const
    {
        return Version.GetValue();
    }
    
    /** Increments the buffer version */
    uint32 IncrementVersion()
    {
        return Version.Increment();
    }
    
    /** Gets the buffer type ID */
    uint32 GetTypeId() const
    {
        return TypeId;
    }
    
    /** Increments the reference count */
    void AddRef()
    {
        RefCount.Increment();
    }
    
    /** Decrements the reference count */
    uint32 Release()
    {
        return RefCount.Decrement();
    }
    
    /** Gets the current reference count */
    uint32 GetRefCount() const
    {
        return RefCount.GetValue();
    }
    
private:
    /** Buffer data */
    void* Data;
    
    /** Buffer capacity */
    uint32 Capacity;
    
    /** Type ID for this buffer */
    uint32 TypeId;
    
    /** Version counter for optimistic concurrency */
    FThreadSafeCounter Version;
    
    /** Reference count */
    FThreadSafeCounter RefCount;
};

/**
 * Structure for caching field operations
 */
struct MININGSPICECOPILOT_API FFieldOperationCacheEntry 
{ 
    /** Version of the cached operation */
    uint32 Version; 
    
    /** The cached operation info */
    TSharedRef<FSDFOperationInfo> OperationInfo; 
    
    /** Default constructor */
    FFieldOperationCacheEntry() 
        : Version(0) 
    {}
    
    /** Constructor with parameters */
    FFieldOperationCacheEntry(uint32 InVersion, TSharedRef<FSDFOperationInfo> InOperationInfo)
        : Version(InVersion)
        , OperationInfo(InOperationInfo)
    {}
};

/**
 * Forward declaration of FSVOFieldReadLock for FFieldEvaluationContext
 */
class FSVOFieldReadLock;

/**
 * Context structure for field evaluation
 */
struct MININGSPICECOPILOT_API FFieldEvaluationContext 
{ 
    /** Version of this evaluation context */
    uint32 Version; 
    
    /** Pointer to the field data */
    void* FieldData;
    
    /** Type ID of the field */
    uint32 TypeId;
    
    /** Default constructor */
    FFieldEvaluationContext()
        : Version(0)
        , FieldData(nullptr)
        , TypeId(0)
    {}
    
    /** Constructor with parameters */
    FFieldEvaluationContext(uint32 InVersion, void* InFieldData, uint32 InTypeId)
        : Version(InVersion)
        , FieldData(InFieldData)
        , TypeId(InTypeId)
    {}
    
    /** Check if this context is valid */
    bool IsValid(const FSVOFieldReadLock& EvaluationLock) const
    { 
        return EvaluationLock.GetCurrentVersion() == Version; 
    }
};

/**
 * Registry for SDF field types in the mining system
 * Handles field type registration, operation compatibility, and evaluation strategies
 */
class MININGSPICECOPILOT_API FSDFTypeRegistry : public IRegistry
{
public:
    /** Constructor */
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
    virtual ERegistryType GetRegistryType() const override;
    virtual ETypeCapabilities GetTypeCapabilities(uint32 TypeId) const override;
    
    /**
     * Gets the extended capabilities of a specific field type
     * @param TypeId The ID of the field type to query
     * @return The extended capabilities of the field type
     */
    virtual ETypeCapabilitiesEx GetTypeCapabilitiesEx(uint32 TypeId) const override;
    
    virtual uint64 ScheduleTypeTask(uint32 TypeId, TFunction<void()> TaskFunc, const FTaskConfig& Config) override;
    virtual bool PreInitializeTypes() override;
    virtual bool ParallelInitializeTypes(bool bParallel = true) override;
    virtual bool PostInitializeTypes() override;
    virtual TArray<int32> GetTypeDependencies(uint32 TypeId) const override;
    //~ End IRegistry Interface
    
    /**
     * Registers a new SDF field type with the registry
     * @param InTypeName Name of the field type
     * @param InChannelCount Number of channels for this field type
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
     * Registers multiple field types in a batch using parallel processing
     * @param TypeInfos Array of field type information to register
     * @param OutTypeIds Array to receive the assigned type IDs
     * @param OutErrors Array to receive any errors that occurred during registration
     * @param Config Configuration for parallel execution
     * @return True if all types were registered successfully
     */
    bool RegisterFieldTypesBatch(
        const TArray<FSDFFieldTypeInfo>& TypeInfos,
        TArray<uint32>& OutTypeIds,
        TArray<FString>& OutErrors,
        struct FParallelConfig Config = FParallelConfig());

    /**
     * Pre-validates a batch of field types before registration
     * @param TypeInfos Array of field type information to validate
     * @param OutErrors Array to receive validation errors
     * @param bParallel Whether to validate in parallel
     * @return True if all types passed validation
     */
    bool PrevalidateFieldTypes(
        const TArray<FSDFFieldTypeInfo>& TypeInfos,
        TArray<FString>& OutErrors,
        bool bParallel = true);

    /**
     * Validates the consistency of all registered types
     * @param OutErrors Array to receive any validation errors
     * @param bParallel Whether to validate in parallel
     * @return True if all types are consistent
     */
    bool ValidateTypeConsistency(
        TArray<FString>& OutErrors,
        bool bParallel = true);

    /**
     * Registers a new operation with the registry
     * @param InOperationName Name of the operation
     * @param InOperationType Type of operation
     * @param InInputCount Number of inputs required by the operation
     * @param bInSupportsSmoothing Whether this operation supports smoothing
     * @return Unique ID for the registered operation, or 0 if registration failed
     */
    uint32 RegisterOperation(
        const FName& InOperationName,
        ESDFOperationType InOperationType,
        uint32 InInputCount,
        bool bInSupportsSmoothing = false);

    /**
     * Registers a new field operation with the registry
     * @param InOperationName Name of the operation
     * @param InOperationType Type of operation
     * @param InInputCount Number of inputs required by the operation
     * @param bInSupportsSmoothing Whether this operation supports smoothing
     * @return Unique ID for the registered operation, or 0 if registration failed
     */
    uint32 RegisterFieldOperation(
        const FName& InOperationName,
        ESDFOperationType InOperationType,
        uint32 InInputCount,
        bool bInSupportsSmoothing = false);

    /**
     * Registers multiple field operations in a batch using parallel processing
     * @param OperationInfos Array of operation information to register
     * @param OutOperationIds Array to receive the assigned operation IDs
     * @param OutErrors Array to receive any errors that occurred during registration
     * @param Config Configuration for parallel execution
     * @return True if all operations were registered successfully
     */
    bool RegisterFieldOperationsBatch(
        const TArray<FSDFOperationInfo>& OperationInfos,
        TArray<uint32>& OutOperationIds,
        TArray<FString>& OutErrors,
        struct FParallelConfig Config = FParallelConfig());
        
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
     * Checks if an operation is registered by name
     * @param InOperationName Name of the operation
     * @return True if the operation is registered
     */
    bool IsOperationRegistered(const FName& InOperationName) const;
    
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
    
    /**
     * Begins asynchronous type registration from a source asset
     * @param SourceAsset Path to the asset containing field type definitions
     * @return Operation ID for tracking the async registration, 0 if failed
     */
    uint64 BeginAsyncTypeRegistration(const FString& SourceAsset);

    /**
     * Begins asynchronous batch registration of multiple field types
     * @param TypeInfos Array of field type information to register
     * @return Operation ID for tracking the async registration, 0 if failed
     */
    uint64 BeginAsyncFieldTypeBatchRegistration(const TArray<FSDFFieldTypeInfo>& TypeInfos);

    /**
     * Begins asynchronous batch registration of multiple operations
     * @param OperationInfos Array of operation information to register
     * @return Operation ID for tracking the async registration, 0 if failed
     */
    uint64 BeginAsyncOperationsBatchRegistration(const TArray<FSDFOperationInfo>& OperationInfos);

    /**
     * Registers for progress updates from an async type registration
     * @param OperationId ID of the async operation
     * @param Callback Delegate to call with progress updates
     * @param UpdateIntervalMs Minimum interval between updates in milliseconds
     * @return True if registration was successful
     */
    bool RegisterTypeRegistrationProgressCallback(uint64 OperationId, const FAsyncProgressDelegate& Callback, uint32 UpdateIntervalMs = 100);

    /**
     * Registers for completion notification from an async type registration
     * @param OperationId ID of the async operation
     * @param Callback Delegate to call when registration completes
     * @return True if registration was successful
     */
    bool RegisterTypeRegistrationCompletionCallback(uint64 OperationId, const FTypeRegistrationCompletionDelegate& Callback);

    /**
     * Cancels an in-progress async type registration
     * @param OperationId ID of the async operation to cancel
     * @param bWaitForCancellation Whether to wait for the operation to be fully cancelled
     * @return True if cancellation was successful or in progress
     */
    bool CancelAsyncTypeRegistration(uint64 OperationId, bool bWaitForCancellation = false);
    
private:
    /** Generates a unique type ID for new field type registrations */
    uint32 GenerateUniqueTypeId();
    
    /** Generates a unique operation ID for new field operation registrations */
    uint32 GenerateUniqueOperationId();
    
    /** Initializes a field type after registration */
    void InitializeFieldType(uint32 TypeId);
    
    /** Thread-safe lock for registry access */
    mutable TSharedPtr<FSpinLock> RegistryLock;
    
    /** Map of field types by ID */
    TMap<uint32, TSharedRef<FSDFFieldTypeInfo>> FieldTypeMap;
    
    /** Map of field types by name */
    TMap<FName, uint32> FieldTypeNameMap;
    
    /** Map of operations by ID */
    TMap<uint32, TSharedRef<FSDFOperationInfo>> OperationMap;
    
    /** Map of operations by name */
    TMap<FName, uint32> OperationNameMap;
    
    /** Map of shared buffer managers by type ID */
    TMap<uint32, TSharedRef<FSharedBufferManager>> TypeBufferMap;
    
    /** Map of type versions by type ID */
    TMap<uint32, uint32> TypeVersionMap;
    
    /** Counter for generating unique type IDs */
    FThreadSafeCounter NextTypeId;
    
    /** Counter for generating unique operation IDs */
    FThreadSafeCounter NextOperationId;
    
    /** Registry name */
    FName RegistryName;
    
    /** Schema version for this registry */
    uint32 SchemaVersion;
    
    /** Whether GPU support is available */
    bool bHasGPUSupport;
    
    /** Whether SSE2 support is available */
    bool bHasSSE2Support;
    
    /** Whether AVX support is available */
    bool bHasAVXSupport;
    
    /** Whether AVX2 support is available */
    bool bHasAVX2Support;
    
    /** Whether types have been initialized */
    bool bTypesInitialized;
    
    /** Whether initialization is in progress */
    bool bInitializationInProgress;
    
    /** Whether hardware capabilities have been detected */
    bool bHardwareCapabilitiesDetected;
    
    /** Errors encountered during initialization */
    TArray<FString> InitializationErrors;
    
    /** Singleton instance */
    static FSDFTypeRegistry* Singleton;
    
    /** Thread-safe initialization flag */
    static FThreadSafeBool bSingletonInitialized;
};