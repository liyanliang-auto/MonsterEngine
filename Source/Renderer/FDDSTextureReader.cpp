// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - DDS Texture Reader Implementation

#include "Renderer/FDDSTextureReader.h"
#include "Core/HAL/FMemory.h"
#include "Core/Log.h"
#include "Containers/String.h"
#include <fstream>
#include <cstring>

namespace MonsterRender {

// DDS constants
static const uint32 DDS_MAGIC = 0x20534444; // "DDS "
static const uint32 DDS_FOURCC_DXT1 = 0x31545844; // "DXT1"
static const uint32 DDS_FOURCC_DXT3 = 0x33545844; // "DXT3"
static const uint32 DDS_FOURCC_DXT5 = 0x35545844; // "DXT5"
static const uint32 DDS_FOURCC_DX10 = 0x30315844; // "DX10"

// DDS header flags
static const uint32 DDSD_CAPS = 0x1;
static const uint32 DDSD_HEIGHT = 0x2;
static const uint32 DDSD_WIDTH = 0x4;
static const uint32 DDSD_PITCH = 0x8;
static const uint32 DDSD_PIXELFORMAT = 0x1000;
static const uint32 DDSD_MIPMAPCOUNT = 0x20000;
static const uint32 DDSD_LINEARSIZE = 0x80000;
static const uint32 DDSD_DEPTH = 0x800000;

// Pixel format flags
static const uint32 DDPF_ALPHAPIXELS = 0x1;
static const uint32 DDPF_ALPHA = 0x2;
static const uint32 DDPF_FOURCC = 0x4;
static const uint32 DDPF_RGB = 0x40;
static const uint32 DDPF_YUV = 0x200;
static const uint32 DDPF_LUMINANCE = 0x20000;

// DXGI formats
static const uint32 DXGI_FORMAT_BC1_UNORM = 71;
static const uint32 DXGI_FORMAT_BC3_UNORM = 77;
static const uint32 DXGI_FORMAT_BC7_UNORM = 98;
static const uint32 DXGI_FORMAT_R8G8B8A8_UNORM = 28;

FDDSTextureReader::FDDSTextureReader() {
}

FDDSTextureReader::~FDDSTextureReader() {
}

bool FDDSTextureReader::LoadFromFile(const FString& FilePath, FTextureFileData& OutData) {
    // Open file in binary mode (use wide character path)
    std::ifstream file(*FilePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        MR_LOG(LogTextureStreaming, Error, "Failed to open DDS file: %ls", *FilePath);
        return false;
    }

    // Get file size
    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    if (fileSize <= 0) {
        MR_LOG(LogTextureStreaming, Error, "Invalid DDS file size: %ls", *FilePath);
        return false;
    }

    // Read file data
    void* fileData = FMemory::Malloc(static_cast<SIZE_T>(fileSize));
    if (!fileData) {
        MR_LOG(LogTextureStreaming, Error, "Failed to allocate memory for DDS file");
        return false;
    }

    file.read(static_cast<char*>(fileData), fileSize);
    file.close();

    // Parse DDS
    bool success = LoadFromMemory(fileData, static_cast<SIZE_T>(fileSize), OutData);

    // Free file data
    FMemory::Free(fileData);

    return success;
}

bool FDDSTextureReader::LoadFromMemory(const void* Data, SIZE_T DataSize, FTextureFileData& OutData) {
    if (!Data || DataSize < 128) { // Minimum DDS file size (magic + header)
        MR_LOG(LogTextureStreaming, Error, "Invalid DDS data");
        return false;
    }

    return _parseDDS(Data, DataSize, OutData);
}

bool FDDSTextureReader::_parseDDS(const void* Data, SIZE_T DataSize, FTextureFileData& OutData) {
    const uint8* dataPtr = static_cast<const uint8*>(Data);
    
    // Check magic number
    uint32 magic;
    std::memcpy(&magic, dataPtr, sizeof(uint32));
    if (magic != DDS_MAGIC) {
        MR_LOG(LogTextureStreaming, Error, "Invalid DDS magic number");
        return false;
    }
    dataPtr += sizeof(uint32);

    // Read DDS header
    DDSHeader header;
    std::memcpy(&header, dataPtr, sizeof(DDSHeader));
    dataPtr += sizeof(DDSHeader);

    // Validate header
    if (header.Size != 124) {
        MR_LOG(LogTextureStreaming, Error, "Invalid DDS header size");
        return false;
    }

    // Check for DX10 extended header
    uint32 dxgiFormat = 0;
    if (header.PixelFormat.Flags & DDPF_FOURCC && header.PixelFormat.FourCC == DDS_FOURCC_DX10) {
        DDSHeaderDXT10 headerDXT10;
        std::memcpy(&headerDXT10, dataPtr, sizeof(DDSHeaderDXT10));
        dataPtr += sizeof(DDSHeaderDXT10);
        dxgiFormat = headerDXT10.DXGIFormat;
    }

    // Get pixel format
    ETexturePixelFormat pixelFormat = _getPixelFormat(header.PixelFormat, dxgiFormat);
    if (pixelFormat == ETexturePixelFormat::Unknown) {
        MR_LOG(LogTextureStreaming, Error, "Unsupported DDS pixel format");
        return false;
    }

    // Fill output data
    OutData.Width = header.Width;
    OutData.Height = header.Height;
    OutData.MipCount = (header.Flags & DDSD_MIPMAPCOUNT) ? header.MipMapCount : 1;
    OutData.PixelFormat = pixelFormat;
    OutData.FileFormat = ETextureFileFormat::DDS;

    // Extract mip data
    OutData.Mips.Empty();
    uint32 mipWidth = header.Width;
    uint32 mipHeight = header.Height;

    for (uint32 mipLevel = 0; mipLevel < OutData.MipCount; ++mipLevel) {
        // Calculate mip size
        SIZE_T mipSize = _calculateMipSize(mipWidth, mipHeight, pixelFormat);
        
        // Check if we have enough data
        SIZE_T offset = dataPtr - static_cast<const uint8*>(Data);
        if (offset + mipSize > DataSize) {
            MR_LOG(LogTextureStreaming, Error, "DDS file truncated at mip level %u", mipLevel);
            OutData.FreeData();
            return false;
        }

        // Allocate and copy mip data
        void* mipData = FMemory::Malloc(mipSize);
        if (!mipData) {
            MR_LOG(LogTextureStreaming, Error, "Failed to allocate memory for mip level %u", mipLevel);
            OutData.FreeData();
            return false;
        }
        std::memcpy(mipData, dataPtr, mipSize);
        dataPtr += mipSize;

        // Create mip data entry
        FTextureMipData mip;
        mip.Width = mipWidth;
        mip.Height = mipHeight;
        mip.DataSize = mipSize;
        mip.Data = mipData;
        OutData.Mips.Add(mip);

        // Calculate next mip dimensions
        mipWidth = (mipWidth > 1) ? (mipWidth / 2) : 1;
        mipHeight = (mipHeight > 1) ? (mipHeight / 2) : 1;
    }

    MR_LOG(LogTextureStreaming, Log, "Loaded DDS: %ux%u, %u mips", 
           header.Width, header.Height, OutData.MipCount);
    return true;
}

ETexturePixelFormat FDDSTextureReader::_getPixelFormat(const DDSPixelFormat& pixelFormat, uint32 dxgiFormat) {
    // Check DX10 format first
    if (dxgiFormat != 0) {
        switch (dxgiFormat) {
            case DXGI_FORMAT_BC1_UNORM:
                return ETexturePixelFormat::BC1_UNORM;
            case DXGI_FORMAT_BC3_UNORM:
                return ETexturePixelFormat::BC3_UNORM;
            case DXGI_FORMAT_BC7_UNORM:
                return ETexturePixelFormat::BC7_UNORM;
            case DXGI_FORMAT_R8G8B8A8_UNORM:
                return ETexturePixelFormat::R8G8B8A8_UNORM;
            default:
                return ETexturePixelFormat::Unknown;
        }
    }

    // Check FourCC
    if (pixelFormat.Flags & DDPF_FOURCC) {
        switch (pixelFormat.FourCC) {
            case DDS_FOURCC_DXT1:
                return ETexturePixelFormat::BC1_UNORM;
            case DDS_FOURCC_DXT5:
                return ETexturePixelFormat::BC3_UNORM;
            default:
                return ETexturePixelFormat::Unknown;
        }
    }

    // Check uncompressed RGB/RGBA
    if (pixelFormat.Flags & DDPF_RGB) {
        if (pixelFormat.RGBBitCount == 32 && (pixelFormat.Flags & DDPF_ALPHAPIXELS)) {
            return ETexturePixelFormat::R8G8B8A8_UNORM;
        }
        if (pixelFormat.RGBBitCount == 24) {
            return ETexturePixelFormat::R8G8B8_UNORM;
        }
    }

    return ETexturePixelFormat::Unknown;
}

SIZE_T FDDSTextureReader::_calculateMipSize(uint32 width, uint32 height, ETexturePixelFormat format) {
    switch (format) {
        case ETexturePixelFormat::R8G8B8A8_UNORM:
            return static_cast<SIZE_T>(width * height * 4);
        
        case ETexturePixelFormat::R8G8B8_UNORM:
            return static_cast<SIZE_T>(width * height * 3);
        
        case ETexturePixelFormat::BC1_UNORM:
            // DXT1: 4x4 blocks, 8 bytes per block
            return static_cast<SIZE_T>(((width + 3) / 4) * ((height + 3) / 4) * 8);
        
        case ETexturePixelFormat::BC3_UNORM:
        case ETexturePixelFormat::BC7_UNORM:
            // DXT5/BC7: 4x4 blocks, 16 bytes per block
            return static_cast<SIZE_T>(((width + 3) / 4) * ((height + 3) / 4) * 16);
        
        default:
            return 0;
    }
}

} // namespace MonsterRender
