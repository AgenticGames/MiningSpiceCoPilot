// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Templates/UniquePtr.h"
#include "Math/Vector.h"

// Forward declarations
class FMemoryReader;
class FMemoryWriter;

/**
 * Enumeration of data types for compression operations
 */
enum class EDataType : uint8
{
    Generic,        // Generic data
    SDF,            // Signed Distance Field data
    SVOMaterial,    // Sparse Voxel Octree Material data
    SVOStructure,   // Sparse Voxel Octree Structure data
    ZoneData,       // Zone data
    PositionData,   // Position/coordinate data
    ColorData,      // Color/texture data
    NormalData,     // Surface normal data
    Metadata        // Metadata
};

/**
 * Compression level for memory operations
 */
enum class ECompressionLevel : uint8
{
    None,       // No compression
    Fast,       // Fast compression with lower ratio
    Normal,     // Balanced compression
    High,       // Higher compression ratio, slower
    Maximum     // Maximum compression, slowest
};

/**
 * Compression algorithm selection
 */
enum class ECompressionAlgorithm : uint8
{
    Auto,       // Automatically select based on data
    LZ4,        // Fast compression/decompression
    Zlib,       // Balanced compression
    Zstd,       // High compression ratio
    RLE,        // Run-length encoding for homogeneous data
    Delta,      // Delta encoding for sequential/similar values
    Custom      // Custom algorithm for specific data types
};

/**
 * Enumeration of compression levels for material data
 */
enum class EMaterialCompressionLevel : uint8
{
    /** No compression */
    None,
    
    /** Low compression (high precision) */
    Low,
    
    /** Medium compression (balanced) */
    Medium,
    
    /** High compression (low precision) */
    High,
    
    /** Custom compression settings */
    Custom
};

/**
 * Structure containing compression settings for a material
 */
struct MININGSPICECOPILOT_API FMaterialCompressionSettings
{
    /** Name of the material */
    FName MaterialName;
    
    /** Compression level */
    EMaterialCompressionLevel CompressionLevel;
    
    /** Whether to enable adaptive precision */
    bool bEnableAdaptivePrecision;
    
    /** Whether to enable lossless mode */
    bool bEnableLosslessMode;
    
    /** Custom compression ratio (0-1, where 0 is max compression) */
    float CustomCompressionRatio;
    
    /** Constructor */
    FMaterialCompressionSettings()
        : MaterialName(NAME_None)
        , CompressionLevel(EMaterialCompressionLevel::Medium)
        , bEnableAdaptivePrecision(false)
        , bEnableLosslessMode(false)
        , CustomCompressionRatio(0.5f)
    {
    }
};

/**
 * Utility class for compression operations focused on SVO+SDF data
 * Provides specialized compression for different mining data types
 */
class MININGSPICECOPILOT_API FCompressionUtility
{
public:
    /** Constructor */
    FCompressionUtility();
    
    /** Destructor */
    ~FCompressionUtility();
    
    /**
     * Compresses data using the specified algorithm
     * @param InData Data to compress
     * @param InSize Size of the data in bytes
     * @param OutData Compressed data (caller must free with ReleaseCompressedData)
     * @param OutSize Size of the compressed data in bytes
     * @param Algorithm Compression algorithm to use
     * @param CompressionLevel Level of compression (0-9, 0 is no compression, 9 is max)
     * @return True if compression was successful
     */
    bool CompressData(
        const void* InData, 
        uint64 InSize, 
        void*& OutData, 
        uint64& OutSize, 
        ECompressionAlgorithm Algorithm = ECompressionAlgorithm::LZ4, 
        int32 CompressionLevel = 5);
    
    /**
     * Decompresses data
     * @param InData Compressed data
     * @param InSize Size of the compressed data in bytes
     * @param OutData Decompressed data (caller must free with ReleaseDecompressedData)
     * @param OutSize Size of the decompressed data in bytes
     * @param Algorithm Compression algorithm used
     * @return True if decompression was successful
     */
    bool DecompressData(
        const void* InData, 
        uint64 InSize, 
        void*& OutData, 
        uint64& OutSize, 
        ECompressionAlgorithm Algorithm = ECompressionAlgorithm::LZ4);
    
    /**
     * Releases memory allocated for compressed data
     * @param Data Pointer to compressed data to release
     */
    void ReleaseCompressedData(void* Data);
    
    /**
     * Releases memory allocated for decompressed data
     * @param Data Pointer to decompressed data to release
     */
    void ReleaseDecompressedData(void* Data);
    
    /**
     * Registers compression settings for a material type
     * @param MaterialTypeId ID of the material type
     * @param Settings Compression settings to use
     * @return True if registration was successful
     */
    bool RegisterMaterialCompression(uint32 MaterialTypeId, const FMaterialCompressionSettings& Settings);
    
    /**
     * Gets the best compression algorithm for the given data type
     * @param DataType Type of data to compress
     * @return Recommended compression algorithm
     */
    ECompressionAlgorithm GetBestAlgorithmForDataType(EDataType DataType) const;
    
    /**
     * Gets the number of channels per element
     * @return Number of channels per element
     */
    uint32 GetChannelCount() const;

    /**
     * Gets compression statistics
     * @return Structure containing compression statistics
     */
    struct FCompressionStats
    {
        /** Total bytes before compression */
        uint64 TotalUncompressedBytes;
        
        /** Total bytes after compression */
        uint64 TotalCompressedBytes;
        
        /** Total compression operations */
        uint32 TotalOperations;
        
        /** Number of successful compressions */
        uint32 SuccessfulOperations;
        
        /** Average compression ratio */
        float AverageCompressionRatio;
        
        /** Average compression time in milliseconds */
        float AverageCompressionTimeMs;
        
        /** Best compression ratio achieved */
        float BestCompressionRatio;
        
        /** Worst compression ratio achieved */
        float WorstCompressionRatio;
        
        /** Default constructor */
        FCompressionStats()
            : TotalUncompressedBytes(0)
            , TotalCompressedBytes(0)
            , TotalOperations(0)
            , SuccessfulOperations(0)
            , AverageCompressionRatio(1.0f)
            , AverageCompressionTimeMs(0.0f)
            , BestCompressionRatio(1.0f)
            , WorstCompressionRatio(1.0f)
        {
        }
    };

    FCompressionStats GetCompressionStats() const;

    /**
     * Compresses a memory buffer
     * @param Src Source memory buffer
     * @param SrcSize Size of source buffer in bytes
     * @param OutDest Destination buffer (allocated internally)
     * @param OutDestSize Size of the compressed data in bytes
     * @param Algorithm Compression algorithm to use
     * @param Level Compression level
     * @return True if compression was successful
     */
    static bool Compress(
        const void* Src, 
        uint32 SrcSize, 
        void*& OutDest, 
        uint32& OutDestSize,
        ECompressionAlgorithm Algorithm = ECompressionAlgorithm::Auto,
        ECompressionLevel Level = ECompressionLevel::Normal);

    /**
     * Decompresses a memory buffer
     * @param Src Compressed source buffer
     * @param SrcSize Size of compressed source in bytes
     * @param OutDest Destination buffer (allocated internally)
     * @param OutDestSize Size of the decompressed data in bytes
     * @param Algorithm Compression algorithm to use (Auto will detect)
     * @return True if decompression was successful
     */
    static bool Decompress(
        const void* Src, 
        uint32 SrcSize, 
        void*& OutDest, 
        uint32& OutDestSize,
        ECompressionAlgorithm Algorithm = ECompressionAlgorithm::Auto);

    /**
     * Specialized compression for signed distance field data
     * @param SdfData Source SDF data
     * @param SdfSize Size of SDF data in bytes
     * @param OutCompressed Compressed data (allocated internally)
     * @param OutCompressedSize Size of compressed data in bytes
     * @param MaterialChannelCount Number of material channels in the data
     * @param Level Compression level to use
     * @return True if compression was successful
     */
    static bool CompressSDFData(
        const void* SdfData,
        uint32 SdfSize,
        void*& OutCompressed,
        uint32& OutCompressedSize,
        uint32 MaterialChannelCount,
        ECompressionLevel Level = ECompressionLevel::Normal);

    /**
     * Specialized decompression for signed distance field data
     * @param CompressedData Compressed SDF data
     * @param CompressedSize Size of compressed data in bytes
     * @param OutSdfData Decompressed SDF data (allocated internally)
     * @param OutSdfSize Size of decompressed data in bytes
     * @param MaterialChannelCount Number of material channels (0 to autodetect)
     * @return True if decompression was successful
     */
    static bool DecompressSDFData(
        const void* CompressedData,
        uint32 CompressedSize,
        void*& OutSdfData,
        uint32& OutSdfSize,
        uint32 MaterialChannelCount = 0);

    /**
     * Specialized compression for sparse voxel octree node data
     * @param NodeData Source node data
     * @param NodeSize Size of node data in bytes
     * @param OutCompressed Compressed data (allocated internally)
     * @param OutCompressedSize Size of compressed data in bytes
     * @param IsLeafNode Whether the node is a leaf node
     * @param Level Compression level to use
     * @return True if compression was successful
     */
    static bool CompressSVONodeData(
        const void* NodeData,
        uint32 NodeSize,
        void*& OutCompressed,
        uint32& OutCompressedSize,
        bool IsLeafNode,
        ECompressionLevel Level = ECompressionLevel::Normal);

    /**
     * Specialized decompression for sparse voxel octree node data
     * @param CompressedData Compressed node data
     * @param CompressedSize Size of compressed data in bytes
     * @param OutNodeData Decompressed node data (allocated internally)
     * @param OutNodeSize Size of decompressed data in bytes
     * @param IsLeafNode Whether the node is a leaf node (0 to autodetect)
     * @return True if decompression was successful
     */
    static bool DecompressSVONodeData(
        const void* CompressedData,
        uint32 CompressedSize,
        void*& OutNodeData,
        uint32& OutNodeSize,
        bool* IsLeafNode = nullptr);

    /**
     * Run-length encoding optimized for homogeneous SDF regions
     * @param SdfData Source SDF data
     * @param SdfSize Size of SDF data in bytes
     * @param OutCompressed Compressed data (allocated internally)
     * @param OutCompressedSize Size of compressed data in bytes
     * @param MaterialChannelCount Number of material channels in the data
     * @return True if compression was successful
     */
    static bool CompressHomogeneousSDFRegion(
        const void* SdfData,
        uint32 SdfSize,
        void*& OutCompressed,
        uint32& OutCompressedSize,
        uint32 MaterialChannelCount);

    /**
     * Delta compression for mining modifications
     * @param OriginalData Original data before mining
     * @param ModifiedData Modified data after mining
     * @param DataSize Size of both data buffers in bytes
     * @param OutDeltaData Delta compressed data (allocated internally)
     * @param OutDeltaSize Size of delta data in bytes
     * @return True if delta compression was successful
     */
    static bool CreateDeltaCompression(
        const void* OriginalData,
        const void* ModifiedData,
        uint32 DataSize,
        void*& OutDeltaData,
        uint32& OutDeltaSize);

    /**
     * Apply delta decompression to restore modified data
     * @param OriginalData Original data before mining
     * @param DeltaData Delta compressed data
     * @param DeltaSize Size of delta data in bytes
     * @param OutRestoredData Restored data after mining (allocated internally)
     * @param OutRestoredSize Size of restored data in bytes
     * @return True if delta decompression was successful
     */
    static bool ApplyDeltaDecompression(
        const void* OriginalData,
        const void* DeltaData,
        uint32 DeltaSize,
        void*& OutRestoredData,
        uint32& OutRestoredSize);

    /**
     * Gets the estimated compression ratio for a given algorithm and data type
     * @param Algorithm Compression algorithm to evaluate
     * @param DataType Type of data to compress
     * @return Estimated compression ratio (1.0 means no compression)
     */
    static float GetEstimatedCompressionRatio(
        ECompressionAlgorithm Algorithm,
        FName DataType);

    /**
     * Selects the optimal compression algorithm for a given data type
     * @param DataType Type of data to compress
     * @param Level Desired compression level
     * @return Recommended compression algorithm
     */
    static ECompressionAlgorithm GetRecommendedAlgorithm(
        FName DataType,
        ECompressionLevel Level = ECompressionLevel::Normal);

    /**
     * Serializes data with compression to a binary archive
     * @param Ar Archive to write to
     * @param Data Data to serialize
     * @param DataSize Size of data in bytes
     * @param Algorithm Compression algorithm to use
     * @param Level Compression level
     * @return True if serialization was successful
     */
    static bool SerializeCompressed(
        FArchive& Ar,
        const void* Data,
        uint32 DataSize,
        ECompressionAlgorithm Algorithm = ECompressionAlgorithm::Auto,
        ECompressionLevel Level = ECompressionLevel::Normal);

    /**
     * Deserializes compressed data from a binary archive
     * @param Ar Archive to read from
     * @param OutData Decompressed data (allocated internally)
     * @param OutDataSize Size of decompressed data in bytes
     * @return True if deserialization was successful
     */
    static bool DeserializeCompressed(
        FArchive& Ar,
        void*& OutData,
        uint32& OutDataSize);

private:
    /**
     * Helper function to select the best compression algorithm for the data
     * @param Data Data to analyze
     * @param DataSize Size of data in bytes
     * @param Level Desired compression level
     * @return Recommended compression algorithm
     */
    static ECompressionAlgorithm AnalyzeDataForCompression(
        const void* Data,
        uint32 DataSize,
        ECompressionLevel Level);

    /**
     * Helper function to detect the compression algorithm used
     * @param CompressedData Compressed data to analyze
     * @param CompressedSize Size of compressed data in bytes
     * @return Detected compression algorithm
     */
    static ECompressionAlgorithm DetectCompressionAlgorithm(
        const void* CompressedData,
        uint32 CompressedSize);

    /**
     * Compresses data using run-length encoding
     * @param Data Data to compress
     * @param DataSize Size of data in bytes
     * @param OutCompressed Compressed data (allocated internally)
     * @param OutCompressedSize Size of compressed data in bytes
     * @return True if compression was successful
     */
    static bool CompressRLE(
        const void* Data,
        uint32 DataSize,
        void*& OutCompressed,
        uint32& OutCompressedSize);

    /**
     * Decompresses data using run-length decoding
     * @param CompressedData Compressed data
     * @param CompressedSize Size of compressed data in bytes
     * @param OutData Decompressed data (allocated internally)
     * @param OutDataSize Size of decompressed data in bytes
     * @return True if decompression was successful
     */
    static bool DecompressRLE(
        const void* CompressedData,
        uint32 CompressedSize,
        void*& OutData,
        uint32& OutDataSize);

    /** Lock for thread safety */
    mutable FCriticalSection CriticalSection;
    
    /** Statistics for compression operations */
    mutable FCompressionStats CompressionStats;
    
    /** Whether to track full statistics (can impact performance) */
    bool bTrackDetailedStats;
    
    /** Mapping of material type IDs to compression settings */
    TMap<uint32, FMaterialCompressionSettings> MaterialCompressionSettings;
    
    /** Maximum memory allocation for temporary buffers */
    uint32 MaxTempAllocationSize;
};
