// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Public/Interfaces/IRegistry.h"
#include "Containers/Map.h"
#include "Templates/SharedPointer.h"
#include "HAL/ThreadSafeBool.h"
#include "Misc/SpinLock.h"
#include "Math/AlignmentTemplates.h"

/**
 * SVO node class types for classification in the registry
 */
enum class ESVONodeClass : uint8
{
    /** Homogeneous nodes with a single material throughout */
    Homogeneous,
    
    /** Interface nodes that contain multiple materials */
    Interface,
    
    /** Empty nodes with no material */
    Empty,
    
    /** Custom node type for specialized behavior */
    Custom
};

/**
 * Structure containing metadata for an SVO node type
 * Memory aligned for SIMD operations
 */
struct MININGSPICECOPILOT_API FSVONodeTypeInfo
{
    /** Unique ID for this node type */
    uint32 TypeId;
    
    /** Name of this node type */
    FName TypeName;
    
    /** Classification of this node type */
    ESVONodeClass NodeClass;
    
    /** Version of this node type's schema */
    uint32 SchemaVersion;
    
    /** Memory alignment requirements for this node type */
    uint32 AlignmentRequirement;
    
    /** Size of the node type data in bytes */
    uint32 DataSize;
    
    /** Whether this node type supports material relationships */
    bool bSupportsMaterialRelationships;
    
    /** Whether this node type supports serialization */
    bool bSupportsSerializaton;
    
    /** Whether this node type supports SIMD operations */
    bool bSupportsSIMD;
};

/**
 * Registry for SVO node types in the mining system
 * Handles type registration, node classification, and memory layout management
 */
class MININGSPICECOPILOT_API FSVOTypeRegistry : public IRegistry
{
public:
    /** Default constructor */
    FSVOTypeRegistry();
    
    /** Destructor */
    virtual ~FSVOTypeRegistry();
    
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
     * Registers a new SVO node type with the registry
     * @param InTypeName Name of the node type
     * @param InNodeClass Classification of this node type
     * @param InDataSize Size of the node data in bytes
     * @param InAlignmentRequirement Memory alignment requirement (must be power of 2)
     * @param bInSupportsMaterialRelationships Whether this node supports material relationships
     * @return Unique ID for the registered type, or 0 if registration failed
     */
    uint32 RegisterNodeType(
        const FName& InTypeName, 
        ESVONodeClass InNodeClass, 
        uint32 InDataSize, 
        uint32 InAlignmentRequirement = 16, 
        bool bInSupportsMaterialRelationships = false);
    
    /**
     * Gets information about a registered node type
     * @param InTypeId Unique ID of the node type
     * @return Pointer to node type info, or nullptr if not found
     */
    const FSVONodeTypeInfo* GetNodeTypeInfo(uint32 InTypeId) const;
    
    /**
     * Gets information about a registered node type by name
     * @param InTypeName Name of the node type
     * @return Pointer to node type info, or nullptr if not found
     */
    const FSVONodeTypeInfo* GetNodeTypeInfoByName(const FName& InTypeName) const;
    
    /**
     * Gets all registered node types
     * @return Array of all node type infos
     */
    TArray<FSVONodeTypeInfo> GetAllNodeTypes() const;
    
    /**
     * Gets all registered node types of a specific class
     * @param InNodeClass Classification to filter by
     * @return Array of matching node type infos
     */
    TArray<FSVONodeTypeInfo> GetNodeTypesByClass(ESVONodeClass InNodeClass) const;
    
    /**
     * Checks if a node type is registered
     * @param InTypeId Unique ID of the node type
     * @return True if the type is registered
     */
    bool IsNodeTypeRegistered(uint32 InTypeId) const;
    
    /**
     * Checks if a node type is registered by name
     * @param InTypeName Name of the node type
     * @return True if the type is registered
     */
    bool IsNodeTypeRegistered(const FName& InTypeName) const;
    
    /** Gets the singleton instance of the SVO type registry */
    static FSVOTypeRegistry& Get();
    
private:
    /** Generates a unique type ID for new registrations */
    uint32 GenerateUniqueTypeId();
    
    /** Map of registered node types by ID */
    TMap<uint32, TSharedRef<FSVONodeTypeInfo>> NodeTypeMap;
    
    /** Map of registered node types by name for fast lookup */
    TMap<FName, uint32> NodeTypeNameMap;
    
    /** Counter for generating unique type IDs */
    uint32 NextTypeId;
    
    /** Thread-safe flag indicating if the registry has been initialized */
    FThreadSafeBool bIsInitialized;
    
    /** Schema version of this registry */
    uint32 SchemaVersion;
    
    /** Lock for thread-safe access to the type maps */
    mutable FSpinLock RegistryLock;
    
    /** Singleton instance of the registry */
    static FSVOTypeRegistry* Singleton;
    
    /** Thread-safe initialization flag for the singleton */
    static FThreadSafeBool bSingletonInitialized;
};