// IMaterialPropertyProvider.h
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IMaterialPropertyProvider.generated.h"

// This class does not need to be modified
UINTERFACE(MinimalAPI)
class UMaterialPropertyProvider : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for providing material properties to SDF factories and components
 */
class MININGSPICECOPILOT_API IMaterialPropertyProvider
{
    GENERATED_BODY()

public:
    /**
     * Get material property value
     * @param MaterialType Type of material to query
     * @param PropertyName Name of the property to retrieve
     * @param DefaultValue Default value if property doesn't exist
     * @return Property value or default if not found
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Material Properties")
    FString GetMaterialProperty(int32 MaterialType, const FName& PropertyName, const FString& DefaultValue) const;

    /**
     * Set material property value
     * @param MaterialType Type of material to modify
     * @param PropertyName Name of the property to set
     * @param Value Value to assign to the property
     * @return True if property was set successfully
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Material Properties")
    bool SetMaterialProperty(int32 MaterialType, const FName& PropertyName, const FString& Value);

    /**
     * Get material numeric property
     * @param MaterialType Type of material to query
     * @param PropertyName Name of the property to retrieve
     * @param DefaultValue Default value if property doesn't exist
     * @return Numeric property value or default if not found
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Material Properties")
    float GetMaterialNumericProperty(int32 MaterialType, const FName& PropertyName, float DefaultValue) const;

    /**
     * Check if material has specific property
     * @param MaterialType Type of material to query
     * @param PropertyName Name of the property to check
     * @return True if the property exists for this material
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Material Properties")
    bool HasMaterialProperty(int32 MaterialType, const FName& PropertyName) const;

    /**
     * Get all properties for a material type
     * @param MaterialType Type of material to query
     * @return Map of all properties for the specified material
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Material Properties")
    TMap<FName, FString> GetAllMaterialProperties(int32 MaterialType) const;
};