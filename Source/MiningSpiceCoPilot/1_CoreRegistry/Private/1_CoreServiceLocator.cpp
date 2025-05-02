// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreServiceLocator.h"
#include "Interfaces/IServiceProvider.h"
#include "Misc/ScopeLock.h"
#include "Logging/LogMacros.h"
#include "SVOTypeRegistry.h"
#include "MaterialRegistry.h"
#include "SDFTypeRegistry.h"
#include "ZoneTypeRegistry.h"

// Add these includes for the memory management systems
#include "../../2_MemoryManagement/Public/Interfaces/IMemoryManager.h"
#include "../../2_MemoryManagement/Public/MemoryPoolManager.h"
#include "../../2_MemoryManagement/Public/SVOAllocator.h"
#include "../../2_MemoryManagement/Public/NarrowBandAllocator.h"
#include "../../2_MemoryManagement/Public/ZeroCopyBuffer.h"
#include "../../2_MemoryManagement/Public/MemoryTelemetry.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoreServiceLocator, Log, All);

// Initialize static members
FCoreServiceLocator* FCoreServiceLocator::Singleton = nullptr;
FThreadSafeBool FCoreServiceLocator::bSingletonInitialized = false;

FCoreServiceLocator::FCoreServiceLocator()
    : bIsInitialized(false)
    , FastPathHits(0)
    , StandardResolutionCount(0)
{
    // Constructor is intentionally minimal
}

FCoreServiceLocator::~FCoreServiceLocator()
{
    // Ensure we're properly shut down
    if (IsInitialized())
    {
        Shutdown();
    }
}

bool FCoreServiceLocator::Initialize()
{
    if (bIsInitialized)
    {
        UE_LOG(LogCoreServiceLocator, Warning, TEXT("CoreServiceLocator already initialized"));
        return true;
    }
    
    UE_LOG(LogCoreServiceLocator, Log, TEXT("Initializing CoreServiceLocator"));
    
    // Initialize core type registries before memory systems
    FSVOTypeRegistry::Get().Initialize();
    FSDFTypeRegistry::Get().Initialize();
    FMaterialRegistry::Get().Initialize();
    FZoneTypeRegistry::Get().Initialize();
    
    // Register memory management systems
    RegisterMemoryAllocators();
    
    // Initialize providers
    for (const TScriptInterface<IServiceProvider>& Provider : ServiceProviders)
    {
        if (Provider.GetInterface())
        {
            Provider->InitializeServices();
        }
    }
    
    bIsInitialized = true;
    
    return true;
}

void FCoreServiceLocator::Shutdown()
{
    if (bIsInitialized)
    {
        // Lock for thread safety
        FScopedWriteLock Lock(ServiceMapRWLock);
        
        // Shutdown service providers in reverse order
        for (int32 i = ServiceProviders.Num() - 1; i >= 0; --i)
        {
            if (ServiceProviders[i].GetInterface())
            {
                ServiceProviders[i]->ShutdownServices();
            }
        }
        
        // Clear all registered services and providers
        ServiceMap.Empty();
        ServiceProviders.Empty();
        
        // Reset state
        bIsInitialized = false;
    }
}

bool FCoreServiceLocator::IsInitialized() const
{
    return bIsInitialized;
}

bool FCoreServiceLocator::RegisterService(void* InService, const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID)
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FCoreServiceLocator::RegisterService failed - locator not initialized"));
        return false;
    }
    
    if (!InService || !InInterfaceType || !InInterfaceType->ImplementsInterface(UInterface::StaticClass()))
    {
        UE_LOG(LogTemp, Error, TEXT("FCoreServiceLocator::RegisterService failed - invalid parameters"));
        return false;
    }
    
    // Generate context key
    FString ContextKey = GetServiceContextKey(InZoneID, InRegionID);
    FName InterfaceName = InInterfaceType->GetFName();
    
    // Lock for thread safety
    FScopedWriteLock Lock(ServiceMapRWLock);
    
    // Get or create interface map entry
    TMap<FString, TArray<FServiceInstance>>& ContextMap = ServiceMap.FindOrAdd(InterfaceName);
    
    // Get or create context array
    TArray<FServiceInstance>& Instances = ContextMap.FindOrAdd(ContextKey);
    
    // Create service instance with priority (zones have higher priority than regions)
    int32 Priority = 0;
    if (InZoneID != INDEX_NONE) Priority += 100;
    if (InRegionID != INDEX_NONE) Priority += 10;
    
    // Add service instance
    Instances.Add(FServiceInstance(InService, InZoneID, InRegionID, Priority));
    
    UE_LOG(LogTemp, Verbose, TEXT("FCoreServiceLocator::RegisterService - registered service type '%s' for context %s"),
        *InterfaceName.ToString(), *ContextKey);
    
    return true;
}

bool FCoreServiceLocator::RegisterServiceByTypeName(const FString& ServiceTypeName, void* InService, int32 InZoneID, int32 InRegionID)
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FCoreServiceLocator::RegisterServiceByTypeName failed - locator not initialized"));
        return false;
    }
    
    if (!InService)
    {
        UE_LOG(LogTemp, Error, TEXT("FCoreServiceLocator::RegisterServiceByTypeName failed - null service"));
        return false;
    }
    
    // Create a FName from the service type name
    FName InterfaceName(*ServiceTypeName);
    
    // Generate context key
    FString ContextKey = GetServiceContextKey(InZoneID, InRegionID);
    
    // Create service instance
    FServiceInstance Instance(InService, InZoneID, InRegionID);
    
    // Take write lock
    FScopedWriteLock Lock(ServiceMapRWLock);
    
    // Get or create interface map entry
    TMap<FString, TArray<FServiceInstance>>& ContextMap = ServiceMap.FindOrAdd(InterfaceName);
    
    // Get or create context entry
    TArray<FServiceInstance>& Instances = ContextMap.FindOrAdd(ContextKey);
    
    // Add service instance
    Instances.Add(Instance);
    
    UE_LOG(LogTemp, Verbose, TEXT("FCoreServiceLocator::RegisterServiceByTypeName - Registered service %s with context %s"),
        *InterfaceName.ToString(), *ContextKey);
    
    return true;
}

void* FCoreServiceLocator::ResolveService(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID)
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FCoreServiceLocator::ResolveService failed - locator not initialized"));
        return nullptr;
    }
    
    if (!InInterfaceType || !InInterfaceType->ImplementsInterface(UInterface::StaticClass()))
    {
        UE_LOG(LogTemp, Error, TEXT("FCoreServiceLocator::ResolveService failed - invalid interface type"));
        return nullptr;
    }
    
    // First try the fast path with the type hash for critical services
    uint32 TypeHash = GetTypeHash(InInterfaceType->GetFName());
    FFastPathEntry* FastPath = GetFastPathForType(TypeHash, InZoneID, InRegionID);
    if (FastPath)
    {
        // Increment usage counter for frequency tracking
        FastPath->UsageCount.Increment();
        FastPathHits.Increment();
        return FastPath->ServiceInstance;
    }
    
    // If fast path failed, fall back to standard resolution
    StandardResolutionCount.Increment();
    
    FName InterfaceName = InInterfaceType->GetFName();
    
    // Lock for thread safety
    FScopedReadLock Lock(ServiceMapRWLock);
    
    // Look up interface map entry
    const TMap<FString, TArray<FServiceInstance>>* ContextMapPtr = ServiceMap.Find(InterfaceName);
    if (!ContextMapPtr)
    {
        UE_LOG(LogTemp, Verbose, TEXT("FCoreServiceLocator::ResolveService - no services registered for interface '%s'"),
            *InterfaceName.ToString());
        return nullptr;
    }
    
    // First, try exact zone and region match
    if (InZoneID != INDEX_NONE && InRegionID != INDEX_NONE)
    {
        FString ExactKey = GetServiceContextKey(InZoneID, InRegionID);
        const TArray<FServiceInstance>* ExactInstancesPtr = ContextMapPtr->Find(ExactKey);
        if (ExactInstancesPtr && ExactInstancesPtr->Num() > 0)
        {
            void* Result = ResolveBestMatchingService(*ExactInstancesPtr, InZoneID, InRegionID);
            
            // If this type is frequently accessed (more than 5 times), register it as a fast path
            // But only do this for exact matches with stable services
            if (Result && StandardResolutionCount.GetValue() > 5)
            {
                const_cast<FCoreServiceLocator*>(this)->RegisterFastPath(InInterfaceType, Result, InZoneID, InRegionID);
            }
            
            return Result;
        }
    }
    
    // Second, try zone-specific match (any region)
    if (InZoneID != INDEX_NONE)
    {
        // Search for any service with matching zone ID
        for (const auto& ContextPair : *ContextMapPtr)
        {
            if (ContextPair.Value.Num() > 0)
            {
                for (const FServiceInstance& Instance : ContextPair.Value)
                {
                    if (Instance.ZoneID == InZoneID)
                    {
                        return Instance.ServiceInstance;
                    }
                }
            }
        }
    }
    
    // Third, try region-specific match (any zone)
    if (InRegionID != INDEX_NONE)
    {
        // Search for any service with matching region ID
        for (const auto& ContextPair : *ContextMapPtr)
        {
            if (ContextPair.Value.Num() > 0)
            {
                for (const FServiceInstance& Instance : ContextPair.Value)
                {
                    if (Instance.RegionID == InRegionID)
                    {
                        return Instance.ServiceInstance;
                    }
                }
            }
        }
    }
    
    // Finally, look for global service (no zone, no region)
    FString GlobalKey = GetServiceContextKey(INDEX_NONE, INDEX_NONE);
    const TArray<FServiceInstance>* GlobalInstancesPtr = ContextMapPtr->Find(GlobalKey);
    if (GlobalInstancesPtr && GlobalInstancesPtr->Num() > 0)
    {
        return ResolveBestMatchingService(*GlobalInstancesPtr, InZoneID, InRegionID);
    }
    
    // No matching service found
    return nullptr;
}

bool FCoreServiceLocator::UnregisterService(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID)
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FCoreServiceLocator::UnregisterService failed - locator not initialized"));
        return false;
    }
    
    if (!InInterfaceType)
    {
        UE_LOG(LogTemp, Error, TEXT("FCoreServiceLocator::UnregisterService failed - invalid interface type"));
        return false;
    }
    
    // Generate context key
    FString ContextKey = GetServiceContextKey(InZoneID, InRegionID);
    FName InterfaceName = InInterfaceType->GetFName();
    
    // Lock for thread safety
    FScopedWriteLock Lock(ServiceMapRWLock);
    
    // Find interface map entry
    TMap<FString, TArray<FServiceInstance>>* ContextMapPtr = ServiceMap.Find(InterfaceName);
    if (!ContextMapPtr)
    {
        UE_LOG(LogTemp, Warning, TEXT("FCoreServiceLocator::UnregisterService - no services registered for interface '%s'"),
            *InterfaceName.ToString());
        return false;
    }
    
    // Find context array
    TArray<FServiceInstance>* InstancesPtr = ContextMapPtr->Find(ContextKey);
    if (!InstancesPtr || InstancesPtr->Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("FCoreServiceLocator::UnregisterService - no services found for interface '%s' and context %s"),
            *InterfaceName.ToString(), *ContextKey);
        return false;
    }
    
    // Remove all instances in this context
    InstancesPtr->Empty();
    
    // If this was the last context for this interface, remove the interface entry
    if (ContextMapPtr->Num() == 0)
    {
        ServiceMap.Remove(InterfaceName);
    }
    
    UE_LOG(LogTemp, Verbose, TEXT("FCoreServiceLocator::UnregisterService - unregistered %d service(s) for interface '%s' and context %s"),
        InstancesPtr->Num(), *InterfaceName.ToString(), *ContextKey);
    
    return true;
}

bool FCoreServiceLocator::HasService(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID) const
{
    if (!IsInitialized())
    {
        return false;
    }
    
    if (!InInterfaceType)
    {
        return false;
    }
    
    FName InterfaceName = InInterfaceType->GetFName();
    
    // Lock for thread safety
    FScopedReadLock Lock(ServiceMapRWLock);
    
    // Find interface map entry
    const TMap<FString, TArray<FServiceInstance>>* ContextMapPtr = ServiceMap.Find(InterfaceName);
    if (!ContextMapPtr)
    {
        return false;
    }
    
    // Check specific context if zone and region are provided
    if (InZoneID != INDEX_NONE || InRegionID != INDEX_NONE)
    {
        FString ContextKey = GetServiceContextKey(InZoneID, InRegionID);
        const TArray<FServiceInstance>* InstancesPtr = ContextMapPtr->Find(ContextKey);
        return (InstancesPtr && InstancesPtr->Num() > 0);
    }
    
    // Check for any service of this type
    for (const auto& ContextPair : *ContextMapPtr)
    {
        if (ContextPair.Value.Num() > 0)
        {
            return true;
        }
    }
    
    return false;
}

bool FCoreServiceLocator::RegisterServiceProvider(TScriptInterface<IServiceProvider> InProvider, int32 InZoneID, int32 InRegionID)
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FCoreServiceLocator::RegisterServiceProvider failed - locator not initialized"));
        return false;
    }
    
    if (!InProvider.GetInterface())
    {
        UE_LOG(LogTemp, Error, TEXT("FCoreServiceLocator::RegisterServiceProvider failed - invalid provider"));
        return false;
    }
    
    // Lock for thread safety
    FScopedWriteLock Lock(ServiceMapRWLock);
    
    // Add provider to list
    ServiceProviders.AddUnique(InProvider);
    
    // Initialize services from this provider
    if (IsInitialized())
    {
        InProvider->InitializeServices();
    }
    
    return true;
}

bool FCoreServiceLocator::UnregisterServiceProvider(TScriptInterface<IServiceProvider> InProvider, int32 InZoneID, int32 InRegionID)
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FCoreServiceLocator::UnregisterServiceProvider failed - locator not initialized"));
        return false;
    }
    
    if (!InProvider.GetInterface())
    {
        UE_LOG(LogTemp, Error, TEXT("FCoreServiceLocator::UnregisterServiceProvider failed - invalid provider"));
        return false;
    }
    
    // Lock for thread safety
    FScopedWriteLock Lock(ServiceMapRWLock);
    
    // Find and remove provider
    int32 ProviderIndex = ServiceProviders.Find(InProvider);
    if (ProviderIndex == INDEX_NONE)
    {
        UE_LOG(LogTemp, Warning, TEXT("FCoreServiceLocator::UnregisterServiceProvider - provider '%s' not found"),
            *InProvider->GetProviderName().ToString());
        return false;
    }
    
    // Shut down services from this provider
    InProvider->ShutdownServices();
    
    // Remove provider from list
    ServiceProviders.RemoveAt(ProviderIndex);
    
    return true;
}

TArray<const UClass*> FCoreServiceLocator::GetAllServiceTypes() const
{
    TArray<const UClass*> Result;
    
    if (!IsInitialized())
    {
        return Result;
    }
    
    // Lock for thread safety
    FScopedReadLock Lock(ServiceMapRWLock);
    
    // Collect all registered service types
    for (const auto& InterfacePair : ServiceMap)
    {
        UClass* InterfaceClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *InterfacePair.Key.ToString()));
        if (InterfaceClass)
        {
            Result.Add(InterfaceClass);
        }
    }
    
    return Result;
}

FString FCoreServiceLocator::GetServiceContextKey(int32 InZoneID, int32 InRegionID)
{
    return FString::Printf(TEXT("Z%d_R%d"), InZoneID, InRegionID);
}

void* FCoreServiceLocator::ResolveBestMatchingService(const TArray<FServiceInstance>& Instances, int32 InZoneID, int32 InRegionID) const
{
    if (Instances.Num() == 0)
    {
        return nullptr;
    }
    
    if (Instances.Num() == 1)
    {
        return Instances[0].ServiceInstance;
    }
    
    // Find the best matching service based on priority
    const FServiceInstance* BestMatch = &Instances[0];
    
    for (int32 i = 1; i < Instances.Num(); ++i)
    {
        const FServiceInstance& Current = Instances[i];
        
        // Check if current instance has higher priority
        if (Current.Priority > BestMatch->Priority)
        {
            BestMatch = &Current;
            continue;
        }
        
        // If equal priority, prefer more specific match
        if (Current.Priority == BestMatch->Priority)
        {
            // If searching for a specific zone, prefer matching zone
            if (InZoneID != INDEX_NONE && Current.ZoneID == InZoneID && BestMatch->ZoneID != InZoneID)
            {
                BestMatch = &Current;
                continue;
            }
            
            // If searching for a specific region, prefer matching region
            if (InRegionID != INDEX_NONE && Current.RegionID == InRegionID && BestMatch->RegionID != InRegionID)
            {
                BestMatch = &Current;
                continue;
            }
        }
    }
    
    return BestMatch->ServiceInstance;
}

// Static implementation of IServiceLocator::Get
IServiceLocator& IServiceLocator::Get()
{
    // Thread-safe singleton initialization
    if (!FCoreServiceLocator::bSingletonInitialized)
    {
        static FSpinLock InitializationLock;
        FScopedSpinLock Lock(InitializationLock);
        
        // Double-checked locking pattern
        if (!FCoreServiceLocator::bSingletonInitialized)
        {
            // Create and initialize the singleton instance with proper access
            FCoreServiceLocator* NewSingleton = new FCoreServiceLocator();
            NewSingleton->Initialize();
            FCoreServiceLocator::Singleton = NewSingleton;
            FCoreServiceLocator::bSingletonInitialized.AtomicSet(true);
        }
    }
    
    check(FCoreServiceLocator::Singleton != nullptr);
    return *FCoreServiceLocator::Singleton;
}

/**
 * Registers memory allocators from MemoryPoolManager with the service locator
 * This establishes critical integration between Core Registry and Memory Management systems
 */
void FCoreServiceLocator::RegisterMemoryAllocators()
{
    // This method registers all memory allocators from the Memory Management system
    // as services in the Core Registry system, establishing the integration point
    
    // First, resolve or create the memory manager
    UClass* MemoryManagerClass = FindObject<UClass>(nullptr, TEXT("UMemoryManager"));
    FMemoryPoolManager* MemoryManager = static_cast<FMemoryPoolManager*>(ResolveService(MemoryManagerClass));
    if (!MemoryManager)
    {
        // If memory manager isn't registered yet, create and register it
        MemoryManager = new FMemoryPoolManager();
        MemoryManager->Initialize();
        
        // Register using a UClass from UObject system or a known interface class
        RegisterService(MemoryManager, MemoryManagerClass ? MemoryManagerClass : UObject::StaticClass());
        
        UE_LOG(LogCoreServiceLocator, Log, TEXT("Created and registered Memory Manager"));
    }
    
    // Create and register SVO Allocator with proper parameters
    FName SVOPoolName(TEXT("DefaultSVOPool"));
    FSVOAllocator* SVOAllocator = new FSVOAllocator(SVOPoolName, 256, 1024, EMemoryAccessPattern::OctreeTraversal, true);
    SVOAllocator->Initialize();
    
    // Find interface class or use UObject as fallback
    UClass* SVOAllocatorClass = FindObject<UClass>(nullptr, TEXT("UPoolAllocator"));
    RegisterService(SVOAllocator, SVOAllocatorClass ? SVOAllocatorClass : UObject::StaticClass());
    
    // Register it as a fast-path for critical operations
    RegisterFastPath(SVOAllocatorClass ? SVOAllocatorClass : UObject::StaticClass(), SVOAllocator);
    
    // Create and register Narrow Band Allocator for SDF fields with proper parameters
    FName NarrowBandPoolName(TEXT("DefaultNarrowBandPool"));
    FNarrowBandAllocator* NarrowBandAllocator = new FNarrowBandAllocator(NarrowBandPoolName, 128, 1024, EMemoryAccessPattern::SDFOperation, true);
    NarrowBandAllocator->Initialize();
    
    // Find interface class or use UObject as fallback
    UClass* NarrowBandClass = FindObject<UClass>(nullptr, TEXT("UPoolAllocator"));
    RegisterService(NarrowBandAllocator, NarrowBandClass ? NarrowBandClass : UObject::StaticClass());
    
    // Register it as a fast-path for critical operations
    RegisterFastPath(NarrowBandClass ? NarrowBandClass : UObject::StaticClass(), NarrowBandAllocator);
    
    // Create and register ZeroCopyBuffer for efficient GPU/CPU data sharing with proper parameters
    FName ZeroCopyBufferName(TEXT("DefaultZeroCopyBuffer"));
    FZeroCopyBuffer* ZeroCopyBuffer = new FZeroCopyBuffer(ZeroCopyBufferName, 1024*1024, EBufferUsage::General, false);
    ZeroCopyBuffer->Initialize();
    
    // Find interface class or use UObject as fallback
    UClass* ZeroCopyClass = FindObject<UClass>(nullptr, TEXT("UBufferProvider"));
    RegisterService(ZeroCopyBuffer, ZeroCopyClass ? ZeroCopyClass : UObject::StaticClass());
    
    // Register it as a fast-path for critical operations
    RegisterFastPath(ZeroCopyClass ? ZeroCopyClass : UObject::StaticClass(), ZeroCopyBuffer);
    
    // Create and register Memory Telemetry for performance tracking
    FMemoryTelemetry* MemoryTelemetry = new FMemoryTelemetry();
    MemoryTelemetry->Initialize();
    
    // Find interface class or use UObject as fallback
    UClass* TelemetryClass = FindObject<UClass>(nullptr, TEXT("UMemoryTracker"));
    RegisterService(MemoryTelemetry, TelemetryClass ? TelemetryClass : UObject::StaticClass());
    
    UE_LOG(LogCoreServiceLocator, Log, TEXT("Registered all Memory Management allocators"));
    
    // Register memory manager with MemoryPoolManager to create fast paths
    // Cast to FMemoryPoolManager* since RegisterFastPath expects this type
    if (FMemoryPoolManager* MemoryPoolManager = static_cast<FMemoryPoolManager*>(MemoryManager))
    {
        MemoryPoolManager->RegisterFastPath(MemoryPoolManager);
    }
    else
    {
        UE_LOG(LogCoreServiceLocator, Warning, TEXT("Failed to cast MemoryManager to FMemoryPoolManager for fast path registration"));
    }
}

// Implementation for GetFastPathForType
typename FCoreServiceLocator::FFastPathEntry* FCoreServiceLocator::GetFastPathForType(uint32 TypeHash, int32 InZoneID, int32 InRegionID)
{
    if (!IsInitialized())
    {
        return nullptr;
    }
    
    // First check without locking for better performance
    if (FastPathCache.Contains(TypeHash))
    {
        FFastPathEntry& Entry = FastPathCache[TypeHash];
        
        // Verify zone and region match
        if ((Entry.ZoneID == INDEX_NONE || Entry.ZoneID == InZoneID) &&
            (Entry.RegionID == INDEX_NONE || Entry.RegionID == InRegionID))
        {
            return &Entry;
        }
    }
    
    // If no exact match, look for a compatible fast path with lock
    FScopedSpinLock Lock(FastPathLock);
    
    // Try finding an entry with matching type but different zone/region
    for (auto& Pair : FastPathCache)
    {
        if (Pair.Key == TypeHash)
        {
            FFastPathEntry& Entry = Pair.Value;
            
            // Check for compatible zone/region (either exact match or global)
            if ((Entry.ZoneID == INDEX_NONE || Entry.ZoneID == InZoneID) &&
                (Entry.RegionID == INDEX_NONE || Entry.RegionID == InRegionID))
            {
                return &Entry;
            }
        }
    }
    
    return nullptr;
}

// Implementation for RegisterFastPath
bool FCoreServiceLocator::RegisterFastPath(const UClass* InInterfaceType, void* InServiceInstance, int32 InZoneID, int32 InRegionID)
{
    if (!IsInitialized() || !InInterfaceType || !InServiceInstance)
    {
        return false;
    }
    
    uint32 TypeHash = GetTypeHash(InInterfaceType->GetFName());
    
    // Create a fast path entry with lock
    FScopedSpinLock Lock(FastPathLock);
    
    // Check if we already have a fast path for this type and context
    FFastPathEntry* ExistingEntry = nullptr;
    for (auto& Pair : FastPathCache)
    {
        if (Pair.Key == TypeHash &&
            Pair.Value.ZoneID == InZoneID &&
            Pair.Value.RegionID == InRegionID)
        {
            ExistingEntry = &Pair.Value;
            break;
        }
    }
    
    if (ExistingEntry)
    {
        // Update existing entry
        ExistingEntry->ServiceInstance = InServiceInstance;
        UE_LOG(LogCoreServiceLocator, Verbose, TEXT("Updated fast path for type %s"),
            *InInterfaceType->GetName());
    }
    else
    {
        // Create new entry
        FFastPathEntry NewEntry(InServiceInstance, TypeHash, InZoneID, InRegionID);
        FastPathCache.Add(TypeHash, NewEntry);
        UE_LOG(LogCoreServiceLocator, Verbose, TEXT("Registered fast path for type %s"),
            *InInterfaceType->GetName());
    }
    
    return true;
}

// Implementation for RegisterServiceWithVersion
bool FCoreServiceLocator::RegisterServiceWithVersion(void* InService, const UClass* InInterfaceType, const FServiceVersion& InVersion, int32 InZoneID, int32 InRegionID)
{
    if (!IsInitialized() || !InService)
    {
        return false;
    }
    
    // Get service name from type or generate a unique name
    FName ServiceName = InInterfaceType ? InInterfaceType->GetFName() : FName(*FGuid::NewGuid().ToString());
    
    // Register the service using standard method
    bool bSuccess = RegisterService(InService, InInterfaceType, InZoneID, InRegionID);
    
    if (bSuccess)
    {
        // Store the version information
        FScopedWriteLock Lock(ServiceMapRWLock);
        ServiceVersionsInfo.Add(ServiceName, InVersion);
        
        // Set scope information
        if (InZoneID != INDEX_NONE && InRegionID != INDEX_NONE)
        {
            ServiceScopes.Add(ServiceName, EServiceScope::Zone);
        }
        else if (InRegionID != INDEX_NONE)
        {
            ServiceScopes.Add(ServiceName, EServiceScope::Region);
        }
        else
        {
            ServiceScopes.Add(ServiceName, EServiceScope::Global);
        }
        
        // Initialize health status
        FString ContextKey = GetServiceContextKey(InZoneID, InRegionID);
        if (!ServiceHealthStatus.Contains(ServiceName))
        {
            ServiceHealthStatus.Add(ServiceName, TMap<FString, EServiceHealthStatus>());
        }
        ServiceHealthStatus[ServiceName].Add(ContextKey, EServiceHealthStatus::Healthy);
    }
    
    return bSuccess;
}

// Implementation for ResolveServiceWithVersion
void* FCoreServiceLocator::ResolveServiceWithVersion(const UClass* InInterfaceType, FServiceVersion& OutVersion, const FServiceVersion* InMinVersion, int32 InZoneID, int32 InRegionID)
{
    if (!IsInitialized() || !InInterfaceType)
    {
        return nullptr;
    }
    
    // Get service name from type
    FName ServiceName = InInterfaceType->GetFName();
    
    // First check if the service exists with the required version
    FScopedReadLock Lock(ServiceMapRWLock);
    
    if (!ServiceVersionsInfo.Contains(ServiceName))
    {
        // No version information available
        return nullptr;
    }
    
    const FServiceVersion& Version = ServiceVersionsInfo[ServiceName];
    OutVersion = Version;
    
    // Check if version meets minimum requirements
    if (InMinVersion && !Version.IsCompatibleWith(*InMinVersion))
    {
        // Version incompatible
        return nullptr;
    }
    
    // Version is compatible, resolve the service
    return ResolveService(InInterfaceType, InZoneID, InRegionID);
}

// Implementation for DeclareDependency
bool FCoreServiceLocator::DeclareDependency(const UClass* InDependentType, const UClass* InDependencyType, EServiceDependencyType InDependencyKind)
{
    if (!IsInitialized() || !InDependentType || !InDependencyType)
    {
        return false;
    }
    
    FScopedWriteLock Lock(ServiceMapRWLock);
    
    // Get service names
    FName DependentName = InDependentType->GetFName();
    FName DependencyName = InDependencyType->GetFName();
    
    // Add the dependency relationship
    if (!ServiceDependencies.Contains(DependentName))
    {
        ServiceDependencies.Add(DependentName, TArray<TPair<FName, EServiceDependencyType>>());
    }
    
    // Check if dependency already exists
    bool bDependencyExists = false;
    for (const auto& Pair : ServiceDependencies[DependentName])
    {
        if (Pair.Key == DependencyName)
        {
            bDependencyExists = true;
            break;
        }
    }
    
    if (!bDependencyExists)
    {
        // Add the new dependency
        ServiceDependencies[DependentName].Add(TPair<FName, EServiceDependencyType>(DependencyName, InDependencyKind));
    }
    
    return true;
}

// Implementation for ValidateDependencies
bool FCoreServiceLocator::ValidateDependencies(TArray<TPair<UClass*, UClass*>>& OutMissingDependencies)
{
    if (!IsInitialized())
    {
        return false;
    }
    
    bool bAllDependenciesSatisfied = true;
    FScopedReadLock Lock(ServiceMapRWLock);
    
    // Iterate through all services with dependencies
    for (const auto& ServicePair : ServiceDependencies)
    {
        FName DependentName = ServicePair.Key;
        
        // Check if the dependent service exists
        if (!ServiceMap.Contains(DependentName))
        {
            continue; // Skip, as the dependent service doesn't exist
        }
        
        // Check each dependency
        for (const auto& DependencyPair : ServicePair.Value)
        {
            FName DependencyName = DependencyPair.Key;
            EServiceDependencyType DependencyKind = DependencyPair.Value;
            
            // Skip optional dependencies
            if (DependencyKind != EServiceDependencyType::Required)
            {
                continue;
            }
            
            // Check if the dependency exists
            if (!ServiceMap.Contains(DependencyName) || ServiceMap[DependencyName].Num() == 0)
            {
                // Missing required dependency
                bAllDependenciesSatisfied = false;
                
                // Find UClass objects for the dependency pair
                UClass* DependentClass = FindObject<UClass>(nullptr, *DependentName.ToString());
                UClass* DependencyClass = FindObject<UClass>(nullptr, *DependencyName.ToString());
                
                if (DependentClass && DependencyClass)
                {
                    OutMissingDependencies.Add(TPair<UClass*, UClass*>(DependentClass, DependencyClass));
                }
            }
        }
    }
    
    return bAllDependenciesSatisfied;
}

// Implementation for GetServiceHealth
EServiceHealthStatus FCoreServiceLocator::GetServiceHealth(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID)
{
    if (!IsInitialized() || !InInterfaceType)
    {
        return EServiceHealthStatus::Unknown;
    }
    
    FScopedReadLock Lock(ServiceMapRWLock);
    
    // Get service name from type
    FName ServiceName = InInterfaceType->GetFName();
    
    // Check if health status is tracked for this service
    if (!ServiceHealthStatus.Contains(ServiceName))
    {
        return EServiceHealthStatus::Unknown;
    }
    
    // Get the context key
    FString ContextKey = GetServiceContextKey(InZoneID, InRegionID);
    
    // Check if health status is tracked for this context
    if (!ServiceHealthStatus[ServiceName].Contains(ContextKey))
    {
        // Try global context
        FString GlobalContextKey = GetServiceContextKey(INDEX_NONE, INDEX_NONE);
        if (ServiceHealthStatus[ServiceName].Contains(GlobalContextKey))
        {
            return ServiceHealthStatus[ServiceName][GlobalContextKey];
        }
        return EServiceHealthStatus::Unknown;
    }
    
    return ServiceHealthStatus[ServiceName][ContextKey];
}

// Implementation for RecoverService
bool FCoreServiceLocator::RecoverService(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID)
{
    if (!IsInitialized() || !InInterfaceType)
    {
        return false;
    }
    
    FScopedWriteLock Lock(ServiceMapRWLock);
    
    // Get service name from type
    FName ServiceName = InInterfaceType->GetFName();
    
    // Check if service exists
    if (!ServiceMap.Contains(ServiceName))
    {
        return false;
    }
    
    // Get the context key
    FString ContextKey = GetServiceContextKey(InZoneID, InRegionID);
    
    // Check if service exists in this context
    if (!ServiceMap[ServiceName].Contains(ContextKey) || ServiceMap[ServiceName][ContextKey].Num() == 0)
    {
        return false;
    }
    
    // Get the service instance
    void* ServiceInstance = ServiceMap[ServiceName][ContextKey][0].ServiceInstance;
    if (!ServiceInstance)
    {
        return false;
    }
    
    // Update health status
    if (!ServiceHealthStatus.Contains(ServiceName))
    {
        ServiceHealthStatus.Add(ServiceName, TMap<FString, EServiceHealthStatus>());
    }
    ServiceHealthStatus[ServiceName].Add(ContextKey, EServiceHealthStatus::Healthy);
    
    // For now, just mark the service as healthy - actual recovery would depend on the specific service
    return true;
}

// Implementation for GetServiceScope
EServiceScope FCoreServiceLocator::GetServiceScope(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID)
{
    if (!IsInitialized() || !InInterfaceType)
    {
        return EServiceScope::Global; // Default to global scope
    }
    
    FScopedReadLock Lock(ServiceMapRWLock);
    
    // Get service name from type
    FName ServiceName = InInterfaceType->GetFName();
    
    // Check if scope is explicitly defined
    if (ServiceScopes.Contains(ServiceName))
    {
        return ServiceScopes[ServiceName];
    }
    
    // Infer scope from context IDs
    if (InZoneID != INDEX_NONE && InRegionID != INDEX_NONE)
    {
        return EServiceScope::Zone;
    }
    else if (InRegionID != INDEX_NONE)
    {
        return EServiceScope::Region;
    }
    
    return EServiceScope::Global;
}

// Implementation for GetDependentServices
TArray<UClass*> FCoreServiceLocator::GetDependentServices(const UClass* InInterfaceType)
{
    TArray<UClass*> DependentServices;
    
    if (!IsInitialized() || !InInterfaceType)
    {
        return DependentServices;
    }
    
    FScopedReadLock Lock(ServiceMapRWLock);
    
    // Get service name from type
    FName ServiceName = InInterfaceType->GetFName();
    
    // Find all services that depend on this one
    for (const auto& ServicePair : ServiceDependencies)
    {
        FName DependentName = ServicePair.Key;
        
        // Check if this service depends on the specified service
        for (const auto& DependencyPair : ServicePair.Value)
        {
            if (DependencyPair.Key == ServiceName)
            {
                // This service depends on the specified service
                UClass* DependentClass = FindObject<UClass>(nullptr, *DependentName.ToString());
                if (DependentClass)
                {
                    DependentServices.Add(DependentClass);
                }
                break;
            }
        }
    }
    
    return DependentServices;
}

// Implementation for GetServiceDependencies
TArray<UClass*> FCoreServiceLocator::GetServiceDependencies(const UClass* InInterfaceType)
{
    TArray<UClass*> Dependencies;
    
    if (!IsInitialized() || !InInterfaceType)
    {
        return Dependencies;
    }
    
    FScopedReadLock Lock(ServiceMapRWLock);
    
    // Get service name from type
    FName ServiceName = InInterfaceType->GetFName();
    
    // Check if this service has any dependencies
    if (!ServiceDependencies.Contains(ServiceName))
    {
        return Dependencies;
    }
    
    // Convert dependency names to UClass objects
    for (const auto& DependencyPair : ServiceDependencies[ServiceName])
    {
        FName DependencyName = DependencyPair.Key;
        UClass* DependencyClass = FindObject<UClass>(nullptr, *DependencyName.ToString());
        
        if (DependencyClass)
        {
            Dependencies.Add(DependencyClass);
        }
    }
    
    return Dependencies;
}