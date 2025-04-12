// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ICompressionManager.generated.h"

/**
 * Compression tier levels for SVO+SDF data
 */
enum class ECompressionTier : uint8
{
    /** No compression, raw data for active mining regions */
    None,
    
    /** Light compression for recently active regions */
    Light,
    
    /** Standard compression for visible inactive regions */
    Standard,
    
    /** High compression for inactive regions */
    High,
    
    /** Ultra compression for long-term storage */
    Ultra,
    
    /** Serialized to disk with minimal memory footprint */
    Hibernated
};

/**
 * Compression quality settings for different data types
 */
enum class ECompressionQuality : uint8
{
    /** Fastest compression with lowest quality */
    Fastest,
    
    /** Fast compression with medium quality */
    Fast,
    
    /** Balanced compression speed/quality */
    Balanced,
    
    /** High quality compression */
    Quality,
    
    /** Maximum quality compression */
    Maximum
};

/**
 * Types of data to be compressed
 */
enum class ECompressionDataType : uint8
{
    /** Generic data with no specific format */
    Generic,
    
    /** SDF field data with distance values */
    SDFField,
    
    /** SVO octree structure data */
    SVOStructure,
    
    /** Material channel data */
    MaterialChannel,
    
    /** Homogeneous region with uniform values */
    HomogeneousVolume
};

/**
 * Structure containing compression statistics
 */
struct MININGSPICECOPILOT_API FCompressionStats
{
    /** Uncompressed data size in bytes */
    uint64 UncompressedSize;
    
    /** Compressed data size in bytes */
    uint64 CompressedSize;
    
    /** Compression ratio (uncompressed / compressed) */
    float CompressionRatio;
    
    /** Time taken to compress in milliseconds */
    float CompressionTimeMs;
    
    /** Time taken to decompress in milliseconds */
    float DecompressionTimeMs;
    
    /** Memory usage during compression in bytes */
    uint64 CompressionMemoryUsage;
    
    /** Compression quality used */
    ECompressionQuality Quality;
    
    /** Compression tier used */
    ECompressionTier Tier;
    
    /** Data type compressed */
    ECompressionDataType DataType;
    
    /** Number of compression operations performed */
    uint64 CompressionCount;
    
    /** Number of decompression operations performed */
    uint64 DecompressionCount;
};

/**
 * Base interface for compression managers in the SVO+SDF mining architecture
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UCompressionManager : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for tiered compression management in the SVO+SDF mining architecture
 * Provides multi-level compression tailored for different precision zones and activity states
 */
class MININGSPICECOPILOT_API ICompressionManager
{
    GENERATED_BODY()

public:
    /**
     * Initializes the compression manager and prepares it for use
     * @return True if initialization was successful
     */
    virtual bool Initialize() = 0;

    /**
     * Shuts down the compression manager and cleans up resources
     */
    virtual void Shutdown() = 0;
    
    /**
     * Checks if the compression manager has been initialized
     * @return True if initialized, false otherwise
     */
    virtual bool IsInitialized() const = 0;
    
    /**
     * Compresses data using the specified tier
     * @param UncompressedData Source data to compress
     * @param CompressedData Output buffer for compressed data
     * @param DataType Type of data being compressed
     * @param Tier Compression tier to use
     * @param Quality Compression quality setting
     * @return True if compression was successful
     */
    virtual bool CompressData(
        const TArrayView<const uint8>& UncompressedData, 
        TArray<uint8>& CompressedData,
        ECompressionDataType DataType = ECompressionDataType::Generic,
        ECompressionTier Tier = ECompressionTier::Standard,
        ECompressionQuality Quality = ECompressionQuality::Balanced) = 0;
    
    /**
     * Decompresses data previously compressed with CompressData
     * @param CompressedData Source compressed data
     * @param UncompressedData Output buffer for decompressed data
     * @param DataType Type of data being decompressed
     * @return True if decompression was successful
     */
    virtual bool DecompressData(
        const TArrayView<const uint8>& CompressedData, 
        TArray<uint8>& UncompressedData,
        ECompressionDataType DataType = ECompressionDataType::Generic) = 0;
    
    /**
     * Gets the optimal compression tier for a region based on activity state
     * @param RegionId Identifier for the region
     * @param DistanceFromActiveRegion Distance from the active region in region units
     * @param bIsVisible Whether the region is currently visible to the player
     * @param TimeSinceLastActive Time in seconds since the region was last active
     * @return Recommended compression tier
     */
    virtual ECompressionTier GetOptimalTier(
        int32 RegionId, 
        float DistanceFromActiveRegion, 
        bool bIsVisible, 
        float TimeSinceLastActive) = 0;
    
    /**
     * Estimates the compressed size for data with a specific tier and type
     * @param UncompressedSize Size of the data before compression in bytes
     * @param DataType Type of data being compressed
     * @param Tier Compression tier to use
     * @return Estimated compressed size in bytes
     */
    virtual uint64 EstimateCompressedSize(
        uint64 UncompressedSize, 
        ECompressionDataType DataType, 
        ECompressionTier Tier) = 0;
    
    /**
     * Sets compression quality settings for a specific tier
     * @param Tier Compression tier to configure
     * @param Quality Quality setting to use for this tier
     */
    virtual void SetTierQuality(ECompressionTier Tier, ECompressionQuality Quality) = 0;
    
    /**
     * Gets the current compression quality for a tier
     * @param Tier Compression tier to query
     * @return Current quality setting for the tier
     */
    virtual ECompressionQuality GetTierQuality(ECompressionTier Tier) const = 0;
    
    /**
     * Gets compression statistics and metrics
     * @param DataType Optional filter for a specific data type
     * @return Structure containing compression statistics
     */
    virtual FCompressionStats GetCompressionStats(ECompressionDataType DataType = ECompressionDataType::Generic) const = 0;
    
    /**
     * Registers a custom compression strategy for a specific data type
     * @param DataType Type of data for the strategy
     * @param StrategyName Name to identify the strategy
     * @return True if registration was successful
     */
    virtual bool RegisterCompressionStrategy(ECompressionDataType DataType, const FName& StrategyName) = 0;
    
    /**
     * Sets the active compression strategy for a data type
     * @param DataType Type of data to set the strategy for
     * @param StrategyName Name of the registered strategy to use
     * @return True if strategy was successfully set
     */
    virtual bool SetActiveStrategy(ECompressionDataType DataType, const FName& StrategyName) = 0;
    
    /**
     * Gets the singleton instance of the compression manager
     * @return Reference to the compression manager instance
     */
    static ICompressionManager& Get();
};