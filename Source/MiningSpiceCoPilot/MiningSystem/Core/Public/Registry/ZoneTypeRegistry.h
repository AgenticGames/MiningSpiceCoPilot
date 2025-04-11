// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IRegistry.h"
#include "Containers/Map.h"
#include "Templates/SharedPointer.h"
#include "HAL/ThreadSafeBool.h"
#include "Misc/SpinLock.h"

/**
 * Transaction concurrency level for zone operations
 */
enum class ETransactionConcurrency : uint8
{
    /** Read-only transactions that don't modify state */
    ReadOnly,
    
    /** Read-write transactions with optimistic concurrency */
    Optimistic,
    
    /** Transactions that require exclusive access */
    Exclusive,
    
    /** Transactions that operate on a specific material channel */
    MaterialChannel
};

/**
 * Retry strategy type for transaction conflicts
 */
enum class ERetryStrategy : uint8
{
    /** No retry, fail immediately */
    None,
    
    /** Retry with fixed interval */
    FixedInterval,
    
    /** Retry with exponential backoff */
    ExponentialBackoff,
    
    /** Custom retry strategy with callback */
    Custom
};

/**
 * Structure containing metadata for a zone transaction type
 */
struct MININGSPICECOPILOT_API FZoneTransactionTypeInfo
{
    /** Unique ID for this transaction type */
    uint32 TypeId;
    
    /** Name of this transaction type */
    FName TypeName;
    
    /** Concurrency level for this transaction type */
    ETransactionConcurrency ConcurrencyLevel;
    
    /** Retry strategy for conflicts */
    ERetryStrategy RetryStrategy;
    
    /** Maximum number of retry attempts */
    uint32 MaxRetries;
    
    /** Base retry interval in milliseconds */
    uint32 BaseRetryIntervalMs;
    
    /** Material channel ID for material-specific transactions */
    int32 MaterialChannelId;
    
    /** Priority for conflict resolution */
    uint32 Priority;
    
    /** Whether this transaction type requires version tracking */
    bool bRequiresVersionTracking;
    
    /** Whether this transaction type supports fast-path execution */
    bool bSupportsFastPath;
    
    /** Fast-path conflict probability threshold (0-1) */
    float FastPathThreshold;
    
    /** Whether this transaction has a read-validate-write pattern */
    bool bHasReadValidateWritePattern;
};

/**
 * Structure containing configuration for a zone grid
 */
struct MININGSPICECOPILOT_API FZoneGridConfig
{
    /** Size of a zone in world units */
    float ZoneSize;
    
    /** Default zone configuration name */
    FName DefaultConfigName;
    
    /** Maximum number of concurrent transactions per zone */
    uint32 MaxConcurrentTransactions;
    
    /** Whether to use material-specific versioning */
    bool bUseMaterialSpecificVersioning;
    
    /** Number of versions to track in history */
    uint32 VersionHistoryLength;
};

/**
 * Registry for zone transaction types in the mining system
 * Handles transaction type registration, zone configuration, and concurrency metadata
 */
class MININGSPICECOPILOT_API FZoneTypeRegistry : public IRegistry
{
public:
    /** Default constructor */
    FZoneTypeRegistry();
    
    /** Destructor */
    virtual ~FZoneTypeRegistry();
    
    //~ Begin IRegistry Interface
    virtual bool Initialize() override;
    virtual void Shutdown() override;
    virtual bool IsInitialized() const override;
    virtual FName GetRegistryName() const override;
    virtual uint32 GetSchemaVersion() const override;
    virtual bool Validate(TArray<FString>& OutErrors) const override;
    virtual void Clear() override;
    //~ End IRegistry Interface
    
    /**
     * Registers a new zone transaction type with the registry
     * @param InTypeName Name of the transaction type
     * @param InConcurrencyLevel Concurrency level for this transaction type
     * @param InRetryStrategy Retry strategy for conflicts
     * @return Unique ID for the registered type, or 0 if registration failed
     */
    uint32 RegisterTransactionType(
        const FName& InTypeName,
        ETransactionConcurrency InConcurrencyLevel = ETransactionConcurrency::Optimistic,
        ERetryStrategy InRetryStrategy = ERetryStrategy::ExponentialBackoff);
    
    /**
     * Registers a material-specific transaction type
     * @param InTypeName Name of the transaction type
     * @param InMaterialChannelId Material channel ID
     * @return Unique ID for the registered type, or 0 if registration failed
     */
    uint32 RegisterMaterialTransaction(
        const FName& InTypeName,
        int32 InMaterialChannelId);
    
    /**
     * Registers a zone grid configuration
     * @param InConfigName Name of the configuration
     * @param InZoneSize Size of a zone in world units
     * @param InMaxConcurrentTransactions Maximum number of concurrent transactions per zone
     * @return True if registration was successful
     */
    bool RegisterZoneGridConfig(
        const FName& InConfigName,
        float InZoneSize = 2.0f,
        uint32 InMaxConcurrentTransactions = 16);
    
    /**
     * Gets information about a registered transaction type
     * @param InTypeId Unique ID of the transaction type
     * @return Pointer to transaction type info, or nullptr if not found
     */
    const FZoneTransactionTypeInfo* GetTransactionTypeInfo(uint32 InTypeId) const;
    
    /**
     * Gets information about a registered transaction type by name
     * @param InTypeName Name of the transaction type
     * @return Pointer to transaction type info, or nullptr if not found
     */
    const FZoneTransactionTypeInfo* GetTransactionTypeInfoByName(const FName& InTypeName) const;
    
    /**
     * Gets a registered zone grid configuration
     * @param InConfigName Name of the configuration
     * @return Pointer to zone grid config, or nullptr if not found
     */
    const FZoneGridConfig* GetZoneGridConfig(const FName& InConfigName) const;
    
    /**
     * Gets the default zone grid configuration
     * @return Pointer to default zone grid config
     */
    const FZoneGridConfig* GetDefaultZoneGridConfig() const;
    
    /**
     * Sets the default zone grid configuration
     * @param InConfigName Name of the configuration to set as default
     * @return True if successful
     */
    bool SetDefaultZoneGridConfig(const FName& InConfigName);
    
    /**
     * Updates a transaction type's properties
     * @param InTypeId Transaction type ID
     * @param InPropertyName Property name
     * @param InValue Property value
     * @return True if update was successful
     */
    bool UpdateTransactionProperty(uint32 InTypeId, const FName& InPropertyName, const FString& InValue);
    
    /**
     * Updates a transaction type's fast-path threshold based on conflict history
     * @param InTypeId Transaction type ID
     * @param InConflictRate Observed conflict rate (0-1)
     * @return True if update was successful
     */
    bool UpdateFastPathThreshold(uint32 InTypeId, float InConflictRate);
    
    /**
     * Checks if a transaction type is registered
     * @param InTypeId Unique ID of the transaction type
     * @return True if the type is registered
     */
    bool IsTransactionTypeRegistered(uint32 InTypeId) const;
    
    /**
     * Checks if a transaction type is registered by name
     * @param InTypeName Name of the transaction type
     * @return True if the type is registered
     */
    bool IsTransactionTypeRegistered(const FName& InTypeName) const;
    
    /** Gets the singleton instance of the zone type registry */
    static FZoneTypeRegistry& Get();
    
private:
    /** Generates a unique type ID for new transaction type registrations */
    uint32 GenerateUniqueTypeId();
    
    /** Map of registered transaction types by ID */
    TMap<uint32, TSharedRef<FZoneTransactionTypeInfo>> TransactionTypeMap;
    
    /** Map of registered transaction types by name for fast lookup */
    TMap<FName, uint32> TransactionTypeNameMap;
    
    /** Map of registered zone grid configurations by name */
    TMap<FName, TSharedRef<FZoneGridConfig>> ZoneGridConfigMap;
    
    /** Name of the default zone grid configuration */
    FName DefaultZoneGridConfigName;
    
    /** Counter for generating unique type IDs */
    uint32 NextTypeId;
    
    /** Thread-safe flag indicating if the registry has been initialized */
    FThreadSafeBool bIsInitialized;
    
    /** Schema version of this registry */
    uint32 SchemaVersion;
    
    /** Lock for thread-safe access to the registry maps */
    mutable FSpinLock RegistryLock;
    
    /** Singleton instance of the registry */
    static FZoneTypeRegistry* Singleton;
    
    /** Thread-safe initialization flag for the singleton */
    static FThreadSafeBool bSingletonInitialized;
};