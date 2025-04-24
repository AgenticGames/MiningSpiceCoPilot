// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ServiceManager.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/CriticalSection.h"
#include "Containers/Map.h"
#include "Templates/SharedPointer.h"
#include "Misc/Optional.h"
#include "Containers/Queue.h"
#include "HAL/ThreadSafeCounter.h"
#include "Tickable.h"
#include "../../1_CoreRegistry/Public/CommonServiceTypes.h"

/** Forward declarations */
class FTimerManager;
class UObject;
enum class EServiceState : uint8;
enum class EServiceHealthStatus : uint8; // Forward declare instead of including

/**
 * Failure reason categorization
 */
enum class EServiceFailureReason : uint8
{
    /** Unknown reason */
    Unknown,
    
    /** Exception during operation */
    Exception,
    
    /** Memory allocation failure */
    MemoryAllocation,
    
    /** Timeout during operation */
    Timeout,
    
    /** Resource exhaustion */
    ResourceExhaustion,
    
    /** Dependency failure */
    DependencyFailure,
    
    /** Invalid input/state */
    InvalidState,
    
    /** External system failure */
    ExternalSystem,
    
    /** Permission or authorization issue */
    Authorization
};

/**
 * Structure for tracking health history
 */
struct FServiceHealthSnapshot
{
    /** Time of the snapshot */
    FDateTime Timestamp;
    
    /** Health status at this time */
    EServiceHealthStatus Status;
    
    /** Current state at this time */
    EServiceState State;
    
    /** Service metrics at this time */
    FServiceMetrics Metrics;
    
    /** Memory usage at this time (bytes) */
    uint64 MemoryUsage;
    
    /** Response time (milliseconds) */
    double ResponseTimeMs;
    
    /** Failure reason if applicable */
    EServiceFailureReason FailureReason;
    
    /** Optional diagnostic message */
    FString DiagnosticMessage;
    
    /** Default constructor */
    FServiceHealthSnapshot()
        : Timestamp(FDateTime::UtcNow())
        , Status(EServiceHealthStatus::Healthy)
        , State(EServiceState::Active)
        , MemoryUsage(0)
        , ResponseTimeMs(0.0)
        , FailureReason(EServiceFailureReason::Unknown)
    {
    }
};

/**
 * Health prediction model classification
 */
enum class EPredictionModel : uint8
{
    /** Simple threshold-based model */
    ThresholdBased,
    
    /** Statistical trend analysis */
    TrendAnalysis,
    
    /** Anomaly detection */
    AnomalyDetection,
    
    /** Resource consumption projection */
    ResourceProjection
};

/**
 * Service health monitor configuration
 */
struct FHealthMonitorConfig
{
    /** How often to check service health in seconds */
    float HealthCheckIntervalSec;
    
    /** Maximum number of health snapshots to retain per service */
    int32 MaxHealthHistoryEntries;
    
    /** Whether to enable automatic recovery attempts */
    bool bEnableAutoRecovery;
    
    /** Maximum number of automatic recovery attempts */
    int32 MaxAutoRecoveryAttempts;
    
    /** Cooldown period between recovery attempts (seconds) */
    float RecoveryCooldownSec;
    
    /** Whether to enable degradation detection */
    bool bEnableDegradationDetection;
    
    /** Whether to enable failure prediction */
    bool bEnableFailurePrediction;
    
    /** Threshold for warning status (0.0-1.0) */
    float WarningThreshold;
    
    /** Threshold for degraded status (0.0-1.0) */
    float DegradedThreshold;
    
    /** Threshold for severely degraded status (0.0-1.0) */
    float SeverelyDegradedThreshold;
    
    /** Default constructor with sensible defaults */
    FHealthMonitorConfig()
        : HealthCheckIntervalSec(5.0f)
        , MaxHealthHistoryEntries(100)
        , bEnableAutoRecovery(true)
        , MaxAutoRecoveryAttempts(3)
        , RecoveryCooldownSec(30.0f)
        , bEnableDegradationDetection(true)
        , bEnableFailurePrediction(true)
        , WarningThreshold(0.7f)
        , DegradedThreshold(0.5f)
        , SeverelyDegradedThreshold(0.3f)
    {
    }
};

/**
 * Delegate for health status change notification
 */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnServiceHealthChanged, FName /*ServiceKey*/, EServiceHealthStatus /*NewStatus*/, const FServiceHealthSnapshot& /*Snapshot*/);

/**
 * Delegate for failure prediction
 */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnServiceFailurePredicted, FName /*ServiceKey*/, float /*PredictedTimeToFailureSec*/, const FServiceHealthSnapshot& /*CurrentSnapshot*/);

/**
 * Service Health Monitor
 * Provides real-time health monitoring, automated failure detection and recovery,
 * degradation detection with early warning, and proactive health checking with failure prediction.
 */
class MININGSPICECOPILOT_API FServiceHealthMonitor : public FTickableGameObject
{
public:
    /** Constructor */
    FServiceHealthMonitor();
    
    /** Destructor */
    virtual ~FServiceHealthMonitor();
    
    /**
     * Initialize the health monitor
     * @param InServiceManager Reference to the service manager
     * @param InConfig Configuration for the health monitor
     * @return True if initialization was successful
     */
    bool Initialize(FServiceManager& InServiceManager, const FHealthMonitorConfig& InConfig = FHealthMonitorConfig());
    
    /**
     * Shutdown the health monitor
     */
    void Shutdown();
    
    /**
     * Check if the health monitor is initialized
     * @return True if initialized
     */
    bool IsInitialized() const;
    
    /**
     * Force an immediate health check of all services
     */
    void CheckAllServicesHealth();
    
    /**
     * Force an immediate health check of a specific service
     * @param ServiceKey Service identifier
     * @return Health snapshot from the check
     */
    TOptional<FServiceHealthSnapshot> CheckServiceHealth(const FName& ServiceKey);
    
    /**
     * Attempt to recover a failing service
     * @param ServiceKey Service identifier
     * @param bPreserveState Whether to preserve service state during recovery
     * @return True if recovery was successful
     */
    bool RecoverService(const FName& ServiceKey, bool bPreserveState = true);
    
    /**
     * Get the current health status of a service
     * @param ServiceKey Service identifier
     * @return Current health status
     */
    EServiceHealthStatus GetServiceHealthStatus(const FName& ServiceKey) const;
    
    /**
     * Get health history for a service
     * @param ServiceKey Service identifier
     * @return Array of health snapshots, newest first
     */
    TArray<FServiceHealthSnapshot> GetServiceHealthHistory(const FName& ServiceKey) const;
    
    /**
     * Predict time to failure for a service
     * @param ServiceKey Service identifier
     * @param OutConfidence Optional output for prediction confidence (0.0-1.0)
     * @return Estimated time to failure in seconds, negative if healthy
     */
    float PredictTimeToFailure(const FName& ServiceKey, float* OutConfidence = nullptr) const;
    
    /**
     * Get health statistics for all services
     * @return Map of service keys to health snapshots
     */
    TMap<FName, FServiceHealthSnapshot> GetAllServicesHealth() const;
    
    /**
     * Set the prediction model to use for failure prediction
     * @param Model Prediction model type
     */
    void SetPredictionModel(EPredictionModel Model);
    
    /**
     * Register a callback for health status changes
     * @param Delegate Delegate to call when health status changes
     * @return Handle to the delegate binding
     */
    FDelegateHandle RegisterHealthChangeCallback(const FOnServiceHealthChanged::FDelegate& Delegate);
    
    /**
     * Register a callback for failure predictions
     * @param Delegate Delegate to call when failure is predicted
     * @return Handle to the delegate binding
     */
    FDelegateHandle RegisterFailurePredictionCallback(const FOnServiceFailurePredicted::FDelegate& Delegate);
    
    /**
     * Unregister a health change callback
     * @param Handle Handle returned from RegisterHealthChangeCallback
     */
    void UnregisterHealthChangeCallback(FDelegateHandle Handle);
    
    /**
     * Unregister a failure prediction callback
     * @param Handle Handle returned from RegisterFailurePredictionCallback
     */
    void UnregisterFailurePredictionCallback(FDelegateHandle Handle);
    
    /**
     * Record a service operation result
     * @param ServiceKey Service identifier
     * @param bSuccess Whether the operation was successful
     * @param ResponseTimeMs Response time in milliseconds
     * @param FailureReason Reason for failure if unsuccessful
     * @param DiagnosticMessage Optional diagnostic message
     */
    void RecordServiceOperation(const FName& ServiceKey, bool bSuccess, double ResponseTimeMs, 
        EServiceFailureReason FailureReason = EServiceFailureReason::Unknown,
        const FString& DiagnosticMessage = TEXT(""));
    
    /**
     * Update a service's memory usage
     * @param ServiceKey Service identifier
     * @param MemoryUsageBytes Current memory usage in bytes
     */
    void UpdateServiceMemoryUsage(const FName& ServiceKey, uint64 MemoryUsageBytes);
    
    /**
     * Enable or disable the health monitor
     * @param bEnable Whether to enable the monitor
     */
    void SetEnabled(bool bEnable);
    
    /**
     * Check if the health monitor is enabled
     * @return True if enabled
     */
    bool IsEnabled() const;
    
    /**
     * Update the health monitor configuration
     * @param NewConfig New configuration settings
     */
    void UpdateConfiguration(const FHealthMonitorConfig& NewConfig);
    
    /**
     * Get the current health monitor configuration
     * @return Current configuration
     */
    const FHealthMonitorConfig& GetConfiguration() const;
    
    //~ Begin FTickableGameObject Interface
    virtual void Tick(float DeltaTime) override;
    virtual bool IsTickable() const override;
    virtual TStatId GetStatId() const override;
    //~ End FTickableGameObject Interface
    
    /**
     * Get the singleton instance
     * @return Reference to the service health monitor
     */
    static FServiceHealthMonitor& Get();

private:
    /** Whether the monitor is initialized */
    FThreadSafeBool bIsInitialized;
    
    /** Whether the monitor is enabled */
    FThreadSafeBool bIsEnabled;
    
    /** Reference to the service manager */
    FServiceManager* ServiceManager;
    
    /** Configuration */
    FHealthMonitorConfig Configuration;
    
    /** Critical section for thread safety */
    mutable FCriticalSection MonitorLock;
    
    /** Health history per service */
    TMap<FName, TArray<FServiceHealthSnapshot>> HealthHistory;
    
    /** Current health status per service */
    TMap<FName, EServiceHealthStatus> CurrentHealthStatus;
    
    /** Recovery attempt counter per service */
    TMap<FName, int32> RecoveryAttempts;
    
    /** Time of last recovery attempt per service */
    TMap<FName, double> LastRecoveryTime;
    
    /** Time of next scheduled health check */
    double NextHealthCheckTime;
    
    /** Prediction model in use */
    EPredictionModel CurrentPredictionModel;
    
    /** Delegate for health changes */
    FOnServiceHealthChanged OnHealthChangedDelegate;
    
    /** Delegate for failure predictions */
    FOnServiceFailurePredicted OnFailurePredictedDelegate;
    
    /** Singleton instance */
    static FServiceHealthMonitor* Instance;
    
    /**
     * Analyze service metrics to determine health status
     * @param ServiceKey Service identifier
     * @param Metrics Current metrics
     * @param State Current service state
     * @return Determined health status and reason
     */
    TPair<EServiceHealthStatus, FString> AnalyzeServiceHealth(const FName& ServiceKey, 
        const FServiceMetrics& Metrics, EServiceState State) const;
    
    /**
     * Create a health snapshot for a service
     * @param ServiceKey Service identifier
     * @param Status Health status
     * @param State Service state
     * @param Metrics Service metrics
     * @param MemoryUsage Memory usage
     * @param ResponseTime Response time
     * @param Reason Failure reason
     * @param Message Diagnostic message
     * @return Complete health snapshot
     */
    FServiceHealthSnapshot CreateHealthSnapshot(const FName& ServiceKey, 
        EServiceHealthStatus Status, EServiceState State, const FServiceMetrics& Metrics,
        uint64 MemoryUsage, double ResponseTime, EServiceFailureReason Reason,
        const FString& Message);
    
    /**
     * Handle a health status change
     * @param ServiceKey Service identifier
     * @param OldStatus Previous health status
     * @param NewStatus New health status
     * @param Snapshot Health snapshot
     */
    void HandleHealthStatusChange(const FName& ServiceKey, EServiceHealthStatus OldStatus, 
        EServiceHealthStatus NewStatus, const FServiceHealthSnapshot& Snapshot);
    
    /**
     * Detect service degradation from health history
     * @param ServiceKey Service identifier
     * @param OutDegradationRate Optional output for degradation rate
     * @return True if degradation detected
     */
    bool DetectDegradation(const FName& ServiceKey, float* OutDegradationRate = nullptr) const;
    
    /**
     * Predict potential service failure based on health trends
     * @param ServiceKey Service identifier
     * @param OutTimeToFailure Estimated time to failure in seconds
     * @param OutConfidence Prediction confidence (0.0-1.0)
     * @return True if failure predicted
     */
    bool PredictFailure(const FName& ServiceKey, float& OutTimeToFailure, float& OutConfidence) const;
    
    /**
     * Perform threshold-based failure prediction
     * @param ServiceKey Service identifier
     * @param History Health history
     * @param OutTimeToFailure Estimated time to failure
     * @param OutConfidence Prediction confidence
     * @return True if failure predicted
     */
    bool PredictFailureThresholdBased(const FName& ServiceKey, const TArray<FServiceHealthSnapshot>& History,
        float& OutTimeToFailure, float& OutConfidence) const;
    
    /**
     * Perform trend analysis failure prediction
     * @param ServiceKey Service identifier
     * @param History Health history
     * @param OutTimeToFailure Estimated time to failure
     * @param OutConfidence Prediction confidence
     * @return True if failure predicted
     */
    bool PredictFailureTrendAnalysis(const FName& ServiceKey, const TArray<FServiceHealthSnapshot>& History,
        float& OutTimeToFailure, float& OutConfidence) const;
    
    /**
     * Perform resource projection failure prediction
     * @param ServiceKey Service identifier
     * @param History Health history
     * @param OutTimeToFailure Estimated time to failure
     * @param OutConfidence Prediction confidence
     * @return True if failure predicted
     */
    bool PredictFailureResourceProjection(const FName& ServiceKey, const TArray<FServiceHealthSnapshot>& History,
        float& OutTimeToFailure, float& OutConfidence) const;
};
