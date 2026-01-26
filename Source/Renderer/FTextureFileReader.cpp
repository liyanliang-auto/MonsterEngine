// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Texture File Reader Implementation

#include "Renderer/FTextureFileReader.h"
#include "Renderer/FPNGTextureReader.h"
#include "Renderer/FDDSTextureReader.h"
#include "Core/HAL/FMemory.h"
#include "Core/Log.h"
#include "Containers/String.h"
#include <cstring>

namespace MonsterRender {

// ===== FTextureFileData Implementation =====

void FTextureFileData::FreeData() {
    for (auto& mip : Mips) {
        if (mip.Data) {
            FMemory::Free(mip.Data);
            mip.Data = nullptr;
        }
    }
    Mips.Empty();
}

// ===== FTextureFileReaderFactory Implementation =====

TUniquePtr<ITextureFileReader> FTextureFileReaderFactory::CreateReader(ETextureFileFormat Format) {
    switch (Format) {
        case ETextureFileFormat::PNG:
            return MakeUnique<FPNGTextureReader>();
        
        case ETextureFileFormat::DDS:
            return MakeUnique<FDDSTextureReader>();
        
        case ETextureFileFormat::KTX:
        case ETextureFileFormat::KTX2:
            // TODO: Implement KTX reader
            MR_LOG(LogTextureStreaming, Warning, "KTX format not yet implemented");
            return nullptr;
        
        default:
            MR_LOG(LogTextureStreaming, Error, "Unknown texture format");
            return nullptr;
    }
}

ETextureFileFormat FTextureFileReaderFactory::DetectFormat(const FString& FilePath) {
    // Get file extension
    const char* path = FilePath.c_str();
    const char* ext = std::strrchr(path, '.');
    
    if (!ext) {
        return ETextureFileFormat::Unknown;
    }
    
    // Convert to lowercase for comparison
    String extension(ext + 1);
    // Simple lowercase comparison using standard library
    for (char& c : extension) {
        if (c >= 'A' && c <= 'Z') {
            c = c - 'A' + 'a';
        }
    }
    
    // Check extension
    if (extension == "png") {
        return ETextureFileFormat::PNG;
    }
    else if (extension == "dds") {
        return ETextureFileFormat::DDS;
    }
    else if (extension == "ktx") {
        return ETextureFileFormat::KTX;
    }
    else if (extension == "ktx2") {
        return ETextureFileFormat::KTX2;
    }
    
    return ETextureFileFormat::Unknown;
}

ETextureFileFormat FTextureFileReaderFactory::DetectFormatFromHeader(const void* Data, SIZE_T DataSize) {
    if (!Data || DataSize < 16) {
        return ETextureFileFormat::Unknown;
    }
    
    const uint8* header = static_cast<const uint8*>(Data);
    
    // Check PNG signature (89 50 4E 47 0D 0A 1A 0A)
    static const uint8 PNG_SIGNATURE[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
    if (std::memcmp(header, PNG_SIGNATURE, 8) == 0) {
        return ETextureFileFormat::PNG;
    }
    
    // Check DDS signature ("DDS ")
    static const uint8 DDS_SIGNATURE[] = { 0x44, 0x44, 0x53, 0x20 };
    if (std::memcmp(header, DDS_SIGNATURE, 4) == 0) {
        return ETextureFileFormat::DDS;
    }
    
    // Check KTX signature
    static const uint8 KTX_SIGNATURE[] = { 0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB };
    if (std::memcmp(header, KTX_SIGNATURE, 8) == 0) {
        return ETextureFileFormat::KTX;
    }
    
    // Check KTX2 signature
    static const uint8 KTX2_SIGNATURE[] = { 0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB };
    if (std::memcmp(header, KTX2_SIGNATURE, 8) == 0) {
        return ETextureFileFormat::KTX2;
    }
    
    return ETextureFileFormat::Unknown;
}

bool FTextureFileReaderFactory::LoadTextureFromFile(const FString& FilePath, FTextureFileData& OutData) {
    // Detect format from extension
    ETextureFileFormat format = DetectFormat(FilePath);
    
    if (format == ETextureFileFormat::Unknown) {
        MR_LOG(LogTextureStreaming, Error, "Unknown texture file format: %ls", *FilePath);
        return false;
    }
    
    // Create reader
    auto reader = CreateReader(format);
    if (!reader) {
        MR_LOG(LogTextureStreaming, Error, "Failed to create reader for format");
        return false;
    }
    
    // Load texture
    return reader->LoadFromFile(FilePath, OutData);
}

} // namespace MonsterRender
