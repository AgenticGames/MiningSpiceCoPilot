// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/ITransactionManager.h"
#include "HAL/ThreadSafeCounter.h"
#include "Utils/SimpleSpinLock.h" // Custom implementation from spinlocks.txt

// Forward declarations
class FMiningTransactionContext;
class FMiningTransactionContextImpl;

/**
 * Mining transaction context implementation
 */
class MININGSPICECOPILOT_API FMiningTransactionContextImpl : public FMiningTransactionContext
{
public:
    /** Constructor */
    FMiningTransactionContextImpl(uint64 InTransactionId, const FTransactionConfig& InConfig);
    
    /** Destructor */
    virtual ~FMiningTransactionContextImpl();

    //~ Begin FMiningTransactionContext Interface
    virtual uint64 GetTransactionId() const override;
    virtual ETransactionStatus GetStatus() const override;
    virtual bool AddToReadSet(int32 ZoneId, int32 MaterialId = INDEX_NONE) override;
    virtual bool AddToWriteSet(int32 ZoneId, int32 MaterialId = INDEX_NONE) override;
    virtual FTransactionStats GetStats() const override;
    virtual const FTransactionConfig& GetConfig() const override;
    virtual TArray<FTransactionConflict> GetConflicts() const override;
    virtual void SetName(const FString& Name) override;
    virtual FString GetName() const override;
    //~ End FMiningTransactionContext Interface
    
    /** Sets the transaction status */
    void SetStatus(ETransactionStatus NewStatus);
    
    /** Gets the read set */
    const TArray<FVersionRecord>& GetReadSet() const;
    
    /** Gets the write set */
    const TArray<FVersionRecord>& GetWriteSet() const;
    
    /** Adds a conflict to the transaction */
    void AddConflict(const FTransactionConflict& Conflict);
    
    /** Gets the start time of the transaction */
    double GetStartTime() const;
    
    /** Gets the commit time of the transaction */
    double GetCommitTime() const;
    
    /** Sets the commit time of the transaction */
    void SetCommitTime(double InCommitTime);
    
    /** Increments the retry count */
    uint32 IncrementRetryCount();
    
    /** Records time spent waiting for locks */
    void RecordLockWaitTime(double WaitTimeMs);
    
    /** Records a validation operation */
    void RecordValidation();
    
    /** Sets the resource usage for the transaction */
    void SetPeakMemoryUsage(uint64 MemoryUsageBytes);
    
    /** Clears the read and write sets for retry */
    void ClearReadWriteSets();
    
    /** Gets a reference to the stats (for internal use by FTransactionManager) */
    FTransactionStats& GetMutableStats() { return Stats; }

private:
    /** Transaction ID */
    uint64 TransactionId;
    
    /** Transaction status */
    ETransactionStatus Status;
    
    /** Transaction configuration */
    FTransactionConfig Config;
    
    /** Read set (versions observed when reading) */
    TArray<FVersionRecord> ReadSet;
    
    /** Write set (zones and materials to be modified) */
    TArray<FVersionRecord> WriteSet;
    
    /** Conflicts detected during validation */
    TArray<FTransactionConflict> Conflicts;
    
    /** Transaction name for debugging */
    FString Name;
    
    /** Transaction statistics */
    FTransactionStats Stats;
    
    /** Start time of the transaction */
    double StartTime;
    
    /** Commit start time */
    double CommitStartTime;
    
    /** Commit end time */
    double CommitEndTime;
    
    /** Lock for modifying transaction state */
    mutable FSimpleSpinLock Lock;
};

/**
 * Transaction manager implementation for the Mining system
 * Provides zone-based transactional framework for SVO+SDF mining operations
 */
class MININGSPICECOPILOT_API FTransactionManager : public ITransactionManager
{
public:
    /** Constructor */
    FTransactionManager();
    
    /** Destructor */
    virtual ~FTransactionManager();

    //~ Begin ITransactionManager Interface
    virtual bool Initialize() override;
    virtual void Shutdown() override;
    virtual bool IsInitialized() const override;
    
    virtual bool BeginTransaction(const FTransactionConfig& Config, FMiningTransactionContext*& OutContext) override;
    virtual bool CommitTransaction(FMiningTransactionContext* Context) override;
    virtual void AbortTransaction(FMiningTransactionContext* Context) override;
    virtual bool ValidateTransaction(FMiningTransactionContext* Context) override;
    
    virtual FMiningTransactionContext* GetCurrentTransaction() override;
    virtual FMiningTransactionContext* GetTransaction(uint64 TransactionId) override;
    
    virtual TMap<FString, double> GetGlobalStats() const override;
    virtual uint32 GetActiveTransactionCount() const override;
    virtual float GetTransactionAbortRate() const override;
    virtual TMap<int32, uint32> GetZoneConflictStats() const override;
    
    virtual class FSimpleSpinLock* GetZoneLock(int32 ZoneId) override;
    virtual bool UpdateFastPathThreshold(uint32 TypeId, float ConflictRate) override;
    
    virtual bool RegisterCompletionCallback(uint32 TypeId, const FTransactionCompletionDelegate& Callback) override;
    
    static ITransactionManager& Get();
    //~ End ITransactionManager Interface
    
private:
    /** Whether the transaction manager has been initialized */
    bool bIsInitialized;
    
    /** Thread-local slot for current transaction */
    static uint32 CurrentTransactionTLS;
    
    /** Map of all active transactions by ID */
    TMap<uint64, FMiningTransactionContextImpl*> ActiveTransactions;
    
    /** Map of zone locks by zone ID */
    TMap<int32, FSimpleSpinLock*> ZoneLocks;
    
    /** Map of zone version counters by zone ID */
    TMap<int32, FThreadSafeCounter*> ZoneVersions;
    
    /** Map of material version counters by zone and material ID */
    TMap<FString, FThreadSafeCounter*> MaterialVersions;
    
    /** Lock for transaction map access */
    mutable FSimpleSpinLock TransactionLock;
    
    /** Lock for zone map access */
    mutable FSimpleSpinLock ZoneLock;
    
    /** Next transaction ID */
    FThreadSafeCounter NextTransactionId;
    
    /** Transaction statistics */
    uint64 TotalTransactions;
    uint64 CommittedTransactions;
    uint64 AbortedTransactions;
    uint64 ConflictCount;
    
    /** Map of zone conflict counts */
    TMap<int32, uint32> ZoneConflicts;
    
    /** Map of fast-path thresholds by transaction type */
    TMap<uint32, float> FastPathThresholds;
    
    /** Map of completion callbacks by transaction type ID */
    TMap<uint32, FTransactionCompletionDelegate> CompletionCallbacks;
    
    /** Lock for callback map access */
    mutable FSimpleSpinLock CallbackLock;
    
    /** Generates a unique transaction ID */
    uint64 GenerateTransactionId();
    
    /** Gets or creates a zone lock */
    FSimpleSpinLock* GetOrCreateZoneLock(int32 ZoneId);
    
    /** Gets or creates a zone version counter */
    FThreadSafeCounter* GetOrCreateZoneVersion(int32 ZoneId);
    
    /** Gets or creates a material version counter */
    FThreadSafeCounter* GetOrCreateMaterialVersion(int32 ZoneId, int32 MaterialId);
    
    /** Gets a version record from a zone and material */
    FVersionRecord GetVersionRecord(int32 ZoneId, int32 MaterialId, bool bIsReadOnly);
    
    /** Updates versions for a committed transaction */
    void UpdateVersions(const FMiningTransactionContextImpl* Transaction);
    
    /** Validates transaction read set versions */
    bool ValidateReadSet(const FMiningTransactionContextImpl* Transaction, TArray<FTransactionConflict>& OutConflicts);
    
    /** Cleans up completed transactions */
    void CleanupCompletedTransactions(double MaxAgeSeconds = 300.0);
    
    /** Records a conflict in the statistics */
    void RecordConflict(int32 ZoneId);
    
    /** Should this transaction use the fast path */
    bool ShouldUseFastPath(const FMiningTransactionContextImpl* Transaction) const;
    
    /** Helper method to merge changes when conflict occurs but can be resolved */
    bool MergeChanges(FMiningTransactionContextImpl* Transaction);

    /** Singleton instance */
    static FTransactionManager* Instance;
};