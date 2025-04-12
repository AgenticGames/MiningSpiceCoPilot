// ServiceMonitor.cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "ServiceMonitor.h"
#include "Logging/LogMacros.h"
#include "Interfaces/IDependencyServiceLocator.h"

// Static instance initialization
FServiceMonitor* FServiceMonitor::Instance = nullptr;

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogServiceMonitor, Log, All);

FServiceMonitor::FServiceMonitor()
    : bIsInitialized(false)
    , HealthCheckInterval(5.0f)
    , TimeSinceLastHealthCheck(0.0f)
    , MinTimeBetweenRecoveries(30.0f)
    , MaxRecoveryAttempts(3)
{
    // Initialize empty
}

FServiceMonitor::~FServiceMonitor()
{
    // Ensure we're shut down
    if (bIsInitialized)
    {
        Shutdown();
    }
}

bool FServiceMonitor::Initialize()
{
    FScopeLock Lock(&CriticalSection);
    
    if (bIsInitialized)
    {
        UE_LOG(LogServiceMonitor, Warning, TEXT("ServiceMonitor already initialized"));
        return true;
    }
    
    UE_LOG(LogServiceMonitor, Log, TEXT("Initializing ServiceMonitor"));
    MonitoredServices.Empty();
    TimeSinceLastHealthCheck = 0.0f;
    bIsInitialized = true;
    
    return true;
}

void FServiceMonitor::Shutdown()
{
    FScopeLock Lock(&CriticalSection);
    
    if (!bIsInitialized)
    {
        UE_LOG(LogServiceMonitor, Warning, TEXT("ServiceMonitor not initialized, cannot shutdown"));
        return;
    }
    
    UE_LOG(LogServiceMonitor, Log, TEXT("Shutting down ServiceMonitor"));
    MonitoredServices.Empty();
    bIsInitialized = false;
}

bool FServiceMonitor::IsInitialized() const
{
    return bIsInitialized;
}

void FServiceMonitor::Update(float DeltaTime)
{
    if (!bIsInitialized)
    {
        return;
    }
    
    // Update time since last health check
    TimeSinceLastHealthCheck += DeltaTime;
    
    // Check if it's time for a health check
    if (TimeSinceLastHealthCheck >= HealthCheckInterval)
    {
        TimeSinceLastHealthCheck = 0.0f;
        
        FScopeLock Lock(&CriticalSection);
        
        // Update metrics for all services
        for (auto& ServicePair : MonitoredServices)
        {
            FServiceKey ServiceKey = ServicePair.Key;
            FMonitoringInfo& MonitoringInfo = ServicePair.Value;
            
            // Update time since last check
            MonitoringInfo.TimeSinceLastCheck += HealthCheckInterval;
            MonitoringInfo.Metrics.TimeSinceLastCheck = MonitoringInfo.TimeSinceLastCheck;
            
            // Additional health metrics could be collected here
            // For example, checking memory usage, CPU usage, etc.
            
            // Update health status
            MonitoringInfo.UpdateHealthStatus();
            
            // Check if service needs recovery and attempt if needed
            if (NeedsRecovery(MonitoringInfo) && !MonitoringInfo.bRecoveryInProgress)
            {
                // Attempt to recover the service
                FDateTime CurrentTime = FDateTime::Now();
                float TimeSinceLastRecovery = (MonitoringInfo.LastRecoveryTime == FDateTime::MinValue()) ? 
                    MinTimeBetweenRecoveries + 1.0f :
                    (CurrentTime - MonitoringInfo.LastRecoveryTime).GetTotalSeconds();
                
                if (MonitoringInfo.RecoveryAttempts < MaxRecoveryAttempts && 
                    TimeSinceLastRecovery >= MinTimeBetweenRecoveries)
                {
                    // Attempt recovery
                    MonitoringInfo.bRecoveryInProgress = true;
                    
                    UE_LOG(LogServiceMonitor, Warning, TEXT("Attempting to recover service: %s"), 
                        *ServiceKey.ToString());
                    
                    if (RecoverService(ServiceKey.InterfaceType, ServiceKey.ZoneID, ServiceKey.RegionID))
                    {
                        // Recovery successful
                        MonitoringInfo.RecoveryAttempts = 0;
                        MonitoringInfo.Metrics.RecoveryCount++;
                        ResetOperationCounters(MonitoringInfo);
                        UE_LOG(LogServiceMonitor, Log, TEXT("Service recovery successful: %s"), 
                            *ServiceKey.ToString());
                    }
                    else
                    {
                        // Recovery failed
                        MonitoringInfo.RecoveryAttempts++;
                        UE_LOG(LogServiceMonitor, Error, TEXT("Service recovery failed: %s. Attempt %d of %d"), 
                            *ServiceKey.ToString(), MonitoringInfo.RecoveryAttempts, MaxRecoveryAttempts);
                    }
                    
                    MonitoringInfo.LastRecoveryTime = CurrentTime;
                    MonitoringInfo.bRecoveryInProgress = false;
                }
                else if (MonitoringInfo.RecoveryAttempts >= MaxRecoveryAttempts)
                {
                    // Too many recovery attempts
                    UE_LOG(LogServiceMonitor, Error, TEXT("Service recovery abandoned after %d attempts: %s"), 
                        MaxRecoveryAttempts, *ServiceKey.ToString());
                }
            }
        }
    }
}

bool FServiceMonitor::RegisterServiceForMonitoring(const UClass* InInterfaceType, float InImportance, int32 InZoneID, int32 InRegionID)
{
    if (!InInterfaceType)
    {
        UE_LOG(LogServiceMonitor, Error, TEXT("Cannot register null service type for monitoring"));
        return false;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    if (!bIsInitialized)
    {
        UE_LOG(LogServiceMonitor, Error, TEXT("ServiceMonitor not initialized, cannot register service"));
        return false;
    }
    
    // Clamp importance to valid range
    float Importance = FMath::Clamp(InImportance, 0.0f, 1.0f);
    
    // Create service key
    FServiceKey ServiceKey(InInterfaceType, InZoneID, InRegionID);
    
    // Check if already registered
    if (MonitoredServices.Contains(ServiceKey))
    {
        UE_LOG(LogServiceMonitor, Warning, TEXT("Service already registered for monitoring: %s"), 
            *ServiceKey.ToString());
        return true;
    }
    
    // Register for monitoring
    FMonitoringInfo& MonitoringInfo = MonitoredServices.Add(ServiceKey, FMonitoringInfo(Importance));
    
    UE_LOG(LogServiceMonitor, Log, TEXT("Registered service for monitoring: %s with importance %.2f"), 
        *ServiceKey.ToString(), Importance);
    
    return true;
}

bool FServiceMonitor::GetServiceHealthMetrics(const UClass* InInterfaceType, FServiceHealthMetrics& OutMetrics, int32 InZoneID, int32 InRegionID) const
{
    if (!InInterfaceType)
    {
        UE_LOG(LogServiceMonitor, Error, TEXT("Cannot get health metrics for null service type"));
        return false;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    if (!bIsInitialized)
    {
        UE_LOG(LogServiceMonitor, Error, TEXT("ServiceMonitor not initialized, cannot get health metrics"));
        return false;
    }
    
    // Create service key
    FServiceKey ServiceKey(InInterfaceType, InZoneID, InRegionID);
    
    // Find monitoring info
    const FMonitoringInfo* MonitoringInfo = MonitoredServices.Find(ServiceKey);
    if (!MonitoringInfo)
    {
        UE_LOG(LogServiceMonitor, Warning, TEXT("Service not registered for monitoring: %s"), 
            *ServiceKey.ToString());
        return false;
    }
    
    // Return metrics
    OutMetrics = MonitoringInfo->Metrics;
    return true;
}

bool FServiceMonitor::RecoverService(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID)
{
    if (!InInterfaceType)
    {
        UE_LOG(LogServiceMonitor, Error, TEXT("Cannot recover null service type"));
        return false;
    }
    
    // Create service key for logging
    FServiceKey ServiceKey(InInterfaceType, InZoneID, InRegionID);
    
    // Here we would implement the actual recovery logic
    // This would typically involve:
    // 1. Checking if the service is registered with the service locator
    // 2. If not, attempting to re-register it
    // 3. If it is, possibly unregistering and re-registering it
    // 4. Resetting its state if possible
    
    // For now, let's just check if the service exists in the service locator
    IDependencyServiceLocator& ServiceLocator = IDependencyServiceLocator::Get();
    
    if (!ServiceLocator.IsInitialized())
    {
        UE_LOG(LogServiceMonitor, Error, TEXT("Service Locator not initialized, cannot recover service: %s"), 
            *ServiceKey.ToString());
        return false;
    }
    
    // Check if service exists
    if (!ServiceLocator.HasService(InInterfaceType, InZoneID, InRegionID))
    {
        UE_LOG(LogServiceMonitor, Error, TEXT("Service not found in locator, cannot recover: %s"), 
            *ServiceKey.ToString());
        return false;
    }
    
    // Try to get the service
    void* Service = ServiceLocator.ResolveService(InInterfaceType, InZoneID, InRegionID);
    if (!Service)
    {
        UE_LOG(LogServiceMonitor, Error, TEXT("Failed to resolve service for recovery: %s"), 
            *ServiceKey.ToString());
        return false;
    }
    
    // For now, assume the service has been recovered
    // In a real implementation, additional recovery steps would be performed here
    
    UE_LOG(LogServiceMonitor, Log, TEXT("Service recovered: %s"), *ServiceKey.ToString());
    return true;
}

void FServiceMonitor::ReportServiceOperation(const UClass* InInterfaceType, bool bSuccess, float ResponseTimeMs, int32 InZoneID, int32 InRegionID)
{
    if (!InInterfaceType)
    {
        return;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    if (!bIsInitialized)
    {
        return;
    }
    
    // Create service key
    FServiceKey ServiceKey(InInterfaceType, InZoneID, InRegionID);
    
    // Find monitoring info
    FMonitoringInfo* MonitoringInfo = MonitoredServices.Find(ServiceKey);
    if (!MonitoringInfo)
    {
        UE_LOG(LogServiceMonitor, Verbose, TEXT("Service not registered for monitoring, auto-registering: %s"), 
            *ServiceKey.ToString());
            
        // Auto-register with default importance
        MonitoringInfo = &MonitoredServices.Add(ServiceKey, FMonitoringInfo(0.5f));
    }
    
    // Update metrics
    if (bSuccess)
    {
        MonitoringInfo->Metrics.SuccessfulOperations++;
    }
    else
    {
        MonitoringInfo->Metrics.FailedOperations++;
    }
    
    // Add response time
    AddResponseTime(*MonitoringInfo, ResponseTimeMs);
    
    // Reset time since last check
    MonitoringInfo->TimeSinceLastCheck = 0.0f;
    MonitoringInfo->Metrics.TimeSinceLastCheck = 0.0f;
    
    // Update health status
    MonitoringInfo->UpdateHealthStatus();
}

TArray<TPair<const UClass*, FServiceHealthMetrics>> FServiceMonitor::GetProblematicServices() const
{
    TArray<TPair<const UClass*, FServiceHealthMetrics>> Result;
    
    FScopeLock Lock(&CriticalSection);
    
    if (!bIsInitialized)
    {
        return Result;
    }
    
    // Find services with Failed or Critical status
    for (const auto& ServicePair : MonitoredServices)
    {
        const FServiceKey& ServiceKey = ServicePair.Key;
        const FMonitoringInfo& MonitoringInfo = ServicePair.Value;
        
        if (MonitoringInfo.Metrics.Status == EServiceHealthStatus::Failed ||
            MonitoringInfo.Metrics.Status == EServiceHealthStatus::Critical)
        {
            Result.Add(TPair<const UClass*, FServiceHealthMetrics>(ServiceKey.InterfaceType, MonitoringInfo.Metrics));
        }
    }
    
    return Result;
}

FServiceMonitor& FServiceMonitor::Get()
{
    if (!Instance)
    {
        Instance = new FServiceMonitor();
        Instance->Initialize();
    }
    
    return *Instance;
}

void FServiceMonitor::AddResponseTime(FMonitoringInfo& Info, float ResponseTimeMs)
{
    // Add to response time list
    Info.ResponseTimes.Add(ResponseTimeMs);
    
    // Limit the number of entries to keep the list from growing too large
    const int32 MaxEntries = 100;
    if (Info.ResponseTimes.Num() > MaxEntries)
    {
        Info.ResponseTimes.RemoveAt(0);
    }
    
    // Calculate average
    float TotalTime = 0.0f;
    for (float Time : Info.ResponseTimes)
    {
        TotalTime += Time;
    }
    Info.Metrics.AverageResponseTimeMs = Info.ResponseTimes.Num() > 0 ? TotalTime / Info.ResponseTimes.Num() : 0.0f;
    
    // Update peak
    Info.Metrics.PeakResponseTimeMs = FMath::Max(Info.Metrics.PeakResponseTimeMs, ResponseTimeMs);
}

bool FServiceMonitor::NeedsRecovery(const FMonitoringInfo& Info) const
{
    // Check if service is in a failed state
    if (Info.Metrics.Status == EServiceHealthStatus::Failed)
    {
        return true;
    }
    
    // Check if service is in a critical state and has high importance
    if (Info.Metrics.Status == EServiceHealthStatus::Critical && Info.Importance >= 0.7f)
    {
        return true;
    }
    
    // Check for consecutive failures
    if (Info.Metrics.FailedOperations >= 5 && Info.Metrics.SuccessfulOperations == 0)
    {
        return true;
    }
    
    return false;
}

void FServiceMonitor::ResetOperationCounters(FMonitoringInfo& Info)
{
    Info.Metrics.SuccessfulOperations = 0;
    Info.Metrics.FailedOperations = 0;
    Info.OperationsInWindow = 0;
    Info.ResponseTimes.Empty();
    Info.Metrics.AverageResponseTimeMs = 0.0f;
    Info.Metrics.PeakResponseTimeMs = 0.0f;
}