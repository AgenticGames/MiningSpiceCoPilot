// IServiceMonitor.h
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IServiceMonitor.generated.h"

/** Service health status */
UENUM(BlueprintType)
enum class EServiceHealthStatus : uint8
{
    Healthy,        // Service is operating normally
    Degraded,       // Service is operational but with reduced performance or functionality
    Critical,       // Service is operational but with severe issues
    Failed,         // Service has failed and is non-operational
    Unknown         // Service status cannot be determined
};

/** Service health metrics */
USTRUCT(BlueprintType)
struct FServiceHealthMetrics
{
    GENERATED_BODY()

    /** Current health status of the service */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Service Health")
    EServiceHealthStatus Status = EServiceHealthStatus::Unknown;

    /** Time since last health check in seconds */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Service Health")
    float TimeSinceLastCheck = 0.0f;
    
    /** Number of successful operations since last failure */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Service Health")
    int32 SuccessfulOperations = 0;
    
    /** Number of failed operations */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Service Health")
    int32 FailedOperations = 0;
    
    /** Average response time in milliseconds */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Service Health")
    float AverageResponseTimeMs = 0.0f;
    
    /** Peak response time in milliseconds */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Service Health")
    float PeakResponseTimeMs = 0.0f;

    /** Number of times this service has been recovered */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Service Health")
    int32 RecoveryCount = 0;
    
    /** Memory usage in bytes */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Service Health")
    int64 MemoryUsageBytes = 0;
    
    /** CPU usage percentage (0-100) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Service Health")
    float CpuUsagePercent = 0.0f;
    
    /** Number of active instances of this service */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Service Health")
    int32 ActiveInstances = 0;

    /** Additional diagnostic information */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Service Health")
    TMap<FString, FString> DiagnosticInfo;
};

/**
 * Base interface for service monitoring in the SVO+SDF mining architecture
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UServiceMonitor : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for service health monitoring in the SVO+SDF mining architecture
 * Provides real-time service health monitoring and recovery capabilities
 */
class MININGSPICECOPILOT_API IServiceMonitor
{
    GENERATED_BODY()

public:
    /**
     * Initialize the service monitor
     * @return True if successfully initialized
     */
    virtual bool Initialize() = 0;
    
    /**
     * Shutdown the service monitor and cleanup resources
     */
    virtual void Shutdown() = 0;
    
    /**
     * Check if the service monitor is initialized
     * @return True if initialized
     */
    virtual bool IsInitialized() const = 0;
    
    /**
     * Update the service monitor (called periodically)
     * @param DeltaTime Time since last update
     */
    virtual void Update(float DeltaTime) = 0;
    
    /**
     * Register a service for monitoring
     * @param InInterfaceType Interface type of the service to monitor
     * @param InImportance Importance of the service (0-1, higher is more critical)
     * @param InZoneID Optional zone ID for zone-specific services
     * @param InRegionID Optional region ID for region-specific services
     * @return True if registration successful
     */
    virtual bool RegisterServiceForMonitoring(const UClass* InInterfaceType, float InImportance, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) = 0;
    
    /**
     * Template helper for type-safe service registration
     * @param InImportance Importance of the service (0-1, higher is more critical)
     * @param InZoneID Optional zone ID for zone-specific services
     * @param InRegionID Optional region ID for region-specific services
     * @return True if registration successful
     */
    template<typename ServiceType>
    bool RegisterServiceForMonitoring(float InImportance, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE)
    {
        return RegisterServiceForMonitoring(ServiceType::StaticClass(), InImportance, InZoneID, InRegionID);
    }
    
    /**
     * Get the current health metrics for a service
     * @param InInterfaceType Interface type of the service
     * @param OutMetrics Reference to health metrics struct to fill
     * @param InZoneID Optional zone ID for zone-specific services
     * @param InRegionID Optional region ID for region-specific services
     * @return True if metrics were retrieved successfully
     */
    virtual bool GetServiceHealthMetrics(const UClass* InInterfaceType, FServiceHealthMetrics& OutMetrics, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) const = 0;
    
    /**
     * Template helper for type-safe metrics retrieval
     * @param OutMetrics Reference to health metrics struct to fill
     * @param InZoneID Optional zone ID for zone-specific services
     * @param InRegionID Optional region ID for region-specific services
     * @return True if metrics were retrieved successfully
     */
    template<typename ServiceType>
    bool GetServiceHealthMetrics(FServiceHealthMetrics& OutMetrics, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) const
    {
        return GetServiceHealthMetrics(ServiceType::StaticClass(), OutMetrics, InZoneID, InRegionID);
    }
    
    /**
     * Attempt to recover a failed service
     * @param InInterfaceType Interface type of the service to recover
     * @param InZoneID Optional zone ID for zone-specific services
     * @param InRegionID Optional region ID for region-specific services
     * @return True if recovery was successful or not needed
     */
    virtual bool RecoverService(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) = 0;
    
    /**
     * Template helper for type-safe service recovery
     * @param InZoneID Optional zone ID for zone-specific services
     * @param InRegionID Optional region ID for region-specific services
     * @return True if recovery was successful or not needed
     */
    template<typename ServiceType>
    bool RecoverService(int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE)
    {
        return RecoverService(ServiceType::StaticClass(), InZoneID, InRegionID);
    }
    
    /**
     * Report a service operation result
     * @param InInterfaceType Interface type of the service
     * @param bSuccess Whether the operation was successful
     * @param ResponseTimeMs Operation response time in milliseconds
     * @param InZoneID Optional zone ID for zone-specific services
     * @param InRegionID Optional region ID for region-specific services
     */
    virtual void ReportServiceOperation(const UClass* InInterfaceType, bool bSuccess, float ResponseTimeMs, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) = 0;
    
    /**
     * Template helper for type-safe operation reporting
     * @param bSuccess Whether the operation was successful
     * @param ResponseTimeMs Operation response time in milliseconds
     * @param InZoneID Optional zone ID for zone-specific services
     * @param InRegionID Optional region ID for region-specific services
     */
    template<typename ServiceType>
    void ReportServiceOperation(bool bSuccess, float ResponseTimeMs, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE)
    {
        ReportServiceOperation(ServiceType::StaticClass(), bSuccess, ResponseTimeMs, InZoneID, InRegionID);
    }
    
    /**
     * Get all services that are in a failed or critical state
     * @return Array of interface types for services in a problematic state
     */
    virtual TArray<TPair<const UClass*, FServiceHealthMetrics>> GetProblematicServices() const = 0;
    
    /**
     * Get the singleton instance of the service monitor
     * @return Reference to the service monitor instance
     */
    static IServiceMonitor& Get();
};
