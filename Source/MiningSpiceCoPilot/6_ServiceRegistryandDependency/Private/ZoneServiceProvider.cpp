// Copyright Epic Games, Inc. All Rights Reserved.

#include "ZoneServiceProvider.h"
#include "../../1_CoreRegistry/Public/ZoneTypeRegistry.h"
#include "../../1_CoreRegistry/Public/Interfaces/IServiceLocator.h"
#include "../../3_ThreadingTaskSystem/Public/Interfaces/ITransactionManager.h"
#include "../../1_CoreRegistry/Public/Interfaces/ITransactionService.h"
#include "../../1_CoreRegistry/Public/Interfaces/IZoneManager.h"
#include "Logging/LogMacros.h"
#include "Misc/ScopeLock.h"
#include "../Public/DependencyResolver.h"

// Forward declarations
class UObject;

// Define log category for this class
DEFINE_LOG_CATEGORY_STATIC(LogZoneServiceProvider, Log, All);

UZoneServiceProvider::UZoneServiceProvider()
    : ServiceLocator(nullptr)
    , ZoneGridConfigName(NAME_None)
    , bInitialized(false)
{
    // Initialize service configuration with default values
    ServiceConfig.SetValue(TEXT("EnableCaching"), TEXT("true"));
    ServiceConfig.SetValue(TEXT("CacheTimeoutSeconds"), TEXT("60"));
    ServiceConfig.SetValue(TEXT("MaxCachedItemsPerType"), TEXT("100"));
    
    // Initialize service health
    ServiceHealth.Status = static_cast<FServiceHealth::EStatus>(EServiceHealthStatus::Unknown);
    ServiceHealth.DiagnosticMessage = TEXT("Not initialized");
    ServiceHealth.ErrorCount = 0;
    ServiceHealth.WarningCount = 0;
    
    // Initialize critical transaction types for fast path optimization
    CriticalTransactionTypes.Empty();
}

UZoneServiceProvider::~UZoneServiceProvider()
{
    if (bInitialized)
    {
        ShutdownServices();
    }
}

TArray<TSubclassOf<UInterface>> UZoneServiceProvider::GetProvidedServices() const
{
    TArray<TSubclassOf<UInterface>> ProvidedServices;
    
    // Add all service interfaces provided by this provider
    // These should be interfaces defined by the Zone system
    // e.g., ProvidedServices.Add(UTransactionService::StaticClass());
    
    return ProvidedServices;
}

bool UZoneServiceProvider::RegisterServices(IServiceLocator* InServiceLocator, int32 InZoneID, int32 InRegionID)
{
    if (!InServiceLocator)
    {
        UE_LOG(LogZoneServiceProvider, Error, TEXT("Cannot register services: Service locator is null"));
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
    
    // Register zone transaction services
    for (const auto& ServicePair : ZoneTransactionServices)
    {
        // Extract zone ID from the key
        int32 ZoneID = static_cast<int32>(ServicePair.Key & 0xFFFFFFFF);
        
        // Only register services for the specified zone if a zone ID was provided
        if (InZoneID != INDEX_NONE && ZoneID != InZoneID)
        {
            continue;
        }
        
        TSharedPtr<ITransactionService> TransactionService = ServicePair.Value;
        if (!ServiceLocator->RegisterService(TransactionService.Get(), ZoneID, InRegionID))
        {
            UE_LOG(LogZoneServiceProvider, Warning, TEXT("Failed to register zone transaction service for zone %d"), ZoneID);
            bSuccess = false;
        }
    }
    
    // Register zone managers
    for (const auto& ManagerPair : ZoneManagers)
    {
        int32 RegionID = ManagerPair.Key;
        
        // Only register services for the specified region if a region ID was provided
        if (InRegionID != INDEX_NONE && RegionID != InRegionID)
        {
            continue;
        }
        
        TSharedPtr<IZoneManager> ZoneManager = ManagerPair.Value;
        if (!ServiceLocator->RegisterService(ZoneManager.Get(), INDEX_NONE, RegionID))
        {
            UE_LOG(LogZoneServiceProvider, Warning, TEXT("Failed to register zone manager for region %d"), RegionID);
            bSuccess = false;
        }
    }
    
    UE_LOG(LogZoneServiceProvider, Log, TEXT("Registered zone services with service locator (Region: %d, Zone: %d)"), 
        InRegionID, InZoneID);
    
    return bSuccess;
}

bool UZoneServiceProvider::UnregisterServices(IServiceLocator* InServiceLocator, int32 InZoneID, int32 InRegionID)
{
    if (!InServiceLocator || !bInitialized)
    {
        UE_LOG(LogZoneServiceProvider, Error, TEXT("Cannot unregister services: Service locator is null or provider not initialized"));
        return false;
    }
    
    FScopeLock Lock(&ServiceLock);
    
    bool bSuccess = true;
    
    // Unregister zone transaction services
    for (const auto& ServicePair : ZoneTransactionServices)
    {
        // Extract zone ID from the key
        int32 ZoneID = static_cast<int32>(ServicePair.Key & 0xFFFFFFFF);
        
        // Only unregister services for the specified zone if a zone ID was provided
        if (InZoneID != INDEX_NONE && ZoneID != InZoneID)
        {
            continue;
        }
        
        const TSubclassOf<UInterface> TransactionServiceClass = UTransactionService::StaticClass();
        if (!InServiceLocator->UnregisterService(TransactionServiceClass, ZoneID, InRegionID))
        {
            UE_LOG(LogZoneServiceProvider, Warning, TEXT("Failed to unregister zone transaction service for zone %d"), ZoneID);
            bSuccess = false;
        }
    }
    
    // Unregister zone managers
    for (const auto& ManagerPair : ZoneManagers)
    {
        int32 RegionID = ManagerPair.Key;
        
        // Only unregister services for the specified region if a region ID was provided
        if (InRegionID != INDEX_NONE && RegionID != InRegionID)
        {
            continue;
        }
        
        const TSubclassOf<UInterface> ZoneManagerClass = UZoneManager::StaticClass();
        if (!InServiceLocator->UnregisterService(ZoneManagerClass, INDEX_NONE, RegionID))
        {
            UE_LOG(LogZoneServiceProvider, Warning, TEXT("Failed to unregister zone manager for region %d"), RegionID);
            bSuccess = false;
        }
    }
    
    UE_LOG(LogZoneServiceProvider, Log, TEXT("Unregistered zone services from service locator (Region: %d, Zone: %d)"), 
        InRegionID, InZoneID);
    
    return bSuccess;
}

bool UZoneServiceProvider::InitializeServices()
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
    
    UE_LOG(LogZoneServiceProvider, Log, TEXT("Zone service provider initialized"));
    
    return true;
}

void UZoneServiceProvider::ShutdownServices()
{
    if (!bInitialized)
    {
        return;
    }
    
    FScopeLock Lock(&ServiceLock);
    
    // Clear all service maps
    ZoneTransactionServices.Empty();
    ZoneManagers.Empty();
    FastPathThresholds.Empty();
    ConflictRates.Empty();
    
    // Update status
    bInitialized = false;
    ServiceHealth.Status = static_cast<FServiceHealth::EStatus>(EServiceHealthStatus::Unknown);
    ServiceHealth.DiagnosticMessage = TEXT("Service shut down");
    ServiceHealth.ErrorCount = 0;
    ServiceHealth.WarningCount = 0;
    
    UE_LOG(LogZoneServiceProvider, Log, TEXT("Zone services shut down"));
}

FName UZoneServiceProvider::GetProviderName() const
{
    return FName(TEXT("ZoneServiceProvider"));
}

TArray<FServiceDependency> UZoneServiceProvider::GetServiceDependencies() const
{
    return ServiceDependencies;
}

bool UZoneServiceProvider::HandleLifecyclePhase(EServiceLifecyclePhase Phase)
{
    // Handle different lifecycle phases
    switch (Phase)
    {
        case EServiceLifecyclePhase::PreInitialize:
            // Set up pre-initialization state
            ServiceHealth.Status = static_cast<FServiceHealth::EStatus>(EServiceHealthStatus::Unknown);
            ServiceHealth.DiagnosticMessage = TEXT("Preparing for initialization");
            return true;
            
        case EServiceLifecyclePhase::Initialize:
            // Main initialization
            return InitializeServices();
            
        case EServiceLifecyclePhase::PostInitialize:
            // Additional setup after main initialization
            return true;
            
        case EServiceLifecyclePhase::PreShutdown:
            // Prepare for shutdown
            return true;
            
        case EServiceLifecyclePhase::Shutdown:
            // Main shutdown
            ShutdownServices();
            return true;
            
        case EServiceLifecyclePhase::PostShutdown:
            // Final cleanup
            return true;
            
        default:
            UE_LOG(LogZoneServiceProvider, Warning, TEXT("Unknown lifecycle phase: %d"), static_cast<int32>(Phase));
            return false;
    }
}

EServiceScope UZoneServiceProvider::GetServiceScope() const
{
    return EServiceScope::Zone;
}

FServiceHealth UZoneServiceProvider::GetServiceHealth() const
{
    return ServiceHealth;
}

bool UZoneServiceProvider::RecoverServices()
{
    FScopeLock Lock(&ServiceLock);
    
    if (ServiceHealth.Status == FServiceHealth::EStatus::Failed || 
        ServiceHealth.Status == FServiceHealth::EStatus::Unresponsive)
    {
        // More aggressive recovery for failed services
        bool bSuccess = InitializeServices();
        
        if (bSuccess)
        {
            ServiceHealth.Status = static_cast<FServiceHealth::EStatus>(EServiceHealthStatus::Healthy);
            ServiceHealth.DiagnosticMessage = TEXT("Service recovered from failure");
        }
        else
        {
            ServiceHealth.Status = static_cast<FServiceHealth::EStatus>(EServiceHealthStatus::Failed);
            ServiceHealth.DiagnosticMessage = TEXT("Service recovery failed");
        }
        
        return bSuccess;
    }
    else if (ServiceHealth.Status == FServiceHealth::EStatus::Degraded || 
             ServiceHealth.Status == FServiceHealth::EStatus::Critical)
    {
        // Lighter recovery for degraded services
        InitializeWithRegistry();
        
        ServiceHealth.Status = static_cast<FServiceHealth::EStatus>(EServiceHealthStatus::Healthy);
        ServiceHealth.DiagnosticMessage = TEXT("Service recovered from degraded state");
        
        return true;
    }
    
    return false;
}

FServiceConfig UZoneServiceProvider::GetServiceConfig() const
{
    return ServiceConfig;
}

bool UZoneServiceProvider::UpdateServiceConfig(const FServiceConfig& InConfig)
{
    FScopeLock Lock(&ServiceLock);
    
    // Update configuration
    ServiceConfig = InConfig;
    
    UE_LOG(LogZoneServiceProvider, Log, TEXT("Updated zone service configuration"));
    
    return true;
}

bool UZoneServiceProvider::ValidateServiceDependencies(IServiceLocator* InServiceLocator, TArray<FServiceDependency>& OutMissingDependencies)
{
    if (!InServiceLocator)
    {
        UE_LOG(LogZoneServiceProvider, Error, TEXT("Cannot validate dependencies: Service locator is null"));
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

TArray<TSubclassOf<UInterface>> UZoneServiceProvider::GetDependentServices(IServiceLocator* InServiceLocator)
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

bool UZoneServiceProvider::RegisterZoneTransactionService(int32 InZoneID, uint32 InTransactionType, TSharedPtr<ITransactionService> InService)
{
    if (!bInitialized || !InService.IsValid())
    {
        UE_LOG(LogZoneServiceProvider, Error, TEXT("Cannot register zone transaction service: Provider not initialized or service is invalid"));
        return false;
    }
    
    FScopeLock Lock(&ServiceLock);
    
    // Generate a unique key for this zone and transaction type
    uint64 Key = GenerateZoneServiceKey(InZoneID, InTransactionType);
    
    // Register in the map
    ZoneTransactionServices.Add(Key, InService);
    
    UE_LOG(LogZoneServiceProvider, Verbose, TEXT("Registered zone transaction service for zone %d, type %u"), InZoneID, InTransactionType);
    
    // If we have a service locator, register with it too
    if (ServiceLocator)
    {
        // Use .Get() to get the raw pointer
        ServiceLocator->RegisterService(InService.Get(), InZoneID, INDEX_NONE);
    }
    
    // Update fast path threshold for this transaction type
    UpdateFastPathThreshold(InZoneID, InTransactionType);
    
    return true;
}

TSharedPtr<ITransactionService> UZoneServiceProvider::ResolveZoneTransactionService(int32 InZoneID, uint32 InTransactionType)
{
    if (!bInitialized)
    {
        UE_LOG(LogZoneServiceProvider, Error, TEXT("Cannot resolve zone transaction service: Provider not initialized"));
        return nullptr;
    }
    
    FScopeLock Lock(&ServiceLock);
    
    // Generate key for lookup
    uint64 Key = GenerateZoneServiceKey(InZoneID, InTransactionType);
    
    // First check if this is a critical transaction type that might use the fast path
    if (CriticalTransactionTypes.Contains(InTransactionType))
    {
        // Use fast path resolution if available
        TSharedPtr<ITransactionService> FastPathService = FastPathResolve(InZoneID, InTransactionType);
        if (FastPathService.IsValid())
        {
            return FastPathService;
        }
    }
    
    // Look up in the map
    TSharedPtr<ITransactionService>* ServicePtr = ZoneTransactionServices.Find(Key);
    if (ServicePtr && ServicePtr->IsValid())
    {
        return *ServicePtr;
    }
    
    // If not found directly, check if there's a wildcard transaction service for this zone
    uint64 WildcardKey = GenerateZoneServiceKey(InZoneID, 0); // 0 is a wildcard transaction type
    ServicePtr = ZoneTransactionServices.Find(WildcardKey);
    if (ServicePtr && ServicePtr->IsValid())
    {
        return *ServicePtr;
    }
    
    // If service locator is available, try to resolve service through it
    if (ServiceLocator)
    {
        void* RawService = ServiceLocator->ResolveService(UTransactionService::StaticClass(), InZoneID, INDEX_NONE);
        if (RawService)
        {
            // Create shared pointer with empty deleter to avoid calling the protected destructor
            ITransactionService* TypedService = static_cast<ITransactionService*>(RawService);
            return TSharedPtr<ITransactionService>(TypedService, [](ITransactionService*) {});
        }
    }
    
    UE_LOG(LogZoneServiceProvider, Verbose, TEXT("Zone transaction service not found for zone %d, type %u"), 
        InZoneID, InTransactionType);
    
    return nullptr;
}

TSharedPtr<UObject> UZoneServiceProvider::ResolveSpatialService(const FVector& InWorldLocation, TSubclassOf<UInterface> InServiceType)
{
    if (!bInitialized)
    {
        UE_LOG(LogZoneServiceProvider, Error, TEXT("Cannot resolve spatial service: Provider not initialized"));
        return nullptr;
    }
    
    // Convert world location to zone ID
    int32 ZoneID = WorldLocationToZoneID(InWorldLocation);
    
    // Resolve service for this zone
    if (ServiceLocator)
    {
        // Get the raw pointer from the service locator
        void* RawService = ServiceLocator->ResolveService(InServiceType, ZoneID, INDEX_NONE);
        
        // Create a proper shared pointer if we got a valid service
        if (RawService)
        {
            UObject* ServiceObj = static_cast<UObject*>(RawService);
            return TSharedPtr<UObject>(ServiceObj, [](UObject*) {});  // Empty deleter since we don't own the object
        }
    }
    
    return nullptr;
}

TSharedPtr<ITransactionService> UZoneServiceProvider::FastPathResolve(int32 InZoneID, uint32 InTransactionType)
{
    if (!bInitialized)
    {
        UE_LOG(LogZoneServiceProvider, Error, TEXT("Cannot fast path resolve: Provider not initialized"));
        return nullptr;
    }
    
    // For critical path, minimize locking and checks
    uint64 Key = GenerateZoneServiceKey(InZoneID, InTransactionType);
    
    // Optimized lookup for critical paths - no locking for read-only access
    TSharedPtr<ITransactionService>* ServicePtr = ZoneTransactionServices.Find(Key);
    if (ServicePtr && ServicePtr->IsValid())
    {
        // Check if we should take fast path based on conflict rate threshold
        const float* ThresholdPtr = FastPathThresholds.Find(Key);
        const float* ConflictRatePtr = ConflictRates.Find(Key);
        
        if (ThresholdPtr && ConflictRatePtr)
        {
            if (*ConflictRatePtr < *ThresholdPtr)
            {
                // Fast path - known low conflict rate
                return *ServicePtr;
            }
            else
            {
                // Potential conflict zone, need full transaction service
                UE_LOG(LogZoneServiceProvider, Verbose, TEXT("Fast path rejected for zone %d, type %u (conflict rate: %.2f > threshold: %.2f)"), 
                    InZoneID, InTransactionType, *ConflictRatePtr, *ThresholdPtr);
            }
        }
        else
        {
            // No threshold or conflict rate info, take default path
            return *ServicePtr;
        }
    }
    
    // Cache miss or high conflict rate, fall back to normal resolution
    return ResolveZoneTransactionService(InZoneID, InTransactionType);
}

TSharedPtr<ITransactionService> UZoneServiceProvider::CoordinateCrossZoneTransaction(int32 InSourceZoneID, int32 InTargetZoneID, uint32 InTransactionType)
{
    if (!bInitialized)
    {
        UE_LOG(LogZoneServiceProvider, Error, TEXT("Cannot coordinate cross-zone transaction: Provider not initialized"));
        return nullptr;
    }
    
    // Get source and target services
    TSharedPtr<ITransactionService> SourceService = ResolveZoneTransactionService(InSourceZoneID, InTransactionType);
    TSharedPtr<ITransactionService> TargetService = ResolveZoneTransactionService(InTargetZoneID, InTransactionType);
    
    if (!SourceService.IsValid() || !TargetService.IsValid())
    {
        UE_LOG(LogZoneServiceProvider, Warning, TEXT("Cannot coordinate cross-zone transaction: Missing services"));
        return nullptr;
    }
    
    // This is a simplified implementation - in a real system you'd need to create
    // a special transaction service that coordinates across zones
    
    // For now, just return the source service and log the coordination
    UE_LOG(LogZoneServiceProvider, Verbose, TEXT("Coordinating cross-zone transaction between zones %d and %d for type %u"), 
        InSourceZoneID, InTargetZoneID, InTransactionType);
    
    return SourceService;
}

TArray<int32> UZoneServiceProvider::GetNeighboringZones(int32 InZoneID) const
{
    TArray<int32> Neighbors;
    
    if (!bInitialized)
    {
        UE_LOG(LogZoneServiceProvider, Error, TEXT("Cannot get neighboring zones: Provider not initialized"));
        return Neighbors;
    }
    
    // This is a simplified implementation - in a real system you'd need to query
    // the zone grid configuration to determine neighboring zones
    
    // Get zone configuration from registry
    if (TypeRegistry.IsValid() && !ZoneGridConfigName.IsNone())
    {
        const FZoneGridConfig* GridConfig = TypeRegistry.Pin()->GetZoneGridConfig(ZoneGridConfigName);
        if (GridConfig)
        {
            // Using grid config, we would calculate neighboring zones
            // For now, just add some placeholder neighbors based on ID
            
            // In a real 3D grid, you'd have up to 26 neighbors (3x3x3 cube - center)
            // This is just a simple approximation for illustration
            Neighbors.Add(InZoneID - 1);  // "Left"
            Neighbors.Add(InZoneID + 1);  // "Right"
            Neighbors.Add(InZoneID - 100); // "Front"
            Neighbors.Add(InZoneID + 100); // "Back"
            Neighbors.Add(InZoneID - 10000); // "Down"
            Neighbors.Add(InZoneID + 10000); // "Up"
            
            // Diagonals would be more complex combinations
        }
    }
    
    return Neighbors;
}

int32 UZoneServiceProvider::WorldLocationToZoneID(const FVector& InWorldLocation) const
{
    if (!bInitialized || !TypeRegistry.IsValid() || ZoneGridConfigName.IsNone())
    {
        UE_LOG(LogZoneServiceProvider, Error, TEXT("Cannot convert world location to zone ID: Provider not initialized or missing configuration"));
        return INDEX_NONE;
    }
    
    // Get zone configuration from registry
    const FZoneGridConfig* GridConfig = TypeRegistry.Pin()->GetZoneGridConfig(ZoneGridConfigName);
    if (!GridConfig)
    {
        UE_LOG(LogZoneServiceProvider, Error, TEXT("Cannot convert world location to zone ID: Missing grid configuration"));
        return INDEX_NONE;
    }
    
    // Calculate zone ID based on world location and zone size
    // This is a simplified implementation - in a real system you'd need a more
    // sophisticated algorithm that takes into account the zone grid configuration
    
    float ZoneSize = GridConfig->ZoneSize;
    
    // Simple grid-based approach - divide world into zones of fixed size
    int32 X = FMath::FloorToInt(InWorldLocation.X / ZoneSize);
    int32 Y = FMath::FloorToInt(InWorldLocation.Y / ZoneSize);
    int32 Z = FMath::FloorToInt(InWorldLocation.Z / ZoneSize);
    
    // Combine XYZ into a single zone ID
    // This is a basic approach - more sophisticated systems might use different
    // encoding methods like Morton codes for better spatial locality
    int32 ZoneID = X + (Y * 100) + (Z * 10000);
    
    return ZoneID;
}

TSharedPtr<IZoneManager> UZoneServiceProvider::GetZoneManager(int32 InRegionID) const
{
    if (!bInitialized)
    {
        return nullptr;
    }
    
    FScopeLock Lock(&ServiceLock);
    
    // Look up in the map
    const TSharedPtr<IZoneManager>* ManagerPtr = ZoneManagers.Find(InRegionID);
    if (ManagerPtr && ManagerPtr->IsValid())
    {
        return *ManagerPtr;
    }
    
    // If not found, try to resolve from service locator
    if (ServiceLocator)
    {
        void* RawManager = ServiceLocator->ResolveService(UZoneManager::StaticClass(), INDEX_NONE, InRegionID);
        if (RawManager)
        {
            // Create shared pointer with empty deleter to avoid calling the protected destructor
            IZoneManager* TypedManager = static_cast<IZoneManager*>(RawManager);
            return TSharedPtr<IZoneManager>(TypedManager, [](IZoneManager*) {});
        }
    }
    
    return nullptr;
}

void UZoneServiceProvider::UpdateTransactionConflictRate(int32 InZoneID, uint32 InTransactionType, float InConflictRate)
{
    if (!bInitialized)
    {
        return;
    }
    
    // Clamp conflict rate to valid range
    float ConflictRate = FMath::Clamp(InConflictRate, 0.0f, 1.0f);
    
    FScopeLock Lock(&ServiceLock);
    
    // Generate key for lookup
    uint64 Key = GenerateZoneServiceKey(InZoneID, InTransactionType);
    
    // Update conflict rate
    ConflictRates.Add(Key, ConflictRate);
    
    // Update fast path threshold dynamically based on conflict rate
    UpdateFastPathThreshold(InZoneID, InTransactionType);
    
    // If we have access to the type registry, update its conflict rate too
    if (TypeRegistry.IsValid())
    {
        TypeRegistry.Pin()->UpdateConflictRate(InTransactionType, ConflictRate);
    }
}

void UZoneServiceProvider::InitializeWithRegistry()
{
    // Get the zone type registry
    if (!TypeRegistry.IsValid())
    {
        // Use a direct reference without copy construction
        FZoneTypeRegistry& RegistryRef = FZoneTypeRegistry::Get();
        TypeRegistry = TSharedPtr<FZoneTypeRegistry>(&RegistryRef, [](FZoneTypeRegistry*) {});
    }
    
    // Initialize dependencies
    ServiceDependencies.Empty();
    
    // Add required dependencies using the correct field names
    FServiceDependency TypeRegistryDependency;
    TypeRegistryDependency.DependencyType = nullptr; // This would be the type registry interface
    TypeRegistryDependency.DependencyKind = EServiceDependencyType::Required;
    ServiceDependencies.Add(TypeRegistryDependency);
    
    // Get default zone grid configuration
    if (TypeRegistry.IsValid())
    {
        TSharedPtr<FZoneTypeRegistry> PinnedRegistry = TypeRegistry.Pin();
        if (PinnedRegistry.IsValid())
        {
            const FZoneGridConfig* DefaultConfig = PinnedRegistry->GetDefaultZoneGridConfig();
            if (DefaultConfig)
            {
                ZoneGridConfigName = DefaultConfig->DefaultConfigName;
            }
        }
    }
    
    // Initialize critical transaction types
    CriticalTransactionTypes.Empty();
    
    // Add known critical transaction types
    // These would be identified through profiling or domain knowledge
    // For now, just placeholder values
    CriticalTransactionTypes.Add(1); // Example: mining transaction
    CriticalTransactionTypes.Add(2); // Example: material update transaction
}

uint64 UZoneServiceProvider::GenerateZoneServiceKey(int32 InZoneID, uint32 InTransactionType) const
{
    // Combine transaction type and zone ID into a single key
    uint64 Key = static_cast<uint64>(InTransactionType) << 32;
    Key |= static_cast<uint32>(InZoneID);
    return Key;
}

void UZoneServiceProvider::UpdateFastPathThreshold(int32 InZoneID, uint32 InTransactionType)
{
    uint64 Key = GenerateZoneServiceKey(InZoneID, InTransactionType);
    
    // Get current conflict rate
    float* ConflictRatePtr = ConflictRates.Find(Key);
    if (!ConflictRatePtr)
    {
        return;
    }
    
    // Current conflict rate
    float CurrentRate = *ConflictRatePtr;
    
    // Get current threshold
    float* ThresholdPtr = FastPathThresholds.Find(Key);
    if (!ThresholdPtr)
    {
        // If no threshold exists, initialize it
        FastPathThresholds.Add(Key, FMath::Max(0.1f, CurrentRate + 0.2f));
        return;
    }
    
    // Adaptive threshold algorithm:
    // 1. If conflict rate is very low, lower threshold to enable more fast paths
    // 2. If conflict rate is close to threshold, increase threshold to reduce conflicts
    // 3. Otherwise, slowly adjust threshold towards optimal value
    
    float CurrentThreshold = *ThresholdPtr;
    float NewThreshold = CurrentThreshold;
    
    if (CurrentRate < 0.05f)
    {
        // Very low conflict rate, decrease threshold
        NewThreshold = FMath::Max(0.1f, CurrentThreshold - 0.05f);
    }
    else if (CurrentRate > CurrentThreshold * 0.8f)
    {
        // Conflict rate getting close to threshold, increase threshold
        NewThreshold = FMath::Min(0.95f, CurrentThreshold + 0.1f);
    }
    else
    {
        // Normal adjustment - exponential moving average towards optimal value
        constexpr float AdjustmentRate = 0.1f;
        float TargetThreshold = CurrentRate + 0.2f; // Target is 20% above current rate
        NewThreshold = CurrentThreshold * (1.0f - AdjustmentRate) + TargetThreshold * AdjustmentRate;
    }
    
    // Update threshold
    *ThresholdPtr = NewThreshold;
    
    UE_LOG(LogZoneServiceProvider, Verbose, TEXT("Updated fast path threshold for zone %d, type %u: %.2f -> %.2f (conflict rate: %.2f)"), 
        InZoneID, InTransactionType, CurrentThreshold, NewThreshold, CurrentRate);
}
