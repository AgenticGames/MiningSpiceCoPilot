// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Templates/SharedPointer.h"
#include "Templates/UnrealTemplate.h"
#include "Containers/Map.h"
#include "Containers/Array.h"
#include "Interfaces/IRegistry.h"

/**
 * Base class for type registry implementations
 * Provides core functionality for type registration and resolution
 */
class MININGSPICECOPILOT_API FTypeRegistry
{
public:
    /** Singleton accessor */
    static FTypeRegistry& Get();

    /** Constructor */
    FTypeRegistry();

    /** Destructor */
    virtual ~FTypeRegistry();

    /**
     * Registers a new type with the registry
     * @param TypeId Unique type identifier
     * @param TypeName Human-readable type name
     * @return True if registration succeeded
     */
    bool RegisterType(uint32 TypeId, const FName& TypeName);

    /**
     * Resolves a type name from its ID
     * @param TypeId Type identifier to look up
     * @return Type name or NAME_None if not found
     */
    FName GetTypeName(uint32 TypeId) const;

    /**
     * Resolves a type ID from its name
     * @param TypeName Type name to look up
     * @return Type ID or 0 if not found
     */
    uint32 GetTypeId(const FName& TypeName) const;

    /**
     * Checks if a type is registered
     * @param TypeId Type identifier to check
     * @return True if type is registered
     */
    bool IsTypeRegistered(uint32 TypeId) const;

    /**
     * Gets all registered type IDs
     * @return Array of registered type IDs
     */
    TArray<uint32> GetAllRegisteredTypes() const;

private:
    /** Map of type IDs to type names */
    TMap<uint32, FName> TypeIdToName;

    /** Map of type names to type IDs */
    TMap<FName, uint32> TypeNameToId;

    /** Critical section for thread-safe access */
    mutable FCriticalSection RegistryLock;

    /** Singleton instance */
    static TSharedPtr<FTypeRegistry> SingletonInstance;
}; 