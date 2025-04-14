// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreServiceLocator.h"

/**
 * Concrete implementation of the service locator
 */
class FServiceLocator : public ICoreServiceLocator
{
public:
    /** Constructor */
    FServiceLocator();
    
    /** Destructor */
    virtual ~FServiceLocator();
    
    // Begin ICoreServiceLocator interface
    virtual bool RegisterService(const FName& ServiceName, TScriptInterface<UObject> ServiceInstance) override;
    virtual bool UnregisterService(const FName& ServiceName) override;
    virtual TScriptInterface<UObject> GetService(const FName& ServiceName) override;
    virtual bool HasService(const FName& ServiceName) const override;
    virtual TArray<FName> GetAllServiceNames() const override;
    // End ICoreServiceLocator interface
    
    /** Initialize the service locator */
    bool Initialize();
    
    /** Shutdown the service locator */
    void Shutdown();
    
    /** Check if the service locator is initialized */
    bool IsInitialized() const;
    
    /** Get singleton instance */
    static FServiceLocator& Get();
    
private:
    /** Map of service instances by name */
    TMap<FName, TScriptInterface<UObject>> ServiceMap;
    
    /** Lock for thread-safe access to the service map */
    mutable FCriticalSection ServiceMapLock;
    
    /** Flag indicating whether the locator is initialized */
    FThreadSafeBool bIsInitialized;
    
    /** Singleton instance */
    static FServiceLocator* Singleton;
    
    /** Thread-safe initialization flag for the singleton */
    static FThreadSafeBool bSingletonInitialized;
};

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
    // Check if already initialized
    if (bIsInitialized)
    {
        return false;
    }
    
    // Set initialized flag
    bIsInitialized.AtomicSet(true);
    
    // Initialize internal maps
    ServiceMap.Empty();
    
    return true;
}

void FServiceLocator::Shutdown()
{
    if (bIsInitialized)
    {
        // Lock for thread safety
        FScopeLock Lock(&ServiceMapLock);
        
        // Clear all registered services
        ServiceMap.Empty();
        
        // Reset state
        bIsInitialized = false;
    }
}

bool FServiceLocator::IsInitialized() const
{
    return bIsInitialized;
}

bool FServiceLocator::RegisterService(const FName& ServiceName, TScriptInterface<UObject> ServiceInstance)
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FServiceLocator::RegisterService failed - locator not initialized"));
        return false;
    }
    
    if (ServiceName.IsNone() || !ServiceInstance.GetObject())
    {
        UE_LOG(LogTemp, Error, TEXT("FServiceLocator::RegisterService failed - invalid parameters"));
        return false;
    }
    
    // Lock for thread safety
    FScopeLock Lock(&ServiceMapLock);
    
    // Check if service is already registered
    if (ServiceMap.Contains(ServiceName))
    {
        UE_LOG(LogTemp, Warning, TEXT("FServiceLocator::RegisterService - service already registered: %s"), *ServiceName.ToString());
        return false;
    }
    
    // Register service
    ServiceMap.Add(ServiceName, ServiceInstance);
    
    UE_LOG(LogTemp, Verbose, TEXT("FServiceLocator::RegisterService - registered service: %s"), *ServiceName.ToString());
    return true;
}

bool FServiceLocator::UnregisterService(const FName& ServiceName)
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FServiceLocator::UnregisterService failed - locator not initialized"));
        return false;
    }
    
    if (ServiceName.IsNone())
    {
        UE_LOG(LogTemp, Error, TEXT("FServiceLocator::UnregisterService failed - invalid service name"));
        return false;
    }
    
    // Lock for thread safety
    FScopeLock Lock(&ServiceMapLock);
    
    // Attempt to remove the service
    int32 NumRemoved = ServiceMap.Remove(ServiceName);
    
    if (NumRemoved > 0)
    {
        UE_LOG(LogTemp, Verbose, TEXT("FServiceLocator::UnregisterService - unregistered service: %s"), *ServiceName.ToString());
        return true;
    }
    
    UE_LOG(LogTemp, Warning, TEXT("FServiceLocator::UnregisterService - service not found: %s"), *ServiceName.ToString());
    return false;
}

TScriptInterface<UObject> FServiceLocator::GetService(const FName& ServiceName)
{
    if (!IsInitialized())
    {
        UE_LOG(LogTemp, Error, TEXT("FServiceLocator::GetService failed - locator not initialized"));
        return TScriptInterface<UObject>();
    }
    
    if (ServiceName.IsNone())
    {
        UE_LOG(LogTemp, Error, TEXT("FServiceLocator::GetService failed - invalid service name"));
        return TScriptInterface<UObject>();
    }
    
    // Lock for thread safety
    FScopeLock Lock(&ServiceMapLock);
    
    // Look up the service
    TScriptInterface<UObject>* ServicePtr = ServiceMap.Find(ServiceName);
    if (ServicePtr)
    {
        return *ServicePtr;
    }
    
    UE_LOG(LogTemp, Verbose, TEXT("FServiceLocator::GetService - service not found: %s"), *ServiceName.ToString());
    return TScriptInterface<UObject>();
}

bool FServiceLocator::HasService(const FName& ServiceName) const
{
    if (!IsInitialized())
    {
        return false;
    }
    
    if (ServiceName.IsNone())
    {
        return false;
    }
    
    // Lock for thread safety
    FScopeLock Lock(&ServiceMapLock);
    
    // Check if service exists
    return ServiceMap.Contains(ServiceName);
}

TArray<FName> FServiceLocator::GetAllServiceNames() const
{
    TArray<FName> Result;
    
    if (!IsInitialized())
    {
        return Result;
    }
    
    // Lock for thread safety
    FScopeLock Lock(&ServiceMapLock);
    
    // Get all service names
    ServiceMap.GetKeys(Result);
    
    return Result;
}

FServiceLocator& FServiceLocator::Get()
{
    // Thread-safe singleton initialization
    if (!bSingletonInitialized)
    {
        if (!bSingletonInitialized.AtomicSet(true))
        {
            Singleton = new FServiceLocator();
            Singleton->Initialize();
        }
    }
    
    check(Singleton != nullptr);
    return *Singleton;
}

// Simplified global interface implementation - we'll use a standard singleton pattern instead of TGlobalResource
ICoreServiceLocator& ICoreServiceLocator::Get()
{
    return FServiceLocator::Get();
}