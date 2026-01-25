// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - DDS Texture Reader
//
// DirectDraw Surface format reader with mipmap support

#pragma once

#include "Renderer/FTextureFileReader.h"

namespace MonsterRender {

/**
 * FDDSTextureReader - DDS format texture reader
 * 
 * Supports:
 * - Uncompressed formats (RGBA, RGB)
 * - Compressed formats (DXT1/BC1, DXT5/BC3, BC7)
 * - Mipmap chains
 */
class FDDSTextureReader : public ITextureFileReader {
public:
    FDDSTextureReader();
    virtual ~FDDSTextureReader();

    // ITextureFileReader interface
    virtual bool LoadFromFile(const FString& FilePath, FTextureFileData& OutData) override;
    virtual bool LoadFromMemory(const void* Data, SIZE_T DataSize, FTextureFileData& OutData) override;
    virtual ETextureFileFormat GetFormat() const override { return ETextureFileFormat::DDS; }

private:
    // DDS file structures
    struct DDSPixelFormat {
        uint32 Size;
        uint32 Flags;
        uint32 FourCC;
        uint32 RGBBitCount;
        uint32 RBitMask;
        uint32 GBitMask;
        uint32 BBitMask;
        uint32 ABitMask;
    };

    struct DDSHeader {
        uint32 Size;
        uint32 Flags;
        uint32 Height;
        uint32 Width;
        uint32 PitchOrLinearSize;
        uint32 Depth;
        uint32 MipMapCount;
        uint32 Reserved1[11];
        DDSPixelFormat PixelFormat;
        uint32 Caps;
        uint32 Caps2;
        uint32 Caps3;
        uint32 Caps4;
        uint32 Reserved2;
    };

    struct DDSHeaderDXT10 {
        uint32 DXGIFormat;
        uint32 ResourceDimension;
        uint32 MiscFlag;
        uint32 ArraySize;
        uint32 MiscFlags2;
    };

    // Parse DDS header and extract mip data
    bool _parseDDS(const void* Data, SIZE_T DataSize, FTextureFileData& OutData);
    
    // Get pixel format from DDS header
    ETexturePixelFormat _getPixelFormat(const DDSPixelFormat& pixelFormat, uint32 dxgiFormat);
    
    // Calculate mip data size
    SIZE_T _calculateMipSize(uint32 width, uint32 height, ETexturePixelFormat format);
};

} // namespace MonsterRender
