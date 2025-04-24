// Copyright Epic Games, Inc. All Rights Reserved.

#include "SVOServiceProvider.h"
#include "../../1_CoreRegistry/Public/SVOTypeRegistry.h"
#include "../../1_CoreRegistry/Public/Interfaces/IServiceLocator.h"
#include "../../3_ThreadingTaskSystem/Public/Interfaces/ITaskScheduler.h"
#include "../../1_CoreRegistry/Public/Interfaces/INodeManager.h"
#include "../../1_CoreRegistry/Public/Interfaces/IFieldOperator.h"
#include "../../1_CoreRegistry/Public/Interfaces/INodeSerializer.h"
#include "Logging/LogMacros.h"
#include "Misc/ScopeLock.h"
#include "../Public/DependencyResolver.h"

// Forward-declared interface concrete implementations
class UObject;

// Define log category for this class
DEFINE_LOG_CATEGORY_STATIC(LogSVOServiceProvider, Log, All);

USVOServiceProvider::USVOServiceProvider()
    : ServiceLocator(nullptr)
    , bInitialized(false)
{
    // Initialize default configuration
    ServiceConfig.SetValue(TEXT("EnableCaching"), TEXT("true"));
    ServiceConfig.SetValue(TEXT("CacheTimeoutSeconds"), TEXT("5.0"));
    ServiceConfig.SetValue(TEXT("MaxCachedItemsPerType"), TEXT("100"));
    
    // Initialize health status
    ServiceHealth.Status = static_cast<FServiceHealth::EStatus>(EServiceHealthStatus::Unknown);
    ServiceHealth.DiagnosticMessage = TEXT("Not initialized");
    ServiceHealth.ErrorCount = 0;
    ServiceHealth.WarningCount = 0;
    
    // Register common high-frequency services for pre-warming
    // This list can be expanded based on profiling results
    HighFrequencyServices.Empty();
}

USVOServiceProvider::~USVOServiceProvider()
{
    if (bInitialized)
    {
        ShutdownServices();
    }
}

TArray<TSubclassOf<UInterface>> USVOServiceProvider::GetProvidedServices() const
{
    TArray<TSubclassOf<UInterface>> ProvidedServices;
    
    // Add all service interfaces provided by this provider
    // These should be interfaces defined by the SVO system
    // e.g., ProvidedServices.Add(UNodeManager::StaticClass());
    
    return ProvidedServices;
}

bool USVOServiceProvider::RegisterServices(IServiceLocator* InServiceLocator, int32 InZoneID, int32 InRegionID)
{
    if (!InServiceLocator)
    {
        UE_LOG(LogSVOServiceProvider, Error, TEXT("Cannot register services: Service locator is null"));
        return false;
    }
    
    // Store the service locator for later use
    ServiceLocator = InServiceLocator;
    
    // Initialize with the type registry if not already done
    if (!bInitialized)
    {
        InitializeWithRegistry();
    }
    
    // Register all services with the locator
    FScopeLock Lock(&ServiceLock);
    
    bool bSuccess = true;
    
    // Register node managers
    for (const auto& NodeManagerPair : NodeManagers)
    {
        if (!ServiceLocator->RegisterService(NodeManagerPair.Value.Get(), InZoneID, InRegionID))
        {
            UE_LOG(LogSVOServiceProvider, Warning, TEXT("Failed to register node manager service"));
            bSuccess = false;
        }
    }
    
    // Register field operators
    for (const auto& OperatorPair : FieldOperators)
    {
        if (!ServiceLocator->RegisterService(OperatorPair.Value.Get(), InZoneID, InRegionID))
        {
            UE_LOG(LogSVOServiceProvider, Warning, TEXT("Failed to register field operator service"));
            bSuccess = false;
        }
    }
    
    // Register serializers
    for (const auto& SerializerPair : Serializers)
    {
        if (!ServiceLocator->RegisterService(SerializerPair.Value.Get(), InZoneID, InRegionID))
        {
            UE_LOG(LogSVOServiceProvider, Warning, TEXT("Failed to register serializer service"));
            bSuccess = false;
        }
    }
    
    // Pre-warm service cache if enabled in config
    if (ServiceConfig.GetValueAsBool(TEXT("EnableCaching"), true))
    {
        PreWarmServiceCache(InRegionID);
    }
    
    UE_LOG(LogSVOServiceProvider, Log, TEXT("Registered SVO services with service locator (Region: %d, Zone: %d)"), 
        InRegionID, InZoneID);
    
    return bSuccess;
}

bool USVOServiceProvider::UnregisterServices(IServiceLocator* InServiceLocator, int32 InZoneID, int32 InRegionID)
{
    if (!InServiceLocator || !bInitialized)
    {
        UE_LOG(LogSVOServiceProvider, Error, TEXT("Cannot unregister services: Service locator is null or provider not initialized"));
        return false;
    }
    
    FScopeLock Lock(&ServiceLock);
    
    bool bSuccess = true;
    
    // Unregister node managers
    for (const auto& NodeManagerPair : NodeManagers)
    {
        const TSubclassOf<UInterface> NodeManagerClass = UNodeManager::StaticClass();
        if (!InServiceLocator->UnregisterService(NodeManagerClass, InZoneID, InRegionID))
        {
            UE_LOG(LogSVOServiceProvider, Warning, TEXT("Failed to unregister node manager service"));
            bSuccess = false;
        }
    }
    
    // Unregister field operators
    for (const auto& OperatorPair : FieldOperators)
    {
        const TSubclassOf<UInterface> FieldOperatorClass = UFieldOperator::StaticClass();
        if (!InServiceLocator->UnregisterService(FieldOperatorClass, InZoneID, InRegionID))
        {
            UE_LOG(LogSVOServiceProvider, Warning, TEXT("Failed to unregister field operator service"));
            bSuccess = false;
        }
    }
    
    // Unregister serializers
    for (const auto& SerializerPair : Serializers)
    {
        const TSubclassOf<UInterface> SerializerClass = UNodeSerializer::StaticClass();
        if (!InServiceLocator->UnregisterService(SerializerClass, InZoneID, InRegionID))
        {
            UE_LOG(LogSVOServiceProvider, Warning, TEXT("Failed to unregister serializer service"));
            bSuccess = false;
        }
    }
    
    // Clear service cache
    ClearServiceCache();
    
    UE_LOG(LogSVOServiceProvider, Log, TEXT("Unregistered SVO services from service locator (Region: %d, Zone: %d)"), 
        InRegionID, InZoneID);
    
    return bSuccess;
}

bool USVOServiceProvider::InitializeServices()
{
    // Prevent double initialization
    if (bInitialized)
    {
        return true;
    }
    
    // Update health status
    ServiceHealth.Status = static_cast<FServiceHealth::EStatus>(EServiceHealthStatus::Healthy);
    ServiceHealth.DiagnosticMessage = TEXT("Service initialized successfully");
    ServiceHealth.ErrorCount = 0;
    ServiceHealth.WarningCount = 0;
    
    // Get configuration from service registry
    InitializeWithRegistry();
    
    bInitialized = true;
    
    UE_LOG(LogSVOServiceProvider, Log, TEXT("SVO services initialized successfully"));
    
    return true;
}

void USVOServiceProvider::ShutdownServices()
{
    if (!bInitialized)
    {
        return;
    }
    
    FScopeLock Lock(&ServiceLock);
    
    // Clear all service maps
    NodeManagers.Empty();
    FieldOperators.Empty();
    Serializers.Empty();
    ServiceCache.Empty();
    
    // Update status
    bInitialized = false;
    ServiceHealth.Status = static_cast<FServiceHealth::EStatus>(EServiceHealthStatus::Unknown);
    ServiceHealth.DiagnosticMessage = TEXT("Service shut down");
    
    UE_LOG(LogSVOServiceProvider, Log, TEXT("SVO services shut down"));
}

FName USVOServiceProvider::GetProviderName() const
{
    return FName(TEXT("SVOServiceProvider"));
}

TArray<FServiceDependency> USVOServiceProvider::GetServiceDependencies() const
{
    return ServiceDependencies;
}

bool USVOServiceProvider::HandleLifecyclePhase(EServiceLifecyclePhase Phase)
{
    switch (Phase)
    {
        case EServiceLifecyclePhase::Initialize:
            return InitializeServices();
            
        case EServiceLifecyclePhase::Shutdown:
            ShutdownServices();
            return true;
            
        case EServiceLifecyclePhase::PreInitialize:
        case EServiceLifecyclePhase::PostInitialize:
        case EServiceLifecyclePhase::PreShutdown:
        case EServiceLifecyclePhase::PostShutdown:
            // Handle other lifecycle phases as needed
            return true;
            
        default:
            return true;
    }
}

EServiceScope USVOServiceProvider::GetServiceScope() const
{
    return EServiceScope::Region;
}

FServiceHealth USVOServiceProvider::GetServiceHealth() const
{
    return ServiceHealth;
}

bool USVOServiceProvider::RecoverServices()
{
    if (!bInitialized)
    {
        return InitializeServices();
    }
    
    FScopeLock Lock(&ServiceLock);
    
    // Check each service type and recover if needed
    bool bAllHealthy = true;
    
    // Check node managers
    for (auto& NodeManagerPair : NodeManagers)
    {
        if (!NodeManagerPair.Value.IsValid())
        {
            UE_LOG(LogSVOServiceProvider, Warning, TEXT("Attempting to recover invalid node manager"));
            // Attempt recovery (implementation depends on specific service)
            bAllHealthy = false;
        }
    }
    
    // Check field operators
    for (auto& OperatorPair : FieldOperators)
    {
        if (!OperatorPair.Value.IsValid())
        {
            UE_LOG(LogSVOServiceProvider, Warning, TEXT("Attempting to recover invalid field operator"));
            // Attempt recovery
            bAllHealthy = false;
        }
    }
    
    // Check serializers
    for (auto& SerializerPair : Serializers)
    {
        if (!SerializerPair.Value.IsValid())
        {
            UE_LOG(LogSVOServiceProvider, Warning, TEXT("Attempting to recover invalid serializer"));
            // Attempt recovery
            bAllHealthy = false;
        }
    }
    
    // Update health status
    if (bAllHealthy)
    {
        ServiceHealth.Status = static_cast<FServiceHealth::EStatus>(EServiceHealthStatus::Healthy);
        ServiceHealth.DiagnosticMessage = TEXT("Healthy");
        ServiceHealth.ErrorCount = 0;
    }
    else
    {
        ServiceHealth.Status = static_cast<FServiceHealth::EStatus>(EServiceHealthStatus::Degraded);
        ServiceHealth.DiagnosticMessage = TEXT("Degraded");
        ServiceHealth.ErrorCount++;
    }
    
    UE_LOG(LogSVOServiceProvider, Log, TEXT("SVO services health status: %s"), *ServiceHealth.DiagnosticMessage);
    
    return bAllHealthy;
}

FServiceConfig USVOServiceProvider::GetServiceConfig() const
{
    return ServiceConfig;
}

bool USVOServiceProvider::UpdateServiceConfig(const FServiceConfig& InConfig)
{
    FScopeLock Lock(&ServiceLock);
    
    // Update configuration
    ServiceConfig = InConfig;
    
    // Apply configuration changes if needed
    if (!ServiceConfig.GetValueAsBool(TEXT("EnableCaching"), true))
    {
        // Clear cache if caching is disabled
        ClearServiceCache();
    }
    
    UE_LOG(LogSVOServiceProvider, Log, TEXT("Updated SVO service configuration"));
    
    return true;
}

bool USVOServiceProvider::ValidateServiceDependencies(IServiceLocator* InServiceLocator, TArray<FServiceDependency>& OutMissingDependencies)
{
    if (!InServiceLocator)
    {
        UE_LOG(LogSVOServiceProvider, Error, TEXT("Cannot validate dependencies: Service locator is null"));
        return false;
    }
    
    bool bAllDependenciesMet = true;
    
    // Check each dependency
    for (const FServiceDependency& Dependency : ServiceDependencies)
    {
        // Skip optional dependencies
        if (Dependency.DependencyKind == EServiceDependencyType::Optional)
        {
            continue;
        }
        
        // Check if the dependency is available
        if (!InServiceLocator->HasService(Dependency.DependencyType))
        {
            OutMissingDependencies.Add(Dependency);
            bAllDependenciesMet = false;
        }
    }
    
    return bAllDependenciesMet;
}

TArray<TSubclassOf<UInterface>> USVOServiceProvider::GetDependentServices(IServiceLocator* InServiceLocator)
{
    TArray<TSubclassOf<UInterface>> DependentServices;
    
    if (!InServiceLocator || !bInitialized)
    {
        return DependentServices;
    }
    
    // This would require querying the service locator for all services that depend on our services
    // Implementation depends on service locator capabilities
    
    return DependentServices;
}

bool USVOServiceProvider::RegisterNodeManager(ESVONodeClass InNodeClass, TSharedPtr<class INodeManager> InManager, int32 InRegionID)
{
    if (!bInitialized || !InManager.IsValid())
    {
        UE_LOG(LogSVOServiceProvider, Error, TEXT("Cannot register node manager: Provider not initialized or manager is invalid"));
        return false;
    }
    
    FScopeLock Lock(&ServiceLock);
    
    // Create a key using the node class and region ID
    uint64 Key = static_cast<uint64>(InNodeClass) << 32 | static_cast<uint32>(InRegionID);
    
    // Register in the map
    NodeManagers.Add(Key, InManager);
    
    UE_LOG(LogSVOServiceProvider, Verbose, TEXT("Registered node manager for class %d, region %d"), 
        static_cast<int32>(InNodeClass), InRegionID);
    
    // If we have a service locator, register with it too
    if (ServiceLocator)
    {
        // Use .Get() to get the raw pointer
        ServiceLocator->RegisterService(InManager.Get(), INDEX_NONE, InRegionID);
    }
    
    return true;
}

TSharedPtr<class INodeManager> USVOServiceProvider::ResolveNodeManager(ESVONodeClass InNodeClass, int32 InRegionID)
{
    if (!bInitialized)
    {
        UE_LOG(LogSVOServiceProvider, Error, TEXT("Cannot resolve node manager: Provider not initialized"));
        return nullptr;
    }
    
    FScopeLock Lock(&ServiceLock);
    
    // Generate key for lookup
    uint64 Key = GenerateCacheKey(static_cast<uint32>(InNodeClass), InRegionID);
    
    // Look up in the map
    TSharedPtr<INodeManager>* ManagerPtr = NodeManagers.Find(Key);
    if (ManagerPtr && ManagerPtr->IsValid())
    {
        return *ManagerPtr;
    }
    
    // If not found and we have a service locator, try to resolve through it
    if (ServiceLocator)
    {
        TSubclassOf<UInterface> NodeManagerClass = UNodeManager::StaticClass();
        
        // Resolve through service locator
        void* RawManager = ServiceLocator->ResolveService(NodeManagerClass, INDEX_NONE, InRegionID);
        if (RawManager)
        {
            INodeManager* ManagerInterface = static_cast<INodeManager*>(RawManager);
            
            // Use TSharedPtr with empty deleter since we don't own the object
            TSharedPtr<INodeManager> Manager(ManagerInterface, [](INodeManager*) {});
            
            // Cache for future use
            NodeManagers.Add(Key, Manager);
            
            return Manager;
        }
    }
    
    // If not found and we have a registry, try to find a compatible manager
    if (TypeRegistry.IsValid())
    {
        // Try to find a compatible node class manager based on node hierarchy
        // This is a simplification - actual implementation would depend on SVO node class relationships
        
        // For now just log the miss
        UE_LOG(LogSVOServiceProvider, Verbose, TEXT("Node manager cache miss for node class %d (Region: %d)"), 
            static_cast<int32>(InNodeClass), InRegionID);
    }
    
    return nullptr;
}

bool USVOServiceProvider::RegisterFieldOperator(uint32 InOperatorType, TSharedPtr<class IFieldOperator> InOperator, int32 InRegionID)
{
    if (!bInitialized || !InOperator.IsValid())
    {
        UE_LOG(LogSVOServiceProvider, Error, TEXT("Cannot register field operator: Provider not initialized or operator is invalid"));
        return false;
    }
    
    FScopeLock Lock(&ServiceLock);
    
    // Generate key for this field operator
    uint64 Key = GenerateCacheKey(InOperatorType, InRegionID);
    
    // Register in the map
    FieldOperators.Add(Key, InOperator);
    
    UE_LOG(LogSVOServiceProvider, Verbose, TEXT("Registered field operator for type %u (Region: %d)"), 
        InOperatorType, InRegionID);
    
    // If we have a service locator, register with it too
    if (ServiceLocator)
    {
        ServiceLocator->RegisterService(InOperator.Get(), INDEX_NONE, InRegionID);
    }
    
    return true;
}

TSharedPtr<class IFieldOperator> USVOServiceProvider::ResolveFieldOperator(uint32 InOperatorType, int32 InRegionID)
{
    if (!bInitialized || !ServiceLocator)
    {
        UE_LOG(LogSVOServiceProvider, Error, TEXT("Cannot resolve field operator: Provider not initialized"));
        return nullptr;
    }
    
    FScopeLock Lock(&ServiceLock);
    
    // Look up in the map
    TSharedPtr<IFieldOperator>* OperatorPtr = FieldOperators.Find(InOperatorType);
    if (OperatorPtr && OperatorPtr->IsValid())
    {
        return *OperatorPtr;
    }
    
    // If we have a service locator, try to resolve from it
    TSubclassOf<UInterface> FieldOperatorClass = UFieldOperator::StaticClass();
    
    // resolve using direct ResolveService method
    void* RawOperator = ServiceLocator->ResolveService(FieldOperatorClass, INDEX_NONE, InRegionID);
    if (RawOperator)
    {
        IFieldOperator* OperatorInterface = static_cast<IFieldOperator*>(RawOperator);
        
        // Use TSharedPtr with empty deleter since we don't own the object
        TSharedPtr<IFieldOperator> Operator(OperatorInterface, [](IFieldOperator*) {});
        
        // Cache for future use
        FieldOperators.Add(InOperatorType, Operator);
        
        return Operator;
    }
    
    return nullptr;
}

bool USVOServiceProvider::RegisterSerializer(ESVONodeClass InNodeClass, TSharedPtr<class INodeSerializer> InSerializer, int32 InRegionID)
{
    if (!bInitialized || !InSerializer.IsValid())
    {
        UE_LOG(LogSVOServiceProvider, Error, TEXT("Cannot register serializer: Provider not initialized or serializer is invalid"));
        return false;
    }
    
    FScopeLock Lock(&ServiceLock);
    
    // Generate key for this serializer
    uint64 Key = GenerateCacheKey(static_cast<uint32>(InNodeClass), InRegionID);
    
    // Register in the map
    Serializers.Add(Key, InSerializer);
    
    UE_LOG(LogSVOServiceProvider, Verbose, TEXT("Registered serializer for node class %d (Region: %d)"), 
        static_cast<int32>(InNodeClass), InRegionID);
    
    // If we have a service locator, register with it too
    if (ServiceLocator)
    {
        ServiceLocator->RegisterService(InSerializer.Get(), INDEX_NONE, InRegionID);
    }
    
    return true;
}

TSharedPtr<class INodeSerializer> USVOServiceProvider::ResolveSerializer(ESVONodeClass InNodeClass, int32 InRegionID)
{
    if (!bInitialized)
    {
        UE_LOG(LogSVOServiceProvider, Error, TEXT("Cannot resolve serializer: Provider not initialized"));
        return nullptr;
    }
    
    FScopeLock Lock(&ServiceLock);
    
    // Generate key for lookup
    uint64 Key = GenerateCacheKey(static_cast<uint32>(InNodeClass), InRegionID);
    
    // Look up in the map
    TSharedPtr<INodeSerializer>* SerializerPtr = Serializers.Find(Key);
    if (SerializerPtr && SerializerPtr->IsValid())
    {
        return *SerializerPtr;
    }
    
    // If not found and we have a service locator, try to resolve through it
    if (ServiceLocator)
    {
        TSubclassOf<UInterface> SerializerClass = UNodeSerializer::StaticClass();
        
        // Resolve through service locator
        void* RawSerializer = ServiceLocator->ResolveService(SerializerClass, INDEX_NONE, InRegionID);
        if (RawSerializer)
        {
            INodeSerializer* SerializerInterface = static_cast<INodeSerializer*>(RawSerializer);
            
            // Use TSharedPtr with empty deleter since we don't own the object
            TSharedPtr<INodeSerializer> Serializer(SerializerInterface, [](INodeSerializer*) {});
            
            // Cache for future use
            Serializers.Add(Key, Serializer);
            
            return Serializer;
        }
    }
    
    // If not found, log the miss
    UE_LOG(LogSVOServiceProvider, Verbose, TEXT("Serializer cache miss for node class %d (Region: %d)"), 
        static_cast<int32>(InNodeClass), InRegionID);
    
    return nullptr;
}

TSharedPtr<class IFieldOperator> USVOServiceProvider::CoordinateCrossRegionOperation(int32 InSourceRegionID, int32 InTargetRegionID, uint32 InOperatorType)
{
    if (!bInitialized)
    {
        UE_LOG(LogSVOServiceProvider, Error, TEXT("Cannot coordinate cross-region operation: Provider not initialized"));
        return nullptr;
    }
    
    // Get source and target operators
    TSharedPtr<IFieldOperator> SourceOperator = ResolveFieldOperator(InOperatorType, InSourceRegionID);
    TSharedPtr<IFieldOperator> TargetOperator = ResolveFieldOperator(InOperatorType, InTargetRegionID);
    
    if (!SourceOperator.IsValid() || !TargetOperator.IsValid())
    {
        UE_LOG(LogSVOServiceProvider, Warning, TEXT("Cannot coordinate cross-region operation: Missing operators"));
        return nullptr;
    }
    
    // This is a simplified implementation - in a real system you'd need to create
    // a special operator that handles cross-region coordination
    
    // For now, just return the source operator and log the coordination
    UE_LOG(LogSVOServiceProvider, Verbose, TEXT("Coordinating cross-region operation between regions %d and %d for operator type %u"), 
        InSourceRegionID, InTargetRegionID, InOperatorType);
    
    return SourceOperator;
}

TSharedPtr<UObject> USVOServiceProvider::FastPathLookup(TSubclassOf<UInterface> InServiceType, int32 InRegionID)
{
    if (!bInitialized || !ServiceLocator)
    {
        return nullptr;
    }
    
    // Generate cache key
    uint64 CacheKey = GenerateCacheKey(InServiceType, InRegionID);
    
    // Check if service is in the cache
    TSharedPtr<UObject>* CachedServicePtr = ServiceCache.Find(CacheKey);
    if (CachedServicePtr && CachedServicePtr->IsValid())
    {
        // Check if cache entry is still valid
        TSharedPtr<FServiceCacheMetadata>* MetadataPtr = ServiceCacheMetadata.Find(CacheKey);
        if (MetadataPtr && MetadataPtr->IsValid())
        {
            // Update last access time
            (*MetadataPtr)->LastAccessTime = FPlatformTime::Seconds();
            (*MetadataPtr)->AccessCount++;
            
            // Fast path successful, return cached service
            return *CachedServicePtr;
        }
    }
    
    // Cache miss - resolve using standard method and update cache
    void* RawService = ServiceLocator->ResolveService(InServiceType, INDEX_NONE, InRegionID);
    if (RawService)
    {
        UObject* ServiceObj = static_cast<UObject*>(RawService);
        TSharedPtr<UObject> Service = TSharedPtr<UObject>(ServiceObj, [](UObject*) {});
        
        // Update cache for future lookups
        UpdateServiceCache(InServiceType, Service, InRegionID);
        
        return Service;
    }
    
    return nullptr;
}

void USVOServiceProvider::ClearServiceCache()
{
    FScopeLock Lock(&ServiceLock);
    ServiceCache.Empty();
}

void USVOServiceProvider::PreWarmServiceCache(int32 InRegionID)
{
    if (!bInitialized || !ServiceLocator)
    {
        return;
    }
    
    // Pre-warm cache for high-frequency services
    for (const TSubclassOf<UInterface> ServiceType : HighFrequencyServices)
    {
        if (!ServiceType)
        {
            continue;
        }
        
        // Try to resolve the service and add to cache
        void* RawService = ServiceLocator->ResolveService(ServiceType, INDEX_NONE, InRegionID);
        if (RawService)
        {
            UObject* ServiceObj = static_cast<UObject*>(RawService);
            TSharedPtr<UObject> Service = TSharedPtr<UObject>(ServiceObj, [](UObject*) {});
            
            // Add to cache
            UpdateServiceCache(ServiceType, Service, InRegionID);
        }
    }
}

void USVOServiceProvider::InitializeWithRegistry()
{
    // Get the SVO type registry
    if (!TypeRegistry.IsValid())
    {
        // Use a direct reference without copy construction
        FSVOTypeRegistry& RegistryRef = FSVOTypeRegistry::Get();
        TypeRegistry = TSharedPtr<FSVOTypeRegistry>(&RegistryRef, [](FSVOTypeRegistry*) {});
    }
    
    // Initialize dependencies
    ServiceDependencies.Empty();
    
    // Add required dependencies
    FServiceDependency TypeRegistryDependency;
    TypeRegistryDependency.DependencyType = nullptr; // This would be the type registry interface
    TypeRegistryDependency.DependencyKind = EServiceDependencyType::Required;
    ServiceDependencies.Add(TypeRegistryDependency);
    
    // Add other dependencies
    FServiceDependency TaskSchedulerDependency;
    TaskSchedulerDependency.DependencyType = nullptr; // This would be the task scheduler interface
    TaskSchedulerDependency.DependencyKind = EServiceDependencyType::Required;
    ServiceDependencies.Add(TaskSchedulerDependency);
    
    // Initialize high-frequency services
    HighFrequencyServices.Empty();
    
    // Add common high-frequency services for pre-warming
    // These would be identified through profiling or domain knowledge
}

void USVOServiceProvider::UpdateServiceCache(TSubclassOf<UInterface> InServiceType, TSharedPtr<UObject> InService, int32 InRegionID)
{
    if (!InService.IsValid() || !ServiceConfig.GetValueAsBool(TEXT("EnableCaching"), true))
    {
        return;
    }
    
    FScopeLock Lock(&ServiceLock);
    
    // Generate cache key
    uint64 CacheKey = GenerateCacheKey(InServiceType, InRegionID);
    
    // Create metadata for the service
    TSharedPtr<FServiceCacheMetadata> MetadataPtr = MakeShared<FServiceCacheMetadata>();
    MetadataPtr->LastAccessTime = FPlatformTime::Seconds();
    MetadataPtr->AccessCount = 1;
    
    // Check if this is a high-frequency service
    if (HighFrequencyServices.Contains(InServiceType))
    {
        MetadataPtr->bIsHighFrequency = true;
        MetadataPtr->Priority = 100; // High priority for high-frequency services
    }
    
    // Update the cache
    ServiceCache.Add(CacheKey, InService);
    ServiceCacheMetadata.Add(CacheKey, MetadataPtr);
    
    // Enforce cache size limits
    const int32 MaxCachedItems = ServiceConfig.GetValueAsInt(TEXT("MaxCachedItemsPerType"), 100);
    if (ServiceCache.Num() > MaxCachedItems)
    {
        // Remove least recently used items
        // In a full implementation, this would use the metadata to make smart decisions
        // For simplicity, we'll just remove the oldest item
        uint64 OldestKey = 0;
        double OldestTime = FPlatformTime::Seconds();
        
        for (const auto& CachePair : ServiceCacheMetadata)
        {
            if (CachePair.Value.IsValid() && CachePair.Value->LastAccessTime < OldestTime)
            {
                OldestTime = CachePair.Value->LastAccessTime;
                OldestKey = CachePair.Key;
            }
        }
        
        if (OldestKey != 0)
        {
            ServiceCache.Remove(OldestKey);
            ServiceCacheMetadata.Remove(OldestKey);
        }
    }
}

uint64 USVOServiceProvider::GenerateCacheKey(TSubclassOf<UInterface> InServiceType, int32 InRegionID) const
{
    if (!InServiceType)
    {
        return 0;
    }
    
    // Combine service type and region ID into a single key
    uint64 TypeHash = static_cast<uint64>(GetTypeHash(InServiceType));
    uint64 RegionHash = static_cast<uint64>(InRegionID) & 0xFFFFFFFF;
    
    return (TypeHash << 32) | RegionHash;
}

// Overloaded version for uint32 type IDs
uint64 USVOServiceProvider::GenerateCacheKey(uint32 TypeId, int32 InRegionID) const
{
    // Combine type ID and region ID into a single key
    uint64 TypeHash = static_cast<uint64>(TypeId);
    uint64 RegionHash = static_cast<uint64>(InRegionID) & 0xFFFFFFFF;
    
    return (TypeHash << 32) | RegionHash;
}
