// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IAsyncOperation.generated.h"

/**
 * Async operation status codes
 */
enum class EAsyncStatus : uint8
{
    /** Operation has not yet started */
    NotStarted,
    
    /** Operation is in progress */
    InProgress,
    
    /** Operation completed successfully */
    Completed,
    
    /** Operation completed with an error */
    Failed,
    
    /** Operation was cancelled */
    Cancelled,
    
    /** Operation timed out */
    TimedOut,
    
    /** Operation is in an invalid state */
    Invalid
};

/**
 * Async operation progress information
 */
struct MININGSPICECOPILOT_API FAsyncProgress
{
    /** Completion percentage (0.0 - 1.0) */
    float CompletionPercentage;
    
    /** Current stage of the operation (operation-specific) */
    int32 CurrentStage;
    
    /** Total number of stages */
    int32 TotalStages;
    
    /** Optional status message */
    FString StatusMessage;
    
    /** Time elapsed since operation start in seconds */
    double ElapsedTimeSeconds;
    
    /** Estimated time remaining in seconds */
    double EstimatedTimeRemainingSeconds;
    
    /** Number of items processed so far */
    int64 ItemsProcessed;
    
    /** Total number of items to process */
    int64 TotalItems;
    
    /** Default constructor */
    FAsyncProgress()
        : CompletionPercentage(0.0f)
        , CurrentStage(0)
        , TotalStages(1)
        , ElapsedTimeSeconds(0.0)
        , EstimatedTimeRemainingSeconds(-1.0) // -1 means unknown
        , ItemsProcessed(0)
        , TotalItems(0)
    {
    }
};

/**
 * Async operation result containing success/failure information
 */
struct MININGSPICECOPILOT_API FAsyncResult
{
    /** Whether the operation was successful */
    bool bSuccess;
    
    /** Error message if operation failed */
    FString ErrorMessage;
    
    /** Error code if operation failed */
    int32 ErrorCode;
    
    /** Whether the operation was cancelled */
    bool bCancelled;
    
    /** Optional result data (operation-specific) */
    TSharedPtr<void> ResultData;
    
    /** Default constructor for successful result */
    FAsyncResult()
        : bSuccess(true)
        , ErrorCode(0)
        , bCancelled(false)
    {
    }
    
    /** Constructor for failed result */
    FAsyncResult(const FString& InErrorMessage, int32 InErrorCode = -1)
        : bSuccess(false)
        , ErrorMessage(InErrorMessage)
        , ErrorCode(InErrorCode)
        , bCancelled(false)
    {
    }
    
    /** Constructor for cancelled result */
    static FAsyncResult Cancelled()
    {
        FAsyncResult Result;
        Result.bSuccess = false;
        Result.bCancelled = true;
        Result.ErrorMessage = TEXT("Operation cancelled");
        Result.ErrorCode = -2;
        return Result;
    }
    
    /** Constructor for timed out result */
    static FAsyncResult TimedOut()
    {
        FAsyncResult Result;
        Result.bSuccess = false;
        Result.bCancelled = false;
        Result.ErrorMessage = TEXT("Operation timed out");
        Result.ErrorCode = -3;
        return Result;
    }
};

/**
 * Callback signature for async operation progress updates
 */
DECLARE_DELEGATE_OneParam(FAsyncProgressDelegate, const FAsyncProgress&);

/**
 * Callback signature for async operation completion
 */
DECLARE_DELEGATE_OneParam(FAsyncCompletionDelegate, const FAsyncResult&);

/**
 * Base interface for async operations in the SVO+SDF mining architecture
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UAsyncOperation : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for async operation management in the SVO+SDF mining architecture
 * Provides asynchronous task management for mining operations
 */
class MININGSPICECOPILOT_API IAsyncOperation
{
    GENERATED_BODY()

public:
    /**
     * Initializes the async operation manager
     * @return True if initialization was successful
     */
    virtual bool Initialize() = 0;
    
    /**
     * Shuts down the async operation manager and cleans up resources
     */
    virtual void Shutdown() = 0;
    
    /**
     * Checks if the async operation manager has been initialized
     * @return True if initialized, false otherwise
     */
    virtual bool IsInitialized() const = 0;
    
    /**
     * Creates a new async operation
     * @param OperationType The type of operation to create
     * @param OperationName Optional name for the operation
     * @return ID of the created operation or 0 if creation failed
     */
    virtual uint64 CreateOperation(const FString& OperationType, const FString& OperationName = TEXT("")) = 0;
    
    /**
     * Starts an async operation
     * @param OperationId ID of the operation to start
     * @param Parameters Optional parameters for the operation
     * @return True if the operation was started successfully
     */
    virtual bool StartOperation(uint64 OperationId, const TMap<FString, FString>& Parameters = TMap<FString, FString>()) = 0;
    
    /**
     * Cancels an async operation
     * @param OperationId ID of the operation to cancel
     * @param bWaitForCancellation Whether to wait for the operation to be fully cancelled
     * @return True if the operation was cancelled or is in the process of cancelling
     */
    virtual bool CancelOperation(uint64 OperationId, bool bWaitForCancellation = false) = 0;
    
    /**
     * Gets the current status of an async operation
     * @param OperationId ID of the operation to check
     * @return Current status of the operation
     */
    virtual EAsyncStatus GetOperationStatus(uint64 OperationId) const = 0;
    
    /**
     * Gets progress information for an async operation
     * @param OperationId ID of the operation to check
     * @return Progress information for the operation
     */
    virtual FAsyncProgress GetOperationProgress(uint64 OperationId) const = 0;
    
    /**
     * Gets the result of a completed operation
     * @param OperationId ID of the operation to get the result for
     * @return Result information for the operation
     */
    virtual FAsyncResult GetOperationResult(uint64 OperationId) const = 0;
    
    /**
     * Waits for an async operation to complete
     * @param OperationId ID of the operation to wait for
     * @param TimeoutMs Maximum time to wait in milliseconds (0 for indefinite)
     * @return True if the operation completed successfully, false if it failed, was cancelled, or timed out
     */
    virtual bool WaitForCompletion(uint64 OperationId, uint32 TimeoutMs = 0) = 0;
    
    /**
     * Registers a callback for operation progress updates
     * @param OperationId ID of the operation to register for
     * @param Callback Delegate to call with progress updates
     * @param UpdateIntervalMs Minimum interval between progress updates in milliseconds
     * @return True if the callback was registered successfully
     */
    virtual bool RegisterProgressCallback(uint64 OperationId, const FAsyncProgressDelegate& Callback, uint32 UpdateIntervalMs = 100) = 0;
    
    /**
     * Registers a callback for operation completion
     * @param OperationId ID of the operation to register for
     * @param Callback Delegate to call when the operation completes
     * @return True if the callback was registered successfully
     */
    virtual bool RegisterCompletionCallback(uint64 OperationId, const FAsyncCompletionDelegate& Callback) = 0;
    
    /**
     * Gets the number of active operations
     * @return Number of active operations
     */
    virtual uint32 GetActiveOperationCount() const = 0;
    
    /**
     * Gets IDs of all active operations
     * @return Array of operation IDs
     */
    virtual TArray<uint64> GetActiveOperations() const = 0;
    
    /**
     * Gets IDs of all active operations of a specific type
     * @param OperationType Type of operations to get
     * @return Array of operation IDs
     */
    virtual TArray<uint64> GetOperationsOfType(const FString& OperationType) const = 0;
    
    /**
     * Cleans up completed operations older than the specified age
     * @param MaxAgeSeconds Maximum age in seconds for operations to keep
     * @return Number of operations cleaned up
     */
    virtual uint32 CleanupCompletedOperations(double MaxAgeSeconds = 300.0) = 0;
    
    /**
     * Gets the singleton instance
     * @return Reference to the async operation manager
     */
    static IAsyncOperation& Get();
};
