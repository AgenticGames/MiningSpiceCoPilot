// ServiceLocator.cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "DependencyServiceLocator.h"
#include "Interfaces/IServiceMonitor.h"
#include "Logging/LogMacros.h"

// Static instance initialization
FServiceLocator* FServiceLocator::Instance = nullptr;

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogServiceLocator, Log, All);

FServiceLocator::FServiceLocator()
    : bIsInitialized(false)
{
    // Initialize empty
}

FServiceLocator::~FServiceLocator()
{
    // Ensure we're shut down
    if (bIsInitialized)
    {
        Shutdown();
    }
}

bool FServiceLocator::Initialize()
{
    FScopeLock Lock(&CriticalSection);
    
    if (bIsInitialized)
    {
        UE_LOG(LogServiceLocator, Warning, TEXT("ServiceLocator already initialized"));
        return true;
    }
    
    UE_LOG(LogServiceLocator, Log, TEXT("Initializing ServiceLocator"));
    RegisteredServices.Empty();
    ServiceCache.Empty();
    bIsInitialized = true;
    
    return true;
}

void FServiceLocator::Shutdown()
{
    FScopeLock Lock(&CriticalSection);
    
    if (!bIsInitialized)
    {
        UE_LOG(LogServiceLocator, Warning, TEXT("ServiceLocator not initialized, cannot shutdown"));
        return;
    }
    
    UE_LOG(LogServiceLocator, Log, TEXT("Shutting down ServiceLocator"));
    RegisteredServices.Empty();
    ServiceCache.Empty();
    bIsInitialized = false;
}

bool FServiceLocator::IsInitialized() const
{
    return bIsInitialized;
}

bool FServiceLocator::RegisterService(void* InService, const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID)
{
    if (!InService)
    {
        UE_LOG(LogServiceLocator, Error, TEXT("Cannot register null service"));
        return false;
    }
    
    if (!InInterfaceType)
    {
        UE_LOG(LogServiceLocator, Error, TEXT("Cannot register service with null interface type"));
        return false;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    if (!bIsInitialized)
    {
        UE_LOG(LogServiceLocator, Error, TEXT("ServiceLocator not initialized, cannot register service for interface %s"),
            *InInterfaceType->GetName());
        return false;
    }
    
    // Create service key
    FServiceKey Key(InInterfaceType, InZoneID, InRegionID);
    
    // Check if service already registered
    if (RegisteredServices.Contains(Key))
    {
        UE_LOG(LogServiceLocator, Warning, TEXT("Service for interface %s already registered, overwriting"),
            *Key.ToString());
            
        // Remove existing entry from cache
        ServiceCache.Remove(Key);
    }
    
    // Register service
    RegisteredServices.Add(Key, FServiceEntry(InService));
    
    // Add to cache for faster resolution
    ServiceCache.Add(Key, InService);
    
    UE_LOG(LogServiceLocator, Log, TEXT("Registered service for interface %s"), *Key.ToString());
    
    return true;
}

void* FServiceLocator::ResolveService(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID)
{
    if (!InInterfaceType)
    {
        UE_LOG(LogServiceLocator, Error, TEXT("Cannot resolve service with null interface type"));
        return nullptr;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    if (!bIsInitialized)
    {
        UE_LOG(LogServiceLocator, Error, TEXT("ServiceLocator not initialized, cannot resolve service for interface %s"),
            *InInterfaceType->GetName());
        return nullptr;
    }
    
    // Create service key
    FServiceKey Key(InInterfaceType, InZoneID, InRegionID);
    
    // Check cache first for faster resolution
    void** CachedService = ServiceCache.Find(Key);
    if (CachedService)
    {
        return *CachedService;
    }
    
    // Find service with fallback to more general scopes
    const FServiceEntry* Entry = FindServiceWithFallback(Key);
    if (!Entry)
    {
        // Service not found
        UE_LOG(LogServiceLocator, Verbose, TEXT("Service for interface %s not found"), *Key.ToString());
        return nullptr;
    }
    
    // Add to cache for faster resolution next time
    ServiceCache.Add(Key, Entry->ServicePtr);
    
    return Entry->ServicePtr;
}

bool FServiceLocator::UnregisterService(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID)
{
    if (!InInterfaceType)
    {
        UE_LOG(LogServiceLocator, Error, TEXT("Cannot unregister service with null interface type"));
        return false;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    if (!bIsInitialized)
    {
        UE_LOG(LogServiceLocator, Error, TEXT("ServiceLocator not initialized, cannot unregister service for interface %s"),
            *InInterfaceType->GetName());
        return false;
    }
    
    // Create service key
    FServiceKey Key(InInterfaceType, InZoneID, InRegionID);
    
    // Check if service is registered
    if (!RegisteredServices.Contains(Key))
    {
        UE_LOG(LogServiceLocator, Warning, TEXT("Service for interface %s not registered, cannot unregister"),
            *Key.ToString());
        return false;
    }
    
    // Unregister service
    RegisteredServices.Remove(Key);
    
    // Remove from cache
    ServiceCache.Remove(Key);
    
    UE_LOG(LogServiceLocator, Log, TEXT("Unregistered service for interface %s"), *Key.ToString());
    
    return true;
}

bool FServiceLocator::HasService(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID) const
{
    if (!InInterfaceType)
    {
        return false;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    if (!bIsInitialized)
    {
        return false;
    }
    
    // Create service key
    FServiceKey Key(InInterfaceType, InZoneID, InRegionID);
    
    // Find service with fallback to more general scopes
    const FServiceEntry* Entry = FindServiceWithFallback(Key);
    
    return Entry != nullptr;
}

FServiceLocator::FServiceEntry* FServiceLocator::FindServiceWithFallback(const FServiceKey& InKey)
{
    // Try exact match first
    FServiceEntry* ExactEntry = RegisteredServices.Find(InKey);
    if (ExactEntry)
    {
        return ExactEntry;
    }
    
    // For zone-specific services, try global services if zone-specific not found
    if (InKey.ZoneID != INDEX_NONE)
    {
        FServiceKey ZoneGlobalKey(InKey.InterfaceType, INDEX_NONE, InKey.RegionID);
        FServiceEntry* ZoneGlobalEntry = RegisteredServices.Find(ZoneGlobalKey);
        if (ZoneGlobalEntry)
        {
            return ZoneGlobalEntry;
        }
    }
    
    // For region-specific services, try global services if region-specific not found
    if (InKey.RegionID != INDEX_NONE)
    {
        FServiceKey RegionGlobalKey(InKey.InterfaceType, InKey.ZoneID, INDEX_NONE);
        FServiceEntry* RegionGlobalEntry = RegisteredServices.Find(RegionGlobalKey);
        if (RegionGlobalEntry)
        {
            return RegionGlobalEntry;
        }
    }
    
    // Finally, try completely global services
    if (InKey.ZoneID != INDEX_NONE || InKey.RegionID != INDEX_NONE)
    {
        FServiceKey GlobalKey(InKey.InterfaceType, INDEX_NONE, INDEX_NONE);
        FServiceEntry* GlobalEntry = RegisteredServices.Find(GlobalKey);
        if (GlobalEntry)
        {
            return GlobalEntry;
        }
    }
    
    // Service not found
    return nullptr;
}

const FServiceLocator::FServiceEntry* FServiceLocator::FindServiceWithFallback(const FServiceKey& InKey) const
{
    // Try exact match first
    const FServiceEntry* ExactEntry = RegisteredServices.Find(InKey);
    if (ExactEntry)
    {
        return ExactEntry;
    }
    
    // For zone-specific services, try global services if zone-specific not found
    if (InKey.ZoneID != INDEX_NONE)
    {
        FServiceKey ZoneGlobalKey(InKey.InterfaceType, INDEX_NONE, InKey.RegionID);
        const FServiceEntry* ZoneGlobalEntry = RegisteredServices.Find(ZoneGlobalKey);
        if (ZoneGlobalEntry)
        {
            return ZoneGlobalEntry;
        }
    }
    
    // For region-specific services, try global services if region-specific not found
    if (InKey.RegionID != INDEX_NONE)
    {
        FServiceKey RegionGlobalKey(InKey.InterfaceType, InKey.ZoneID, INDEX_NONE);
        const FServiceEntry* RegionGlobalEntry = RegisteredServices.Find(RegionGlobalKey);
        if (RegionGlobalEntry)
        {
            return RegionGlobalEntry;
        }
    }
    
    // Finally, try completely global services
    if (InKey.ZoneID != INDEX_NONE || InKey.RegionID != INDEX_NONE)
    {
        FServiceKey GlobalKey(InKey.InterfaceType, INDEX_NONE, INDEX_NONE);
        const FServiceEntry* GlobalEntry = RegisteredServices.Find(GlobalKey);
        if (GlobalEntry)
        {
            return GlobalEntry;
        }
    }
    
    // Service not found
    return nullptr;
}

void FServiceLocator::ClearServiceCache()
{
    FScopeLock Lock(&CriticalSection);
    ServiceCache.Empty();
}

FServiceLocator& FServiceLocator::Get()
{
    if (!Instance)
    {
        Instance = new FServiceLocator();
        Instance->Initialize();
    }
    
    return *Instance;
}

// Implementation of IDependencyServiceLocator::Get()
IDependencyServiceLocator& IDependencyServiceLocator::Get()
{
    return FServiceLocator::Get();
}