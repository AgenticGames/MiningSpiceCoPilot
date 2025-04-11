// IServiceLocator.h
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IServiceLocator.generated.h"

/**
 * Base interface for the service locator
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UServiceLocator : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for service registration and resolution in the SVO+SDF mining architecture
 * Provides lifecycle management and dependency resolution for subsystems
 */
class MININGSPICECOPILOT_API IServiceLocator
{
    GENERATED_BODY()

public:
    /**
     * Initialize the service locator
     * @return True if successfully initialized
     */
    virtual bool Initialize() = 0;
    
    /**
     * Shutdown the service locator and cleanup resources
     */
    virtual void Shutdown() = 0;
    
    /**
     * Check if the service locator is initialized
     * @return True if initialized
     */
    virtual bool IsInitialized() const = 0;
    
    /**
     * Register a service implementation
     * @param InService Pointer to the service implementation
     * @param InInterfaceType Interface class that the service implements
     * @param InZoneID Optional zone ID for zone-specific services
     * @param InRegionID Optional region ID for region-specific services
     * @return True if registration was successful
     */
    virtual bool RegisterService(void* InService, const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) = 0;
    
    /**
     * Register a service implementation with type safety
     * @param InService Reference to the service implementation
     * @param InZoneID Optional zone ID for zone-specific services
     * @param InRegionID Optional region ID for region-specific services
     * @return True if registration was successful
     */
    template<typename ServiceType>
    bool RegisterService(ServiceType* InService, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE)
    {
        return RegisterService(InService, ServiceType::StaticClass(), InZoneID, InRegionID);
    }
    
    /**
     * Resolve a service instance
     * @param InInterfaceType Interface class to resolve
     * @param InZoneID Optional zone ID for zone-specific services
     * @param InRegionID Optional region ID for region-specific services
     * @return Pointer to the service instance or nullptr if not found
     */
    virtual void* ResolveService(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) = 0;
    
    /**
     * Resolve a service instance with type safety
     * @param InZoneID Optional zone ID for zone-specific services
     * @param InRegionID Optional region ID for region-specific services
     * @return Typed pointer to the service instance or nullptr if not found
     */
    template<typename ServiceType>
    ServiceType* ResolveService(int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE)
    {
        return static_cast<ServiceType*>(ResolveService(ServiceType::StaticClass(), InZoneID, InRegionID));
    }
    
    /**
     * Unregister a service implementation
     * @param InInterfaceType Interface class to unregister
     * @param InZoneID Optional zone ID for zone-specific services
     * @param InRegionID Optional region ID for region-specific services
     * @return True if unregistration was successful
     */
    virtual bool UnregisterService(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) = 0;
    
    /**
     * Unregister a service implementation with type safety
     * @param InZoneID Optional zone ID for zone-specific services
     * @param InRegionID Optional region ID for region-specific services
     * @return True if unregistration was successful
     */
    template<typename ServiceType>
    bool UnregisterService(int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE)
    {
        return UnregisterService(ServiceType::StaticClass(), InZoneID, InRegionID);
    }
    
    /**
     * Check if a service is registered
     * @param InInterfaceType Interface class to check
     * @param InZoneID Optional zone ID for zone-specific services
     * @param InRegionID Optional region ID for region-specific services
     * @return True if the service is registered
     */
    virtual bool HasService(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) const = 0;
    
    /**
     * Check if a service is registered with type safety
     * @param InZoneID Optional zone ID for zone-specific services
     * @param InRegionID Optional region ID for region-specific services
     * @return True if the service is registered
     */
    template<typename ServiceType>
    bool HasService(int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) const
    {
        return HasService(ServiceType::StaticClass(), InZoneID, InRegionID);
    }
    
    /**
     * Get the singleton instance
     * @return Reference to the service locator instance
     */
    static IServiceLocator& Get();
};
