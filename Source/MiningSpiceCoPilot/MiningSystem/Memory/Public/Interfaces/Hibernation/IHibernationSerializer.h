// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Interfaces/Hibernation/IHibernationManager.h"
#include "IHibernationSerializer.generated.h"

/**
 * Serialization format for hibernated regions
 */
enum class EHibernationSerializationFormat : uint8
{
    /** Standard binary format for most regions */
    StandardBinary,
    
    /** Optimized format for regions with lots of homogeneous space */
    HomogeneousOptimized,
    
    /** Optimized format for regions with mining modifications */
    MiningModificationOptimized,
    
    /** Format with material-specific optimizations */
    MaterialSpecific,
    
    /** Format with maximum compression */
    MaxCompression,
    
    /** Format optimized for fast loading */
    FastLoading,
    
    /** Differential format for regions with minor changes */
    Differential
};

/**
 * Serialization stage for incremental hibernation
 */
enum class EHibernationSerializationStage : uint8
{
    /** Preparing serialization metadata */
    Preparation,
    
    /** Serializing SVO structure */
    SVOStructure,
    
    /** Serializing SDF fields */
    SDFFields,
    
    /** Serializing material data */
    MaterialData,
    
    /** Serializing mining modifications */
    MiningModifications,
    
    /** Serializing portal connections */
    PortalConnections,
    
    /** Compressing serialized data */
    Compression,
    
    /** Writing to disk */
    DiskWrite,
    
    /** Serialization completed */
    Completed,
    
    /** Serialization failed */
    Failed
};

/**
 * Structure containing serialization metrics
 */
struct MININGSPICECOPILOT_API FSerializationMetrics
{
    /** Original region memory usage in bytes */
    uint64 OriginalMemoryBytes;
    
    /** Serialized size in bytes */
    uint64 SerializedSizeBytes;
    
    /** Compression ratio achieved */
    float CompressionRatio;
    
    /** Time spent on serialization in milliseconds */
    float SerializationTimeMs;
    
    /** Number of SVO nodes serialized */
    uint32 SVONodeCount;
    
    /** Memory size of SVO structure in bytes */
    uint64 SVOStructureSizeBytes;
    
    /** Memory size of SDF fields in bytes */
    uint64 SDFFieldsSizeBytes;
    
    /** Memory size of material data in bytes */
    uint64 MaterialDataSizeBytes;
    
    /** Memory size of mining modifications in bytes */
    uint64 MiningModificationsSizeBytes;
    
    /** Memory size of portal connections in bytes */
    uint64 PortalConnectionsSizeBytes;
    
    /** Compressed size of SVO structure in bytes */
    uint64 CompressedSVOStructureSizeBytes;
    
    /** Compressed size of SDF fields in bytes */
    uint64 CompressedSDFFieldsSizeBytes;
    
    /** Compressed size of material data in bytes */
    uint64 CompressedMaterialDataSizeBytes;
    
    /** Compressed size of mining modifications in bytes */
    uint64 CompressedMiningModificationsSizeBytes;
    
    /** Compressed size of portal connections in bytes */
    uint64 CompressedPortalConnectionsSizeBytes;
    
    /** Whether this was a differential serialization */
    bool bWasDifferentialSerialization;
    
    /** Number of material channels serialized */
    uint32 MaterialChannelCount;
    
    /** Number of mining modifications serialized */
    uint32 MiningModificationCount;
    
    /** Number of portal connections serialized */
    uint32 PortalConnectionCount;
    
    /** Number of incremental serialization steps */
    uint32 IncrementalSerializationStepCount;
    
    /** Total incremental serialization time in milliseconds */
    float TotalIncrementalTimeMs;
    
    /** Maximum single step time in milliseconds */
    float MaxStepTimeMs;
    
    /** Whether hardware acceleration was used */
    bool bUsedHardwareAcceleration;
    
    /** Time spent on compression in milliseconds */
    float CompressionTimeMs;
    
    /** Time spent on disk write in milliseconds */
    float DiskWriteTimeMs;
    
    /** Checksum for data verification */
    uint32 DataChecksum;
    
    /** Format version used */
    uint32 FormatVersion;
};

/**
 * Structure containing incremental serialization progress
 */
struct MININGSPICECOPILOT_API FIncrementalSerializationProgress
{
    /** Region ID being serialized */
    int32 RegionId;
    
    /** Current stage of serialization */
    EHibernationSerializationStage CurrentStage;
    
    /** Progress within current stage (0.0-1.0) */
    float StageProgress;
    
    /** Overall progress (0.0-1.0) */
    float TotalProgress;
    
    /** Time spent in current stage in milliseconds */
    float CurrentStageTimeMs;
    
    /** Total time spent serializing in milliseconds */
    float TotalTimeMs;
    
    /** Number of completed stages */
    int32 CompletedStages;
    
    /** Total number of stages */
    int32 TotalStages;
    
    /** Whether serialization is completed */
    bool bIsCompleted;
    
    /** Whether serialization encountered an error */
    bool bHasError;
    
    /** Error message if any */
    FString ErrorMessage;
    
    /** Memory processed so far in bytes */
    uint64 ProcessedMemoryBytes;
    
    /** Total memory to process in bytes */
    uint64 TotalMemoryBytes;
    
    /** Estimated time remaining in milliseconds */
    float EstimatedRemainingTimeMs;
};

/**
 * Base interface for hibernation serializers in the SVO+SDF mining architecture
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UHibernationSerializer : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for serializing and deserializing hibernated regions
 * Optimized for SVO+SDF data structures
 */
class MININGSPICECOPILOT_API IHibernationSerializer
{
    GENERATED_BODY()

public:
    /**
     * Initializes the serializer
     * @return True if initialization was successful
     */
    virtual bool Initialize() = 0;

    /**
     * Shuts down the serializer
     */
    virtual void Shutdown() = 0;
    
    /**
     * Checks if the serializer is initialized
     * @return True if initialized
     */
    virtual bool IsInitialized() const = 0;
    
    /**
     * Serializes region data to disk
     * @param RegionId ID of the region
     * @param RegionData Pointer to region data
     * @param Parameters Hibernation parameters
     * @return True if serialization was successful
     */
    virtual bool SerializeRegion(
        int32 RegionId, 
        void* RegionData, 
        const FHibernationParameters& Parameters) = 0;
    
    /**
     * Deserializes region data from disk
     * @param RegionId ID of the region
     * @param OutRegionData Pointer to store deserialized region data
     * @return True if deserialization was successful
     */
    virtual bool DeserializeRegion(int32 RegionId, void*& OutRegionData) = 0;
    
    /**
     * Begins incremental serialization of a region
     * @param RegionId ID of the region
     * @param RegionData Pointer to region data
     * @param Parameters Hibernation parameters
     * @return True if incremental serialization was initiated
     */
    virtual bool BeginIncrementalSerialization(
        int32 RegionId, 
        void* RegionData, 
        const FHibernationParameters& Parameters) = 0;
    
    /**
     * Processes a step of incremental serialization
     * @param RegionId ID of the region
     * @param MaxTimeMs Maximum time to spend on this step in milliseconds
     * @param OutProgress Progress information for the serialization
     * @return True if there are more steps to process
     */
    virtual bool ProcessIncrementalSerializationStep(
        int32 RegionId, 
        float MaxTimeMs, 
        FIncrementalSerializationProgress& OutProgress) = 0;
    
    /**
     * Gets the serialization format used for a hibernated region
     * @param RegionId ID of the region
     * @return The serialization format used
     */
    virtual EHibernationSerializationFormat GetSerializationFormat(int32 RegionId) const = 0;
    
    /**
     * Gets serialization metrics for a hibernated region
     * @param RegionId ID of the region
     * @return Structure containing serialization metrics
     */
    virtual FSerializationMetrics GetSerializationMetrics(int32 RegionId) const = 0;
    
    /**
     * Checks if a region can use differential serialization
     * @param RegionId ID of the region
     * @return True if differential serialization is available
     */
    virtual bool CanUseDifferentialSerialization(int32 RegionId) const = 0;
    
    /**
     * Gets the serialized size of a region
     * @param RegionId ID of the region
     * @return Size in bytes
     */
    virtual uint64 GetSerializedSize(int32 RegionId) const = 0;
    
    /**
     * Gets the incremental serialization progress for a region
     * @param RegionId ID of the region
     * @return Structure containing progress information
     */
    virtual FIncrementalSerializationProgress GetIncrementalProgress(int32 RegionId) const = 0;
    
    /**
     * Cancels an in-progress serialization or deserialization
     * @param RegionId ID of the region
     * @return True if the operation was canceled
     */
    virtual bool CancelOperation(int32 RegionId) = 0;
    
    /**
     * Validates serialized data for a region
     * @param RegionId ID of the region
     * @return True if data is valid
     */
    virtual bool ValidateSerializedData(int32 RegionId) = 0;
    
    /**
     * Preloads essential region components from serialized data
     * @param RegionId ID of the region
     * @return True if preload was successful
     */
    virtual bool PreloadEssentialComponents(int32 RegionId) = 0;
    
    /**
     * Gets the disk path for a hibernated region
     * @param RegionId ID of the region
     * @return Path to the hibernated region file
     */
    virtual FString GetHibernationFilePath(int32 RegionId) const = 0;
    
    /**
     * Removes serialized data for a region
     * @param RegionId ID of the region
     * @return True if the data was removed
     */
    virtual bool RemoveSerializedData(int32 RegionId) = 0;
    
    /**
     * Lists all serialized regions
     * @return Array of serialized region IDs
     */
    virtual TArray<int32> GetAllSerializedRegionIds() const = 0;
    
    /**
     * Checks if a region has serialized data
     * @param RegionId ID of the region
     * @return True if serialized data exists
     */
    virtual bool HasSerializedData(int32 RegionId) const = 0;
    
    /**
     * Memory-maps a hibernated region file for fast access
     * @param RegionId ID of the region
     * @return True if memory mapping was successful
     */
    virtual bool MemoryMapRegion(int32 RegionId) = 0;
    
    /**
     * Unmaps a previously memory-mapped region
     * @param RegionId ID of the region
     * @return True if unmapping was successful
     */
    virtual bool UnmapRegion(int32 RegionId) = 0;
    
    /**
     * Gets the singleton instance of the hibernation serializer
     * @return Reference to the hibernation serializer instance
     */
    static IHibernationSerializer& Get();
};