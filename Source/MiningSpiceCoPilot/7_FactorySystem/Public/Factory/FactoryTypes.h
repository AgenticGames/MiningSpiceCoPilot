// FactoryTypes.h
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FactoryTypes.generated.h"

/** Factory operation types */
UENUM(BlueprintType)
enum class EFactoryOperationType : uint8
{
    Create,          // Component creation
    ReturnToPool,    // Return to pool
    GetFromPool,     // Get from pool
    Initialize,      // Initialization
    Configure,       // Configuration
    Cleanup,         // Cleanup
    Cache            // Cache operation
};

/** Factory operation metrics */
USTRUCT(BlueprintType)
struct MININGSPICECOPILOT_API FFactoryOperationMetrics
{
    GENERATED_BODY()

    // Operation count
    UPROPERTY(BlueprintReadOnly, Category = "Factory Metrics")
    int32 Count = 0;

    // Average operation time (ms)
    UPROPERTY(BlueprintReadOnly, Category = "Factory Metrics")
    float AverageTime = 0.0f;

    // Minimum operation time (ms)
    UPROPERTY(BlueprintReadOnly, Category = "Factory Metrics")
    float MinTime = FLT_MAX;

    // Maximum operation time (ms)
    UPROPERTY(BlueprintReadOnly, Category = "Factory Metrics")
    float MaxTime = 0.0f;

    // Total operation time (ms)
    UPROPERTY(BlueprintReadOnly, Category = "Factory Metrics")
    float TotalTime = 0.0f;

    // Success count
    UPROPERTY(BlueprintReadOnly, Category = "Factory Metrics")
    int32 SuccessCount = 0;

    // Failure count
    UPROPERTY(BlueprintReadOnly, Category = "Factory Metrics")
    int32 FailureCount = 0;

    // Cache miss count
    UPROPERTY(BlueprintReadOnly, Category = "Factory Metrics")
    int32 CacheMissCount = 0;
};

/** Factory creation pattern */
USTRUCT(BlueprintType)
struct MININGSPICECOPILOT_API FFactoryCreationPattern
{
    GENERATED_BODY()

    // Unique pattern ID
    UPROPERTY(BlueprintReadOnly, Category = "Factory Metrics")
    FGuid PatternId;

    // Pattern description
    UPROPERTY(BlueprintReadOnly, Category = "Factory Metrics")
    FString Description;

    // Component types in the pattern
    UPROPERTY(BlueprintReadOnly, Category = "Factory Metrics")
    TArray<TSubclassOf<UObject>> ComponentTypes;

    // Number of times this pattern was observed
    UPROPERTY(BlueprintReadOnly, Category = "Factory Metrics")
    int32 ObservedCount = 0;
    
    // Average execution time (ms)
    UPROPERTY(BlueprintReadOnly, Category = "Factory Metrics")
    float AverageTime = 0.0f;

    // Last observed time
    UPROPERTY(BlueprintReadOnly, Category = "Factory Metrics")
    FDateTime LastObserved;
};

/** Factory performance snapshot */
USTRUCT(BlueprintType)
struct MININGSPICECOPILOT_API FFactoryPerformanceSnapshot
{
    GENERATED_BODY()

    // Total components created
    UPROPERTY(BlueprintReadOnly, Category = "Factory Metrics")
    int64 TotalComponentsCreated = 0;

    // Components in pools
    UPROPERTY(BlueprintReadOnly, Category = "Factory Metrics")
    int64 PooledComponents = 0;

    // Pool utilization percentage
    UPROPERTY(BlueprintReadOnly, Category = "Factory Metrics")
    float PoolUtilization = 0.0f;

    // Average component creation time
    UPROPERTY(BlueprintReadOnly, Category = "Factory Metrics")
    float AverageCreateTime = 0.0f;

    // Pool hit rate
    UPROPERTY(BlueprintReadOnly, Category = "Factory Metrics")
    float PoolHitRate = 0.0f;

    // Most frequent pattern
    UPROPERTY(BlueprintReadOnly, Category = "Factory Metrics")
    FFactoryCreationPattern MostFrequentPattern;
    
    // Timestamp for this snapshot
    UPROPERTY(BlueprintReadOnly, Category = "Factory Metrics")
    FDateTime Timestamp;
};