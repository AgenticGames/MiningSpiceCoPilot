// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FMaterialPropertyDependency.generated.h"

/**
 * Defines a dependency relationship between material properties
 * Used to track how properties in one material type affect properties in another
 */
USTRUCT()
struct MININGSPICECOPILOT_API FMaterialPropertyDependency
{
    GENERATED_BODY()

    /** Source material type identifier */
    UPROPERTY()
    uint32 SourceMaterialId;
    
    /** Name of the source property */
    UPROPERTY()
    FName SourcePropertyName;
    
    /** Target material type identifier */
    UPROPERTY()
    uint32 TargetMaterialId;
    
    /** Name of the target property */
    UPROPERTY()
    FName TargetPropertyName;
    
    /** Dependency strength/influence factor (0.0-1.0) */
    UPROPERTY()
    float InfluenceFactor;
    
    /** Whether changes to the source must always update the target */
    UPROPERTY()
    bool bIsRequired;
    
    /** Default constructor */
    FMaterialPropertyDependency()
        : SourceMaterialId(0)
        , SourcePropertyName(NAME_None)
        , TargetMaterialId(0)
        , TargetPropertyName(NAME_None)
        , InfluenceFactor(1.0f)
        , bIsRequired(true)
    {
    }
    
    /** Constructor with parameters */
    FMaterialPropertyDependency(uint32 InSourceMaterialId, FName InSourcePropertyName, uint32 InTargetMaterialId, FName InTargetPropertyName)
        : SourceMaterialId(InSourceMaterialId)
        , SourcePropertyName(InSourcePropertyName)
        , TargetMaterialId(InTargetMaterialId)
        , TargetPropertyName(InTargetPropertyName)
        , InfluenceFactor(1.0f)
        , bIsRequired(true)
    {
    }
    
    /** Equality operator */
    bool operator==(const FMaterialPropertyDependency& Other) const
    {
        return SourceMaterialId == Other.SourceMaterialId &&
               SourcePropertyName == Other.SourcePropertyName &&
               TargetMaterialId == Other.TargetMaterialId &&
               TargetPropertyName == Other.TargetPropertyName;
    }
    
    /** Hash function for use in containers */
    friend uint32 GetTypeHash(const FMaterialPropertyDependency& Dependency)
    {
        return HashCombine(
            HashCombine(
                GetTypeHash(Dependency.SourceMaterialId),
                GetTypeHash(Dependency.SourcePropertyName)
            ),
            HashCombine(
                GetTypeHash(Dependency.TargetMaterialId),
                GetTypeHash(Dependency.TargetPropertyName)
            )
        );
    }
}; 