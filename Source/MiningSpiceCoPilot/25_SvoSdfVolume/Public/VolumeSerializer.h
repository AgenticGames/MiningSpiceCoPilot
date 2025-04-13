// VolumeSerializer.h
// Serialization system for SVO+SDF hybrid volume data

#pragma once

#include "CoreMinimal.h"
#include "Math/Box.h"
#include "Containers/Map.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/ArchiveSaveCompressedProxy.h"
#include "Serialization/ArchiveLoadCompressedProxy.h"

// Forward declarations
class USVOHybridVolume;
class FOctreeNodeManager;
class FMaterialSDFManager;

/**
 * Serialization system for SVO+SDF hybrid volume data
 * Provides efficient serialization with multi-tier compression and delta encoding
 * Supports different types of serialization for various use cases
 */
class MININGSPICECOPILOT_API FVolumeSerializer
{
public:
    // Serialization modes for different use cases
    enum class ESerializeMode : uint8
    {
        Full,           // Complete volume serialization
        Delta,          // Delta serialization for efficient updates
        Streaming,      // Streaming-optimized format with essential data first
        Partial         // Partial volume serialization for specific regions
    };

    // Compression levels for serialization
    enum class ECompressionLevel : uint8
    {
        None,           // No compression
        Fast,           // Fast, low-ratio compression
        Normal,         // Balanced compression
        High            // High-ratio, slower compression
    };

    // Structure for delta serialization state
    struct FDeltaState
    {
        uint64 BaseVersion;
        uint64 TargetVersion;
        TArray<uint32> ModifiedNodes;
        TArray<uint8> MaterialIndices;
        TSet<FBox> ModifiedRegions;
        
        FDeltaState()
            : BaseVersion(0)
            , TargetVersion(0)
        {}
    };

    // Constructor and destructor
    FVolumeSerializer();
    ~FVolumeSerializer();
    
    // Initialization
    void Initialize(USVOHybridVolume* InVolume);
    void SetDependencies(FOctreeNodeManager* InNodeManager, FMaterialSDFManager* InMaterialManager);
    
    // Full volume serialization
    void SerializeState(FArchive& Ar);
    TArray<uint8> SerializeToBuffer(ESerializeMode Mode = ESerializeMode::Full,
                                   ECompressionLevel Compression = ECompressionLevel::Normal);
    bool DeserializeFromBuffer(const TArray<uint8>& Buffer);
    
    // Version-controlled serialization
    void SerializeStateDelta(FArchive& Ar, uint64 BaseVersion);
    TArray<uint8> SerializeDeltaToBuffer(uint64 BaseVersion, 
                                        ECompressionLevel Compression = ECompressionLevel::Fast);
    bool DeserializeDeltaFromBuffer(const TArray<uint8>& Buffer, uint64 BaseVersion);
    
    // Region-based serialization
    void SerializeRegion(FArchive& Ar, const FBox& Region);
    TArray<uint8> SerializeRegionToBuffer(const FBox& Region,
                                         ECompressionLevel Compression = ECompressionLevel::Normal);
    bool DeserializeRegionFromBuffer(const TArray<uint8>& Buffer, const FBox& Region);
    
    // Material-specific serialization
    void SerializeMaterialChannel(FArchive& Ar, uint8 MaterialIndex);
    TArray<uint8> SerializeMaterialChannelToBuffer(uint8 MaterialIndex,
                                                  ECompressionLevel Compression = ECompressionLevel::Normal);
    bool DeserializeMaterialChannelFromBuffer(const TArray<uint8>& Buffer, uint8 MaterialIndex);
    
    // Progressive loading support
    TArray<uint8> SerializeEssentialData();
    bool DeserializeEssentialData(const TArray<uint8>& Buffer);
    TArray<uint8> SerializeDetailData(uint8 DetailLevel);
    bool DeserializeDetailData(const TArray<uint8>& Buffer, uint8 DetailLevel);
    
    // Optimization and configuration
    void SetCompressionLevel(ECompressionLevel Level);
    void OptimizeForNetworkTransfer();
    void OptimizeForStorageSize();
    void OptimizeForLoadTime();
    
    // Network synchronization support
    TArray<uint8> GenerateNetworkDelta(uint64 BaseVersion, uint64 TargetVersion);
    bool ApplyNetworkDelta(const TArray<uint8>& DeltaBuffer, uint64 BaseVersion, uint64 TargetVersion);
    uint64 GetLastSerializedVersion() const;
    
    // Serialization statistics
    uint32 GetLastSerializedSize() const;
    float GetCompressionRatio() const;
    double GetLastSerializationTime() const;
    double GetLastDeserializationTime() const;
    
private:
    // Internal data
    USVOHybridVolume* Volume;
    FOctreeNodeManager* NodeManager;
    FMaterialSDFManager* MaterialManager;
    ECompressionLevel CompressionLevel;
    
    // Serialization state tracking
    uint64 LastSerializedVersion;
    uint32 LastSerializedSize;
    uint32 LastUncompressedSize;
    double LastSerializationTime;
    double LastDeserializationTime;
    
    // Delta encoding state
    TMap<uint64, FDeltaState> DeltaStates;
    
    // Internal implementation methods
    void SerializeNodeStructure(FMemoryWriter& MemWriter);
    void SerializeMaterialData(FMemoryWriter& MemWriter);
    void SerializeMetadata(FMemoryWriter& MemWriter);
    
    bool DeserializeNodeStructure(FMemoryReader& MemReader);
    bool DeserializeMaterialData(FMemoryReader& MemReader);
    bool DeserializeMetadata(FMemoryReader& MemReader);
    
    TArray<uint8> CompressBuffer(const TArray<uint8>& UncompressedData, ECompressionLevel Level);
    TArray<uint8> DecompressBuffer(const TArray<uint8>& CompressedData);
    
    // Memory optimization helpers
    void CleanupOldDeltaStates(int32 MaxStatesToKeep = 5);
    TArray<uint32> GetNodesInRegion(const FBox& Region) const;
    bool IsNodeInRegion(uint32 NodeIndex, const FBox& Region) const;
};