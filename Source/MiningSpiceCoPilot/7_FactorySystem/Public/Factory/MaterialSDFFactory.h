// MaterialSDFFactory.h
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IFactory.h"
#include "Interfaces/IMaterialPropertyProvider.h" 
#include "MaterialSDFFactory.generated.h"

class IComponentPoolManager;

/** Material CSG operations */
UENUM(BlueprintType)
enum class EMiningCsgOperation : uint8
{
    Union,           // Union operation
    Subtraction,     // Subtraction operation
    Intersection,    // Intersection operation
    SmoothUnion,     // Smooth union operation
    SmoothSubtract,  // Smooth subtraction operation
    Replace          // Material replacement operation
};

/** Material blending modes */
UENUM(BlueprintType)
enum class EMaterialBlendMode : uint8
{
    Hard,            // Hard transitions between materials
    Smooth,          // Smooth transitions between materials
    Fractional,      // Fractional transitions with material mixing
    Layered          // Layered material transitions
};

/** Material SDF configuration */
USTRUCT(BlueprintType)
struct MININGSPICECOPILOT_API FMaterialSDFConfig
{
    GENERATED_BODY()

    // Material type ID
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material SDF")
    int32 MaterialType = 0;

    // Default CSG operation
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material SDF")
    EMiningCsgOperation DefaultOperation = EMiningCsgOperation::Union;

    // Default blending mode
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material SDF")
    EMaterialBlendMode DefaultBlendMode = EMaterialBlendMode::Smooth;

    // Default blend radius
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material SDF")
    float DefaultBlendRadius = 1.0f;

    // Default field resolution
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material SDF")
    FIntVector DefaultResolution = FIntVector(32, 32, 32);
    
    // Default pool size
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material SDF")
    int32 DefaultPoolSize = 8;
};

/**
 * Specialized factory for material-specific SDF components
 * Handles material property integration and interaction rules
 */
UCLASS()
class MININGSPICECOPILOT_API UMaterialSDFFactory : public UObject, public IMiningFactory
{
    GENERATED_BODY()

public:
    UMaterialSDFFactory();

    //~ Begin IMiningFactory Interface
    virtual bool Initialize() override;
    virtual void Shutdown() override;
    virtual bool IsInitialized() const override;
    virtual FName GetFactoryName() const override;
    virtual bool SupportsType(UClass* ComponentType) const override;
    virtual UObject* CreateComponent(UClass* ComponentType, const TMap<FName, FString>& Parameters = TMap<FName, FString>()) override;
    virtual TArray<UClass*> GetSupportedTypes() const override;
    virtual bool RegisterArchetype(UClass* ComponentType, UObject* Archetype) override;
    virtual bool HasPool(UClass* ComponentType) const override;
    virtual bool CreatePool(UClass* ComponentType, int32 InitialSize, int32 MaxSize, bool bEnablePooling = true) override;
    virtual bool ReturnToPool(UObject* Component) override;
    virtual int32 FlushPool(UClass* ComponentType) override;
    virtual bool GetPoolStats(UClass* ComponentType, int32& OutAvailable, int32& OutTotal) const override;
    //~ End IMiningFactory Interface

    /**
     * Create a material SDF component with specific properties
     * @param MaterialType Material type for the SDF
     * @param Operation CSG operation for material field
     * @param Resolution Field resolution
     * @param BlendMode Blending mode for material transitions
     * @return New material SDF component
     */
    UObject* CreateMaterialSDF(
        int32 MaterialType,
        EMiningCsgOperation Operation = EMiningCsgOperation::Union,
        const FIntVector& Resolution = FIntVector(32, 32, 32),
        EMaterialBlendMode BlendMode = EMaterialBlendMode::Smooth);

    /**
     * Create a material SDF for a specific environment
     * @param MaterialTypes Array of material types to support
     * @param Resolution Field resolution
     * @param BlendMode Blending mode for material transitions
     * @return New material SDF component supporting multiple materials
     */
    UObject* CreateMultiMaterialSDF(
        const TArray<int32>& MaterialTypes,
        const FIntVector& Resolution = FIntVector(32, 32, 32),
        EMaterialBlendMode BlendMode = EMaterialBlendMode::Smooth);
        
    /**
     * Set material property provider
     * @param InPropertyProvider Material property provider to use
     */
    void SetMaterialPropertyProvider(TScriptInterface<IMaterialPropertyProvider> InPropertyProvider);

    /**
     * Get singleton instance
     * @return Singleton factory instance
     */
    static UMaterialSDFFactory* Get();

protected:
    /** Pool manager reference */
    UPROPERTY()
    TScriptInterface<IComponentPoolManager> PoolManager;

    /** Material property provider */
    UPROPERTY()
    TScriptInterface<IMaterialPropertyProvider> MaterialPropertyProvider;
    
    /** Registered component types */
    UPROPERTY()
    TSet<UClass*> SupportedTypes;

    /** Component archetypes */
    UPROPERTY()
    TMap<UClass*, UObject*> Archetypes;

    /** Whether the factory is initialized */
    UPROPERTY()
    bool bIsInitialized;

    /** Factory name */
    UPROPERTY()
    FName FactoryName;

    /** Material SDF configurations */
    UPROPERTY()
    TMap<int32, FMaterialSDFConfig> MaterialConfigs;

    /**
     * Configure material properties for an SDF component
     * @param Component SDF component to configure
     * @param MaterialType Material type ID
     * @param Operation CSG operation
     * @param BlendMode Blending mode
     * @return True if configuration was successful
     */
    bool ConfigureMaterialProperties(
        UObject* Component, 
        int32 MaterialType,
        EMiningCsgOperation Operation,
        EMaterialBlendMode BlendMode);

    /**
     * Configure material blending parameters
     * @param Component SDF component to configure
     * @param MaterialTypes Material types for blending
     * @param BlendMode Blending mode
     * @return True if configuration was successful
     */
    bool ConfigureBlending(
        UObject* Component,
        const TArray<int32>& MaterialTypes,
        EMaterialBlendMode BlendMode);

    /**
     * Get the appropriate pool for the given material configuration
     * @param MaterialType Material type ID
     * @param Operation CSG operation
     * @return Pool name for the specified material configuration
     */
    FName GetMaterialPoolName(int32 MaterialType, EMiningCsgOperation Operation);
};