// MaterialServiceProvider.h
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ServiceProvider.h"
#include "Containers/Map.h"
#include "Containers/Set.h"

/**
 * Specialized service provider for material-specific components
 * Manages services related to material properties, field operations, and interactions
 */
class FMaterialServiceProvider : public FServiceProvider
{
public:
    /** Constructor */
    FMaterialServiceProvider();
    
    /** Destructor */
    virtual ~FMaterialServiceProvider();

    //~ Begin FServiceProvider Interface
    virtual bool Initialize() override;
    virtual void Shutdown() override;
    virtual bool RegisterServices() override;
    virtual void UnregisterServices() override;
    //~ End FServiceProvider Interface

    /** Get singleton instance */
    static FMaterialServiceProvider& Get();

private:
    /** Singleton instance */
    static FMaterialServiceProvider* Instance;
    
    /**
     * Register material property services
     * @return True if registration was successful
     */
    bool RegisterPropertyServices();
    
    /**
     * Register material field operation services
     * @return True if registration was successful
     */
    bool RegisterFieldOperationServices();
    
    /**
     * Register material interaction services
     * @return True if registration was successful
     */
    bool RegisterInteractionServices();
    
    /**
     * Register material resource services
     * @return True if registration was successful
     */
    bool RegisterResourceServices();
};