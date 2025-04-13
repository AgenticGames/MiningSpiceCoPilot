// DistanceFieldFactory.h
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factory/SVONodeFactory.h" // Include the file where IMiningFactory is now defined
#include "DistanceFieldFactory.generated.h"

class IComponentPoolManager;

/** Precision levels for distance fields */
UENUM(BlueprintType)
enum class EFieldPrecision : uint8
{
    Low,      // Lowest precision, fastest performance
    Medium,   // Medium precision, balanced
    High,     // High precision
    Ultra     // Ultra-precision, slowest performance
};

/** Distance field configuration struct */
USTRUCT()
struct FFieldPoolConfig
{
    GENERATED_BODY()

    // Pool name
    UPROPERTY()
    FName PoolName;

    // Field precision level
    UPROPERTY()
    EFieldPrecision Precision = EFieldPrecision::Medium;

    // Default grid resolution (voxels)
    UPROPERTY()
    FIntVector DefaultResolution = FIntVector(32, 32, 32);

    // Default grid cell size
    UPROPERTY()
    float CellSize = 1.0f;

    // Default memory footprint per field (in KB)
    UPROPERTY()
    int32 EstimatedMemoryPerField = 512;

    // Initial pool size
    UPROPERTY()
    int32 PoolSize = 16;

    // Whether to use out-of-core paging by default
    UPROPERTY()
    bool bUseOutOfCorePaging = false;
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