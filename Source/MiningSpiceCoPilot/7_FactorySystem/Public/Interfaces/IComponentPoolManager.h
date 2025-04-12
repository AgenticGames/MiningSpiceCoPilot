// IComponentPoolManager.h
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IComponentPoolManager.generated.h"

/** Pool allocation strategy */
UENUM(BlueprintType)
enum class EPoolAllocationStrategy : uint8
{
    FirstAvailable,     // Use the first available object in the pool
    LeastRecentlyUsed,  // Use the least recently used object in the pool
    MostRecentlyUsed,   // Use the most recently used object in the pool
    Random              // Randomly select an available object from the pool
};

/** Pool growth strategy */
UENUM(BlueprintType)
enum class EPoolGrowthStrategy : uint8
{
    Fixed,              // Fixed size pool, no growth allowed
    Linear,             // Grow linearly by specified increment
    Exponential,        // Grow exponentially (typically doubles in size)
    OnDemand            // Grow exactly as needed, one at a time
};

/** Component pool configuration */
USTRUCT(BlueprintType)
struct FComponentPoolConfig
{
    GENERATED_BODY()

    // Pool name
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Component Pool")
    FName PoolName;
    
    // Component type for this pool
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Component Pool")
    UClass* ComponentType = nullptr;
    
    // Initial pool size
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Component Pool")
    int32 InitialSize = 10;
    
    // Maximum pool size
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Component Pool")
    int32 MaxSize = 100;
    
    // Whether to pre-allocate the initial size
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Component Pool")
    bool bPreallocate = true;
    
    // Growth strategy when pool is exhausted
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Component Pool")
    EPoolGrowthStrategy GrowthStrategy = EPoolGrowthStrategy::Linear;
    
    // Growth increment (interpretation depends on growth strategy)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Component Pool")
    int32 GrowthIncrement = 5;
    
    // Allocation strategy from the pool
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Component Pool")
    EPoolAllocationStrategy AllocationStrategy = EPoolAllocationStrategy::FirstAvailable;
    
    // Whether components are automatically reset before allocation
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Component Pool")
    bool bAutoReset = true;
    
    // Whether to shrink the pool automatically during idle periods
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Component Pool")
    bool bAutoShrink = false;
    
    // Time in seconds an object must be unused before being eligible for auto-shrink
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Component Pool")
    float ShrinkThresholdSeconds = 30.0f;
    
    // Template object to use for creation (optional)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Component Pool")
    UObject* Template = nullptr;
};

/** Component pool statistics */
USTRUCT(BlueprintType)
struct FComponentPoolStats
{
    GENERATED_BODY()

    // Pool name
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Component Pool Stats")
    FName PoolName;
    
    // Component type
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Component Pool Stats")
    UClass* ComponentType = nullptr;
    
    // Current size of the pool (total objects)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Component Pool Stats")
    int32 CurrentSize = 0;
    
    // Number of available objects in the pool
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Component Pool Stats")
    int32 AvailableCount = 0;
    
    // Number of allocated objects from the pool
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Component Pool Stats")
    int32 AllocatedCount = 0;
    
    // Peak number of allocated objects
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Component Pool Stats")
    int32 PeakAllocated = 0;
    
    // Number of times the pool was grown
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Component Pool Stats")
    int32 GrowthCount = 0;
    
    // Number of times the pool was shrunk
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Component Pool Stats")
    int32 ShrinkCount = 0;
    
    // Number of miss allocations (allocations that triggered growth)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Component Pool Stats")
    int32 MissCount = 0;
    
    // Total number of allocations
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Component Pool Stats")
    int32 TotalAllocations = 0;
    
    // Total number of releases
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Component Pool Stats")
    int32 TotalReleases = 0;
    
    // Average time an object spends allocated (in seconds)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Component Pool Stats")
    float AverageAllocationTimeSeconds = 0.0f;
};

/**
 * Base interface for component pool manager
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UComponentPoolManager : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for managing component instance pools in the SVO+SDF mining architecture
 * Provides efficient object reuse and memory optimization
 */
class MININGSPICECOPILOT_API IComponentPoolManager
{
    GENERATED_BODY()

public:
    /**
     * Initialize the component pool manager
     * @return True if successfully initialized
     */
    virtual bool Initialize() = 0;
    
    /**
     * Shutdown the component pool manager and cleanup resources
     */
    virtual void Shutdown() = 0;
    
    /**
     * Check if the component pool manager is initialized
     * @return True if initialized
     */
    virtual bool IsInitialized() const = 0;
    
    /**
     * Create a component pool with the specified configuration
     * @param InConfig Configuration for the new pool
     * @return True if the pool was created successfully
     */
    virtual bool CreatePool(const FComponentPoolConfig& InConfig) = 0;
    
    /**
     * Destroy a component pool
     * @param InPoolName Name of the pool to destroy
     * @return True if the pool was destroyed successfully
     */
    virtual bool DestroyPool(const FName& InPoolName) = 0;
    
    /**
     * Get a component from the specified pool
     * @param InPoolName Name of the pool to allocate from
     * @param InOuter Outer object for the component (if needed)
     * @param InName Optional name for the component
     * @return Allocated component or nullptr if allocation failed
     */
    virtual UObject* AllocateComponent(const FName& InPoolName, UObject* InOuter = nullptr, FName InName = NAME_None) = 0;
    
    /**
     * Get a component from the pool for the specified component type
     * @param InComponentType Component class to allocate
     * @param InOuter Outer object for the component (if needed)
     * @param InName Optional name for the component
     * @return Allocated component or nullptr if allocation failed
     */
    virtual UObject* AllocateComponentByType(UClass* InComponentType, UObject* InOuter = nullptr, FName InName = NAME_None) = 0;
    
    /**
     * Template helper for type-safe component allocation
     * @param InPoolName Name of the pool to allocate from
     * @param InOuter Outer object for the component (if needed)
     * @param InName Optional name for the component
     * @return Typed allocated component or nullptr if allocation failed
     */
    template<typename ComponentType>
    ComponentType* AllocateComponent(const FName& InPoolName, UObject* InOuter = nullptr, FName InName = NAME_None)
    {
        return Cast<ComponentType>(AllocateComponent(InPoolName, InOuter, InName));
    }
    
    /**
     * Template helper for type-safe component allocation by type
     * @param InOuter Outer object for the component (if needed)
     * @param InName Optional name for the component
     * @return Typed allocated component or nullptr if allocation failed
     */
    template<typename ComponentType>
    ComponentType* AllocateComponentByType(UObject* InOuter = nullptr, FName InName = NAME_None)
    {
        return Cast<ComponentType>(AllocateComponentByType(ComponentType::StaticClass(), InOuter, InName));
    }
    
    /**
     * Return a component to its pool
     * @param InComponent Component to return
     * @return True if the component was successfully returned to its pool
     */
    virtual bool ReleaseComponent(UObject* InComponent) = 0;
    
    /**
     * Check if a component belongs to a managed pool
     * @param InComponent Component to check
     * @return True if the component belongs to a pool
     */
    virtual bool IsPooledComponent(UObject* InComponent) const = 0;
    
    /**
     * Reset a component to its initial state
     * @param InComponent Component to reset
     * @return True if the component was successfully reset
     */
    virtual bool ResetComponent(UObject* InComponent) = 0;
    
    /**
     * Grow a pool by a specified amount
     * @param InPoolName Name of the pool to grow
     * @param InGrowthAmount Number of objects to add to the pool
     * @return True if the pool was successfully grown
     */
    virtual bool GrowPool(const FName& InPoolName, int32 InGrowthAmount) = 0;
    
    /**
     * Shrink a pool by a specified amount or by removing unused objects
     * @param InPoolName Name of the pool to shrink
     * @param InMaxReduction Maximum number of objects to remove (0 for unlimited)
     * @param InMinIdleTimeSeconds Minimum time in seconds an object must be idle to be removed
     * @return Number of objects removed from the pool
     */
    virtual int32 ShrinkPool(const FName& InPoolName, int32 InMaxReduction = 0, float InMinIdleTimeSeconds = 0.0f) = 0;
    
    /**
     * Shrink all pools to reclaim memory
     * @param InMaxReductionPercentage Maximum percentage of each pool to reduce (0-1)
     * @param InMinIdleTimeSeconds Minimum time in seconds an object must be idle to be removed
     * @return Total number of objects removed across all pools
     */
    virtual int32 ShrinkAllPools(float InMaxReductionPercentage = 0.25f, float InMinIdleTimeSeconds = 30.0f) = 0;
    
    /**
     * Get statistics for a specific pool
     * @param InPoolName Name of the pool to get statistics for
     * @param OutStats Statistics for the specified pool
     * @return True if statistics were successfully retrieved
     */
    virtual bool GetPoolStats(const FName& InPoolName, FComponentPoolStats& OutStats) const = 0;
    
    /**
     * Get statistics for all pools
     * @return Array of statistics for all managed pools
     */
    virtual TArray<FComponentPoolStats> GetAllPoolStats() const = 0;
    
    /**
     * Check if a pool exists
     * @param InPoolName Name of the pool to check
     * @return True if the pool exists
     */
    virtual bool HasPool(const FName& InPoolName) const = 0;
    
    /**
     * Check if a pool exists for the specified component type
     * @param InComponentType Component type to check for
     * @return True if a pool exists for the component type
     */
    virtual bool HasPoolForType(UClass* InComponentType) const = 0;
    
    /**
     * Update pools (call periodically to handle auto-shrinking)
     * @param DeltaTime Time elapsed since last update
     */
    virtual void UpdatePools(float DeltaTime) = 0;
    
    /**
     * Get the pool name for a given component (if it is pooled)
     * @param InComponent Component to check
     * @return Pool name or NAME_None if not pooled
     */
    virtual FName GetComponentPoolName(UObject* InComponent) const = 0;
    
    /**
     * Get the singleton instance of the component pool manager
     * @return Reference to the component pool manager instance
     */
    static IComponentPoolManager& Get();
};
