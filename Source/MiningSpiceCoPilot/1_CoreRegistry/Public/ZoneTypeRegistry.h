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

// Forward declarations
class FTransactionManager;
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
    virtual uint64 ScheduleTypeTask(uint32 TypeId, TFunction<void()> TaskFunc, const FTaskConfig& Config) override;
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
     * Checks if a transaction type is registered
     * @param InTypeId Unique ID of the transaction type
     * @return True if the type is registered
     */
    bool IsTransactionTypeRegistered(uint32 InTypeId) const;
    
    /**
     * Checks if a transaction type is registered by name
     * @param InTypeName Name of the transaction type
     * @return True if the type is registered
     */
    bool IsTransactionTypeRegistered(const FName& InTypeName) const;
    
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

private:
    /** Generates a unique type ID for new transaction type registrations */
    uint32 GenerateUniqueTypeId();
    
    /** Map of registered transaction types by ID */
    TMap<uint32, TSharedRef<FZoneTransactionTypeInfo>> TransactionTypeMap;
    
    /** Map of transaction type names to IDs for quick lookup */
    TMap<FName, uint32> TransactionTypeNameMap;
    
    /** Map of zone grid configurations by name */
    TMap<FName, TSharedRef<FZoneGridConfig>> ZoneGridConfigMap;
    
    /** Name of the default zone grid configuration */
    FName DefaultConfigName;
    
    /** Map of zone hierarchies by parent ID */
    TMap<int32, TArray<int32>> ZoneHierarchy;
    
    /** Reverse map of child to parent zones */
    TMap<int32, int32> ChildToParentMap;
    
    /** Registry name */
    FName RegistryName;
    
    /** Schema version for this registry */
    uint32 SchemaVersion;
    
    /** Next available type ID */
    FThreadSafeCounter NextTypeId;
    
    /** Version counter for type registrations */
    FThreadSafeCounter TypeVersion;
    
    /** Lock for thread safety */
    FSpinLock RegistryLock;
    
    /** Lock for initialization */
    FSpinLock InitializationLock;
    
    /** Initialization flag */
    bool bIsInitialized;
    
    /** Map of transaction completion callbacks */
    TMap<uint32, FTransactionCompletionDelegate> CompletionCallbacks;
    
    /** Singleton instance */
    static FZoneTypeRegistry* Instance;
};