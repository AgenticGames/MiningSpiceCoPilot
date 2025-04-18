// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Utils/SimpleSpinLock.h" // Replace the Misc/SpinLock.h include with our custom implementation
#include "ITransactionManager.generated.h"



/**
 * Transaction conflict resolution strategy
 */
enum class EConflictResolution : uint8
{
    /** Abort and retry the transaction */
    Retry,
    
    /** Abort the transaction without retrying */
    Abort,
    
    /** Force the transaction through (caution: may cause inconsistencies) */
    Force,
    
    /** Merge changes with the latest version */
    Merge
};

/**
 * Transaction isolation level
 */
enum class ETransactionIsolation : uint8
{
    /** Read uncommitted data (may see partial updates) */
    ReadUncommitted,
    
    /** Read only committed data */
    ReadCommitted,
    
    /** Repeatable reads (values won't change during transaction) */
    RepeatableRead,
    
    /** Serializable (strongest isolation, prevents phantom reads) */
    Serializable
};

/**
 * Transaction status
 */
enum class ETransactionStatus : uint8
{
    /** Transaction is in progress */
    InProgress,
    
    /** Transaction has been successfully committed */
    Committed,
    
    /** Transaction has been aborted */
    Aborted,
    
    /** Transaction is in the process of committing */
    Committing,
    
    /** Transaction is in the process of aborting */
    Aborting,
    
    /** Transaction has been created but not started */
    NotStarted,
    
    /** Transaction is in an invalid state */
    Invalid
};

/**
 * Transaction conflict type
 */
enum class ETransactionConflictType : uint8
{
    /** Version mismatch (optimistic concurrency conflict) */
    VersionMismatch,
    
    /** Lock conflict (pessimistic concurrency conflict) */
    LockConflict,
    
    /** Resource conflict (insufficient resources) */
    ResourceConflict,
    
    /** Deadlock detected */
    Deadlock,
    
    /** Custom conflict type */
    Custom
};

/**
 * Transaction conflict information
 */
struct MININGSPICECOPILOT_API FTransactionConflict
{
    /** ID of the zone where conflict occurred */
    int32 ZoneId;
    
    /** ID of the material where conflict occurred (INDEX_NONE for zone-level conflict) */
    int32 MaterialId;
    
    /** Version that was expected */
    uint32 ExpectedVersion;
    
    /** Version that was found */
    uint32 ActualVersion;
    
    /** ID of the conflicting transaction (0 if unknown) */
    uint64 ConflictingTransactionId;
    
    /** Whether this is a read conflict or write conflict */
    bool bIsReadConflict;
    
    /** Whether this is a critical conflict that must be resolved */
    bool bIsCritical;
    
    /** Type of conflict that occurred */
    ETransactionConflictType ConflictType;
};

/**
 * Transaction version record for optimistic concurrency
 */
struct MININGSPICECOPILOT_API FVersionRecord
{
    /** ID of the zone being accessed */
    int32 ZoneId;
    
    /** ID of the material being accessed (INDEX_NONE for zone-level access) */
    int32 MaterialId;
    
    /** Version observed at read time */
    uint32 Version;
    
    /** Whether this is a read-only or read-write access */
    bool bIsReadOnly;
};

/**
 * Transaction statistics
 */
struct MININGSPICECOPILOT_API FTransactionStats
{
    /** Start time in milliseconds since epoch */
    double StartTimeMs;
    
    /** Total execution time in milliseconds */
    double ExecutionTimeMs;
    
    /** Time spent in commit phase in milliseconds */
    double CommitTimeMs;
    
    /** Time spent in validation phase in milliseconds */
    double ValidationTimeMs;
    
    /** Number of zones accessed */
    uint32 ZoneAccessCount;
    
    /** Number of retry attempts */
    uint32 RetryCount;
    
    /** Number of materials accessed */
    uint32 MaterialAccessCount;
    
    /** Number of conflicts encountered */
    uint32 ConflictCount;
    
    /** Time spent waiting for locks in milliseconds */
    double LockWaitTimeMs;
    
    /** Number of validation operations performed */
    uint32 ValidationCount;
    
    /** Read set size (number of version records) */
    uint32 ReadSetSize;
    
    /** Write set size (number of version records with write access) */
    uint32 WriteSetSize;
    
    /** Transaction size in bytes */
    uint64 TransactionSizeBytes;
    
    /** Peak memory usage in bytes */
    uint64 PeakMemoryBytes;
};

/**
 * Transaction configuration
 */
struct MININGSPICECOPILOT_API FTransactionConfig
{
    /** Transaction type ID for statistics and optimizations */
    uint32 TypeId;
    
    /** Priority of the transaction */
    uint8 Priority;
    
    /** Maximum number of retry attempts */
    uint32 MaxRetries;
    
    /** Base retry interval in milliseconds */
    uint32 BaseRetryIntervalMs;
    
    /** Whether to use exponential backoff for retries */
    bool bUseExponentialBackoff;
    
    /** Isolation level for this transaction */
    ETransactionIsolation IsolationLevel;
    
    /** Whether to use fast path for this transaction type */
    bool bUseFastPath;
    
    /** Whether this transaction is read-only */
    bool bReadOnly;
    
    /** Maximum execution time in milliseconds (0 for no limit) */
    uint32 MaxExecutionTimeMs;
    
    /** Whether to automatically retry on conflict */
    bool bAutoRetry;
    
    /** Optional conflict resolution strategy to use instead of retry */
    EConflictResolution ConflictStrategy;
    
    /** Whether to record detailed statistics for this transaction */
    bool bRecordStatistics;
    
    /** Constructor with default values */
    FTransactionConfig()
        : TypeId(0)
        , Priority(128) // Medium priority
        , MaxRetries(3)
        , BaseRetryIntervalMs(10)
        , bUseExponentialBackoff(true)
        , IsolationLevel(ETransactionIsolation::ReadCommitted)
        , bUseFastPath(true)
        , bReadOnly(false)
        , MaxExecutionTimeMs(0)
        , bAutoRetry(true)
        , ConflictStrategy(EConflictResolution::Retry)
        , bRecordStatistics(false)
    {
    }
};

/**
 * Transaction context for executing operations within a transaction
 */
class MININGSPICECOPILOT_API FMiningTransactionContext
{
public:
    /**
     * Gets the unique ID of this transaction
     * @return Transaction ID
     */
    virtual uint64 GetTransactionId() const = 0;
    
    /**
     * Gets the current status of this transaction
     * @return Transaction status
     */
    virtual ETransactionStatus GetStatus() const = 0;
    
    /**
     * Adds a zone to the read set of this transaction
     * @param ZoneId ID of the zone to read
     * @param MaterialId ID of the material to read (INDEX_NONE for zone-level access)
     * @return True if the zone was successfully added to the read set
     */
    virtual bool AddToReadSet(int32 ZoneId, int32 MaterialId = INDEX_NONE) = 0;
    
    /**
     * Adds a zone to the write set of this transaction
     * @param ZoneId ID of the zone to write
     * @param MaterialId ID of the material to write (INDEX_NONE for zone-level access)
     * @return True if the zone was successfully added to the write set
     */
    virtual bool AddToWriteSet(int32 ZoneId, int32 MaterialId = INDEX_NONE) = 0;
    
    /**
     * Gets statistics for this transaction
     * @return Transaction statistics
     */
    virtual FTransactionStats GetStats() const = 0;
    
    /**
     * Gets the configuration for this transaction
     * @return Transaction configuration
     */
    virtual const FTransactionConfig& GetConfig() const = 0;
    
    /**
     * Gets conflicts that occurred during this transaction
     * @return Array of transaction conflicts
     */
    virtual TArray<FTransactionConflict> GetConflicts() const = 0;
    
    /**
     * Sets a name for this transaction for debugging purposes
     * @param Name Name to set
     */
    virtual void SetName(const FString& Name) = 0;
    
    /**
     * Gets the name of this transaction
     * @return Transaction name
     */
    virtual FString GetName() const = 0;
    
    /** Virtual destructor */
    virtual ~FMiningTransactionContext() {}
};

/**
 * Base interface for transaction managers in the SVO+SDF mining architecture
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UTransactionManager : public UInterface
{
    GENERATED_BODY()
};

// Forward declare the spinlock class
class FSimpleSpinLock;

/**
 * Interface for transaction management in the SVO+SDF mining architecture
 * Provides zone-based transaction management for concurrent mining operations
 */
class MININGSPICECOPILOT_API ITransactionManager
{
    GENERATED_BODY()

public:
    /**
     * Initializes the transaction manager
     * @return True if initialization was successful
     */
    virtual bool Initialize() = 0;
    
    /**
     * Shuts down the transaction manager and cleans up resources
     */
    virtual void Shutdown() = 0;
    
    /**
     * Checks if the transaction manager has been initialized
     * @return True if initialized, false otherwise
     */
    virtual bool IsInitialized() const = 0;
    
    /**
     * Begins a new transaction
     * @param Config Transaction configuration
     * @param OutContext Receives the created transaction context
     * @return True if transaction was successfully created
     */
    virtual bool BeginTransaction(const FTransactionConfig& Config, FMiningTransactionContext*& OutContext) = 0;
    
    /**
     * Commits a transaction
     * @param Context Transaction context to commit
     * @return True if transaction was successfully committed, false if it failed or was aborted
     */
    virtual bool CommitTransaction(FMiningTransactionContext* Context) = 0;
    
    /**
     * Aborts a transaction
     * @param Context Transaction context to abort
     */
    virtual void AbortTransaction(FMiningTransactionContext* Context) = 0;
    
    /**
     * Validates the read set of a transaction without committing
     * @param Context Transaction context to validate
     * @return True if the transaction is still valid, false if conflicts were detected
     */
    virtual bool ValidateTransaction(FMiningTransactionContext* Context) = 0;
    
    /**
     * Gets the current transaction for this thread
     * @return Current transaction context or nullptr if not in a transaction
     */
    virtual FMiningTransactionContext* GetCurrentTransaction() = 0;
    
    /**
     * Gets a transaction by ID
     * @param TransactionId ID of the transaction to get
     * @return Transaction context or nullptr if not found
     */
    virtual FMiningTransactionContext* GetTransaction(uint64 TransactionId) = 0;
    
    /**
     * Gets global transaction statistics
     * @return Transaction statistics aggregated across all transactions
     */
    virtual TMap<FString, double> GetGlobalStats() const = 0;
    
    /**
     * Gets the number of active transactions
     * @return Number of active transactions
     */
    virtual uint32 GetActiveTransactionCount() const = 0;
    
    /**
     * Gets the transaction abort rate
     * @return Percentage of aborted transactions (0-100)
     */
    virtual float GetTransactionAbortRate() const = 0;
    
    /**
     * Gets conflict statistics for zones
     * @return Map of zone IDs to conflict counts
     */
    virtual TMap<int32, uint32> GetZoneConflictStats() const = 0;
    
    /**
     * Gets the lock for a zone
     * @param ZoneId The zone ID
     * @return Lock object for the zone
     */
    virtual FSimpleSpinLock* GetZoneLock(int32 ZoneId) = 0;
    
    /**
     * Updates the fast-path threshold for a transaction type
     * @param TypeId Type ID of the transaction
     * @param ConflictRate New conflict rate threshold (0.0-1.0)
     * @return True if threshold was updated
     */
    virtual bool UpdateFastPathThreshold(uint32 TypeId, float ConflictRate) = 0;
    
    /**
     * Gets the singleton instance
     * @return Reference to the transaction manager
     */
    static ITransactionManager& Get();
};
