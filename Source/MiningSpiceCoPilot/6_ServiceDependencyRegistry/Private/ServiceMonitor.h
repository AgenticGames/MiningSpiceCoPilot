// ServiceMonitor.h
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IServiceMonitor.h"
#include "HAL/CriticalSection.h"
#include "Containers/Map.h"
#include "Containers/Array.h"
#include "Templates/SharedPointer.h"
#include "Misc/DateTime.h"

/**
 * Implementation of IServiceMonitor interface
 * Provides service health monitoring and automatic recovery capabilities
 */
class FServiceMonitor : public IServiceMonitor
{
public:
    /** Constructor */
    FServiceMonitor();
    
    /** Destructor */
    virtual ~FServiceMonitor();

    //~ Begin IServiceMonitor Interface
    virtual bool Initialize() override;
    virtual void Shutdown() override;
    virtual bool IsInitialized() const override;
    virtual void Update(float DeltaTime) override;
    virtual bool RegisterServiceForMonitoring(const UClass* InInterfaceType, float InImportance, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) override;
    virtual bool GetServiceHealthMetrics(const UClass* InInterfaceType, FServiceHealthMetrics& OutMetrics, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) const override;
    virtual bool RecoverService(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) override;
    virtual void ReportServiceOperation(const UClass* InInterfaceType, bool bSuccess, float ResponseTimeMs, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) override;
    virtual TArray<TPair<const UClass*, FServiceHealthMetrics>> GetProblematicServices() const override;
    //~ End IServiceMonitor Interface

    /** Get singleton instance */
    static FServiceMonitor& Get();

private:
    /** Key for service lookup */
    struct FServiceKey
    {
        const UClass* InterfaceType;
        int32 ZoneID;
        int32 RegionID;

        /** Constructor */
        FServiceKey(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE)
            : InterfaceType(InInterfaceType)
            , ZoneID(InZoneID)
            , RegionID(InRegionID)
        {
        }

        /** Equality operator */
        bool operator==(const FServiceKey& Other) const
        {
            return InterfaceType == Other.InterfaceType &&
                   ZoneID == Other.ZoneID &&
                   RegionID == Other.RegionID;
        }

        /** Less than operator for map sorting */
        bool operator<(const FServiceKey& Other) const
        {
            if (InterfaceType != Other.InterfaceType)
                return InterfaceType < Other.InterfaceType;
            if (ZoneID != Other.ZoneID)
                return ZoneID < Other.ZoneID;
            return RegionID < Other.RegionID;
        }

        /** Get string representation for logging */
        FString ToString() const
        {
            return FString::Printf(TEXT("%s (Zone: %d, Region: %d)"), 
                *InterfaceType->GetName(), ZoneID, RegionID);
        }
    };

    /** Monitoring information for a service */
    struct FMonitoringInfo
    {
        /** Health metrics for the service */
        FServiceHealthMetrics Metrics;
        
        /** Importance of the service (0-1, higher is more critical) */
        float Importance;
        
        /** Time since last health check in seconds */
        float TimeSinceLastCheck;
        
        /** Flag indicating if an automatic recovery attempt is in progress */
        bool bRecoveryInProgress;
        
        /** Number of recovery attempts */
        int32 RecoveryAttempts;
        
        /** Last time a recovery was attempted */
        FDateTime LastRecoveryTime;
        
        /** Window of time for measuring operation success rate */
        float OperationWindow;
        
        /** Number of operations in the current window */
        int32 OperationsInWindow;
        
        /** List of past response times for calculating averages */
        TArray<float> ResponseTimes;
        
        /** Constructor */
        FMonitoringInfo(float InImportance = 0.5f)
            : Importance(InImportance)
            , TimeSinceLastCheck(0.0f)
            , bRecoveryInProgress(false)
            , RecoveryAttempts(0)
            , LastRecoveryTime(FDateTime::MinValue())
            , OperationWindow(60.0f) // 60 second window by default
            , OperationsInWindow(0)
        {
            // Initialize metrics
            Metrics.Status = EServiceHealthStatus::Unknown;
            Metrics.TimeSinceLastCheck = 0.0f;
            Metrics.SuccessfulOperations = 0;
            Metrics.FailedOperations = 0;
            Metrics.AverageResponseTimeMs = 0.0f;
            Metrics.PeakResponseTimeMs = 0.0f;
            Metrics.RecoveryCount = 0;
            Metrics.MemoryUsageBytes = 0;
            Metrics.CpuUsagePercent = 0.0f;
            Metrics.ActiveInstances = 1; // Assume one instance by default
        }
        
        /** Update health status based on metrics */
        void UpdateHealthStatus()
        {
            // Calculate overall health status based on metrics
            if (Metrics.FailedOperations > 0 && Metrics.SuccessfulOperations == 0)
            {
                // All operations failing
                Metrics.Status = EServiceHealthStatus::Failed;
            }
            else if (Metrics.FailedOperations > Metrics.SuccessfulOperations * 0.5f)
            {
                // More than 50% operations failing
                Metrics.Status = EServiceHealthStatus::Critical;
            }
            else if (Metrics.FailedOperations > Metrics.SuccessfulOperations * 0.2f)
            {
                // More than 20% operations failing
                Metrics.Status = EServiceHealthStatus::Degraded;
            }
            else if (Metrics.SuccessfulOperations > 0)
            {
                // Less than 20% operations failing, and at least one successful operation
                Metrics.Status = EServiceHealthStatus::Healthy;
            }
            else
            {
                // No operations recorded yet
                Metrics.Status = EServiceHealthStatus::Unknown;
            }
            
            // Factor in response times
            if (Metrics.Status == EServiceHealthStatus::Healthy && 
                Metrics.AverageResponseTimeMs > 100.0f) // Example threshold
            {
                // Slow response times
                Metrics.Status = EServiceHealthStatus::Degraded;
            }
            
            // Factor in high resource usage
            if (Metrics.Status == EServiceHealthStatus::Healthy && 
                Metrics.CpuUsagePercent > 90.0f) // Example threshold
            {
                // High CPU usage
                Metrics.Status = EServiceHealthStatus::Degraded;
            }
        }
    };
    
    /** Map of registered services being monitored */
    TMap<FServiceKey, FMonitoringInfo> MonitoredServices;

    /** Critical section for thread safety */
    mutable FCriticalSection CriticalSection;

    /** Flag indicating if the monitor is initialized */
    bool bIsInitialized;

    /** Time between automatic health checks in seconds */
    float HealthCheckInterval;
    
    /** Time since last automatic health check */
    float TimeSinceLastHealthCheck;
    
    /** Minimum time between recovery attempts for the same service in seconds */
    float MinTimeBetweenRecoveries;
    
    /** Maximum number of recovery attempts before giving up */
    int32 MaxRecoveryAttempts;

    /** Singleton instance */
    static FServiceMonitor* Instance;
    
    /**
     * Add a response time to the monitoring info and update metrics
     * @param Info Monitoring info to update
     * @param ResponseTimeMs Response time to add in milliseconds
     */
    void AddResponseTime(FMonitoringInfo& Info, float ResponseTimeMs);
    
    /**
     * Check if a service needs recovery
     * @param Info Monitoring info for the service
     * @return True if the service needs recovery
     */
    bool NeedsRecovery(const FMonitoringInfo& Info) const;
    
    /**
     * Reset the operation counters for a service
     * @param Info Monitoring info to reset
     */
    void ResetOperationCounters(FMonitoringInfo& Info);
};