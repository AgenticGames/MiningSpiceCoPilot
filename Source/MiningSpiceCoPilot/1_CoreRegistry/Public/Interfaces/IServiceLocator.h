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
     * @param InService Pointer to the service implementation
     * @param InInterfaceType Type information for the service interface
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return True if registration was successful
     */
    virtual bool RegisterService(void* InService, const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) = 0;
    
    /**
     * Template method to register a typed service
     * @param InService Pointer to the service implementation
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return True if registration was successful
     */
    template<typename InterfaceType>
    bool RegisterService(InterfaceType* InService, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE)
    {
        return RegisterService(InService, InterfaceType::StaticClass(), InZoneID, InRegionID);
    }
    
    /**
     * Resolves a service instance based on interface type and optional context
     * @param InInterfaceType Type information for the service interface
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return Pointer to the service implementation or nullptr if not found
     */
    virtual void* ResolveService(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) = 0;
    
    /**
     * Template method to resolve a typed service
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return Typed pointer to the service implementation or nullptr if not found
     */
    template<typename InterfaceType>
    InterfaceType* ResolveService(int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE)
    {
        return static_cast<InterfaceType*>(ResolveService(InterfaceType::StaticClass(), InZoneID, InRegionID));
    }
    
    /**
     * Unregisters a service implementation
     * @param InInterfaceType Type information for the service interface
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return True if unregistration was successful
     */
    virtual bool UnregisterService(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) = 0;
    
    /**
     * Template method to unregister a typed service
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return True if unregistration was successful
     */
    template<typename InterfaceType>
    bool UnregisterService(int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE)
    {
        return UnregisterService(InterfaceType::StaticClass(), InZoneID, InRegionID);
    }
    
    /**
     * Checks if a service is registered
     * @param InInterfaceType Type information for the service interface
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return True if service is registered
     */
    virtual bool HasService(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) const = 0;
    
    /**
     * Template method to check if a typed service is registered
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return True if service is registered
     */
    template<typename InterfaceType>
    bool HasService(int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) const
    {
        return HasService(InterfaceType::StaticClass(), InZoneID, InRegionID);
    }
    
    /**
     * Gets the singleton instance of the service locator
     * @return Reference to the service locator instance
     */
    static IServiceLocator& Get();
};