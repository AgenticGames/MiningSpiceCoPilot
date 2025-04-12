// MaterialServiceProvider.cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "MaterialServiceProvider.h"
#include "Interfaces/IDependencyServiceLocator.h"
#include "Interfaces/IServiceMonitor.h"
#include "Logging/LogMacros.h"

// Static instance initialization
FMaterialServiceProvider* FMaterialServiceProvider::Instance = nullptr;

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogMaterialServiceProvider, Log, All);

FMaterialServiceProvider::FMaterialServiceProvider()
    : FServiceProvider(TEXT("MaterialServiceProvider"))
{
    // Initialize empty
}

FMaterialServiceProvider::~FMaterialServiceProvider()
{
    // Ensure we're shut down
    if (IsInitialized())
    {
        Shutdown();
    }
}

bool FMaterialServiceProvider::Initialize()
{
    bool bResult = FServiceProvider::Initialize();
    
    if (bResult)
    {
        UE_LOG(LogMaterialServiceProvider, Log, TEXT("Initialized MaterialServiceProvider"));
    }
    
    return bResult;
}

void FMaterialServiceProvider::Shutdown()
{
    UE_LOG(LogMaterialServiceProvider, Log, TEXT("Shutting down MaterialServiceProvider"));
    FServiceProvider::Shutdown();
}

bool FMaterialServiceProvider::RegisterServices()
{
    bool bSuccess = true;
    
    // Register different types of services
    bSuccess &= RegisterPropertyServices();
    bSuccess &= RegisterFieldOperationServices();
    bSuccess &= RegisterInteractionServices();
    bSuccess &= RegisterResourceServices();
    
    if (bSuccess)
    {
        UE_LOG(LogMaterialServiceProvider, Log, TEXT("Successfully registered all Material services"));
    }
    else
    {
        UE_LOG(LogMaterialServiceProvider, Error, TEXT("Failed to register some Material services"));
    }
    
    return bSuccess;
}

void FMaterialServiceProvider::UnregisterServices()
{
    UE_LOG(LogMaterialServiceProvider, Log, TEXT("Unregistering Material services"));
    
    // Let the base class handle the cleanup
    FServiceProvider::UnregisterServices();
}

bool FMaterialServiceProvider::RegisterPropertyServices()
{
    // In a real implementation, this would register actual service factories
    // For now, we'll just log that it would happen
    UE_LOG(LogMaterialServiceProvider, Log, TEXT("Registering Material property services"));
    
    // Example of how it would be implemented with real services:
    /*
    // Register IMaterialPropertyService
    RegisterServiceFactory<IMaterialPropertyService>(IMaterialPropertyService::StaticClass(), [this](int32 ZoneID, int32 RegionID) -> IMaterialPropertyService* {
        FMaterialPropertyService* Service = new FMaterialPropertyService();
        Service->Initialize(ZoneID, RegionID);
        return Service;
    });
    
    // Register IMaterialChannelService
    RegisterServiceFactory<IMaterialChannelService>(IMaterialChannelService::StaticClass(), [this](int32 ZoneID, int32 RegionID) -> IMaterialChannelService* {
        FMaterialChannelService* Service = new FMaterialChannelService();
        Service->Initialize(ZoneID, RegionID);
        return Service;
    });
    */
    
    return true;
}

bool FMaterialServiceProvider::RegisterFieldOperationServices()
{
    // In a real implementation, this would register actual service factories
    // For now, we'll just log that it would happen
    UE_LOG(LogMaterialServiceProvider, Log, TEXT("Registering Material field operation services"));
    
    // Example of how it would be implemented with real services:
    /*
    // Register IMaterialFieldOperatorService
    RegisterServiceFactory<IMaterialFieldOperatorService>(IMaterialFieldOperatorService::StaticClass(), [this](int32 ZoneID, int32 RegionID) -> IMaterialFieldOperatorService* {
        FMaterialFieldOperatorService* Service = new FMaterialFieldOperatorService();
        Service->Initialize(ZoneID, RegionID);
        return Service;
    });
    
    // Register IMaterialBlendOperatorService
    RegisterServiceFactory<IMaterialBlendOperatorService>(IMaterialBlendOperatorService::StaticClass(), [this](int32 ZoneID, int32 RegionID) -> IMaterialBlendOperatorService* {
        FMaterialBlendOperatorService* Service = new FMaterialBlendOperatorService();
        Service->Initialize(ZoneID, RegionID);
        return Service;
    });
    */
    
    return true;
}

bool FMaterialServiceProvider::RegisterInteractionServices()
{
    // In a real implementation, this would register actual service factories
    // For now, we'll just log that it would happen
    UE_LOG(LogMaterialServiceProvider, Log, TEXT("Registering Material interaction services"));
    
    // Example of how it would be implemented with real services:
    /*
    // Register IMaterialInteractionService
    RegisterServiceFactory<IMaterialInteractionService>(IMaterialInteractionService::StaticClass(), [this](int32 ZoneID, int32 RegionID) -> IMaterialInteractionService* {
        FMaterialInteractionService* Service = new FMaterialInteractionService();
        Service->Initialize(ZoneID, RegionID);
        return Service;
    });
    
    // Register IMaterialReactionService
    RegisterServiceFactory<IMaterialReactionService>(IMaterialReactionService::StaticClass(), [this](int32 ZoneID, int32 RegionID) -> IMaterialReactionService* {
        FMaterialReactionService* Service = new FMaterialReactionService();
        Service->Initialize(ZoneID, RegionID);
        return Service;
    });
    */
    
    return true;
}

bool FMaterialServiceProvider::RegisterResourceServices()
{
    // In a real implementation, this would register actual service factories
    // For now, we'll just log that it would happen
    UE_LOG(LogMaterialServiceProvider, Log, TEXT("Registering Material resource services"));
    
    // Example of how it would be implemented with real services:
    /*
    // Register IMaterialResourceService
    RegisterServiceFactory<IMaterialResourceService>(IMaterialResourceService::StaticClass(), [this](int32 ZoneID, int32 RegionID) -> IMaterialResourceService* {
        FMaterialResourceService* Service = new FMaterialResourceService();
        Service->Initialize(ZoneID, RegionID);
        return Service;
    });
    
    // Register IMaterialValueService
    RegisterServiceFactory<IMaterialValueService>(IMaterialValueService::StaticClass(), [this](int32 ZoneID, int32 RegionID) -> IMaterialValueService* {
        FMaterialValueService* Service = new FMaterialValueService();
        Service->Initialize(ZoneID, RegionID);
        return Service;
    });
    */
    
    return true;
}

FMaterialServiceProvider& FMaterialServiceProvider::Get()
{
    if (!Instance)
    {
        Instance = new FMaterialServiceProvider();
        Instance->Initialize();
    }
    
    return *Instance;
}