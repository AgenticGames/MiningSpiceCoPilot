// Copyright Epic Games, Inc. All Rights Reserved.

#include "1_CoreRegistry/Public/CoreServiceLocator.h"

// Initialize static members
FServiceLocator* FServiceLocator::Singleton = nullptr;
FThreadSafeBool FServiceLocator::bSingletonInitialized = false;

FServiceLocator::FServiceLocator()
    : bIsInitialized(false)
{
    // Constructor is intentionally minimal
}

FServiceLocator::~FServiceLocator()
{
    // Ensure we're properly shut down
    if (IsInitialized())
    {
        Shutdown();
    }
}

bool FServiceLocator::Initialize()
{
    bool bWasAlreadyInitialized = false;
    if (!bIsInitialized.AtomicSet(true, bWasAlreadyInitialized))
    {
        // Initialize any internal structures
        ServiceMap.Empty();
        return true;
    }
    
    // Already initialized
    return false;
}

void FServiceLocator::Shutdown()
{
    if (bIsInitialized)
    {
        // Lock service map for shutdown
        FScopeLock Lock(&ServiceMapLock);
        
        // Clear service registrations
        ServiceMap.Empty();
        
        // Mark as uninitialized
        bIsInitialized = false;
    }
}

bool FServiceLocator::IsInitialized() const
{
    return bIsInitialized;
}

bool FServiceLocator::RegisterService(void* InService, const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID)
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FServiceLocator::RegisterService failed - locator not initialized"));
        return false;
    }
    
    if (InService == nullptr || InInterfaceType == nullptr)
    {
        UE_LOG(LogTemp, Error, TEXT("FServiceLocator::RegisterService failed - invalid parameters"));
        return false;
    }
    
    // Generate unique key for this service
    FName ServiceKey = CreateServiceKey(InInterfaceType, InZoneID, InRegionID);
    
    // Lock for thread safety
    FScopeLock Lock(&ServiceMapLock);
    
    // Check if service is already registered
    if (ServiceMap.Contains(ServiceKey))
    {
        UE_LOG(LogTemp, Warning, TEXT("FServiceLocator::RegisterService - service already registered: %s"), *ServiceKey.ToString());
        return false;
    }
    
    // Register service
    ServiceMap.Add(ServiceKey, InService);
    
    return true;
}

void* FServiceLocator::ResolveService(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID)
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FServiceLocator::ResolveService failed - locator not initialized"));
        return nullptr;
    }
    
    if (InInterfaceType == nullptr)
    {
        UE_LOG(LogTemp, Error, TEXT("FServiceLocator::ResolveService failed - invalid interface type"));
        return nullptr;
    }
    
    // Layered resolution strategy:
    // 1. Try to resolve zone-specific, region-specific service
    // 2. Try to resolve region-specific service (any zone)
    // 3. Try to resolve global service
    
    void* Result = nullptr;
    
    // Lock for thread safety
    FScopeLock Lock(&ServiceMapLock);
    
    // Try zone+region specific first if both are specified
    if (InZoneID != INDEX_NONE && InRegionID != INDEX_NONE)
    {
        FName ZoneRegionKey = CreateServiceKey(InInterfaceType, InZoneID, InRegionID);
        Result = ServiceMap.FindRef(ZoneRegionKey);
        if (Result != nullptr)
        {
            return Result;
        }
    }
    
    // Try region-specific (any zone)
    if (InRegionID != INDEX_NONE)
    {
        FName RegionKey = CreateServiceKey(InInterfaceType, INDEX_NONE, InRegionID);
        Result = ServiceMap.FindRef(RegionKey);
        if (Result != nullptr)
        {
            return Result;
        }
    }
    
    // Try global service
    FName GlobalKey = CreateServiceKey(InInterfaceType, INDEX_NONE, INDEX_NONE);
    return ServiceMap.FindRef(GlobalKey);
}

bool FServiceLocator::UnregisterService(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID)
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FServiceLocator::UnregisterService failed - locator not initialized"));
        return false;
    }
    
    if (InInterfaceType == nullptr)
    {
        UE_LOG(LogTemp, Error, TEXT("FServiceLocator::UnregisterService failed - invalid interface type"));
        return false;
    }
    
    // Generate unique key for this service
    FName ServiceKey = CreateServiceKey(InInterfaceType, InZoneID, InRegionID);
    
    // Lock for thread safety
    FScopeLock Lock(&ServiceMapLock);
    
    // Attempt to remove entry
    return ServiceMap.Remove(ServiceKey) > 0;
}

bool FServiceLocator::HasService(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID) const
{
    if (!IsInitialized())
    {
        return false;
    }
    
    if (InInterfaceType == nullptr)
    {
        return false;
    }
    
    // Generate unique key for this service
    FName ServiceKey = CreateServiceKey(InInterfaceType, InZoneID, InRegionID);
    
    // Lock for thread safety
    FScopeLock Lock(&ServiceMapLock);
    
    // Check if service exists
    return ServiceMap.Contains(ServiceKey);
}

FServiceLocator& FServiceLocator::Get()
{
    // Thread-safe singleton initialization
    if (!bSingletonInitialized)
    {
        bool bWasAlreadyInitialized = false;
        if (!bSingletonInitialized.AtomicSet(true, bWasAlreadyInitialized))
        {
            Singleton = new FServiceLocator();
            Singleton->Initialize();
        }
    }
    
    check(Singleton != nullptr);
    return *Singleton;
}

FName FServiceLocator::CreateServiceKey(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID) const
{
    FString KeyString = InInterfaceType->GetName();
    
    // Append region ID if specified
    if (InRegionID != INDEX_NONE)
    {
        KeyString.Append(FString::Printf(TEXT("_R%d"), InRegionID));
    }
    
    // Append zone ID if specified
    if (InZoneID != INDEX_NONE)
    {
        KeyString.Append(FString::Printf(TEXT("_Z%d"), InZoneID));
    }
    
    return FName(*KeyString);
}

// Static implementation of IServiceLocator::Get()
IServiceLocator& IServiceLocator::Get()
{
    return FServiceLocator::Get();
}