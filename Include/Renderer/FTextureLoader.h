// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Texture Loader (UE5-style)
//
// Reference: UE5 Engine/Source/Runtime/Engine/Classes/Engine/Texture.h

#pragma once

#include "Core/CoreMinimal.h"
#include "RHI/RHI.h"

namespace MonsterRender {

// Forward declarations
namespace RHI {
    class IRHIDevice;
    class IRHITexture;
    class IRHICommandList;
}

/**
 * FTextureLoadInfo - Texture loading parameters
 * 
 * Describes how to load and process texture data
 * Reference: UE5 FTextureSource
 */
struct FTextureLoadInfo {
    String FilePath;                    // File path to load from
    bool bGenerateMips;                 // Whether to generate mipmaps
    bool bSRGB;                         // Whether texture is in sRGB space
    bool bFlipVertical;                 // Whether to flip image vertically
    uint32 DesiredChannels;             // Desired channel count (0 = use source)
    
    FTextureLoadInfo()
        : bGenerateMips(true)
        , bSRGB(true)
        , bFlipVertical(true)
        , DesiredChannels(4)            // Default RGBA
    {}
};

/**
 * FTextureData - Raw texture pixel data
 * 
 * Contains loaded image data and metadata
 * Reference: UE5 FTexturePlatformData
 */
struct FTextureData {
    uint8* Pixels;                      // Raw pixel data (owned by this struct)
    uint32 Width;                       // Image width
    uint32 Height;                      // Image height
    uint32 Channels;                    // Number of channels (1-4)
    uint32 MipLevels;                   // Number of mipmap levels
    RHI::EPixelFormat Format;           // Pixel format
    TArray<uint8*> MipData;             // Mipmap level data pointers
    TArray<uint32> MipSizes;            // Mipmap level sizes in bytes
    
    FTextureData()
        : Pixels(nullptr)
        , Width(0)
        , Height(0)
        , Channels(0)
        , MipLevels(1)
        , Format(RHI::EPixelFormat::Unknown)
    {}
    
    ~FTextureData() {
        Release();
    }
    
    // Release all allocated memory
    void Release();
    
    // Calculate total size in bytes
    SIZE_T GetTotalSize() const;
    
    // Non-copyable
    FTextureData(const FTextureData&) = delete;
    FTextureData& operator=(const FTextureData&) = delete;
};

/**
 * FTextureLoader - Static texture loading utilities
 * 
 * Provides functions to load textures from disk using STB Image
 * Reference: UE5 IImageWrapper
 */
class FTextureLoader {
public:
    /**
     * Load texture from file and create RHI texture
     * 
     * @param Device RHI device for texture creation
     * @param LoadInfo Texture loading parameters
     * @return Created RHI texture, or nullptr on failure
     */
    static TSharedPtr<RHI::IRHITexture> LoadFromFile(
        RHI::IRHIDevice* Device,
        const FTextureLoadInfo& LoadInfo
    );
    
    /**
     * Load texture from file with command list for GPU upload
     * 
     * @param Device RHI device for texture creation
     * @param CommandList Command list for upload commands
     * @param LoadInfo Texture loading parameters
     * @return Created RHI texture, or nullptr on failure
     */
    static TSharedPtr<RHI::IRHITexture> LoadFromFileWithUpload(
        RHI::IRHIDevice* Device,
        RHI::IRHICommandList* CommandList,
        const FTextureLoadInfo& LoadInfo
    );
    
    /**
     * Load raw texture data from file
     * 
     * @param LoadInfo Texture loading parameters
     * @param OutData Output texture data
     * @return true if loading succeeded
     */
    static bool LoadTextureData(
        const FTextureLoadInfo& LoadInfo,
        FTextureData& OutData
    );
    
    /**
     * Generate mipmaps for texture data
     * 
     * @param SourceData Source texture data (must be RGBA)
     * @param OutData Output texture data with generated mipmaps
     * @return true if generation succeeded
     */
    static bool GenerateMipmaps(
        const FTextureData& SourceData,
        FTextureData& OutData
    );
    
    /**
     * Create RHI texture from texture data
     * 
     * @param Device RHI device for texture creation
     * @param TextureData Source texture data
     * @param bGenerateMips Whether to generate mipmaps
     * @return Created RHI texture, or nullptr on failure
     */
    static TSharedPtr<RHI::IRHITexture> CreateTexture(
        RHI::IRHIDevice* Device,
        const FTextureData& TextureData,
        bool bGenerateMips
    );
    
    /**
     * Upload texture data to GPU using command list
     * 
     * @param Device RHI device
     * @param CommandList Command list for upload
     * @param Texture Target RHI texture
     * @param TextureData Source texture data
     * @return true if upload succeeded
     */
    static bool UploadTextureData(
        RHI::IRHIDevice* Device,
        RHI::IRHICommandList* CommandList,
        TSharedPtr<RHI::IRHITexture> Texture,
        const FTextureData& TextureData
    );
    
    /**
     * Get supported image file extensions
     * 
     * @return Array of supported extensions (e.g., "jpg", "png", "bmp", "tga")
     */
    static TArray<String> GetSupportedExtensions();
    
    /**
     * Check if file extension is supported
     * 
     * @param Extension File extension (without dot)
     * @return true if extension is supported
     */
    static bool IsSupportedExtension(const String& Extension);
    
private:
    /**
     * Load image from file using STB Image
     * 
     * @param FilePath Path to image file
     * @param OutWidth Output image width
     * @param OutHeight Output image height
     * @param OutChannels Output channel count
     * @param DesiredChannels Desired channel count (0 = use source)
     * @param bFlipVertical Whether to flip image vertically
     * @return Loaded pixel data (must be freed with stbi_image_free)
     */
    static uint8* LoadImageFromFile(
        const String& FilePath,
        int32& OutWidth,
        int32& OutHeight,
        int32& OutChannels,
        int32 DesiredChannels,
        bool bFlipVertical
    );
    
    /**
     * Generate single mipmap level using box filter
     * 
     * @param SourceData Source level data (RGBA)
     * @param SourceWidth Source width
     * @param SourceHeight Source height
     * @param OutData Output mip level data (caller must allocate)
     */
    static void GenerateMipLevel(
        const uint8* SourceData,
        uint32 SourceWidth,
        uint32 SourceHeight,
        uint8* OutData
    );
    
    /**
     * Calculate number of mipmap levels for given dimensions
     * 
     * @param Width Texture width
     * @param Height Texture height
     * @return Number of mip levels
     */
    static uint32 CalculateMipLevels(uint32 Width, uint32 Height);
    
    /**
     * Convert channel count to pixel format
     * 
     * @param Channels Number of channels (1-4)
     * @param bSRGB Whether format is sRGB
     * @return Corresponding pixel format
     */
    static RHI::EPixelFormat GetPixelFormat(uint32 Channels, bool bSRGB);
};

} // namespace MonsterRender
