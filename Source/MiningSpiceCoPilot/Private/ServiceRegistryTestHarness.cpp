// Copyright Epic Games, Inc. All Rights Reserved.

#include "ServiceRegistryTestHarness.h"
#include "../6_ServiceRegistryandDependency/Public/ServiceManager.h"
#include "../6_ServiceRegistryandDependency/Public/DependencyResolver.h"
#include "Logging/LogMacros.h"
#include "Misc/ScopedSlowTask.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"

// Forward declarations of service classes to avoid include conflicts
class FServiceLocator;
class FCoreServiceLocator;

// Log category for test harness
DEFINE_LOG_CATEGORY_STATIC(LogServiceTest, Log, All);

// Main test harness implementation
bool FServiceRegistryTestHarness::RunTests()
{
    UE_LOG(LogServiceTest, Display, TEXT("=========================================="));
    UE_LOG(LogServiceTest, Display, TEXT("Starting Service Registry Test Harness"));
    UE_LOG(LogServiceTest, Display, TEXT("=========================================="));

    // Progress UI feedback
    FScopedSlowTask Progress(100.0f, NSLOCTEXT("ServiceTest", "RunningTests", "Running Service Registry Tests..."));
    Progress.MakeDialog();

    bool bSuccess = true;
    
    // Step 1: Initialize the Service Registry system
    Progress.EnterProgressFrame(20.0f, NSLOCTEXT("ServiceTest", "InitializingServiceSystem", "Initializing Service Registry System..."));
    
    // Initialize service manager
    FServiceManager& ServiceManager = FServiceManager::Get();
    if (!ServiceManager.Initialize())
    {
        UE_LOG(LogServiceTest, Error, TEXT("Failed to initialize ServiceManager"));
        return false;
    }
    UE_LOG(LogServiceTest, Display, TEXT("ServiceManager initialized successfully"));
    
    // Step 2: Test the dependency resolver functionality
    Progress.EnterProgressFrame(25.0f, NSLOCTEXT("ServiceTest", "TestingDependencyResolver", "Testing Dependency Resolver..."));
    
    FDependencyResolver DependencyResolver;
    
    // Register some test nodes
    const uint32 MemoryManagerID = 1;
    const uint32 MaterialRegistryID = 2;
    const uint32 ZoneManagerID = 3;
    
    DependencyResolver.RegisterNode(MemoryManagerID, FName("MemoryPoolManager"));
    DependencyResolver.RegisterNode(MaterialRegistryID, FName("MaterialRegistry"));
    DependencyResolver.RegisterNode(ZoneManagerID, FName("ZoneManager"));
    
    // Define dependencies
    DependencyResolver.RegisterDependency(MaterialRegistryID, MemoryManagerID, FDependencyResolver::EDependencyType::Required);
    DependencyResolver.RegisterDependency(ZoneManagerID, MemoryManagerID, FDependencyResolver::EDependencyType::Required);
    
    // Define a conditional dependency
    DependencyResolver.RegisterConditionalDependency(
        ZoneManagerID, 
        MaterialRegistryID, 
        []() { return true; }, // Always active for this test
        FDependencyResolver::EDependencyType::Optional);
    
    // Define a hardware dependent dependency
    DependencyResolver.RegisterHardwareDependency(
        ZoneManagerID,
        MaterialRegistryID,
        static_cast<uint32>(FDependencyResolver::EHardwareCapability::GPU),
        FDependencyResolver::EDependencyType::Optional);
    
    // Determine initialization order
    TArray<uint32> InitOrder;
    TArray<FString> Errors;
    FDependencyResolver::EResolutionStatus Status = DependencyResolver.DetermineInitializationOrder(InitOrder, Errors);
    
    if (Status != FDependencyResolver::EResolutionStatus::Success)
    {
        UE_LOG(LogServiceTest, Error, TEXT("Failed to resolve dependencies: %s"), Errors.Num() > 0 ? *Errors[0] : TEXT("Unknown error"));
        bSuccess = false;
    }
    else
    {
        UE_LOG(LogServiceTest, Display, TEXT("Dependencies resolved successfully"));
        UE_LOG(LogServiceTest, Display, TEXT("Initialization order:"));
        for (int32 i = 0; i < InitOrder.Num(); ++i)
        {
            FName ServiceName;
            switch (InitOrder[i])
            {
                case MemoryManagerID: ServiceName = TEXT("MemoryPoolManager"); break;
                case MaterialRegistryID: ServiceName = TEXT("MaterialRegistry"); break;
                case ZoneManagerID: ServiceName = TEXT("ZoneManager"); break;
                default: ServiceName = TEXT("Unknown"); break;
            }
            UE_LOG(LogServiceTest, Display, TEXT("  %d. Service: %s"), i + 1, *ServiceName.ToString());
        }
    }
    
    // Step 3: Test cycle detection in the dependency resolver
    Progress.EnterProgressFrame(25.0f, NSLOCTEXT("ServiceTest", "TestingCycleDetection", "Testing Cycle Detection..."));
    
    // Create a new resolver for cycle testing
    FDependencyResolver CycleResolver;
    
    // Register test nodes
    const uint32 ServiceA = 1;
    const uint32 ServiceB = 2;
    const uint32 ServiceC = 3;
    
    CycleResolver.RegisterNode(ServiceA, FName("ServiceA"));
    CycleResolver.RegisterNode(ServiceB, FName("ServiceB"));
    CycleResolver.RegisterNode(ServiceC, FName("ServiceC"));
    
    // Create a dependency cycle
    CycleResolver.RegisterDependency(ServiceA, ServiceB);
    CycleResolver.RegisterDependency(ServiceB, ServiceC);
    CycleResolver.RegisterDependency(ServiceC, ServiceA);
    
    // Detect cycles
    TArray<FDependencyResolver::FCycleInfo> DetectedCycles;
    TArray<FString> CycleErrors;
    bool bNoCycles = CycleResolver.DetectCycles(DetectedCycles, CycleErrors);
    
    if (bNoCycles)
    {
        UE_LOG(LogServiceTest, Error, TEXT("Failed to detect cycles that should have been present"));
        bSuccess = false;
    }
    else
    {
        UE_LOG(LogServiceTest, Display, TEXT("Successfully detected %d cycles"), DetectedCycles.Num());
        for (int32 i = 0; i < DetectedCycles.Num(); ++i)
        {
            UE_LOG(LogServiceTest, Display, TEXT("  Cycle %d: %s"), i + 1, *DetectedCycles[i].Description);
        }
    }
    
    // Step 4: Test service configuration
    Progress.EnterProgressFrame(25.0f, NSLOCTEXT("ServiceTest", "TestingServiceConfiguration", "Testing Service Configuration..."));
    
    // Create and display service configurations
    FServiceConfiguration PooledConfig;
    PooledConfig.bEnablePooling = true;
    PooledConfig.MaxPoolSize = 10;
    PooledConfig.bCanRecover = true;
    PooledConfig.bSaveStateForRecovery = true;
    
    FServiceConfiguration StandardConfig;
    StandardConfig.bEnablePooling = false;
    StandardConfig.bCanRecover = true;
    StandardConfig.bSaveStateForRecovery = false;
    
    // Display configuration parameters
    UE_LOG(LogServiceTest, Display, TEXT("Pooled Service Configuration:"));
    UE_LOG(LogServiceTest, Display, TEXT("  Pooling: %s"), PooledConfig.bEnablePooling ? TEXT("Enabled") : TEXT("Disabled"));
    UE_LOG(LogServiceTest, Display, TEXT("  Max Pool Size: %d"), PooledConfig.MaxPoolSize);
    UE_LOG(LogServiceTest, Display, TEXT("  Recovery: %s"), PooledConfig.bCanRecover ? TEXT("Enabled") : TEXT("Disabled"));
    UE_LOG(LogServiceTest, Display, TEXT("  State Preservation: %s"), PooledConfig.bSaveStateForRecovery ? TEXT("Enabled") : TEXT("Disabled"));
    
    UE_LOG(LogServiceTest, Display, TEXT("Standard Service Configuration:"));
    UE_LOG(LogServiceTest, Display, TEXT("  Pooling: %s"), StandardConfig.bEnablePooling ? TEXT("Enabled") : TEXT("Disabled"));
    UE_LOG(LogServiceTest, Display, TEXT("  Recovery: %s"), StandardConfig.bCanRecover ? TEXT("Enabled") : TEXT("Disabled"));
    UE_LOG(LogServiceTest, Display, TEXT("  State Preservation: %s"), StandardConfig.bSaveStateForRecovery ? TEXT("Enabled") : TEXT("Disabled"));
    
    // Step 5: Test service metrics collection
    Progress.EnterProgressFrame(25.0f, NSLOCTEXT("ServiceTest", "TestingMetricsCollection", "Testing Service Metrics Collection..."));
    
    // Create and populate a service metrics object
    FServiceMetrics TestMetrics;
    TestMetrics.SuccessfulOperations.Set(1000);
    TestMetrics.FailedOperations.Set(50);
    TestMetrics.TotalOperationTimeMs.Set(25000);
    TestMetrics.MaxOperationTimeMs.Set(500);
    TestMetrics.MemoryUsageBytes.Set(1024 * 1024 * 10); // 10 MB
    TestMetrics.ActiveInstances.Set(5);
    TestMetrics.LastHealthCheckTime = FPlatformTime::Seconds();
    TestMetrics.LastFailureTime = FPlatformTime::Seconds() - 3600; // 1 hour ago
    TestMetrics.LastRecoveryTime = FPlatformTime::Seconds() - 1800; // 30 minutes ago
    
    // Display metrics
    UE_LOG(LogServiceTest, Display, TEXT("Service Metrics Example:"));
    UE_LOG(LogServiceTest, Display, TEXT("  Successful Operations: %lld"), TestMetrics.SuccessfulOperations.GetValue());
    UE_LOG(LogServiceTest, Display, TEXT("  Failed Operations: %lld"), TestMetrics.FailedOperations.GetValue());
    UE_LOG(LogServiceTest, Display, TEXT("  Total Operation Time: %lld ms"), TestMetrics.TotalOperationTimeMs.GetValue());
    UE_LOG(LogServiceTest, Display, TEXT("  Max Operation Time: %lld ms"), TestMetrics.MaxOperationTimeMs.GetValue());
    UE_LOG(LogServiceTest, Display, TEXT("  Memory Usage: %lld bytes"), TestMetrics.MemoryUsageBytes.GetValue());
    UE_LOG(LogServiceTest, Display, TEXT("  Active Instances: %d"), TestMetrics.ActiveInstances.GetValue());
    UE_LOG(LogServiceTest, Display, TEXT("  Success Rate: %.2f%%"), 
        100.0f * TestMetrics.SuccessfulOperations.GetValue() / 
        (TestMetrics.SuccessfulOperations.GetValue() + TestMetrics.FailedOperations.GetValue()));
    
    // Record some synthetic operations using the service manager
    // (These won't affect real services since we're not registering/using real services)
    ServiceManager.RecordServiceOperation(FName("TestService1"), true, 15.0f, 1024);
    ServiceManager.RecordServiceOperation(FName("TestService1"), true, 20.0f, 2048);
    ServiceManager.RecordServiceOperation(FName("TestService1"), false, 50.0f, 4096);
    
    ServiceManager.RecordServiceOperation(FName("TestService2"), true, 5.0f, 512);
    ServiceManager.RecordServiceOperation(FName("TestService2"), true, 8.0f, 1024);
    
    UE_LOG(LogServiceTest, Display, TEXT("Recorded synthetic operations for metrics tracking"));
    
    // Cleanup
    ServiceManager.Shutdown();
    
    UE_LOG(LogServiceTest, Display, TEXT("=========================================="));
    UE_LOG(LogServiceTest, Display, TEXT("Service Registry Test Harness Complete"));
    UE_LOG(LogServiceTest, Display, TEXT("Result: %s"), bSuccess ? TEXT("SUCCESS") : TEXT("FAILURE"));
    UE_LOG(LogServiceTest, Display, TEXT("=========================================="));
    
    return bSuccess;
} 