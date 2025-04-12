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
 * Utility class for compression operations focused on SVO+SDF data
 * Provides specialized compression for different mining data types
 */
class MININGSPICECOPILOT_API FCompressionUtility
{
public:
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
};
