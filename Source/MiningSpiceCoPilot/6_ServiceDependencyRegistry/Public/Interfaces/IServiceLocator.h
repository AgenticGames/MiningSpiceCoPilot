// IServiceLocator.h
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IServiceLocator.generated.h"

/**
 * Base interface for service locator in the SVO+SDF mining architecture
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UServiceLocator : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for service locator in the SVO+SDF mining architecture
 * Provides service registration, resolution, and lifecycle management for subsystems
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
     * @param InService Pointer to service implementation
     * @param InInterfaceType Interface type class for the service
     * @param InZoneID Optional zone ID for zone-specific services
     * @param InRegionID Optional region ID for region-specific services
     * @return True if registration successful
     */
    virtual bool RegisterService(void* InService, const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) = 0;
    
    /**
     * Template helper for type-safe service registration
     * @param InService Typed pointer to service implementation
     * @param InZoneID Optional zone ID for zone-specific services
     * @param InRegionID Optional region ID for region-specific services
     * @return True if registration successful
     */
    template<typename ServiceType>
    bool RegisterService(ServiceType* InService, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE)
    {
        return RegisterService(static_cast<void*>(InService), ServiceType::StaticClass(), InZoneID, InRegionID);
    }
    
    /**
     * Resolve a service instance
     * @param InInterfaceType Interface type class to resolve
     * @param InZoneID Optional zone ID for zone-specific services
     * @param InRegionID Optional region ID for region-specific services
     * @return Pointer to resolved service or nullptr if not found
     */
    virtual void* ResolveService(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) = 0;
    
    /**
     * Template helper for type-safe service resolution
     * @param InZoneID Optional zone ID for zone-specific services
     * @param InRegionID Optional region ID for region-specific services
     * @return Typed pointer to resolved service or nullptr if not found
     */
    template<typename ServiceType>
    ServiceType* ResolveService(int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE)
    {
        return static_cast<ServiceType*>(ResolveService(ServiceType::StaticClass(), InZoneID, InRegionID));
    }
    
    /**
     * Unregister a service implementation
     * @param InInterfaceType Interface type class to unregister
     * @param InZoneID Optional zone ID for zone-specific services
     * @param InRegionID Optional region ID for region-specific services
     * @return True if service was unregistered
     */
    virtual bool UnregisterService(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) = 0;
    
    /**
     * Template helper for type-safe service unregistration
     * @param InZoneID Optional zone ID for zone-specific services
     * @param InRegionID Optional region ID for region-specific services
     * @return True if service was unregistered
     */
    template<typename ServiceType>
    bool UnregisterService(int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE)
    {
        return UnregisterService(ServiceType::StaticClass(), InZoneID, InRegionID);
    }
    
    /**
     * Check if a service is registered
     * @param InInterfaceType Interface type class to check
     * @param InZoneID Optional zone ID for zone-specific services
     * @param InRegionID Optional region ID for region-specific services
     * @return True if service is registered
     */
    virtual bool HasService(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) const = 0;
    
    /**
     * Template helper for type-safe service checking
     * @param InZoneID Optional zone ID for zone-specific services
     * @param InRegionID Optional region ID for region-specific services
     * @return True if service is registered
     */
    template<typename ServiceType>
    bool HasService(int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) const
    {
        return HasService(ServiceType::StaticClass(), InZoneID, InRegionID);
    }
    
    /**
     * Get the singleton instance of the service locator
     * @return Reference to the service locator instance
     */
    static IServiceLocator& Get();
};
