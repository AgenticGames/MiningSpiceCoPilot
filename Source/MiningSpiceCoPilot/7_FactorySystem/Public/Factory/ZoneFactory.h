// ZoneFactory.h
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IFactory.h"
#include "ZoneFactory.generated.h"

class IComponentPoolManager;

/**
 * Specialized factory for zone-based transaction components
 * Handles zone configuration and state initialization
 */
UCLASS()
class MININGSPICECOPILOT_API UZoneFactory : public UObject, public IMiningFactory
{
    GENERATED_BODY()

public:
    UZoneFactory();

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
     * Create a mining zone with specific dimensions
     * @param Location Center location of the zone
     * @param Dimensions Dimensions of the zone
     * @param RegionId Region ID containing this zone
     * @param ZoneType Type of mining zone to create
     * @return New zone instance
     */
    UObject* CreateZone(
        const FVector& Location,
        const FVector& Dimensions,
        int32 RegionId,
        EZoneType ZoneType = EZoneType::Standard);

    /**
     * Create a zone with transaction tracking
     * @param Location Center location of the zone
     * @param Dimensions Dimensions of the zone
     * @param RegionId Region ID containing this zone
     * @param MaterialTypes Material types in this zone
     * @param TransactionCapacity Expected transaction capacity
     * @return New transaction-enabled zone
     */
    UObject* CreateTransactionZone(
        const FVector& Location,
        const FVector& Dimensions,
        int32 RegionId,
        const TArray<uint32>& MaterialTypes,
        int32 TransactionCapacity = 64);

    /**
     * Create a linking zone that connects multiple regions
     * @param Location Center location of the zone
     * @param Dimensions Dimensions of the zone
     * @param RegionIds Regions connected by this zone
     * @return New linking zone
     */
    UObject* CreateLinkingZone(
        const FVector& Location,
        const FVector& Dimensions,
        const TArray<int32>& RegionIds);

    /**
     * Get singleton instance
     * @return Singleton factory instance
     */
    static UZoneFactory* Get();

protected:
    /** Pool manager reference */
    UPROPERTY()
    TScriptInterface<IComponentPoolManager> PoolManager;

    /** Registered zone types */
    UPROPERTY()
    TSet<UClass*> SupportedTypes;

    /** Zone archetypes */
    UPROPERTY()
    TMap<UClass*, UObject*> Archetypes;

    /** Whether the factory is initialized */
    UPROPERTY()
    bool bIsInitialized;

    /** Factory name */
    UPROPERTY()
    FName FactoryName;

    /** Zone pool configurations */
    UPROPERTY()
    TMap<FName, FZonePoolConfig> ZonePoolConfigs;

    /**
     * Configure zone parameters
     * @param Zone Zone to configure
     * @param Location Center location
     * @param Dimensions Zone dimensions
     * @param RegionId Parent region ID
     * @param ZoneType Zone type
     * @return True if configuration was successful
     */
    bool ConfigureZone(
        UObject* Zone,
        const FVector& Location,
        const FVector& Dimensions,
        int32 RegionId,
        EZoneType ZoneType);

    /**
     * Configure transaction tracking for a zone
     * @param Zone Zone to configure
     * @param MaterialTypes Material types to track
     * @param TransactionCapacity Expected transaction capacity
     * @return True if configuration was successful
     */
    bool ConfigureTransactions(
        UObject* Zone,
        const TArray<uint32>& MaterialTypes,
        int32 TransactionCapacity);

    /**
     * Configure zone linking between regions
     * @param Zone Zone to configure
     * @param RegionIds Regions to link
     * @return True if configuration was successful
     */
    bool ConfigureZoneLinking(UObject* Zone, const TArray<int32>& RegionIds);
};

/** Mining zone types */
UENUM(BlueprintType)
enum class EZoneType : uint8
{
    Standard,       // Standard mining zone
    Transaction,    // Transaction-focused zone with version tracking
    Linking,        // Zone linking multiple regions
    Boundary,       // Zone at region boundary
    HighActivity    // Zone optimized for high activity
};

/** Zone pool configuration */
USTRUCT()
struct FZonePoolConfig
{
    GENERATED_BODY()

    // Pool name
    UPROPERTY()
    FName PoolName;

    // Zone type
    UPROPERTY()
    EZoneType ZoneType = EZoneType::Standard;

    // Default transaction capacity
    UPROPERTY()
    int32 TransactionCapacity = 64;

    // Default zone dimensions
    UPROPERTY()
    FVector DefaultDimensions = FVector(32.0f, 32.0f, 32.0f);

    // Pool size
    UPROPERTY()
    int32 PoolSize = 32;

    // Whether to enable transaction tracking by default
    UPROPERTY()
    bool bEnableTransactionTracking = false;
};