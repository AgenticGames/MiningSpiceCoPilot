// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "../../1_CoreRegistry/Public/Interfaces/FTransactionData.h"
#include "ITransactionService.generated.h"

// Forward declarations
// struct FTransactionData;  // Remove this since we now include the header

/**
 * Interface for zone transaction services
 * Provides operations for managing transactions within a zone
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UTransactionService : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for transaction services
 * Implementations handle zone-specific transaction processing
 */
class MININGSPICECOPILOT_API ITransactionService
{
    GENERATED_BODY()

public:
    /**
     * Gets the zone ID this service is responsible for
     * @return Zone identifier
     */
    virtual int32 GetZoneId() const = 0;
    
    /**
     * Gets the transaction type this service handles
     * @return Transaction type identifier
     */
    virtual uint32 GetTransactionType() const = 0;
    
    /**
     * Begins a new transaction
     * @param OutTransactionId Output parameter for transaction identifier
     * @return True if transaction was started successfully
     */
    virtual bool BeginTransaction(uint64& OutTransactionId) = 0;
    
    /**
     * Commits a transaction
     * @param InTransactionId Transaction identifier to commit
     * @return True if transaction was committed successfully
     */
    virtual bool CommitTransaction(uint64 InTransactionId) = 0;
    
    /**
     * Aborts a transaction
     * @param InTransactionId Transaction identifier to abort
     * @return True if transaction was aborted successfully
     */
    virtual bool AbortTransaction(uint64 InTransactionId) = 0;
    
    /**
     * Adds an operation to a transaction
     * @param InTransactionId Transaction identifier
     * @param InOperation Operation data
     * @return True if operation was added successfully
     */
    virtual bool AddOperation(uint64 InTransactionId, const FTransactionData& InOperation) = 0;
    
    /**
     * Gets the current transaction conflict rate
     * @return Conflict rate as a percentage
     */
    virtual float GetConflictRate() const = 0;
    
    /**
     * Checks if this transaction service supports coordination with other zones
     * @return True if cross-zone coordination is supported
     */
    virtual bool SupportsCrossZoneTransactions() const = 0;
    
    /**
     * Flushes pending transactions
     * @return True if flush was successful
     */
    virtual bool Flush() = 0;
}; 