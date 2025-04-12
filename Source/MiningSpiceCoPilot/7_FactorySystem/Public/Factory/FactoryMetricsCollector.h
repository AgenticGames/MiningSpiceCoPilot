// FactoryMetricsCollector.h
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IFactoryMetrics.h"
#include "FactoryMetricsCollector.generated.h"

/**
 * Implements metrics collection system for factory performance
 * Tracks creation patterns and operation performance
 */
UCLASS()
class MININGSPICECOPILOT_API UFactoryMetricsCollector : public UObject, public IFactoryMetrics
{
    GENERATED_BODY()

public:
    UFactoryMetricsCollector();

    //~ Begin IFactoryMetrics Interface
    virtual bool Initialize() override;
    virtual void Shutdown() override;
    virtual bool IsEnabled() const override;
    virtual void SetEnabled(bool bEnable) override;
    virtual int32 BeginOperation(const FName& FactoryName, UClass* ComponentType, EFactoryOperationType OperationType) override;
    virtual void EndOperation(int32 OperationHandle, bool bSuccess = true, bool bCacheMiss = false) override;
    virtual void TrackOperation(const FName& FactoryName, UClass* ComponentType, EFactoryOperationType OperationType, float DurationMs, bool bSuccess = true, bool bCacheMiss = false) override;
    virtual int32 BeginCreationPattern(const FString& Description) override;
    virtual void AddComponentToPattern(int32 PatternHandle, UClass* ComponentType) override;
    virtual void EndCreationPattern(int32 PatternHandle, bool bSuccess = true) override;
    virtual bool GetOperationMetrics(const FName& FactoryName, UClass* ComponentType, EFactoryOperationType OperationType, FFactoryOperationMetrics& OutMetrics) const override;
    virtual TArray<FFactoryOperationMetrics> GetAllOperationMetrics() const override;
    virtual TArray<FFactoryCreationPattern> GetAllCreationPatterns() const override;
    virtual FFactoryPerformanceSnapshot TakePerformanceSnapshot() const override;
    virtual void ResetMetrics() override;
    //~ End IFactoryMetrics Interface

    /**
     * Get singleton instance
     * @return Singleton metrics collector instance
     */
    static UFactoryMetricsCollector* Get();

    /**
     * Set buffer size for pattern history
     * @param InPatternHistorySize Maximum number of patterns to remember
     */
    void SetPatternHistorySize(int32 InPatternHistorySize);

    /**
     * Enable or disable creation pattern identification
     * @param bEnable Whether to enable pattern identification
     */
    void EnablePatternIdentification(bool bEnable);

    /**
     * Check if a component creation is part of an ongoing pattern
     * @param ComponentType Component type being created
     * @param OutPatternId Pattern ID if part of a pattern
     * @return True if this creation is part of an identified pattern
     */
    bool IsPartOfPattern(UClass* ComponentType, FGuid& OutPatternId) const;

protected:
    /** Whether metrics collection is enabled */
    UPROPERTY()
    bool bIsEnabled;

    /** Whether the system is initialized */
    UPROPERTY()
    bool bIsInitialized;

    /** Whether pattern identification is enabled */
    UPROPERTY()
    bool bPatternIdentificationEnabled;

    /** Maximum size of the pattern history */
    UPROPERTY()
    int32 PatternHistorySize;

    /** Metrics by factory, component type, and operation */
    UPROPERTY()
    TMap<FName, TMap<UClass*, TMap<EFactoryOperationType, FFactoryOperationMetrics>>> MetricsMap;

    /** In-progress operations */
    struct FInProgressOperation
    {
        FName FactoryName;
        UClass* ComponentType;
        EFactoryOperationType OperationType;
        double StartTime;
    };

    /** Currently active operations */
    TMap<int32, FInProgressOperation> InProgressOperations;

    /** Last used operation handle */
    int32 LastOperationHandle;

    /** Creation patterns */
    UPROPERTY()
    TArray<FFactoryCreationPattern> CreationPatterns;

    /** In-progress patterns */
    struct FInProgressPattern
    {
        FGuid PatternId;
        FString Description;
        TArray<UClass*> ComponentTypes;
        double StartTime;
    };

    /** Currently active patterns */
    TMap<int32, FInProgressPattern> InProgressPatterns;

    /** Last used pattern handle */
    int32 LastPatternHandle;

    /** Recent operation sequence for pattern identification */
    TArray<TPair<UClass*, EFactoryOperationType>> RecentOperations;

    /** Total component count */
    int64 TotalComponentCount;

    /** Pooled component count */
    int64 PooledComponentCount;

    /**
     * Update metrics for an operation
     * @param FactoryName Factory name
     * @param ComponentType Component type
     * @param OperationType Operation type
     * @param DurationMs Operation duration in milliseconds
     * @param bCacheMiss Whether a cache miss occurred
     */
    void UpdateMetrics(
        const FName& FactoryName,
        UClass* ComponentType,
        EFactoryOperationType OperationType,
        float DurationMs,
        bool bCacheMiss);

    /**
     * Identify patterns in recent operations
     */
    void IdentifyPatterns();

    /**
     * Update pattern metrics
     * @param Pattern Pattern to update
     * @param DurationMs Pattern duration in milliseconds
     */
    void UpdatePatternMetrics(FFactoryCreationPattern& Pattern, float DurationMs);

    /**
     * Get a key for a pattern based on component types
     * @param ComponentTypes Array of component types in the pattern
     * @return String key representing the pattern
     */
    FString GetPatternKey(const TArray<UClass*>& ComponentTypes) const;
};