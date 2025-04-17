#include "CompressionUtility.h"
#include "HAL/PlatformMemory.h"
#include "Misc/ScopeLock.h"
#include "Misc/Compression.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/Archive.h"
#include "Serialization/MemoryArchive.h"
#include "Misc/CoreDelegates.h"

// Example: Run-length encoding for homogeneous SDF regions
bool FCompressionUtility::CompressRLE(const void* Data, uint32 DataSize, void*& OutCompressed, uint32& OutCompressedSize)
{
    if (!Data || DataSize == 0) return false;
    const uint8* Src = static_cast<const uint8*>(Data);
    TArray<uint8> Compressed;
    uint32 i = 0;
    while (i < DataSize) {
        uint8 value = Src[i];
        uint8 runLength = 1;
        while (i + runLength < DataSize && Src[i + runLength] == value && runLength < 255) {
            ++runLength;
        }
        Compressed.Add(value);
        Compressed.Add(runLength);
        i += runLength;
    }
    OutCompressedSize = Compressed.Num();
    OutCompressed = FMemory::Malloc(OutCompressedSize);
    FMemory::Memcpy(OutCompressed, Compressed.GetData(), OutCompressedSize);
    return true;
}

bool FCompressionUtility::DecompressRLE(const void* CompressedData, uint32 CompressedSize, void*& OutData, uint32& OutDataSize)
{
    if (!CompressedData || CompressedSize == 0) return false;
    const uint8* Src = static_cast<const uint8*>(CompressedData);
    TArray<uint8> Decompressed;
    uint32 i = 0;
    while (i + 1 < CompressedSize) {
        uint8 value = Src[i];
        uint8 runLength = Src[i + 1];
        for (uint8 k = 0; k < runLength; ++k) {
            Decompressed.Add(value);
        }
        i += 2;
    }
    OutDataSize = Decompressed.Num();
    OutData = FMemory::Malloc(OutDataSize);
    FMemory::Memcpy(OutData, Decompressed.GetData(), OutDataSize);
    return true;
}

// New implementations:

bool FCompressionUtility::CompressSDFData(const void* SdfData, uint32 SdfSize, void*& OutCompressed, uint32& OutCompressedSize, uint32 MaterialChannelCount, ECompressionLevel Level)
{
    if (!SdfData || SdfSize == 0)
    {
        return false;
    }
    
    // For homogeneous regions, RLE is often more efficient
    if (AnalyzeDataForCompression(SdfData, SdfSize, Level) == ECompressionAlgorithm::RLE)
    {
        return CompressHomogeneousSDFRegion(SdfData, SdfSize, OutCompressed, OutCompressedSize, MaterialChannelCount);
    }
    
    // For general SDF data, select algorithm based on data characteristics
    ECompressionAlgorithm Algorithm = ECompressionAlgorithm::Zstd; // Default to high compression
    
    // Adjust algorithm based on compression level
    switch (Level)
    {
        case ECompressionLevel::Fast:
            Algorithm = ECompressionAlgorithm::LZ4;
            break;
        case ECompressionLevel::Normal:
            Algorithm = ECompressionAlgorithm::Zlib;
            break;
        case ECompressionLevel::High:
        case ECompressionLevel::Maximum:
            Algorithm = ECompressionAlgorithm::Zstd;
            break;
        default:
            Algorithm = ECompressionAlgorithm::Zlib;
            break;
    }
    
    // Add compression header with metadata
    TArray<uint8> CompressedWithHeader;
    CompressedWithHeader.AddUninitialized(8); // 8-byte header
    
    // Store algorithm and material channel count in header
    CompressedWithHeader[0] = static_cast<uint8>(Algorithm);
    CompressedWithHeader[1] = static_cast<uint8>(MaterialChannelCount);
    
    // Store original size in header (32-bit)
    uint32* OriginalSizePtr = reinterpret_cast<uint32*>(&CompressedWithHeader[4]);
    *OriginalSizePtr = SdfSize;
    
    // Compress the data
    void* TempCompressed = nullptr;
    uint32 TempCompressedSize = 0;
    
    bool bSuccess = Compress(SdfData, SdfSize, TempCompressed, TempCompressedSize, Algorithm, Level);
    if (!bSuccess || !TempCompressed)
    {
        if (TempCompressed)
        {
            FMemory::Free(TempCompressed);
        }
        return false;
    }
    
    // Add compressed data after header
    const uint32 HeaderSize = CompressedWithHeader.Num();
    CompressedWithHeader.AddUninitialized(TempCompressedSize);
    FMemory::Memcpy(CompressedWithHeader.GetData() + HeaderSize, TempCompressed, TempCompressedSize);
    
    // Free temporary buffer
    FMemory::Free(TempCompressed);
    
    // Allocate and copy final result
    OutCompressedSize = CompressedWithHeader.Num();
    OutCompressed = FMemory::Malloc(OutCompressedSize);
    FMemory::Memcpy(OutCompressed, CompressedWithHeader.GetData(), OutCompressedSize);
    
    return true;
}

bool FCompressionUtility::DecompressSDFData(const void* CompressedData, uint32 CompressedSize, void*& OutSdfData, uint32& OutSdfSize, uint32 MaterialChannelCount)
{
    if (!CompressedData || CompressedSize < 8) // Minimum size for header
    {
        return false;
    }
    
    const uint8* CompressedBytes = static_cast<const uint8*>(CompressedData);
    
    // Read header
    const ECompressionAlgorithm Algorithm = static_cast<ECompressionAlgorithm>(CompressedBytes[0]);
    const uint32 StoredMaterialChannelCount = CompressedBytes[1];
    const uint32 OriginalSize = *reinterpret_cast<const uint32*>(&CompressedBytes[4]);
    
    // Use stored material channel count if none provided
    if (MaterialChannelCount == 0)
    {
        MaterialChannelCount = StoredMaterialChannelCount;
    }
    
    // Check if it's RLE compressed for homogeneous regions
    if (Algorithm == ECompressionAlgorithm::RLE)
    {
        return DecompressRLE(CompressedBytes + 8, CompressedSize - 8, OutSdfData, OutSdfSize);
    }
    
    // Regular decompression
    return Decompress(CompressedBytes + 8, CompressedSize - 8, OutSdfData, OutSdfSize, Algorithm);
}

bool FCompressionUtility::CompressHomogeneousSDFRegion(const void* SdfData, uint32 SdfSize, void*& OutCompressed, uint32& OutCompressedSize, uint32 MaterialChannelCount)
{
    if (!SdfData || SdfSize == 0)
    {
        return false;
    }
    
    // For homogeneous regions, use specialized RLE
    const uint8* Data = static_cast<const uint8*>(SdfData);
    
    // Add a simple header with metadata
    TArray<uint8> CompressedWithHeader;
    CompressedWithHeader.AddUninitialized(8);
    
    // Mark as RLE compressed with material channel count
    CompressedWithHeader[0] = static_cast<uint8>(ECompressionAlgorithm::RLE);
    CompressedWithHeader[1] = static_cast<uint8>(MaterialChannelCount);
    
    // Store original size
    uint32* OriginalSizePtr = reinterpret_cast<uint32*>(&CompressedWithHeader[4]);
    *OriginalSizePtr = SdfSize;
    
    // RLE compression
    void* TempCompressed = nullptr;
    uint32 TempCompressedSize = 0;
    if (!CompressRLE(SdfData, SdfSize, TempCompressed, TempCompressedSize))
    {
        return false;
    }
    
    // Add compressed data after header
    const uint32 HeaderSize = CompressedWithHeader.Num();
    CompressedWithHeader.AddUninitialized(TempCompressedSize);
    FMemory::Memcpy(CompressedWithHeader.GetData() + HeaderSize, TempCompressed, TempCompressedSize);
    
    // Free temporary buffer
    FMemory::Free(TempCompressed);
    
    // Allocate and copy final result
    OutCompressedSize = CompressedWithHeader.Num();
    OutCompressed = FMemory::Malloc(OutCompressedSize);
    FMemory::Memcpy(OutCompressed, CompressedWithHeader.GetData(), OutCompressedSize);
    
    return true;
}

bool FCompressionUtility::CreateDeltaCompression(const void* OriginalData, const void* ModifiedData, uint32 DataSize, void*& OutDeltaData, uint32& OutDeltaSize)
{
    if (!OriginalData || !ModifiedData || DataSize == 0)
    {
        return false;
    }
    
    const uint8* Original = static_cast<const uint8*>(OriginalData);
    const uint8* Modified = static_cast<const uint8*>(ModifiedData);
    
    // Header + delta format:
    // [0-3]: Delta format version (uint32 = 1)
    // [4-7]: Original data size (uint32)
    // [8-11]: Delta entry count (uint32)
    // For each delta entry:
    //   [+0-3]: Offset (uint32)
    //   [+4-7]: Length (uint32)
    //   [+8-...]: Changed data
    
    TArray<uint8> DeltaBuffer;
    DeltaBuffer.AddUninitialized(12); // Initial header
    
    // Version 1
    uint32* VersionPtr = reinterpret_cast<uint32*>(DeltaBuffer.GetData());
    *VersionPtr = 1;
    
    // Original size
    uint32* SizePtr = reinterpret_cast<uint32*>(DeltaBuffer.GetData() + 4);
    *SizePtr = DataSize;
    
    // We'll fill entry count later
    uint32* EntryCountPtr = reinterpret_cast<uint32*>(DeltaBuffer.GetData() + 8);
    *EntryCountPtr = 0;
    
    uint32 EntryCount = 0;
    uint32 CurrentOffset = 0;
    uint32 CurrentRunStart = 0;
    uint32 CurrentRunLength = 0;
    bool bInDifferentRun = false;
    
    // Find and store deltas
    for (uint32 i = 0; i < DataSize; ++i)
    {
        bool bDifferent = (Original[i] != Modified[i]);
        
        if (bDifferent && !bInDifferentRun)
        {
            // Start of a new different run
            CurrentRunStart = i;
            CurrentRunLength = 1;
            bInDifferentRun = true;
        }
        else if (bDifferent && bInDifferentRun)
        {
            // Continue current different run
            CurrentRunLength++;
        }
        else if (!bDifferent && bInDifferentRun)
        {
            // End of a different run, store the delta
            const uint32 EntryHeaderSize = 8; // offset + length
            const uint32 EntrySize = EntryHeaderSize + CurrentRunLength;
            
            const uint32 CurrentSize = DeltaBuffer.Num();
            DeltaBuffer.AddUninitialized(EntrySize);
            
            // Write offset
            uint32* OffsetPtr = reinterpret_cast<uint32*>(DeltaBuffer.GetData() + CurrentSize);
            *OffsetPtr = CurrentRunStart;
            
            // Write length
            uint32* LengthPtr = reinterpret_cast<uint32*>(DeltaBuffer.GetData() + CurrentSize + 4);
            *LengthPtr = CurrentRunLength;
            
            // Write changed data
            FMemory::Memcpy(DeltaBuffer.GetData() + CurrentSize + 8, Modified + CurrentRunStart, CurrentRunLength);
            
            EntryCount++;
            bInDifferentRun = false;
        }
    }
    
    // Handle case where the last bytes are different
    if (bInDifferentRun)
    {
        const uint32 EntryHeaderSize = 8; // offset + length
        const uint32 EntrySize = EntryHeaderSize + CurrentRunLength;
        
        const uint32 CurrentSize = DeltaBuffer.Num();
        DeltaBuffer.AddUninitialized(EntrySize);
        
        uint32* OffsetPtr = reinterpret_cast<uint32*>(DeltaBuffer.GetData() + CurrentSize);
        *OffsetPtr = CurrentRunStart;
        
        uint32* LengthPtr = reinterpret_cast<uint32*>(DeltaBuffer.GetData() + CurrentSize + 4);
        *LengthPtr = CurrentRunLength;
        
        FMemory::Memcpy(DeltaBuffer.GetData() + CurrentSize + 8, Modified + CurrentRunStart, CurrentRunLength);
        
        EntryCount++;
    }
    
    // Update entry count in header
    *EntryCountPtr = EntryCount;
    
    // If the delta is larger than the original data, just use the modified data directly
    if (static_cast<uint32>(DeltaBuffer.Num()) >= static_cast<uint32>(DataSize))
    {
        OutDeltaSize = DataSize + 4; // 4 bytes for a special marker
        OutDeltaData = FMemory::Malloc(OutDeltaSize);
        
        uint32* MarkerPtr = reinterpret_cast<uint32*>(OutDeltaData);
        *MarkerPtr = 0xFFFFFFFF; // Special marker for full data
        
        FMemory::Memcpy(static_cast<uint8*>(OutDeltaData) + 4, ModifiedData, DataSize);
    }
    else
    {
        // Use the delta
        OutDeltaSize = static_cast<uint32>(DeltaBuffer.Num());
        OutDeltaData = FMemory::Malloc(OutDeltaSize);
        FMemory::Memcpy(OutDeltaData, DeltaBuffer.GetData(), OutDeltaSize);
    }
    
    return true;
}

bool FCompressionUtility::ApplyDeltaDecompression(const void* OriginalData, const void* DeltaData, uint32 DeltaSize, void*& OutRestoredData, uint32& OutRestoredSize)
{
    if (!OriginalData || !DeltaData || DeltaSize < 4)
    {
        return false;
    }
    
    const uint8* DeltaBytes = static_cast<const uint8*>(DeltaData);
    const uint32 Marker = *reinterpret_cast<const uint32*>(DeltaBytes);
    
    // Check for special marker (full data)
    if (Marker == 0xFFFFFFFF)
    {
        const uint32 DataSize = DeltaSize - 4;
        OutRestoredData = FMemory::Malloc(DataSize);
        OutRestoredSize = DataSize;
        FMemory::Memcpy(OutRestoredData, DeltaBytes + 4, DataSize);
        return true;
    }
    
    // Normal delta format
    if (DeltaSize < 12) // Minimum header size
    {
        return false;
    }
    
    const uint32 Version = *reinterpret_cast<const uint32*>(DeltaBytes);
    if (Version != 1) // Only support version 1 for now
    {
        return false;
    }
    
    const uint32 OriginalSize = *reinterpret_cast<const uint32*>(DeltaBytes + 4);
    const uint32 EntryCount = *reinterpret_cast<const uint32*>(DeltaBytes + 8);
    
    // Allocate and copy original data first
    OutRestoredData = FMemory::Malloc(OriginalSize);
    OutRestoredSize = OriginalSize;
    FMemory::Memcpy(OutRestoredData, OriginalData, OriginalSize);
    
    // Apply deltas
    uint32 CurrentPosition = 12; // Start after header
    for (uint32 i = 0; i < EntryCount; ++i)
    {
        if (CurrentPosition + 8 > DeltaSize)
        {
            FMemory::Free(OutRestoredData);
            OutRestoredData = nullptr;
            OutRestoredSize = 0;
            return false;
        }
        
        const uint32 Offset = *reinterpret_cast<const uint32*>(DeltaBytes + CurrentPosition);
        CurrentPosition += 4;
        
        const uint32 Length = *reinterpret_cast<const uint32*>(DeltaBytes + CurrentPosition);
        CurrentPosition += 4;
        
        if (CurrentPosition + Length > DeltaSize || Offset + Length > OriginalSize)
        {
            FMemory::Free(OutRestoredData);
            OutRestoredData = nullptr;
            OutRestoredSize = 0;
            return false;
        }
        
        // Apply change
        FMemory::Memcpy(static_cast<uint8*>(OutRestoredData) + Offset, DeltaBytes + CurrentPosition, Length);
        CurrentPosition += Length;
    }
    
    return true;
}

ECompressionAlgorithm FCompressionUtility::AnalyzeDataForCompression(const void* Data, uint32 DataSize, ECompressionLevel Level)
{
    if (!Data || DataSize < 64)
    {
        return ECompressionAlgorithm::Auto;
    }
    
    const uint8* Bytes = static_cast<const uint8*>(Data);
    
    // Check if data might be suitable for RLE (simple homogeneity check)
    uint32 SameValueCount = 1;
    uint32 MaxRunLength = 0;
    uint8 LastValue = Bytes[0];
    
    // Sample the first 1024 bytes or the entire buffer if smaller
    const uint32 SampleSize = FMath::Min(DataSize, 1024u);
    for (uint32 i = 1; i < SampleSize; ++i)
    {
        if (Bytes[i] == LastValue)
        {
            SameValueCount++;
        }
        else
        {
            MaxRunLength = FMath::Max(MaxRunLength, SameValueCount);
            SameValueCount = 1;
            LastValue = Bytes[i];
        }
    }
    MaxRunLength = FMath::Max(MaxRunLength, SameValueCount);
    
    // Check if we have long runs of the same value, RLE might be efficient
    if (MaxRunLength > 16 || (static_cast<float>(MaxRunLength) / static_cast<float>(SampleSize) > 0.2f))
    {
        return ECompressionAlgorithm::RLE;
    }
    
    // Default algorithms based on compression level
    switch (Level)
    {
        case ECompressionLevel::Fast:
            return ECompressionAlgorithm::LZ4;
        case ECompressionLevel::Normal:
            return ECompressionAlgorithm::Zlib;
        case ECompressionLevel::High:
        case ECompressionLevel::Maximum:
            return ECompressionAlgorithm::Zstd;
        default:
            return ECompressionAlgorithm::Zlib;
    }
}

ECompressionAlgorithm FCompressionUtility::GetRecommendedAlgorithm(FName DataType, ECompressionLevel Level)
{
    // Special cases for known data types
    if (DataType == TEXT("SDF_Field"))
    {
        return (Level == ECompressionLevel::Fast) ? ECompressionAlgorithm::LZ4 : ECompressionAlgorithm::Zstd;
    }
    else if (DataType == TEXT("SVO_Node"))
    {
        return (Level == ECompressionLevel::Fast) ? ECompressionAlgorithm::LZ4 : ECompressionAlgorithm::Zlib;
    }
    else if (DataType == TEXT("Material_Channel"))
    {
        // Material channels often have patterns that delta encoding can exploit
        return ECompressionAlgorithm::Delta;
    }
    else if (DataType == TEXT("Mining_Delta"))
    {
        // Mining deltas are already in delta format, use a light compression
        return ECompressionAlgorithm::LZ4;
    }
    
    // Default based on compression level
    switch (Level)
    {
        case ECompressionLevel::Fast:
            return ECompressionAlgorithm::LZ4;
        case ECompressionLevel::Normal:
            return ECompressionAlgorithm::Zlib;
        case ECompressionLevel::High:
        case ECompressionLevel::Maximum:
            return ECompressionAlgorithm::Zstd;
        default:
            return ECompressionAlgorithm::Zlib;
    }
}

float FCompressionUtility::GetEstimatedCompressionRatio(ECompressionAlgorithm Algorithm, FName DataType)
{
    // Default estimate based on algorithm
    float BaseRatio = 1.0f;
    
    switch (Algorithm)
    {
        case ECompressionAlgorithm::LZ4:
            BaseRatio = 1.5f;
            break;
        case ECompressionAlgorithm::Zlib:
            BaseRatio = 2.0f;
            break;
        case ECompressionAlgorithm::Zstd:
            BaseRatio = 2.5f;
            break;
        case ECompressionAlgorithm::RLE:
            BaseRatio = 1.8f; // Highly dependent on data redundancy
            break;
        case ECompressionAlgorithm::Delta:
            BaseRatio = 3.0f; // Highly effective for sequential data
            break;
        default:
            BaseRatio = 1.5f;
            break;
    }
    
    // Adjust based on known data type characteristics
    if (DataType == TEXT("SDF_Field"))
    {
        if (Algorithm == ECompressionAlgorithm::Zstd)
        {
            return BaseRatio * 1.2f; // Zstd works well with SDF data
        }
        else if (Algorithm == ECompressionAlgorithm::RLE && DataType == TEXT("SDF_Homogeneous"))
        {
            return BaseRatio * 5.0f; // RLE excellent for homogeneous fields
        }
    }
    else if (DataType == TEXT("SVO_Node"))
    {
        if (Algorithm == ECompressionAlgorithm::Zlib)
        {
            return BaseRatio * 1.1f; // Zlib works well with SVO node data
        }
    }
    else if (DataType == TEXT("Material_Channel"))
    {
        if (Algorithm == ECompressionAlgorithm::Delta)
        {
            return BaseRatio * 1.5f; // Delta encoding excellent for material channels
        }
    }
    
    return BaseRatio;
}

// Implement the missing Compress function
bool FCompressionUtility::Compress(
    const void* Src, 
    uint32 SrcSize, 
    void*& OutDest, 
    uint32& OutDestSize,
    ECompressionAlgorithm Algorithm,
    ECompressionLevel Level)
{
    if (!Src || SrcSize == 0)
    {
        return false;
    }
    
    // Auto-detect the best algorithm if set to Auto
    if (Algorithm == ECompressionAlgorithm::Auto)
    {
        Algorithm = AnalyzeDataForCompression(Src, SrcSize, Level);
    }
    
    // Reserve enough memory for compressed data (worst case: slightly larger than source)
    const uint32 MaxCompressedSize = SrcSize + 64; // Small buffer for potential overhead
    uint8* CompressedBuffer = static_cast<uint8*>(FMemory::Malloc(MaxCompressedSize));
    
    if (!CompressedBuffer)
    {
        return false;
    }
    
    // Map our compression level to UE's compression flags
    ECompressionFlags CompressionFlags = (ECompressionFlags)0; // Default flags
    switch (Level)
    {
        case ECompressionLevel::Fast:
            CompressionFlags = COMPRESS_BiasSpeed;
            break;
        case ECompressionLevel::Normal:
            CompressionFlags = (ECompressionFlags)0;
            break;
        case ECompressionLevel::High:
        case ECompressionLevel::Maximum:
            CompressionFlags = COMPRESS_BiasMemory;
            break;
        default:
            CompressionFlags = (ECompressionFlags)0;
            break;
    }
    
    // Map our compression level to UE's compression level (1-9)
    int32 CompressionLevel = 6; // Default balanced level
    switch (Level)
    {
        case ECompressionLevel::Fast:    CompressionLevel = 1; break;
        case ECompressionLevel::Normal:  CompressionLevel = 6; break;
        case ECompressionLevel::High:    CompressionLevel = 8; break;
        case ECompressionLevel::Maximum: CompressionLevel = 9; break;
        default: CompressionLevel = 6; break;
    }
    
    bool bSuccess = false;
    int32 CompressedSize = MaxCompressedSize;
    
    // Handle different compression algorithms
    switch (Algorithm)
    {
        case ECompressionAlgorithm::LZ4:
        {
            // Use UE's compression API with LZ4
            bSuccess = FCompression::CompressMemory(
                NAME_LZ4,
                CompressedBuffer,
                CompressedSize,  // This is an output parameter
                Src,
                SrcSize,
                CompressionFlags,
                CompressionLevel);
            break;
        }
        
        case ECompressionAlgorithm::Zlib:
        {
            // Use Zlib for balanced compression
            bSuccess = FCompression::CompressMemory(
                NAME_Zlib,
                CompressedBuffer,
                CompressedSize,  // This is an output parameter
                Src,
                SrcSize,
                CompressionFlags,
                CompressionLevel);
            break;
        }
        
        case ECompressionAlgorithm::Zstd:
        {
            // In UE 5.5, use the standard Zlib as fallback
            bSuccess = FCompression::CompressMemory(
                NAME_Zlib,
                CompressedBuffer,
                CompressedSize,  // This is an output parameter
                Src,
                SrcSize,
                CompressionFlags,
                CompressionLevel);
            break;
        }
        
        case ECompressionAlgorithm::RLE:
        {
            // Use our own RLE implementation (defined in this file)
            uint32 TempCompressedSize = 0;
            void* TempBuffer = nullptr;
            bSuccess = CompressRLE(Src, SrcSize, TempBuffer, TempCompressedSize);
            
            if (bSuccess && TempBuffer)
            {
                // Copy to our pre-allocated buffer
                if (TempCompressedSize <= (uint32)MaxCompressedSize)
                {
                    FMemory::Memcpy(CompressedBuffer, TempBuffer, TempCompressedSize);
                    CompressedSize = TempCompressedSize;
                }
                else
                {
                    bSuccess = false;
                }
                
                // Free temporary buffer
                FMemory::Free(TempBuffer);
            }
            break;
        }
        
        case ECompressionAlgorithm::Delta:
        case ECompressionAlgorithm::Custom:
        default:
        {
            // Use UE's compression API with LZ4 as a default option
            bSuccess = FCompression::CompressMemory(
                NAME_LZ4,
                CompressedBuffer,
                CompressedSize,  // This is an output parameter
                Src,
                SrcSize,
                CompressionFlags,
                CompressionLevel);
            break;
        }
    }
    
    // Handle the result
    if (bSuccess && CompressedSize > 0)
    {
        // Allocate correctly sized output buffer
        OutDest = FMemory::Malloc(CompressedSize);
        if (!OutDest)
        {
            FMemory::Free(CompressedBuffer);
            return false;
        }
        
        // Copy data and set size
        FMemory::Memcpy(OutDest, CompressedBuffer, CompressedSize);
        OutDestSize = CompressedSize;
        
        // Free temp buffer
        FMemory::Free(CompressedBuffer);
        return true;
    }
    else
    {
        // Compression failed
        FMemory::Free(CompressedBuffer);
        OutDest = nullptr;
        OutDestSize = 0;
        return false;
    }
}

// Implement the missing Decompress function
bool FCompressionUtility::Decompress(
    const void* Src, 
    uint32 SrcSize, 
    void*& OutDest, 
    uint32& OutDestSize,
    ECompressionAlgorithm Algorithm)
{
    if (!Src || SrcSize == 0)
    {
        return false;
    }
    
    // Auto-detect the algorithm if not specified
    if (Algorithm == ECompressionAlgorithm::Auto)
    {
        Algorithm = DetectCompressionAlgorithm(Src, SrcSize);
    }
    
    bool bSuccess = false;
    
    // Handle different decompression algorithms
    switch (Algorithm)
    {
        case ECompressionAlgorithm::LZ4:
        {
            // Use UE's LZ4 decompression
            // We need to determine the uncompressed size
            // For simplicity, we'll estimate it as 4x the compressed size
            const int32 EstimatedSize = SrcSize * 4;
            OutDest = FMemory::Malloc(EstimatedSize);
            
            if (!OutDest)
            {
                return false;
            }
            
            int32 UncompressedSize = EstimatedSize;
            // Decompress using UE's API
            bSuccess = FCompression::UncompressMemory(
                NAME_LZ4,
                OutDest,
                UncompressedSize,
                Src,
                SrcSize,
                (ECompressionFlags)0);
                
            if (bSuccess && UncompressedSize > 0)
            {
                OutDestSize = UncompressedSize;
            }
            else
            {
                FMemory::Free(OutDest);
                OutDest = nullptr;
                OutDestSize = 0;
            }
            break;
        }
        
        case ECompressionAlgorithm::Zlib:
        {
            // Determine uncompressed size (often we would store this in a header)
            // For now, estimate as 4x the compressed size
            const int32 EstimatedSize = SrcSize * 4;
            OutDest = FMemory::Malloc(EstimatedSize);
            
            if (!OutDest)
            {
                return false;
            }
            
            int32 UncompressedSize = EstimatedSize;
            // Use Zlib decompression
            bSuccess = FCompression::UncompressMemory(
                NAME_Zlib,
                OutDest,
                UncompressedSize,
                Src,
                SrcSize,
                (ECompressionFlags)0);
                
            if (bSuccess && UncompressedSize > 0)
            {
                OutDestSize = UncompressedSize;
            }
            else
            {
                FMemory::Free(OutDest);
                OutDest = nullptr;
                OutDestSize = 0;
            }
            break;
        }
        
        case ECompressionAlgorithm::Zstd:
        {
            // Fallback to Zlib for Zstd in UE 5.5
            const int32 EstimatedSize = SrcSize * 4;
            OutDest = FMemory::Malloc(EstimatedSize);
            
            if (!OutDest)
            {
                return false;
            }
            
            int32 UncompressedSize = EstimatedSize;
            // Try standard Zlib decompression
            bSuccess = FCompression::UncompressMemory(
                NAME_Zlib,
                OutDest,
                UncompressedSize,
                Src,
                SrcSize,
                (ECompressionFlags)0);
                
            if (bSuccess && UncompressedSize > 0)
            {
                OutDestSize = UncompressedSize;
            }
            else
            {
                FMemory::Free(OutDest);
                OutDest = nullptr;
                OutDestSize = 0;
            }
            break;
        }
        
        case ECompressionAlgorithm::RLE:
        {
            // Use our custom RLE decompression
            bSuccess = DecompressRLE(Src, SrcSize, OutDest, OutDestSize);
            break;
        }
        
        case ECompressionAlgorithm::Delta:
        case ECompressionAlgorithm::Custom:
        default:
        {
            // Try LZ4 as a fallback
            const int32 EstimatedSize = SrcSize * 4;
            OutDest = FMemory::Malloc(EstimatedSize);
            
            if (!OutDest)
            {
                return false;
            }
            
            int32 UncompressedSize = EstimatedSize;
            // Decompress using UE's API
            bSuccess = FCompression::UncompressMemory(
                NAME_LZ4,
                OutDest,
                UncompressedSize,
                Src,
                SrcSize,
                (ECompressionFlags)0);
                
            if (bSuccess && UncompressedSize > 0)
            {
                OutDestSize = UncompressedSize;
            }
            else
            {
                FMemory::Free(OutDest);
                OutDest = nullptr;
                OutDestSize = 0;
            }
            break;
        }
    }
    
    return bSuccess && OutDest != nullptr && OutDestSize > 0;
}

/**
 * Detects which compression algorithm was used based on analyzing the compressed data
 * @param CompressedData Pointer to the compressed data
 * @param CompressedSize Size of the compressed data in bytes
 * @return Detected compression algorithm
 */
ECompressionAlgorithm FCompressionUtility::DetectCompressionAlgorithm(const void* CompressedData, uint32 CompressedSize)
{
    if (!CompressedData || CompressedSize < 4)
    {
        return ECompressionAlgorithm::Auto;
    }
    
    const uint8* Bytes = static_cast<const uint8*>(CompressedData);
    
    // Check for standard compression header signatures
    
    // Check for LZ4 signature (first byte is usually a version byte followed by token)
    if (CompressedSize > 4 && (Bytes[0] == 0x04 || Bytes[0] == 0x05 || Bytes[0] == 0x06))
    {
        // Simple heuristic for LZ4 - this is not foolproof but a reasonable guess
        return ECompressionAlgorithm::LZ4;
    }
    
    // Check for Zlib signature (first two bytes: 0x78 0x01 or 0x78 0x9C or 0x78 0xDA)
    if (CompressedSize > 2 && Bytes[0] == 0x78 && 
        (Bytes[1] == 0x01 || Bytes[1] == 0x9C || Bytes[1] == 0xDA))
    {
        return ECompressionAlgorithm::Zlib;
    }
    
    // Check for our custom RLE signature pattern
    // Our RLE format typically has frequent value/count pairs, so we can check for repeating patterns
    bool bMightBeRLE = true;
    for (uint32 i = 0; i < FMath::Min(CompressedSize, 20u); i += 2)
    {
        // In RLE, we shouldn't normally see two identical consecutive byte values
        // (since the second byte is supposed to be a count)
        if (i + 1 < CompressedSize && Bytes[i] == Bytes[i + 1])
        {
            bMightBeRLE = false;
            break;
        }
    }
    
    if (bMightBeRLE)
    {
        return ECompressionAlgorithm::RLE;
    }
    
    // If our engine-provided compression header has a marker at the beginning
    if (CompressedSize > 8 && Bytes[0] == 0x0U && Bytes[1] == 0x1U)
    {
        // Check additional bytes to determine which algorithm
        if (Bytes[2] == 0x4U) // Hypothetical marker for Zstd
        {
            return ECompressionAlgorithm::Zstd;
        }
    }
    
    // If our custom compressed data has algorithm info in the first byte
    if (CompressedSize > 8)
    {
        uint8 AlgorithmByte = Bytes[0];
        if (AlgorithmByte < 10) // Range check to make sure this is plausible
        {
            switch (AlgorithmByte)
            {
                case 1: return ECompressionAlgorithm::LZ4;
                case 2: return ECompressionAlgorithm::Zlib;
                case 3: return ECompressionAlgorithm::Zstd;
                case 4: return ECompressionAlgorithm::RLE;
                case 5: return ECompressionAlgorithm::Delta;
                default: break;
            }
        }
    }
    
    // Fallback based on data characteristics
    uint32 ZerosCount = 0;
    uint32 RepeatedByteCount = 0;
    uint8 LastByte = 0;
    
    // Sample part of the buffer to check for repeated patterns
    uint32 SampleSize = FMath::Min(CompressedSize, 64u);
    for (uint32 i = 0; i < SampleSize; ++i)
    {
        if (Bytes[i] == 0)
        {
            ZerosCount++;
        }
        
        if (i > 0 && Bytes[i] == LastByte)
        {
            RepeatedByteCount++;
        }
        
        LastByte = Bytes[i];
    }
    
    // If high ratio of zeros, likely Zlib which uses zeros for padding
    if (ZerosCount > SampleSize / 4)
    {
        return ECompressionAlgorithm::Zlib;
    }
    
    // If high ratio of repeated bytes, might be RLE
    if (RepeatedByteCount > SampleSize / 5)
    {
        return ECompressionAlgorithm::RLE;
    }
    
    // Default to LZ4 as most general purpose
    return ECompressionAlgorithm::LZ4;
}

bool FCompressionUtility::RegisterMaterialCompression(uint32 MaterialTypeId, const FMaterialCompressionSettings& Settings)
{
    // Store the compression settings for this material type
    MaterialCompressionSettings.Add(MaterialTypeId, Settings);
    
    // Log the registration
    UE_LOG(LogTemp, Log, TEXT("Registered compression settings for material %u (%s): Level=%d, AdaptivePrecision=%s, Lossless=%s"), 
        MaterialTypeId, 
        *Settings.MaterialName.ToString(), 
        static_cast<int32>(Settings.CompressionLevel),
        Settings.bEnableAdaptivePrecision ? TEXT("true") : TEXT("false"),
        Settings.bEnableLosslessMode ? TEXT("true") : TEXT("false"));
    
    return true;
}