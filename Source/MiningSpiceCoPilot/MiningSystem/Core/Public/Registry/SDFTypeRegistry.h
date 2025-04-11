// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IRegistry.h"
#include "Containers/Map.h"
#include "Templates/SharedPointer.h"
#include "HAL/ThreadSafeBool.h"
#include "Misc/SpinLock.h"
#include "Math/Vector.h"
#include "Math/Transform.h"

// Forward declarations
class UMaterialInterface;

/**
 * SDF field operation types for CSG operations
 */
enum class ESDFOperationType : uint8
{
    /** Union operation (min) */
    Union,
    
    /** Subtraction operation */
    Subtraction,
    
    /** Intersection operation (max) */
    Intersection,
    
    /** Smooth union with blending */
    SmoothUnion,
    
    /** Smooth subtraction with blending */
    SmoothSubtraction,
    
    /** Smooth intersection with blending */
    SmoothIntersection,
    
    /** Custom operation type */
    Custom
};

/**
 * SDF field evaluation contexts
 */
enum class ESDFEvaluationContext : uint8
{
    /** Mining operations */
    Mining,
    
    /** Rendering and visualization */
    Rendering,
    
    /** Physics simulation */
    Physics,
    
    /** Game logic and effects */
    GameLogic
};

/**
 * Structure containing metadata for an SDF field type
 */
struct MININGSPICECOPILOT_API FSDFFieldTypeInfo
{
    /** Unique ID for this field type */
    uint32 TypeId;
    
    /** Name of this field type */
    FName TypeName;
    
    /** Version of this field type's schema */
    uint32 SchemaVersion;
    
    /** Number of channels supported by this field type */
    uint32 ChannelCount;
    
    /** Whether this field type supports narrow-band optimization */
    bool bSupportsNarrowBand;
    
    /** Whether this field type supports GPU evaluation */
    bool bSupportsGPUEvaluation;
    
    /** Whether this field type supports SIMD operations */
    bool bSupportsSIMD;
    
    /** Whether this field type supports serialization */
    bool bSupportsSerialization;
    
    /** Optimal evaluation batch size for SIMD operations */
    uint32 OptimalBatchSize;
    
    /** Default narrow band width for this field type */
    float DefaultNarrowBandWidth;
};

/**
 * Structure containing information about an SDF field operation
 */
struct MININGSPICECOPILOT_API FSDFOperationInfo
{
    /** Unique ID for this operation */
    uint32 OperationId;
    
    /** Name of this operation */
    FName OperationName;
    
    /** Type of this operation */
    ESDFOperationType OperationType;
    
    /** Field types that this operation is compatible with */
    TArray<uint32> CompatibleFieldTypes;
    
    /** Material types that this operation is compatible with */
    TArray<uint32> CompatibleMaterialTypes;
    
    /** Whether this operation supports GPU evaluation */
    bool bSupportsGPUEvaluation;
    
    /** Whether this operation supports SIMD operations */
    bool bSupportsSIMD;
    
    /** Approximate computational cost of this operation (normalized value) */
    float ComputationalCost;
};

/**
 * Registry for SDF field types in the mining system
 * Handles field type registration, operation compatibility, and evaluation strategies
 */
class MININGSPICECOPILOT_API FSDFTypeRegistry : public IRegistry
{
public:
    /** Default constructor */
    FSDFTypeRegistry();
    
    /** Destructor */
    virtual ~FSDFTypeRegistry();
    
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
     * Registers a new SDF field type with the registry
     * @param InTypeName Name of the field type
     * @param InChannelCount Number of channels supported by this field type
     * @param bInSupportsNarrowBand Whether this field supports narrow-band optimization
     * @return Unique ID for the registered type, or 0 if registration failed
     */
    uint32 RegisterFieldType(
        const FName& InTypeName,
        uint32 InChannelCount = 1,
        bool bInSupportsNarrowBand = true);
    
    /**
     * Registers a new field operation with the registry
     * @param InOperationName Name of the operation
     * @param InOperationType Type of the operation
     * @param InCompatibleFieldTypes Field types that this operation is compatible with
     * @return Unique ID for the registered operation, or 0 if registration failed
     */
    uint32 RegisterFieldOperation(
        const FName& InOperationName,
        ESDFOperationType InOperationType,
        const TArray<uint32>& InCompatibleFieldTypes);
    
    /**
     * Gets information about a registered field type
     * @param InTypeId Unique ID of the field type
     * @return Pointer to field type info, or nullptr if not found
     */
    const FSDFFieldTypeInfo* GetFieldTypeInfo(uint32 InTypeId) const;
    
    /**
     * Gets information about a registered field type by name
     * @param InTypeName Name of the field type
     * @return Pointer to field type info, or nullptr if not found
     */
    const FSDFFieldTypeInfo* GetFieldTypeInfoByName(const FName& InTypeName) const;
    
    /**
     * Gets information about a registered field operation
     * @param InOperationId Unique ID of the operation
     * @return Pointer to operation info, or nullptr if not found
     */
    const FSDFOperationInfo* GetFieldOperationInfo(uint32 InOperationId) const;
    
    /**
     * Gets information about a registered field operation by name
     * @param InOperationName Name of the operation
     * @return Pointer to operation info, or nullptr if not found
     */
    const FSDFOperationInfo* GetFieldOperationInfoByName(const FName& InOperationName) const;
    
    /**
     * Gets all registered field types
     * @return Array of all field type infos
     */
    TArray<FSDFFieldTypeInfo> GetAllFieldTypes() const;
    
    /**
     * Gets all registered field operations
     * @return Array of all operation infos
     */
    TArray<FSDFOperationInfo> GetAllFieldOperations() const;
    
    /**
     * Gets compatible operations for a specific field type
     * @param InTypeId Field type ID
     * @return Array of compatible operation infos
     */
    TArray<FSDFOperationInfo> GetCompatibleOperations(uint32 InTypeId) const;
    
    /**
     * Checks if an operation is compatible with a field type
     * @param InOperationId Operation ID
     * @param InTypeId Field type ID
     * @return True if compatible
     */
    bool IsOperationCompatible(uint32 InOperationId, uint32 InTypeId) const;
    
    /**
     * Checks if a field type is registered
     * @param InTypeId Unique ID of the field type
     * @return True if the type is registered
     */
    bool IsFieldTypeRegistered(uint32 InTypeId) const;
    
    /**
     * Checks if a field operation is registered
     * @param InOperationId Unique ID of the operation
     * @return True if the operation is registered
     */
    bool IsFieldOperationRegistered(uint32 InOperationId) const;
    
    /** Gets the singleton instance of the SDF type registry */
    static FSDFTypeRegistry& Get();
    
private:
    /** Generates a unique type ID for new field type registrations */
    uint32 GenerateUniqueTypeId();
    
    /** Generates a unique operation ID for new operation registrations */
    uint32 GenerateUniqueOperationId();
    
    /** Map of registered field types by ID */
    TMap<uint32, TSharedRef<FSDFFieldTypeInfo>> FieldTypeMap;
    
    /** Map of registered field types by name for fast lookup */
    TMap<FName, uint32> FieldTypeNameMap;
    
    /** Map of registered operations by ID */
    TMap<uint32, TSharedRef<FSDFOperationInfo>> OperationMap;
    
    /** Map of registered operations by name for fast lookup */
    TMap<FName, uint32> OperationNameMap;
    
    /** Counter for generating unique type IDs */
    uint32 NextTypeId;
    
    /** Counter for generating unique operation IDs */
    uint32 NextOperationId;
    
    /** Thread-safe flag indicating if the registry has been initialized */
    FThreadSafeBool bIsInitialized;
    
    /** Schema version of this registry */
    uint32 SchemaVersion;
    
    /** Lock for thread-safe access to the registry maps */
    mutable FSpinLock RegistryLock;
    
    /** Singleton instance of the registry */
    static FSDFTypeRegistry* Singleton;
    
    /** Thread-safe initialization flag for the singleton */
    static FThreadSafeBool bSingletonInitialized;
};