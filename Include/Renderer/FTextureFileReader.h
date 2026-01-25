// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Texture File Reader
//
// Reference: UE5 Engine/Source/Runtime/ImageWrapper/Public/IImageWrapper.h

#pragma once

#include "Core/CoreTypes.h"
#include "Core/Templates/UniquePtr.h"
#include "Containers/Array.h"

namespace MonsterRender {

// Use MonsterEngine containers
using MonsterEngine::TArray;
using MonsterEngine::TUniquePtr;

// Forward declarations
class FString;

/**
 * Texture file format enumeration
 */
enum class ETextureFileFormat : uint8 {
    Unknown = 0,
    PNG,        // PNG format (compressed, no mips)
    DDS,        // DirectDraw Surface (can contain mips)
    KTX,        // Khronos Texture (can contain mips)
    KTX2        // Khronos Texture 2.0 (can contain mips)
};

/**
 * Texture pixel format enumeration
 */
enum class ETexturePixelFormat : uint8 {
    Unknown = 0,
    R8G8B8A8_UNORM,     // 32-bit RGBA
    R8G8B8_UNORM,       // 24-bit RGB
    BC1_UNORM,          // DXT1 compression
    BC3_UNORM,          // DXT5 compression
    BC7_UNORM,          // BC7 compression
    ETC2_R8G8B8_UNORM,  // ETC2 compression (mobile)
    ASTC_4x4_UNORM      // ASTC compression (mobile)
};

/**
 * Mip level data
 */
struct FTextureMipData {
    uint32 Width;           // Mip width
    uint32 Height;          // Mip height
    SIZE_T DataSize;        // Data size in bytes
    void* Data;             // Mip data (caller owns memory)

    FTextureMipData()
        : Width(0)
        , Height(0)
        , DataSize(0)
        , Data(nullptr)
    {}
};

/**
 * Texture file data
 */
struct FTextureFileData {
    uint32 Width;                       // Base width
    uint32 Height;                      // Base height
    uint32 MipCount;                    // Number of mip levels
    ETexturePixelFormat PixelFormat;    // Pixel format
    ETextureFileFormat FileFormat;      // File format
    TArray<FTextureMipData> Mips;       // Mip level data

    FTextureFileData()
        : Width(0)
        , Height(0)
        , MipCount(0)
        , PixelFormat(ETexturePixelFormat::Unknown)
        , FileFormat(ETextureFileFormat::Unknown)
    {}

    // Free all mip data
    void FreeData();
};

/**
 * ITextureFileReader - Abstract interface for texture file readers
 * 
 * Reference: UE5 IImageWrapper
 */
class ITextureFileReader {
public:
    virtual ~ITextureFileReader() = default;

    /**
     * Load texture from file
     * @param FilePath Absolute or relative file path
     * @param OutData Output texture data
     * @return true if successful
     */
    virtual bool LoadFromFile(const FString& FilePath, FTextureFileData& OutData) = 0;

    /**
     * Load texture from memory
     * @param Data File data in memory
     * @param DataSize Size of data
     * @param OutData Output texture data
     * @return true if successful
     */
    virtual bool LoadFromMemory(const void* Data, SIZE_T DataSize, FTextureFileData& OutData) = 0;

    /**
     * Get supported file format
     */
    virtual ETextureFileFormat GetFormat() const = 0;
};

/**
 * FTextureFileReaderFactory - Factory for creating texture file readers
 * 
 * Reference: UE5 FImageWrapperModule
 */
class FTextureFileReaderFactory {
public:
    /**
     * Create reader for file format
     * @param Format File format
     * @return Reader instance (nullptr if format not supported)
     */
    static TUniquePtr<ITextureFileReader> CreateReader(ETextureFileFormat Format);

    /**
     * Detect file format from file extension
     * @param FilePath File path
     * @return Detected format
     */
    static ETextureFileFormat DetectFormat(const FString& FilePath);

    /**
     * Detect file format from file header
     * @param Data File data
     * @param DataSize Data size (at least 16 bytes)
     * @return Detected format
     */
    static ETextureFileFormat DetectFormatFromHeader(const void* Data, SIZE_T DataSize);

    /**
     * Load texture from file (auto-detect format)
     * @param FilePath File path
     * @param OutData Output texture data
     * @return true if successful
     */
    static bool LoadTextureFromFile(const FString& FilePath, FTextureFileData& OutData);
};

} // namespace MonsterRender
