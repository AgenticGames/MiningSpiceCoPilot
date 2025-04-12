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

    // Initialization
    void Initialize(USVOHybridVolume* InVolume, FOctreeNodeManager* InNodeManager, FMaterialSDFManager* InMaterialManager);
    
    // Full volume serialization
    void SerializeVolume(FArchive& Ar, ESerializationFormat Format);
    void DeserializeVolume(FArchive& Ar, ESerializationFormat Format);
    
    // Delta serialization
    void SerializeVolumeDelta(FArchive& Ar, uint64 BaseVersion, uint64 TargetVersion);
    void DeserializeVolumeDelta(FArchive& Ar, uint64 BaseVersion);
    
    // Region-specific serialization
    void SerializeRegion(FArchive& Ar, const FBox& Region, bool IncludeAllMaterials);
    void DeserializeRegion(FArchive& Ar, const FBox& Region);
    
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
    
    // Compression settings
    void SetCompressionLevel(int32 Level);
    void SetDeltaCompressionThreshold(float Threshold);

private:
    // Internal data structures
    struct FSerializationHeader;
    struct FDeltaRecord;
    struct FVersionSnapshot;
    
    // Implementation details
    USVOHybridVolume* Volume;
    FOctreeNodeManager* NodeManager;
    FMaterialSDFManager* MaterialManager;
    TMap<uint64, FVersionSnapshot> VersionHistory;
    int32 CompressionLevel;
    float DeltaCompressionThreshold;
    
    // Helper methods
    void SerializeOctree(FMemoryWriter& Writer, bool FullData);
    void DeserializeOctree(FMemoryReader& Reader, bool FullData);
    void SerializeMaterialFields(FMemoryWriter& Writer, const TArray<uint8>& MaterialIndices, bool FullData);
    void DeserializeMaterialFields(FMemoryReader& Reader, const TArray<uint8>& MaterialIndices, bool FullData);
    void CompressBuffer(const TArray<uint8>& UncompressedData, TArray<uint8>& CompressedData);
    bool DecompressBuffer(const TArray<uint8>& CompressedData, TArray<uint8>& UncompressedData);
    void CaptureVersionSnapshot(uint64 Version);
    void PruneVersionHistory(uint32 MaxEntries);
};