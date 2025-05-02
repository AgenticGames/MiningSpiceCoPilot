// Copyright Epic Games, Inc. All Rights Reserved.

#include "ServiceLocator.h"
#include "../../3_ThreadingTaskSystem/Public/ThreadSafety.h"
#include "Async/TaskGraphInterfaces.h" // For thread ID
#include "HAL/PlatformTLS.h" // For thread-local storage
#include "HAL/PlatformProcess.h" // For current thread ID
#include "UObject/UObjectIterator.h" // For type iteration
#include "Misc/ScopeLock.h"

// Static member initialization
FServiceLocator* FServiceLocator::Singleton = nullptr;
FThreadSafeBool FServiceLocator::bSingletonInitialized = false;

// Define thread-local storage for service cache
thread_local FServiceLocator::FThreadLocalServiceCache* ThreadLocalCachePtr = nullptr;

// Implementation of thread-local cache accessor
FServiceLocator::FThreadLocalServiceCache& FServiceLocator::FThreadLocalServiceCache::Get()
{
    if (!ThreadLocalCachePtr)
    {
        ThreadLocalCachePtr = new FThreadLocalServiceCache();
    }
    return *ThreadLocalCachePtr;
}

FServiceLocator::FServiceLocator()
    : bIsInitialized(false)
{
    // Initialize NUMA topology
    NUMATopology.DetectTopology();
    
    // Create NUMA-local type caches
    // Since NUMATopology doesn't expose a GetDomainCount() method,
    // we'll initialize a reasonable default number of caches
    const uint32 DefaultDomainCount = 1; // Start with just one domain
    for (uint32 DomainId = 0; DomainId < DefaultDomainCount; ++DomainId)
    {
        DomainTypeCaches.Add(new FNUMALocalTypeCache(DomainId));
    }
    
    // Create optimized locks
    FastPathLock.SetPreferredDomain(0); // Default to first domain
}

FServiceLocator::~FServiceLocator()
{
    // Clean up NUMA-local type caches
    for (FNUMALocalTypeCache* Cache : DomainTypeCaches)
    {
        delete Cache;
    }
    DomainTypeCaches.Empty();
    
    // Clean up version counters
    for (auto& Pair : ServiceVersions)
    {
        delete Pair.Value;
    }
    ServiceVersions.Empty();
}

FServiceLocator& FServiceLocator::Get()
{
    if (!bSingletonInitialized)
    {
        // Thread-safe singleton initialization with double-checked locking
        static FCriticalSection InitLock;
        FScopeLock Lock(&InitLock);
        
        if (!bSingletonInitialized)
        {
            Singleton = new FServiceLocator();
            bSingletonInitialized = true;
            
            // Auto-initialize
            if (!Singleton->IsInitialized())
            {
                Singleton->Initialize();
            }
        }
    }
    
    check(Singleton);
    return *Singleton;
}

bool FServiceLocator::Initialize()
{
    if (bIsInitialized)
    {
        return true;
    }
    
    // Initialize member variables
    ServiceMap.Empty();
    FastPathMap.Empty();
    ServiceVersions.Empty();
    ServiceDependencies.Empty();
    ServiceProviders.Empty();
    
    // Reset performance counters
    FastPathHits.Set(0);
    CacheHits.Set(0);
    StandardResolutionCount.Set(0);
    
    bIsInitialized = true;
    return true;
}

void FServiceLocator::Shutdown()
{
    if (!bIsInitialized)
    {
        return;
    }
    
    // Take write lock to ensure exclusive access during shutdown
    FScopedWriteLock WriteLock(ServiceMapLock);
    
    // Clear all maps and arrays
    ServiceMap.Empty();
    FastPathMap.Empty();
    ServiceDependencies.Empty();
    ServiceProviders.Empty();
    
    // Clean up version counters
    for (auto& Pair : ServiceVersions)
    {
        delete Pair.Value;
    }
    ServiceVersions.Empty();
    
    // Reset initialized state
    bIsInitialized = false;
}

bool FServiceLocator::IsInitialized() const
{
    return bIsInitialized;
}

bool FServiceLocator::RegisterService(void* InService, const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID)
{
    if (!bIsInitialized || !InService)
    {
        return false;
    }
    
    if (!InInterfaceType)
    {
        // Handle non-UObject types
        return false;
    }
    
    // Get interface type name
    const FName TypeName = InInterfaceType->GetFName();
    
    // Determine service scope
    EServiceScope Scope = EServiceScope::Global;
    if (InZoneID != INDEX_NONE)
    {
        Scope = EServiceScope::Zone;
    }
    else if (InRegionID != INDEX_NONE)
    {
        Scope = EServiceScope::Region;
    }
    
    // Create service entry
    FServiceEntry Entry(InService, InZoneID, InRegionID, FServiceVersion(), Scope);
    
    // Add service to map (with write lock)
    {
        FScopedWriteLock WriteLock(ServiceMapLock);
        
        TArray<FServiceEntry>& Entries = ServiceMap.FindOrAdd(TypeName);
        
        // Check if service already exists in this context
        for (int32 i = 0; i < Entries.Num(); ++i)
        {
            if (Entries[i].ZoneID == InZoneID && Entries[i].RegionID == InRegionID)
            {
                // Replace existing entry
                Entries[i] = Entry;
                
                // Update version counter
                UpdateServiceVersion(InInterfaceType, InZoneID, InRegionID);
                
                // Invalidate cache for this entry
                InvalidateCacheEntry(InInterfaceType, InZoneID, InRegionID);
                
                return true;
            }
        }
        
        // Add new entry
        Entries.Add(Entry);
        
        // Update version counter
        UpdateServiceVersion(InInterfaceType, InZoneID, InRegionID);
        
        // Invalidate cache for this entry
        InvalidateCacheEntry(InInterfaceType, InZoneID, InRegionID);
    }
    
    // Register fast-path if this is a global service
    if (Scope == EServiceScope::Global)
    {
        RegisterFastPath(InInterfaceType, InZoneID, InRegionID);
    }
    
    return true;
}

bool FServiceLocator::RegisterServiceByTypeName(const FString& ServiceTypeName, void* InService, int32 InZoneID, int32 InRegionID)
{
    if (!bIsInitialized || !InService)
    {
        return false;
    }
    
    // Create a UClass*-equivalent NULL for the interface type
    UE_LOG(LogTemp, Verbose, TEXT("Registering service of type %s"), *ServiceTypeName);
    
    // Determine service scope
    EServiceScope Scope = EServiceScope::Global;
    if (InZoneID != INDEX_NONE)
    {
        Scope = EServiceScope::Zone;
    }
    if (InRegionID != INDEX_NONE)
    {
        Scope = EServiceScope::Region;
    }
    
    // Take write lock
    FScopedWriteLock WriteLock(ServiceMapLock);
    
    // Create the service key (just FName in this implementation)
    FName TypeName(*ServiceTypeName);
    
    // Create service entry
    FServiceEntry Entry;
    Entry.ServiceInstance = InService;
    Entry.ZoneID = InZoneID;
    Entry.RegionID = InRegionID;
    Entry.Version = FServiceVersion(1, 0, 0);  // Default version
    Entry.HealthStatus = EServiceHealthStatus::Healthy;
    Entry.Scope = Scope;
    Entry.Priority = 0;
    
    // Create or update the array of entries for this service type
    TArray<FServiceEntry>& Entries = ServiceMap.FindOrAdd(TypeName);
    Entries.Add(Entry);
    
    UE_LOG(LogTemp, Verbose, TEXT("Service of type %s registered successfully"), *ServiceTypeName);
    return true;
}

void* FServiceLocator::ResolveService(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID)
{
    if (!bIsInitialized || !InInterfaceType)
    {
        return nullptr;
    }
    
    // Try fast-path resolution first
    const uint32 TypeHash = GetTypeHash(InInterfaceType->GetFName());
    void* Result = ResolveServiceDirect(TypeHash, InZoneID, InRegionID);
    if (Result)
    {
        FastPathHits.Increment();
        return Result;
    }
    
    // Try thread-local cache
    Result = ResolveServiceCached(InInterfaceType, InZoneID, InRegionID);
    if (Result)
    {
        CacheHits.Increment();
        return Result;
    }
    
    // Standard resolution path
    StandardResolutionCount.Increment();
    
    const FName TypeName = InInterfaceType->GetFName();
    
    // Take read lock for service map access
    FScopedReadLock ReadLock(ServiceMapLock);
    
    // Find service entries for the requested type
    const TArray<FServiceEntry>* Entries = ServiceMap.Find(TypeName);
    if (!Entries || Entries->Num() == 0)
    {
        return nullptr;
    }
    
    // Resolve best matching service based on context
    const FServiceEntry* Entry = ResolveBestMatchingService(*Entries, InZoneID, InRegionID);
    if (!Entry)
    {
        return nullptr;
    }
    
    // Cache the result for future lookups
    const FServiceTypeKey Key(InInterfaceType, InZoneID, InRegionID);
    FWaitFreeCounter* VersionCounter = GetServiceVersionCounter(InInterfaceType, InZoneID, InRegionID);
    if (VersionCounter)
    {
        const uint32 Version = VersionCounter->GetValue();
        FThreadLocalServiceCache::Get().AddCachedService(Key, Entry->ServiceInstance, Version);
    }
    
    return Entry->ServiceInstance;
}

bool FServiceLocator::UnregisterService(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID)
{
    if (!bIsInitialized || !InInterfaceType)
    {
        return false;
    }
    
    const FName TypeName = InInterfaceType->GetFName();
    bool bRemoved = false;
    
    // Take write lock for service map modification
    FScopedWriteLock WriteLock(ServiceMapLock);
    
    // Find service entries for the type
    TArray<FServiceEntry>* Entries = ServiceMap.Find(TypeName);
    if (!Entries)
    {
        return false;
    }
    
    // Find and remove the specific entry
    for (int32 i = 0; i < Entries->Num(); ++i)
    {
        if ((*Entries)[i].ZoneID == InZoneID && (*Entries)[i].RegionID == InRegionID)
        {
            Entries->RemoveAt(i);
            bRemoved = true;
            break;
        }
    }
    
    // If all entries are removed, remove the type from the map
    if (Entries->Num() == 0)
    {
        ServiceMap.Remove(TypeName);
    }
    
    // Update version counter
    if (bRemoved)
    {
        UpdateServiceVersion(InInterfaceType, InZoneID, InRegionID);
        
        // Invalidate fast-path
        const uint32 TypeHash = GetTypeHash(TypeName);
        {
            // Use FCriticalSection instead of FScopeLock with FNUMAOptimizedSpinLock
            FastPathLock.Lock();
            FastPathMap.Remove(TypeHash);
            FastPathLock.Unlock();
        }
        
        // Invalidate cache
        InvalidateCacheEntry(InInterfaceType, InZoneID, InRegionID);
    }
    
    return bRemoved;
}

bool FServiceLocator::HasService(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID) const
{
    if (!bIsInitialized || !InInterfaceType)
    {
        return false;
    }
    
    const FName TypeName = InInterfaceType->GetFName();
    
    // Take read lock for service map access
    FScopedReadLock ReadLock(ServiceMapLock);
    
    // Find service entries for the type
    const TArray<FServiceEntry>* Entries = ServiceMap.Find(TypeName);
    if (!Entries || Entries->Num() == 0)
    {
        return false;
    }
    
    // Check if we have an entry matching the context
    for (const FServiceEntry& Entry : *Entries)
    {
        if ((InZoneID == INDEX_NONE || Entry.ZoneID == InZoneID) &&
            (InRegionID == INDEX_NONE || Entry.RegionID == InRegionID))
        {
            return true;
        }
    }
    
    return false;
}

bool FServiceLocator::RegisterServiceWithVersion(void* InService, const UClass* InInterfaceType, const FServiceVersion& InVersion, int32 InZoneID, int32 InRegionID)
{
    if (!bIsInitialized || !InService)
    {
        return false;
    }
    
    if (!InInterfaceType)
    {
        // Handle non-UObject types
        return false;
    }
    
    // Get interface type name
    const FName TypeName = InInterfaceType->GetFName();
    
    // Determine service scope
    EServiceScope Scope = EServiceScope::Global;
    if (InZoneID != INDEX_NONE)
    {
        Scope = EServiceScope::Zone;
    }
    else if (InRegionID != INDEX_NONE)
    {
        Scope = EServiceScope::Region;
    }
    
    // Create service entry
    FServiceEntry Entry(InService, InZoneID, InRegionID, InVersion, Scope);
    
    // Add service to map (with write lock)
    {
        FScopedWriteLock WriteLock(ServiceMapLock);
        
        TArray<FServiceEntry>& Entries = ServiceMap.FindOrAdd(TypeName);
        
        // Check if service already exists in this context
        for (int32 i = 0; i < Entries.Num(); ++i)
        {
            if (Entries[i].ZoneID == InZoneID && Entries[i].RegionID == InRegionID)
            {
                // Replace existing entry
                Entries[i] = Entry;
                
                // Update version counter
                UpdateServiceVersion(InInterfaceType, InZoneID, InRegionID);
                
                // Invalidate cache for this entry
                InvalidateCacheEntry(InInterfaceType, InZoneID, InRegionID);
                
                return true;
            }
        }
        
        // Add new entry
        Entries.Add(Entry);
        
        // Update version counter
        UpdateServiceVersion(InInterfaceType, InZoneID, InRegionID);
        
        // Invalidate cache for this entry
        InvalidateCacheEntry(InInterfaceType, InZoneID, InRegionID);
    }
    
    // Register fast-path if this is a global service
    if (Scope == EServiceScope::Global)
    {
        RegisterFastPath(InInterfaceType, InZoneID, InRegionID);
    }
    
    return true;
}

void* FServiceLocator::ResolveServiceWithVersion(const UClass* InInterfaceType, FServiceVersion& OutVersion, const FServiceVersion* InMinVersion, int32 InZoneID, int32 InRegionID)
{
    if (!bIsInitialized || !InInterfaceType)
    {
        return nullptr;
    }
    
    const FName TypeName = InInterfaceType->GetFName();
    
    // Take read lock for service map access
    FScopedReadLock ReadLock(ServiceMapLock);
    
    // Find service entries for the requested type
    const TArray<FServiceEntry>* Entries = ServiceMap.Find(TypeName);
    if (!Entries || Entries->Num() == 0)
    {
        return nullptr;
    }
    
    // Resolve best matching service based on context
    const FServiceEntry* Entry = ResolveBestMatchingService(*Entries, InZoneID, InRegionID);
    if (!Entry)
    {
        return nullptr;
    }
    
    // Check version compatibility if minimum version is specified
    if (InMinVersion && !Entry->Version.IsCompatibleWith(*InMinVersion))
    {
        return nullptr;
    }
    
    // Return the version
    OutVersion = Entry->Version;
    
    return Entry->ServiceInstance;
}

bool FServiceLocator::DeclareDependency(const UClass* InDependentType, const UClass* InDependencyType, EServiceDependencyType InDependencyKind)
{
    if (!bIsInitialized || !InDependentType || !InDependencyType)
    {
        return false;
    }
    
    const FName DependentName = InDependentType->GetFName();
    const FName DependencyName = InDependencyType->GetFName();
    
    // Take write lock for dependency map modification
    FScopedWriteLock WriteLock(DependencyMapLock);
    
    // Add dependency to the map
    TArray<TPair<FName, EServiceDependencyType>>& Dependencies = ServiceDependencies.FindOrAdd(DependentName);
    
    // Check if dependency already exists
    for (TPair<FName, EServiceDependencyType>& Pair : Dependencies)
    {
        if (Pair.Key == DependencyName)
        {
            // Update dependency kind
            Pair.Value = InDependencyKind;
            return true;
        }
    }
    
    // Add new dependency
    Dependencies.Add(TPair<FName, EServiceDependencyType>(DependencyName, InDependencyKind));
    
    return true;
}

bool FServiceLocator::ValidateDependencies(TArray<TPair<UClass*, UClass*>>& OutMissingDependencies)
{
    if (!bIsInitialized)
    {
        return false;
    }
    
    OutMissingDependencies.Empty();
    bool bAllDependenciesSatisfied = true;
    
    // Take read locks for dependency and service maps
    FScopedReadLock DependencyLock(DependencyMapLock);
    FScopedReadLock ServiceLock(ServiceMapLock);
    
    // Iterate over all declared dependencies
    for (const auto& DependencyPair : ServiceDependencies)
    {
        const FName DependentName = DependencyPair.Key;
        
        // Skip validation if dependent service is not registered
        if (!ServiceMap.Contains(DependentName))
        {
            continue;
        }
        
        // Check each dependency
        for (const TPair<FName, EServiceDependencyType>& Dependency : DependencyPair.Value)
        {
            // Skip optional dependencies
            if (Dependency.Value != EServiceDependencyType::Required)
            {
                continue;
            }
            
            // Check if dependency is satisfied
            if (!ServiceMap.Contains(Dependency.Key))
            {
                // Find UClass objects for dependent and dependency
                UClass* DependentClass = nullptr;
                UClass* DependencyClass = nullptr;
                
                for (TObjectIterator<UClass> It; It; ++It)
                {
                    if (It->GetFName() == DependentName)
                    {
                        DependentClass = *It;
                    }
                    else if (It->GetFName() == Dependency.Key)
                    {
                        DependencyClass = *It;
                    }
                    
                    if (DependentClass && DependencyClass)
                    {
                        break;
                    }
                }
                
                if (DependentClass && DependencyClass)
                {
                    OutMissingDependencies.Add(TPair<UClass*, UClass*>(DependentClass, DependencyClass));
                }
                
                bAllDependenciesSatisfied = false;
            }
        }
    }
    
    return bAllDependenciesSatisfied;
}

EServiceHealthStatus FServiceLocator::GetServiceHealth(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID)
{
    if (!bIsInitialized || !InInterfaceType)
    {
        return EServiceHealthStatus::NotRegistered;
    }
    
    const FName TypeName = InInterfaceType->GetFName();
    
    // Take read lock for service map access
    FScopedReadLock ReadLock(ServiceMapLock);
    
    // Find service entries for the requested type
    const TArray<FServiceEntry>* Entries = ServiceMap.Find(TypeName);
    if (!Entries || Entries->Num() == 0)
    {
        return EServiceHealthStatus::NotRegistered;
    }
    
    // Find specific entry matching the context
    for (const FServiceEntry& Entry : *Entries)
    {
        if ((InZoneID == INDEX_NONE || Entry.ZoneID == InZoneID) &&
            (InRegionID == INDEX_NONE || Entry.RegionID == InRegionID))
        {
            return Entry.HealthStatus;
        }
    }
    
    return EServiceHealthStatus::NotRegistered;
}

bool FServiceLocator::RecoverService(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID)
{
    if (!bIsInitialized || !InInterfaceType)
    {
        return false;
    }
    
    const FName TypeName = InInterfaceType->GetFName();
    
    // Take write lock for service map modification
    FScopedWriteLock WriteLock(ServiceMapLock);
    
    // Find service entries for the requested type
    TArray<FServiceEntry>* Entries = ServiceMap.Find(TypeName);
    if (!Entries || Entries->Num() == 0)
    {
        return false;
    }
    
    // Find specific entry matching the context
    for (FServiceEntry& Entry : *Entries)
    {
        if ((InZoneID == INDEX_NONE || Entry.ZoneID == InZoneID) &&
            (InRegionID == INDEX_NONE || Entry.RegionID == InRegionID))
        {
            // Set health status to recovering
            if (Entry.HealthStatus != EServiceHealthStatus::Healthy)
            {
                Entry.HealthStatus = EServiceHealthStatus::Recovering;
                
                // Update version counter to invalidate caches
                UpdateServiceVersion(InInterfaceType, InZoneID, InRegionID);
                
                // Additional recovery logic would go here
                // ...
                
                // Set health status to healthy
                Entry.HealthStatus = EServiceHealthStatus::Healthy;
                
                return true;
            }
        }
    }
    
    return false;
}

EServiceScope FServiceLocator::GetServiceScope(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID)
{
    if (!bIsInitialized || !InInterfaceType)
    {
        return EServiceScope::Global;
    }
    
    const FName TypeName = InInterfaceType->GetFName();
    
    // Take read lock for service map access
    FScopedReadLock ReadLock(ServiceMapLock);
    
    // Find service entries for the requested type
    const TArray<FServiceEntry>* Entries = ServiceMap.Find(TypeName);
    if (!Entries || Entries->Num() == 0)
    {
        return EServiceScope::Global;
    }
    
    // Find specific entry matching the context
    for (const FServiceEntry& Entry : *Entries)
    {
        if ((InZoneID == INDEX_NONE || Entry.ZoneID == InZoneID) &&
            (InRegionID == INDEX_NONE || Entry.RegionID == InRegionID))
        {
            return Entry.Scope;
        }
    }
    
    return EServiceScope::Global;
}

TArray<UClass*> FServiceLocator::GetDependentServices(const UClass* InInterfaceType)
{
    TArray<UClass*> Result;
    
    if (!bIsInitialized || !InInterfaceType)
    {
        return Result;
    }
    
    const FName TypeName = InInterfaceType->GetFName();
    
    // Take read lock for dependency map access
    FScopedReadLock ReadLock(DependencyMapLock);
    
    // Find all services that depend on this type
    for (const auto& Pair : ServiceDependencies)
    {
        for (const TPair<FName, EServiceDependencyType>& Dependency : Pair.Value)
        {
            if (Dependency.Key == TypeName)
            {
                // Find UClass for dependent
                for (TObjectIterator<UClass> It; It; ++It)
                {
                    if (It->GetFName() == Pair.Key)
                    {
                        Result.Add(*It);
                        break;
                    }
                }
            }
        }
    }
    
    return Result;
}

TArray<UClass*> FServiceLocator::GetServiceDependencies(const UClass* InInterfaceType)
{
    TArray<UClass*> Result;
    
    if (!bIsInitialized || !InInterfaceType)
    {
        return Result;
    }
    
    const FName TypeName = InInterfaceType->GetFName();
    
    // Take read lock for dependency map access
    FScopedReadLock ReadLock(DependencyMapLock);
    
    // Find dependencies for this type
    const TArray<TPair<FName, EServiceDependencyType>>* Dependencies = ServiceDependencies.Find(TypeName);
    if (!Dependencies)
    {
        return Result;
    }
    
    // Add each dependency to the result
    for (const TPair<FName, EServiceDependencyType>& Dependency : *Dependencies)
    {
        // Find UClass for dependency
        for (TObjectIterator<UClass> It; It; ++It)
        {
            if (It->GetFName() == Dependency.Key)
            {
                Result.Add(*It);
                break;
            }
        }
    }
    
    return Result;
}

bool FServiceLocator::RegisterServiceProvider(TScriptInterface<IServiceProvider> InProvider, int32 InZoneID, int32 InRegionID)
{
    if (!bIsInitialized || !InProvider.GetObject())
    {
        return false;
    }
    
    // Take write lock for provider array
    FScopedWriteLock WriteLock(ProviderLock);
    
    // Check if provider already exists
    for (const TScriptInterface<IServiceProvider>& Provider : ServiceProviders)
    {
        if (Provider.GetObject() == InProvider.GetObject())
        {
            return false;
        }
    }
    
    // Add provider
    ServiceProviders.Add(InProvider);
    
    // Register services from the provider
    // This would normally call into the provider's interface to get services
    // For now, we'll just implement a placeholder
    
    return true;
}

bool FServiceLocator::UnregisterServiceProvider(TScriptInterface<IServiceProvider> InProvider, int32 InZoneID, int32 InRegionID)
{
    if (!bIsInitialized || !InProvider.GetObject())
    {
        return false;
    }
    
    // Take write lock for provider array
    FScopedWriteLock WriteLock(ProviderLock);
    
    // Find and remove provider
    for (int32 i = 0; i < ServiceProviders.Num(); ++i)
    {
        if (ServiceProviders[i].GetObject() == InProvider.GetObject())
        {
            ServiceProviders.RemoveAt(i);
            return true;
        }
    }
    
    return false;
}

bool FServiceLocator::RegisterFastPath(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID)
{
    if (!bIsInitialized || !InInterfaceType)
    {
        return false;
    }
    
    const FName TypeName = InInterfaceType->GetFName();
    const uint32 TypeHash = GetTypeHash(TypeName);
    
    // Find the service instance
    void* ServiceInstance = nullptr;
    
    {
        FScopedReadLock ReadLock(ServiceMapLock);
        
        // Find service entries for the type
        const TArray<FServiceEntry>* Entries = ServiceMap.Find(TypeName);
        if (!Entries || Entries->Num() == 0)
        {
            return false;
        }
        
        // Find the best matching service
        const FServiceEntry* Entry = ResolveBestMatchingService(*Entries, InZoneID, InRegionID);
        if (!Entry)
        {
            return false;
        }
        
        ServiceInstance = Entry->ServiceInstance;
    }
    
    if (!ServiceInstance)
    {
        return false;
    }
    
    // Register fast-path entry
    FastPathLock.Lock();
    FastPathMap.Add(TypeHash, FFastPathEntry(ServiceInstance, TypeHash, InZoneID, InRegionID));
    FastPathLock.Unlock();
    
    return true;
}

void* FServiceLocator::ResolveServiceDirect(uint32 TypeHash, int32 InZoneID, int32 InRegionID)
{
    if (!bIsInitialized)
    {
        return nullptr;
    }
    
    // Fast lookup using type hash (no locks for read)
    FFastPathEntry* Entry = FastPathMap.Find(TypeHash);
    if (Entry && Entry->ServiceInstance)
    {
        // Check if context matches
        if ((InZoneID == INDEX_NONE || Entry->ZoneID == InZoneID) &&
            (InRegionID == INDEX_NONE || Entry->RegionID == InRegionID))
        {
            // Increment usage counter atomically
            Entry->UsageCount.Increment();
            return Entry->ServiceInstance;
        }
    }
    
    return nullptr;
}

void* FServiceLocator::ResolveServiceCached(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID)
{
    if (!bIsInitialized || !InInterfaceType)
    {
        return nullptr;
    }
    
    // Try thread-local cache
    const FServiceTypeKey Key(InInterfaceType, InZoneID, InRegionID);
    FThreadLocalServiceCache& Cache = FThreadLocalServiceCache::Get();
    
    const FCachedServiceEntry* CachedEntry = Cache.Cache.Find(Key);
    if (CachedEntry)
    {
        // Get version counter
        FWaitFreeCounter* VersionCounter = GetServiceVersionCounter(InInterfaceType, InZoneID, InRegionID);
        if (VersionCounter && CachedEntry->Version == VersionCounter->GetValue())
        {
            // Cache hit with current version
            return CachedEntry->Service.Get();
        }
    }
    
    return nullptr;
}

void FServiceLocator::InvalidateGlobalCache()
{
    // This would normally need to invalidate all thread caches
    // For thread-local storage, we can't directly access other threads' caches
    // Instead, we update version counters which will cause cache misses
    
    // Take write lock for version map
    FScopedWriteLock WriteLock(VersionMapLock);
    
    // Increment all version counters
    for (auto& Pair : ServiceVersions)
    {
        Pair.Value->Increment();
    }
    
    // Invalidate current thread's cache
    FThreadLocalServiceCache::Get().Invalidate();
}

void FServiceLocator::InvalidateContextCache(int32 InZoneID, int32 InRegionID)
{
    // Take write lock for version map
    FScopedWriteLock WriteLock(VersionMapLock);
    
    // Increment version counters for matching context
    for (auto& Pair : ServiceVersions)
    {
        if ((InZoneID == INDEX_NONE || Pair.Key.ZoneID == InZoneID) &&
            (InRegionID == INDEX_NONE || Pair.Key.RegionID == InRegionID))
        {
            Pair.Value->Increment();
        }
    }
    
    // Invalidate current thread's matching entries
    FThreadLocalServiceCache& Cache = FThreadLocalServiceCache::Get();
    TArray<FServiceTypeKey> KeysToRemove;
    
    for (const auto& CachePair : Cache.Cache)
    {
        if ((InZoneID == INDEX_NONE || CachePair.Key.ZoneID == InZoneID) &&
            (InRegionID == INDEX_NONE || CachePair.Key.RegionID == InRegionID))
        {
            KeysToRemove.Add(CachePair.Key);
        }
    }
    
    for (const FServiceTypeKey& Key : KeysToRemove)
    {
        Cache.Cache.Remove(Key);
    }
}

void FServiceLocator::InvalidateCacheEntry(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID)
{
    if (!InInterfaceType)
    {
        return;
    }
    
    // Update version counter
    UpdateServiceVersion(InInterfaceType, InZoneID, InRegionID);
    
    // Invalidate current thread's cache entry
    FThreadLocalServiceCache::Get().InvalidateEntry(InInterfaceType, InZoneID, InRegionID);
}

void FServiceLocator::UpdateServiceVersion(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID)
{
    if (!InInterfaceType)
    {
        return;
    }
    
    const FServiceTypeKey Key(InInterfaceType, InZoneID, InRegionID);
    
    // Take write lock for version map
    FScopedWriteLock WriteLock(VersionMapLock);
    
    // Get or create version counter
    FWaitFreeCounter* VersionCounter = GetServiceVersionCounter(InInterfaceType, InZoneID, InRegionID);
    
    // Increment version
    VersionCounter->Increment();
}

FWaitFreeCounter* FServiceLocator::GetServiceVersionCounter(const UClass* InInterfaceType, int32 InZoneID, int32 InRegionID)
{
    if (!InInterfaceType)
    {
        return nullptr;
    }
    
    const FServiceTypeKey Key(InInterfaceType, InZoneID, InRegionID);
    
    // Find existing counter
    FWaitFreeCounter** CounterPtr = ServiceVersions.Find(Key);
    if (CounterPtr && *CounterPtr)
    {
        return *CounterPtr;
    }
    
    // Create new counter
    FWaitFreeCounter* NewCounter = FThreadSafety::Get().CreateWaitFreeCounter(1);
    ServiceVersions.Add(Key, NewCounter);
    
    return NewCounter;
}

uint32 FServiceLocator::GetCurrentThreadNUMADomain() const
{
    // Use FPlatformTLS to get the current thread ID
    uint32 ThreadId = FPlatformTLS::GetCurrentThreadId();
    
    // Use the thread ID to get the NUMA domain
    return NUMATopology.GetDomainForThread(ThreadId);
}

const FServiceEntry* FServiceLocator::ResolveBestMatchingService(const TArray<FServiceEntry>& Entries, int32 InZoneID, int32 InRegionID) const
{
    // First pass: find exact match for zone and region
    if (InZoneID != INDEX_NONE && InRegionID != INDEX_NONE)
    {
        for (const FServiceEntry& Entry : Entries)
        {
            if (Entry.ZoneID == InZoneID && Entry.RegionID == InRegionID)
            {
                return &Entry;
            }
        }
    }
    
    // Second pass: find zone match with any region
    if (InZoneID != INDEX_NONE)
    {
        for (const FServiceEntry& Entry : Entries)
        {
            if (Entry.ZoneID == InZoneID)
            {
                return &Entry;
            }
        }
    }
    
    // Third pass: find region match with any zone
    if (InRegionID != INDEX_NONE)
    {
        for (const FServiceEntry& Entry : Entries)
        {
            if (Entry.RegionID == InRegionID)
            {
                return &Entry;
            }
        }
    }
    
    // Fourth pass: find global service (no zone or region)
    for (const FServiceEntry& Entry : Entries)
    {
        if (Entry.ZoneID == INDEX_NONE && Entry.RegionID == INDEX_NONE)
        {
            return &Entry;
        }
    }
    
    // Final pass: return first available service as fallback
    if (Entries.Num() > 0)
    {
        return &Entries[0];
    }
    
    return nullptr;
}

FString FServiceLocator::GetServiceContextKey(int32 InZoneID, int32 InRegionID)
{
    return FString::Printf(TEXT("Zone=%d,Region=%d"), InZoneID, InRegionID);
}

TArray<const UClass*> FServiceLocator::GetAllServiceTypes() const
{
    TArray<const UClass*> Result;
    
    if (!bIsInitialized)
    {
        return Result;
    }
    
    // Take read lock for service map
    FScopedReadLock ReadLock(ServiceMapLock);
    
    // Find UClass for each service type
    for (const auto& Pair : ServiceMap)
    {
        for (TObjectIterator<UClass> It; It; ++It)
        {
            if (It->GetFName() == Pair.Key)
            {
                Result.AddUnique(*It);
                break;
            }
        }
    }
    
    return Result;
}

TArray<FServiceEntry> FServiceLocator::GetAllServiceInstances(const UClass* InInterfaceType) const
{
    TArray<FServiceEntry> Result;
    
    if (!bIsInitialized || !InInterfaceType)
    {
        return Result;
    }
    
    const FName TypeName = InInterfaceType->GetFName();
    
    // Take read lock for service map
    FScopedReadLock ReadLock(ServiceMapLock);
    
    // Find service entries for the type
    const TArray<FServiceEntry>* Entries = ServiceMap.Find(TypeName);
    if (Entries)
    {
        Result = *Entries;
    }
    
    return Result;
}
