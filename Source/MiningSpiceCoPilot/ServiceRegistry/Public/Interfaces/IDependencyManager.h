// IDependencyManager.h
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IDependencyManager.generated.h"

/** Service dependency type */
UENUM(BlueprintType)
enum class EDependencyType : uint8
{
    Required,       // Required dependency (service cannot start without it)
    Optional,       // Optional dependency (service can operate without it)
    Deferred,       // Dependency that can be resolved after service startup
    Cyclical        // Part of a circular dependency chain with special handling
};

/** Dependency resolution status */
UENUM(BlueprintType)
enum class EDependencyStatus : uint8
{
    NotResolved,    // Dependency not yet resolved
    Resolved,       // Dependency successfully resolved
    Missing,        // Required dependency is missing
    Unavailable,    // Optional dependency is unavailable
    Deferred,       // Deferred dependency pending resolution
    Error           // Error during dependency resolution
};

/** Service dependency information */
USTRUCT(BlueprintType)
struct FServiceDependency
{
    GENERATED_BODY()

    // Service type that is depended upon
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Service Dependency")
    FName DependencyName;
    
    // Class of the interface that is required
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Service Dependency")
    const UClass* InterfaceType = nullptr;
    
    // Type of dependency relationship
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Service Dependency")
    EDependencyType DependencyType = EDependencyType::Required;
    
    // Specific zone ID for zone-specific dependencies, INDEX_NONE for any
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Service Dependency")
    int32 ZoneID = INDEX_NONE;
    
    // Specific region ID for region-specific dependencies, INDEX_NONE for any
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Service Dependency")
    int32 RegionID = INDEX_NONE;
    
    // Minimum required version of the dependency, empty for any version
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Service Dependency")
    FString MinVersion;
    
    // Current resolution status
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Service Dependency")
    EDependencyStatus Status = EDependencyStatus::NotResolved;
    
    // Error message if resolution failed
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Service Dependency")
    FString ErrorMessage;
};

/** Dependency graph node */
USTRUCT(BlueprintType)
struct FDependencyNode
{
    GENERATED_BODY()

    // Service name
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dependency Graph")
    FName ServiceName;
    
    // Service interface class
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dependency Graph")
    const UClass* InterfaceType = nullptr;
    
    // Dependencies required by this service
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dependency Graph")
    TArray<FServiceDependency> Dependencies;
    
    // Start order priority (higher values start earlier)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dependency Graph")
    int32 StartPriority = 0;
    
    // Whether this service has been started
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dependency Graph")
    bool bStarted = false;
    
    // Whether this service has been visited during graph traversal
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dependency Graph")
    bool bVisited = false;
    
    // Whether this service is part of a circular dependency
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dependency Graph")
    bool bInCircularDependency = false;
};

/**
 * Base interface for dependency manager
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UDependencyManager : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for managing service dependencies in the SVO+SDF mining architecture
 * Provides dependency registration, resolution, and service startup ordering
 */
class MININGSPICECOPILOT_API IDependencyManager
{
    GENERATED_BODY()

public:
    /**
     * Initialize the dependency manager
     * @return True if successfully initialized
     */
    virtual bool Initialize() = 0;
    
    /**
     * Shutdown the dependency manager and cleanup resources
     */
    virtual void Shutdown() = 0;
    
    /**
     * Check if the dependency manager is initialized
     * @return True if initialized
     */
    virtual bool IsInitialized() const = 0;
    
    /**
     * Register a service with its dependencies
     * @param InServiceName Name of the service being registered
     * @param InInterfaceType Interface type of the service
     * @param InDependencies Array of dependencies required by this service
     * @param InStartPriority Priority for startup ordering (higher starts earlier)
     * @return True if registration was successful
     */
    virtual bool RegisterService(const FName& InServiceName, const UClass* InInterfaceType, const TArray<FServiceDependency>& InDependencies, int32 InStartPriority = 0) = 0;
    
    /**
     * Add a dependency to an existing service
     * @param InServiceName Name of the service to add dependency to
     * @param InDependency Dependency to add
     * @return True if dependency was added successfully
     */
    virtual bool AddDependency(const FName& InServiceName, const FServiceDependency& InDependency) = 0;
    
    /**
     * Resolve dependencies for a service
     * @param InServiceName Name of the service to resolve dependencies for
     * @param OutMissingDependencies Array to populate with missing dependencies
     * @return True if all required dependencies were resolved successfully
     */
    virtual bool ResolveDependencies(const FName& InServiceName, TArray<FServiceDependency>& OutMissingDependencies) = 0;
    
    /**
     * Resolve dependencies for all registered services
     * @param OutFailedServices Array to populate with services that failed dependency resolution
     * @return True if all services had their required dependencies resolved
     */
    virtual bool ResolveAllDependencies(TArray<FName>& OutFailedServices) = 0;
    
    /**
     * Get the dependency node for a service
     * @param InServiceName Name of the service to get dependency info for
     * @return Pointer to dependency node or nullptr if not found
     */
    virtual const FDependencyNode* GetDependencyNode(const FName& InServiceName) const = 0;
    
    /**
     * Get all registered dependency nodes
     * @return Array of all dependency nodes
     */
    virtual TArray<FDependencyNode> GetAllDependencyNodes() const = 0;
    
    /**
     * Check if a service has all required dependencies resolved
     * @param InServiceName Name of the service to check
     * @return True if all required dependencies are resolved
     */
    virtual bool HasRequiredDependencies(const FName& InServiceName) const = 0;
    
    /**
     * Calculate the optimal start order for services based on dependencies
     * @param OutStartOrder Array to populate with service names in start order
     * @param OutCircularDependencies Array to populate with circular dependency chains
     * @return True if a valid start order was calculated
     */
    virtual bool CalculateStartOrder(TArray<FName>& OutStartOrder, TArray<TArray<FName>>& OutCircularDependencies) = 0;
    
    /**
     * Notify that a service has started
     * @param InServiceName Name of the service that has started
     * @return True if the notification was processed successfully
     */
    virtual bool NotifyServiceStarted(const FName& InServiceName) = 0;
    
    /**
     * Notify that a service has stopped
     * @param InServiceName Name of the service that has stopped
     * @return True if the notification was processed successfully
     */
    virtual bool NotifyServiceStopped(const FName& InServiceName) = 0;
    
    /**
     * Detect circular dependencies
     * @param OutCircularDependencies Array to populate with circular dependency chains
     * @return True if circular dependencies were detected
     */
    virtual bool DetectCircularDependencies(TArray<TArray<FName>>& OutCircularDependencies) const = 0;
    
    /**
     * Visualize the dependency graph
     * @param OutGraphVisualization String representation of the dependency graph
     * @return True if visualization was successful
     */
    virtual bool VisualizeDependencyGraph(FString& OutGraphVisualization) const = 0;
    
    /**
     * Get the singleton instance of the dependency manager
     * @return Reference to the dependency manager instance
     */
    static IDependencyManager& Get();
};
