// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IRegistry.h"
#include "Containers/Map.h"
#include "Templates/SharedPointer.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/CriticalSection.h"
#include "SDFTypeRegistry.h" // Include the SDFTypeRegistry to use its ESIMD_InstructionSet enum
#include "Interfaces/IServiceLocator.h"
#include "TypeVersionMigrationInfo.h"
#include "Interfaces/IMemoryManager.h"
#include "Interfaces/IPoolAllocator.h"
#include "HAL/ThreadSafeCounter.h" // For atomic operations
#include "ThreadSafety.h"
#include "../../3_ThreadingTaskSystem/Public/TaskScheduler.h"
#include "../../3_ThreadingTaskSystem/Public/AsyncTaskTypes.h"

/**
 * SVO node class types for classification in the registry
 */
enum class ESVONodeClass : uint8
{
    /** Homogeneous nodes with a single material throughout */
    Homogeneous,
    
    /** Interface nodes that contain multiple materials */
    Interface,
    
    /** Empty nodes with no material */
    Empty,
    
    /** Custom node type for specialized behavior */
    Custom
};

/**
 * Memory layout types for optimal access patterns
 */
enum class ESVOMemoryLayout : uint8
{
    Sequential,
    Interleaved,
    Tiled
};

/**
 * Capabilities for SVO node types
 */
enum class ESVONodeCapabilities : uint32
{
    None = 0x0,
    SupportsSerialization = 0x1,
    SupportsSIMD = 0x2,
    SupportsConcurrentAccess = 0x4,
    SupportsHotReload = 0x8
};

/**
 * Structure containing metadata for an SVO node type
 * Memory aligned for SIMD operations
 */
struct MININGSPICECOPILOT_API FSVONodeTypeInfo
{
    /** Unique ID for this node type */
    uint32 TypeId;
    
    /** Name of this node type */
    FName TypeName;
    
    /** Classification of this node type */
    ESVONodeClass NodeClass;
    
    /** Version of this node type's schema */
    uint32 SchemaVersion;
    
    /** Memory alignment requirements for this node type */
    uint32 AlignmentRequirement;
    
    /** Size of the node type data in bytes */
    uint32 DataSize;
    
    /** Whether this node type supports material relationships */
    bool bSupportsMaterialRelationships;
    
    /** Whether this node type supports serialization */
    bool bSupportsSerializaton;
    
    /** Whether this node type supports SIMD operations */
    bool bSupportsSIMD;
    
    /** SIMD instruction set required for optimized operations */
    ESIMD_InstructionSet RequiredInstructionSet;
    
    /** Memory layout type for optimal access patterns */
    ESVOMemoryLayout MemoryLayout;
    
    /** Whether this node type supports concurrent access */
    bool bSupportsConcurrentAccess;
    
    /** Whether this node type supports threading */
    bool bSupportsThreading;
    
    /** Whether this node type supports spatial coherence */
    bool bSupportsSpatialCoherence;
    
    /** Whether this node type supports incremental updates */
    bool bSupportsIncrementalUpdates;
    
    /** Whether this node type uses optimized memory access patterns */
    bool bOptimizedMemoryAccess;
    
    /** Capabilities flags for this node type (bitwise combination of ESVONodeCapabilities) */
    uint32 CapabilitiesFlags;
    
    /** Preferred thread block size for parallel processing */
    uint32 PreferredThreadBlockSize;
    
    /** Whether this node type supports hot-reloading */
    bool bSupportsHotReload;

    /** UE Class type for blueprint access (if applicable) */
    TSoftClassPtr<UObject> BlueprintClassType;
    
    /** Default constructor */
    FSVONodeTypeInfo()
        : TypeId(0)
        , NodeClass(ESVONodeClass::Empty)
        , SchemaVersion(1)
        , AlignmentRequirement(16)
        , DataSize(0)
        , bSupportsMaterialRelationships(false)
        , bSupportsSerializaton(true)
        , bSupportsSIMD(false)
        , RequiredInstructionSet(ESIMD_InstructionSet::SSE2)
        , MemoryLayout(ESVOMemoryLayout::Sequential)
        , bSupportsConcurrentAccess(false)
        , bSupportsThreading(false)
        , bSupportsSpatialCoherence(false)
        , bSupportsIncrementalUpdates(false)
        , bOptimizedMemoryAccess(false)
        , CapabilitiesFlags(0)
        , PreferredThreadBlockSize(64)
        , bSupportsHotReload(false)
    {}
    
    /** Helper method to check if this node type has a specific capability */
    bool HasCapability(ESVONodeCapabilities Capability) const
    {
        return (CapabilitiesFlags & static_cast<uint32>(Capability)) != 0;
    }
    
    /** Helper method to add a capability to this node type */
    void AddCapability(ESVONodeCapabilities Capability)
    {
        CapabilitiesFlags |= static_cast<uint32>(Capability);
    }
};

/**
 * Registry for SVO node types in the mining system
 * Handles type registration, node classification, and memory layout management
 */
class MININGSPICECOPILOT_API FSVOTypeRegistry : public IRegistry
{
public:
    /** Default constructor */
    FSVOTypeRegistry();
    
    /** Destructor */
    virtual ~FSVOTypeRegistry();
    
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
    virtual uint64 ScheduleTypeTask(uint32 TypeId, TFunction<void()> TaskFunc, const FTaskConfig& Config) override;
    //~ End IRegistry Interface
    
    /**
     * Registers a new SVO node type with the registry
     * @param InTypeName Name of the node type
     * @param InNodeClass Classification of this node type
     * @param InDataSize Size of the node data in bytes
     * @param InAlignmentRequirement Memory alignment requirement (must be power of 2)
     * @param bInSupportsMaterialRelationships Whether this node supports material relationships
     * @return Unique ID for the registered type, or 0 if registration failed
     */
    uint32 RegisterNodeType(
        const FName& InTypeName, 
        ESVONodeClass InNodeClass, 
        uint32 InDataSize, 
        uint32 InAlignmentRequirement = 16, 
        bool bInSupportsMaterialRelationships = false);
    
    /**
     * Attempts to register a node type using optimistic locking for better performance in hot paths
     * Falls back to regular registration if optimistic approach fails
     * @param InTypeName Name of the node type
     * @param InNodeClass Classification of this node type
     * @param InDataSize Size of the node data in bytes
     * @param InAlignmentRequirement Memory alignment requirement (must be power of 2)
     * @param bInSupportsMaterialRelationships Whether this node supports material relationships
     * @return True if optimistic registration succeeded, false if it failed (will need regular registration)
     */
    bool TryOptimisticRegisterNodeType(
        const FName& InTypeName, 
        ESVONodeClass InNodeClass, 
        uint32 InDataSize, 
        uint32 InAlignmentRequirement = 16, 
        bool bInSupportsMaterialRelationships = false);
    
    /**
     * Synchronizes memory pool creation across threads to reduce contention
     * @param TypeId The type ID to synchronize pool creation for
     */
    void SynchronizePoolCreation(uint32 TypeId);
    
    /**
     * Gets information about a registered node type
     * @param InTypeId Unique ID of the node type
     * @return Pointer to node type info, or nullptr if not found
     */
    const FSVONodeTypeInfo* GetNodeTypeInfo(uint32 InTypeId) const;
    
    /**
     * Gets information about a registered node type by name
     * @param InTypeName Name of the node type
     * @return Pointer to node type info, or nullptr if not found
     */
    const FSVONodeTypeInfo* GetNodeTypeInfoByName(const FName& InTypeName) const;
    
    /**
     * Gets all registered node types
     * @return Array of all node type infos
     */
    TArray<FSVONodeTypeInfo> GetAllNodeTypes() const;
    
    /**
     * Gets all registered node types of a specific class
     * @param InNodeClass Classification to filter by
     * @return Array of matching node type infos
     */
    TArray<FSVONodeTypeInfo> GetNodeTypesByClass(ESVONodeClass InNodeClass) const;
    
    /**
     * Checks if a node type is registered
     * @param InTypeId Unique ID of the node type
     * @return True if the type is registered
     */
    bool IsNodeTypeRegistered(uint32 InTypeId) const;
    
    /**
     * Checks if a node type is registered by name
     * @param InTypeName Name of the node type
     * @return True if the type is registered
     */
    bool IsNodeTypeRegistered(const FName& InTypeName) const;

    /**
     * Registers additional capabilities for a node type
     * @param TypeId The node type ID
     * @param Capabilities The capabilities to add to the node type
     * @return True if the capabilities were registered successfully
     */
    bool RegisterCapabilities(uint32 TypeId, uint32 Capabilities);
    
    /**
     * Optimizes memory layout for a specific node type
     * Analyzes field access patterns and configures for cache coherence
     * @param TypeId The node type ID to optimize
     * @param bUseZOrderCurve Whether to use Z-order curve for spatial data
     * @param bEnablePrefetching Whether to enable memory prefetching
     * @return True if layout optimization was successful
     */
    bool OptimizeNodeLayout(uint32 TypeId, bool bUseZOrderCurve = true, bool bEnablePrefetching = true);
    
    /**
     * Checks if the specified SIMD instruction set is supported by the current hardware
     * @param InInstructionSet SIMD instruction set to check
     * @return True if the instruction set is supported
     */
    bool IsSIMDInstructionSetSupported(ESIMD_InstructionSet InInstructionSet) const;
    
    /** Gets the singleton instance of the SVO type registry */
    static FSVOTypeRegistry& Get();
    
    /**
     * Begins asynchronous type registration from a source asset
     * @param SourceAsset Path to the asset containing node type definitions
     * @return Operation ID for tracking the async registration, 0 if failed
     */
    uint64 BeginAsyncTypeRegistration(const FString& SourceAsset);

    /**
     * Begins asynchronous batch registration of multiple node types
     * @param TypeInfos Array of node type information to register
     * @return Operation ID for tracking the async registration, 0 if failed
     */
    uint64 BeginAsyncNodeTypeBatchRegistration(const TArray<FSVONodeTypeInfo>& TypeInfos);

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
    /** Generates a unique type ID for new registrations */
    uint32 GenerateUniqueTypeId();
    
    /** Detect CPU SIMD capabilities */
    void DetectSIMDCapabilities();
    
    /**
     * Creates a type-specific memory pool for a registered node type
     * @param TypeInfo Node type information
     */
    void CreateTypeSpecificPool(const TSharedRef<FSVONodeTypeInfo>& TypeInfo);
    
    /**
     * Configures memory pool capabilities based on node type characteristics
     * @param PoolManager Memory pool manager
     * @param TypeInfo Node type information
     */
    void ConfigurePoolCapabilities(class FMemoryPoolManager* PoolManager, const TSharedRef<FSVONodeTypeInfo>& TypeInfo);
    
    /** Map of registered node types by ID */
    TMap<uint32, TSharedRef<FSVONodeTypeInfo>> NodeTypeMap;
    
    /** Map of node type names to IDs for fast lookup */
    TMap<FName, uint32> NodeTypeNameMap;
    
    /** Counter for generating new type IDs */
    FThreadSafeCounter NextTypeId;
    
    /** Thread-safe flag indicating if the registry is initialized */
    FThreadSafeBool bIsInitialized;
    
    /** Current schema version for the SVO type system */
    uint32 SchemaVersion;
    
    /** Flag indicating if SIMD capabilities have been detected */
    bool bSIMDCapabilitiesDetected;
    
    /** Counter for tracking memory pool contention */
    FThreadSafeCounter PoolContentionCount;
    
    /** Flags indicating which SIMD instruction sets are available */
    bool bSupportsSSE2;
    
    /** Counter for tracking optimistic lock failures */
    FThreadSafeCounter OptimisticLockFailures;
    
    bool bSupportsAVX;
    bool bSupportsAVX2;
    bool bSupportsAVX512;
    
    /** Lock for thread-safe access to the registry data */
    mutable FSpinLock RegistryLock;
    
    /** Singleton instance of the registry */
    static FSVOTypeRegistry* Singleton;
    
    /** Thread-safe flag for singleton initialization */
    static FThreadSafeBool bSingletonInitialized;
};