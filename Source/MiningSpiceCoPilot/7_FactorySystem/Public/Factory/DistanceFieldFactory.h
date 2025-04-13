// DistanceFieldFactory.h
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IFactory.h"
#include "DistanceFieldFactory.generated.h"

class IComponentPoolManager;

/** Field precision levels */
UENUM(BlueprintType)
enum class EFieldPrecision : uint8
{
    Low,            // Low precision (8-bit)
    Medium,         // Medium precision (16-bit)
    High,           // High precision (32-bit)
    Double          // Double precision (64-bit)
};

/** Field pool configuration */
USTRUCT(BlueprintType)
struct MININGSPICECOPILOT_API FFieldPoolConfig
{
    GENERATED_BODY()

    // Pool name
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field Pool")
    FName PoolName;

    // Field resolution
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field Pool")
    FIntVector Resolution;

    // Material channel count
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field Pool")
    int32 MaterialChannels = 1;

    // Narrow band width
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field Pool")
    float NarrowBandWidth = 4.0f;

    // Field precision
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field Pool")
    EFieldPrecision Precision = EFieldPrecision::Medium;

    // Pool size
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field Pool")
    int32 PoolSize = 16;

    // Memory footprint per field in bytes
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field Pool")
    int64 MemoryPerField = 0;
};

/**
 * Specialized factory for multi-channel distance field components
 * Optimized for memory-efficient field allocation with narrow-band focus
 */
UCLASS()
class MININGSPICECOPILOT_API UDistanceFieldFactory : public UObject, public IMiningFactory
{
    GENERATED_BODY()

public:
    UDistanceFieldFactory();

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
     * Create a distance field with specific resolution and material channels
     * @param Resolution Grid resolution for the field
     * @param MaterialChannels Number of material channels to support
     * @param NarrowBandWidth Width of the narrow band in voxels
     * @param Precision Field precision level
     * @return New distance field instance
     */
    UObject* CreateDistanceField(
        const FIntVector& Resolution, 
        int32 MaterialChannels = 1, 
        float NarrowBandWidth = 4.0f,
        EFieldPrecision Precision = EFieldPrecision::Medium);

    /**
     * Create a signed distance field from a mesh
     * @param Mesh Source mesh to generate field from
     * @param Resolution Grid resolution for the field
     * @param MaterialIndex Material index to use
     * @param Precision Field precision level
     * @return New distance field instance
     */
    UObject* CreateDistanceFieldFromMesh(
        UStaticMesh* Mesh, 
        const FIntVector& Resolution, 
        int32 MaterialIndex = 0,
        EFieldPrecision Precision = EFieldPrecision::Medium);

    /**
     * Get singleton instance
     * @return Singleton factory instance
     */
    static UDistanceFieldFactory* Get();

protected:
    /** Pool manager reference */
    UPROPERTY()
    TScriptInterface<IComponentPoolManager> PoolManager;

    /** Registered field types */
    UPROPERTY()
    TSet<UClass*> SupportedTypes;

    /** Field archetypes */
    UPROPERTY()
    TMap<UClass*, UObject*> Archetypes;

    /** Whether the factory is initialized */
    UPROPERTY()
    bool bIsInitialized;

    /** Factory name */
    UPROPERTY()
    FName FactoryName;

    /** Field pool configurations by resolution */
    UPROPERTY()
    TMap<FIntVector, FFieldPoolConfig> FieldPoolConfigs;

    /**
     * Configure field parameters and memory layout
     * @param Field Distance field to configure
     * @param Resolution Grid resolution
     * @param MaterialChannels Number of material channels
     * @param NarrowBandWidth Width of narrow band
     * @param Precision Field precision level
     * @return True if configuration was successful
     */
    bool ConfigureField(
        UObject* Field, 
        const FIntVector& Resolution, 
        int32 MaterialChannels,
        float NarrowBandWidth,
        EFieldPrecision Precision);

    /**
     * Optimize memory allocation for narrow-band field
     * @param Resolution Field resolution
     * @param MaterialChannels Number of material channels
     * @param NarrowBandWidth Width of narrow band
     * @param Precision Field precision level
     * @return Optimal memory allocation size
     */
    int64 CalculateOptimalMemoryAllocation(
        const FIntVector& Resolution, 
        int32 MaterialChannels,
        float NarrowBandWidth,
        EFieldPrecision Precision);

    /**
     * Get the appropriate field pool for the given parameters
     * @param Resolution Field resolution
     * @param MaterialChannels Number of material channels
     * @param Precision Field precision level
     * @return Pool name for the specified field configuration
     */
    FName GetFieldPoolName(
        const FIntVector& Resolution,
        int32 MaterialChannels,
        EFieldPrecision Precision);
};