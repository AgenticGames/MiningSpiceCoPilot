// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "1_CoreRegistry/Public/Interfaces/IServiceLocator.h"
#include "1_CoreRegistry/Public/Interfaces/IServiceProvider.h"
#include "Templates/Atomic.h"
#include "Containers/Map.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/ThreadSafeCounter.h"
#include "Misc/SpinLock.h"

/**
 * Implementation of the IServiceLocator interface that provides
 * service registration and resolution for the mining subsystems.
 * Supports zone and region-specific service instances with thread-safe operations.
 */
class MININGSPICECOPILOT_API FServiceLocator : public IServiceLocator
{
public:
    /** Default constructor */
    FServiceLocator();
    
    /** Destructor */
    virtual ~FServiceLocator();
    
    //~ Begin IServiceLocator Interface
    virtual bool Initialize() override;
    virtual void Shutdown() override;
    virtual bool IsInitialized() const override;
    virtual bool RegisterService(void* InService, const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) override;
    virtual void* ResolveService(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) override;
    virtual bool UnregisterService(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) override;
    virtual bool HasService(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) const override;
    //~ End IServiceLocator Interface
    
    /** Gets the singleton instance of the service locator */
    static FServiceLocator& Get();
    
private:
    /** Creates a unique key for service lookup based on interface type and context */
    FName CreateServiceKey(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID) const;
    
    /** Map of registered services with context-specific keys */
    TMap<FName, void*> ServiceMap;
    
    /** Thread-safe flag indicating if the locator has been initialized */
    FThreadSafeBool bIsInitialized;
    
    /** Lock for thread-safe access to the service map */
    mutable FSpinLock ServiceMapLock;
    
    /** Singleton instance of the service locator */
    static FServiceLocator* Singleton;
    
    /** Thread-safe initialization flag for the singleton */
    static FThreadSafeBool bSingletonInitialized;
};