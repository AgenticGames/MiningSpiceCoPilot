// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/LowLevelMemTracker.h"
#include "HAL/MallocAnsi.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformMath.h"
#include "Misc/SpinLock.h"
#include "Misc/ScopeLock.h"
#include "Interfaces/IMemoryManager.h"
#include "Interfaces/IPoolAllocator.h"
#include "Interfaces/IBufferProvider.h"
#include "Interfaces/IMemoryTracker.h"

/**
 * Memory pool manager implementation for the SVO+SDF mining system
 * Provides comprehensive memory management optimized for mining operations
 */
class MININGSPICECOPILOT_API FMemoryPoolManager : public IMemoryManager
{
public:
    /** Constructor */
    FMemoryPoolManager();
    
    /** Destructor */
    virtual ~FMemoryPoolManager();
    
    //~ Begin IMemoryManager Interface
    virtual bool Initialize() override;
    virtual void Shutdown() override;
    virtual bool IsInitialized() const override;
    
    virtual IPoolAllocator* CreatePool(
        const FName& PoolName, 
        uint32 BlockSize, 
        uint32 BlockCount, 
        EMemoryAccessPattern AccessPattern = EMemoryAccessPattern::General,
        bool bAllowGrowth = true) override;
    
    virtual IPoolAllocator* GetPool(const FName& PoolName) const override;
    
    virtual IBufferProvider* CreateBuffer(
        const FName& BufferName, 
        uint64 SizeInBytes, 
        bool bZeroCopy = true,
        bool bGPUWritable = false) override;
    
    virtual IBufferProvider* GetBuffer(const FName& BufferName) const override;
    
    virtual IMemoryTracker* GetMemoryTracker() const override;
    
    virtual bool DefragmentMemory(float MaxTimeMs = 5.0f, EMemoryPriority Priority = EMemoryPriority::Normal) override;
    
    virtual void* Allocate(uint64 SizeInBytes, uint32 Alignment = 16) override;
    
    virtual void Free(void* Ptr) override;
    
    virtual void SetMemoryBudget(const FName& CategoryName, uint64 BudgetInBytes) override;
    
    virtual uint64 GetMemoryBudget(const FName& CategoryName) const override;
    
    virtual uint64 GetMemoryUsage(const FName& CategoryName) const override;
    
    virtual void RegisterAllocation(void* Ptr, uint64 SizeInBytes, const FName& CategoryName, const FName& AllocationName = NAME_None) override;
    
    virtual void UnregisterAllocation(void* Ptr, const FName& CategoryName) override;
    
    virtual TArray<FName> GetPoolNames() const override;
    virtual bool UpdatePointerReference(void* OldPtr, void* NewPtr, uint64 Size) override;
    virtual IPoolAllocator* GetPoolAllocator(const void* Ptr) const override;
    virtual IPoolAllocator* GetPoolForType(uint32 TypeId) const override;

    static IMemoryManager& Get();
    //~ End IMemoryManager Interface

    /**
     * Checks if this system is supported on the current device
     * @return True if the memory manager is supported on this platform
     */
    bool IsSupported() const;
    
    /**
     * Sets the NUMA memory allocation policy
     * @param bUseNUMAAwareness Whether to enable NUMA-aware allocation
     * @param PreferredNode Preferred NUMA node for allocation (if supported)
     * @return True if the policy was successfully set
     */
    bool SetNUMAPolicy(bool bUseNUMAAwareness, int32 PreferredNode = 0);
    
    /**
     * Creates a specialized allocator for SVO octree nodes
     * @param PoolName Name of the pool for identification
     * @param NodeSize Size of each octree node in bytes
     * @param NodeCount Initial number of nodes to allocate
     * @return Specialized SVO node allocator
     */
    IPoolAllocator* CreateSVONodePool(
        const FName& PoolName,
        uint32 NodeSize,
        uint32 NodeCount);
    
    /**
     * Creates a specialized allocator for narrow-band distance field data
     * @param PoolName Name of the pool for identification
     * @param PrecisionTier Precision tier (high/medium/low)
     * @param ChannelCount Number of material channels
     * @param ElementCount Number of elements in the narrow band
     * @return Specialized narrow band allocator
     */
    IPoolAllocator* CreateNarrowBandPool(
        const FName& PoolName,
        EMemoryTier PrecisionTier,
        uint32 ChannelCount,
        uint32 ElementCount);
    
    /**
     * Gets memory usage statistics broken down by components
     * @return Structure containing detailed memory statistics
     */
    FMemoryStats GetDetailedMemoryStats() const;
    
    /**
     * Gets SVO+SDF specific memory metrics
     * @return Structure containing SVO+SDF memory metrics
     */
    FSVOSDFMemoryMetrics GetSVOSDFMemoryMetrics() const;
    
    /**
     * Attempts to proactively reduce memory usage when under pressure
     * @param TargetReductionBytes Desired amount of memory to free in bytes
     * @param MaxTimeMs Maximum time to spend reducing memory
     * @return The number of bytes actually freed
     */
    uint64 ReduceMemoryUsage(uint64 TargetReductionBytes, float MaxTimeMs = 5.0f);
    
    /**
     * Checks if the system is currently under memory pressure
     * @param OutAvailableBytes Optional pointer to receive available physical memory
     * @return True if memory pressure is detected
     */
    bool IsUnderMemoryPressure(uint64* OutAvailableBytes = nullptr) const;
    
    /**
     * Sets an upper limit on the total memory usage
     * @param MaxMemoryBytes Maximum memory usage in bytes
     */
    void SetMaxMemoryLimit(uint64 MaxMemoryBytes);

    /**
     * Configures pool capabilities based on type capabilities
     * @param TypeId The ID of the type to configure
     * @param TypeCapabilities Bitfield of type capabilities that affect memory behavior
     * @param AccessPattern Memory access pattern for the type
     * @param MemoryLayout Memory layout type (0=Sequential, 1=Interleaved, 2=Tiled)
     * @return True if the pool capabilities were configured successfully
     */
    bool ConfigurePoolCapabilities(uint32 TypeId, uint32 TypeCapabilities, 
        EMemoryAccessPattern AccessPattern = EMemoryAccessPattern::General, 
        uint32 MemoryLayout = 0);

    /**
     * Callback for memory warning events from the operating system
     * Used to proactively reduce memory usage
     */
    void OnMemoryWarning();

    /**
     * Gets the total memory used by this manager
     * @return Total memory usage in bytes
     */
    uint64 GetTotalMemoryUsage() const { return GetMemoryUsage(NAME_None); }
    
    /**
     * Creates a type-specific memory pool for registered types
     * @param TypeId The ID of the type to create a pool for
     * @param BlockSize Size of each block in bytes
     * @param BlockCount Initial number of blocks to allocate
     * @param AccessPattern Memory access pattern for the pool
     * @return Pointer to the created pool allocator
     */
    IPoolAllocator* CreateTypeSpecificPool(uint32 TypeId, uint32 BlockSize, uint32 BlockCount, 
        EMemoryAccessPattern AccessPattern = EMemoryAccessPattern::General);
    
    /**
     * Registers fast paths for critical memory operations
     * Integrates with the Core Registry system to optimize memory access for hot paths
     * @param ManagerInstance The memory manager instance to register
     * @return True if fast paths were successfully registered
     */
    bool RegisterFastPath(FMemoryPoolManager* ManagerInstance);
    
    /**
     * Creates a batch of memory pools for multiple types
     * Optimizes allocation by acquiring a single lock for multiple registrations
     * @param TypeInfos Array of type information for pool creation
     * @return Number of pools successfully created
     */
    int32 CreateBatchPools(const TArray<struct FTypePoolInfo>& TypeInfos);
    
    /**
     * Structure containing information for batch pool creation
     */
    struct FTypePoolInfo
    {
        /** Type ID for the pool */
        uint32 TypeId;
        
        /** Name for the pool */
        FName PoolName;
        
        /** Size of each block in bytes */
        uint32 BlockSize;
        
        /** Initial number of blocks to allocate */
        uint32 BlockCount;
        
        /** Memory access pattern for the pool */
        EMemoryAccessPattern AccessPattern;
        
        /** Constructor */
        FTypePoolInfo()
            : TypeId(0)
            , BlockSize(0)
            , BlockCount(0)
            , AccessPattern(EMemoryAccessPattern::General)
        {}
        
        /** Constructor with parameters */
        FTypePoolInfo(uint32 InTypeId, const FName& InPoolName, uint32 InBlockSize, uint32 InBlockCount,
            EMemoryAccessPattern InAccessPattern = EMemoryAccessPattern::General)
            : TypeId(InTypeId)
            , PoolName(InPoolName)
            , BlockSize(InBlockSize)
            , BlockCount(InBlockCount)
            , AccessPattern(InAccessPattern)
        {}
    };

private:
    /**
     * Creates a memory tracker instance
     * @return New memory tracker
     */
    IMemoryTracker* CreateMemoryTracker();
    
    /**
     * Creates an appropriate memory defragmenter instance
     * @return New memory defragmenter
     */
    class FMemoryDefragmenter* CreateDefragmenter();
    
    /**
     * Updates physical memory statistics from the platform
     */
    void UpdateMemoryStats();
    
    /**
     * Dynamically adjusts pool sizes based on memory pressure
     */
    void AdjustPoolSizes();
    
    /**
     * Enforces memory budgets when memory pressure is detected
     * @param PriorityThreshold Minimum priority level to release allocations
     * @return The number of bytes freed
     */
    uint64 EnforceBudgets(EMemoryPriority PriorityThreshold = EMemoryPriority::Cacheable);
    
    /**
     * Releases unused SVO+SDF resources to free memory
     * @param MaxTimeMs Maximum time to spend releasing resources
     * @return The number of bytes freed
     */
    uint64 ReleaseUnusedResources(float MaxTimeMs = 5.0f);

    /** Memory tracker instance */
    IMemoryTracker* MemoryTracker;
    
    /** Memory defragmenter instance */
    class FMemoryDefragmenter* Defragmenter;
    
    /** Map of registered memory pools by name */
    TMap<FName, TSharedPtr<IPoolAllocator>> Pools;
    
    /** Map of registered buffers by name */
    TMap<FName, TSharedPtr<IBufferProvider>> Buffers;
    
    /** Map of type-specific memory pools */
    TMap<uint32, TSharedPtr<IPoolAllocator>> TypePools;
    
    /** Memory budget tracking by category */
    TMap<FName, uint64> MemoryBudgets;
    
    /** Lock for thread-safe access to the pools map */
    mutable FRWLock PoolsLock;
    
    /** Lock for thread-safe access to the buffers map */
    mutable FRWLock BuffersLock;
    
    /** Lock for thread-safe access to the type pools map */
    mutable FCriticalSection TypePoolsLock;
    
    /** Lock for thread-safe access to budgets */
    mutable FRWLock BudgetsLock;
    
    /** Flag indicating if the manager has been initialized */
    FThreadSafeBool bIsInitialized;
    
    /** Maximum memory usage limit in bytes */
    FThreadSafeCounter64 MaxMemoryLimit;
    
    /** Available physical memory from last update */
    FThreadSafeCounter64 AvailablePhysicalMemory;
    
    /** Flag indicating whether NUMA awareness is enabled */
    bool bNUMAAwarenessEnabled;
    
    /** Preferred NUMA node for allocation */
    int32 NUMAPreferredNode;
    
    /** Singleton instance */
    static FMemoryPoolManager* ManagerInstance;
    
    /** Critical section for singleton initialization */
    static FCriticalSection SingletonLock;
};