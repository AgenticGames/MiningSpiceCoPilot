// DependencyManager.cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "DependencyManager.h"
#include "Logging/LogMacros.h"

// Static instance initialization
FDependencyManager* FDependencyManager::Instance = nullptr;

// Define log category
DEFINE_LOG_CATEGORY_STATIC(LogDependencyManager, Log, All);

FDependencyManager::FDependencyManager()
    : bIsInitialized(false)
{
    // Initialize empty
}

FDependencyManager::~FDependencyManager()
{
    // Ensure we're shut down
    if (bIsInitialized)
    {
        Shutdown();
    }
}

bool FDependencyManager::Initialize()
{
    FScopeLock Lock(&CriticalSection);
    
    if (bIsInitialized)
    {
        UE_LOG(LogDependencyManager, Warning, TEXT("DependencyManager already initialized"));
        return true;
    }
    
    UE_LOG(LogDependencyManager, Log, TEXT("Initializing DependencyManager"));
    Dependencies.Empty();
    bIsInitialized = true;
    
    return true;
}

void FDependencyManager::Shutdown()
{
    FScopeLock Lock(&CriticalSection);
    
    if (!bIsInitialized)
    {
        UE_LOG(LogDependencyManager, Warning, TEXT("DependencyManager not initialized, cannot shutdown"));
        return;
    }
    
    UE_LOG(LogDependencyManager, Log, TEXT("Shutting down DependencyManager"));
    Dependencies.Empty();
    bIsInitialized = false;
}

bool FDependencyManager::IsInitialized() const
{
    return bIsInitialized;
}

bool FDependencyManager::RegisterDependency(const UClass* InDependentType, const UClass* InDependencyType, bool bIsMandatory)
{
    if (!InDependentType || !InDependencyType)
    {
        UE_LOG(LogDependencyManager, Error, TEXT("Cannot register dependency with null service type"));
        return false;
    }
    
    if (InDependentType == InDependencyType)
    {
        UE_LOG(LogDependencyManager, Error, TEXT("Service %s cannot depend on itself"), 
            *InDependentType->GetName());
        return false;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    if (!bIsInitialized)
    {
        UE_LOG(LogDependencyManager, Error, TEXT("DependencyManager not initialized, cannot register dependency"));
        return false;
    }
    
    // Get or create dependency info for dependent service
    FDependencyInfo& DependencyInfo = Dependencies.FindOrAdd(InDependentType);
    
    // Add dependency
    if (bIsMandatory)
    {
        DependencyInfo.MandatoryDependencies.Add(InDependencyType);
        UE_LOG(LogDependencyManager, Log, TEXT("Registered mandatory dependency: %s depends on %s"),
            *InDependentType->GetName(), *InDependencyType->GetName());
    }
    else
    {
        DependencyInfo.OptionalDependencies.Add(InDependencyType);
        UE_LOG(LogDependencyManager, Log, TEXT("Registered optional dependency: %s optionally depends on %s"),
            *InDependentType->GetName(), *InDependencyType->GetName());
    }
    
    // Check for cycles
    TArray<TPair<const UClass*, const UClass*>> CyclicDependencies;
    if (!CheckForCycles(CyclicDependencies))
    {
        // Remove the dependency if it creates a cycle
        if (bIsMandatory)
        {
            DependencyInfo.MandatoryDependencies.Remove(InDependencyType);
        }
        else
        {
            DependencyInfo.OptionalDependencies.Remove(InDependencyType);
        }
        
        UE_LOG(LogDependencyManager, Error, TEXT("Cannot register dependency: %s depends on %s - would create a cycle"),
            *InDependentType->GetName(), *InDependencyType->GetName());
            
        // Log the cycle
        for (const auto& CyclePair : CyclicDependencies)
        {
            UE_LOG(LogDependencyManager, Error, TEXT("Cyclic dependency: %s -> %s"),
                *CyclePair.Key->GetName(), *CyclePair.Value->GetName());
        }
        
        return false;
    }
    
    return true;
}

void FDependencyManager::GetDependencies(const UClass* InServiceType, TArray<const UClass*>& OutMandatoryDependencies, TArray<const UClass*>& OutOptionalDependencies) const
{
    OutMandatoryDependencies.Empty();
    OutOptionalDependencies.Empty();
    
    if (!InServiceType)
    {
        return;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    if (!bIsInitialized)
    {
        return;
    }
    
    // Find dependency info
    const FDependencyInfo* DependencyInfo = Dependencies.Find(InServiceType);
    if (!DependencyInfo)
    {
        // No dependencies registered
        return;
    }
    
    // Add dependencies to output arrays
    OutMandatoryDependencies.Append(DependencyInfo->MandatoryDependencies.Array());
    OutOptionalDependencies.Append(DependencyInfo->OptionalDependencies.Array());
}

bool FDependencyManager::DependsOn(const UClass* InDependentType, const UClass* InDependencyType, bool bCheckTransitive) const
{
    if (!InDependentType || !InDependencyType || InDependentType == InDependencyType)
    {
        return false;
    }
    
    FScopeLock Lock(&CriticalSection);
    
    if (!bIsInitialized)
    {
        return false;
    }
    
    // Find dependency info
    const FDependencyInfo* DependencyInfo = Dependencies.Find(InDependentType);
    if (!DependencyInfo)
    {
        // No dependencies registered
        return false;
    }
    
    // Check direct dependencies
    if (DependencyInfo->MandatoryDependencies.Contains(InDependencyType) ||
        DependencyInfo->OptionalDependencies.Contains(InDependencyType))
    {
        return true;
    }
    
    // Check transitive dependencies if requested
    if (bCheckTransitive)
    {
        // Check all mandatory dependencies
        for (const UClass* DirectDependency : DependencyInfo->MandatoryDependencies)
        {
            if (DependsOn(DirectDependency, InDependencyType, true))
            {
                return true;
            }
        }
        
        // Check all optional dependencies
        for (const UClass* DirectDependency : DependencyInfo->OptionalDependencies)
        {
            if (DependsOn(DirectDependency, InDependencyType, true))
            {
                return true;
            }
        }
    }
    
    // No dependency found
    return false;
}

bool FDependencyManager::CalculateInitializationOrder(const TArray<const UClass*>& InServiceTypes, 
                                                    TArray<const UClass*>& OutOrderedServices,
                                                    TArray<TPair<const UClass*, const UClass*>>& OutCyclicDependencies) const
{
    OutOrderedServices.Empty();
    OutCyclicDependencies.Empty();
    
    FScopeLock Lock(&CriticalSection);
    
    if (!bIsInitialized)
    {
        UE_LOG(LogDependencyManager, Error, TEXT("DependencyManager not initialized, cannot calculate initialization order"));
        return false;
    }
    
    // Check for cycles first
    if (!CheckForCycles(OutCyclicDependencies))
    {
        UE_LOG(LogDependencyManager, Error, TEXT("Cannot calculate initialization order: dependency cycle detected"));
        return false;
    }
    
    // Perform topological sort
    TSet<const UClass*> VisitedNodes;
    OutOrderedServices.Reserve(InServiceTypes.Num());
    
    // Visit each node
    for (const UClass* ServiceType : InServiceTypes)
    {
        if (!VisitedNodes.Contains(ServiceType))
        {
            VisitNodeForSort(ServiceType, VisitedNodes, OutOrderedServices);
        }
    }
    
    // The array is currently in reverse order (dependencies last), so we need to reverse it
    Algo::Reverse(OutOrderedServices);
    
    UE_LOG(LogDependencyManager, Log, TEXT("Calculated initialization order for %d services"), OutOrderedServices.Num());
    return true;
}

bool FDependencyManager::CheckForCycles(TArray<TPair<const UClass*, const UClass*>>& OutCyclicDependencies) const
{
    OutCyclicDependencies.Empty();
    
    FScopeLock Lock(&CriticalSection);
    
    if (!bIsInitialized)
    {
        return true; // No cycles in an empty graph
    }
    
    TSet<const UClass*> VisitedNodes;
    TSet<const UClass*> CurrentPath;
    bool bNoCycles = true;
    
    // Check each service type
    for (const auto& ServicePair : Dependencies)
    {
        const UClass* ServiceType = ServicePair.Key;
        
        if (!VisitedNodes.Contains(ServiceType))
        {
            if (!VisitNode(ServiceType, VisitedNodes, CurrentPath, OutCyclicDependencies))
            {
                bNoCycles = false;
                // Continue checking to find all cycles
            }
        }
    }
    
    return bNoCycles;
}

bool FDependencyManager::ValidateDependencies(const TArray<const UClass*>& InServiceTypes, 
                                            TArray<TPair<const UClass*, const UClass*>>& OutMissingDependencies) const
{
    OutMissingDependencies.Empty();
    
    FScopeLock Lock(&CriticalSection);
    
    if (!bIsInitialized)
    {
        UE_LOG(LogDependencyManager, Error, TEXT("DependencyManager not initialized, cannot validate dependencies"));
        return false;
    }
    
    // Create a set of available services for faster lookup
    TSet<const UClass*> AvailableServices;
    for (const UClass* ServiceType : InServiceTypes)
    {
        AvailableServices.Add(ServiceType);
    }
    
    // Check each service's dependencies
    bool bAllDependenciesSatisfied = true;
    
    for (const UClass* ServiceType : InServiceTypes)
    {
        const FDependencyInfo* DependencyInfo = Dependencies.Find(ServiceType);
        if (!DependencyInfo)
        {
            // No dependencies to check
            continue;
        }
        
        // Check mandatory dependencies
        for (const UClass* DependencyType : DependencyInfo->MandatoryDependencies)
        {
            if (!AvailableServices.Contains(DependencyType))
            {
                OutMissingDependencies.Add(TPair<const UClass*, const UClass*>(ServiceType, DependencyType));
                bAllDependenciesSatisfied = false;
                
                UE_LOG(LogDependencyManager, Error, TEXT("Missing mandatory dependency: %s requires %s"),
                    *ServiceType->GetName(), *DependencyType->GetName());
            }
        }
    }
    
    return bAllDependenciesSatisfied;
}

bool FDependencyManager::VisitNode(const UClass* InServiceType, 
                                TSet<const UClass*>& VisitedNodes, 
                                TSet<const UClass*>& CurrentPath,
                                TArray<TPair<const UClass*, const UClass*>>& OutCyclicDependencies) const
{
    // Add to current path
    CurrentPath.Add(InServiceType);
    
    // Get dependencies
    const FDependencyInfo* DependencyInfo = Dependencies.Find(InServiceType);
    bool bNoCycles = true;
    
    if (DependencyInfo)
    {
        // Check all mandatory dependencies
        for (const UClass* DependencyType : DependencyInfo->MandatoryDependencies)
        {
            if (CurrentPath.Contains(DependencyType))
            {
                // Cycle detected
                OutCyclicDependencies.Add(TPair<const UClass*, const UClass*>(InServiceType, DependencyType));
                bNoCycles = false;
            }
            else if (!VisitedNodes.Contains(DependencyType))
            {
                if (!VisitNode(DependencyType, VisitedNodes, CurrentPath, OutCyclicDependencies))
                {
                    bNoCycles = false;
                }
            }
        }
        
        // Check all optional dependencies
        for (const UClass* DependencyType : DependencyInfo->OptionalDependencies)
        {
            if (CurrentPath.Contains(DependencyType))
            {
                // Cycle detected
                OutCyclicDependencies.Add(TPair<const UClass*, const UClass*>(InServiceType, DependencyType));
                bNoCycles = false;
            }
            else if (!VisitedNodes.Contains(DependencyType))
            {
                if (!VisitNode(DependencyType, VisitedNodes, CurrentPath, OutCyclicDependencies))
                {
                    bNoCycles = false;
                }
            }
        }
    }
    
    // Mark as visited
    VisitedNodes.Add(InServiceType);
    
    // Remove from current path
    CurrentPath.Remove(InServiceType);
    
    return bNoCycles;
}

void FDependencyManager::VisitNodeForSort(const UClass* InServiceType,
                                       TSet<const UClass*>& VisitedNodes,
                                       TArray<const UClass*>& OutOrderedServices) const
{
    // Mark as visited
    VisitedNodes.Add(InServiceType);
    
    // Get dependencies
    const FDependencyInfo* DependencyInfo = Dependencies.Find(InServiceType);
    
    if (DependencyInfo)
    {
        // Visit all mandatory dependencies first
        for (const UClass* DependencyType : DependencyInfo->MandatoryDependencies)
        {
            if (!VisitedNodes.Contains(DependencyType))
            {
                VisitNodeForSort(DependencyType, VisitedNodes, OutOrderedServices);
            }
        }
        
        // Then visit all optional dependencies
        for (const UClass* DependencyType : DependencyInfo->OptionalDependencies)
        {
            if (!VisitedNodes.Contains(DependencyType))
            {
                VisitNodeForSort(DependencyType, VisitedNodes, OutOrderedServices);
            }
        }
    }
    
    // Add to ordered list (in reverse order)
    OutOrderedServices.Add(InServiceType);
}

FDependencyManager& FDependencyManager::Get()
{
    if (!Instance)
    {
        Instance = new FDependencyManager();
        Instance->Initialize();
    }
    
    return *Instance;
}