// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include <type_traits> // For std::is_same
#include "../../2_MemoryManagement/Public/Interfaces/IMemoryManager.h"
#include "../../2_MemoryManagement/Public/Interfaces/IPoolAllocator.h"
#include "../../2_MemoryManagement/Public/Interfaces/IBufferProvider.h"
#include "../../2_MemoryManagement/Public/Interfaces/IMemoryTracker.h"
#include "../../2_MemoryManagement/Public/CompressionUtility.h"
#include "../CommonServiceTypes.h"
#include "IServiceLocator.generated.h"

// Tag structs for tag dispatch
namespace ServiceTags {
    struct MemoryManagerTag {};
    struct PoolAllocatorTag {};
    struct BufferProviderTag {};
    struct MemoryTrackerTag {};
    struct CompressionUtilityTag {};
    struct DefaultTag {};
}

// Service tag trait structure
template<typename T>
struct ServiceTagTrait
{
    using Tag = ServiceTags::DefaultTag;
    static Tag GetTag() { return Tag(); }
};

// Specializations for different service types
template<>
struct ServiceTagTrait<IMemoryManager>
{
    using Tag = ServiceTags::MemoryManagerTag;
    static Tag GetTag() { return Tag(); }
};

template<>
struct ServiceTagTrait<IPoolAllocator>
{
    using Tag = ServiceTags::PoolAllocatorTag;
    static Tag GetTag() { return Tag(); }
};

template<>
struct ServiceTagTrait<IBufferProvider>
{
    using Tag = ServiceTags::BufferProviderTag;
    static Tag GetTag() { return Tag(); }
};

template<>
struct ServiceTagTrait<IMemoryTracker>
{
    using Tag = ServiceTags::MemoryTrackerTag;
    static Tag GetTag() { return Tag(); }
};

template<>
struct ServiceTagTrait<FCompressionUtility>
{
    using Tag = ServiceTags::CompressionUtilityTag;
    static Tag GetTag() { return Tag(); }
};

/**
 * Base interface for service locator in the SVO+SDF mining architecture
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UServiceLocator : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for service locator in the SVO+SDF mining architecture
 * Provides service registration, resolution, and lifecycle management for subsystems
 */
class MININGSPICECOPILOT_API IServiceLocator
{
    GENERATED_BODY()

public:
    /**
     * Initialize the service locator
     * @return True if successfully initialized
     */
    virtual bool Initialize() = 0;
    
    /**
     * Shutdown the service locator and cleanup resources
     */
    virtual void Shutdown() = 0;
    
    /**
     * Check if the service locator is initialized
     * @return True if initialized
     */
    virtual bool IsInitialized() const = 0;
    
    /**
     * Register a service implementation
     * @param InService Pointer to the service implementation
     * @param InInterfaceType Type information for the service interface
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return True if registration was successful
     */
    virtual bool RegisterService(void* InService, const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) = 0;
    
    /**
     * Template method to register a typed service
     * @param InService Pointer to the service implementation
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return True if registration was successful
     */
    template<typename T>
    bool RegisterService(T* InService, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE)
    {
        return RegisterServiceImpl(InService, InZoneID, InRegionID, ServiceTagTrait<T>::GetTag());
    }
    
    /**
     * Resolves a service instance based on interface type and optional context
     * @param InInterfaceType Type information for the service interface
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return Pointer to the service implementation or nullptr if not found
     */
    virtual void* ResolveService(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) = 0;
    
    /**
     * Template method to resolve a typed service
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return Typed pointer to the service implementation or nullptr if not found
     */
    template<typename T>
    T* ResolveService(int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE)
    {
        return ResolveServiceImpl<T>(InZoneID, InRegionID, ServiceTagTrait<T>::GetTag());
    }
    
    /**
     * Unregisters a service implementation
     * @param InInterfaceType Type information for the service interface
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return True if unregistration was successful
     */
    virtual bool UnregisterService(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) = 0;
    
    /**
     * Template method to unregister a typed service
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return True if unregistration was successful
     */
    template<typename T>
    bool UnregisterService(int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE)
    {
        return UnregisterServiceImpl<T>(InZoneID, InRegionID, ServiceTagTrait<T>::GetTag());
    }
    
    /**
     * Checks if a service is registered
     * @param InInterfaceType Type information for the service interface
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return True if service is registered
     */
    virtual bool HasService(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) const = 0;
    
    /**
     * Template method to check if a typed service is registered
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return True if service is registered
     */
    template<typename T>
    bool HasService(int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) const
    {
        return HasServiceImpl<T>(InZoneID, InRegionID, ServiceTagTrait<T>::GetTag());
    }
    
    /**
     * Register a service implementation with version information
     * @param InService Pointer to the service implementation
     * @param InInterfaceType Type information for the service interface
     * @param InVersion Version information for the service
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return True if registration was successful
     */
    virtual bool RegisterServiceWithVersion(void* InService, const UClass* InInterfaceType, const FServiceVersion& InVersion, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) = 0;
    
    /**
     * Template method to register a typed service with version information
     * @param InService Pointer to the service implementation
     * @param InVersion Version information for the service
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return True if registration was successful
     */
    template<typename T>
    bool RegisterServiceWithVersion(T* InService, const FServiceVersion& InVersion, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE)
    {
        return RegisterServiceWithVersionImpl(InService, InVersion, InZoneID, InRegionID, ServiceTagTrait<T>::GetTag());
    }
    
    /**
     * Resolves a service instance based on interface type and optional context, with version validation
     * @param InInterfaceType Type information for the service interface
     * @param OutVersion Output parameter for the resolved service version
     * @param InMinVersion Optional minimum version requirement
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return Pointer to the service implementation or nullptr if not found or version incompatible
     */
    virtual void* ResolveServiceWithVersion(const UClass* InInterfaceType, FServiceVersion& OutVersion, const FServiceVersion* InMinVersion = nullptr, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) = 0;
    
    /**
     * Template method to resolve a typed service with version validation
     * @param OutVersion Output parameter for the resolved service version
     * @param InMinVersion Optional minimum version requirement
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return Typed pointer to the service implementation or nullptr if not found or version incompatible
     */
    template<typename T>
    T* ResolveServiceWithVersion(FServiceVersion& OutVersion, const FServiceVersion* InMinVersion = nullptr, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE)
    {
        return ResolveServiceWithVersionImpl<T>(OutVersion, InMinVersion, InZoneID, InRegionID, ServiceTagTrait<T>::GetTag());
    }
    
    /**
     * Declares a dependency between two services
     * @param InDependentType Interface type of the dependent service
     * @param InDependencyType Interface type of the service being depended on
     * @param InDependencyKind Type of dependency (required, optional, conditional)
     * @return True if dependency was successfully declared
     */
    virtual bool DeclareDependency(const UClass* InDependentType, const UClass* InDependencyType, EServiceDependencyType InDependencyKind = EServiceDependencyType::Required) = 0;
    
    /**
     * Template method to declare a dependency between two typed services
     * @param InDependencyKind Type of dependency (required, optional, conditional)
     * @return True if dependency was successfully declared
     */
    template<typename TDependent, typename TDependency>
    bool DeclareDependency(EServiceDependencyType InDependencyKind = EServiceDependencyType::Required)
    {
        return DeclareDependencyImpl<TDependent, TDependency>(InDependencyKind, ServiceTagTrait<TDependent>::GetTag(), ServiceTagTrait<TDependency>::GetTag());
    }
    
    /**
     * Validates service dependencies for registered services
     * @param OutMissingDependencies Output array of missing required dependencies
     * @return True if all required dependencies are satisfied
     */
    virtual bool ValidateDependencies(TArray<TPair<UClass*, UClass*>>& OutMissingDependencies) = 0;
    
    /**
     * Gets the health status of a service
     * @param InInterfaceType Type information for the service interface
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return Health status of the service
     */
    virtual EServiceHealthStatus GetServiceHealth(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) = 0;
    
    /**
     * Template method to get the health status of a typed service
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return Health status of the service
     */
    template<typename T>
    EServiceHealthStatus GetServiceHealth(int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE)
    {
        return GetServiceHealthImpl<T>(InZoneID, InRegionID, ServiceTagTrait<T>::GetTag());
    }
    
    /**
     * Attempts to recover a failed service
     * @param InInterfaceType Type information for the service interface
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return True if recovery was successful
     */
    virtual bool RecoverService(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) = 0;
    
    /**
     * Template method to recover a failed typed service
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return True if recovery was successful
     */
    template<typename T>
    bool RecoverService(int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE)
    {
        return RecoverServiceImpl<T>(InZoneID, InRegionID, ServiceTagTrait<T>::GetTag());
    }
    
    /**
     * Gets the service scope for a registered service
     * @param InInterfaceType Type information for the service interface
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return Scope of the service
     */
    virtual EServiceScope GetServiceScope(const UClass* InInterfaceType, int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE) = 0;
    
    /**
     * Template method to get the service scope for a typed service
     * @param InZoneID Optional zone identifier for zone-specific services
     * @param InRegionID Optional region identifier for region-specific services
     * @return Scope of the service
     */
    template<typename T>
    EServiceScope GetServiceScope(int32 InZoneID = INDEX_NONE, int32 InRegionID = INDEX_NONE)
    {
        return GetServiceScopeImpl<T>(InZoneID, InRegionID, ServiceTagTrait<T>::GetTag());
    }
    
    /**
     * Gets all services that depend on the specified service
     * @param InInterfaceType Type information for the service interface
     * @return Array of service interface types that depend on the specified service
     */
    virtual TArray<UClass*> GetDependentServices(const UClass* InInterfaceType) = 0;
    
    /**
     * Template method to get all services that depend on the specified typed service
     * @return Array of service interface types that depend on the specified service
     */
    template<typename T>
    TArray<UClass*> GetDependentServices()
    {
        return GetDependentServicesImpl<T>(ServiceTagTrait<T>::GetTag());
    }
    
    /**
     * Gets all services that the specified service depends on
     * @param InInterfaceType Type information for the service interface
     * @return Array of service interface types that the specified service depends on
     */
    virtual TArray<UClass*> GetServiceDependencies(const UClass* InInterfaceType) = 0;
    
    /**
     * Template method to get all services that the specified typed service depends on
     * @return Array of service interface types that the specified service depends on
     */
    template<typename T>
    TArray<UClass*> GetServiceDependencies()
    {
        return GetServiceDependenciesImpl<T>(ServiceTagTrait<T>::GetTag());
    }
    
    /**
     * Gets the singleton instance of the service locator
     * @return Reference to the service locator instance
     */
    static IServiceLocator& Get();

private:
    // ************** Implementation for RegisterService **************
    bool RegisterServiceImpl(IMemoryManager* InService, int32 InZoneID, int32 InRegionID, ServiceTags::MemoryManagerTag)
    {
        // Use a hard-coded name to avoid relying on StaticClass()
        static FName MemoryManagerName = TEXT("UMemoryManager");
        UClass* InterfaceClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/MiningSpiceCoPilot.%s"), *MemoryManagerName.ToString()));
        return RegisterService(InService, InterfaceClass, InZoneID, InRegionID);
    }

    bool RegisterServiceImpl(IPoolAllocator* InService, int32 InZoneID, int32 InRegionID, ServiceTags::PoolAllocatorTag)
    {
        // Use a hard-coded name to avoid relying on StaticClass()
        static FName PoolAllocatorName = TEXT("UPoolAllocator");
        UClass* InterfaceClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/MiningSpiceCoPilot.%s"), *PoolAllocatorName.ToString()));
        return RegisterService(InService, InterfaceClass, InZoneID, InRegionID);
    }

    bool RegisterServiceImpl(IBufferProvider* InService, int32 InZoneID, int32 InRegionID, ServiceTags::BufferProviderTag)
    {
        // Use a hard-coded name to avoid relying on StaticClass()
        static FName BufferProviderName = TEXT("UBufferProvider");
        UClass* InterfaceClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/MiningSpiceCoPilot.%s"), *BufferProviderName.ToString()));
        return RegisterService(InService, InterfaceClass, InZoneID, InRegionID);
    }

    bool RegisterServiceImpl(IMemoryTracker* InService, int32 InZoneID, int32 InRegionID, ServiceTags::MemoryTrackerTag)
    {
        // Use a hard-coded name to avoid relying on StaticClass()
        static FName MemoryTrackerName = TEXT("UMemoryTracker");
        UClass* InterfaceClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/MiningSpiceCoPilot.%s"), *MemoryTrackerName.ToString()));
        return RegisterService(InService, InterfaceClass, InZoneID, InRegionID);
    }

    bool RegisterServiceImpl(FCompressionUtility* InService, int32 InZoneID, int32 InRegionID, ServiceTags::CompressionUtilityTag)
    {
        // Special handling for non-UObject types
        return RegisterService(InService, nullptr, InZoneID, InRegionID);
    }

    template<typename T>
    bool RegisterServiceImpl(T* InService, int32 InZoneID, int32 InRegionID, ServiceTags::DefaultTag)
    {
        return RegisterService(InService, nullptr, InZoneID, InRegionID);
    }

    // ************** Implementation for ResolveService **************
    template<typename T>
    T* ResolveServiceImpl(int32 InZoneID, int32 InRegionID, ServiceTags::MemoryManagerTag)
    {
        // Use a hard-coded name to avoid relying on StaticClass()
        static FName MemoryManagerName = TEXT("UMemoryManager");
        UClass* InterfaceClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/MiningSpiceCoPilot.%s"), *MemoryManagerName.ToString()));
        return static_cast<T*>(ResolveService(InterfaceClass, InZoneID, InRegionID));
    }

    template<typename T>
    T* ResolveServiceImpl(int32 InZoneID, int32 InRegionID, ServiceTags::PoolAllocatorTag)
    {
        // Use a hard-coded name to avoid relying on StaticClass()
        static FName PoolAllocatorName = TEXT("UPoolAllocator");
        UClass* InterfaceClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/MiningSpiceCoPilot.%s"), *PoolAllocatorName.ToString()));
        return static_cast<T*>(ResolveService(InterfaceClass, InZoneID, InRegionID));
    }

    template<typename T>
    T* ResolveServiceImpl(int32 InZoneID, int32 InRegionID, ServiceTags::BufferProviderTag)
    {
        // Use a hard-coded name to avoid relying on StaticClass()
        static FName BufferProviderName = TEXT("UBufferProvider");
        UClass* InterfaceClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/MiningSpiceCoPilot.%s"), *BufferProviderName.ToString()));
        return static_cast<T*>(ResolveService(InterfaceClass, InZoneID, InRegionID));
    }

    template<typename T>
    T* ResolveServiceImpl(int32 InZoneID, int32 InRegionID, ServiceTags::MemoryTrackerTag)
    {
        // Use a hard-coded name to avoid relying on StaticClass()
        static FName MemoryTrackerName = TEXT("UMemoryTracker");
        UClass* InterfaceClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/MiningSpiceCoPilot.%s"), *MemoryTrackerName.ToString()));
        return static_cast<T*>(ResolveService(InterfaceClass, InZoneID, InRegionID));
    }

    template<typename T>
    T* ResolveServiceImpl(int32 InZoneID, int32 InRegionID, ServiceTags::CompressionUtilityTag)
    {
        return static_cast<T*>(ResolveService(nullptr, InZoneID, InRegionID));
    }

    template<typename T>
    T* ResolveServiceImpl(int32 InZoneID, int32 InRegionID, ServiceTags::DefaultTag)
    {
        return static_cast<T*>(ResolveService(nullptr, InZoneID, InRegionID));
    }

    // ************** Implementation for UnregisterService **************
    template<typename T>
    bool UnregisterServiceImpl(int32 InZoneID, int32 InRegionID, ServiceTags::MemoryManagerTag)
    {
        // Use a hard-coded name to avoid relying on StaticClass()
        static FName MemoryManagerName = TEXT("UMemoryManager");
        UClass* InterfaceClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/MiningSpiceCoPilot.%s"), *MemoryManagerName.ToString()));
        return UnregisterService(InterfaceClass, InZoneID, InRegionID);
    }

    template<typename T>
    bool UnregisterServiceImpl(int32 InZoneID, int32 InRegionID, ServiceTags::PoolAllocatorTag)
    {
        // Use a hard-coded name to avoid relying on StaticClass()
        static FName PoolAllocatorName = TEXT("UPoolAllocator");
        UClass* InterfaceClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/MiningSpiceCoPilot.%s"), *PoolAllocatorName.ToString()));
        return UnregisterService(InterfaceClass, InZoneID, InRegionID);
    }
    
    template<typename T>
    bool UnregisterServiceImpl(int32 InZoneID, int32 InRegionID, ServiceTags::BufferProviderTag)
    {
        // Use a hard-coded name to avoid relying on StaticClass()
        static FName BufferProviderName = TEXT("UBufferProvider");
        UClass* InterfaceClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/MiningSpiceCoPilot.%s"), *BufferProviderName.ToString()));
        return UnregisterService(InterfaceClass, InZoneID, InRegionID);
    }
    
    template<typename T>
    bool UnregisterServiceImpl(int32 InZoneID, int32 InRegionID, ServiceTags::MemoryTrackerTag)
    {
        // Use a hard-coded name to avoid relying on StaticClass()
        static FName MemoryTrackerName = TEXT("UMemoryTracker");
        UClass* InterfaceClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/MiningSpiceCoPilot.%s"), *MemoryTrackerName.ToString()));
        return UnregisterService(InterfaceClass, InZoneID, InRegionID);
    }
    
    template<typename T>
    bool UnregisterServiceImpl(int32 InZoneID, int32 InRegionID, ServiceTags::CompressionUtilityTag)
    {
        return UnregisterService(nullptr, InZoneID, InRegionID);
    }
    
    template<typename T>
    bool UnregisterServiceImpl(int32 InZoneID, int32 InRegionID, ServiceTags::DefaultTag)
    {
        return UnregisterService(nullptr, InZoneID, InRegionID);
    }
    
    // ************** Implementation for HasService **************
    template<typename T>
    bool HasServiceImpl(int32 InZoneID, int32 InRegionID, ServiceTags::MemoryManagerTag) const
    {
        // Use a hard-coded name to avoid relying on StaticClass()
        static FName MemoryManagerName = TEXT("UMemoryManager");
        UClass* InterfaceClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/MiningSpiceCoPilot.%s"), *MemoryManagerName.ToString()));
        return HasService(InterfaceClass, InZoneID, InRegionID);
    }
    
    template<typename T>
    bool HasServiceImpl(int32 InZoneID, int32 InRegionID, ServiceTags::PoolAllocatorTag) const
    {
        // Use a hard-coded name to avoid relying on StaticClass()
        static FName PoolAllocatorName = TEXT("UPoolAllocator");
        UClass* InterfaceClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/MiningSpiceCoPilot.%s"), *PoolAllocatorName.ToString()));
        return HasService(InterfaceClass, InZoneID, InRegionID);
    }
    
    template<typename T>
    bool HasServiceImpl(int32 InZoneID, int32 InRegionID, ServiceTags::BufferProviderTag) const
    {
        // Use a hard-coded name to avoid relying on StaticClass()
        static FName BufferProviderName = TEXT("UBufferProvider");
        UClass* InterfaceClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/MiningSpiceCoPilot.%s"), *BufferProviderName.ToString()));
        return HasService(InterfaceClass, InZoneID, InRegionID);
    }
    
    template<typename T>
    bool HasServiceImpl(int32 InZoneID, int32 InRegionID, ServiceTags::MemoryTrackerTag) const
    {
        // Use a hard-coded name to avoid relying on StaticClass()
        static FName MemoryTrackerName = TEXT("UMemoryTracker");
        UClass* InterfaceClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/MiningSpiceCoPilot.%s"), *MemoryTrackerName.ToString()));
        return HasService(InterfaceClass, InZoneID, InRegionID);
    }
    
    template<typename T>
    bool HasServiceImpl(int32 InZoneID, int32 InRegionID, ServiceTags::CompressionUtilityTag) const
    {
        return HasService(nullptr, InZoneID, InRegionID);
    }
    
    template<typename T>
    bool HasServiceImpl(int32 InZoneID, int32 InRegionID, ServiceTags::DefaultTag) const
    {
        return HasService(nullptr, InZoneID, InRegionID);
    }

    // ************** Implementation for RegisterServiceWithVersion **************
    template<typename T>
    bool RegisterServiceWithVersionImpl(T* InService, const FServiceVersion& InVersion, int32 InZoneID, int32 InRegionID, ServiceTags::DefaultTag)
    {
        return RegisterServiceWithVersion(InService, nullptr, InVersion, InZoneID, InRegionID);
    }
    
    // ************** Implementation for ResolveServiceWithVersion **************
    template<typename T>
    T* ResolveServiceWithVersionImpl(FServiceVersion& OutVersion, const FServiceVersion* InMinVersion, int32 InZoneID, int32 InRegionID, ServiceTags::DefaultTag)
    {
        return static_cast<T*>(ResolveServiceWithVersion(nullptr, OutVersion, InMinVersion, InZoneID, InRegionID));
    }
    
    // ************** Implementation for DeclareDependency **************
    template<typename TDependent, typename TDependency>
    bool DeclareDependencyImpl(EServiceDependencyType InDependencyKind, ServiceTags::DefaultTag, ServiceTags::DefaultTag)
    {
        return DeclareDependency(nullptr, nullptr, InDependencyKind);
    }
    
    // ************** Implementation for GetServiceHealth **************
    template<typename T>
    EServiceHealthStatus GetServiceHealthImpl(int32 InZoneID, int32 InRegionID, ServiceTags::DefaultTag)
    {
        return GetServiceHealth(nullptr, InZoneID, InRegionID);
    }
    
    // ************** Implementation for RecoverService **************
    template<typename T>
    bool RecoverServiceImpl(int32 InZoneID, int32 InRegionID, ServiceTags::DefaultTag)
    {
        return RecoverService(nullptr, InZoneID, InRegionID);
    }
    
    // ************** Implementation for GetServiceScope **************
    template<typename T>
    EServiceScope GetServiceScopeImpl(int32 InZoneID, int32 InRegionID, ServiceTags::DefaultTag)
    {
        return GetServiceScope(nullptr, InZoneID, InRegionID);
    }
    
    // ************** Implementation for GetDependentServices **************
    template<typename T>
    TArray<UClass*> GetDependentServicesImpl(ServiceTags::DefaultTag)
    {
        return GetDependentServices(nullptr);
    }
    
    // ************** Implementation for GetServiceDependencies **************
    template<typename T>
    TArray<UClass*> GetServiceDependenciesImpl(ServiceTags::DefaultTag)
    {
        return GetServiceDependencies(nullptr);
    }
};