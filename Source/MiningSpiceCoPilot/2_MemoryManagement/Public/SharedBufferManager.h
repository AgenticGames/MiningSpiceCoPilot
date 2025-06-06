// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IBufferProvider.h"
#include "HAL/ThreadSafeBool.h"
#include "Containers/Queue.h"
#include "HAL/ThreadSafeCounter64.h"
#include "HAL/CriticalSection.h"
#include "SharedBufferManager.generated.h"

/**
 * Buffer priority used to determine which buffers to retain during memory pressure
 */
UENUM()
enum class EBufferPriority : uint8
{
    /** Low priority buffers are released first under memory pressure */
    Low = 0,
    
    /** Medium priority buffers are standard buffers */
    Medium,
    
    /** High priority buffers are retained longer under memory pressure */
    High,
    
    /** Critical buffers are only released as a last resort */
    Critical
};

/**
 * A zone within a buffer that can be managed independently
 * Allows for fine-grained synchronization of specific buffer regions
 */
struct MININGSPICECOPILOT_API FBufferZone
{
    /** Name of this zone */
    FName ZoneName;
    
    /** Offset of this zone in the buffer (in bytes) */
    uint64 OffsetInBytes;
    
    /** Size of this zone in bytes */
    uint64 SizeInBytes;
    
    /** Current version number of the zone content */
    uint64 VersionNumber;
    
    /** Priority of this zone for memory retention */
    EBufferPriority Priority;
    
    /** Whether this zone is currently locked for access */
    bool bIsLocked;
    
    /** Constructor */
    FBufferZone()
        : ZoneName(NAME_None)
        , OffsetInBytes(0)
        , SizeInBytes(0)
        , VersionNumber(1)
        , Priority(EBufferPriority::Medium)
        , bIsLocked(false)
    {
    }
    
    /** Constructor with parameters */
    FBufferZone(FName InZoneName, uint64 InOffsetInBytes, uint64 InSizeInBytes, EBufferPriority InPriority = EBufferPriority::Medium)
        : ZoneName(InZoneName)
        , OffsetInBytes(InOffsetInBytes)
        , SizeInBytes(InSizeInBytes)
        , VersionNumber(1)
        , Priority(InPriority)
        , bIsLocked(false)
    {
    }
};

/**
 * Record of buffer access for telemetry and optimization
 */
struct MININGSPICECOPILOT_API FBufferAccessRecord
{
    /** Time the access occurred */
    double AccessTime;
    
    /** ID of thread that performed the access */
    uint32 ThreadId;
    
    /** Access mode used */
    EBufferAccessMode AccessMode;
    
    /** Constructor */
    FBufferAccessRecord()
        : AccessTime(0.0)
        , ThreadId(0)
        , AccessMode(EBufferAccessMode::ReadOnly)
    {
    }
    
    /** Constructor with parameters */
    FBufferAccessRecord(double InAccessTime, uint32 InThreadId, EBufferAccessMode InAccessMode)
        : AccessTime(InAccessTime)
        , ThreadId(InThreadId)
        , AccessMode(InAccessMode)
    {
    }
};

/**
 * SharedBufferManager implements a reference-counted, version tracked buffer
 * with support for zone-based access and priority management.
 * This class provides thread-safe access to buffer data for both CPU and GPU operations.
 */
class MININGSPICECOPILOT_API FSharedBufferManager : public IBufferProvider
{
public:
    /**
     * Constructor
     * @param InName Name of this buffer
     * @param InSizeInBytes Size of the buffer in bytes
     * @param InGPUWritable Whether the buffer can be written to by the GPU
     */
    FSharedBufferManager(const FName& InName, uint64 InSizeInBytes, bool InGPUWritable = false);

    /** Destructor */
    virtual ~FSharedBufferManager();

    //~ Begin IBufferProvider Interface
    virtual bool Initialize() override;
    virtual void Shutdown() override;
    virtual bool IsInitialized() const override;
    virtual FName GetBufferName() const override;
    virtual uint64 GetSizeInBytes() const override;
    virtual void* Map(EBufferAccessMode AccessMode = EBufferAccessMode::ReadWrite) override;
    virtual bool Unmap() override;
    virtual bool IsMapped() const override;
    virtual bool Resize(uint64 NewSizeInBytes, bool bPreserveContent = true) override;
    virtual void SetUsageHint(EBufferUsage UsageHint) override;
    virtual EBufferUsage GetUsageHint() const override;
    virtual bool SupportsZeroCopy() const override;
    virtual bool IsGPUWritable() const override;
    virtual uint64 GetVersionNumber() const override;
    virtual void* GetGPUResource() const override;
    virtual void AddRef() override;
    virtual uint32 Release() override;
    virtual FBufferStats GetStats() const override;
    virtual bool Validate(TArray<FString>& OutErrors) const override;
    //~ End IBufferProvider Interface

    /**
     * Gets the reference count for this buffer
     * @return Current reference count
     */
    int32 GetRefCount() const;

    /**
     * Gets the raw buffer pointer (CPU memory)
     * @return Raw buffer pointer
     */
    void* GetRawBuffer() const;

    /**
     * Maps the buffer for CPU access
     * @param AccessMode How the buffer will be accessed
     * @return Pointer to the mapped memory
     */
    void* MapBuffer(EBufferAccessMode AccessMode = EBufferAccessMode::ReadWrite);

    /**
     * Unmaps the buffer from CPU access
     */
    void UnmapBuffer();

    /**
     * Checks if the buffer is currently mapped
     * @return True if mapped
     */
    bool IsBufferMapped() const;

    /**
     * Manually syncs CPU data to GPU buffer
     */
    void SyncToGPU();

    /**
     * Manually syncs GPU data to CPU buffer
     */
    void SyncFromGPU();

    /**
     * Checks if the GPU buffer is valid
     * @return True if valid
     */
    bool IsGPUBufferValid() const;

    /**
     * Sets the priority of this buffer for memory management
     * @param InPriority New priority level
     */
    void SetPriority(EBufferPriority InPriority);

    /**
     * Gets the current priority of this buffer
     * @return Current priority level
     */
    EBufferPriority GetPriority() const;

    /**
     * Writes data to a specific position in the buffer
     * @param Data Data to write
     * @param DataSize Size of data in bytes
     * @param OffsetInBytes Offset in buffer to write to
     * @return True if write was successful
     */
    bool Write(const void* Data, uint64 DataSize, uint64 OffsetInBytes);

    /**
     * Reads data from a specific position in the buffer
     * @param OutData Buffer to receive data
     * @param DataSize Size to read in bytes
     * @param OffsetInBytes Offset in buffer to read from
     * @return True if read was successful
     */
    bool Read(void* OutData, uint64 DataSize, uint64 OffsetInBytes) const;

    /**
     * Creates a named zone within the buffer
     * @param ZoneName Name to identify the zone
     * @param OffsetInBytes Offset of the zone in the buffer
     * @param SizeInBytes Size of the zone in bytes
     * @param Priority Priority level for this zone
     * @return True if zone was created successfully
     */
    bool CreateZone(FName ZoneName, uint64 OffsetInBytes, uint64 SizeInBytes, EBufferPriority Priority = EBufferPriority::Medium);

    /**
     * Removes a named zone from the buffer
     * @param ZoneName Name of the zone to remove
     * @return True if zone was removed successfully
     */
    bool RemoveZone(FName ZoneName);

    /**
     * Maps a specific zone for access
     * @param ZoneName Name of the zone to map
     * @param AccessMode How the zone will be accessed
     * @return Pointer to the mapped zone memory
     */
    void* MapZone(FName ZoneName, EBufferAccessMode AccessMode = EBufferAccessMode::ReadWrite);

    /**
     * Unmaps a previously mapped zone
     * @param ZoneName Name of the zone to unmap
     * @return True if zone was unmapped successfully
     */
    bool UnmapZone(FName ZoneName);

    /**
     * Checks if a specific zone is currently mapped
     * @param ZoneName Name of the zone to check
     * @return True if the zone is mapped
     */
    bool IsZoneMapped(FName ZoneName) const;

    /**
     * Gets the version number of a specific zone
     * @param ZoneName Name of the zone
     * @return Version number of the zone
     */
    uint64 GetZoneVersion(FName ZoneName) const;

    /**
     * Gets a pointer to a specific zone's memory
     * @param ZoneName Name of the zone
     * @return Pointer to the zone memory or nullptr if not found
     */
    void* GetZoneBuffer(FName ZoneName) const;

    /**
     * Gets the size of a specific zone
     * @param ZoneName Name of the zone
     * @return Size of the zone in bytes or 0 if not found
     */
    uint64 GetZoneSize(FName ZoneName) const;

    /**
     * Waits for any pending GPU operations to complete
     */
    void WaitForGPU();

    /**
     * Manually synchronizes the buffer
     * @param bToGPU True to sync CPU to GPU, false for GPU to CPU
     */
    void Synchronize(bool bToGPU);

    /**
     * Synchronizes a specific zone
     * @param ZoneName Name of the zone to synchronize
     * @param bToGPU True to sync CPU to GPU, false for GPU to CPU
     * @return True if zone was synchronized successfully
     */
    bool SynchronizeZone(FName ZoneName, bool bToGPU);

    /**
     * Creates a type-safe buffer wrapper based on field type information
     * @param TypeName Name of the field type
     * @param TypeId ID of the field type
     * @param DataSize Size of each field data element in bytes
     * @param AlignmentRequirement Memory alignment requirement for the field
     * @param bSupportsGPU Whether the field type supports GPU operations
     * @param MemoryLayout Memory layout type for field data
     * @param FieldCapabilities Bitfield of field capabilities that affect memory access patterns
     * @return A properly configured buffer optimized for the field type
     */
    static TSharedPtr<FSharedBufferManager> CreateTypedBuffer(
        const FName& TypeName,
        uint32 TypeId,
        uint32 DataSize,
        uint32 AlignmentRequirement,
        bool bSupportsGPU,
        uint32 MemoryLayout,
        uint32 FieldCapabilities,
        uint64 ElementCount = 1024);

private:
    /**
     * Increments the buffer version number
     */
    void BumpVersion();

    /**
     * Increments a zone's version number
     * @param ZoneName Name of the zone
     */
    void BumpZoneVersion(FName ZoneName);

    /**
     * Finds a zone by name
     * @param ZoneName Name of the zone to find
     * @return Pointer to the zone or nullptr if not found
     */
    FBufferZone* FindZone(FName ZoneName);

    /**
     * Finds a zone by name (const version)
     * @param ZoneName Name of the zone to find
     * @return Pointer to the zone or nullptr if not found
     */
    const FBufferZone* FindZone(FName ZoneName) const;

    /**
     * Records an access to the buffer for telemetry
     * @param AccessMode How the buffer was accessed
     */
    void RecordAccess(EBufferAccessMode AccessMode);

    /** Name of this buffer */
    FName Name;

    /** Size of the buffer in bytes */
    uint64 SizeInBytes;

    /** Raw memory buffer */
    void* RawData;

    /** Pointer to mapped memory when buffer is mapped */
    void* MappedData;

    /** Current access mode when buffer is mapped */
    EBufferAccessMode CurrentAccessMode;

    /** Whether the buffer is initialized */
    bool bInitialized;

    /** Whether CPU has made changes that need to be synced to GPU */
    FThreadSafeBool bHasPendingCPUChanges;

    /** Whether GPU has made changes that need to be synced to CPU */
    FThreadSafeBool bHasPendingGPUChanges;

    /** Usage hint for optimization */
    EBufferUsage UsageHint;

    /** Whether the buffer is GPU writable */
    bool bGPUWritable;

    /** Buffer priority for memory management */
    EBufferPriority Priority;

    /** Current version number of the buffer (atomic) */
    FThreadSafeCounter64 VersionNumber;

    /** Reference count for the buffer (atomic) */
    FThreadSafeCounter64 RefCount;

    /** Counter for map operations */
    FThreadSafeCounter64 MapCount;

    /** Counter for unmap operations */
    FThreadSafeCounter64 UnmapCount;

    /** Counter for CPU to GPU sync operations */
    FThreadSafeCounter64 SyncToGPUCount;

    /** Counter for GPU to CPU sync operations */
    FThreadSafeCounter64 SyncFromGPUCount;

    /** Name of the currently mapped zone (if any) */
    FName MappedZoneName;

    /** Map of zones within this buffer */
    TMap<FName, FBufferZone> Zones;

    /** Lock for thread-safe access to buffer */
    mutable FCriticalSection BufferLock;

    /** Lock for thread-safe access to zones */
    mutable FCriticalSection ZoneLock;

    /** History of recent buffer accesses for telemetry */
    TQueue<FBufferAccessRecord> AccessHistory;

    /** Cached stats to avoid recalculating */
    mutable FBufferStats CachedStats;
};
