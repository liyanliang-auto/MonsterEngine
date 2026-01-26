// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - PNG Texture Reader Implementation

#include "Renderer/FPNGTextureReader.h"
#include "Core/HAL/FMemory.h"
#include "Core/Log.h"
#include "Containers/String.h"
#include <fstream>

// stb_image implementation
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO  // We handle file IO ourselves
#define STBI_MALLOC(sz)           MonsterEngine::FMemory::Malloc(sz)
#define STBI_REALLOC(p,newsz)     MonsterEngine::FMemory::Realloc(p, newsz)
#define STBI_FREE(p)              MonsterEngine::FMemory::Free(p)
#include "stb/stb_image.h"

namespace MonsterRender {

FPNGTextureReader::FPNGTextureReader() {
}

FPNGTextureReader::~FPNGTextureReader() {
}

bool FPNGTextureReader::LoadFromFile(const FString& FilePath, FTextureFileData& OutData) {
    // Open file in binary mode (use wide character path)
    std::ifstream file(*FilePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        MR_LOG(LogTextureStreaming, Error, "Failed to open PNG file: %ls", *FilePath);
        return false;
    }

    // Get file size
    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    if (fileSize <= 0) {
        MR_LOG(LogTextureStreaming, Error, "Invalid PNG file size: %ls", *FilePath);
        return false;
    }

    // Read file data
    void* fileData = FMemory::Malloc(static_cast<SIZE_T>(fileSize));
    if (!fileData) {
        MR_LOG(LogTextureStreaming, Error, "Failed to allocate memory for PNG file");
        return false;
    }

    file.read(static_cast<char*>(fileData), fileSize);
    file.close();

    // Decode PNG
    bool success = LoadFromMemory(fileData, static_cast<SIZE_T>(fileSize), OutData);

    // Free file data
    FMemory::Free(fileData);

    return success;
}

bool FPNGTextureReader::LoadFromMemory(const void* Data, SIZE_T DataSize, FTextureFileData& OutData) {
    if (!Data || DataSize == 0) {
        MR_LOG(LogTextureStreaming, Error, "Invalid PNG data");
        return false;
    }

    return _decodePNG(Data, DataSize, OutData);
}

bool FPNGTextureReader::_decodePNG(const void* CompressedData, SIZE_T CompressedSize, FTextureFileData& OutData) {
    // Decode PNG using stb_image
    int32 width = 0;
    int32 height = 0;
    int32 channels = 0;

    // Request 4 channels (RGBA)
    stbi_uc* pixels = stbi_load_from_memory(
        static_cast<const stbi_uc*>(CompressedData),
        static_cast<int>(CompressedSize),
        &width,
        &height,
        &channels,
        4  // Force RGBA
    );

    if (!pixels) {
        const char* error = stbi_failure_reason();
        MR_LOG(LogTextureStreaming, Error, "Failed to decode PNG: %s", error ? error : "unknown error");
        return false;
    }

    // Fill output data
    OutData.Width = static_cast<uint32>(width);
    OutData.Height = static_cast<uint32>(height);
    OutData.MipCount = 1;  // PNG doesn't contain mipmaps
    OutData.PixelFormat = ETexturePixelFormat::R8G8B8A8_UNORM;
    OutData.FileFormat = ETextureFileFormat::PNG;

    // Create mip data
    FTextureMipData mipData;
    mipData.Width = OutData.Width;
    mipData.Height = OutData.Height;
    mipData.DataSize = static_cast<SIZE_T>(width * height * 4);  // RGBA
    mipData.Data = pixels;  // Caller takes ownership

    OutData.Mips.Empty();
    OutData.Mips.Add(mipData);

    MR_LOG(LogTextureStreaming, Log, "Decoded PNG: %ux%u, %d channels", width, height, channels);
    return true;
}

} // namespace MonsterRender
