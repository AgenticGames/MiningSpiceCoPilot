// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "../../1_CoreRegistry/Public/Interfaces/IServiceProvider.h"
#include "../../1_CoreRegistry/Public/CommonServiceTypes.h"
#include "../../1_CoreRegistry/Public/FMaterialPropertyDependency.h"
#include "Templates/SharedPointer.h"
#include "Containers/Map.h"
#include "HAL/CriticalSection.h"
#include "MaterialServiceProvider.generated.h"

// Forward declarations
class FMaterialRegistry;
class IServiceLocator;
class IMaterialPropertyService;
class IMaterialFieldOperator;
class IMaterialInteractionService;
class FTypeRegistry;

/**
 * Enumeration of property dependency types
 */
UENUM(BlueprintType)
enum class EPropertyDependencyType : uint8
{
    Direct      UMETA(DisplayName = "Direct"),       // Direct dependency on the source property
    Indirect    UMETA(DisplayName = "Indirect"),     // Indirect dependency through another property
    Conditional UMETA(DisplayName = "Conditional"),  // Dependency that only applies under certain conditions
    Computed    UMETA(DisplayName = "Computed")      // Property is computed based on the source property
};

/**
 * Specialized service provider for material-specific components
 * Provides material-specific service registration and resolution with channel awareness
 * Supports cross-material coordination and property dependency tracking
 */
UCLASS()
class MININGSPICECOPILOT_API UMaterialServiceProvider : public UObject, public IServiceProvider
{
    GENERATED_BODY()

public:
    UMaterialServiceProvider();
    virtual ~UMaterialServiceProvider();
    
    //~ Begin IServiceProvider Interface
    virtual TArray<TSubclassOf<UInterface>> GetProvidedServices() const override;
    virtual bool RegisterServices(IServiceLocator* InServiceLocator, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) override;
    virtual bool UnregisterServices(IServiceLocator* InServiceLocator, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) override;
    virtual bool InitializeServices() override;
    virtual void ShutdownServices() override;
    virtual FName GetProviderName() const override;
    virtual TArray<FServiceDependency> GetServiceDependencies() const override;
    virtual bool HandleLifecyclePhase(EServiceLifecyclePhase Phase) override;
    virtual EServiceScope GetServiceScope() const override;
    virtual FServiceHealth GetServiceHealth() const override;
    virtual bool RecoverServices() override;
    virtual FServiceConfig GetServiceConfig() const override;
    virtual bool UpdateServiceConfig(const FServiceConfig& InConfig) override;
    virtual bool ValidateServiceDependencies(IServiceLocator* InServiceLocator, TArray<FServiceDependency>& OutMissingDependencies) override;
    virtual TArray<TSubclassOf<UInterface>> GetDependentServices(IServiceLocator* InServiceLocator) override;
    //~ End IServiceProvider Interface
    
    /**
     * Registers a material property service
     * @param InMaterialTypeId Material type identifier
     * @param InPropertyService Property service implementation
     * @return True if registration was successful
     */
    bool RegisterMaterialPropertyService(uint32 InMaterialTypeId, TSharedPtr<IMaterialPropertyService> InPropertyService);
    
    /**
     * Resolves a material property service for the specified material type
     * @param InMaterialTypeId Material type identifier
     * @return Material property service implementation or nullptr if not found
     */
    TSharedPtr<IMaterialPropertyService> ResolveMaterialPropertyService(uint32 InMaterialTypeId);
    
    /**
     * Registers a material field operator service
     * @param InMaterialTypeId Material type identifier
     * @param InChannelId Material channel identifier
     * @param InFieldOperator Field operator implementation
     * @return True if registration was successful
     */
    bool RegisterMaterialFieldOperator(
        uint32 InMaterialTypeId, 
        int32 InChannelId, 
        TSharedPtr<IMaterialFieldOperator> InFieldOperator);
    
    /**
     * Resolves a material field operator service for the specified material type and channel
     * @param InMaterialTypeId Material type identifier
     * @param InChannelId Material channel identifier
     * @return Material field operator implementation or nullptr if not found
     */
    TSharedPtr<IMaterialFieldOperator> ResolveMaterialFieldOperator(uint32 InMaterialTypeId, int32 InChannelId);
    
    /**
     * Registers a material interaction service
     * @param InSourceMaterialId Source material type identifier
     * @param InTargetMaterialId Target material type identifier
     * @param InInteractionService Interaction service implementation
     * @return True if registration was successful
     */
    bool RegisterMaterialInteractionService(
        uint32 InSourceMaterialId, 
        uint32 InTargetMaterialId, 
        TSharedPtr<IMaterialInteractionService> InInteractionService);
    
    /**
     * Resolves a material interaction service for the specified materials
     * @param InSourceMaterialId Source material type identifier
     * @param InTargetMaterialId Target material type identifier
     * @return Material interaction service implementation or nullptr if not found
     */
    TSharedPtr<IMaterialInteractionService> ResolveMaterialInteractionService(
        uint32 InSourceMaterialId, 
        uint32 InTargetMaterialId);
    
    /**
     * Coordinates operations across material boundaries
     * @param InMaterialIds Array of material type identifiers involved in the operation
     * @param InServiceType Service type identifier
     * @return Service instance that can coordinate across materials
     */
    TSharedPtr<UObject> CoordinateCrossMaterialOperation(
        const TArray<uint32>& InMaterialIds, 
        TSubclassOf<UInterface> InServiceType);
    
    /**
     * Resolves material services with channel awareness
     * @param InMaterialTypeId Material type identifier
     * @param InChannelId Channel identifier
     * @param InServiceType Service type identifier
     * @return Service instance or nullptr if not found
     */
    TSharedPtr<UObject> ResolveChannelAwareService(
        uint32 InMaterialTypeId, 
        int32 InChannelId, 
        TSubclassOf<UInterface> InServiceType);
    
    /**
     * Tracks a dependency between material properties
     * @param InDependentMaterialId Dependent material type identifier
     * @param InDependentPropertyName Dependent property name
     * @param InSourceMaterialId Source material type identifier
     * @param InSourcePropertyName Source property name
     * @return True if dependency was successfully tracked
     */
    bool TrackMaterialPropertyDependency(
        uint32 InDependentMaterialId, 
        const FName& InDependentPropertyName, 
        uint32 InSourceMaterialId, 
        const FName& InSourcePropertyName);
    
    /**
     * Gets all dependencies for a material property
     * @param InMaterialTypeId Material type identifier
     * @param InPropertyName Property name
     * @return Array of property dependencies
     */
    TArray<FMaterialPropertyDependency> GetMaterialPropertyDependencies(
        uint32 InMaterialTypeId, 
        const FName& InPropertyName) const;
    
    /**
     * Gets all material types that depend on a specific material type
     * @param InMaterialTypeId Material type identifier
     * @return Array of dependent material type identifiers
     */
    TArray<uint32> GetDependentMaterialTypes(uint32 InMaterialTypeId) const;
    
    /**
     * Updates a property value and propagates changes to dependent properties
     * @param InMaterialTypeId Material type identifier
     * @param InPropertyName Property name
     * @param InPropertyValue New property value
     * @return True if update was successful
     */
    bool UpdateAndPropagateMaterialProperty(
        uint32 InMaterialTypeId, 
        const FName& InPropertyName, 
        const FString& InPropertyValue);

private:
    /**
     * Initializes the service provider with the material registry
     */
    void InitializeWithRegistry();
    
    /**
     * Generates a material channel key for lookups
     * @param InMaterialTypeId Material type identifier
     * @param InChannelId Channel identifier
     * @return Unique material channel key
     */
    uint64 GenerateMaterialChannelKey(uint32 InMaterialTypeId, int32 InChannelId) const;
    
    /**
     * Generates a material interaction key for lookups
     * @param InSourceMaterialId Source material type identifier
     * @param InTargetMaterialId Target material type identifier
     * @return Unique material interaction key
     */
    uint64 GenerateMaterialInteractionKey(uint32 InSourceMaterialId, uint32 InTargetMaterialId) const;
    
    /**
     * Resolves all material services that might apply based on inheritance
     * @param InMaterialTypeId Material type identifier
     * @param InServiceType Service type identifier
     * @return Array of applicable services
     */
    TArray<TSharedPtr<UObject>> ResolveMaterialServiceHierarchy(
        uint32 InMaterialTypeId, 
        TSubclassOf<UInterface> InServiceType);
    
    /**
     * Propagates a property change to all dependent properties
     * @param InMaterialTypeId Material type identifier
     * @param InPropertyName Property name
     * @param InVisitedProperties Set of already visited properties to avoid cycles
     * @return True if propagation was successful
     */
    bool PropagateMaterialPropertyChange(
        uint32 InMaterialTypeId, 
        const FName& InPropertyName, 
        TSet<FName>& InVisitedProperties);
    
    /** Reference to the material registry */
    TWeakPtr<FMaterialRegistry> MaterialRegistry;
    
    /** Reference to the type registry */
    TWeakPtr<FTypeRegistry> TypeRegistry;
    
    /** Service locator reference */
    IServiceLocator* ServiceLocator;
    
    /** Material property services by material type */
    TMap<uint32, TSharedPtr<IMaterialPropertyService>> MaterialPropertyServices;
    
    /** Material field operator services by material type and channel */
    TMap<uint64, TSharedPtr<IMaterialFieldOperator>> MaterialFieldOperators;
    
    /** Material interaction services by source and target material types */
    TMap<uint64, TSharedPtr<IMaterialInteractionService>> MaterialInteractionServices;
    
    /** Material property dependencies */
    TMap<uint32, TMap<FName, TArray<FMaterialPropertyDependency>>> PropertyDependencies;
    
    /** Dependent material types */
    TMap<uint32, TArray<uint32>> DependentMaterialMap;
    
    /** Critical section for thread-safe access */
    mutable FCriticalSection ServiceLock;
    
    /** Configuration for this provider */
    FServiceConfig ServiceConfig;
    
    /** Health status for this provider */
    FServiceHealth ServiceHealth;
    
    /** List of service dependencies */
    TArray<FServiceDependency> ServiceDependencies;
    
    /** Flag to track initialization status */
    bool bInitialized;
};
