// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Interfaces/Compression/ICompressionManager.h"
#include "ISDFCompressionSystem.generated.h"

/**
 * SDF field encoding methods for specialized compression
 */
enum class ESDFEncodingMethod : uint8
{
    /** Floating-point precision encoding */
    FloatingPoint,
    
    /** Fixed-point quantized encoding */
    FixedPoint,
    
    /** Adaptive precision based on distance */
    AdaptivePrecision,
    
    /** Distance-based precision with wavelet encoding */
    WaveletEncoding,
    
    /** Gradient-based encoding preserving material boundaries */
    GradientBased,
    
    /** Run-length encoding for homogeneous regions */
    RunLength,
    
    /** Multi-resolution octree-based encoding */
    Hierarchical
};

/**
 * Structure containing SDF-specific compression parameters
 */
struct MININGSPICECOPILOT_API FSDFCompressionParams
{
    /** Encoding method to use */
    ESDFEncodingMethod EncodingMethod;
    
    /** Precision in bits for fixed-point encoding */
    uint8 FixedPointPrecision;
    
    /** Narrow band width in voxel units */
    float NarrowBandWidth;
    
    /** Error tolerance for lossy compression */
    float ErrorTolerance;
    
    /** Material boundary preservation weight (0-1) */
    float BoundaryPreservationWeight;
    
    /** Whether to preserve gradients at material interfaces */
    bool bPreserveGradients;
    
    /** Block size for block-based compression */
    uint32 BlockSize;
    
    /** Number of material channels to encode */
    uint32 MaterialChannelCount;
    
    /** Weights for material channels (higher weight = less compression) */
    TArray<float> ChannelWeights;
    
    /** Whether to use delta encoding for modifications */
    bool bUseDeltaEncoding;
    
    /** Constructor with defaults */
    FSDFCompressionParams()
        : EncodingMethod(ESDFEncodingMethod::AdaptivePrecision)
        , FixedPointPrecision(16)
        , NarrowBandWidth(3.0f)
        , ErrorTolerance(0.01f)
        , BoundaryPreservationWeight(0.8f)
        , bPreserveGradients(true)
        , BlockSize(8)
        , MaterialChannelCount(1)
        , bUseDeltaEncoding(false)
    {
        ChannelWeights.Add(1.0f);
    }
};

/**
 * Structure containing SDF field compression metrics
 */
struct MININGSPICECOPILOT_API FSDFCompressionMetrics : public FCompressionStats
{
    /** Mean absolute error after compression */
    float MeanAbsoluteError;
    
    /** Maximum absolute error after compression */
    float MaxAbsoluteError;
    
    /** Root mean squared error */
    float RootMeanSquaredError;
    
    /** Peak signal-to-noise ratio */
    float PSNR;
    
    /** Error at material boundaries */
    float BoundaryError;
    
    /** Percentage of zero values preserved exactly */
    float ZeroPreservationRate;
    
    /** Percentage of material boundaries preserved accurately */
    float BoundaryPreservationRate;
    
    /** Accuracy of gradient preservation */
    float GradientPreservationAccuracy;
    
    /** Effective bits per value after compression */
    float BitsPerValue;
    
    /** Encoding method used */
    ESDFEncodingMethod EncodingMethod;
    
    /** Compression metrics per material channel */
    TArray<float> PerChannelErrors;
};

/**
 * Base interface for SDF field compression systems in the SVO+SDF mining architecture
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class USDFCompressionSystem : public UInterface
{
    GENERATED_BODY()
};

/**
 * Interface for SDF field compression in the SVO+SDF mining architecture
 * Provides SDF-specific compression techniques optimized for distance field data
 */
class MININGSPICECOPILOT_API ISDFCompressionSystem
{
    GENERATED_BODY()

public:
    /**
     * Initializes the SDF compression system and prepares it for use
     * @return True if initialization was successful
     */
    virtual bool Initialize() = 0;

    /**
     * Shuts down the SDF compression system and cleans up resources
     */
    virtual void Shutdown() = 0;
    
    /**
     * Checks if the SDF compression system has been initialized
     * @return True if initialized, false otherwise
     */
    virtual bool IsInitialized() const = 0;
    
    /**
     * Compresses an SDF field using the specified parameters
     * @param SDFData Source SDF field data
     * @param CompressedData Output buffer for compressed data
     * @param Params Compression parameters
     * @param Tier Compression tier to use
     * @return True if compression was successful
     */
    virtual bool CompressSDFField(
        const TArrayView<const float>& SDFData, 
        TArray<uint8>& CompressedData,
        const FSDFCompressionParams& Params,
        ECompressionTier Tier = ECompressionTier::Standard) = 0;
    
    /**
     * Decompresses an SDF field previously compressed with CompressSDFField
     * @param CompressedData Source compressed data
     * @param SDFData Output buffer for decompressed SDF field
     * @param Params Compression parameters used during compression
     * @return True if decompression was successful
     */
    virtual bool DecompressSDFField(
        const TArrayView<const uint8>& CompressedData, 
        TArray<float>& SDFData,
        const FSDFCompressionParams& Params) = 0;
    
    /**
     * Compresses a multi-channel SDF field
     * @param ChannelData Array of SDF channels
     * @param CompressedData Output buffer for compressed data
     * @param Params Compression parameters
     * @param Tier Compression tier to use
     * @return True if compression was successful
     */
    virtual bool CompressMultiChannelField(
        const TArray<TArrayView<const float>>& ChannelData, 
        TArray<uint8>& CompressedData,
        const FSDFCompressionParams& Params,
        ECompressionTier Tier = ECompressionTier::Standard) = 0;
    
    /**
     * Decompresses a multi-channel SDF field
     * @param CompressedData Source compressed data
     * @param ChannelData Output array of SDF channels
     * @param Params Compression parameters used during compression
     * @return True if decompression was successful
     */
    virtual bool DecompressMultiChannelField(
        const TArrayView<const uint8>& CompressedData, 
        TArray<TArray<float>>& ChannelData,
        const FSDFCompressionParams& Params) = 0;
    
    /**
     * Compresses a delta modification to an existing SDF field
     * @param OriginalData Original SDF field data
     * @param ModifiedData Modified SDF field data
     * @param CompressedDelta Output buffer for compressed delta
     * @param Params Compression parameters
     * @return True if compression was successful
     */
    virtual bool CompressDeltaModification(
        const TArrayView<const float>& OriginalData,
        const TArrayView<const float>& ModifiedData, 
        TArray<uint8>& CompressedDelta,
        const FSDFCompressionParams& Params) = 0;
    
    /**
     * Applies a compressed delta to an SDF field
     * @param OriginalData Original SDF field data to modify
     * @param CompressedDelta Compressed delta data
     * @param ModifiedData Output buffer for modified SDF field
     * @param Params Compression parameters used during compression
     * @return True if delta application was successful
     */
    virtual bool ApplyCompressedDelta(
        const TArrayView<const float>& OriginalData,
        const TArrayView<const uint8>& CompressedDelta, 
        TArray<float>& ModifiedData,
        const FSDFCompressionParams& Params) = 0;
    
    /**
     * Gets the optimal encoding method for an SDF field
     * @param SDFData SDF field data to analyze
     * @param MaterialBoundaryMask Optional mask indicating material boundaries
     * @param Tier Compression tier to use
     * @return Recommended encoding method
     */
    virtual ESDFEncodingMethod GetOptimalEncodingMethod(
        const TArrayView<const float>& SDFData,
        const TArrayView<const uint8>& MaterialBoundaryMask,
        ECompressionTier Tier) = 0;
    
    /**
     * Analyzes an SDF field for compression characteristics
     * @param SDFData SDF field data to analyze
     * @param Params Optional compression parameters to guide analysis
     * @param OutMetrics Optional output structure for analysis metrics
     * @return True if analysis was successful
     */
    virtual bool AnalyzeSDFField(
        const TArrayView<const float>& SDFData,
        const FSDFCompressionParams& Params,
        FSDFCompressionMetrics* OutMetrics = nullptr) = 0;
    
    /**
     * Estimates the compressed size for an SDF field
     * @param SDFData SDF field data to analyze
     * @param Params Compression parameters
     * @param Tier Compression tier to use
     * @return Estimated compressed size in bytes
     */
    virtual uint64 EstimateCompressedSize(
        const TArrayView<const float>& SDFData,
        const FSDFCompressionParams& Params,
        ECompressionTier Tier) = 0;
    
    /**
     * Gets compression metrics for an SDF field
     * @param SDFData Original SDF field data
     * @param CompressedData Compressed data
     * @param Params Compression parameters
     * @return Structure containing compression metrics
     */
    virtual FSDFCompressionMetrics GetCompressionMetrics(
        const TArrayView<const float>& SDFData,
        const TArrayView<const uint8>& CompressedData,
        const FSDFCompressionParams& Params) = 0;
    
    /**
     * Registers a custom encoding method for SDF compression
     * @param EncodingMethodName Name to identify the encoding method
     * @return True if registration was successful
     */
    virtual bool RegisterEncodingMethod(const FName& EncodingMethodName) = 0;
    
    /**
     * Gets the singleton instance of the SDF compression system
     * @return Reference to the SDF compression system instance
     */
    static ISDFCompressionSystem& Get();
};