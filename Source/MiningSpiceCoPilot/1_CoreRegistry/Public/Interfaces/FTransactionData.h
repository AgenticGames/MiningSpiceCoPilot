// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FTransactionData.generated.h"

/**
 * Data structure for transaction operations
 * Contains information about a single operation within a transaction
 */
USTRUCT()
struct MININGSPICECOPILOT_API FTransactionData
{
    GENERATED_BODY()

    /** Operation type identifier */
    UPROPERTY()
    uint32 OperationType;
    
    /** Target object identifier */
    UPROPERTY()
    uint64 TargetObjectId;
    
    /** Binary data for the operation */
    UPROPERTY()
    TArray<uint8> OperationData;
    
    /** Priority of this operation (higher values are processed first) */
    UPROPERTY()
    int32 Priority;
    
    /** Whether this operation is required to succeed for the transaction to succeed */
    UPROPERTY()
    bool bIsRequired;
    
    /** Timestamp when this operation was created */
    UPROPERTY()
    int64 Timestamp;
    
    /** Default constructor */
    FTransactionData()
        : OperationType(0)
        , TargetObjectId(0)
        , Priority(0)
        , bIsRequired(true)
        , Timestamp(0)
    {
    }
}; 