// IConfigSchema.h
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Templates/SharedPointer.h"
#include "Dom/JsonObject.h"
#include "IConfigSchema.generated.h"

/**
 * Defines validation schema type
 */
UENUM(BlueprintType)
enum class ESchemaValueType : uint8
{
    Bool,       // Boolean values
    Int,        // Integer values
    Float,      // Floating point values
    String,     // String values
    Vector,     // 3D vector values
    Rotator,    // Rotation values
    Color,      // Color values
    Enum,       // Enumeration values
    Array,      // Array of values
    Object,     // Nested object with properties
    Custom      // Custom data type with specialized validation
};

/**
 * Configuration value constraint
 */
USTRUCT(BlueprintType)
struct FConfigValueConstraint
{
    GENERATED_BODY()

    // Type of constraint
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Schema")
    FString Type;
    
    // Value for the constraint (interpretation depends on constraint type)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Schema")
    FString Value;
    
    // Error message to display when constraint is violated
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Schema")
    FText ErrorMessage;
    
    // Whether this constraint is a warning rather than an error
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Schema")
    bool bIsWarning = false;
};

/**
 * Item schema for array items - separate struct to avoid recursion
 */
USTRUCT(BlueprintType)
struct FArrayItemSchema
{
    GENERATED_BODY()

    // Property name
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Schema")
    FString Name;
    
    // Property data type
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Schema")
    ESchemaValueType ValueType = ESchemaValueType::String;
    
    // Default value as string
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Schema")
    FString DefaultValue;
    
    // Property description
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Schema")
    FText Description;
    
    // Is this property required?
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Schema")
    bool bRequired = false;
    
    // Is this property deprecated?
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Schema")
    bool bDeprecated = false;
    
    // Constraints for validation
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Schema")
    TArray<FConfigValueConstraint> Constraints;
    
    // For enum types, allowed values
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Schema")
    TArray<FString> EnumValues;
};

/**
 * Structure for nested properties to avoid recursion
 */
USTRUCT(BlueprintType)
struct FNestedPropertySchema
{
    GENERATED_BODY()
    
    // Property name
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Schema")
    FString Name;
    
    // Property data type
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Schema")
    ESchemaValueType ValueType = ESchemaValueType::String;
    
    // Default value as string
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Schema")
    FString DefaultValue;
    
    // Property description
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Schema")
    FText Description;
    
    // Is this property required?
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Schema")
    bool bRequired = false;
    
    // Is this property deprecated?
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Schema")
    bool bDeprecated = false;
    
    // Constraints for validation
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Schema")
    TArray<FConfigValueConstraint> Constraints;
};

/**
 * Configuration property schema definition
 */
USTRUCT(BlueprintType)
struct FConfigPropertySchema
{
    GENERATED_BODY()

    // Property name
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Schema")
    FString Name;
    
    // Property data type
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Schema")
    ESchemaValueType ValueType = ESchemaValueType::String;
    
    // Default value as string
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Schema")
    FString DefaultValue;
    
    // Property description
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Schema")
    FText Description;
    
    // Is this property required?
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Schema")
    bool bRequired = false;
    
    // Is this property deprecated?
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Schema")
    bool bDeprecated = false;
    
    // Constraints for validation
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Schema")
    TArray<FConfigValueConstraint> Constraints;
    
    // For object types, nested properties - using a non-recursive type
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Schema")
    TArray<FNestedPropertySchema> Properties;
    
    // For array types, schema for items - using separate struct to avoid recursion
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Schema")
    FArrayItemSchema ItemSchema;
    
    // Flag to indicate if the ItemSchema is valid and should be used
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Schema")
    bool bHasItemSchema = false;
    
    // For enum types, allowed values
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Schema")
    TArray<FString> EnumValues;
};

/**
 * Configuration section schema definition
 */
USTRUCT(BlueprintType)
struct FConfigSectionSchema
{
    GENERATED_BODY()

    // Section name
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Schema")
    FString Name;
    
    // Section description
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Schema")
    FText Description;
    
    // Properties in this section
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Schema")
    TArray<FConfigPropertySchema> Properties;
    
    // Is this section deprecated?
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Schema")
    bool bDeprecated = false;
    
    // Is this section required?
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Schema")
    bool bRequired = false;
};

/**
 * Configuration validation result
 */
USTRUCT(BlueprintType)
struct FConfigValidationResult
{
    GENERATED_BODY()

    // Is the configuration valid?
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Validation")
    bool bIsValid = true;
    
    // Error messages for invalid configurations
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Validation")
    TArray<FText> Errors;
    
    // Warning messages (valid but potentially problematic)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config Validation")
    TArray<FText> Warnings;
};

/**
 * Base interface for configuration schema
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UConfigSchema : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for configuration schema validation and serialization
 * Provides schema definition, validation, and serialization capabilities
 */
class MININGSPICECOPILOT_API IConfigSchema
{
    GENERATED_BODY()

public:
    /**
     * Initialize the configuration schema
     * @return True if successfully initialized
     */
    virtual bool Initialize() = 0;
    
    /**
     * Shutdown the configuration schema and cleanup
     */
    virtual void Shutdown() = 0;
    
    /**
     * Check if the configuration schema is initialized
     * @return True if initialized
     */
    virtual bool IsInitialized() const = 0;
    
    /**
     * Get the name of this schema
     * @return Schema name
     */
    virtual FName GetSchemaName() const = 0;
    
    /**
     * Get the version of this schema
     * @return Schema version
     */
    virtual FString GetSchemaVersion() const = 0;
    
    /**
     * Register a configuration section schema
     * @param InSection Configuration section schema to register
     * @return True if registration was successful
     */
    virtual bool RegisterSection(const FConfigSectionSchema& InSection) = 0;
    
    /**
     * Get a configuration section schema by name
     * @param InSectionName Name of the section to get
     * @return Pointer to section schema or nullptr if not found
     */
    virtual const FConfigSectionSchema* GetSection(const FString& InSectionName) const = 0;
    
    /**
     * Get all configuration section schemas
     * @return Array of all registered section schemas
     */
    virtual TArray<FConfigSectionSchema> GetAllSections() const = 0;
    
    /**
     * Validate configuration data against this schema
     * @param InData Configuration data to validate
     * @param OutResult Validation result
     * @return True if the configuration is valid
     */
    virtual bool ValidateConfig(const TSharedPtr<FJsonObject>& InData, FConfigValidationResult& OutResult) const = 0;
    
    /**
     * Create a default configuration based on schema defaults
     * @return JSON object with default configuration values
     */
    virtual TSharedPtr<FJsonObject> CreateDefaultConfig() const = 0;
    
    /**
     * Serialize the schema to JSON
     * @return JSON representation of the schema
     */
    virtual TSharedPtr<FJsonObject> SerializeSchema() const = 0;
    
    /**
     * Deserialize the schema from JSON
     * @param InSchema JSON representation of schema
     * @return True if deserialization was successful
     */
    virtual bool DeserializeSchema(const TSharedPtr<FJsonObject>& InSchema) = 0;
    
    /**
     * Migrate configuration data from a previous schema version
     * @param InData Configuration data in old format
     * @param InFromVersion Original schema version
     * @return Migrated configuration data
     */
    virtual TSharedPtr<FJsonObject> MigrateConfig(const TSharedPtr<FJsonObject>& InData, const FString& InFromVersion) const = 0;
    
    /**
     * Get the singleton instance of the config schema
     * @param InSchemaName Name of the schema to get
     * @return Reference to the config schema instance
     */
    static IConfigSchema* Get(const FName& InSchemaName);
};
