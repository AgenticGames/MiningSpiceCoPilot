// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IAsyncOperation.h"
#include "HAL/ThreadSafeCounter.h"
#include "Async/TaskGraphInterfaces.h"
#include "Async/AsyncWork.h"

/**
 * Async operation implementation class
 */
class MININGSPICECOPILOT_API FAsyncOperationImpl
{
public:
    /** Constructor */
    FAsyncOperationImpl(uint64 InId, const FString& InType, const FString& InName);
    
    /** Destructor */
    ~FAsyncOperationImpl();
    
    /** Gets the unique identifier for this operation */
    uint64 GetId() const;
    
    /** Gets the operation type */
    const FString& GetType() const;
    
    /** Gets the operation name */
    const FString& GetName() const;
    
    /** Sets the operation status */
    void SetStatus(EAsyncStatus Status);
    
    /** Gets the operation status */
    EAsyncStatus GetStatus() const;
    
    /** Updates the progress of the operation */
    void UpdateProgress(const FAsyncProgress& Progress);
    
    /** Gets the current progress */
    FAsyncProgress GetProgress() const;
    
    /** Sets the operation result */
    void SetResult(const FAsyncResult& Result);
    
    /** Gets the operation result */
    FAsyncResult GetResult() const;
    
    /** Registers a progress callback */
    bool RegisterProgressCallback(const FAsyncProgressDelegate& Callback, uint32 UpdateIntervalMs);
    
    /** Registers a completion callback */
    bool RegisterCompletionCallback(const FAsyncCompletionDelegate& Callback);
    
    /** Sets the operation parameters */
    void SetParameters(const TMap<FString, FString>& Params);
    
    /** Gets the operation parameters */
    const TMap<FString, FString>& GetParameters() const;
    
    /** Executes the operation */
    virtual bool Execute() = 0;
    
    /** Cancels the operation */
    virtual bool Cancel() = 0;
    
    /** Called when the operation completes */
    void NotifyCompletion();
    
    /** Called to trigger progress callbacks */
    void NotifyProgress();
    
    /** Gets the creation time of this operation */
    double GetCreationTime() const;
    
    /** Gets the start time of this operation */
    double GetStartTime() const;
    
    /** Gets the completion time of this operation */
    double GetCompletionTime() const;
    
    /** Sets the start time */
    void SetStartTime(double Time);
    
    /** Sets the completion time */
    void SetCompletionTime(double Time);
    
    /** Checks if a progress update is due */
    bool IsProgressUpdateDue() const;

protected:
    /** Operation ID */
    uint64 Id;
    
    /** Operation type */
    FString Type;
    
    /** Operation name */
    FString Name;
    
    /** Operation status */
    EAsyncStatus Status;
    
    /** Operation progress */
    FAsyncProgress Progress;
    
    /** Operation result */
    FAsyncResult Result;
    
    /** Operation parameters */
    TMap<FString, FString> Parameters;
    
    /** Progress callback */
    FAsyncProgressDelegate ProgressCallback;
    
    /** Completion callback */
    FAsyncCompletionDelegate CompletionCallback;
    
    /** Lock for state access */
    mutable FCriticalSection StateLock;
    
    /** Progress update interval in seconds */
    double ProgressUpdateIntervalSeconds;
    
    /** Last progress update time */
    double LastProgressUpdateTime;
    
    /** Creation time */
    double CreationTime;
    
    /** Start time */
    double StartTime;
    
    /** Completion time */
    double CompletionTime;
    
    /** Whether the operation has been cancelled */
    FThreadSafeCounter bCancelled;
};

/**
 * Async operation task for execution on task graph
 */
class MININGSPICECOPILOT_API FAsyncOperationTask : public FAsyncTask
{
public:
    /** Constructor */
    FAsyncOperationTask(FAsyncOperationImpl* InOperation);
    
    //~ Begin FAsyncTask Interface
    virtual void DoWork() override;
    virtual void Abandon() override;
    //~ End FAsyncTask Interface

protected:
    /** The operation to execute */
    FAsyncOperationImpl* Operation;
};

/**
 * Async operation factory for creating various types of operations
 */
class MININGSPICECOPILOT_API FAsyncOperationFactory
{
public:
    /** Constructor */
    FAsyncOperationFactory();
    
    /** Destructor */
    ~FAsyncOperationFactory();
    
    /** Registers an operation type */
    bool RegisterOperationType(const FString& Type, TFunction<FAsyncOperationImpl*(uint64, const FString&)> Creator);
    
    /** Creates an operation instance */
    FAsyncOperationImpl* CreateOperation(uint64 Id, const FString& Type, const FString& Name);
    
    /** Gets all registered operation types */
    TArray<FString> GetRegisteredTypes() const;
    
    /** Checks if an operation type is registered */
    bool IsTypeRegistered(const FString& Type) const;

private:
    /** Map of operation type to creator function */
    TMap<FString, TFunction<FAsyncOperationImpl*(uint64, const FString&)>> OperationCreators;
    
    /** Lock for creators map */
    mutable FCriticalSection CreatorsLock;
};

/**
 * Async task manager implementation for the Mining system
 * Provides asynchronous task management for long-running operations
 * with progress tracking and cancellation support
 */
class MININGSPICECOPILOT_API FAsyncTaskManager : public IAsyncOperation
{
public:
    /** Constructor */
    FAsyncTaskManager();
    
    /** Destructor */
    virtual ~FAsyncTaskManager();

    //~ Begin IAsyncOperation Interface
    virtual bool Initialize() override;
    virtual void Shutdown() override;
    virtual bool IsInitialized() const override;
    
    virtual uint64 CreateOperation(const FString& OperationType, const FString& OperationName = TEXT("")) override;
    virtual bool StartOperation(uint64 OperationId, const TMap<FString, FString>& Parameters = TMap<FString, FString>()) override;
    virtual bool CancelOperation(uint64 OperationId, bool bWaitForCancellation = false) override;
    virtual EAsyncStatus GetOperationStatus(uint64 OperationId) const override;
    virtual FAsyncProgress GetOperationProgress(uint64 OperationId) const override;
    virtual FAsyncResult GetOperationResult(uint64 OperationId) const override;
    virtual bool WaitForCompletion(uint64 OperationId, uint32 TimeoutMs = 0) override;
    virtual bool RegisterProgressCallback(uint64 OperationId, const FAsyncProgressDelegate& Callback, uint32 UpdateIntervalMs = 100) override;
    virtual bool RegisterCompletionCallback(uint64 OperationId, const FAsyncCompletionDelegate& Callback) override;
    virtual uint32 GetActiveOperationCount() const override;
    virtual TArray<uint64> GetActiveOperations() const override;
    virtual TArray<uint64> GetOperationsOfType(const FString& OperationType) const override;
    virtual uint32 CleanupCompletedOperations(double MaxAgeSeconds = 300.0) override;
    
    static IAsyncOperation& Get() override;
    //~ End IAsyncOperation Interface
    
    /** Registers a new operation type */
    bool RegisterOperationType(const FString& Type, TFunction<FAsyncOperationImpl*(uint64, const FString&)> Creator);
    
    /** Gets all registered operation types */
    TArray<FString> GetRegisteredOperationTypes() const;
    
    /** Updates progress for all active operations */
    void UpdateOperations();
    
    /** Gets operation by ID */
    FAsyncOperationImpl* GetOperationById(uint64 OperationId) const;
    
    /** Called when an operation completes */
    void OnOperationCompleted(uint64 OperationId, const FAsyncResult& Result);
    
    /** Creates a delta between two progress updates */
    static FString CreateProgressDelta(const FAsyncProgress& Previous, const FAsyncProgress& Current);

private:
    /** Whether the manager has been initialized */
    bool bIsInitialized;
    
    /** Map of operations by ID */
    TMap<uint64, FAsyncOperationImpl*> Operations;
    
    /** Map of active operations by type */
    TMap<FString, TArray<uint64>> ActiveOperationsByType;
    
    /** Operation factory */
    FAsyncOperationFactory Factory;
    
    /** Lock for operations map */
    mutable FCriticalSection OperationsLock;
    
    /** Next operation ID */
    FThreadSafeCounter NextOperationId;
    
    /** Timer handle for periodic updates */
    FDelegateHandle UpdateTimerHandle;
    
    /** Generates a unique operation ID */
    uint64 GenerateOperationId();
    
    /** Moves an operation to completed state */
    void MoveToCompleted(FAsyncOperationImpl* Operation);
    
    /** Updates the active operations map */
    void UpdateActiveOperationsMap(const FString& Type, uint64 Id, bool bAdd);

    /** Singleton instance */
    static FAsyncTaskManager* Instance;
};