// DependencyManager.h
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Containers/Map.h"
#include "Containers/Array.h"
#include "Containers/Set.h"
#include "Templates/SharedPointer.h"

/**
 * Dependency manager responsible for resolving service dependencies
 * and determining proper initialization order
 */
class FDependencyManager
{
public:
    /** Constructor */
    FDependencyManager();
    
    /** Destructor */
    virtual ~FDependencyManager();

    /**
     * Initialize the dependency manager
     * @return True if successfully initialized
     */
    bool Initialize();
    
    /**
     * Shutdown the dependency manager
     */
    void Shutdown();
    
    /**
     * Check if the dependency manager is initialized
     * @return True if initialized
     */
    bool IsInitialized() const;
    
    /**
     * Register a dependency between two services
     * @param InDependentType Service type that depends on another
     * @param InDependencyType Service type that is depended upon
     * @param bIsMandatory Whether this dependency is mandatory or optional
     * @return True if registration was successful
     */
    bool RegisterDependency(const UClass* InDependentType, const UClass* InDependencyType, bool bIsMandatory = true);
    
    /**
     * Get all dependencies for a service type
     * @param InServiceType Service type to get dependencies for
     * @param OutMandatoryDependencies Array to populate with mandatory dependencies
     * @param OutOptionalDependencies Array to populate with optional dependencies
     */
    void GetDependencies(const UClass* InServiceType, TArray<const UClass*>& OutMandatoryDependencies, TArray<const UClass*>& OutOptionalDependencies) const;
    
    /**
     * Check if one service depends on another
     * @param InDependentType Service type that might depend on another
     * @param InDependencyType Service type that might be depended upon
     * @param bCheckTransitive Whether to check for transitive dependencies
     * @return True if there is a dependency relationship
     */
    bool DependsOn(const UClass* InDependentType, const UClass* InDependencyType, bool bCheckTransitive = true) const;
    
    /**
     * Calculate initialization order for services
     * @param InServiceTypes Array of service types to order
     * @param OutOrderedServices Array to populate with ordered services
     * @param OutCyclicDependencies Array to populate with detected cyclic dependencies
     * @return True if ordering was successful (no cycles)
     */
    bool CalculateInitializationOrder(const TArray<const UClass*>& InServiceTypes, 
                                    TArray<const UClass*>& OutOrderedServices,
                                    TArray<TPair<const UClass*, const UClass*>>& OutCyclicDependencies) const;
    
    /**
     * Check for dependency cycles
     * @param OutCyclicDependencies Array to populate with detected cyclic dependencies
     * @return True if no cycles were detected
     */
    bool CheckForCycles(TArray<TPair<const UClass*, const UClass*>>& OutCyclicDependencies) const;
    
    /**
     * Validate dependencies for a set of services
     * @param InServiceTypes Array of service types to validate
     * @param OutMissingDependencies Array to populate with missing mandatory dependencies
     * @return True if all mandatory dependencies are satisfied
     */
    bool ValidateDependencies(const TArray<const UClass*>& InServiceTypes, 
                             TArray<TPair<const UClass*, const UClass*>>& OutMissingDependencies) const;
    
    /** Get singleton instance */
    static FDependencyManager& Get();

private:
    /** Dependency information */
    struct FDependencyInfo
    {
        /** Set of mandatory dependencies */
        TSet<const UClass*> MandatoryDependencies;
        
        /** Set of optional dependencies */
        TSet<const UClass*> OptionalDependencies;
    };

    /** Map of service dependencies */
    TMap<const UClass*, FDependencyInfo> Dependencies;

    /** Critical section for thread safety */
    mutable FCriticalSection CriticalSection;

    /** Flag indicating if the manager is initialized */
    bool bIsInitialized;

    /** Singleton instance */
    static FDependencyManager* Instance;
    
    /**
     * Visit a node in the service dependency graph (for cycle detection)
     * @param InServiceType Service type to visit
     * @param VisitedNodes Set of already visited nodes
     * @param InProgress Set of nodes currently being processed (for cycle detection)
     * @param OutCyclicDependencies Array to populate with detected cyclic dependencies
     * @return True if no cycles were detected
     */
    bool VisitNode(const UClass* InServiceType, 
                 TSet<const UClass*>& VisitedNodes, 
                 TSet<const UClass*>& InProgress,
                 TArray<TPair<const UClass*, const UClass*>>& OutCyclicDependencies) const;
                 
    /**
     * Visit a node in the service dependency graph (for topological sort)
     * @param InServiceType Service type to visit
     * @param VisitedNodes Set of already visited nodes
     * @param OutOrderedServices Array to populate with ordered services
     */
    void VisitNodeForSort(const UClass* InServiceType,
                        TSet<const UClass*>& VisitedNodes,
                        TArray<const UClass*>& OutOrderedServices) const;
};