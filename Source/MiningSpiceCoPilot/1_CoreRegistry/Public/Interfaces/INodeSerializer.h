// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "../../Public/SVONodeTypes.h"
#include "INodeSerializer.generated.h"

/**
 * Interface for node serialization services
 * Provides operations for serializing and deserializing SVO node data
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UNodeSerializer : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for node serializers
 * Implementations handle node-specific serialization logic
 */
class MININGSPICECOPILOT_API INodeSerializer
{
    GENERATED_BODY()

public:
    /**
     * Serializes a node to binary data
     * @param InNodeId Node identifier to serialize
     * @param OutData Output buffer to receive serialized data
     * @return True if serialization was successful
     */
    virtual bool SerializeNode(uint64 InNodeId, TArray<uint8>& OutData) const = 0;
    
    /**
     * Deserializes a node from binary data
     * @param InData Binary data to deserialize from
     * @param OutNodeId Output parameter to receive the created node ID
     * @param InParentNode Optional parent node reference
     * @return True if deserialization was successful
     */
    virtual bool DeserializeNode(const TArray<uint8>& InData, uint64& OutNodeId, uint64 InParentNode = 0) = 0;
    
    /**
     * Gets the node class supported by this serializer
     * @return Node class enum value
     */
    virtual ESVONodeClass GetSupportedNodeClass() const = 0;
    
    /**
     * Gets the current serialization format version
     * @return Version number
     */
    virtual uint32 GetSerializationVersion() const = 0;
    
    /**
     * Checks if this serializer can handle a specific binary format version
     * @param InVersion Version to check compatibility with
     * @return True if the serializer can handle this version
     */
    virtual bool CanHandleVersion(uint32 InVersion) const = 0;
    
    /**
     * Gets the region identifier this serializer is responsible for
     * @return Region identifier
     */
    virtual int32 GetRegionId() const = 0;
}; 