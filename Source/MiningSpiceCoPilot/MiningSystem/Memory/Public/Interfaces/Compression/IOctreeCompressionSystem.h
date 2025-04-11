// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Interfaces/Compression/ICompressionManager.h"
#include "IOctreeCompressionSystem.generated.h"

/**
 * SVO node types for octree compression
 */
enum class ESVONodeType : uint8
{
    /** Empty node with no content */
    Empty,
    
    /** Leaf node containing homogeneous content */
    Homogeneous,
    
    /** Node with a material interface boundary */
    Interface,
    
    /** Node containing mixed material content */
    Mixed,
    
    /** Node with hierarchical child nodes */
    Branch
};

/**
 * Octree encoding methods for specialized compression
 */
enum class EOctreeEncodingMethod : uint8
{
    /** Basic encoding with node type flags */
    Basic,
    
    /** Run-length encoding for node sequences */
    RunLength,
    
    /** Hierarchical encoding with parent-child relationships */
    Hierarchical,
    
    /** Dictionary-based encoding for repeating patterns */
    Dictionary,
    
    /** Pattern-based encoding with instancing */
    Instancing,
    
    /** Entropy coding with prediction */
    Entropy,
    
    /** Type-specific specialized encoding */
    TypeSpecialized
};

/**
 * Structure containing octree-specific compression parameters
 */
struct MININGSPICECOPILOT_API FOctreeCompressionParams
{
    /** Encoding method to use */
    EOctreeEncodingMethod EncodingMethod;
    
    /** Maximum octree depth to encode */
    uint8 MaxDepth;
    
    /** Whether to use node instancing for repeating patterns */
    bool bUseInstancing;
    
    /** Whether to prune empty regions */
    bool bPruneEmpty;
    
    /** Whether to merge similar nodes */
    bool bMergeSimilar;
    
    /** Similarity threshold for node merging (0-1) */
    float SimilarityThreshold;
    
    /** Whether to compress material IDs */
    bool bCompressMaterialIDs;
    
    /** Whether to encode traversal hints for efficient access */
    bool bEncodeTraversalHints;
    
    /** Whether to use delta encoding for modifications */
    bool bUseDeltaEncoding;
    
    /** Block size for pattern matching */
    uint32 BlockSize;
    
    /** Dictionary size for dictionary-based encoding */
    uint32 DictionarySize;
    
    /** Constructor with defaults */
    FOctreeCompressionParams()
        : EncodingMethod(EOctreeEncodingMethod::TypeSpecialized)
        , MaxDepth(8)
        , bUseInstancing(true)
        , bPruneEmpty(true)
        , bMergeSimilar(true)
        , SimilarityThreshold(0.95f)
        , bCompressMaterialIDs(true)
        , bEncodeTraversalHints(true)
        , bUseDeltaEncoding(false)
        , BlockSize(4)
        , DictionarySize(1024)
    {
    }
};

/**
 * Structure containing octree compression metrics
 */
struct MININGSPICECOPILOT_API FOctreeCompressionMetrics : public FCompressionStats
{
    /** Number of nodes in the original octree */
    uint32 OriginalNodeCount;
    
    /** Number of nodes after compression */
    uint32 CompressedNodeCount;
    
    /** Node count reduction percentage */
    float NodeCountReduction;
    
    /** Number of instanced nodes */
    uint32 InstancedNodeCount;
    
    /** Number of pruned empty nodes */
    uint32 PrunedEmptyNodeCount;
    
    /** Number of merged similar nodes */
    uint32 MergedNodeCount;
    
    /** Mean node size in bits */
    float MeanNodeSizeBits;
    
    /** Traversal efficiency metric (lower is better) */
    float TraversalEfficiency;
    
    /** Structure preservation accuracy (0-1) */
    float StructurePreservation;
    
    /** Effective bits per node after compression */
    float BitsPerNode;
    
    /** Encoding method used */
    EOctreeEncodingMethod EncodingMethod;
    
    /** Node metrics by type */
    TMap<ESVONodeType, uint32> NodeCountByType;
    
    /** Compression ratio by node type */
    TMap<ESVONodeType, float> CompressionRatioByType;
};

/**
 * Base interface for octree compression systems in the SVO+SDF mining architecture
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UOctreeCompressionSystem : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for octree compression in the SVO+SDF mining architecture
 * Provides octree-specific compression techniques optimized for SVO structures
 */
class MININGSPICECOPILOT_API IOctreeCompressionSystem
{
    GENERATED_BODY()

public:
    /**
     * Initializes the octree compression system and prepares it for use
     * @return True if initialization was successful
     */
    virtual bool Initialize() = 0;

    /**
     * Shuts down the octree compression system and cleans up resources
     */
    virtual void Shutdown() = 0;
    
    /**
     * Checks if the octree compression system has been initialized
     * @return True if initialized, false otherwise
     */
    virtual bool IsInitialized() const = 0;
    
    /**
     * Compresses an octree structure using the specified parameters
     * @param OctreeData Source octree data buffer
     * @param CompressedData Output buffer for compressed data
     * @param Params Compression parameters
     * @param Tier Compression tier to use
     * @return True if compression was successful
     */
    virtual bool CompressOctree(
        const TArrayView<const uint8>& OctreeData, 
        TArray<uint8>& CompressedData,
        const FOctreeCompressionParams& Params,
        ECompressionTier Tier = ECompressionTier::Standard) = 0;
    
    /**
     * Decompresses an octree structure previously compressed with CompressOctree
     * @param CompressedData Source compressed data
     * @param OctreeData Output buffer for decompressed octree data
     * @param Params Compression parameters used during compression
     * @return True if decompression was successful
     */
    virtual bool DecompressOctree(
        const TArrayView<const uint8>& CompressedData, 
        TArray<uint8>& OctreeData,
        const FOctreeCompressionParams& Params) = 0;
    
    /**
     * Compresses a specific octree node type
     * @param NodeData Source node data buffer
     * @param CompressedData Output buffer for compressed data
     * @param NodeType Type of node being compressed
     * @param Params Compression parameters
     * @return True if compression was successful
     */
    virtual bool CompressNodeType(
        const TArrayView<const uint8>& NodeData, 
        TArray<uint8>& CompressedData,
        ESVONodeType NodeType,
        const FOctreeCompressionParams& Params) = 0;
    
    /**
     * Decompresses a specific octree node type
     * @param CompressedData Source compressed data
     * @param NodeData Output buffer for decompressed node data
     * @param NodeType Type of node being decompressed
     * @param Params Compression parameters used during compression
     * @return True if decompression was successful
     */
    virtual bool DecompressNodeType(
        const TArrayView<const uint8>& CompressedData, 
        TArray<uint8>& NodeData,
        ESVONodeType NodeType,
        const FOctreeCompressionParams& Params) = 0;
    
    /**
     * Compresses a delta modification to an existing octree structure
     * @param OriginalData Original octree data
     * @param ModifiedData Modified octree data
     * @param CompressedDelta Output buffer for compressed delta
     * @param Params Compression parameters
     * @return True if compression was successful
     */
    virtual bool CompressDeltaModification(
        const TArrayView<const uint8>& OriginalData,
        const TArrayView<const uint8>& ModifiedData, 
        TArray<uint8>& CompressedDelta,
        const FOctreeCompressionParams& Params) = 0;
    
    /**
     * Applies a compressed delta to an octree structure
     * @param OriginalData Original octree data to modify
     * @param CompressedDelta Compressed delta data
     * @param ModifiedData Output buffer for modified octree data
     * @param Params Compression parameters used during compression
     * @return True if delta application was successful
     */
    virtual bool ApplyCompressedDelta(
        const TArrayView<const uint8>& OriginalData,
        const TArrayView<const uint8>& CompressedDelta, 
        TArray<uint8>& ModifiedData,
        const FOctreeCompressionParams& Params) = 0;
    
    /**
     * Gets the optimal encoding method for an octree structure
     * @param OctreeData Octree data to analyze
     * @param NodeTypeCounts Optional distribution of node types
     * @param Tier Compression tier to use
     * @return Recommended encoding method
     */
    virtual EOctreeEncodingMethod GetOptimalEncodingMethod(
        const TArrayView<const uint8>& OctreeData,
        const TMap<ESVONodeType, uint32>& NodeTypeCounts,
        ECompressionTier Tier) = 0;
    
    /**
     * Analyzes an octree structure for compression characteristics
     * @param OctreeData Octree data to analyze
     * @param Params Optional compression parameters to guide analysis
     * @param OutNodeTypeCounts Optional output for node type distribution
     * @param OutMetrics Optional output structure for analysis metrics
     * @return True if analysis was successful
     */
    virtual bool AnalyzeOctree(
        const TArrayView<const uint8>& OctreeData,
        const FOctreeCompressionParams& Params,
        TMap<ESVONodeType, uint32>* OutNodeTypeCounts = nullptr,
        FOctreeCompressionMetrics* OutMetrics = nullptr) = 0;
    
    /**
     * Estimates the compressed size for an octree structure
     * @param OctreeData Octree data to analyze
     * @param Params Compression parameters
     * @param Tier Compression tier to use
     * @return Estimated compressed size in bytes
     */
    virtual uint64 EstimateCompressedSize(
        const TArrayView<const uint8>& OctreeData,
        const FOctreeCompressionParams& Params,
        ECompressionTier Tier) = 0;
    
    /**
     * Gets compression metrics for an octree structure
     * @param OctreeData Original octree data
     * @param CompressedData Compressed data
     * @param Params Compression parameters
     * @return Structure containing compression metrics
     */
    virtual FOctreeCompressionMetrics GetCompressionMetrics(
        const TArrayView<const uint8>& OctreeData,
        const TArrayView<const uint8>& CompressedData,
        const FOctreeCompressionParams& Params) = 0;
    
    /**
     * Registers a custom encoding method for octree compression
     * @param EncodingMethodName Name to identify the encoding method
     * @return True if registration was successful
     */
    virtual bool RegisterEncodingMethod(const FName& EncodingMethodName) = 0;
    
    /**
     * Gets the singleton instance of the octree compression system
     * @return Reference to the octree compression system instance
     */
    static IOctreeCompressionSystem& Get();
};