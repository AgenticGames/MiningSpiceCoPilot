// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"
#include "../1_CoreRegistry/Public/CommonServiceTypes.h"
#include "../1_CoreRegistry/Public/Interfaces/IService.h"

/**
 * Helper functions for the Service Registry test harness
 */
namespace ServiceRegistryTest
{
    /**
     * Get a valid UClass for use with the service registry system
     * For testing purposes, we use existing engine classes as stand-ins
     * In a real system, these would be proper UObject-derived classes with UCLASS()
     */
    static UClass* GetServiceClass(const FName& ServiceName)
    {
        // Cache the classes to avoid lookups
        static TMap<FName, UClass*> ClassCache;
        
        if (UClass* FoundClass = ClassCache.FindRef(ServiceName))
        {
            return FoundClass;
        }
        
        // Use known engine classes as stand-ins based on the service name
        UClass* StandInClass = nullptr;
        
        if (ServiceName == TEXT("LoggingService"))
        {
            StandInClass = FindObject<UClass>(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("Actor")));
        }
        else if (ServiceName == TEXT("DataProcessorService"))
        {
            StandInClass = FindObject<UClass>(FTopLevelAssetPath(TEXT("/Script/CoreUObject"), TEXT("Object")));
        }
        else if (ServiceName.ToString().Contains(TEXT("ZoneVoxelService")))
        {
            StandInClass = FindObject<UClass>(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("Actor")));
        }
        else
        {
            // Default fallback
            StandInClass = FindObject<UClass>(FTopLevelAssetPath(TEXT("/Script/CoreUObject"), TEXT("Object")));
        }
        
        // Cache and return
        ClassCache.Add(ServiceName, StandInClass);
        return StandInClass;
    }
    
    /**
     * Creates a service key from service name and context
     */
    static FName CreateServiceKey(const FName& ServiceName, int32 ZoneID = INDEX_NONE, int32 RegionID = INDEX_NONE)
    {
        if (ZoneID == INDEX_NONE && RegionID == INDEX_NONE)
        {
            return ServiceName;
        }
        else if (RegionID == INDEX_NONE)
        {
            return FName(*FString::Printf(TEXT("%s_Zone%d"), *ServiceName.ToString(), ZoneID));
        }
        else if (ZoneID == INDEX_NONE)
        {
            return FName(*FString::Printf(TEXT("%s_Region%d"), *ServiceName.ToString(), RegionID));
        }
        else
        {
            return FName(*FString::Printf(TEXT("%s_Zone%d_Region%d"), *ServiceName.ToString(), ZoneID, RegionID));
        }
    }
} 