// IFactoryMetrics.h
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factory/FactoryTypes.h"
#include "UObject/Interface.h"
#include "IFactoryMetrics.generated.h"

/**
 * Base interface for factory metrics collector
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UFactoryMetrics : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for collecting and analyzing factory performance metrics
 */
class MININGSPICECOPILOT_API IFactoryMetrics
{
    GENERATED_BODY()

public:
    /**
     * Initialize the metrics system
     * @return True if successfully initialized
     */
    virtual bool Initialize() = 0;
    
    /**
     * Shutdown the metrics system
     */
    virtual void Shutdown() = 0;
    
    /**
     * Check if metrics collection is enabled
     * @return True if metrics collection is enabled
     */
    virtual bool IsEnabled() const = 0;
    
    /**
     * Enable or disable metrics collection
     * @param bEnable Whether to enable metrics collection
     */
    virtual void SetEnabled(bool bEnable) = 0;
    
    /**
     * Begin tracking an operation
     * @param FactoryName Name of the factory performing the operation
     * @param ComponentType Component type being operated on
     * @param OperationType Type of operation being performed
     * @return Handle to the operation for ending tracking
     */
    virtual int32 BeginOperation(const FName& FactoryName, UClass* ComponentType, EFactoryOperationType OperationType) = 0;
    
    /**
     * End tracking an operation
     * @param OperationHandle Handle returned from BeginOperation
     * @param bSuccess Whether the operation was successful
     * @param CacheMiss Whether a cache miss occurred (for pool operations)
     */
    virtual void EndOperation(int32 OperationHandle, bool bSuccess = true, bool bCacheMiss = false) = 0;
    
    /**
     * Track a simple operation (immediately completed)
     * @param FactoryName Name of the factory performing the operation
     * @param ComponentType Component type being operated on
     * @param OperationType Type of operation being performed
     * @param DurationMs Duration of the operation in milliseconds
     * @param bSuccess Whether the operation was successful
     * @param bCacheMiss Whether a cache miss occurred (for pool operations)
     */
    virtual void TrackOperation(const FName& FactoryName, UClass* ComponentType, EFactoryOperationType OperationType, float DurationMs, bool bSuccess = true, bool bCacheMiss = false) = 0;
    
    /**
     * Begin tracking a creation pattern
     * @param Description Description of the pattern being created
     * @return Handle to the pattern for adding components and ending tracking
     */
    virtual int32 BeginCreationPattern(const FString& Description) = 0;
    
    /**
     * Add a component type to a creation pattern
     * @param PatternHandle Handle returned from BeginCreationPattern
     * @param ComponentType Component type being added to the pattern
     */
    virtual void AddComponentToPattern(int32 PatternHandle, UClass* ComponentType) = 0;
    
    /**
     * End tracking a creation pattern
     * @param PatternHandle Handle returned from BeginCreationPattern
     * @param bSuccess Whether the pattern creation was successful
     */
    virtual void EndCreationPattern(int32 PatternHandle, bool bSuccess = true) = 0;
    
    /**
     * Get metrics for a specific factory and operation type
     * @param FactoryName Name of the factory
     * @param ComponentType Component type to get metrics for (nullptr for all types)
     * @param OperationType Operation type to get metrics for
     * @param OutMetrics Metrics for the specified factory and operation
     * @return True if metrics were found
     */
    virtual bool GetOperationMetrics(const FName& FactoryName, UClass* ComponentType, EFactoryOperationType OperationType, FFactoryOperationMetrics& OutMetrics) const = 0;
    
    /**
     * Get all operation metrics
     * @return Array of all operation metrics
     */
    virtual TArray<FFactoryOperationMetrics> GetAllOperationMetrics() const = 0;
    
    /**
     * Get all creation patterns
     * @return Array of all tracked creation patterns
     */
    virtual TArray<FFactoryCreationPattern> GetAllCreationPatterns() const = 0;
    
    /**
     * Take a performance snapshot
     * @return Current performance snapshot
     */
    virtual FFactoryPerformanceSnapshot TakePerformanceSnapshot() const = 0;
    
    /**
     * Reset all metrics
     */
    virtual void ResetMetrics() = 0;
    
    /**
     * Get the singleton instance of the factory metrics
     * @return Reference to the factory metrics instance
     */
    static IFactoryMetrics& Get();
};