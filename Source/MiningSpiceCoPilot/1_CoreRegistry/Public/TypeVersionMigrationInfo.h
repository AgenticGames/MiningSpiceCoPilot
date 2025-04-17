// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Structure containing information for type version migration
 * Used to migrate memory between different versions of the same type
 */
struct MININGSPICECOPILOT_API FTypeVersionMigrationInfo
{
    /** The type ID */
    uint32 TypeId;
    
    /** The type name */
    FName TypeName;
    
    /** The old version of the type */
    uint32 OldVersion;
    
    /** The new version of the type */
    uint32 NewVersion;
    
    /** Size of the type data in bytes */
    uint32 DataSize;
    
    /** Memory alignment requirement for the type */
    uint32 AlignmentRequirement;
    
    /** Constructor */
    FTypeVersionMigrationInfo()
        : TypeId(0)
        , TypeName(NAME_None)
        , OldVersion(0)
        , NewVersion(0)
        , DataSize(0)
        , AlignmentRequirement(16)
    {
    }
}; 