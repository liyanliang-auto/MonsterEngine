// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - PNG Texture Reader
//
// Uses stb_image for PNG decoding

#pragma once

#include "Renderer/FTextureFileReader.h"

namespace MonsterRender {

/**
 * FPNGTextureReader - PNG format texture reader
 * 
 * Uses stb_image library for decoding
 * Note: PNG files typically don't contain mipmaps
 */
class FPNGTextureReader : public ITextureFileReader {
public:
    FPNGTextureReader();
    virtual ~FPNGTextureReader();

    // ITextureFileReader interface
    virtual bool LoadFromFile(const FString& FilePath, FTextureFileData& OutData) override;
    virtual bool LoadFromMemory(const void* Data, SIZE_T DataSize, FTextureFileData& OutData) override;
    virtual ETextureFileFormat GetFormat() const override { return ETextureFileFormat::PNG; }

private:
    // Decode PNG data using stb_image
    bool _decodePNG(const void* CompressedData, SIZE_T CompressedSize, FTextureFileData& OutData);
};

} // namespace MonsterRender
