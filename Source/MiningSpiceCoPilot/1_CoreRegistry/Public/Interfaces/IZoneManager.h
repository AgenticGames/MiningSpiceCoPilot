// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IZoneManager.generated.h"

/**
 * Interface for zone management services
 * Provides operations for managing zones within a region
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UZoneManager : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for zone managers
 * Implementations handle region-specific zone management
 */
class MININGSPICECOPILOT_API IZoneManager
{
    GENERATED_BODY()

public:
    /**
     * Gets the region identifier this manager is responsible for
     * @return Region identifier
     */
    virtual int32 GetRegionId() const = 0;
    
    /**
     * Creates a new zone
     * @param InBounds Bounds for the new zone
     * @param OutZoneId Output parameter for zone identifier
     * @return True if zone creation was successful
     */
    virtual bool CreateZone(const FBox& InBounds, int32& OutZoneId) = 0;
    
    /**
     * Removes a zone
     * @param InZoneId Zone identifier to remove
     * @return True if zone removal was successful
     */
    virtual bool RemoveZone(int32 InZoneId) = 0;
    
    /**
     * Gets the zone containing a specific world location
     * @param InWorldLocation World location to check
     * @param OutZoneId Output parameter for zone identifier
     * @return True if a zone was found
     */
    virtual bool GetZoneAtLocation(const FVector& InWorldLocation, int32& OutZoneId) const = 0;
    
    /**
     * Gets the bounds for a zone
     * @param InZoneId Zone identifier
     * @param OutBounds Output parameter for zone bounds
     * @return True if bounds were retrieved successfully
     */
    virtual bool GetZoneBounds(int32 InZoneId, FBox& OutBounds) const = 0;
    
    /**
     * Gets all zone identifiers within this region
     * @param OutZoneIds Array to receive zone identifiers
     * @return True if zone IDs were retrieved successfully
     */
    virtual bool GetAllZoneIds(TArray<int32>& OutZoneIds) const = 0;
    
    /**
     * Gets neighboring zones for a specific zone
     * @param InZoneId Zone identifier to get neighbors for
     * @param OutNeighborIds Array to receive neighboring zone identifiers
     * @return True if neighbor IDs were retrieved successfully
     */
    virtual bool GetNeighboringZones(int32 InZoneId, TArray<int32>& OutNeighborIds) const = 0;
    
    /**
     * Updates the zone grid configuration
     * @param InConfigName Configuration name
     * @param InZoneSize Size of each zone
     * @param InOverlapMargin Overlap margin between zones
     * @return True if configuration update was successful
     */
    virtual bool UpdateZoneGridConfig(const FName& InConfigName, const FVector& InZoneSize, float InOverlapMargin) = 0;
}; 