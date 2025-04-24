// Copyright Epic Games, Inc. All Rights Reserved.

#include "MaterialServiceProvider.h"
#include "../../1_CoreRegistry/Public/MaterialRegistry.h"
#include "../../1_CoreRegistry/Public/Interfaces/IServiceLocator.h"
#include "../../1_CoreRegistry/Public/FMaterialPropertyDependency.h"
#include "../../1_CoreRegistry/Public/Interfaces/IMaterialPropertyService.h"
#include "../../1_CoreRegistry/Public/Interfaces/IMaterialFieldOperator.h"
#include "../../1_CoreRegistry/Public/Interfaces/IMaterialInteractionService.h"
#include "../../1_CoreRegistry/Public/TypeRegistry.h"
#include "Logging/LogMacros.h"
#include "Misc/ScopeLock.h"
#include "../Public/DependencyResolver.h"

// Forward-declared interface concrete implementations
class UObject;
class FTypeRegistry;  // Add FTypeRegistry forward declaration

// Define log category for this class
DEFINE_LOG_CATEGORY_STATIC(LogMaterialServiceProvider, Log, All);

UMaterialServiceProvider::UMaterialServiceProvider()
    : ServiceLocator(nullptr)
    , bInitialized(false)
{
    // Initialize default configuration
    ServiceConfig.SetValue(TEXT("EnableCaching"), TEXT("true"));
    ServiceConfig.SetValue(TEXT("CacheTimeoutSeconds"), TEXT("5.0"));
    ServiceConfig.SetValue(TEXT("MaxCachedItemsPerType"), TEXT("50"));
    
    // Initialize health status
    ServiceHealth.Status = static_cast<FServiceHealth::EStatus>(EServiceHealthStatus::Unknown);
    ServiceHealth.DiagnosticMessage = TEXT("Not initialized");
    ServiceHealth.ErrorCount = 0;
    ServiceHealth.WarningCount = 0;
}

UMaterialServiceProvider::~UMaterialServiceProvider()
{
    if (bInitialized)
    {
        ShutdownServices();
    }
}

TArray<TSubclassOf<UInterface>> UMaterialServiceProvider::GetProvidedServices() const
{
    TArray<TSubclassOf<UInterface>> ProvidedServices;
    
    // Add all service interfaces provided by this provider
    // These should be interfaces defined by the Material system
    // e.g., ProvidedServices.Add(UMaterialPropertyService::StaticClass());
    
    return ProvidedServices;
}

bool UMaterialServiceProvider::RegisterServices(IServiceLocator* InServiceLocator, int32 InZoneID, int32 InRegionID)
{
    if (!InServiceLocator)
    {
        UE_LOG(LogMaterialServiceProvider, Error, TEXT("Cannot register services: Service locator is null"));
        return false;
    }
    
    // Store the service locator for later use
    ServiceLocator = InServiceLocator;
    
    // Initialize with the material registry if not already done
    if (!bInitialized)
    {
        InitializeWithRegistry();
    }
    
    // Register all services with the locator
    FScopeLock Lock(&ServiceLock);
    
    bool bSuccess = true;
    
    // Register material property services
    for (const auto& ServicePair : MaterialPropertyServices)
    {
        uint32 MaterialTypeId = ServicePair.Key;
        TSharedPtr<IMaterialPropertyService> PropertyService = ServicePair.Value;
        
        if (!ServiceLocator->RegisterService(PropertyService.Get(), InZoneID, InRegionID))
        {
            UE_LOG(LogMaterialServiceProvider, Warning, TEXT("Failed to register material property service for type %u"), MaterialTypeId);
            bSuccess = false;
        }
    }
    
    // Register material field operators
    for (const auto& OperatorPair : MaterialFieldOperators)
    {
        // Extract material type and channel from the key
        uint32 MaterialTypeId = static_cast<uint32>(OperatorPair.Key & 0xFFFFFFFF);
        int32 ChannelId = static_cast<int32>((OperatorPair.Key >> 32) & 0xFFFFFFFF);
        TSharedPtr<IMaterialFieldOperator> FieldOperator = OperatorPair.Value;
        
        if (!ServiceLocator->RegisterService(FieldOperator.Get(), InZoneID, InRegionID))
        {
            UE_LOG(LogMaterialServiceProvider, Warning, TEXT("Failed to register material field operator for type %u, channel %d"), 
                MaterialTypeId, ChannelId);
            bSuccess = false;
        }
    }
    
    // Register material interaction services
    for (const auto& InteractionPair : MaterialInteractionServices)
    {
        // Extract source and target material types from the key
        uint32 SourceMaterialId = static_cast<uint32>(InteractionPair.Key & 0xFFFFFFFF);
        uint32 TargetMaterialId = static_cast<uint32>((InteractionPair.Key >> 32) & 0xFFFFFFFF);
        TSharedPtr<IMaterialInteractionService> InteractionService = InteractionPair.Value;
        
        if (!ServiceLocator->RegisterService(InteractionService.Get(), InZoneID, InRegionID))
        {
            UE_LOG(LogMaterialServiceProvider, Warning, TEXT("Failed to register material interaction service for source %u, target %u"), 
                SourceMaterialId, TargetMaterialId);
            bSuccess = false;
        }
    }
    
    UE_LOG(LogMaterialServiceProvider, Log, TEXT("Registered material services with service locator (Region: %d, Zone: %d)"), 
        InRegionID, InZoneID);
    
    return bSuccess;
}

bool UMaterialServiceProvider::UnregisterServices(IServiceLocator* InServiceLocator, int32 InZoneID, int32 InRegionID)
{
    if (!InServiceLocator || !bInitialized)
    {
        UE_LOG(LogMaterialServiceProvider, Error, TEXT("Cannot unregister services: Service locator is null or provider not initialized"));
        return false;
    }
    
    FScopeLock Lock(&ServiceLock);
    
    bool bSuccess = true;
    
    // Unregister material property services
    for (const auto& ServicePair : MaterialPropertyServices)
    {
        uint32 MaterialTypeId = ServicePair.Key;
        const TSubclassOf<UInterface> PropertyServiceClass = UMaterialPropertyService::StaticClass();
        
        if (!InServiceLocator->UnregisterService(PropertyServiceClass, InZoneID, InRegionID))
        {
            UE_LOG(LogMaterialServiceProvider, Warning, TEXT("Failed to unregister material property service for type %u"), MaterialTypeId);
            bSuccess = false;
        }
    }
    
    // Unregister material field operators
    for (const auto& OperatorPair : MaterialFieldOperators)
    {
        // Extract material type and channel from the key
        uint32 MaterialTypeId = static_cast<uint32>(OperatorPair.Key & 0xFFFFFFFF);
        int32 ChannelId = static_cast<int32>((OperatorPair.Key >> 32) & 0xFFFFFFFF);
        const TSubclassOf<UInterface> FieldOperatorClass = UMaterialFieldOperator::StaticClass();
        
        if (!InServiceLocator->UnregisterService(FieldOperatorClass, InZoneID, InRegionID))
        {
            UE_LOG(LogMaterialServiceProvider, Warning, TEXT("Failed to unregister material field operator for type %u, channel %d"), 
                MaterialTypeId, ChannelId);
            bSuccess = false;
        }
    }
    
    // Unregister material interaction services
    for (const auto& InteractionPair : MaterialInteractionServices)
    {
        // Extract source and target material types from the key
        uint32 SourceMaterialId = static_cast<uint32>(InteractionPair.Key & 0xFFFFFFFF);
        uint32 TargetMaterialId = static_cast<uint32>((InteractionPair.Key >> 32) & 0xFFFFFFFF);
        const TSubclassOf<UInterface> InteractionServiceClass = UMaterialInteractionService::StaticClass();
        
        if (!InServiceLocator->UnregisterService(InteractionServiceClass, InZoneID, InRegionID))
        {
            UE_LOG(LogMaterialServiceProvider, Warning, TEXT("Failed to unregister material interaction service for source %u, target %u"), 
                SourceMaterialId, TargetMaterialId);
            bSuccess = false;
        }
    }
    
    UE_LOG(LogMaterialServiceProvider, Log, TEXT("Unregistered material services from service locator (Region: %d, Zone: %d)"), 
        InRegionID, InZoneID);
    
    return bSuccess;
}

bool UMaterialServiceProvider::InitializeServices()
{
    if (bInitialized)
    {
        UE_LOG(LogMaterialServiceProvider, Warning, TEXT("Services already initialized"));
        return true;
    }
    
    // Initialize with registry
    InitializeWithRegistry();
    
    // Set initialized flag
    bInitialized = true;
    
    // Update health status
    ServiceHealth.Status = static_cast<FServiceHealth::EStatus>(EServiceHealthStatus::Healthy);
    ServiceHealth.DiagnosticMessage = TEXT("Service initialized successfully");
    ServiceHealth.ErrorCount = 0;
    ServiceHealth.WarningCount = 0;
    
    UE_LOG(LogMaterialServiceProvider, Log, TEXT("Material services initialized successfully"));
    
    return true;
}

void UMaterialServiceProvider::ShutdownServices()
{
    if (!bInitialized)
    {
        return;
    }
    
    FScopeLock Lock(&ServiceLock);
    
    // Clear all service maps
    MaterialPropertyServices.Empty();
    MaterialFieldOperators.Empty();
    MaterialInteractionServices.Empty();
    PropertyDependencies.Empty();
    DependentMaterialMap.Empty();
    
    // Update status
    bInitialized = false;
    ServiceHealth.Status = static_cast<FServiceHealth::EStatus>(EServiceHealthStatus::Unknown);
    ServiceHealth.DiagnosticMessage = TEXT("Service shut down");
    ServiceHealth.ErrorCount = 0;
    ServiceHealth.WarningCount = 0;
    
    UE_LOG(LogMaterialServiceProvider, Log, TEXT("Material services shut down"));
}

FName UMaterialServiceProvider::GetProviderName() const
{
    return FName(TEXT("MaterialServiceProvider"));
}

TArray<FServiceDependency> UMaterialServiceProvider::GetServiceDependencies() const
{
    return ServiceDependencies;
}

bool UMaterialServiceProvider::HandleLifecyclePhase(EServiceLifecyclePhase Phase)
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

EServiceScope UMaterialServiceProvider::GetServiceScope() const
{
    return EServiceScope::Global;
}

FServiceHealth UMaterialServiceProvider::GetServiceHealth() const
{
    return ServiceHealth;
}

bool UMaterialServiceProvider::RecoverServices()
{
    if (!bInitialized)
    {
        return InitializeServices();
    }
    
    FScopeLock Lock(&ServiceLock);
    
    // Check each service type and recover if needed
    bool bAllHealthy = true;
    
    // Check property services
    for (auto& PropertyServicePair : MaterialPropertyServices)
    {
        if (!PropertyServicePair.Value.IsValid())
        {
            UE_LOG(LogMaterialServiceProvider, Warning, TEXT("Attempting to recover invalid property service"));
            // Attempt recovery (implementation depends on specific service)
            bAllHealthy = false;
        }
    }
    
    // Check field operators
    for (auto& OperatorPair : MaterialFieldOperators)
    {
        if (!OperatorPair.Value.IsValid())
        {
            UE_LOG(LogMaterialServiceProvider, Warning, TEXT("Attempting to recover invalid field operator"));
            // Attempt recovery
            bAllHealthy = false;
        }
    }
    
    // Check interaction services
    for (auto& InteractionPair : MaterialInteractionServices)
    {
        if (!InteractionPair.Value.IsValid())
        {
            UE_LOG(LogMaterialServiceProvider, Warning, TEXT("Attempting to recover invalid interaction service"));
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
        ServiceHealth.WarningCount = 0;
    }
    else
    {
        ServiceHealth.Status = static_cast<FServiceHealth::EStatus>(EServiceHealthStatus::Degraded);
        ServiceHealth.DiagnosticMessage = TEXT("Degraded");
        ServiceHealth.ErrorCount++;
        ServiceHealth.WarningCount++;
    }
    
    UE_LOG(LogMaterialServiceProvider, Log, TEXT("Material services health check completed. Status: %s"), *ServiceHealth.DiagnosticMessage);
    
    return bAllHealthy;
}

FServiceConfig UMaterialServiceProvider::GetServiceConfig() const
{
    return ServiceConfig;
}

bool UMaterialServiceProvider::UpdateServiceConfig(const FServiceConfig& InConfig)
{
    FScopeLock Lock(&ServiceLock);
    
    // Update configuration
    ServiceConfig = InConfig;
    
    UE_LOG(LogMaterialServiceProvider, Log, TEXT("Updated material service configuration"));
    
    return true;
}

bool UMaterialServiceProvider::ValidateServiceDependencies(IServiceLocator* InServiceLocator, TArray<FServiceDependency>& OutMissingDependencies)
{
    if (!InServiceLocator)
    {
        UE_LOG(LogMaterialServiceProvider, Error, TEXT("Cannot validate dependencies: Service locator is null"));
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

TArray<TSubclassOf<UInterface>> UMaterialServiceProvider::GetDependentServices(IServiceLocator* InServiceLocator)
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

bool UMaterialServiceProvider::RegisterMaterialPropertyService(uint32 InMaterialTypeId, TSharedPtr<IMaterialPropertyService> InPropertyService)
{
    if (!bInitialized || !InPropertyService.IsValid())
    {
        UE_LOG(LogMaterialServiceProvider, Error, TEXT("Cannot register material property service: Provider not initialized or service is invalid"));
        return false;
    }
    
    FScopeLock Lock(&ServiceLock);
    
    // Register in the map
    MaterialPropertyServices.Add(InMaterialTypeId, InPropertyService);
    
    UE_LOG(LogMaterialServiceProvider, Verbose, TEXT("Registered material property service for type %u"), InMaterialTypeId);
    
    // If we have a service locator, register with it too
    if (ServiceLocator)
    {
        // Use .Get() to get the raw pointer
        ServiceLocator->RegisterService(InPropertyService.Get(), INDEX_NONE, INDEX_NONE);
    }
    
    return true;
}

TSharedPtr<IMaterialPropertyService> UMaterialServiceProvider::ResolveMaterialPropertyService(uint32 InMaterialTypeId)
{
    if (!bInitialized)
    {
        UE_LOG(LogMaterialServiceProvider, Error, TEXT("Cannot resolve material property service: Provider not initialized"));
        return nullptr;
    }
    
    FScopeLock Lock(&ServiceLock);
    
    // Look up in the map
    TSharedPtr<IMaterialPropertyService>* ServicePtr = MaterialPropertyServices.Find(InMaterialTypeId);
    if (ServicePtr && ServicePtr->IsValid())
    {
        return *ServicePtr;
    }
    
    // If not found and we have a material registry, try to find based on inheritance
    if (MaterialRegistry.IsValid())
    {
        // Check if this material inherits from another material type
        TSharedPtr<FMaterialRegistry> PinnedRegistry = MaterialRegistry.Pin();
        TArray<FMaterialTypeInfo> DerivedTypes = PinnedRegistry->GetDerivedMaterialTypes(InMaterialTypeId);
        
        if (DerivedTypes.Num() > 0)
        {
            // This is the base material for some derived types
            // Try to find a service for the derived types (not ideal, but might work in some cases)
            
            for (const FMaterialTypeInfo& TypeInfo : DerivedTypes)
            {
                TSharedPtr<IMaterialPropertyService>* DerivedServicePtr = MaterialPropertyServices.Find(TypeInfo.TypeId);
                if (DerivedServicePtr && DerivedServicePtr->IsValid())
                {
                    UE_LOG(LogMaterialServiceProvider, Verbose, TEXT("Resolving material property service for type %u from derived type %u"), 
                        InMaterialTypeId, TypeInfo.TypeId);
                    return *DerivedServicePtr;
                }
            }
        }
        
        // Try to find service for parent material if this is a derived material
        // This is a simplification - actual implementation would need to traverse
        // the material inheritance hierarchy
    }
    
    // If not found, log the miss
    UE_LOG(LogMaterialServiceProvider, Verbose, TEXT("Material property service cache miss for type %u"), InMaterialTypeId);
    
    return nullptr;
}

bool UMaterialServiceProvider::RegisterMaterialFieldOperator(uint32 InMaterialTypeId, int32 InChannelId, TSharedPtr<IMaterialFieldOperator> InFieldOperator)
{
    if (!InFieldOperator)
    {
        UE_LOG(LogMaterialServiceProvider, Error, TEXT("Cannot register null field operator"));
        return false;
    }
    
    FScopeLock Lock(&ServiceLock);
    
    // Generate key for material type + channel
    uint64 Key = GenerateMaterialChannelKey(InMaterialTypeId, InChannelId);
    
    // Register with service map
    MaterialFieldOperators.Add(Key, InFieldOperator);
    
    // Register with service locator if available
    if (ServiceLocator)
    {
        if (!ServiceLocator->RegisterService(InFieldOperator.Get(), INDEX_NONE, INDEX_NONE))
        {
            UE_LOG(LogMaterialServiceProvider, Warning, TEXT("Failed to register field operator with service locator"));
            return false;
        }
    }
    
    UE_LOG(LogMaterialServiceProvider, Log, TEXT("Registered field operator for material type %u, channel %d"), InMaterialTypeId, InChannelId);
    
    return true;
}

TSharedPtr<IMaterialFieldOperator> UMaterialServiceProvider::ResolveMaterialFieldOperator(uint32 InMaterialTypeId, int32 InChannelId)
{
    if (!bInitialized)
    {
        UE_LOG(LogMaterialServiceProvider, Error, TEXT("Cannot resolve material field operator: Provider not initialized"));
        return nullptr;
    }
    
    FScopeLock Lock(&ServiceLock);
    
    // Generate key for lookup
    uint64 Key = GenerateMaterialChannelKey(InMaterialTypeId, InChannelId);
    
    // Look up in the map
    TSharedPtr<IMaterialFieldOperator>* OperatorPtr = MaterialFieldOperators.Find(Key);
    if (OperatorPtr && OperatorPtr->IsValid())
    {
        return *OperatorPtr;
    }
    
    // If not found, log the miss
    UE_LOG(LogMaterialServiceProvider, Verbose, TEXT("Material field operator cache miss for type %u, channel %d"), 
        InMaterialTypeId, InChannelId);
    
    return nullptr;
}

bool UMaterialServiceProvider::RegisterMaterialInteractionService(uint32 InSourceMaterialId, uint32 InTargetMaterialId, TSharedPtr<IMaterialInteractionService> InInteractionService)
{
    if (!InInteractionService)
    {
        UE_LOG(LogMaterialServiceProvider, Error, TEXT("Cannot register null interaction service"));
        return false;
    }
    
    FScopeLock Lock(&ServiceLock);
    
    // Generate key for source + target materials
    uint64 Key = GenerateMaterialInteractionKey(InSourceMaterialId, InTargetMaterialId);
    
    // Register with service map
    MaterialInteractionServices.Add(Key, InInteractionService);
    
    // Register with service locator if available
    if (ServiceLocator)
    {
        if (!ServiceLocator->RegisterService(InInteractionService.Get(), INDEX_NONE, INDEX_NONE))
        {
            UE_LOG(LogMaterialServiceProvider, Warning, TEXT("Failed to register interaction service with service locator"));
            return false;
        }
    }
    
    UE_LOG(LogMaterialServiceProvider, Log, TEXT("Registered interaction service for source material %u, target material %u"), 
        InSourceMaterialId, InTargetMaterialId);
    
    return true;
}

TSharedPtr<IMaterialInteractionService> UMaterialServiceProvider::ResolveMaterialInteractionService(uint32 InSourceMaterialId, uint32 InTargetMaterialId)
{
    if (!bInitialized)
    {
        UE_LOG(LogMaterialServiceProvider, Error, TEXT("Cannot resolve material interaction service: Provider not initialized"));
        return nullptr;
    }
    
    FScopeLock Lock(&ServiceLock);
    
    // Generate key for lookup
    uint64 Key = GenerateMaterialInteractionKey(InSourceMaterialId, InTargetMaterialId);
    
    // Look up in the map
    TSharedPtr<IMaterialInteractionService>* ServicePtr = MaterialInteractionServices.Find(Key);
    if (ServicePtr && ServicePtr->IsValid())
    {
        return *ServicePtr;
    }
    
    // If not found, check if there's a reverse interaction service
    // (some interactions are bidirectional)
    uint64 ReverseKey = GenerateMaterialInteractionKey(InTargetMaterialId, InSourceMaterialId);
    TSharedPtr<IMaterialInteractionService>* ReverseServicePtr = MaterialInteractionServices.Find(ReverseKey);
    if (ReverseServicePtr && ReverseServicePtr->IsValid())
    {
        UE_LOG(LogMaterialServiceProvider, Verbose, TEXT("Resolved reverse material interaction service for source %u, target %u"), 
            InSourceMaterialId, InTargetMaterialId);
        return *ReverseServicePtr;
    }
    
    // If not found, log the miss
    UE_LOG(LogMaterialServiceProvider, Verbose, TEXT("Material interaction service cache miss for source %u, target %u"), 
        InSourceMaterialId, InTargetMaterialId);
    
    return nullptr;
}

TSharedPtr<UObject> UMaterialServiceProvider::CoordinateCrossMaterialOperation(const TArray<uint32>& InMaterialIds, TSubclassOf<UInterface> InServiceType)
{
    if (!bInitialized || InMaterialIds.Num() < 2)
    {
        UE_LOG(LogMaterialServiceProvider, Error, TEXT("Cannot coordinate cross-material operation: Provider not initialized or insufficient materials"));
        return nullptr;
    }
    
    // For operations involving multiple materials, we need to find or create
    // a service that can coordinate across all the materials
    
    // This is a simplified implementation - in a real system you'd need to create
    // a specialized service for cross-material operations
    
    // For now, just log the coordination and return the first service found
    uint32 PrimaryMaterialId = InMaterialIds[0];
    
    UE_LOG(LogMaterialServiceProvider, Verbose, TEXT("Coordinating cross-material operation for %d materials, primary material %u"), 
        InMaterialIds.Num(), PrimaryMaterialId);
    
    // Try to resolve a service for the primary material
    if (ServiceLocator)
    {
        void* RawService = ServiceLocator->ResolveService(InServiceType, INDEX_NONE, INDEX_NONE);
        
        // Create a proper shared pointer if we got a valid service
        if (RawService)
        {
            return TSharedPtr<UObject>(static_cast<UObject*>(RawService));
        }
    }
    
    return nullptr;
}

TSharedPtr<UObject> UMaterialServiceProvider::ResolveChannelAwareService(uint32 InMaterialTypeId, int32 InChannelId, TSubclassOf<UInterface> InServiceType)
{
    FScopeLock Lock(&ServiceLock);
    
    // Since we can't directly use IMaterialFieldOperator::StaticClass() with forward declarations,
    // check based on channel-related logic
    
    // First check if we have a field operator for this type and channel
    TSharedPtr<IMaterialFieldOperator> FieldOperator = ResolveMaterialFieldOperator(InMaterialTypeId, InChannelId);
    if (FieldOperator.IsValid())
    {
        // Create a new shared pointer to UObject using the raw pointer
        // and an empty deleter since we don't own the object
        UObject* RawFieldOperator = static_cast<UObject*>(static_cast<void*>(FieldOperator.Get()));
        return TSharedPtr<UObject>(RawFieldOperator, [](UObject*) {});
    }
    
    // Check if service locator can resolve this
    if (ServiceLocator)
    {
        void* ServiceInstance = ServiceLocator->ResolveService(InServiceType, INDEX_NONE, INDEX_NONE);
        if (ServiceInstance)
        {
            // Convert void* to UObject* with proper explicit cast
            UObject* ObjectPtr = static_cast<UObject*>(ServiceInstance);
            // Create shared pointer with empty deleter since we don't own the object
            return TSharedPtr<UObject>(ObjectPtr, [](UObject*) {});
        }
    }
    
    return nullptr;
}

bool UMaterialServiceProvider::TrackMaterialPropertyDependency(uint32 InDependentMaterialId, const FName& InDependentPropertyName, uint32 InSourceMaterialId, const FName& InSourcePropertyName)
{
    if (!bInitialized)
    {
        UE_LOG(LogMaterialServiceProvider, Error, TEXT("Cannot track material property dependency: Provider not initialized"));
        return false;
    }
    
    FScopeLock Lock(&ServiceLock);
    
    // Create dependency info
    FMaterialPropertyDependency Dependency;
    Dependency.SourceMaterialId = InSourceMaterialId;
    Dependency.SourcePropertyName = InSourcePropertyName;
    Dependency.TargetMaterialId = InDependentMaterialId;
    Dependency.TargetPropertyName = InDependentPropertyName;
    Dependency.InfluenceFactor = 1.0f;
    Dependency.bIsRequired = true;
    
    // Ensure property map for this material exists
    if (!PropertyDependencies.Contains(InDependentMaterialId))
    {
        PropertyDependencies.Add(InDependentMaterialId, TMap<FName, TArray<FMaterialPropertyDependency>>());
    }
    
    // Ensure property array for this property exists
    if (!PropertyDependencies[InDependentMaterialId].Contains(InDependentPropertyName))
    {
        PropertyDependencies[InDependentMaterialId].Add(InDependentPropertyName, TArray<FMaterialPropertyDependency>());
    }
    
    // Add dependency
    PropertyDependencies[InDependentMaterialId][InDependentPropertyName].Add(Dependency);
    
    // Update dependent material map for faster lookups
    if (!DependentMaterialMap.Contains(InSourceMaterialId))
    {
        DependentMaterialMap.Add(InSourceMaterialId, TArray<uint32>());
    }
    if (!DependentMaterialMap[InSourceMaterialId].Contains(InDependentMaterialId))
    {
        DependentMaterialMap[InSourceMaterialId].Add(InDependentMaterialId);
    }
    
    UE_LOG(LogMaterialServiceProvider, Verbose, TEXT("Tracked material property dependency: Material %u, Property '%s' depends on Material %u, Property '%s'"), 
        InDependentMaterialId, *InDependentPropertyName.ToString(), InSourceMaterialId, *InSourcePropertyName.ToString());
    
    return true;
}

TArray<FMaterialPropertyDependency> UMaterialServiceProvider::GetMaterialPropertyDependencies(uint32 InMaterialTypeId, const FName& InPropertyName) const
{
    TArray<FMaterialPropertyDependency> Dependencies;
    
    if (!bInitialized)
    {
        return Dependencies;
    }
    
    FScopeLock Lock(&ServiceLock);
    
    // Check if this material has any property dependencies
    const TMap<FName, TArray<FMaterialPropertyDependency>>* PropertyMap = PropertyDependencies.Find(InMaterialTypeId);
    if (!PropertyMap)
    {
        return Dependencies;
    }
    
    // Check if this property has any dependencies
    const TArray<FMaterialPropertyDependency>* DependencyArray = PropertyMap->Find(InPropertyName);
    if (DependencyArray)
    {
        // Return a copy of the dependencies
        Dependencies = *DependencyArray;
    }
    
    return Dependencies;
}

TArray<uint32> UMaterialServiceProvider::GetDependentMaterialTypes(uint32 InMaterialTypeId) const
{
    FScopeLock Lock(&ServiceLock);
    
    TArray<uint32> DependentMaterials;
    
    // Get the pointer to the registry
    if (MaterialRegistry.IsValid())
    {
        TSharedPtr<FMaterialRegistry> RegistryPtr = MaterialRegistry.Pin();
        if (RegistryPtr.IsValid())
        {
            // Need to convert from the registry's type to uint32 array
            const TArray<FMaterialTypeInfo>& DerivedTypes = RegistryPtr->GetDerivedMaterialTypes(InMaterialTypeId);
            for (const FMaterialTypeInfo& TypeInfo : DerivedTypes)
            {
                DependentMaterials.Add(TypeInfo.TypeId);
            }
            return DependentMaterials;
        }
    }
    
    // Check direct dependencies
    const TArray<uint32>* FoundDependents = DependentMaterialMap.Find(InMaterialTypeId);
    if (FoundDependents)
    {
        DependentMaterials.Append(*FoundDependents);
    }
    
    return DependentMaterials;
}

bool UMaterialServiceProvider::UpdateAndPropagateMaterialProperty(uint32 InMaterialTypeId, const FName& InPropertyName, const FString& InPropertyValue)
{
    if (!bInitialized)
    {
        UE_LOG(LogMaterialServiceProvider, Error, TEXT("Cannot update material property: Provider not initialized"));
        return false;
    }
    
    // First, update the property value
    TSharedPtr<IMaterialPropertyService> PropertyService = ResolveMaterialPropertyService(InMaterialTypeId);
    if (!PropertyService.IsValid())
    {
        UE_LOG(LogMaterialServiceProvider, Error, TEXT("Cannot update material property: Property service not found for material %u"), 
            InMaterialTypeId);
        return false;
    }
    
    // This is a placeholder - in a real implementation, you'd use the property service
    // to update the property value
    // PropertyService->SetPropertyValue(InPropertyName, InPropertyValue);
    
    // Now propagate the change to dependent properties
    TSet<FName> VisitedProperties;
    return PropagateMaterialPropertyChange(InMaterialTypeId, InPropertyName, VisitedProperties);
}

void UMaterialServiceProvider::InitializeWithRegistry()
{
    // Get the material registry
    if (!MaterialRegistry.IsValid())
    {
        // Use a direct reference without copy construction
        FMaterialRegistry& RegistryRef = FMaterialRegistry::Get();
        MaterialRegistry = TSharedPtr<FMaterialRegistry>(&RegistryRef, [](FMaterialRegistry*) {});
    }
    
    // Initialize dependencies
    ServiceDependencies.Empty();
    
    // Add required dependencies
    FServiceDependency TypeRegistryDependency;
    TypeRegistryDependency.DependencyType = nullptr; // This would be the type registry interface
    TypeRegistryDependency.DependencyKind = EServiceDependencyType::Required;
    ServiceDependencies.Add(TypeRegistryDependency);
    
    // Get material type information
    if (MaterialRegistry.IsValid())
    {
        TSharedPtr<FMaterialRegistry> PinnedRegistry = MaterialRegistry.Pin();
        if (PinnedRegistry.IsValid())
        {
            // Initialize material property dependencies
            PropertyDependencies.Empty();
            
            // You could load dependency information from the registry here
            // Example: PinnedRegistry->GetAllMaterialTypeDependencies();
        }
    }
    
    // Initialize TypeRegistry
    if (ServiceLocator)
    {
        // Get the type registry from the service locator
        FTypeRegistry* RawRegistry = ServiceLocator->ResolveService<FTypeRegistry>();
        if (RawRegistry)
        {
            // Create a weak pointer to the type registry
            // Don't create a shared pointer with MakeShareable since we don't own it
            TSharedPtr<FTypeRegistry> SharedRegistry(RawRegistry, [](FTypeRegistry*) {});
            TypeRegistry = SharedRegistry;
        }
    }
}

uint64 UMaterialServiceProvider::GenerateMaterialChannelKey(uint32 InMaterialTypeId, int32 InChannelId) const
{
    // Combine material type and channel ID into a single key
    uint64 Key = static_cast<uint64>(static_cast<uint32>(InChannelId)) << 32;
    Key |= InMaterialTypeId;
    return Key;
}

uint64 UMaterialServiceProvider::GenerateMaterialInteractionKey(uint32 InSourceMaterialId, uint32 InTargetMaterialId) const
{
    // Combine source and target material types into a single key
    uint64 Key = static_cast<uint64>(InTargetMaterialId) << 32;
    Key |= InSourceMaterialId;
    return Key;
}

TArray<TSharedPtr<UObject>> UMaterialServiceProvider::ResolveMaterialServiceHierarchy(uint32 InMaterialTypeId, TSubclassOf<UInterface> InServiceType)
{
    TArray<TSharedPtr<UObject>> Services;
    
    if (!ServiceLocator || !InServiceType)
    {
        return Services;
    }
    
    // First add the service for the specified material type
    void* RawService = ServiceLocator->ResolveService(InServiceType, INDEX_NONE, INDEX_NONE);
    if (RawService)
    {
        Services.Add(TSharedPtr<UObject>(static_cast<UObject*>(RawService)));
    }
    
    // Then get all parent material types and resolve services for those too
    if (TypeRegistry.IsValid())
    {
        TSharedPtr<FTypeRegistry> PinnedRegistry = TypeRegistry.Pin();
        if (PinnedRegistry.IsValid())
        {
            TArray<uint32> ParentMaterialTypes;
            // Implement appropriate method to get parent types
            // Example: ParentMaterialTypes = PinnedRegistry->GetParentTypes(InMaterialTypeId);
            
            for (const uint32 ParentTypeId : ParentMaterialTypes)
            {
                void* ParentRawService = ServiceLocator->ResolveService(InServiceType, INDEX_NONE, INDEX_NONE);
                if (ParentRawService)
                {
                    Services.Add(TSharedPtr<UObject>(static_cast<UObject*>(ParentRawService)));
                }
            }
        }
    }
    
    return Services;
}

bool UMaterialServiceProvider::PropagateMaterialPropertyChange(uint32 InMaterialTypeId, const FName& InPropertyName, TSet<FName>& InVisitedProperties)
{
    if (!bInitialized)
    {
        return false;
    }
    
    // Get the dependent materials
    TArray<uint32> DependentMaterials = GetDependentMaterialTypes(InMaterialTypeId);
    if (DependentMaterials.Num() == 0)
    {
        // No dependent materials, nothing to propagate
        return true;
    }
    
    // Mark this property as visited to prevent cycles
    FString VisitKey = FString::Printf(TEXT("%u:%s"), InMaterialTypeId, *InPropertyName.ToString());
    if (InVisitedProperties.Contains(*VisitKey))
    {
        // Already visited, skip to prevent infinite recursion
        return true;
    }
    InVisitedProperties.Add(*VisitKey);
    
    bool bSuccess = true;
    
    // Resolve the property service for the source material
    TSharedPtr<IMaterialPropertyService> SourcePropertyService = ResolveMaterialPropertyService(InMaterialTypeId);
    if (!SourcePropertyService.IsValid())
    {
        UE_LOG(LogMaterialServiceProvider, Warning, TEXT("Cannot propagate property change: Source property service not found for material %u"), 
            InMaterialTypeId);
        return false;
    }
    
    // Get the property value from the source material
    // This is a placeholder - in a real implementation, you'd use the property service
    FString PropertyValue = TEXT(""); // SourcePropertyService->GetPropertyValue(InPropertyName);
    
    // For each dependent material, find properties that depend on this property
    for (uint32 DependentMaterialId : DependentMaterials)
    {
        // Check if this material has any property dependencies
        const TMap<FName, TArray<FMaterialPropertyDependency>>* PropertyMap = PropertyDependencies.Find(DependentMaterialId);
        if (!PropertyMap)
        {
            continue;
        }
        
        // Check each property in the dependent material
        for (const auto& PropertyPair : *PropertyMap)
        {
            const FName& DependentPropertyName = PropertyPair.Key;
            const TArray<FMaterialPropertyDependency>& Dependencies = PropertyPair.Value;
            
            // Check if any of the dependencies reference the changed property
            bool bShouldUpdate = false;
            for (const FMaterialPropertyDependency& Dependency : Dependencies)
            {
                if (Dependency.SourceMaterialId == InMaterialTypeId && Dependency.SourcePropertyName == InPropertyName)
                {
                    // Apply the influence factor from the dependency if available
                    // And check if this dependency is required
                    if (Dependency.bIsRequired)
                    {
                        bShouldUpdate = true;
                        break;
                    }
                }
            }
            
            if (bShouldUpdate)
            {
                // Update the dependent property
                TSharedPtr<IMaterialPropertyService> DependentPropertyService = ResolveMaterialPropertyService(DependentMaterialId);
                if (DependentPropertyService.IsValid())
                {
                    // This is a placeholder - in a real implementation, you'd use the property service
                    // to update the property value, possibly using a transform function
                    // DependentPropertyService->SetPropertyValue(DependentPropertyName, PropertyValue);
                    
                    UE_LOG(LogMaterialServiceProvider, Verbose, TEXT("Propagated property change: Material %u, Property '%s' from Material %u, Property '%s'"), 
                        DependentMaterialId, *DependentPropertyName.ToString(), InMaterialTypeId, *InPropertyName.ToString());
                    
                    // Recursively propagate the change to properties that depend on this one
                    if (!PropagateMaterialPropertyChange(DependentMaterialId, DependentPropertyName, InVisitedProperties))
                    {
                        bSuccess = false;
                    }
                }
                else
                {
                    UE_LOG(LogMaterialServiceProvider, Warning, TEXT("Cannot propagate property change: Dependent property service not found for material %u"), 
                        DependentMaterialId);
                    bSuccess = false;
                }
            }
        }
    }
    
    return bSuccess;
}
