// TransactionContextFactory.h
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IFactory.h"
#include "TransactionContextFactory.generated.h"

class IComponentPoolManager;

/**
 * Specialized factory for mining transaction contexts
 * Handles transaction configuration and state tracking
 */
UCLASS()
class MININGSPICECOPILOT_API UTransactionContextFactory : public UObject, public IMiningFactory
{
    GENERATED_BODY()

public:
    UTransactionContextFactory();

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
     * Create a transaction context for a mining operation
     * @param ZoneId Zone ID where the transaction will occur
     * @param TransactionType Type of mining transaction
     * @param Priority Transaction priority
     * @return New transaction context
     */
    UObject* CreateTransactionContext(
        int32 ZoneId,
        ETransactionType TransactionType = ETransactionType::Standard,
        ETransactionPriority Priority = ETransactionPriority::Normal);

    /**
     * Create a compound transaction spanning multiple zones
     * @param ZoneIds Zone IDs involved in the transaction
     * @param TransactionType Type of mining transaction
     * @param Priority Transaction priority
     * @return New compound transaction context
     */
    UObject* CreateCompoundTransaction(
        const TArray<int32>& ZoneIds,
        ETransactionType TransactionType = ETransactionType::Compound,
        ETransactionPriority Priority = ETransactionPriority::Normal);

    /**
     * Create a material-specific transaction context
     * @param ZoneId Zone ID where the transaction will occur
     * @param MaterialTypeId Material type ID involved in the transaction
     * @param TransactionType Type of mining transaction
     * @param Priority Transaction priority
     * @return New material-specific transaction context
     */
    UObject* CreateMaterialTransaction(
        int32 ZoneId,
        uint32 MaterialTypeId,
        ETransactionType TransactionType = ETransactionType::Standard,
        ETransactionPriority Priority = ETransactionPriority::Normal);

    /**
     * Get singleton instance
     * @return Singleton factory instance
     */
    static UTransactionContextFactory* Get();

protected:
    /** Pool manager reference */
    UPROPERTY()
    TScriptInterface<IComponentPoolManager> PoolManager;

    /** Registered transaction types */
    UPROPERTY()
    TSet<UClass*> SupportedTypes;

    /** Transaction archetypes */
    UPROPERTY()
    TMap<UClass*, UObject*> Archetypes;

    /** Whether the factory is initialized */
    UPROPERTY()
    bool bIsInitialized;

    /** Factory name */
    UPROPERTY()
    FName FactoryName;

    /** Transaction pool configurations */
    UPROPERTY()
    TMap<ETransactionType, FTransactionPoolConfig> TransactionPoolConfigs;

    /**
     * Configure transaction parameters
     * @param Transaction Transaction to configure
     * @param ZoneId Zone ID
     * @param TransactionType Transaction type
     * @param Priority Transaction priority
     * @return True if configuration was successful
     */
    bool ConfigureTransaction(
        UObject* Transaction,
        int32 ZoneId,
        ETransactionType TransactionType,
        ETransactionPriority Priority);

    /**
     * Configure a compound transaction across multiple zones
     * @param Transaction Transaction to configure
     * @param ZoneIds Zone IDs
     * @param TransactionType Transaction type
     * @param Priority Transaction priority
     * @return True if configuration was successful
     */
    bool ConfigureCompoundTransaction(
        UObject* Transaction,
        const TArray<int32>& ZoneIds,
        ETransactionType TransactionType,
        ETransactionPriority Priority);

    /**
     * Configure material-specific transaction parameters
     * @param Transaction Transaction to configure
     * @param ZoneId Zone ID
     * @param MaterialTypeId Material type ID
     * @param TransactionType Transaction type
     * @param Priority Transaction priority
     * @return True if configuration was successful
     */
    bool ConfigureMaterialTransaction(
        UObject* Transaction,
        int32 ZoneId,
        uint32 MaterialTypeId,
        ETransactionType TransactionType,
        ETransactionPriority Priority);

    /**
     * Get the appropriate pool for the given transaction configuration
     * @param TransactionType Transaction type
     * @param Priority Transaction priority
     * @return Pool name for the specified transaction configuration
     */
    FName GetTransactionPoolName(
        ETransactionType TransactionType,
        ETransactionPriority Priority);
};

/** Mining transaction types */
UENUM(BlueprintType)
enum class ETransactionType : uint8
{
    Standard,        // Standard single-zone transaction
    Compound,        // Compound multi-zone transaction
    ReadOnly,        // Read-only transaction (no writes)
    WriteOnly,       // Write-only transaction (no reads)
    MaterialSpecific // Material-specific transaction
};

/** Transaction priorities */
UENUM(BlueprintType)
enum class ETransactionPriority : uint8
{
    Low,             // Low priority transaction
    Normal,          // Normal priority transaction
    High,            // High priority transaction
    Critical         // Critical priority transaction
};

/** Transaction pool configuration */
USTRUCT()
struct FTransactionPoolConfig
{
    GENERATED_BODY()

    // Pool name
    UPROPERTY()
    FName PoolName;

    // Transaction type
    UPROPERTY()
    ETransactionType TransactionType = ETransactionType::Standard;

    // Default priority
    UPROPERTY()
    ETransactionPriority DefaultPriority = ETransactionPriority::Normal;

    // Read set capacity
    UPROPERTY()
    int32 ReadSetCapacity = 64;

    // Write set capacity
    UPROPERTY()
    int32 WriteSetCapacity = 32;

    // Default pool size
    UPROPERTY()
    int32 PoolSize = 128;

    // Whether to collect transaction metrics by default
    UPROPERTY()
    bool bCollectMetrics = true;
};