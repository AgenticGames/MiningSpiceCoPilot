// Copyright Epic Games, Inc. All Rights Reserved.

#include "ServiceHealthMonitor.h"
#include "ServiceManager.h"
#include "../../1_CoreRegistry/Public/CommonServiceTypes.h"
#include "Logging/LogMining.h"
#include "Misc/DateTime.h"
#include "Misc/ScopeLock.h"
#include "Stats/Stats.h"
#include "Interfaces/IMemoryAwareService.h"

// Initialize static singleton instance
FServiceHealthMonitor* FServiceHealthMonitor::Instance = nullptr;

FServiceHealthMonitor::FServiceHealthMonitor()
    : bIsInitialized(false)
    , bIsEnabled(false)
    , ServiceManager(nullptr)
    , NextHealthCheckTime(0.0)
    , CurrentPredictionModel(EPredictionModel::ThresholdBased)
{
    // Set the singleton instance
    check(Instance == nullptr);
    Instance = this;
}

FServiceHealthMonitor::~FServiceHealthMonitor()
{
    // Shutdown if still running
    if (bIsInitialized)
    {
        Shutdown();
    }
    
    // Clear the singleton instance if it's us
    if (Instance == this)
    {
        Instance = nullptr;
    }
}

FServiceHealthMonitor& FServiceHealthMonitor::Get()
{
    // Create instance if needed
    if (Instance == nullptr)
    {
        Instance = new FServiceHealthMonitor();
    }
    
    return *Instance;
}

bool FServiceHealthMonitor::Initialize(FServiceManager& InServiceManager, const FHealthMonitorConfig& InConfig)
{
    // Already initialized check
    if (bIsInitialized)
    {
        UE_LOG(LogMiningRegistry, Warning, TEXT("FServiceHealthMonitor::Initialize - Already initialized"));
        return true;
    }
    
    // Store references
    ServiceManager = &InServiceManager;
    Configuration = InConfig;
    
    // Initialize collections
    HealthHistory.Empty();
    CurrentHealthStatus.Empty();
    RecoveryAttempts.Empty();
    LastRecoveryTime.Empty();
    
    // Set next health check time
    NextHealthCheckTime = FPlatformTime::Seconds() + Configuration.HealthCheckIntervalSec;
    
    // Enable the monitor
    bIsEnabled = true;
    bIsInitialized = true;
    
    UE_LOG(LogMiningRegistry, Log, TEXT("FServiceHealthMonitor::Initialize - Health monitor initialized"));
    
    // Perform initial health check
    CheckAllServicesHealth();
    
    return true;
}

void FServiceHealthMonitor::Shutdown()
{
    if (!bIsInitialized)
    {
        return;
    }
    
    // Disable monitoring
    bIsEnabled = false;
    
    // Clear collections
    HealthHistory.Empty();
    CurrentHealthStatus.Empty();
    RecoveryAttempts.Empty();
    LastRecoveryTime.Empty();
    
    // Clear references
    ServiceManager = nullptr;
    
    bIsInitialized = false;
    
    UE_LOG(LogMiningRegistry, Log, TEXT("FServiceHealthMonitor::Shutdown - Health monitor shutdown"));
}

bool FServiceHealthMonitor::IsInitialized() const
{
    return bIsInitialized;
}

void FServiceHealthMonitor::CheckAllServicesHealth()
{
    if (!bIsInitialized || !bIsEnabled)
    {
        return;
    }
    
    // Ensure service manager is valid
    if (!ServiceManager)
    {
        UE_LOG(LogMiningRegistry, Error, TEXT("FServiceHealthMonitor::CheckAllServicesHealth - Invalid service manager"));
        return;
    }
    
    // Get all service keys
    TArray<FName> ServiceKeys;
    ServiceManager->GetAllServiceKeys(ServiceKeys);
    
    // Check each service
    for (const FName& ServiceKey : ServiceKeys)
    {
        CheckServiceHealth(ServiceKey);
    }
    
    // Update next check time
    NextHealthCheckTime = FPlatformTime::Seconds() + Configuration.HealthCheckIntervalSec;
}

TOptional<FServiceHealthSnapshot> FServiceHealthMonitor::CheckServiceHealth(const FName& ServiceKey)
{
    if (!bIsInitialized || !bIsEnabled)
    {
        return TOptional<FServiceHealthSnapshot>();
    }
    
    // Ensure service manager is valid
    if (!ServiceManager)
    {
        UE_LOG(LogMiningRegistry, Error, TEXT("FServiceHealthMonitor::CheckServiceHealth - Invalid service manager"));
        return TOptional<FServiceHealthSnapshot>();
    }
    
    // Get service instance
    const FServiceInstance* ServiceInstance = ServiceManager->GetServiceInstanceByKey(ServiceKey);
    if (!ServiceInstance)
    {
        UE_LOG(LogMiningRegistry, Warning, TEXT("FServiceHealthMonitor::CheckServiceHealth - Service not found: %s"), *ServiceKey.ToString());
        return TOptional<FServiceHealthSnapshot>();
    }
    
    // Skip services that aren't active or initializing
    if (ServiceInstance->State != EServiceState::Active && ServiceInstance->State != EServiceState::Initializing)
    {
        return TOptional<FServiceHealthSnapshot>();
    }
    
    // Get current metrics
    FServiceMetrics CurrentMetrics = ServiceInstance->Metrics;
    
    // Analyze health
    TPair<EServiceHealthStatus, FString> HealthAnalysis = AnalyzeServiceHealth(ServiceKey, CurrentMetrics, ServiceInstance->State);
    EServiceHealthStatus CurrentStatus = HealthAnalysis.Key;
    FString DiagnosticMessage = HealthAnalysis.Value;
    
    // Get memory usage for memory-aware services
    uint64 MemoryUsage = 0;
    if (ServiceInstance->ServicePtr && ServiceInstance->InterfaceType)
    {
        // We need to check if the service is a UObject and implements the interface
        // Rather than using GetClass() which assumes ServicePtr is a UObject
        UObject* ServiceObject = static_cast<UObject*>(ServiceInstance->ServicePtr);
        if (ServiceObject && ServiceObject->GetClass()->ImplementsInterface(UMemoryAwareService::StaticClass()))
        {
            IMemoryAwareService* MemoryAwareService = Cast<IMemoryAwareService>(ServiceObject);
            if (MemoryAwareService)
            {
                MemoryUsage = MemoryAwareService->GetMemoryUsage();
            }
        }
    }
    
    // Check for status change
    EServiceHealthStatus PreviousStatus = EServiceHealthStatus::Healthy;
    bool bStatusChanged = false;
    
    {
        FScopeLock Lock(&MonitorLock);
        
        if (CurrentHealthStatus.Contains(ServiceKey))
        {
            PreviousStatus = CurrentHealthStatus[ServiceKey];
            bStatusChanged = (PreviousStatus != CurrentStatus);
        }
        else
        {
            // First check for this service
            bStatusChanged = (CurrentStatus != EServiceHealthStatus::Healthy);
        }
        
        // Update current status
        CurrentHealthStatus.Add(ServiceKey, CurrentStatus);
    }
    
    // Create health snapshot
    FServiceHealthSnapshot Snapshot = CreateHealthSnapshot(
        ServiceKey, 
        CurrentStatus, 
        ServiceInstance->State, 
        CurrentMetrics, 
        MemoryUsage, 
        0.0, // No response time for general health check
        EServiceFailureReason::Unknown, 
        DiagnosticMessage);
    
    // Add to history
    {
        FScopeLock Lock(&MonitorLock);
        
        // Create history array if needed
        if (!HealthHistory.Contains(ServiceKey))
        {
            HealthHistory.Add(ServiceKey, TArray<FServiceHealthSnapshot>());
        }
        
        // Add snapshot to history
        HealthHistory[ServiceKey].Insert(Snapshot, 0);
        
        // Trim history if needed
        if (HealthHistory[ServiceKey].Num() > Configuration.MaxHealthHistoryEntries)
        {
            HealthHistory[ServiceKey].RemoveAt(Configuration.MaxHealthHistoryEntries, 
                HealthHistory[ServiceKey].Num() - Configuration.MaxHealthHistoryEntries);
        }
    }
    
    // Handle status change
    if (bStatusChanged)
    {
        HandleHealthStatusChange(ServiceKey, PreviousStatus, CurrentStatus, Snapshot);
    }
    
    // Check for degradation if enabled
    if (Configuration.bEnableDegradationDetection)
    {
        float DegradationRate = 0.0f;
        if (DetectDegradation(ServiceKey, &DegradationRate))
        {
            UE_LOG(LogMiningRegistry, Warning, TEXT("FServiceHealthMonitor::CheckServiceHealth - Service %s is degrading (rate: %.2f)"), 
                *ServiceKey.ToString(), DegradationRate);
        }
    }
    
    // Predict failure if enabled and service is not already failed
    if (Configuration.bEnableFailurePrediction && 
        CurrentStatus != EServiceHealthStatus::Failed && 
        CurrentStatus != EServiceHealthStatus::CriticalFailure)
    {
        float TimeToFailure = 0.0f;
        float Confidence = 0.0f;
        
        if (PredictFailure(ServiceKey, TimeToFailure, Confidence))
        {
            UE_LOG(LogMiningRegistry, Warning, TEXT("FServiceHealthMonitor::CheckServiceHealth - Service %s predicted to fail in %.2f seconds (confidence: %.2f)"), 
                *ServiceKey.ToString(), TimeToFailure, Confidence);
            
            // Broadcast prediction
            OnFailurePredictedDelegate.Broadcast(ServiceKey, TimeToFailure, Snapshot);
        }
    }
    
    return Snapshot;
}

bool FServiceHealthMonitor::RecoverService(const FName& ServiceKey, bool bPreserveState)
{
    if (!bIsInitialized || !bIsEnabled)
    {
        return false;
    }
    
    // Ensure service manager is valid
    if (!ServiceManager)
    {
        UE_LOG(LogMiningRegistry, Error, TEXT("FServiceHealthMonitor::RecoverService - Invalid service manager"));
        return false;
    }
    
    // Get service instance
    const FServiceInstance* ServiceInstance = ServiceManager->GetServiceInstanceByKey(ServiceKey);
    if (!ServiceInstance)
    {
        UE_LOG(LogMiningRegistry, Warning, TEXT("FServiceHealthMonitor::RecoverService - Service not found: %s"), *ServiceKey.ToString());
        return false;
    }
    
    // Check recovery cooldown
    double CurrentTime = FPlatformTime::Seconds();
    {
        FScopeLock Lock(&MonitorLock);
        
        if (LastRecoveryTime.Contains(ServiceKey))
        {
            double TimeSinceLastRecovery = CurrentTime - LastRecoveryTime[ServiceKey];
            if (TimeSinceLastRecovery < Configuration.RecoveryCooldownSec)
            {
                UE_LOG(LogMiningRegistry, Warning, TEXT("FServiceHealthMonitor::RecoverService - Recovery for %s skipped due to cooldown (%.2f seconds remaining)"), 
                    *ServiceKey.ToString(), Configuration.RecoveryCooldownSec - TimeSinceLastRecovery);
                return false;
            }
        }
        
        // Check recovery attempts
        int32 AttemptCount = RecoveryAttempts.FindRef(ServiceKey);
        if (AttemptCount >= Configuration.MaxAutoRecoveryAttempts)
        {
            UE_LOG(LogMiningRegistry, Error, TEXT("FServiceHealthMonitor::RecoverService - Maximum recovery attempts (%d) reached for %s"), 
                Configuration.MaxAutoRecoveryAttempts, *ServiceKey.ToString());
            return false;
        }
        
        // Increment attempt counter
        RecoveryAttempts.Add(ServiceKey, AttemptCount + 1);
        
        // Update last recovery time
        LastRecoveryTime.Add(ServiceKey, CurrentTime);
    }
    
    // Attempt to restart the service
    bool bSuccess = ServiceManager->RestartService(ServiceInstance->InterfaceType, ServiceInstance->ZoneID, ServiceInstance->RegionID, bPreserveState);
    
    if (bSuccess)
    {
        UE_LOG(LogMiningRegistry, Log, TEXT("FServiceHealthMonitor::RecoverService - Successfully recovered service %s"), 
            *ServiceKey.ToString());
        
        // Update health status
        FScopeLock Lock(&MonitorLock);
        CurrentHealthStatus.Add(ServiceKey, EServiceHealthStatus::Warning);
    }
    else
    {
        UE_LOG(LogMiningRegistry, Error, TEXT("FServiceHealthMonitor::RecoverService - Failed to recover service %s"), 
            *ServiceKey.ToString());
    }
    
    return bSuccess;
}

EServiceHealthStatus FServiceHealthMonitor::GetServiceHealthStatus(const FName& ServiceKey) const
{
    FScopeLock Lock(&MonitorLock);
    
    if (CurrentHealthStatus.Contains(ServiceKey))
    {
        return CurrentHealthStatus[ServiceKey];
    }
    
    return EServiceHealthStatus::Healthy;
}

TArray<FServiceHealthSnapshot> FServiceHealthMonitor::GetServiceHealthHistory(const FName& ServiceKey) const
{
    FScopeLock Lock(&MonitorLock);
    
    if (HealthHistory.Contains(ServiceKey))
    {
        return HealthHistory[ServiceKey];
    }
    
    return TArray<FServiceHealthSnapshot>();
}

float FServiceHealthMonitor::PredictTimeToFailure(const FName& ServiceKey, float* OutConfidence) const
{
    if (!bIsInitialized || !bIsEnabled)
    {
        if (OutConfidence)
        {
            *OutConfidence = 0.0f;
        }
        return -1.0f;
    }
    
    // Get current health status
    EServiceHealthStatus Status = GetServiceHealthStatus(ServiceKey);
    
    // Already failed
    if (Status == EServiceHealthStatus::Failed || Status == EServiceHealthStatus::CriticalFailure)
    {
        if (OutConfidence)
        {
            *OutConfidence = 1.0f;
        }
        return 0.0f;
    }
    
    // Healthy, not predicted to fail
    if (Status == EServiceHealthStatus::Healthy)
    {
        if (OutConfidence)
        {
            *OutConfidence = 0.0f;
        }
        return -1.0f;
    }
    
    // Perform prediction
    float TimeToFailure = 0.0f;
    float Confidence = 0.0f;
    bool bPredicted = PredictFailure(ServiceKey, TimeToFailure, Confidence);
    
    if (OutConfidence)
    {
        *OutConfidence = Confidence;
    }
    
    if (bPredicted)
    {
        return TimeToFailure;
    }
    
    return -1.0f;
}

TMap<FName, FServiceHealthSnapshot> FServiceHealthMonitor::GetAllServicesHealth() const
{
    TMap<FName, FServiceHealthSnapshot> Result;
    
    if (!bIsInitialized || !bIsEnabled)
    {
        return Result;
    }
    
    FScopeLock Lock(&MonitorLock);
    
    // For each service with health history, get the most recent snapshot
    for (const auto& Pair : HealthHistory)
    {
        if (Pair.Value.Num() > 0)
        {
            Result.Add(Pair.Key, Pair.Value[0]);
        }
    }
    
    return Result;
}

void FServiceHealthMonitor::SetPredictionModel(EPredictionModel Model)
{
    CurrentPredictionModel = Model;
}

FDelegateHandle FServiceHealthMonitor::RegisterHealthChangeCallback(const FOnServiceHealthChanged::FDelegate& Delegate)
{
    return OnHealthChangedDelegate.Add(Delegate);
}

FDelegateHandle FServiceHealthMonitor::RegisterFailurePredictionCallback(const FOnServiceFailurePredicted::FDelegate& Delegate)
{
    return OnFailurePredictedDelegate.Add(Delegate);
}

void FServiceHealthMonitor::UnregisterHealthChangeCallback(FDelegateHandle Handle)
{
    OnHealthChangedDelegate.Remove(Handle);
}

void FServiceHealthMonitor::UnregisterFailurePredictionCallback(FDelegateHandle Handle)
{
    OnFailurePredictedDelegate.Remove(Handle);
}

void FServiceHealthMonitor::RecordServiceOperation(const FName& ServiceKey, bool bSuccess, double ResponseTimeMs, 
    EServiceFailureReason FailureReason, const FString& DiagnosticMessage)
{
    if (!bIsInitialized || !bIsEnabled)
    {
        return;
    }
    
    // Ensure service manager is valid
    if (!ServiceManager)
    {
        UE_LOG(LogMiningRegistry, Error, TEXT("FServiceHealthMonitor::RecordServiceOperation - Invalid service manager"));
        return;
    }
    
    // Get service instance
    const FServiceInstance* ServiceInstance = ServiceManager->GetServiceInstanceByKey(ServiceKey);
    if (!ServiceInstance)
    {
        UE_LOG(LogMiningRegistry, Warning, TEXT("FServiceHealthMonitor::RecordServiceOperation - Service not found: %s"), *ServiceKey.ToString());
        return;
    }
    
    // If operation failed, update health status
    if (!bSuccess)
    {
        // Get current status
        EServiceHealthStatus PreviousStatus = GetServiceHealthStatus(ServiceKey);
        EServiceHealthStatus NewStatus = PreviousStatus;
        
        // Determine new status based on failure severity
        switch (FailureReason)
        {
            case EServiceFailureReason::Exception:
            case EServiceFailureReason::MemoryAllocation:
            case EServiceFailureReason::ResourceExhaustion:
                NewStatus = EServiceHealthStatus::Failed;
                break;
                
            case EServiceFailureReason::Timeout:
            case EServiceFailureReason::InvalidState:
                NewStatus = EServiceHealthStatus::SeverelyDegraded;
                break;
                
            case EServiceFailureReason::DependencyFailure:
            case EServiceFailureReason::ExternalSystem:
            case EServiceFailureReason::Authorization:
                NewStatus = EServiceHealthStatus::Degraded;
                break;
                
            default:
                NewStatus = EServiceHealthStatus::Warning;
                break;
        }
        
        // Only downgrade status (make worse), never upgrade (make better)
        if (static_cast<uint8>(NewStatus) > static_cast<uint8>(PreviousStatus))
        {
            // Create snapshot
            FServiceHealthSnapshot Snapshot = CreateHealthSnapshot(
                ServiceKey, 
                NewStatus, 
                ServiceInstance->State, 
                ServiceInstance->Metrics, 
                0, // No memory usage info
                ResponseTimeMs,
                FailureReason, 
                DiagnosticMessage);
            
            // Update status and add to history
            {
                FScopeLock Lock(&MonitorLock);
                
                // Update current status
                CurrentHealthStatus.Add(ServiceKey, NewStatus);
                
                // Add to history
                if (!HealthHistory.Contains(ServiceKey))
                {
                    HealthHistory.Add(ServiceKey, TArray<FServiceHealthSnapshot>());
                }
                
                HealthHistory[ServiceKey].Insert(Snapshot, 0);
                
                // Trim history if needed
                if (HealthHistory[ServiceKey].Num() > Configuration.MaxHealthHistoryEntries)
                {
                    HealthHistory[ServiceKey].RemoveAt(Configuration.MaxHealthHistoryEntries, 
                        HealthHistory[ServiceKey].Num() - Configuration.MaxHealthHistoryEntries);
                }
            }
            
            // Handle status change
            HandleHealthStatusChange(ServiceKey, PreviousStatus, NewStatus, Snapshot);
        }
    }
    
    // Update service metrics in manager
    if (ServiceManager)
    {
        ServiceManager->RecordServiceOperation(ServiceKey, bSuccess, ResponseTimeMs);
        
        // Update service metrics directly
        // Make a non-const copy of the metrics to modify them
        auto& Metrics = const_cast<FServiceMetrics&>(ServiceInstance->Metrics);
        Metrics.SuccessfulOperations.Add(bSuccess ? 1 : 0);
        Metrics.FailedOperations.Add(bSuccess ? 0 : 1);
        Metrics.TotalOperationTimeMs.Add(static_cast<int64>(ResponseTimeMs));
        // No direct way to just update memory usage in ServiceManager
        // This is just tracking for health monitor visualization
    }
    
    // No need to update health snapshot, will be captured in next health check
}

void FServiceHealthMonitor::SetEnabled(bool bEnable)
{
    bIsEnabled = bEnable;
    
    if (bEnable)
    {
        UE_LOG(LogMiningRegistry, Log, TEXT("FServiceHealthMonitor::SetEnabled - Health monitor enabled"));
    }
    else
    {
        UE_LOG(LogMiningRegistry, Log, TEXT("FServiceHealthMonitor::SetEnabled - Health monitor disabled"));
    }
}

bool FServiceHealthMonitor::IsEnabled() const
{
    return bIsEnabled;
}

void FServiceHealthMonitor::UpdateConfiguration(const FHealthMonitorConfig& NewConfig)
{
    Configuration = NewConfig;
}

const FHealthMonitorConfig& FServiceHealthMonitor::GetConfiguration() const
{
    return Configuration;
}

void FServiceHealthMonitor::Tick(float DeltaTime)
{
    if (!bIsInitialized || !bIsEnabled)
    {
        return;
    }
    
    // Check if it's time for the next health check
    double CurrentTime = FPlatformTime::Seconds();
    if (CurrentTime >= NextHealthCheckTime)
    {
        CheckAllServicesHealth();
    }
}

bool FServiceHealthMonitor::IsTickable() const
{
    return bIsInitialized && bIsEnabled;
}

TStatId FServiceHealthMonitor::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(FServiceHealthMonitor, STATGROUP_Tickables);
}

TPair<EServiceHealthStatus, FString> FServiceHealthMonitor::AnalyzeServiceHealth(const FName& ServiceKey, 
    const FServiceMetrics& Metrics, EServiceState State) const
{
    // Default result
    TPair<EServiceHealthStatus, FString> Result(EServiceHealthStatus::Healthy, TEXT("Service is healthy"));
    
    // If service is not active, it's not healthy
    if (State != EServiceState::Active)
    {
        if (State == EServiceState::Failing)
        {
            Result.Key = EServiceHealthStatus::Failed;
            Result.Value = TEXT("Service is in failing state");
        }
        else if (State == EServiceState::Initializing)
        {
            Result.Key = EServiceHealthStatus::Warning;
            Result.Value = TEXT("Service is initializing");
        }
        else if (State == EServiceState::ShuttingDown)
        {
            Result.Key = EServiceHealthStatus::Warning;
            Result.Value = TEXT("Service is shutting down");
        }
        else
        {
            Result.Key = EServiceHealthStatus::CriticalFailure;
            Result.Value = TEXT("Service is not active");
        }
        
        return Result;
    }
    
    // Calculate failure rate
    int64 TotalOperations = Metrics.SuccessfulOperations.GetValue() + Metrics.FailedOperations.GetValue();
    float FailureRate = 0.0f;
    
    if (TotalOperations > 0)
    {
        FailureRate = static_cast<float>(Metrics.FailedOperations.GetValue()) / static_cast<float>(TotalOperations);
    }
    
    // Calculate average response time
    float AvgResponseTime = 0.0f;
    if (TotalOperations > 0)
    {
        AvgResponseTime = static_cast<float>(Metrics.TotalOperationTimeMs.GetValue()) / static_cast<float>(TotalOperations);
    }
    
    // Check for failure conditions
    if (FailureRate >= Configuration.SeverelyDegradedThreshold)
    {
        Result.Key = EServiceHealthStatus::SeverelyDegraded;
        Result.Value = FString::Printf(TEXT("High failure rate (%.1f%%)"), FailureRate * 100.0f);
    }
    else if (FailureRate >= Configuration.DegradedThreshold)
    {
        Result.Key = EServiceHealthStatus::Degraded;
        Result.Value = FString::Printf(TEXT("Elevated failure rate (%.1f%%)"), FailureRate * 100.0f);
    }
    else if (FailureRate >= Configuration.WarningThreshold)
    {
        Result.Key = EServiceHealthStatus::Warning;
        Result.Value = FString::Printf(TEXT("Minor failure rate (%.1f%%)"), FailureRate * 100.0f);
    }
    
    // Check for very recent failures
    double TimeSinceLastFailure = FPlatformTime::Seconds() - Metrics.LastFailureTime;
    if (Metrics.LastFailureTime > 0.0 && TimeSinceLastFailure < 60.0)
    {
        // Increase severity if failure was very recent
        if (Result.Key == EServiceHealthStatus::Healthy)
        {
            Result.Key = EServiceHealthStatus::Warning;
            Result.Value = FString::Printf(TEXT("Recent failure (%.1f seconds ago)"), TimeSinceLastFailure);
        }
        else if (Result.Key == EServiceHealthStatus::Warning)
        {
            Result.Key = EServiceHealthStatus::Degraded;
            Result.Value = FString::Printf(TEXT("Recent failure (%.1f seconds ago) with elevated failure rate"), TimeSinceLastFailure);
        }
    }
    
    // Check if response time is concerning
    if (AvgResponseTime > 1000.0f) // More than 1 second
    {
        if (Result.Key == EServiceHealthStatus::Healthy)
        {
            Result.Key = EServiceHealthStatus::Warning;
            Result.Value = FString::Printf(TEXT("High average response time (%.1f ms)"), AvgResponseTime);
        }
        else if (Result.Key != EServiceHealthStatus::SeverelyDegraded && Result.Key != EServiceHealthStatus::Failed)
        {
            Result.Key = EServiceHealthStatus::Degraded;
            Result.Value += FString::Printf(TEXT(" and high response time (%.1f ms)"), AvgResponseTime);
        }
    }
    
    return Result;
}

FServiceHealthSnapshot FServiceHealthMonitor::CreateHealthSnapshot(const FName& ServiceKey, 
    EServiceHealthStatus Status, EServiceState State, const FServiceMetrics& Metrics,
    uint64 MemoryUsage, double ResponseTime, EServiceFailureReason Reason,
    const FString& Message)
{
    FServiceHealthSnapshot Snapshot;
    
    Snapshot.Timestamp = FDateTime::UtcNow();
    Snapshot.Status = Status;
    Snapshot.State = State;
    Snapshot.Metrics = Metrics;
    Snapshot.MemoryUsage = MemoryUsage;
    Snapshot.ResponseTimeMs = ResponseTime;
    Snapshot.FailureReason = Reason;
    Snapshot.DiagnosticMessage = Message;
    
    return Snapshot;
}

void FServiceHealthMonitor::HandleHealthStatusChange(const FName& ServiceKey, EServiceHealthStatus OldStatus, 
    EServiceHealthStatus NewStatus, const FServiceHealthSnapshot& Snapshot)
{
    // Log status change
    UE_LOG(LogMiningRegistry, Log, TEXT("FServiceHealthMonitor::HandleHealthStatusChange - Service %s health changed from %d to %d: %s"),
        *ServiceKey.ToString(), static_cast<int32>(OldStatus), static_cast<int32>(NewStatus), *Snapshot.DiagnosticMessage);
    
    // Broadcast status change
    OnHealthChangedDelegate.Broadcast(ServiceKey, NewStatus, Snapshot);
    
    // If service degraded to Failed or worse, and auto-recovery is enabled, attempt recovery
    if ((NewStatus == EServiceHealthStatus::Failed || NewStatus == EServiceHealthStatus::CriticalFailure) &&
        Configuration.bEnableAutoRecovery)
    {
        // Check if we've already attempted recovery too many times
        int32 AttemptCount = RecoveryAttempts.FindRef(ServiceKey);
        if (AttemptCount < Configuration.MaxAutoRecoveryAttempts)
        {
            // Queue recovery on next tick to avoid recursion/reentrance issues
            NextHealthCheckTime = 0.0; // Force immediate check on next tick
        }
    }
}

bool FServiceHealthMonitor::DetectDegradation(const FName& ServiceKey, float* OutDegradationRate) const
{
    if (!bIsInitialized || !bIsEnabled)
    {
        if (OutDegradationRate)
        {
            *OutDegradationRate = 0.0f;
        }
        return false;
    }
    
    FScopeLock Lock(&MonitorLock);
    
    // Need at least 5 entries to detect a trend
    if (!HealthHistory.Contains(ServiceKey) || HealthHistory[ServiceKey].Num() < 5)
    {
        if (OutDegradationRate)
        {
            *OutDegradationRate = 0.0f;
        }
        return false;
    }
    
    const TArray<FServiceHealthSnapshot>& History = HealthHistory[ServiceKey];
    
    // Calculate trend in failure rate
    float PreviousFailureRate = 0.0f;
    float CurrentFailureRate = 0.0f;
    
    // Calculate recent failure rate (last 2 entries)
    int64 RecentSuccessful = 0;
    int64 RecentFailed = 0;
    
    for (int32 i = 0; i < 2 && i < History.Num(); ++i)
    {
        RecentSuccessful += History[i].Metrics.SuccessfulOperations.GetValue();
        RecentFailed += History[i].Metrics.FailedOperations.GetValue();
    }
    
    int64 RecentTotal = RecentSuccessful + RecentFailed;
    if (RecentTotal > 0)
    {
        CurrentFailureRate = static_cast<float>(RecentFailed) / static_cast<float>(RecentTotal);
    }
    
    // Calculate older failure rate (entries 3-5)
    int64 OlderSuccessful = 0;
    int64 OlderFailed = 0;
    
    for (int32 i = 2; i < 5 && i < History.Num(); ++i)
    {
        OlderSuccessful += History[i].Metrics.SuccessfulOperations.GetValue();
        OlderFailed += History[i].Metrics.FailedOperations.GetValue();
    }
    
    int64 OlderTotal = OlderSuccessful + OlderFailed;
    if (OlderTotal > 0)
    {
        PreviousFailureRate = static_cast<float>(OlderFailed) / static_cast<float>(OlderTotal);
    }
    
    // Calculate degradation rate (positive means getting worse)
    float DegradationRate = CurrentFailureRate - PreviousFailureRate;
    
    if (OutDegradationRate)
    {
        *OutDegradationRate = DegradationRate;
    }
    
    // Degradation detected if rate is positive and current rate is above warning threshold
    return (DegradationRate > 0.01f && CurrentFailureRate > Configuration.WarningThreshold);
}

bool FServiceHealthMonitor::PredictFailure(const FName& ServiceKey, float& OutTimeToFailure, float& OutConfidence) const
{
    // Select prediction model based on configuration
    switch (CurrentPredictionModel)
    {
        case EPredictionModel::ThresholdBased:
        {
            FScopeLock Lock(&MonitorLock);
            if (!HealthHistory.Contains(ServiceKey))
            {
                OutTimeToFailure = -1.0f;
                OutConfidence = 0.0f;
                return false;
            }
            
            return PredictFailureThresholdBased(ServiceKey, HealthHistory[ServiceKey], OutTimeToFailure, OutConfidence);
        }
        
        case EPredictionModel::TrendAnalysis:
        {
            FScopeLock Lock(&MonitorLock);
            if (!HealthHistory.Contains(ServiceKey))
            {
                OutTimeToFailure = -1.0f;
                OutConfidence = 0.0f;
                return false;
            }
            
            return PredictFailureTrendAnalysis(ServiceKey, HealthHistory[ServiceKey], OutTimeToFailure, OutConfidence);
        }
        
        case EPredictionModel::ResourceProjection:
        {
            FScopeLock Lock(&MonitorLock);
            if (!HealthHistory.Contains(ServiceKey))
            {
                OutTimeToFailure = -1.0f;
                OutConfidence = 0.0f;
                return false;
            }
            
            return PredictFailureResourceProjection(ServiceKey, HealthHistory[ServiceKey], OutTimeToFailure, OutConfidence);
        }
        
        default:
        {
            FScopeLock Lock(&MonitorLock);
            if (!HealthHistory.Contains(ServiceKey))
            {
                OutTimeToFailure = -1.0f;
                OutConfidence = 0.0f;
                return false;
            }
            
            return PredictFailureThresholdBased(ServiceKey, HealthHistory[ServiceKey], OutTimeToFailure, OutConfidence);
        }
    }
}

bool FServiceHealthMonitor::PredictFailureThresholdBased(const FName& ServiceKey, const TArray<FServiceHealthSnapshot>& History,
    float& OutTimeToFailure, float& OutConfidence) const
{
    // Need some history to make predictions
    if (History.Num() < 2)
    {
        OutTimeToFailure = -1.0f;
        OutConfidence = 0.0f;
        return false;
    }
    
    // Get current health status
    EServiceHealthStatus CurrentStatus = History[0].Status;
    
    // Already failed
    if (CurrentStatus == EServiceHealthStatus::Failed || CurrentStatus == EServiceHealthStatus::CriticalFailure)
    {
        OutTimeToFailure = 0.0f;
        OutConfidence = 1.0f;
        return true;
    }
    
    // If severely degraded, predict failure soon
    if (CurrentStatus == EServiceHealthStatus::SeverelyDegraded)
    {
        OutTimeToFailure = 60.0f; // Assume 1 minute to failure
        OutConfidence = 0.8f;
        return true;
    }
    
    // If degraded, predict failure based on history
    if (CurrentStatus == EServiceHealthStatus::Degraded)
    {
        // Count how many consecutive snapshots have been degraded
        int32 ConsecutiveDegraded = 0;
        for (int32 i = 0; i < History.Num(); ++i)
        {
            if (History[i].Status == EServiceHealthStatus::Degraded || 
                History[i].Status == EServiceHealthStatus::SeverelyDegraded)
            {
                ConsecutiveDegraded++;
            }
            else
            {
                break;
            }
        }
        
        if (ConsecutiveDegraded >= 3)
        {
            // More consecutive degraded snapshots increases confidence
            float ConfidenceFactor = FMath::Min(0.7f + (ConsecutiveDegraded - 3) * 0.05f, 0.9f);
            
            OutTimeToFailure = 300.0f - ConsecutiveDegraded * 30.0f; // 5 minutes minus 30 seconds per degraded snapshot
            OutTimeToFailure = FMath::Max(OutTimeToFailure, 60.0f); // At least 1 minute
            OutConfidence = ConfidenceFactor;
            return true;
        }
    }
    
    // Check failure rate trend
    float DegradationRate = 0.0f;
    bool bIsDegrading = DetectDegradation(ServiceKey, &DegradationRate);
    
    if (bIsDegrading && DegradationRate > 0.05f)
    {
        // High degradation rate, predict failure
        OutTimeToFailure = 600.0f - DegradationRate * 3000.0f; // 10 minutes minus 50 minutes per 0.1 degradation rate
        OutTimeToFailure = FMath::Max(OutTimeToFailure, 120.0f); // At least 2 minutes
        OutConfidence = 0.5f + DegradationRate * 2.0f; // Confidence increases with degradation rate
        OutConfidence = FMath::Min(OutConfidence, 0.8f); // Cap at 0.8
        return true;
    }
    
    // No failure predicted
    OutTimeToFailure = -1.0f;
    OutConfidence = 0.0f;
    return false;
}

bool FServiceHealthMonitor::PredictFailureTrendAnalysis(const FName& ServiceKey, const TArray<FServiceHealthSnapshot>& History,
    float& OutTimeToFailure, float& OutConfidence) const
{
    // Need decent amount of history for trend analysis
    if (History.Num() < 5)
    {
        OutTimeToFailure = -1.0f;
        OutConfidence = 0.0f;
        return false;
    }
    
    // Get current health status
    EServiceHealthStatus CurrentStatus = History[0].Status;
    
    // Already failed
    if (CurrentStatus == EServiceHealthStatus::Failed || CurrentStatus == EServiceHealthStatus::CriticalFailure)
    {
        OutTimeToFailure = 0.0f;
        OutConfidence = 1.0f;
        return true;
    }
    
    // Calculate failure rate trend over time
    TArray<float> FailureRates;
    TArray<double> TimePoints;
    
    for (int32 i = 0; i < History.Num(); ++i)
    {
        const FServiceMetrics& Metrics = History[i].Metrics;
        int64 TotalOps = Metrics.SuccessfulOperations.GetValue() + Metrics.FailedOperations.GetValue();
        
        if (TotalOps > 0)
        {
            float FailureRate = static_cast<float>(Metrics.FailedOperations.GetValue()) / static_cast<float>(TotalOps);
            FailureRates.Add(FailureRate);
            
            // Time point in seconds since first sample
            double TimePoint = 0.0;
            if (i < History.Num() - 1)
            {
                FTimespan TimeDiff = History[i].Timestamp - History[History.Num() - 1].Timestamp;
                TimePoint = TimeDiff.GetTotalSeconds();
            }
            TimePoints.Add(TimePoint);
        }
    }
    
    // Need at least 3 points for linear regression
    if (FailureRates.Num() < 3)
    {
        OutTimeToFailure = -1.0f;
        OutConfidence = 0.0f;
        return false;
    }
    
    // Simple linear regression
    float SumX = 0.0f, SumY = 0.0f, SumXY = 0.0f, SumXX = 0.0f;
    int32 N = FMath::Min(FailureRates.Num(), TimePoints.Num());
    
    for (int32 i = 0; i < N; ++i)
    {
        float X = static_cast<float>(TimePoints[i]);
        float Y = FailureRates[i];
        
        SumX += X;
        SumY += Y;
        SumXY += X * Y;
        SumXX += X * X;
    }
    
    float Slope = 0.0f;
    float Intercept = 0.0f;
    
    if (N * SumXX - SumX * SumX != 0.0f)
    {
        Slope = (N * SumXY - SumX * SumY) / (N * SumXX - SumX * SumX);
        Intercept = (SumY - Slope * SumX) / N;
    }
    
    // If slope is positive or very small, failure rate isn't increasing significantly
    if (Slope <= 0.0001f)
    {
        OutTimeToFailure = -1.0f;
        OutConfidence = 0.0f;
        return false;
    }
    
    // Predict time to reach critical failure rate (0.5)
    float TargetRate = 0.5f; // 50% failure rate
    float CurrentRate = FailureRates[0];
    
    if (CurrentRate >= TargetRate)
    {
        // Already at critical rate
        OutTimeToFailure = 0.0f;
        OutConfidence = 0.9f;
        return true;
    }
    
    // Time to reach target rate
    float TimeToTarget = (TargetRate - CurrentRate) / Slope;
    
    // Calculate R-squared to determine confidence
    float MeanY = SumY / N;
    float TotalSS = 0.0f, ResiduaSS = 0.0f;
    
    for (int32 i = 0; i < N; ++i)
    {
        float X = static_cast<float>(TimePoints[i]);
        float Y = FailureRates[i];
        float PredictedY = Intercept + Slope * X;
        
        TotalSS += (Y - MeanY) * (Y - MeanY);
        ResiduaSS += (Y - PredictedY) * (Y - PredictedY);
    }
    
    float RSquared = 0.0f;
    if (TotalSS > 0.0f)
    {
        RSquared = 1.0f - (ResiduaSS / TotalSS);
    }
    
    // Set outputs
    OutTimeToFailure = TimeToTarget;
    OutConfidence = FMath::Clamp(RSquared, 0.0f, 0.95f); // Cap confidence at 0.95
    
    return true;
}

bool FServiceHealthMonitor::PredictFailureResourceProjection(const FName& ServiceKey, const TArray<FServiceHealthSnapshot>& History,
    float& OutTimeToFailure, float& OutConfidence) const
{
    // Need history with memory usage information
    if (History.Num() < 3)
    {
        OutTimeToFailure = -1.0f;
        OutConfidence = 0.0f;
        return false;
    }
    
    // Check if we have memory usage information
    bool bHasMemoryInfo = false;
    for (const FServiceHealthSnapshot& Snapshot : History)
    {
        if (Snapshot.MemoryUsage > 0)
        {
            bHasMemoryInfo = true;
            break;
        }
    }
    
    if (!bHasMemoryInfo)
    {
        // Fall back to trend analysis
        return PredictFailureTrendAnalysis(ServiceKey, History, OutTimeToFailure, OutConfidence);
    }
    
    // Calculate memory usage trend
    TArray<uint64> MemoryUsages;
    TArray<double> TimePoints;
    
    for (int32 i = 0; i < History.Num(); ++i)
    {
        if (History[i].MemoryUsage > 0)
        {
            MemoryUsages.Add(History[i].MemoryUsage);
            
            // Time point in seconds since first sample
            double TimePoint = 0.0;
            if (i < History.Num() - 1)
            {
                FTimespan TimeDiff = History[i].Timestamp - History[History.Num() - 1].Timestamp;
                TimePoint = TimeDiff.GetTotalSeconds();
            }
            TimePoints.Add(TimePoint);
        }
    }
    
    // Need at least 3 points for linear regression
    if (MemoryUsages.Num() < 3)
    {
        OutTimeToFailure = -1.0f;
        OutConfidence = 0.0f;
        return false;
    }
    
    // Simple linear regression for memory usage
    double SumX = 0.0, SumY = 0.0, SumXY = 0.0, SumXX = 0.0;
    int32 N = FMath::Min(MemoryUsages.Num(), TimePoints.Num());
    
    for (int32 i = 0; i < N; ++i)
    {
        double X = TimePoints[i];
        double Y = static_cast<double>(MemoryUsages[i]);
        
        SumX += X;
        SumY += Y;
        SumXY += X * Y;
        SumXX += X * X;
    }
    
    double Slope = 0.0;
    double Intercept = 0.0;
    
    if (N * SumXX - SumX * SumX != 0.0)
    {
        Slope = (N * SumXY - SumX * SumY) / (N * SumXX - SumX * SumX);
        Intercept = (SumY - Slope * SumX) / N;
    }
    
    // If slope is negative or very small, memory usage isn't increasing
    if (Slope <= 1000.0) // Less than 1KB/s growth
    {
        OutTimeToFailure = -1.0f;
        OutConfidence = 0.0f;
        return false;
    }
    
    // Predict time to reach critical memory usage
    // Assume 2GB as critical for this simple model
    const uint64 CriticalMemory = 2ULL * 1024ULL * 1024ULL * 1024ULL; // 2GB
    uint64 CurrentMemory = MemoryUsages[0];
    
    if (CurrentMemory >= CriticalMemory)
    {
        // Already at critical memory
        OutTimeToFailure = 0.0f;
        OutConfidence = 0.9f;
        return true;
    }
    
    // Time to reach critical memory
    double TimeToTarget = (CriticalMemory - CurrentMemory) / Slope;
    
    // Calculate R-squared to determine confidence
    double MeanY = SumY / N;
    double TotalSS = 0.0, ResiduaSS = 0.0;
    
    for (int32 i = 0; i < N; ++i)
    {
        double X = TimePoints[i];
        double Y = static_cast<double>(MemoryUsages[i]);
        double PredictedY = Intercept + Slope * X;
        
        TotalSS += (Y - MeanY) * (Y - MeanY);
        ResiduaSS += (Y - PredictedY) * (Y - PredictedY);
    }
    
    double RSquared = 0.0;
    if (TotalSS > 0.0)
    {
        RSquared = 1.0 - (ResiduaSS / TotalSS);
    }
    
    // Set outputs
    OutTimeToFailure = static_cast<float>(TimeToTarget);
    OutConfidence = FMath::Clamp(static_cast<float>(RSquared), 0.0f, 0.9f); // Cap confidence at 0.9
    
    return true;
}
