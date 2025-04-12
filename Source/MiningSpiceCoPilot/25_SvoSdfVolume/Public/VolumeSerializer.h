// VolumeSerializer.h
// Efficient serialization for hybrid volume data with multi-tier compression

#pragma once

#include "CoreMinimal.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Compression/CompressedBuffer.h"

// Forward declarations
class USVOHybridVolume;
class FOctreeNodeManager;
class FMaterialSDFManager;

/**
 * Efficient serialization for hybrid volume data with multi-tier compression
 * Handles hierarchical serialization, delta encoding, and version management
 * Supports network-optimized formats for bandwidth efficiency
 */
class MININGSPICECOPILOT_API FVolumeSerializer
{
public:
    FVolumeSerializer();
    ~FVolumeSerializer();

    // Serialization format types
    enum class ESerializationFormat : uint8
    {
        Full,       // Complete volume data
        Delta,      // Changes only
        Streaming,  // Progressive loading format
        Partial     // Region-specific data
    };

    // Serialization approach selection
    enum class ESerializationMode : uint8
    {
        Complete,       // Full serialization
        Structure,      // Structure only
        Materials,      // Material data only
        DeltaBased      // Incremental changes
    };
    
    // Compression method selection
    enum class ECompressionMethod : uint8
    {
        None,           // No compression
        ZLib,           // Standard compression
        Octree,         // Topology-aware compression
        Hybrid          // Context-dependent hybrid approach
    };

    // Version tracking for incremental updates
    struct FVersionSnapshot
    {
        uint64 VersionId;
        double Timestamp;
        FString Description;
        FBox ModifiedRegion;
        uint32 DataSize;
        
        // Constructor with defaults
        FVersionSnapshot()
            : VersionId(0)
            , Timestamp(0.0)
            , Description("")
            , ModifiedRegion(ForceInit)
            , DataSize(0)
        {}
        
        // Constructor with params
        FVersionSnapshot(uint64 InVersionId, const FString& InDescription, const FBox& InRegion)
            : VersionId(InVersionId)
            , Timestamp(FPlatformTime::Seconds())
            , Description(InDescription)
            , ModifiedRegion(InRegion)
            , DataSize(0)
        {}
    };

    // Initialization
    void Initialize(USVOHybridVolume* InVolume, FOctreeNodeManager* InNodeManager, FMaterialSDFManager* InMaterialManager);
    void Initialize(FOctreeNodeManager* InOctreeManager, FMaterialSDFManager* InMaterialManager);
    
    // Full volume serialization
    void SerializeVolume(FArchive& Ar, ESerializationFormat Format);
    TArray<uint8> SerializeVolume(ESerializationMode Mode = ESerializationMode::Complete, 
                                 ECompressionMethod Compression = ECompressionMethod::Hybrid);
    void DeserializeVolume(FArchive& Ar, ESerializationFormat Format);
    bool DeserializeVolume(const TArray<uint8>& Data);
    
    // Delta serialization
    void SerializeVolumeDelta(FArchive& Ar, uint64 BaseVersion, uint64 TargetVersion);
    void DeserializeVolumeDelta(FArchive& Ar, uint64 BaseVersion);
    TArray<uint8> GenerateDelta(uint64 BaseVersion, uint64 TargetVersion);
    bool ApplyDelta(const TArray<uint8>& DeltaData, uint64 BaseVersion);
    
    // Region-specific serialization
    void SerializeRegion(FArchive& Ar, const FBox& Region, bool IncludeAllMaterials);
    TArray<uint8> SerializeRegion(const FBox& Region, 
                                 ESerializationMode Mode = ESerializationMode::Complete,
                                 ECompressionMethod Compression = ECompressionMethod::Hybrid);
    void DeserializeRegion(FArchive& Ar, const FBox& Region);
    bool DeserializeRegion(const TArray<uint8>& Data, const FBox& TargetRegion);
    
    // Material-selective serialization
    void SerializeMaterialChannels(FArchive& Ar, const TArray<uint8>& MaterialIndices);
    void DeserializeMaterialChannels(FArchive& Ar, const TArray<uint8>& MaterialIndices);
    
    // Network-optimized serialization
    TArray<uint8> GenerateNetworkDelta(uint64 BaseVersion, uint64 TargetVersion);
    void ApplyNetworkDelta(const TArray<uint8>& DeltaData, uint64 BaseVersion);
    int32 EstimateNetworkDeltaSize(uint64 BaseVersion, uint64 TargetVersion);
    
    // Progressive streaming
    void SerializeStreamingData(FArchive& Ar, int32 Priority);
    void DeserializeStreamingData(FArchive& Ar, int32& OutPriority);
    
    // Version management
    uint64 GetCurrentDataVersion() const;
    bool ValidateDataVersion(uint64 Version) const;
    void RegisterVersion(uint64 Version);
    void RegisterVersion(uint64 VersionId, const FString& Description, const FBox& ModifiedRegion);
    TArray<FVersionSnapshot> GetVersionHistory() const;
    
    // Compression settings
    void SetCompressionLevel(int32 Level);
    void SetDeltaCompressionThreshold(float Threshold);
    void SetMaterialChannelMask(const TBitArray<>& ChannelMask);
    void SetPrecisionLevel(uint8 OctreePrecision, uint8 SDFPrecision);
    
    // Performance and size metrics
    uint64 EstimateSerializedSize(const FBox& Region, ESerializationMode Mode) const;
    double GetLastSerializationTime() const;
    double GetLastDeserializationTime() const;
    float GetCompressionRatio() const;
    
    // File operations
    bool SaveToFile(const FString& FilePath, ESerializationMode Mode = ESerializationMode::Complete);
    bool LoadFromFile(const FString& FilePath);

private:
    // Internal data structures
    struct FSerializationHeader;
    struct FDeltaRecord;
    
    // Internal data
    USVOHybridVolume* Volume;
    FOctreeNodeManager* NodeManager;
    FOctreeNodeManager* OctreeManager;
    FMaterialSDFManager* MaterialManager;
    TMap<uint64, FVersionSnapshot> VersionHistory;
    
    // Configuration
    int32 CompressionLevel;
    float DeltaCompressionThreshold;
    TBitArray<> MaterialChannelMask;
    uint8 OctreePrecision;
    uint8 SDFPrecision;
    
    // Performance metrics
    double LastSerializationTime;
    double LastDeserializationTime;
    uint64 UncompressedSize;
    uint64 CompressedSize;
    
    // Helper methods
    void SerializeOctree(FMemoryWriter& Writer, bool FullData);
    void DeserializeOctree(FMemoryReader& Reader, bool FullData);
    void SerializeMaterialFields(FMemoryWriter& Writer, const TArray<uint8>& MaterialIndices, bool FullData);
    void DeserializeMaterialFields(FMemoryReader& Reader, const TArray<uint8>& MaterialIndices, bool FullData);
    void CompressBuffer(const TArray<uint8>& UncompressedData, TArray<uint8>& CompressedData);
    bool DecompressBuffer(const TArray<uint8>& CompressedData, TArray<uint8>& UncompressedData);
    void CaptureVersionSnapshot(uint64 Version);
    void PruneVersionHistory(uint32 MaxEntries);
    void SerializeOctreeStructure(FArchive& Ar);
    void SerializeMaterialData(FArchive& Ar);
    void DeserializeOctreeStructure(FArchive& Ar);
    void DeserializeMaterialData(FArchive& Ar);
    TArray<uint8> CompressData(const TArray<uint8>& RawData, ECompressionMethod Method);
    TArray<uint8> DecompressData(const TArray<uint8>& CompressedData, ECompressionMethod Method);
    uint64 CalculateVersionHash(const TArray<uint8>& Data) const;
    TArray<uint8> GenerateDeltaInternal(const TArray<uint8>& BaseData, const TArray<uint8>& TargetData);
    TArray<uint8> ApplyDeltaInternal(const TArray<uint8>& BaseData, const TArray<uint8>& DeltaData);
};