// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IRegistry.h"
#include "Containers/Map.h"
#include "Templates/SharedPointer.h"
#include "HAL/ThreadSafeBool.h"
#include "Misc/SpinLock.h"

/**
 * SDF field operation types for CSG operations
 */
enum class ESDFOperationType : uint8
{
    /** Union of two SDF fields */
    Union,
    
    /** Subtraction of two SDF fields */
    Subtraction,
    
    /** Intersection of two SDF fields */
    Intersection,
    
    /** Smooth union of two SDF fields */
    SmoothUnion,
    
    /** Smooth subtraction of two SDF fields */
    SmoothSubtraction,
    
    /** Smooth intersection of two SDF fields */
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
    
    /** Game logic and gameplay */
    GameLogic
};

/**
 * Information about a registered SDF field type
 */
struct MININGSPICECOPILOT_API FSDFFieldTypeInfo
{
    /** Unique ID for this field type */
    uint32 TypeId;
    
    /** Name of this field type */
    FName TypeName;
    
    /** Number of channels supported by this field */
    uint32 ChannelCount;
    
    /** Version of this field type's schema */
    uint32 SchemaVersion;
    
    /** Alignment requirements for field data */
    uint32 AlignmentRequirement;
    
    /** Whether this field type supports GPU evaluation */
    bool bSupportsGPU;
    
    /** Whether this field type supports multi-threaded evaluation */
    bool bSupportsThreading;
    
    /** Whether this field type supports SIMD operations */
    bool bSupportsSIMD;
};

/**
 * Information about a registered SDF operation
 */
struct MININGSPICECOPILOT_API FSDFOperationInfo
{
    /** Unique ID for this operation */
    uint32 OperationId;
    
    /** Name of this operation */
    FName OperationName;
    
    /** Type of CSG operation */
    ESDFOperationType OperationType;
    
    /** Number of input fields required */
    uint32 InputCount;
    
    /** Whether operation supports variable smoothing */
    bool bSupportsSmoothing;
    
    /** Whether operation preserves field sign */
    bool bPreservesSign;
    
    /** Whether operation is commutative */
    bool bIsCommutative;
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
     * Registers a new SDF field type
     * @param InTypeName Name of the field type
     * @param InChannelCount Number of channels supported
     * @param InAlignmentRequirement Memory alignment requirement (must be power of 2)
     * @param bInSupportsGPU Whether this field type supports GPU evaluation
     * @return Unique ID for the registered type, or 0 if registration failed
     */
    uint32 RegisterFieldType(
        const FName& InTypeName,
        uint32 InChannelCount,
        uint32 InAlignmentRequirement = 16,
        bool bInSupportsGPU = false);

    /**
     * Registers a new SDF operation
     * @param InOperationName Name of the operation
     * @param InOperationType Type of CSG operation
     * @param InInputCount Number of input fields required
     * @param bInSupportsSmoothing Whether operation supports smoothing
     * @return Unique ID for the registered operation, or 0 if registration failed
     */
    uint32 RegisterOperation(
        const FName& InOperationName,
        ESDFOperationType InOperationType,
        uint32 InInputCount,
        bool bInSupportsSmoothing = false);
        
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
     * Gets information about a registered operation
     * @param InOperationId Unique ID of the operation
     * @return Pointer to operation info, or nullptr if not found
     */
    const FSDFOperationInfo* GetOperationInfo(uint32 InOperationId) const;
    
    /**
     * Gets information about a registered operation by name
     * @param InOperationName Name of the operation
     * @return Pointer to operation info, or nullptr if not found
     */
    const FSDFOperationInfo* GetOperationInfoByName(const FName& InOperationName) const;
    
    /**
     * Gets all registered field types
     * @return Array of all field type infos
     */
    TArray<FSDFFieldTypeInfo> GetAllFieldTypes() const;
    
    /**
     * Gets all registered operations
     * @return Array of all operation infos
     */
    TArray<FSDFOperationInfo> GetAllOperations() const;
    
    /**
     * Checks if a field type is registered
     * @param InTypeId Unique ID of the field type
     * @return True if the type is registered
     */
    bool IsFieldTypeRegistered(uint32 InTypeId) const;
    
    /**
     * Checks if an operation is registered
     * @param InOperationId Unique ID of the operation
     * @return True if the operation is registered
     */
    bool IsOperationRegistered(uint32 InOperationId) const;
    
    /** Gets the singleton instance of the SDF type registry */
    static FSDFTypeRegistry& Get();
    
private:
    /** Generates a unique type ID for new field type registrations */
    uint32 GenerateUniqueTypeId();
    
    /** Generates a unique ID for new operation registrations */
    uint32 GenerateUniqueOperationId();
    
    /** Map of registered field types by ID */
    TMap<uint32, TSharedRef<FSDFFieldTypeInfo>> FieldTypeMap;
    
    /** Map of field types by name for fast lookup */
    TMap<FName, uint32> FieldTypeNameMap;
    
    /** Map of registered operations by ID */
    TMap<uint32, TSharedRef<FSDFOperationInfo>> OperationMap;
    
    /** Map of operations by name for fast lookup */
    TMap<FName, uint32> OperationNameMap;
    
    /** Counter for generating unique field type IDs */
    uint32 NextTypeId;
    
    /** Counter for generating unique operation IDs */
    uint32 NextOperationId;
    
    /** Thread-safe flag indicating if the registry has been initialized */
    FThreadSafeBool bIsInitialized;
    
    /** Schema version of this registry */
    uint32 SchemaVersion;
    
    /** Lock for thread-safe access to the registry maps */
    mutable FRWLock RegistryLock;
    
    /** Singleton instance of the registry */
    static FSDFTypeRegistry* Singleton;
    
    /** Thread-safe initialization flag for the singleton */
    static FThreadSafeBool bSingletonInitialized;
};