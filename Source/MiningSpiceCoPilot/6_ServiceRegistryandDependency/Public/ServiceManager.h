// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DependencyResolver.h"
#include "../../1_CoreRegistry/Public/Interfaces/IServiceLocator.h"
#include "../../2_MemoryManagement/Public/Interfaces/IMemoryManager.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/CriticalSection.h"
#include "Containers/Map.h"
#include "Templates/SharedPointer.h"
#include "Misc/Optional.h"
#include "Containers/Queue.h"
#include "HAL/ThreadSafeCounter.h"

// Forward declarations
class FServiceHealthMonitor;
class FServiceDebugVisualizer;

/**
 * Service state enumeration
 */
enum class EServiceState : uint8
{
    /** Service is not initialized */
    Uninitialized,
    
    /** Service is initializing */
    Initializing,
    
    /** Service is active and functioning normally */
    Active,
    
    /** Service is experiencing failures */
    Failing,
    
    /** Service is shutting down */
    ShuttingDown,
    
    /** Service is destroyed */
    Destroyed
};

/**
 * Structure for tracking service metrics
 */
struct FServiceMetrics
{
    /** Number of successful operations */
    FThreadSafeCounter64 SuccessfulOperations;
    
    /** Number of failed operations */
    FThreadSafeCounter64 FailedOperations;
    
    /** Total operation time in milliseconds */
    FThreadSafeCounter64 TotalOperationTimeMs;
    
    /** Maximum operation time in milliseconds */
    FThreadSafeCounter64 MaxOperationTimeMs;
    
    /** Memory used by the service in bytes */
    FThreadSafeCounter64 MemoryUsageBytes;
    
    /** Number of active instances */
    FThreadSafeCounter ActiveInstances;
    
    /** Time of last health check */
    double LastHealthCheckTime;
    
    /** Time of last failure */
    double LastFailureTime;
    
    /** Time of last recovery */
    double LastRecoveryTime;
    
    /** Default constructor */
    FServiceMetrics()
        : LastHealthCheckTime(0.0)
        , LastFailureTime(0.0)
        , LastRecoveryTime(0.0)
    {
    }
    
    /** Copy assignment operator */
    FServiceMetrics& operator=(const FServiceMetrics& Other)
    {
        if (this != &Other)
        {
            SuccessfulOperations.Set(Other.SuccessfulOperations.GetValue());
            FailedOperations.Set(Other.FailedOperations.GetValue());
            TotalOperationTimeMs.Set(Other.TotalOperationTimeMs.GetValue());
            MaxOperationTimeMs.Set(Other.MaxOperationTimeMs.GetValue());
            MemoryUsageBytes.Set(Other.MemoryUsageBytes.GetValue());
            ActiveInstances.Set(Other.ActiveInstances.GetValue());
            LastHealthCheckTime = Other.LastHealthCheckTime;
            LastFailureTime = Other.LastFailureTime;
            LastRecoveryTime = Other.LastRecoveryTime;
        }
        return *this;
    }
    
    /** Resets all metrics */
    void Reset()
    {
        SuccessfulOperations.Set(0);
        FailedOperations.Set(0);
        TotalOperationTimeMs.Set(0);
        MaxOperationTimeMs.Set(0);
        MemoryUsageBytes.Set(0);
        ActiveInstances.Set(0);
        LastHealthCheckTime = 0.0;
        LastFailureTime = 0.0;
        LastRecoveryTime = 0.0;
    }
};

/**
 * Structure for service instance
 */
struct FServiceInstance
{
    /** Service interface class */
    const UClass* InterfaceType;
    
    /** Service implementation pointer */
    void* ServicePtr;
    
    /** Current state of the service */
    EServiceState State;
    
    /** Zone ID for zone-specific services */
    int32 ZoneID;
    
    /** Region ID for region-specific services */
    int32 RegionID;
    
    /** Metrics for this service */
    FServiceMetrics Metrics;
    
    /** Saved state for recovery */
    TArray<uint8> SavedState;
    
    /** Whether this service is part of a pool */
    bool bIsPooled;
    
    /** Time when the service was created */
    double CreationTime;
    
    /** Time when the service was last accessed */
    double LastAccessTime;
    
    /** Default constructor */
    FServiceInstance()
        : InterfaceType(nullptr)
        , ServicePtr(nullptr)
        , State(EServiceState::Uninitialized)
        , ZoneID(INDEX_NONE)
        , RegionID(INDEX_NONE)
        , bIsPooled(false)
        , CreationTime(0.0)
        , LastAccessTime(0.0)
    {
    }
};

/**
 * Service configuration structure
 */
struct FServiceConfiguration
{
    /** Map of configuration parameters */
    TMap<FName, FString> Parameters;
    
    /** Whether this service can be recovered after failure */
    bool bCanRecover;
    
    /** Whether this service should save state for recovery */
    bool bSaveStateForRecovery;
    
    /** Whether this service should be pooled */
    bool bEnablePooling;
    
    /** Maximum size of service pool */
    int32 MaxPoolSize;
    
    /** Default constructor */
    FServiceConfiguration()
        : bCanRecover(true)
        , bSaveStateForRecovery(true)
        , bEnablePooling(false)
        , MaxPoolSize(5)
    {
    }
};

/**
 * Service manager for the mining system
 * Manages service lifecycle, initialization ordering, and performance metrics
 */
class MININGSPICECOPILOT_API FServiceManager
{
    // Add friend class declarations
    friend class FServiceHealthMonitor;
    friend class FServiceDebugVisualizer;

public:
    /** Default constructor */
    FServiceManager();
    
    /** Destructor */
    ~FServiceManager();
    
    /**
     * Initialize the service manager
     * @return True if initialization was successful
     */
    bool Initialize();
    
    /**
     * Shutdown the service manager
     */
    void Shutdown();
    
    /**
     * Check if the service manager is initialized
     * @return True if initialized
     */
    bool IsInitialized() const;
    
    /**
     * Record a service operation for metrics tracking
     * @param ServiceKey Service identifier
     * @param bSuccess Whether the operation was successful
     * @param DurationMs Duration of the operation in milliseconds
     * @param MemoryUsed Memory used by the operation in bytes
     */
    void RecordServiceOperation(const FName& ServiceKey, bool bSuccess, float DurationMs, uint64 MemoryUsed = 0);
    
    /**
     * Register a service with the manager
     * @param InService Pointer to the service implementation
     * @param InInterfaceType Type information for the service interface
     * @param InConfiguration Optional configuration for the service
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return True if registration was successful
     */
    bool RegisterService(void* InService, const UClass* InInterfaceType, 
        const FServiceConfiguration& InConfiguration = FServiceConfiguration(),
        int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE);
    
    /**
     * Template method to register a typed service
     * @param InService Pointer to the service implementation
     * @param InConfiguration Optional configuration for the service
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return True if registration was successful
     */
    template<typename T>
    bool RegisterService(T* InService, 
        const FServiceConfiguration& InConfiguration = FServiceConfiguration(),
        int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE)
    {
        return RegisterService(static_cast<void*>(InService), T::StaticClass(), 
            InConfiguration, InZoneID, InRegionID);
    }
    
    /**
     * Unregister a service from the manager
     * @param InInterfaceType Type information for the service interface
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return True if unregistration was successful
     */
    bool UnregisterService(const UClass* InInterfaceType, 
        int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE);
    
    /**
     * Template method to unregister a typed service
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return True if unregistration was successful
     */
    template<typename T>
    bool UnregisterService(int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE)
    {
        return UnregisterService(T::StaticClass(), InZoneID, InRegionID);
    }
    
    /**
     * Start a service
     * @param InInterfaceType Type information for the service interface
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return True if service was started successfully
     */
    bool StartService(const UClass* InInterfaceType, 
        int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE);
    
    /**
     * Template method to start a typed service
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return True if service was started successfully
     */
    template<typename T>
    bool StartService(int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE)
    {
        return StartService(T::StaticClass(), InZoneID, InRegionID);
    }
    
    /**
     * Stop a service
     * @param InInterfaceType Type information for the service interface
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return True if service was stopped successfully
     */
    bool StopService(const UClass* InInterfaceType, 
        int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE);
    
    /**
     * Template method to stop a typed service
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return True if service was stopped successfully
     */
    template<typename T>
    bool StopService(int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE)
    {
        return StopService(T::StaticClass(), InZoneID, InRegionID);
    }
    
    /**
     * Restart a service
     * @param InInterfaceType Type information for the service interface
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @param bPreserveState Whether to preserve state during restart
     * @return True if service was restarted successfully
     */
    bool RestartService(const UClass* InInterfaceType, 
        int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE,
        bool bPreserveState = true);
    
    /**
     * Template method to restart a typed service
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @param bPreserveState Whether to preserve state during restart
     * @return True if service was restarted successfully
     */
    template<typename T>
    bool RestartService(int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE,
        bool bPreserveState = true)
    {
        return RestartService(T::StaticClass(), InZoneID, InRegionID, bPreserveState);
    }
    
    /**
     * Get the state of a service
     * @param InInterfaceType Type information for the service interface
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return State of the service or Uninitialized if not found
     */
    EServiceState GetServiceState(const UClass* InInterfaceType, 
        int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) const;
    
    /**
     * Template method to get the state of a typed service
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return State of the service or Uninitialized if not found
     */
    template<typename T>
    EServiceState GetServiceState(int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) const
    {
        return GetServiceState(T::StaticClass(), InZoneID, InRegionID);
    }
    
    /**
     * Get the metrics for a service
     * @param InInterfaceType Type information for the service interface
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return Metrics for the service or nullptr if not found
     */
    const FServiceMetrics* GetServiceMetrics(const UClass* InInterfaceType, 
        int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) const;
    
    /**
     * Template method to get the metrics for a typed service
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return Metrics for the service or nullptr if not found
     */
    template<typename T>
    const FServiceMetrics* GetServiceMetrics(int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) const
    {
        return GetServiceMetrics(T::StaticClass(), InZoneID, InRegionID);
    }
    
    /**
     * Record operation metrics for a service
     * @param InInterfaceType Type information for the service interface
     * @param bSuccess Whether the operation was successful
     * @param DurationMs Duration of the operation in milliseconds
     * @param MemoryUsed Memory used by the operation in bytes
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     */
    void RecordServiceMetrics(const UClass* InInterfaceType, 
        bool bSuccess, float DurationMs, uint64 MemoryUsed = 0,
        int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE);
    
    /**
     * Template method to record operation metrics for a typed service
     * @param bSuccess Whether the operation was successful
     * @param DurationMs Duration of the operation in milliseconds
     * @param MemoryUsed Memory used by the operation in bytes
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     */
    template<typename T>
    void RecordServiceMetrics(bool bSuccess, float DurationMs, uint64 MemoryUsed = 0,
        int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE)
    {
        RecordServiceMetrics(T::StaticClass(), bSuccess, DurationMs, MemoryUsed, InZoneID, InRegionID);
    }
    
    /**
     * Get a service from the pool or create a new instance
     * @param InInterfaceType Type information for the service interface
     * @param InZoneID Zone identifier for the service
     * @return Pointer to the service or nullptr if unable to acquire
     */
    void* AcquirePooledService(const UClass* InInterfaceType, int32 InZoneID);
    
    /**
     * Template method to get a typed service from the pool
     * @param InZoneID Zone identifier for the service
     * @return Pointer to the typed service or nullptr if unable to acquire
     */
    template<typename T>
    T* AcquirePooledService(int32 InZoneID)
    {
        return static_cast<T*>(AcquirePooledService(T::StaticClass(), InZoneID));
    }
    
    /**
     * Return a service to the pool
     * @param InService Pointer to the service
     * @param InInterfaceType Type information for the service interface
     * @param InZoneID Zone identifier for the service
     * @return True if the service was returned to the pool successfully
     */
    bool ReleasePooledService(void* InService, const UClass* InInterfaceType, int32 InZoneID);
    
    /**
     * Template method to return a typed service to the pool
     * @param InService Pointer to the typed service
     * @param InZoneID Zone identifier for the service
     * @return True if the service was returned to the pool successfully
     */
    template<typename T>
    bool ReleasePooledService(T* InService, int32 InZoneID)
    {
        return ReleasePooledService(static_cast<void*>(InService), T::StaticClass(), InZoneID);
    }
    
    /**
     * Create a service pool for a specific type and zone
     * @param InInterfaceType Type information for the service interface
     * @param InZoneID Zone identifier for the services
     * @param InMaxPoolSize Maximum size of the pool
     * @return True if the pool was created successfully
     */
    bool CreateServicePool(const UClass* InInterfaceType, int32 InZoneID, int32 InMaxPoolSize = 5);
    
    /**
     * Template method to create a service pool for a specific type and zone
     * @param InZoneID Zone identifier for the services
     * @param InMaxPoolSize Maximum size of the pool
     * @return True if the pool was created successfully
     */
    template<typename T>
    bool CreateServicePool(int32 InZoneID, int32 InMaxPoolSize = 5)
    {
        return CreateServicePool(T::StaticClass(), InZoneID, InMaxPoolSize);
    }
    
    /**
     * Reconfigure a service with new parameters
     * @param InInterfaceType Type information for the service interface
     * @param InConfiguration New configuration for the service
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return True if reconfiguration was successful
     */
    bool ReconfigureService(const UClass* InInterfaceType, 
        const FServiceConfiguration& InConfiguration,
        int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE);
    
    /**
     * Template method to reconfigure a typed service
     * @param InConfiguration New configuration for the service
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return True if reconfiguration was successful
     */
    template<typename T>
    bool ReconfigureService(const FServiceConfiguration& InConfiguration,
        int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE)
    {
        return ReconfigureService(T::StaticClass(), InConfiguration, InZoneID, InRegionID);
    }
    
    /**
     * Get the configuration of a service
     * @param InInterfaceType Type information for the service interface
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return Configuration of the service or default if not found
     */
    FServiceConfiguration GetServiceConfiguration(const UClass* InInterfaceType,
        int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) const;
    
    /**
     * Template method to get the configuration of a typed service
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return Configuration of the service or default if not found
     */
    template<typename T>
    FServiceConfiguration GetServiceConfiguration(int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) const
    {
        return GetServiceConfiguration(T::StaticClass(), InZoneID, InRegionID);
    }
    
    /**
     * Get all services managed by the manager
     * @return Array of service instances
     */
    TArray<FServiceInstance> GetAllServices() const;
    
    /**
     * Get services by state
     * @param InState State to filter by
     * @return Array of service instances in the specified state
     */
    TArray<FServiceInstance> GetServicesByState(EServiceState InState) const;
    
    /**
     * Get the singleton instance of the service manager
     * @return Reference to the service manager instance
     */
    static FServiceManager& Get();

    /**
     * Get all service keys
     * @param OutKeys Array to store the keys
     */
    void GetAllServiceKeys(TArray<FName>& OutKeys) const;

    /**
     * Create a key for a service
     * @param InInterfaceType Type information for the service interface
     * @param InZoneID Zone identifier
     * @param InRegionID Region identifier
     * @return Name identifier for the service
     */
    FName CreateServiceKey(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID) const;
    
    /**
     * Get a service instance by its key
     * @param InKey Service key
     * @return Pointer to the service instance, or nullptr if not found
     */
    FServiceInstance* GetServiceInstanceByKey(const FName& InKey);
    
    /**
     * Get a const service instance by its key
     * @param InKey Service key
     * @return Const pointer to the service instance, or nullptr if not found
     */
    const FServiceInstance* GetServiceInstanceByKey(const FName& InKey) const;

private:
    /**
     * Determine initialization order for services
     * @param OutInitializationOrder Array to store the ordered service keys
     * @param OutErrors Array to store error messages
     * @return True if order was determined successfully
     */
    bool DetermineServiceOrder(TArray<FName>& OutInitializationOrder, TArray<FString>& OutErrors);
    
    /**
     * Start services in the specified order
     * @param InOrder Array of service keys in initialization order
     * @return True if all services were started successfully
     */
    bool StartServicesInOrder(const TArray<FName>& InOrder);
    
    /**
     * Stop services in the specified order
     * @param InOrder Array of service keys in shutdown order
     * @return True if all services were stopped successfully
     */
    bool StopServicesInOrder(const TArray<FName>& InOrder);
    
    /**
     * Save the state of a service instance
     * @param InInstance Service instance to save state for
     * @return True if state was saved successfully
     */
    bool SaveServiceState(FServiceInstance& InInstance);
    
    /**
     * Restore the state of a service instance
     * @param InInstance Service instance to restore state to
     * @return True if state was restored successfully
     */
    bool RestoreServiceState(FServiceInstance& InInstance);
    
    /**
     * Clean up service pools
     */
    void CleanupServicePools();
    
    /**
     * Update metrics for all services
     */
    void UpdateAllServiceMetrics();
    
    /**
     * Create factory for a service type and its dependencies
     * @param InInterfaceType Type information for the service interface
     * @return True if factory and dependencies were created successfully
     */
    bool CreateServiceFactoryAndDependencies(const UClass* InInterfaceType);

    /** Map of service instances by key */
    TMap<FName, FServiceInstance> ServiceInstances;
    
    /** Map of service configurations by key */
    TMap<FName, FServiceConfiguration> ServiceConfigurations;
    
    /** Map of service pools by type and zone */
    TMap<FName, TSharedPtr<TQueue<void*>>> ServicePools;
    
    /** Map of service pool sizes by type and zone */
    TMap<FName, int32> ServicePoolSizes;
    
    /** Map of current item counts in each service pool */
    TMap<FName, int32> ServicePoolItemCounts;
    
    /** Dependency resolver for determining initialization order */
    TSharedPtr<FDependencyResolver> DependencyResolver;
    
    /** Service locator reference */
    IServiceLocator* ServiceLocator;
    
    /** Memory manager reference */
    IMemoryManager* MemoryManager;
    
    /** Lock for thread-safe access to service instances */
    mutable FCriticalSection InstancesLock;
    
    /** Lock for thread-safe access to service pools */
    mutable FCriticalSection PoolsLock;
    
    /** Flag indicating if the manager is initialized */
    FThreadSafeBool bIsInitialized;
    
    /** Singleton instance */
    static FServiceManager* ManagerInstance;
    
    /** Critical section for singleton initialization */
    static FCriticalSection SingletonLock;
}; 