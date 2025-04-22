// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IRegistry.h"
#include "Containers/Map.h"
#include "Templates/SharedPointer.h"
#include "HAL/ThreadSafeBool.h"
#include "Interfaces/IServiceLocator.h"
#include "TypeVersionMigrationInfo.h"
#include "Interfaces/IMemoryManager.h"
#include "Interfaces/IPoolAllocator.h"
#include "ThreadSafety.h"
#include "../../3_ThreadingTaskSystem/Public/TaskScheduler.h"
#include "../../3_ThreadingTaskSystem/Public/AsyncTaskTypes.h"
#include "../../3_ThreadingTaskSystem/Public/ParallelExecutor.h"

// Forward declarations
class FTransactionManager;
class FTypeRegistrationOperation;
struct FTransactionStats;

/**
 * Transaction concurrency level for zone operations
 */
enum class ETransactionConcurrency : uint8
{
    /** Read-only transactions that don't modify state */
    ReadOnly,
    
    /** Read-write transactions with optimistic concurrency */
    Optimistic,
    
    /** Transactions that require exclusive access */
    Exclusive,
    
    /** Transactions that operate on a specific material channel */
    MaterialChannel
};

/**
 * Retry strategy type for transaction conflicts
 */
enum class ERetryStrategy : uint8
{
    /** No retry, fail immediately */
    None,
    
    /** Retry with fixed interval */
    FixedInterval,
    
    /** Retry with exponential backoff */
    ExponentialBackoff,
    
    /** Custom retry strategy with callback */
    Custom
};

/**
 * Transaction priority levels
 */
enum class ETransactionPriority : uint8
{
    /** Low priority transactions */
    Low = 0,
    
    /** Normal priority transactions */
    Normal = 128,
    
    /** High priority transactions */
    High = 192,
    
    /** Critical priority transactions */
    Critical = 255
};

/**
 * Zone conflict detection level for transaction operations
 */
enum class EZoneConflictDetectionLevel : uint8
{
    /** Optimistic conflict detection for low-contention scenarios */
    Optimistic,
    
    /** Pessimistic conflict detection for high-contention scenarios */
    Pessimistic,
    
    /** No conflict detection (use with caution) */
    None
};

/**
 * Zone operation priority levels
 */
enum class EZoneOperationPriority : uint8
{
    /** Low priority zone operations */
    Low = 0,
    
    /** Normal priority zone operations */
    Normal = 128,
    
    /** High priority zone operations */
    High = 192,
    
    /** Critical priority zone operations */
    Critical = 255
};

/**
 * Structure containing metadata for a zone transaction type
 */
struct MININGSPICECOPILOT_API FZoneTransactionTypeInfo
{
    /** Unique ID for this transaction type */
    uint32 TypeId;
    
    /** Name of this transaction type */
    FName TypeName;
    
    /** Concurrency level for this transaction type */
    ETransactionConcurrency ConcurrencyLevel;
    
    /** Retry strategy for conflicts */
    ERetryStrategy RetryStrategy;
    
    /** Maximum number of retry attempts */
    uint32 MaxRetries;
    
    /** Base retry interval in milliseconds */
    uint32 BaseRetryIntervalMs;
    
    /** Material channel ID for material-specific transactions */
    int32 MaterialChannelId;
    
    /** Priority for conflict resolution */
    uint32 ConflictPriority;
    
    /** Whether this transaction type requires version tracking */
    bool bRequiresVersionTracking;
    
    /** Whether this transaction type supports fast-path execution */
    bool bSupportsFastPath;
    
    /** Fast-path conflict probability threshold (0-1) */
    float FastPathThreshold;
    
    /** Whether this transaction has a read-validate-write pattern */
    bool bHasReadValidateWritePattern;
    
    /** Whether this transaction type supports thread-safe access */
    bool bSupportsThreadSafeAccess;
    
    /** Whether this transaction type supports partial processing */
    bool bSupportsPartialProcessing;
    
    /** Whether this transaction type supports incremental updates */
    bool bSupportsIncrementalUpdates;
    
    /** Whether this transaction type has low contention characteristics */
    bool bLowContention;
    
    /** Whether results from this transaction type can be merged */
    bool bSupportsResultMerging;
    
    /** Whether this transaction type supports asynchronous processing */
    bool bSupportsAsyncProcessing;
    
    /** Schema version for this transaction type */
    uint32 SchemaVersion;
    
    /** Historical conflict rates for adaptive optimization */
    TArray<float> HistoricalConflictRates;
    
    /** Total number of times this transaction type has been executed */
    uint32 TotalExecutions;
    
    /** Total number of conflicts encountered */
    uint32 ConflictCount;
    
    /** Whether this transaction type supports partial execution */
    bool bSupportsPartialExecution;
    
    /** Whether results can be merged in case of conflicts */
    bool bCanMergeResults;
    
    /** Transaction priority for scheduling */
    ETransactionPriority Priority;
    
    /** Whether this transaction type supports threading */
    bool bSupportsThreading;
    
    /** Default constructor */
    FZoneTransactionTypeInfo()
        : TypeId(0)
        , ConcurrencyLevel(ETransactionConcurrency::ReadOnly)
        , RetryStrategy(ERetryStrategy::None)
        , MaxRetries(3)
        , BaseRetryIntervalMs(100)
        , MaterialChannelId(-1)
        , ConflictPriority(0)
        , bRequiresVersionTracking(false)
        , bSupportsFastPath(false)
        , FastPathThreshold(0.1f)
        , bHasReadValidateWritePattern(false)
        , bSupportsThreadSafeAccess(false)
        , bSupportsPartialProcessing(false)
        , bSupportsIncrementalUpdates(false)
        , bLowContention(false)
        , bSupportsResultMerging(false)
        , bSupportsAsyncProcessing(false)
        , SchemaVersion(1)
        , TotalExecutions(0)
        , ConflictCount(0)
        , bSupportsPartialExecution(false)
        , bCanMergeResults(false)
        , Priority(ETransactionPriority::Normal)
        , bSupportsThreading(false)
    {
    }
};

/**
 * Structure containing configuration for a zone grid
 */
struct MININGSPICECOPILOT_API FZoneGridConfig
{
    /** Size of a zone in world units */
    float ZoneSize;
    
    /** Default zone configuration name */
    FName DefaultConfigName;
    
    /** Maximum number of concurrent transactions per zone */
    uint32 MaxConcurrentTransactions;
    
    /** Whether to use material-specific versioning */
    bool bUseMaterialSpecificVersioning;
    
    /** Number of versions to track in history */
    uint32 VersionHistoryLength;
};

/**
 * Structure containing information about a zone type
 */
struct MININGSPICECOPILOT_API FZoneTypeInfo
{
    /** Unique ID for this zone type */
    uint32 TypeId;
    
    /** Name of this zone type */
    FName TypeName;
    
    /** Schema version for this zone type */
    uint32 SchemaVersion;
    
    /** Whether this zone type supports threading */
    bool bSupportsThreading;
    
    /** Parent zone type ID (if any) */
    uint32 ParentZoneTypeId;
    
    /** List of supported material types */
    TArray<uint32> SupportedMaterialTypes;
    
    /** Default constructor */
    FZoneTypeInfo()
        : TypeId(0)
        , SchemaVersion(1)
        , bSupportsThreading(false)
        , ParentZoneTypeId(0)
    {
    }
};

/**
 * Zone hierarchy information for nested zones
 */
struct MININGSPICECOPILOT_API FZoneHierarchyInfo
{
    /** Parent zone ID */
    int32 ParentZoneId;
    
    /** Child zone IDs */
    TArray<int32> ChildZoneIds;
    
    /** Propagation policy for access control */
    bool bPropagateReadsToChildren;
    
    /** Propagation policy for writes */
    bool bPropagateWritesToChildren;
};

/** 
 * Delegate for transaction completion events 
 */
DECLARE_DELEGATE_TwoParams(FTransactionCompletionDelegate, uint32 /*TypeId*/, const FTransactionStats& /*Stats*/);

/**
 * Structure containing metadata for a zone operation type
 */
struct MININGSPICECOPILOT_API FZoneOperationTypeInfo
{
    /** Unique ID for this operation type */
    uint32 TypeId;
    
    /** Name of this operation type */
    FName TypeName;
    
    /** Associated transaction type ID */
    uint32 TransactionTypeId;
    
    /** Priority level for this operation */
    EZoneOperationPriority Priority;
    
    /** Whether this operation can be batched */
    bool bSupportsBatching;
    
    /** Whether this operation can be executed in parallel */
    bool bSupportsParallelExecution;
    
    /** Default constructor */
    FZoneOperationTypeInfo()
        : TypeId(0)
        , TransactionTypeId(0)
        , Priority(EZoneOperationPriority::Normal)
        , bSupportsBatching(false)
        , bSupportsParallelExecution(false)
    {
    }
};

/**
 * Type registration completion delegate
 */
DECLARE_DELEGATE_OneParam(FTypeRegistrationCompletionDelegate, bool /*bSuccess*/);

/**
 * Registry for zone transaction types in the mining system
 * Handles transaction type registration, zone configuration, and concurrency metadata
 */
class MININGSPICECOPILOT_API FZoneTypeRegistry : public IRegistry
{
public:
    /** Default constructor */
    FZoneTypeRegistry();
    
    /** Destructor */
    virtual ~FZoneTypeRegistry();
    
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
     * Gets the extended capabilities of a specific transaction type
     * @param TypeId The ID of the transaction type to query
     * @return The extended capabilities of the transaction type
     */
    virtual ETypeCapabilitiesEx GetTypeCapabilitiesEx(uint32 TypeId) const override;
    
    virtual uint64 ScheduleTypeTask(uint32 TypeId, TFunction<void()> TaskFunc, const FTaskConfig& Config) override;
    virtual bool PreInitializeTypes() override;
    virtual bool ParallelInitializeTypes(bool bParallel = true) override;
    virtual bool PostInitializeTypes() override;
    virtual TArray<int32> GetTypeDependencies(uint32 TypeId) const override;
    //~ End IRegistry Interface
    
    /**
     * Registers a new zone transaction type with the registry
     * @param InTypeName Name of the transaction type
     * @param InConcurrencyLevel Concurrency level for this transaction type
     * @param InRetryStrategy Retry strategy for conflicts
     * @return Unique ID for the registered type, or 0 if registration failed
     */
    uint32 RegisterTransactionType(
        const FName& InTypeName,
        ETransactionConcurrency InConcurrencyLevel = ETransactionConcurrency::Optimistic,
        ERetryStrategy InRetryStrategy = ERetryStrategy::ExponentialBackoff);
    
    /**
     * Registers a material-specific transaction type
     * @param InTypeName Name of the transaction type
     * @param InMaterialChannelId Material channel ID
     * @return Unique ID for the registered type, or 0 if registration failed
     */
    uint32 RegisterMaterialTransaction(
        const FName& InTypeName,
        int32 InMaterialChannelId);
    
    /**
     * Registers a zone grid configuration
     * @param InConfigName Name of the configuration
     * @param InZoneSize Size of a zone in world units
     * @param InMaxConcurrentTransactions Maximum number of concurrent transactions per zone
     * @return True if registration was successful
     */
    bool RegisterZoneGridConfig(
        const FName& InConfigName,
        float InZoneSize = 2.0f,
        uint32 InMaxConcurrentTransactions = 16);
    
    /**
     * Gets information about a registered transaction type
     * @param InTypeId Unique ID of the transaction type
     * @return Pointer to transaction type info, or nullptr if not found
     */
    const FZoneTransactionTypeInfo* GetTransactionTypeInfo(uint32 InTypeId) const;
    
    /**
     * Gets information about a registered transaction type by name
     * @param InTypeName Name of the transaction type
     * @return Pointer to transaction type info, or nullptr if not found
     */
    const FZoneTransactionTypeInfo* GetTransactionTypeInfoByName(const FName& InTypeName) const;
    
    /**
     * Gets a registered zone grid configuration
     * @param InConfigName Name of the configuration
     * @return Pointer to zone grid config, or nullptr if not found
     */
    const FZoneGridConfig* GetZoneGridConfig(const FName& InConfigName) const;
    
    /**
     * Gets the default zone grid configuration
     * @return Pointer to default zone grid config
     */
    const FZoneGridConfig* GetDefaultZoneGridConfig() const;
    
    /**
     * Sets the default zone grid configuration
     * @param InConfigName Name of the configuration to set as default
     * @return True if successful
     */
    bool SetDefaultZoneGridConfig(const FName& InConfigName);
    
    /**
     * Updates a transaction type's properties
     * @param InTypeId Transaction type ID
     * @param InPropertyName Property name
     * @param InValue Property value
     * @return True if update was successful
     */
    bool UpdateTransactionProperty(uint32 InTypeId, const FName& InPropertyName, const FString& InValue);
    
    /**
     * Updates a transaction type's fast-path threshold based on conflict history
     * @param InTypeId Transaction type ID
     * @param InConflictRate Observed conflict rate (0-1)
     * @return True if update was successful
     */
    bool UpdateFastPathThreshold(uint32 InTypeId, float InConflictRate);
    
    /**
     * Updates conflict statistics for a transaction type
     * @param InTypeId Transaction type ID
     * @param InNewRate New conflict rate to record
     * @return True if statistics were updated successfully
     */
    bool UpdateConflictRate(uint32 InTypeId, float InNewRate);
    
    /**
     * Registers a parent-child relationship between zones
     * @param ParentZoneId Parent zone ID
     * @param ChildZones Array of child zone IDs
     * @return True if registration was successful
     */
    bool RegisterZoneHierarchy(int32 ParentZoneId, const TArray<int32>& ChildZones);
    
    /**
     * Gets the child zones for a parent zone
     * @param ParentZoneId Parent zone ID
     * @return Array of child zone IDs, or empty array if none found
     */
    TArray<int32> GetChildZones(int32 ParentZoneId) const;
    
    /**
     * Gets the parent zone for a child zone
     * @param ChildZoneId Child zone ID
     * @return Parent zone ID, or INDEX_NONE if no parent exists
     */
    int32 GetParentZone(int32 ChildZoneId) const;
    
    /**
     * Callback handler for transaction completion
     * @param TypeId Transaction type ID
     * @param Stats Transaction statistics
     */
    void OnTransactionCompleted(uint32 TypeId, const FTransactionStats& Stats);
    
    /**
     * Gets all registered zone types
     * @return Array of all zone type infos
     */
    TArray<FZoneTypeInfo> GetAllZoneTypes() const;
    
    /**
     * Checks if a transaction type is registered
     * @param InTypeId Unique ID of the transaction type
     * @return True if the transaction type is registered
     */
    bool IsTransactionTypeRegistered(uint32 InTypeId) const;
    
    /**
     * Checks if a transaction type is registered by name
     * @param InTypeName Name of the transaction type
     * @return True if the type is registered
     */
    bool IsTransactionTypeRegistered(const FName& InTypeName) const;
    
    /**
     * Checks if a zone type is registered
     * @param InTypeId Unique ID of the zone type
     * @return True if the type is registered
     */
    bool IsZoneTypeRegistered(uint32 InTypeId) const;
    
    /** Gets the singleton instance of the zone type registry */
    static FZoneTypeRegistry& Get();

    /**
     * Begins asynchronous type registration from a source asset
     * @param SourceAsset Path to the asset containing type definitions
     * @return Operation ID for tracking the async registration, 0 if failed
     */
    uint64 BeginAsyncTypeRegistration(const FString& SourceAsset);

    /**
     * Begins asynchronous batch registration of multiple transaction types
     * @param TypeInfos Array of transaction type information to register
     * @return Operation ID for tracking the async registration, 0 if failed
     */
    uint64 BeginAsyncTransactionTypeBatchRegistration(const TArray<FZoneTransactionTypeInfo>& TypeInfos);

    /**
     * Registers a callback for progress updates during async type registration
     * @param OperationId The ID of the operation to monitor
     * @param Callback The callback to invoke with progress updates
     * @param UpdateIntervalMs How often to update progress (in milliseconds)
     * @return True if the callback was registered successfully
     */
    bool RegisterTypeRegistrationProgressCallback(uint64 OperationId, const FAsyncProgressDelegate& Callback, uint32 UpdateIntervalMs = 100);

    /**
     * Registers a callback for completion of async type registration
     * @param OperationId The ID of the operation to monitor
     * @param Callback The callback to invoke when the operation completes
     * @return True if the callback was registered successfully
     */
    bool RegisterTypeRegistrationCompletionCallback(uint64 OperationId, const FTypeRegistrationCompletionDelegate& Callback);

    /**
     * Cancels an ongoing async type registration
     * @param OperationId ID of the async operation to cancel
     * @param bWaitForCancellation Whether to wait for the operation to be fully cancelled
     * @return True if cancellation was successful or in progress
     */
    bool CancelAsyncTypeRegistration(uint64 OperationId, bool bWaitForCancellation = false);

    /**
     * Registers a new transaction type with the registry
     * @param InTypeName Name of the transaction type
     * @param InConflictDetectionLevel Level of conflict detection required
     * @param InFastPathThreshold Threshold for using the fast path
     * @param bInSupportsPreemption Whether this transaction supports preemption
     * @return Unique ID for the registered type, or 0 if registration failed
     */
    uint32 RegisterTransactionType(
        const FName& InTypeName, 
        EZoneConflictDetectionLevel InConflictDetectionLevel, 
        float InFastPathThreshold = 0.75f, 
        bool bInSupportsPreemption = false);
    
    /**
     * Registers multiple transaction types in a batch using parallel processing
     * @param TypeInfos Array of transaction type information to register
     * @param OutTypeIds Array to receive the assigned type IDs
     * @param OutErrors Array to receive any errors that occurred during registration
     * @param Config Configuration for parallel execution
     * @return True if all types were registered successfully
     */
    bool RegisterTransactionTypesBatch(
        const TArray<FZoneTransactionTypeInfo>& TypeInfos,
        TArray<uint32>& OutTypeIds,
        TArray<FString>& OutErrors,
        struct FParallelConfig Config = FParallelConfig());
    
    /**
     * Pre-validates a batch of transaction types before registration
     * @param TypeInfos Array of transaction type information to validate
     * @param OutErrors Array to receive validation errors
     * @param bParallel Whether to validate in parallel
     * @return True if all types passed validation
     */
    bool PrevalidateTransactionTypes(
        const TArray<FZoneTransactionTypeInfo>& TypeInfos,
        TArray<FString>& OutErrors,
        bool bParallel = true);
    
    /**
     * Validates the consistency of all registered transaction types
     * @param OutErrors Array to receive any validation errors
     * @param bParallel Whether to validate in parallel
     * @return True if all types are consistent
     */
    bool ValidateTypeConsistency(
        TArray<FString>& OutErrors,
        bool bParallel = true);
    
    /**
     * Registers a new zone operation with the registry
     * @param InTypeName Name of the zone operation type
     * @param InTransactionTypeId ID of the transaction type this operation belongs to
     * @param InPriority Priority of this operation type
     * @return Unique ID for the registered type, or 0 if registration failed
     */
    uint32 RegisterZoneOperationType(
        const FName& InTypeName,
        uint32 InTransactionTypeId,
        EZoneOperationPriority InPriority = EZoneOperationPriority::Normal);
    
    /**
     * Registers multiple zone operation types in a batch using parallel processing
     * @param TypeInfos Array of zone operation type information to register
     * @param OutTypeIds Array to receive the assigned type IDs
     * @param OutErrors Array to receive any errors that occurred during registration
     * @param Config Configuration for parallel execution
     * @return True if all types were registered successfully
     */
    bool RegisterZoneOperationTypesBatch(
        const TArray<FZoneOperationTypeInfo>& TypeInfos,
        TArray<uint32>& OutTypeIds,
        TArray<FString>& OutErrors,
        struct FParallelConfig Config = FParallelConfig());

private:
    /** Generates a unique type ID for new transaction type registrations */
    uint32 GenerateUniqueTypeId();
    
    /** Initializes a transaction type */
    void InitializeTransactionType(uint32 TypeId);
    
    /** Lock for the registry */
    mutable FSpinLock RegistryLock;
    
    /** Lock for pending operations */
    mutable FSpinLock PendingOperationsLock;
    
    /** Map of pending type registration operations */
    TMap<uint64, TSharedPtr<FTypeRegistrationOperation>> PendingOperations;
    
    /** Next available type ID */
    FThreadSafeCounter NextTypeId;

    /** Map of transaction types by ID */
    TMap<uint32, TSharedRef<FZoneTransactionTypeInfo>> TransactionTypeMap;
    
    /** Map of transaction type names to IDs */
    TMap<FName, uint32> TransactionTypeNameMap;
    
    /** Map of zone grid configurations by name */
    TMap<FName, TSharedRef<FZoneGridConfig>> ZoneConfigMap;
    
    /** Default zone grid configuration name */
    FName DefaultZoneConfigName;
    
    /** Map of zone types by ID */
    TMap<uint32, TSharedRef<FZoneTypeInfo>> ZoneTypeMap;
    
    /** Map of zone type names to IDs */
    TMap<FName, uint32> ZoneTypeNameMap;
    
    /** Map of zone operation types by ID */
    TMap<uint32, TSharedRef<FZoneOperationTypeInfo>> ZoneOperationTypeMap;
    
    /** Map of zone operation type names to IDs */
    TMap<FName, uint32> ZoneOperationTypeNameMap;
    
    /** Map of parent zones to child zones */
    TMap<int32, TArray<int32>> ZoneHierarchy;
    
    /** Map of child zones to parent zones */
    TMap<int32, int32> ChildToParentMap;
    
    /** Flag indicating whether the registry has been initialized */
    FThreadSafeBool bIsInitialized;
    
    /** Singleton instance */
    static FZoneTypeRegistry* Singleton;
    
    /** Thread-safe initialization flag */
    static FThreadSafeBool bSingletonInitialized;
};