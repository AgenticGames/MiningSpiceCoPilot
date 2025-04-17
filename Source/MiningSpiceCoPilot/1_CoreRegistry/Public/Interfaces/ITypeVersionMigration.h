// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Information for type version migration in the SVO+SDF mining system
 * Used to upgrade data from one version to another during type version changes
 */
struct MININGSPICECOPILOT_API FTypeVersionMigrationInfo
{
    /** ID of the type being migrated */
    uint32 TypeId;
    
    /** Source version number */
    uint32 SourceVersion;
    
    /** Target version number */
    uint32 TargetVersion;
    
    /** Name of the type */
    FName TypeName;
    
    /** Size of data in bytes for the source version */
    uint32 SourceDataSize;
    
    /** Size of data in bytes for the target version */
    uint32 TargetDataSize;
    
    /** Whether this migration requires reallocation of memory */
    bool bRequiresReallocation;
    
    /** Whether fields were added during this migration */
    bool bFieldsAdded;
    
    /** Whether fields were removed during this migration */
    bool bFieldsRemoved;
    
    /** Whether fields were reordered during this migration */
    bool bFieldsReordered;
    
    /** Whether field types were changed during this migration */
    bool bFieldTypesChanged;
    
    /** Migration priority (higher numbers = higher priority) */
    int32 Priority;
    
    /** Default constructor */
    FTypeVersionMigrationInfo()
        : TypeId(0)
        , SourceVersion(0)
        , TargetVersion(0)
        , TypeName(NAME_None)
        , SourceDataSize(0)
        , TargetDataSize(0)
        , bRequiresReallocation(false)
        , bFieldsAdded(false)
        , bFieldsRemoved(false)
        , bFieldsReordered(false)
        , bFieldTypesChanged(false)
        , Priority(0)
    {
    }
    
    /**
     * Constructor with parameters
     * @param InTypeId ID of the type being migrated
     * @param InSourceVersion Source version number
     * @param InTargetVersion Target version number
     * @param InTypeName Name of the type
     */
    FTypeVersionMigrationInfo(uint32 InTypeId, uint32 InSourceVersion, uint32 InTargetVersion, FName InTypeName)
        : TypeId(InTypeId)
        , SourceVersion(InSourceVersion)
        , TargetVersion(InTargetVersion)
        , TypeName(InTypeName)
        , SourceDataSize(0)
        , TargetDataSize(0)
        , bRequiresReallocation(false)
        , bFieldsAdded(false)
        , bFieldsRemoved(false)
        , bFieldsReordered(false)
        , bFieldTypesChanged(false)
        , Priority(0)
    {
    }
}; 