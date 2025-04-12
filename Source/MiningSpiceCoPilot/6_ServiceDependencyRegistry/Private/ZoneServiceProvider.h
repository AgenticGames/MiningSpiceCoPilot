// ZoneServiceProvider.h
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ServiceProvider.h"
#include "Containers/Map.h"
#include "Containers/Set.h"

/**
 * Specialized service provider for zone-based transaction components
 * Manages services related to zone transactions, mining operations, and boundary handling
 */
class FZoneServiceProvider : public FServiceProvider
{
public:
    /** Constructor */
    FZoneServiceProvider();
    
    /** Destructor */
    virtual ~FZoneServiceProvider();

    //~ Begin FServiceProvider Interface
    virtual bool Initialize() override;
    virtual void Shutdown() override;
    virtual bool RegisterServices() override;
    virtual void UnregisterServices() override;
    //~ End FServiceProvider Interface

    /** Get singleton instance */
    static FZoneServiceProvider& Get();

private:
    /** Singleton instance */
    static FZoneServiceProvider* Instance;
    
    /**
     * Register zone transaction services
     * @return True if registration was successful
     */
    bool RegisterTransactionServices();
    
    /**
     * Register zone mining operation services
     * @return True if registration was successful
     */
    bool RegisterMiningOperationServices();
    
    /**
     * Register zone boundary services
     * @return True if registration was successful
     */
    bool RegisterBoundaryServices();
    
    /**
     * Register zone authority services for multiplayer support
     * @return True if registration was successful
     */
    bool RegisterAuthorityServices();
};