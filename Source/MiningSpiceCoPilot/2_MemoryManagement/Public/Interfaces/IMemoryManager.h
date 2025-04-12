// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IMemoryManager.generated.h"

// Forward declarations
class IPoolAllocator;
class IBufferProvider;
class IMemoryTracker;

/**
 * Memory access patterns for optimizing allocation strategies
 */
enum class EMemoryAccessPattern : uint8
{
    /** General purpose with balanced characteristics */
    General,
    
    /** Sequential access optimized for streaming operations */
    Sequential,
    
    /** Random access with cache-friendly behavior */
    Random,
    
    /** Mining pattern with focused locality around active zones */
    Mining,
    
    /** SDF operation pattern optimized for distance field calculations */
    SDFOperation,
    
    /** Octree traversal optimized for spatial queries */
    OctreeTraversal
};

/**
 * Memory allocation priority levels for resource management
 */
enum class EMemoryPriority : uint8
{
    /** Critical allocations for player-facing functionality */
    Critical,
    
    /** High priority allocations for active gameplay */
    High,
    
    /** Normal priority for standard game objects */
    Normal,
    
    /** Low priority for background systems */
    Low,
    
    /** Minimal priority for cached data that can be regenerated */
    Cacheable
};

/**
 * Memory tier classifications for hierarchical memory management
 */
enum class EMemoryTier : uint8
{
    /** Hot tier for frequently accessed data with highest performance */
    Hot,
    
    /** Warm tier for actively used data */
    Warm,
    
    /** Cold tier for infrequently accessed data */
    Cold,
    
    /** Archive tier for rarely accessed data that may be compressed */
    Archive
};

/**
 * Base interface for memory manager in the SVO+SDF mining architecture
 * Provides service registration and resolution for memory management
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UMemoryManager : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for memory management in the SVO+SDF mining architecture
 * Provides comprehensive memory management capabilities optimized for mining operations
 */
class MININGSPICECOPILOT_API IMemoryManager
{
    GENERATED_BODY()

public:
    /**
     * Initializes the memory manager and prepares it for use
     * @return True if initialization was successful
     */
    virtual bool Initialize() = 0;

    /**
     * Shuts down the memory manager and cleans up resources
     */
    virtual void Shutdown() = 0;
    
    /**
     * Checks if the memory manager has been initialized
     * @return True if initialized, false otherwise
     */
    virtual bool IsInitialized() const = 0;
    
    /**
     * Creates a memory pool with the specified characteristics
     * @param PoolName Name of the pool for identification
     * @param BlockSize Size of each block in the pool in bytes
     * @param BlockCount Initial number of blocks in the pool
     * @param AccessPattern Expected access pattern for optimizing the pool
     * @param bAllowGrowth Whether the pool can grow beyond initial size
     * @return New pool allocator instance or nullptr if creation failed
     */
    virtual IPoolAllocator* CreatePool(
        const FName& PoolName, 
        uint32 BlockSize, 
        uint32 BlockCount, 
        EMemoryAccessPattern AccessPattern = EMemoryAccessPattern::General,
        bool bAllowGrowth = true) = 0;
    
    /**
     * Gets a pool allocator by name
     * @param PoolName Name of the pool to retrieve
     * @return Pool allocator instance or nullptr if not found
     */
    virtual IPoolAllocator* GetPool(const FName& PoolName) const = 0;
    
    /**
     * Creates a shared buffer for CPU/GPU operations
     * @param BufferName Name of the buffer for identification
     * @param SizeInBytes Size of the buffer in bytes
     * @param bZeroCopy Whether to create a zero-copy buffer when possible
     * @param bGPUWritable Whether the buffer needs to be written to from GPU
     * @return New buffer provider instance or nullptr if creation failed
     */
    virtual IBufferProvider* CreateBuffer(
        const FName& BufferName, 
        uint64 SizeInBytes, 
        bool bZeroCopy = true,
        bool bGPUWritable = false) = 0;
    
    /**
     * Gets a buffer provider by name
     * @param BufferName Name of the buffer to retrieve
     * @return Buffer provider instance or nullptr if not found
     */
    virtual IBufferProvider* GetBuffer(const FName& BufferName) const = 0;
    
    /**
     * Gets the memory tracker for telemetry
     * @return Memory tracker instance
     */
    virtual IMemoryTracker* GetMemoryTracker() const = 0;
    
    /**
     * Performs memory defragmentation during gameplay pauses
     * @param MaxTimeMs Maximum time to spend on defragmentation in milliseconds
     * @param Priority Minimum priority level to consider for defragmentation
     * @return True if defragmentation was performed
     */
    virtual bool DefragmentMemory(float MaxTimeMs = 5.0f, EMemoryPriority Priority = EMemoryPriority::Normal) = 0;
    
    /**
     * Allocates memory from the general heap with specified alignment
     * @param SizeInBytes Size to allocate in bytes
     * @param Alignment Memory alignment requirement (must be power of 2)
     * @return Pointer to allocated memory or nullptr if allocation failed
     */
    virtual void* Allocate(uint64 SizeInBytes, uint32 Alignment = 16) = 0;
    
    /**
     * Frees memory previously allocated with Allocate
     * @param Ptr Pointer to memory to free
     */
    virtual void Free(void* Ptr) = 0;
    
    /**
     * Sets the memory budget for a specific category
     * @param CategoryName Name of the memory category
     * @param BudgetInBytes Budget in bytes
     */
    virtual void SetMemoryBudget(const FName& CategoryName, uint64 BudgetInBytes) = 0;
    
    /**
     * Gets the current memory budget for a specific category
     * @param CategoryName Name of the memory category
     * @return Budget in bytes, or 0 if category not found
     */
    virtual uint64 GetMemoryBudget(const FName& CategoryName) const = 0;
    
    /**
     * Gets the current memory usage for a specific category
     * @param CategoryName Name of the memory category
     * @return Usage in bytes, or 0 if category not found
     */
    virtual uint64 GetMemoryUsage(const FName& CategoryName) const = 0;
    
    /**
     * Registers a memory allocation with the manager for tracking
     * @param Ptr Pointer to allocated memory
     * @param SizeInBytes Size of the allocation in bytes
     * @param CategoryName Category for budget tracking
     * @param AllocationName Optional name for the allocation
     */
    virtual void RegisterAllocation(void* Ptr, uint64 SizeInBytes, const FName& CategoryName, const FName& AllocationName = NAME_None) = 0;
    
    /**
     * Unregisters a memory allocation with the manager
     * @param Ptr Pointer to allocated memory
     * @param CategoryName Category for budget tracking
     */
    virtual void UnregisterAllocation(void* Ptr, const FName& CategoryName) = 0;
    
    /**
     * Gets the singleton instance of the memory manager
     * @return Reference to the memory manager instance
     */
    static IMemoryManager& Get();
};