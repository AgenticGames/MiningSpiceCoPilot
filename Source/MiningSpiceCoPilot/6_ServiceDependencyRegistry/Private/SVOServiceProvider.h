// SVOServiceProvider.h
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ServiceProvider.h"
#include "Containers/Map.h"
#include "Containers/Set.h"

/**
 * Specialized service provider for SVO+SDF hybrid volume components
 * Manages services related to node managers, field operators, and serializers
 */
class FSVOServiceProvider : public FServiceProvider
{
public:
    /** Constructor */
    FSVOServiceProvider();
    
    /** Destructor */
    virtual ~FSVOServiceProvider();

    //~ Begin FServiceProvider Interface
    virtual bool Initialize() override;
    virtual void Shutdown() override;
    virtual bool RegisterServices() override;
    virtual void UnregisterServices() override;
    //~ End FServiceProvider Interface

    /** Get singleton instance */
    static FSVOServiceProvider& Get();

private:
    /** Singleton instance */
    static FSVOServiceProvider* Instance;
    
    /**
     * Register SVO volume related services
     * @return True if registration was successful
     */
    bool RegisterVolumeServices();
    
    /**
     * Register SVO node manager services
     * @return True if registration was successful
     */
    bool RegisterNodeManagerServices();
    
    /**
     * Register SDF field operator services
     * @return True if registration was successful
     */
    bool RegisterFieldOperatorServices();
    
    /**
     * Register serialization services
     * @return True if registration was successful
     */
    bool RegisterSerializationServices();
};