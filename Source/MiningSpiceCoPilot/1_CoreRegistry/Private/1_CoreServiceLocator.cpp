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
        FRWScopeLock Lock(ServiceMapLock, SLT_Write);
        
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
    FRWScopeLock Lock(ServiceMapLock, SLT_Write);
    
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
    FRWScopeLock Lock(ServiceMapLock, SLT_ReadOnly);
    
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
                RegisterFastPath(InInterfaceType, Result, InZoneID, InRegionID);
            }
            
            return Result;
        }
    }
    
    // Next, try zone-only match
    if (InZoneID != INDEX_NONE)
    {
        FString ZoneKey = GetServiceContextKey(InZoneID, INDEX_NONE);
        const TArray<FServiceInstance>* ZoneInstancesPtr = ContextMapPtr->Find(ZoneKey);
        if (ZoneInstancesPtr && ZoneInstancesPtr->Num() > 0)
        {
            return ResolveBestMatchingService(*ZoneInstancesPtr, InZoneID, InRegionID);
        }
    }
    
    // Next, try region-only match
    if (InRegionID != INDEX_NONE)
    {
        FString RegionKey = GetServiceContextKey(INDEX_NONE, InRegionID);
        const TArray<FServiceInstance>* RegionInstancesPtr = ContextMapPtr->Find(RegionKey);
        if (RegionInstancesPtr && RegionInstancesPtr->Num() > 0)
        {
            return ResolveBestMatchingService(*RegionInstancesPtr, InZoneID, InRegionID);
        }
    }
    
    // Finally, try global match
    FString GlobalKey = GetServiceContextKey(INDEX_NONE, INDEX_NONE);
    const TArray<FServiceInstance>* GlobalInstancesPtr = ContextMapPtr->Find(GlobalKey);
    if (GlobalInstancesPtr && GlobalInstancesPtr->Num() > 0)
    {
        return ResolveBestMatchingService(*GlobalInstancesPtr, InZoneID, InRegionID);
    }
    
    UE_LOG(LogTemp, Verbose, TEXT("FCoreServiceLocator::ResolveService - no matching service found for interface '%s' and context Zone=%d, Region=%d"),
        *InterfaceName.ToString(), InZoneID, InRegionID);
    
    return nullptr;
}

bool FCoreServiceLocator::UnregisterService(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID)
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FCoreServiceLocator::UnregisterService failed - locator not initialized"));
        return false;
    }
    
    if (!InInterfaceType || !InInterfaceType->ImplementsInterface(UInterface::StaticClass()))
    {
        UE_LOG(LogTemp, Error, TEXT("FCoreServiceLocator::UnregisterService failed - invalid interface type"));
        return false;
    }
    
    FName InterfaceName = InInterfaceType->GetFName();
    FString ContextKey = GetServiceContextKey(InZoneID, InRegionID);
    
    // Lock for thread safety
    FRWScopeLock Lock(ServiceMapLock, SLT_Write);
    
    // Look up interface map entry
    TMap<FString, TArray<FServiceInstance>>* ContextMapPtr = ServiceMap.Find(InterfaceName);
    if (!ContextMapPtr)
    {
        UE_LOG(LogTemp, Warning, TEXT("FCoreServiceLocator::UnregisterService - no services registered for interface '%s'"),
            *InterfaceName.ToString());
        return false;
    }
    
    // Remove services from the context
    int32 NumRemoved = ContextMapPtr->Remove(ContextKey);
    
    // If the context map is now empty, remove the interface entry
    if (ContextMapPtr->Num() == 0)
    {
        ServiceMap.Remove(InterfaceName);
    }
    
    if (NumRemoved > 0)
    {
        UE_LOG(LogTemp, Verbose, TEXT("FCoreServiceLocator::UnregisterService - unregistered %d service(s) for interface '%s' and context %s"),
            NumRemoved, *InterfaceName.ToString(), *ContextKey);
        return true;
    }
    
    UE_LOG(LogTemp, Warning, TEXT("FCoreServiceLocator::UnregisterService - no services found for interface '%s' and context %s"),
        *InterfaceName.ToString(), *ContextKey);
    
    return false;
}

bool FCoreServiceLocator::HasService(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID) const
{
    if (!IsInitialized())
    {
        return false;
    }
    
    if (!InInterfaceType || !InInterfaceType->ImplementsInterface(UInterface::StaticClass()))
    {
        return false;
    }
    
    FName InterfaceName = InInterfaceType->GetFName();
    
    // Lock for thread safety
    FRWScopeLock Lock(ServiceMapLock, SLT_ReadOnly);
    
    // Look up interface map entry
    const TMap<FString, TArray<FServiceInstance>>* ContextMapPtr = ServiceMap.Find(InterfaceName);
    if (!ContextMapPtr)
    {
        return false;
    }
    
    // Check for services with exact zone and region match
    if (InZoneID != INDEX_NONE && InRegionID != INDEX_NONE)
    {
        FString ExactKey = GetServiceContextKey(InZoneID, InRegionID);
        const TArray<FServiceInstance>* ExactInstancesPtr = ContextMapPtr->Find(ExactKey);
        if (ExactInstancesPtr && ExactInstancesPtr->Num() > 0)
        {
            return true;
        }
    }
    
    // Check for services with zone-only match
    if (InZoneID != INDEX_NONE)
    {
        FString ZoneKey = GetServiceContextKey(InZoneID, INDEX_NONE);
        const TArray<FServiceInstance>* ZoneInstancesPtr = ContextMapPtr->Find(ZoneKey);
        if (ZoneInstancesPtr && ZoneInstancesPtr->Num() > 0)
        {
            return true;
        }
    }
    
    // Check for services with region-only match
    if (InRegionID != INDEX_NONE)
    {
        FString RegionKey = GetServiceContextKey(INDEX_NONE, InRegionID);
        const TArray<FServiceInstance>* RegionInstancesPtr = ContextMapPtr->Find(RegionKey);
        if (RegionInstancesPtr && RegionInstancesPtr->Num() > 0)
        {
            return true;
        }
    }
    
    // Check for global services
    FString GlobalKey = GetServiceContextKey(INDEX_NONE, INDEX_NONE);
    const TArray<FServiceInstance>* GlobalInstancesPtr = ContextMapPtr->Find(GlobalKey);
    if (GlobalInstancesPtr && GlobalInstancesPtr->Num() > 0)
    {
        return true;
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
    
    // Initialize provider's services
    if (!InProvider->InitializeServices())
    {
        UE_LOG(LogTemp, Error, TEXT("FCoreServiceLocator::RegisterServiceProvider failed - provider '%s' initialization failed"),
            *InProvider->GetProviderName().ToString());
        return false;
    }
    
    // Lock for thread safety (for the ServiceProviders array)
    FRWScopeLock Lock(ServiceMapLock, SLT_Write);
    
    // Add provider to the list
    ServiceProviders.Add(InProvider);
    
    // Register provider's services
    bool bSuccess = InProvider->RegisterServices(this, InZoneID, InRegionID);
    if (!bSuccess)
    {
        UE_LOG(LogTemp, Warning, TEXT("FCoreServiceLocator::RegisterServiceProvider - provider '%s' failed to register some services"),
            *InProvider->GetProviderName().ToString());
    }
    
    UE_LOG(LogTemp, Verbose, TEXT("FCoreServiceLocator::RegisterServiceProvider - registered provider '%s'"),
        *InProvider->GetProviderName().ToString());
    
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
    FRWScopeLock Lock(ServiceMapLock, SLT_Write);
    
    // Find provider in the list
    int32 ProviderIndex = ServiceProviders.Find(InProvider);
    if (ProviderIndex == INDEX_NONE)
    {
        UE_LOG(LogTemp, Warning, TEXT("FCoreServiceLocator::UnregisterServiceProvider - provider '%s' not found"),
            *InProvider->GetProviderName().ToString());
        return false;
    }
    
    // Unregister provider's services
    bool bSuccess = InProvider->UnregisterServices(this, InZoneID, InRegionID);
    if (!bSuccess)
    {
        UE_LOG(LogTemp, Warning, TEXT("FCoreServiceLocator::UnregisterServiceProvider - provider '%s' failed to unregister some services"),
            *InProvider->GetProviderName().ToString());
    }
    
    // Remove provider from the list
    ServiceProviders.RemoveAt(ProviderIndex);
    
    // Shutdown provider's services
    InProvider->ShutdownServices();
    
    UE_LOG(LogTemp, Verbose, TEXT("FCoreServiceLocator::UnregisterServiceProvider - unregistered provider '%s'"),
        *InProvider->GetProviderName().ToString());
    
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
    FRWScopeLock Lock(ServiceMapLock, SLT_ReadOnly);
    
    // Collect all service types
    for (const auto& ServiceTypePair : ServiceMap)
    {
        const FName& InterfaceName = ServiceTypePair.Key;
        const UClass* InterfaceClass = FindObject<UClass>(ANY_PACKAGE, *InterfaceName.ToString());
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
        // Use the static method to properly handle initialization
        if (!FCoreServiceLocator::bSingletonInitialized.AtomicSet(true))
        {
            // Create and initialize the singleton instance with proper access
            FCoreServiceLocator* NewSingleton = new FCoreServiceLocator();
            NewSingleton->Initialize();
            FCoreServiceLocator::Singleton = NewSingleton;
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
    UClass* MemoryManagerClass = FindObject<UClass>(ANY_PACKAGE, TEXT("UMemoryManager"));
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
    UClass* SVOAllocatorClass = FindObject<UClass>(ANY_PACKAGE, TEXT("UPoolAllocator"));
    RegisterService(SVOAllocator, SVOAllocatorClass ? SVOAllocatorClass : UObject::StaticClass());
    
    // Register it as a fast-path for critical operations
    RegisterFastPath(SVOAllocatorClass ? SVOAllocatorClass : UObject::StaticClass(), SVOAllocator);
    
    // Create and register Narrow Band Allocator for SDF fields with proper parameters
    FName NarrowBandPoolName(TEXT("DefaultNarrowBandPool"));
    FNarrowBandAllocator* NarrowBandAllocator = new FNarrowBandAllocator(NarrowBandPoolName, 128, 1024, EMemoryAccessPattern::SDFOperation, true);
    NarrowBandAllocator->Initialize();
    
    // Find interface class or use UObject as fallback
    UClass* NarrowBandClass = FindObject<UClass>(ANY_PACKAGE, TEXT("UPoolAllocator"));
    RegisterService(NarrowBandAllocator, NarrowBandClass ? NarrowBandClass : UObject::StaticClass());
    
    // Register it as a fast-path for critical operations
    RegisterFastPath(NarrowBandClass ? NarrowBandClass : UObject::StaticClass(), NarrowBandAllocator);
    
    // Create and register ZeroCopyBuffer for efficient GPU/CPU data sharing with proper parameters
    FName ZeroCopyBufferName(TEXT("DefaultZeroCopyBuffer"));
    FZeroCopyBuffer* ZeroCopyBuffer = new FZeroCopyBuffer(ZeroCopyBufferName, 1024*1024, EBufferUsage::General, false);
    ZeroCopyBuffer->Initialize();
    
    // Find interface class or use UObject as fallback
    UClass* ZeroCopyClass = FindObject<UClass>(ANY_PACKAGE, TEXT("UBufferProvider"));
    RegisterService(ZeroCopyBuffer, ZeroCopyClass ? ZeroCopyClass : UObject::StaticClass());
    
    // Register it as a fast-path for critical operations
    RegisterFastPath(ZeroCopyClass ? ZeroCopyClass : UObject::StaticClass(), ZeroCopyBuffer);
    
    // Create and register Memory Telemetry for performance tracking
    FMemoryTelemetry* MemoryTelemetry = new FMemoryTelemetry();
    MemoryTelemetry->Initialize();
    
    // Find interface class or use UObject as fallback
    UClass* TelemetryClass = FindObject<UClass>(ANY_PACKAGE, TEXT("UMemoryTracker"));
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
    FScopeLock Lock(&FastPathLock);
    
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
    FScopeLock Lock(&FastPathLock);
    
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