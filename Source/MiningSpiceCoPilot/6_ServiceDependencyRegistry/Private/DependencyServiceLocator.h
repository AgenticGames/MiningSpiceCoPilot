// ServiceLocator.h
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IDependencyServiceLocator.h"
#include "HAL/CriticalSection.h"
#include "Containers/Map.h"
#include "Containers/Set.h"
#include "Templates/SharedPointer.h"

/**
 * Implementation of IDependencyServiceLocator interface
 * Provides service registration, resolution, and lifecycle management 
 */
class FServiceLocator : public IDependencyServiceLocator
{
public:
    /** Constructor */
    FServiceLocator();
    
    /** Destructor */
    virtual ~FServiceLocator();

    //~ Begin IDependencyServiceLocator Interface
    virtual bool Initialize() override;
    virtual void Shutdown() override;
    virtual bool IsInitialized() const override;
    virtual bool RegisterService(void* InService, const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) override;
    virtual void* ResolveService(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) override;
    virtual bool UnregisterService(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) override;
    virtual bool HasService(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) const override;
    //~ End IDependencyServiceLocator Interface

    /** Get singleton instance */
    static FServiceLocator& Get();

private:
    /** Key for service lookup */

    // Hash function for FServiceKey
    friend uint32 GetTypeHash(const FServiceKey& Key)
    {
        return HashCombine(GetTypeHash(Key.ServiceType), GetTypeHash(Key.ServiceName));
    }


        /** Equality operator */
        bool operator==(const FServiceKey& Other) const
        {
            return InterfaceType == Other.InterfaceType &&
                   ZoneID == Other.ZoneID &&
                   RegionID == Other.RegionID;
        }

        /** Less than operator for map sorting */
        bool operator<(const FServiceKey& Other) const
        {
            if (InterfaceType != Other.InterfaceType)
                return InterfaceType < Other.InterfaceType;
            if (ZoneID != Other.ZoneID)
                return ZoneID < Other.ZoneID;
            return RegionID < Other.RegionID;
        }

        /** Get string representation for logging */
        FString ToString() const
        {
            return FString::Printf(TEXT("%s (Zone: %d, Region: %d)"), 
                *InterfaceType->GetName(), ZoneID, RegionID);
        }
    };

    /** Service registry entry */
    struct FServiceEntry
    {
        /** Pointer to the service instance */
        void* ServicePtr;
        
        /** Time when the service was registered */
        FDateTime RegistrationTime;

        /** Constructor */
        FServiceEntry(void* InServicePtr)
            : ServicePtr(InServicePtr)
            , RegistrationTime(FDateTime::Now())
        {
        }
    };

    /** Map of registered services */
    TMap<FServiceKey, FServiceEntry> RegisteredServices;

    /** Cache of resolved services for performance */
    mutable TMap<FServiceKey, void*> ServiceCache;

    /** Critical section for thread safety */
    mutable FCriticalSection CriticalSection;

    /** Flag indicating if the locator is initialized */
    bool bIsInitialized;

    /** Singleton instance */
    static FServiceLocator* Instance;
    
    /**
     * Find a service in the registry with fallback search for global services
     * @param InKey Key to search for
     * @return Found service entry or nullptr if not found
     */
    FServiceEntry* FindServiceWithFallback(const FServiceKey& InKey);
    
    /**
     * Find a service in the registry with fallback search for global services (const version)
     * @param InKey Key to search for
     * @return Found service entry or nullptr if not found
     */
    const FServiceEntry* FindServiceWithFallback(const FServiceKey& InKey) const;
    
    /**
     * Clear the service cache
     */
    void ClearServiceCache();
};
