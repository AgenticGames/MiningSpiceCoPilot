// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IEventHandler.h"

/**
 * Transaction state enumeration
 */
enum class ETransactionState : uint8
{
    /** Transaction has begun */
    Begin,
    
    /** Transaction is in progress */
    InProgress,
    
    /** Transaction has been committed */
    Committed,
    
    /** Transaction has been aborted */
    Aborted,
    
    /** Transaction has encountered a conflict */
    Conflict,
    
    /** Transaction is being rolled back */
    RollingBack,
    
    /** Transaction rollback completed */
    RolledBack
};

/**
 * Conflict type enumeration
 */
enum class ETransactionConflictType : uint8
{
    /** No conflict */
    None,
    
    /** Read-write conflict */
    ReadWrite,
    
    /** Write-write conflict */
    WriteWrite,
    
    /** Version conflict */
    Version,
    
    /** Resource conflict */
    Resource,
    
    /** Zone boundary conflict */
    ZoneBoundary,
    
    /** Material incompatibility conflict */
    MaterialIncompatibility,
    
    /** Lock contention conflict */
    LockContention
};

/**
 * Zone transaction event types as FName constants
 */
struct MININGSPICECOPILOT_API FZoneTransactionEventTypes
{
    static const FName TransactionBegin;
    static const FName TransactionInProgress;
    static const FName TransactionCommitted;
    static const FName TransactionAborted;
    static const FName TransactionConflict;
    static const FName TransactionRollingBack;
    static const FName TransactionRolledBack;
    static const FName TransactionVersionChanged;
    static const FName TransactionResourceAcquired;
    static const FName TransactionResourceReleased;
    static const FName TransactionLockAcquired;
    static const FName TransactionLockReleased;
    static const FName TransactionZoneMerged;
    static const FName TransactionZoneSplit;
};

/**
 * Transaction event data
 */
struct MININGSPICECOPILOT_API FTransactionEventData
{
    /** Unique identifier for this transaction */
    FGuid TransactionId;
    
    /** Current transaction state */
    ETransactionState State;
    
    /** Region ID */
    int32 RegionId;
    
    /** Zone ID */
    int32 ZoneId;
    
    /** Previous transaction state (for state changes) */
    ETransactionState PreviousState;
    
    /** Version number for optimistic concurrency */
    int32 VersionNumber;
    
    /** Previous version number (for version changes) */
    int32 PreviousVersionNumber;
    
    /** User or system that initiated the transaction */
    FString Initiator;
    
    /** Time when the transaction began */
    double StartTimeSeconds;
    
    /** Time when the transaction ended (commit or abort) */
    double EndTimeSeconds;
    
    /** Materials affected by this transaction */
    TArray<uint8> AffectedMaterials;
    
    /** Number of operations in this transaction */
    int32 OperationCount;
    
    /** Whether this transaction is read-only */
    bool bReadOnly;
    
    /** Whether this transaction allows dirty reads */
    bool bAllowDirtyReads;
    
    /** Parent transaction ID (if this is a child transaction) */
    FGuid ParentTransactionId;
    
    /** Dependency transaction IDs (transactions this one depends on) */
    TArray<FGuid> DependencyTransactionIds;
    
    /** Conflict information (if state is Conflict) */
    ETransactionConflictType ConflictType;
    
    /** ID of the conflicting transaction (if any) */
    FGuid ConflictingTransactionId;
    
    /** Error message for conflict or abort */
    FString ErrorMessage;
    
    /**
     * Converts the transaction data to JSON
     * @return Transaction data as JSON object
     */
    TSharedRef<FJsonObject> ToJson() const;
    
    /**
     * Creates transaction data from JSON
     * @param JsonObject JSON object to parse
     * @return Transaction data
     */
    static FTransactionEventData FromJson(const TSharedRef<FJsonObject>& JsonObject);
    
    /** Default constructor */
    FTransactionEventData()
        : State(ETransactionState::Begin)
        , RegionId(INDEX_NONE)
        , ZoneId(INDEX_NONE)
        , PreviousState(ETransactionState::Begin)
        , VersionNumber(0)
        , PreviousVersionNumber(0)
        , StartTimeSeconds(0.0)
        , EndTimeSeconds(0.0)
        , OperationCount(0)
        , bReadOnly(false)
        , bAllowDirtyReads(false)
        , ConflictType(ETransactionConflictType::None)
    {
    }
};

/**
 * Transaction resource event data
 */
struct MININGSPICECOPILOT_API FTransactionResourceEventData
{
    /** Transaction ID */
    FGuid TransactionId;
    
    /** Resource type */
    FName ResourceType;
    
    /** Resource ID */
    FGuid ResourceId;
    
    /** Region ID */
    int32 RegionId;
    
    /** Zone ID */
    int32 ZoneId;
    
    /** Resource amount or count */
    float ResourceAmount;
    
    /** Whether the resource was acquired exclusively */
    bool bExclusive;
    
    /** Time when the resource was acquired */
    double AcquisitionTimeSeconds;
    
    /** Duration the resource is expected to be held (0 for unknown) */
    double ExpectedDurationSeconds;
    
    /** Priority of the resource request */
    int32 Priority;
    
    /** Whether the resource was successfully acquired */
    bool bAcquired;
    
    /** Error message if acquisition failed */
    FString ErrorMessage;
    
    /**
     * Converts the resource data to JSON
     * @return Resource data as JSON object
     */
    TSharedRef<FJsonObject> ToJson() const;
    
    /**
     * Creates resource data from JSON
     * @param JsonObject JSON object to parse
     * @return Resource data
     */
    static FTransactionResourceEventData FromJson(const TSharedRef<FJsonObject>& JsonObject);
    
    /** Default constructor */
    FTransactionResourceEventData()
        : RegionId(INDEX_NONE)
        , ZoneId(INDEX_NONE)
        , ResourceAmount(0.0f)
        , bExclusive(false)
        , AcquisitionTimeSeconds(0.0)
        , ExpectedDurationSeconds(0.0)
        , Priority(0)
        , bAcquired(false)
    {
    }
};

/**
 * Helper class for creating zone transaction events
 */
class MININGSPICECOPILOT_API FZoneTransactionEventFactory
{
public:
    /**
     * Creates a transaction state change event
     * @param TransactionData Transaction data
     * @param Priority Event priority
     * @return Event data ready for publishing
     */
    static FEventData CreateTransactionStateEvent(
        const FTransactionEventData& TransactionData,
        EEventPriority Priority = EEventPriority::Normal
    );
    
    /**
     * Creates a transaction begin event
     * @param TransactionId Transaction ID
     * @param RegionId Region ID
     * @param ZoneId Zone ID
     * @param Initiator User or system that initiated the transaction
     * @param bReadOnly Whether this transaction is read-only
     * @param Priority Event priority
     * @return Event data ready for publishing
     */
    static FEventData CreateTransactionBeginEvent(
        const FGuid& TransactionId,
        int32 RegionId,
        int32 ZoneId,
        const FString& Initiator,
        bool bReadOnly = false,
        EEventPriority Priority = EEventPriority::Normal
    );
    
    /**
     * Creates a transaction commit event
     * @param TransactionId Transaction ID
     * @param RegionId Region ID
     * @param ZoneId Zone ID
     * @param OperationCount Number of operations in this transaction
     * @param AffectedMaterials Materials affected by this transaction
     * @param StartTimeSeconds Time when the transaction began
     * @param Priority Event priority
     * @return Event data ready for publishing
     */
    static FEventData CreateTransactionCommitEvent(
        const FGuid& TransactionId,
        int32 RegionId,
        int32 ZoneId,
        int32 OperationCount,
        const TArray<uint8>& AffectedMaterials,
        double StartTimeSeconds,
        EEventPriority Priority = EEventPriority::Normal
    );
    
    /**
     * Creates a transaction abort event
     * @param TransactionId Transaction ID
     * @param RegionId Region ID
     * @param ZoneId Zone ID
     * @param ErrorMessage Error message for abort
     * @param Priority Event priority
     * @return Event data ready for publishing
     */
    static FEventData CreateTransactionAbortEvent(
        const FGuid& TransactionId,
        int32 RegionId,
        int32 ZoneId,
        const FString& ErrorMessage,
        EEventPriority Priority = EEventPriority::High
    );
    
    /**
     * Creates a transaction conflict event
     * @param TransactionId Transaction ID
     * @param ConflictingTransactionId ID of the conflicting transaction
     * @param RegionId Region ID
     * @param ZoneId Zone ID
     * @param ConflictType Type of conflict
     * @param ErrorMessage Error message describing the conflict
     * @param Priority Event priority
     * @return Event data ready for publishing
     */
    static FEventData CreateTransactionConflictEvent(
        const FGuid& TransactionId,
        const FGuid& ConflictingTransactionId,
        int32 RegionId,
        int32 ZoneId,
        ETransactionConflictType ConflictType,
        const FString& ErrorMessage,
        EEventPriority Priority = EEventPriority::High
    );
    
    /**
     * Creates a resource acquisition event
     * @param ResourceData Resource data
     * @param Priority Event priority
     * @return Event data ready for publishing
     */
    static FEventData CreateResourceAcquisitionEvent(
        const FTransactionResourceEventData& ResourceData,
        EEventPriority Priority = EEventPriority::Normal
    );
    
    /**
     * Creates a resource release event
     * @param ResourceData Resource data
     * @param Priority Event priority
     * @return Event data ready for publishing
     */
    static FEventData CreateResourceReleaseEvent(
        const FTransactionResourceEventData& ResourceData,
        EEventPriority Priority = EEventPriority::Normal
    );
};