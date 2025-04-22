// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Shared types and enums for the Service Registry and Dependency Management system
 */

/**
 * Service version information
 */
struct MININGSPICECOPILOT_API FServiceVersion
{
    int32 Major = 1;
    int32 Minor = 0;
    int32 Patch = 0;
    
    FServiceVersion() = default;
    FServiceVersion(int32 InMajor, int32 InMinor, int32 InPatch)
        : Major(InMajor), Minor(InMinor), Patch(InPatch)
    {}
    
    bool IsCompatibleWith(const FServiceVersion& Other) const
    {
        return Major == Other.Major;
    }
    
    FString ToString() const
    {
        return FString::Printf(TEXT("%d.%d.%d"), Major, Minor, Patch);
    }
};

/**
 * Service dependency options
 */
enum class EServiceDependencyType : uint8
{
    Required,   // Service must be present
    Optional,   // Service is used if present but not required
    Conditional // Service is required under certain conditions
};

/**
 * Service health status
 */
enum class EServiceHealthStatus : uint8
{
    Healthy,      // Service is functioning normally
    Degraded,     // Service is functioning but with reduced capabilities
    Critical,     // Service is functioning but at risk of failure
    Failed,       // Service has failed but may be recoverable
    Unresponsive, // Service is not responding to health checks
    Unknown       // Service health cannot be determined
};

/**
 * Service scope types
 */
enum class EServiceScope : uint8
{
    Global,  // Available to all regions and zones
    Region,  // Available to all zones in a region
    Zone,    // Available only within a specific zone
    Custom   // Custom scope with special resolution rules
};

/**
 * Lifecycle phases for service initialization and shutdown
 */
enum class EServiceLifecyclePhase : uint8
{
    PreInitialize,   // Preparation phase before full initialization
    Initialize,      // Main initialization phase
    PostInitialize,  // Final setup phase after main initialization
    PreShutdown,     // Preparation phase before full shutdown
    Shutdown,        // Main shutdown phase
    PostShutdown     // Final cleanup phase after main shutdown
};

/**
 * Service dependency declaration structure
 */
struct MININGSPICECOPILOT_API FServiceDependency
{
    TSubclassOf<UInterface> DependencyType;           // Interface type of the dependency
    EServiceDependencyType DependencyKind;            // Type of dependency
    
    FServiceDependency() : DependencyType(nullptr), DependencyKind(EServiceDependencyType::Required) {}
    FServiceDependency(TSubclassOf<UInterface> InType, EServiceDependencyType InKind = EServiceDependencyType::Required)
        : DependencyType(InType), DependencyKind(InKind) {}
};

/**
 * Service configuration structure for runtime configuration
 */
struct MININGSPICECOPILOT_API FServiceConfig
{
    TMap<FName, FString> ConfigValues;
    
    FString GetValue(const FName& Key, const FString& DefaultValue = TEXT("")) const
    {
        const FString* Value = ConfigValues.Find(Key);
        return Value ? *Value : DefaultValue;
    }
    
    int32 GetValueAsInt(const FName& Key, int32 DefaultValue = 0) const
    {
        const FString* Value = ConfigValues.Find(Key);
        return Value ? FCString::Atoi(**Value) : DefaultValue;
    }
    
    float GetValueAsFloat(const FName& Key, float DefaultValue = 0.0f) const
    {
        const FString* Value = ConfigValues.Find(Key);
        return Value ? FCString::Atof(**Value) : DefaultValue;
    }
    
    bool GetValueAsBool(const FName& Key, bool DefaultValue = false) const
    {
        const FString* Value = ConfigValues.Find(Key);
        if (!Value)
        {
            return DefaultValue;
        }
        return Value->ToBool();
    }
    
    void SetValue(const FName& Key, const FString& Value)
    {
        ConfigValues.Add(Key, Value);
    }
};

/**
 * Service health information structure
 */
struct MININGSPICECOPILOT_API FServiceHealth
{
    enum class EStatus : uint8
    {
        Healthy,      // Service is functioning normally
        Degraded,     // Service is functioning but with reduced capabilities
        Critical,     // Service is functioning but at risk of failure
        Failed,       // Service has failed but may be recoverable
        Unresponsive, // Service is not responding to health checks
        Unknown       // Service health cannot be determined
    } Status;
    
    FString DiagnosticMessage;
    float PerformanceMetric;
    int32 ErrorCount;
    int32 WarningCount;
    
    FServiceHealth()
        : Status(EStatus::Unknown)
        , PerformanceMetric(0.0f)
        , ErrorCount(0)
        , WarningCount(0)
    {}
}; 