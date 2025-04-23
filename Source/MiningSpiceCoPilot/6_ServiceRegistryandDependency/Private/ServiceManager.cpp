// Copyright Epic Games, Inc. All Rights Reserved.

#include "ServiceManager.h"
#include "ServiceLocator.h"
#include "Misc/ScopeLock.h"
#include "Misc/CoreDelegates.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/ISaveableService.h"
#include "Interfaces/IMemoryAwareService.h"
#include "../../1_CoreRegistry/Public/Interfaces/IService.h"
#include "../../2_MemoryManagement/Public/Interfaces/IMemoryManager.h"

// Initialize static members
FServiceManager* FServiceManager::ManagerInstance = nullptr;
FCriticalSection FServiceManager::SingletonLock;

FServiceManager::FServiceManager()
    : DependencyResolver(nullptr)
    , ServiceLocator(nullptr)
    , MemoryManager(nullptr)
    , bIsInitialized(false)
{
    // ServicePoolItemCounts will be initialized empty
}

FServiceManager::~FServiceManager()
{
    if (IsInitialized())
    {
        Shutdown();
    }
}

bool FServiceManager::Initialize()
{
    // Check if already initialized
    if (bIsInitialized)
    {
        return true;
    }
    
    // Acquire dependencies
    ServiceLocator = &IServiceLocator::Get();
    MemoryManager = &IMemoryManager::Get();
    
    // Create dependency resolver
    DependencyResolver = MakeShared<FDependencyResolver>();
    
    // Initialize successful
    bIsInitialized = true;
    
    UE_LOG(LogTemp, Log, TEXT("ServiceManager initialized"));
    return true;
}

void FServiceManager::Shutdown()
{
    if (!bIsInitialized)
    {
        return;
    }
    
    // Determine service shutdown order based on dependencies
    TArray<FName> ShutdownOrder;
    TArray<FString> Errors;
    
    if (DetermineServiceOrder(ShutdownOrder, Errors))
    {
        // Shutdown services in reverse dependency order
        Algo::Reverse(ShutdownOrder);
        
        StopServicesInOrder(ShutdownOrder);
    }
    else
    {
        // Log errors
        for (const FString& Error : Errors)
        {
            UE_LOG(LogTemp, Error, TEXT("ServiceManager shutdown error: %s"), *Error);
        }
        
        // Attempt to shutdown all services in arbitrary order as fallback
        FScopeLock Lock(&InstancesLock);
        
        for (auto& Pair : ServiceInstances)
        {
            FServiceInstance& Instance = Pair.Value;
            
            if (Instance.State != EServiceState::Uninitialized && Instance.State != EServiceState::Destroyed)
            {
                StopService(Instance.InterfaceType, Instance.ZoneID, Instance.RegionID);
            }
        }
    }
    
    // Clean up service pools
    CleanupServicePools();
    
    // Clear collections
    {
        FScopeLock Lock(&InstancesLock);
        ServiceInstances.Empty();
        ServiceConfigurations.Empty();
    }
    
    // Clear dependency resolver
    DependencyResolver.Reset();
    
    // Set initialized flag to false
    bIsInitialized = false;
    
    UE_LOG(LogTemp, Log, TEXT("ServiceManager shutdown completed"));
}

bool FServiceManager::IsInitialized() const
{
    return bIsInitialized;
}

FServiceManager& FServiceManager::Get()
{
    // Thread-safe singleton initialization
    if (!ManagerInstance)
    {
        FScopeLock Lock(&SingletonLock);
        
        if (!ManagerInstance)
        {
            ManagerInstance = new FServiceManager();
            ManagerInstance->Initialize();
        }
    }
    
    check(ManagerInstance);
    return *ManagerInstance;
}

bool FServiceManager::RegisterService(void* InService, const UClass* InInterfaceType, 
    const FServiceConfiguration& InConfiguration, int32 InZoneID, int32 InRegionID)
{
    if (!bIsInitialized || !InService || !InInterfaceType)
    {
        return false;
    }
    
    // Create the service key
    FName ServiceKey = CreateServiceKey(InInterfaceType, InZoneID, InRegionID);
    
    // Check if service already exists
    {
        FScopeLock Lock(&InstancesLock);
        
        if (ServiceInstances.Contains(ServiceKey))
        {
            UE_LOG(LogTemp, Warning, TEXT("Service '%s' already registered"), *ServiceKey.ToString());
            return false;
        }
        
        // Create new service instance
        FServiceInstance Instance;
        Instance.InterfaceType = InInterfaceType;
        Instance.ServicePtr = InService;
        Instance.State = EServiceState::Uninitialized;
        Instance.ZoneID = InZoneID;
        Instance.RegionID = InRegionID;
        Instance.bIsPooled = false;
        Instance.CreationTime = FPlatformTime::Seconds();
        Instance.LastAccessTime = Instance.CreationTime;
        
        // Add to instances map
        ServiceInstances.Add(ServiceKey, Instance);
        
        // Add configuration
        ServiceConfigurations.Add(ServiceKey, InConfiguration);
    }
    
    // Register service with the ServiceLocator
    if (ServiceLocator)
    {
        if (!ServiceLocator->RegisterService(InService, InInterfaceType, InZoneID, InRegionID))
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to register service '%s' with ServiceLocator"), *ServiceKey.ToString());
            
            // Remove from our maps
            FScopeLock Lock(&InstancesLock);
            ServiceInstances.Remove(ServiceKey);
            ServiceConfigurations.Remove(ServiceKey);
            
            return false;
        }
    }
    
    // Extract dependencies if needed
    if (DependencyResolver)
    {
        // Register node in dependency resolver
        uint32 NodeId = DependencyResolver->GetNodeCount() + 1;
        DependencyResolver->RegisterNode(NodeId, ServiceKey);
        
        // Get service dependencies from ServiceLocator
        if (ServiceLocator)
        {
            TArray<UClass*> Dependencies = ServiceLocator->GetServiceDependencies(InInterfaceType);
            
            // Register dependencies
            for (UClass* Dependency : Dependencies)
            {
                FName DependencyKey = CreateServiceKey(Dependency, InZoneID, InRegionID);
                
                // Find dependency node
                const FDependencyResolver::FDependencyNode* DependencyNode = nullptr;
                for (const FDependencyResolver::FDependencyNode& Node : DependencyResolver->GetAllNodes())
                {
                    if (Node.Name == DependencyKey)
                    {
                        DependencyNode = &Node;
                        break;
                    }
                }
                
                if (DependencyNode)
                {
                    DependencyResolver->RegisterDependency(NodeId, DependencyNode->Id);
                }
            }
        }
    }
    
    UE_LOG(LogTemp, Verbose, TEXT("Registered service '%s'"), *ServiceKey.ToString());
    return true;
}

bool FServiceManager::UnregisterService(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID)
{
    if (!bIsInitialized || !InInterfaceType)
    {
        return false;
    }
    
    // Create the service key
    FName ServiceKey = CreateServiceKey(InInterfaceType, InZoneID, InRegionID);
    
    // Check if service exists
    FServiceInstance* Instance = nullptr;
    {
        FScopeLock Lock(&InstancesLock);
        Instance = ServiceInstances.Find(ServiceKey);
        
        if (!Instance)
        {
            UE_LOG(LogTemp, Warning, TEXT("Service '%s' not found for unregistration"), *ServiceKey.ToString());
            return false;
        }
    }
    
    // Stop the service if needed
    if (Instance->State != EServiceState::Uninitialized && Instance->State != EServiceState::Destroyed)
    {
        if (!StopService(InInterfaceType, InZoneID, InRegionID))
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to stop service '%s' during unregistration"), *ServiceKey.ToString());
            return false;
        }
    }
    
    // Unregister service from the ServiceLocator
    if (ServiceLocator)
    {
        if (!ServiceLocator->UnregisterService(InInterfaceType, InZoneID, InRegionID))
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to unregister service '%s' from ServiceLocator"), *ServiceKey.ToString());
            return false;
        }
    }
    
    // Remove from our maps
    {
        FScopeLock Lock(&InstancesLock);
        ServiceInstances.Remove(ServiceKey);
        ServiceConfigurations.Remove(ServiceKey);
    }
    
    UE_LOG(LogTemp, Verbose, TEXT("Unregistered service '%s'"), *ServiceKey.ToString());
    return true;
}

bool FServiceManager::StartService(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID)
{
    if (!bIsInitialized || !InInterfaceType)
    {
        return false;
    }
    
    // Create the service key
    FName ServiceKey = CreateServiceKey(InInterfaceType, InZoneID, InRegionID);
    
    // Get the service instance
    FServiceInstance* Instance = GetServiceInstanceByKey(ServiceKey);
    if (!Instance)
    {
        UE_LOG(LogTemp, Error, TEXT("Service '%s' not found for starting"), *ServiceKey.ToString());
        return false;
    }
    
    // Check if service is already active
    if (Instance->State == EServiceState::Active)
    {
        return true;
    }
    
    // Check if service is initializing
    if (Instance->State == EServiceState::Initializing)
    {
        UE_LOG(LogTemp, Warning, TEXT("Service '%s' is already initializing"), *ServiceKey.ToString());
        return false;
    }
    
    // Set service state to initializing
    Instance->State = EServiceState::Initializing;
    
    // Start dependencies first
    TArray<UClass*> Dependencies;
    if (ServiceLocator)
    {
        Dependencies = ServiceLocator->GetServiceDependencies(InInterfaceType);
    }
    
    bool bAllDependenciesStarted = true;
    for (UClass* Dependency : Dependencies)
    {
        // Start dependency if not already started
        if (!StartService(Dependency, InZoneID, InRegionID))
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to start dependency '%s' for service '%s'"), 
                *Dependency->GetName(), *ServiceKey.ToString());
            bAllDependenciesStarted = false;
            break;
        }
    }
    
    if (!bAllDependenciesStarted)
    {
        Instance->State = EServiceState::Uninitialized;
        return false;
    }
    
    // Initialize the service
    bool bInitialized = false;
    if (IService* Service = static_cast<IService*>(Instance->ServicePtr))
    {
        bInitialized = Service->Initialize();
    }
    else
    {
        // For non-IService objects, consider them initialized automatically
        bInitialized = true;
    }
    
    if (bInitialized)
    {
        // Set service state to active
        Instance->State = EServiceState::Active;
        Instance->LastAccessTime = FPlatformTime::Seconds();
        
        // Increment active instances counter
        Instance->Metrics.ActiveInstances.Increment();
        
        UE_LOG(LogTemp, Verbose, TEXT("Started service '%s'"), *ServiceKey.ToString());
        return true;
    }
    else
    {
        // Set service state back to uninitialized
        Instance->State = EServiceState::Uninitialized;
        
        UE_LOG(LogTemp, Error, TEXT("Failed to initialize service '%s'"), *ServiceKey.ToString());
        return false;
    }
}

bool FServiceManager::StopService(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID)
{
    if (!bIsInitialized || !InInterfaceType)
    {
        return false;
    }
    
    // Create the service key
    FName ServiceKey = CreateServiceKey(InInterfaceType, InZoneID, InRegionID);
    
    // Get the service instance
    FServiceInstance* Instance = GetServiceInstanceByKey(ServiceKey);
    if (!Instance)
    {
        UE_LOG(LogTemp, Error, TEXT("Service '%s' not found for stopping"), *ServiceKey.ToString());
        return false;
    }
    
    // Check if service is already inactive
    if (Instance->State == EServiceState::Uninitialized || Instance->State == EServiceState::Destroyed)
    {
        return true;
    }
    
    // Check if service is shutting down
    if (Instance->State == EServiceState::ShuttingDown)
    {
        UE_LOG(LogTemp, Warning, TEXT("Service '%s' is already shutting down"), *ServiceKey.ToString());
        return false;
    }
    
    // Check for dependent services
    TArray<UClass*> Dependents;
    if (ServiceLocator)
    {
        Dependents = ServiceLocator->GetDependentServices(InInterfaceType);
    }
    
    // Stop all dependent services first
    for (UClass* Dependent : Dependents)
    {
        if (!StopService(Dependent, InZoneID, InRegionID))
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to stop dependent service '%s' of '%s'"), 
                *Dependent->GetName(), *ServiceKey.ToString());
            return false;
        }
    }
    
    // Set service state to shutting down
    Instance->State = EServiceState::ShuttingDown;
    
    // Save service state if configured to do so
    FServiceConfiguration Config = GetServiceConfiguration(InInterfaceType, InZoneID, InRegionID);
    if (Config.bSaveStateForRecovery)
    {
        SaveServiceState(*Instance);
    }
    
    // Shutdown the service
    bool bShutdown = false;
    if (IService* Service = static_cast<IService*>(Instance->ServicePtr))
    {
        Service->Shutdown();
        // Assume shutdown was successful since the method doesn't return a value
        bShutdown = true;
    }
    else
    {
        // For non-IService objects, consider them shutdown automatically
        bShutdown = true;
    }
    
    if (bShutdown)
    {
        // Set service state to uninitialized
        Instance->State = EServiceState::Uninitialized;
        
        // Decrement active instances counter
        Instance->Metrics.ActiveInstances.Decrement();
        
        UE_LOG(LogTemp, Verbose, TEXT("Stopped service '%s'"), *ServiceKey.ToString());
        return true;
    }
    else
    {
        // Set service state to failing
        Instance->State = EServiceState::Failing;
        
        UE_LOG(LogTemp, Error, TEXT("Failed to shutdown service '%s'"), *ServiceKey.ToString());
        return false;
    }
}

bool FServiceManager::RestartService(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID, bool bPreserveState)
{
    if (!bIsInitialized || !InInterfaceType)
    {
        return false;
    }
    
    // Create the service key
    FName ServiceKey = CreateServiceKey(InInterfaceType, InZoneID, InRegionID);
    
    // Get the service instance
    FServiceInstance* Instance = GetServiceInstanceByKey(ServiceKey);
    if (!Instance)
    {
        UE_LOG(LogTemp, Error, TEXT("Service '%s' not found for restarting"), *ServiceKey.ToString());
        return false;
    }
    
    // Save state if needed and configured to do so
    if (bPreserveState)
    {
        FServiceConfiguration Config = GetServiceConfiguration(InInterfaceType, InZoneID, InRegionID);
        if (Config.bSaveStateForRecovery)
        {
            SaveServiceState(*Instance);
        }
    }
    
    // Stop the service
    if (!StopService(InInterfaceType, InZoneID, InRegionID))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to stop service '%s' during restart"), *ServiceKey.ToString());
        return false;
    }
    
    // Start the service
    if (!StartService(InInterfaceType, InZoneID, InRegionID))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to start service '%s' during restart"), *ServiceKey.ToString());
        return false;
    }
    
    // Restore state if needed and state was saved
    if (bPreserveState && Instance->SavedState.Num() > 0)
    {
        if (!RestoreServiceState(*Instance))
        {
            UE_LOG(LogTemp, Warning, TEXT("Failed to restore state for service '%s' during restart"), 
                *ServiceKey.ToString());
            // Continue even if state restoration failed
        }
    }
    
    UE_LOG(LogTemp, Verbose, TEXT("Restarted service '%s'"), *ServiceKey.ToString());
    return true;
}

EServiceState FServiceManager::GetServiceState(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID) const
{
    if (!bIsInitialized || !InInterfaceType)
    {
        return EServiceState::Uninitialized;
    }
    
    // Create the service key
    FName ServiceKey = CreateServiceKey(InInterfaceType, InZoneID, InRegionID);
    
    // Get the service instance
    const FServiceInstance* Instance = GetServiceInstanceByKey(ServiceKey);
    if (!Instance)
    {
        return EServiceState::Uninitialized;
    }
    
    return Instance->State;
}

const FServiceMetrics* FServiceManager::GetServiceMetrics(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID) const
{
    if (!bIsInitialized || !InInterfaceType)
    {
        return nullptr;
    }
    
    // Create the service key
    FName ServiceKey = CreateServiceKey(InInterfaceType, InZoneID, InRegionID);
    
    // Get the service instance
    const FServiceInstance* Instance = GetServiceInstanceByKey(ServiceKey);
    if (!Instance)
    {
        return nullptr;
    }
    
    return &Instance->Metrics;
}

void FServiceManager::RecordServiceMetrics(const UClass* InInterfaceType, bool bSuccess, float DurationMs, 
    uint64 MemoryUsed, int32 InZoneID, int32 InRegionID)
{
    if (!bIsInitialized || !InInterfaceType)
    {
        return;
    }
    
    // Create the service key
    FName ServiceKey = CreateServiceKey(InInterfaceType, InZoneID, InRegionID);
    
    // Get the service instance
    FServiceInstance* Instance = GetServiceInstanceByKey(ServiceKey);
    if (!Instance)
    {
        return;
    }
    
    // Update metrics
    if (bSuccess)
    {
        Instance->Metrics.SuccessfulOperations.Increment();
    }
    else
    {
        Instance->Metrics.FailedOperations.Increment();
        Instance->Metrics.LastFailureTime = FPlatformTime::Seconds();
    }
    
    Instance->Metrics.TotalOperationTimeMs.Add(DurationMs);
    
    // Update max operation time if needed
    uint64 CurrentMaxTime = Instance->Metrics.MaxOperationTimeMs.GetValue();
    if (DurationMs > CurrentMaxTime)
    {
        Instance->Metrics.MaxOperationTimeMs.Set(DurationMs);
    }
    
    // Update memory usage if provided
    if (MemoryUsed > 0)
    {
        Instance->Metrics.MemoryUsageBytes.Set(MemoryUsed);
    }
    
    // Update last access time
    Instance->LastAccessTime = FPlatformTime::Seconds();
}

void* FServiceManager::AcquirePooledService(const UClass* InInterfaceType, int32 InZoneID)
{
    if (!bIsInitialized || !InInterfaceType || InZoneID == INDEX_NONE)
    {
        return nullptr;
    }
    
    // Create pool key
    FName PoolKey = FName(*FString::Printf(TEXT("%s_Zone%d_Pool"), *InInterfaceType->GetName(), InZoneID));
    
    // Try to get a service from the pool
    void* ServicePtr = nullptr;
    
    {
        FScopeLock Lock(&PoolsLock);
        
        TSharedPtr<TQueue<void*>>* PoolPtr = ServicePools.Find(PoolKey);
        if (PoolPtr && *PoolPtr && !(*PoolPtr)->IsEmpty())
        {
            (*PoolPtr)->Dequeue(ServicePtr);
            
            // Decrement pool item count
            int32* ItemCount = ServicePoolItemCounts.Find(PoolKey);
            if (ItemCount && *ItemCount > 0)
            {
                (*ItemCount)--;
            }
        }
    }
    
    if (ServicePtr)
    {
        // Create service key
        FName ServiceKey = CreateServiceKey(InInterfaceType, InZoneID, INDEX_NONE);
        
        // Create new service instance
        FServiceInstance Instance;
        Instance.InterfaceType = InInterfaceType;
        Instance.ServicePtr = ServicePtr;
        Instance.State = EServiceState::Uninitialized;
        Instance.ZoneID = InZoneID;
        Instance.RegionID = INDEX_NONE;
        Instance.bIsPooled = true;
        Instance.CreationTime = FPlatformTime::Seconds();
        Instance.LastAccessTime = Instance.CreationTime;
        
        // Add to instances map
        FScopeLock Lock(&InstancesLock);
        ServiceInstances.Add(ServiceKey, Instance);
        
        // Start the service
        StartService(InInterfaceType, InZoneID, INDEX_NONE);
    }
    
    return ServicePtr;
}

bool FServiceManager::ReleasePooledService(void* InService, const UClass* InInterfaceType, int32 InZoneID)
{
    if (!bIsInitialized || !InService || !InInterfaceType || InZoneID == INDEX_NONE)
    {
        return false;
    }
    
    // Create service key
    FName ServiceKey = CreateServiceKey(InInterfaceType, InZoneID, INDEX_NONE);
    
    // Get the service instance
    FServiceInstance* Instance = GetServiceInstanceByKey(ServiceKey);
    if (!Instance || Instance->ServicePtr != InService || !Instance->bIsPooled)
    {
        return false;
    }
    
    // Stop the service
    StopService(InInterfaceType, InZoneID, INDEX_NONE);
    
    // Create pool key
    FName PoolKey = FName(*FString::Printf(TEXT("%s_Zone%d_Pool"), *InInterfaceType->GetName(), InZoneID));
    
    // Return the service to the pool
    {
        FScopeLock PoolLock(&PoolsLock);
        
        TSharedPtr<TQueue<void*>>* PoolPtr = ServicePools.Find(PoolKey);
        if (!PoolPtr || !(*PoolPtr))
        {
            // Create the pool if it doesn't exist
            ServicePools.Add(PoolKey, MakeShared<TQueue<void*>>());
            PoolPtr = ServicePools.Find(PoolKey);
            ServicePoolItemCounts.Add(PoolKey, 0);
        }
        
        // Check pool size limit
        int32* MaxSize = ServicePoolSizes.Find(PoolKey);
        int32* CurrentSize = ServicePoolItemCounts.Find(PoolKey);
        
        if (MaxSize && CurrentSize)
        {
            // Check if pool is full
            if (*CurrentSize >= *MaxSize)
            {
                // Pool is full, don't return the service
                // Just release it to be garbage collected
                UE_LOG(LogTemp, Verbose, TEXT("Service pool '%s' is full, releasing service"), *PoolKey.ToString());
                
                // Remove from instances map
                FScopeLock InstanceLock(&InstancesLock);
                ServiceInstances.Remove(ServiceKey);
                
                return true;
            }
            
            // Increment the item count
            (*CurrentSize)++;
        }
        else if (!CurrentSize)
        {
            // Initialize the counter if it doesn't exist
            ServicePoolItemCounts.Add(PoolKey, 1);
        }
        else
        {
            // Increment the counter
            (*CurrentSize)++;
        }
        
        // Add service to the pool
        (*PoolPtr)->Enqueue(InService);
    }
    
    // Remove from instances map
    {
        FScopeLock Lock(&InstancesLock);
        ServiceInstances.Remove(ServiceKey);
    }
    
    return true;
}

bool FServiceManager::CreateServicePool(const UClass* InInterfaceType, int32 InZoneID, int32 InMaxPoolSize)
{
    if (!bIsInitialized || !InInterfaceType || InZoneID == INDEX_NONE || InMaxPoolSize <= 0)
    {
        return false;
    }
    
    // Create pool key
    FName PoolKey = FName(*FString::Printf(TEXT("%s_Zone%d_Pool"), *InInterfaceType->GetName(), InZoneID));
    
    // Create or update the pool
    {
        FScopeLock Lock(&PoolsLock);
        
        // Create pool if it doesn't exist
        if (!ServicePools.Contains(PoolKey))
        {
            ServicePools.Add(PoolKey, MakeShared<TQueue<void*>>());
            // Initialize the item counter to 0
            ServicePoolItemCounts.Add(PoolKey, 0);
        }
        
        // Set pool size
        ServicePoolSizes.Add(PoolKey, InMaxPoolSize);
    }
    
    return true;
}

FServiceConfiguration FServiceManager::GetServiceConfiguration(const UClass* InInterfaceType, 
    int32 InZoneID, int32 InRegionID) const
{
    if (!bIsInitialized || !InInterfaceType)
    {
        return FServiceConfiguration();
    }
    
    // Create the service key
    FName ServiceKey = CreateServiceKey(InInterfaceType, InZoneID, InRegionID);
    
    // Get the configuration
    FScopeLock Lock(&InstancesLock);
    const FServiceConfiguration* Config = ServiceConfigurations.Find(ServiceKey);
    
    if (Config)
    {
        return *Config;
    }
    
    // Return default configuration if not found
    return FServiceConfiguration();
}

bool FServiceManager::ReconfigureService(const UClass* InInterfaceType, const FServiceConfiguration& InConfiguration, 
    int32 InZoneID, int32 InRegionID)
{
    if (!bIsInitialized || !InInterfaceType)
    {
        return false;
    }
    
    // Create the service key
    FName ServiceKey = CreateServiceKey(InInterfaceType, InZoneID, InRegionID);
    
    // Check if service exists
    {
        FScopeLock Lock(&InstancesLock);
        
        if (!ServiceInstances.Contains(ServiceKey))
        {
            UE_LOG(LogTemp, Warning, TEXT("Service '%s' not found for reconfiguration"), *ServiceKey.ToString());
            return false;
        }
        
        // Update configuration
        ServiceConfigurations.Add(ServiceKey, InConfiguration);
    }
    
    // If service is poolable, update pool size
    if (InConfiguration.bEnablePooling && InZoneID != INDEX_NONE)
    {
        FName PoolKey = FName(*FString::Printf(TEXT("%s_Zone%d_Pool"), *InInterfaceType->GetName(), InZoneID));
        
        FScopeLock Lock(&PoolsLock);
        ServicePoolSizes.Add(PoolKey, InConfiguration.MaxPoolSize);
    }
    
    return true;
}

TArray<FServiceInstance> FServiceManager::GetAllServices() const
{
    TArray<FServiceInstance> Result;
    
    if (!bIsInitialized)
    {
        return Result;
    }
    
    FScopeLock Lock(&InstancesLock);
    
    for (const auto& Pair : ServiceInstances)
    {
        Result.Add(Pair.Value);
    }
    
    return Result;
}

TArray<FServiceInstance> FServiceManager::GetServicesByState(EServiceState InState) const
{
    TArray<FServiceInstance> Result;
    
    if (!bIsInitialized)
    {
        return Result;
    }
    
    FScopeLock Lock(&InstancesLock);
    
    for (const auto& Pair : ServiceInstances)
    {
        if (Pair.Value.State == InState)
        {
            Result.Add(Pair.Value);
        }
    }
    
    return Result;
}

FName FServiceManager::CreateServiceKey(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID) const
{
    check(InInterfaceType);
    
    FString KeyStr = InInterfaceType->GetName();
    
    if (InRegionID != INDEX_NONE)
    {
        KeyStr += FString::Printf(TEXT("_Region%d"), InRegionID);
    }
    
    if (InZoneID != INDEX_NONE)
    {
        KeyStr += FString::Printf(TEXT("_Zone%d"), InZoneID);
    }
    
    return FName(*KeyStr);
}

FServiceInstance* FServiceManager::GetServiceInstanceByKey(const FName& InKey)
{
    if (!bIsInitialized)
    {
        return nullptr;
    }
    
    FScopeLock Lock(&InstancesLock);
    return ServiceInstances.Find(InKey);
}

const FServiceInstance* FServiceManager::GetServiceInstanceByKey(const FName& InKey) const
{
    if (!bIsInitialized)
    {
        return nullptr;
    }
    
    FScopeLock Lock(&InstancesLock);
    return ServiceInstances.Find(InKey);
}

bool FServiceManager::DetermineServiceOrder(TArray<FName>& OutInitializationOrder, TArray<FString>& OutErrors)
{
    if (!bIsInitialized || !DependencyResolver)
    {
        OutErrors.Add(TEXT("ServiceManager not initialized or DependencyResolver is null"));
        return false;
    }
    
    // Build the dependency graph
    if (!DependencyResolver->BuildDependencyGraph(OutErrors))
    {
        OutErrors.Add(TEXT("Failed to build dependency graph"));
        return false;
    }
    
    // Determine initialization order
    TArray<uint32> NodeOrder;
    FDependencyResolver::EResolutionStatus Status = 
        DependencyResolver->DetermineInitializationOrder(NodeOrder, OutErrors);
    
    if (Status != FDependencyResolver::EResolutionStatus::Success)
    {
        OutErrors.Add(FString::Printf(TEXT("Failed to determine initialization order, status: %d"), 
            static_cast<int32>(Status)));
        return false;
    }
    
    // Convert node IDs to service keys
    for (uint32 NodeId : NodeOrder)
    {
        const FDependencyResolver::FDependencyNode* Node = DependencyResolver->GetNode(NodeId);
        if (Node)
        {
            OutInitializationOrder.Add(Node->Name);
        }
    }
    
    return true;
}

bool FServiceManager::StartServicesInOrder(const TArray<FName>& InOrder)
{
    if (!bIsInitialized)
    {
        return false;
    }
    
    bool bAllStarted = true;
    
    for (const FName& ServiceKey : InOrder)
    {
        FServiceInstance* Instance = GetServiceInstanceByKey(ServiceKey);
        if (!Instance)
        {
            continue;
        }
        
        if (!StartService(Instance->InterfaceType, Instance->ZoneID, Instance->RegionID))
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to start service '%s'"), *ServiceKey.ToString());
            bAllStarted = false;
        }
    }
    
    return bAllStarted;
}

bool FServiceManager::StopServicesInOrder(const TArray<FName>& InOrder)
{
    if (!bIsInitialized)
    {
        return false;
    }
    
    bool bAllStopped = true;
    
    // Stop in reverse order
    for (int32 i = InOrder.Num() - 1; i >= 0; --i)
    {
        const FName& ServiceKey = InOrder[i];
        FServiceInstance* Instance = GetServiceInstanceByKey(ServiceKey);
        if (!Instance)
        {
            continue;
        }
        
        if (!StopService(Instance->InterfaceType, Instance->ZoneID, Instance->RegionID))
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to stop service '%s'"), *ServiceKey.ToString());
            bAllStopped = false;
        }
    }
    
    return bAllStopped;
}

bool FServiceManager::SaveServiceState(FServiceInstance& InInstance)
{
    if (!bIsInitialized || !InInstance.ServicePtr)
    {
        return false;
    }
    
    // Clear previous saved state
    InInstance.SavedState.Empty();
    
    // Save state if service implements ISaveableService
    if (ISaveableService* SaveableService = static_cast<ISaveableService*>(InInstance.ServicePtr))
    {
        return SaveableService->SaveState(InInstance.SavedState);
    }
    
    // No state to save
    return true;
}

bool FServiceManager::RestoreServiceState(FServiceInstance& InInstance)
{
    if (!bIsInitialized || !InInstance.ServicePtr || InInstance.SavedState.Num() == 0)
    {
        return false;
    }
    
    // Restore state if service implements ISaveableService
    if (ISaveableService* SaveableService = static_cast<ISaveableService*>(InInstance.ServicePtr))
    {
        bool bResult = SaveableService->RestoreState(InInstance.SavedState);
        
        // Clear saved state after successful restoration
        if (bResult)
        {
            InInstance.SavedState.Empty();
        }
        
        return bResult;
    }
    
    // No state to restore
    return false;
}

void FServiceManager::CleanupServicePools()
{
    if (!bIsInitialized)
    {
        return;
    }
    
    FScopeLock Lock(&PoolsLock);
    
    for (auto& Pair : ServicePools)
    {
        TSharedPtr<TQueue<void*>> PoolPtr = Pair.Value;
        void* ServicePtr = nullptr;
        
        // Release all services in the pool
        if (PoolPtr)
        {
            while (!PoolPtr->IsEmpty())
            {
                PoolPtr->Dequeue(ServicePtr);
                
                // Release the service
                if (IService* Service = static_cast<IService*>(ServicePtr))
                {
                    Service->Shutdown();
                }
            }
        }
    }
    
    // Clear the pools
    ServicePools.Empty();
    ServicePoolSizes.Empty();
    ServicePoolItemCounts.Empty();
}

void FServiceManager::UpdateAllServiceMetrics()
{
    if (!bIsInitialized)
    {
        return;
    }
    
    double CurrentTime = FPlatformTime::Seconds();
    
    FScopeLock Lock(&InstancesLock);
    
    for (auto& Pair : ServiceInstances)
    {
        FServiceInstance& Instance = Pair.Value;
        
        // Update last health check time
        Instance.Metrics.LastHealthCheckTime = CurrentTime;
        
        // Check service health
        if (IService* Service = static_cast<IService*>(Instance.ServicePtr))
        {
            bool bIsHealthy = Service->IsHealthy();
            
            // Update service state based on health
            if (Instance.State == EServiceState::Active && !bIsHealthy)
            {
                Instance.State = EServiceState::Failing;
                Instance.Metrics.LastFailureTime = CurrentTime;
                
                UE_LOG(LogTemp, Warning, TEXT("Service '%s' is no longer healthy"), *Pair.Key.ToString());
            }
            else if (Instance.State == EServiceState::Failing && bIsHealthy)
            {
                Instance.State = EServiceState::Active;
                Instance.Metrics.LastRecoveryTime = CurrentTime;
                
                UE_LOG(LogTemp, Log, TEXT("Service '%s' has recovered"), *Pair.Key.ToString());
            }
        }
        
        // Update memory usage if possible
        if (IMemoryAwareService* MemoryService = static_cast<IMemoryAwareService*>(Instance.ServicePtr))
        {
            uint64 MemoryUsage = MemoryService->GetMemoryUsage();
            Instance.Metrics.MemoryUsageBytes.Set(MemoryUsage);
        }
    }
}

bool FServiceManager::CreateServiceFactoryAndDependencies(const UClass* InInterfaceType)
{
    if (!bIsInitialized || !InInterfaceType)
    {
        return false;
    }
    
    // This is a placeholder for creating service factories and registering dependencies
    // with the dependency resolver directly
    
    // In a real implementation, this would:
    // 1. Create a factory for the service type if needed
    // 2. Register the factory with the dependency resolver
    // 3. Extract and register dependencies for the service type
    
    return true;
} 