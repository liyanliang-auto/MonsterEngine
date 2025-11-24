// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Texture Loader Implementation

#include "Renderer/FTextureLoader.h"
#include "Core/Log.h"
#include "Core/HAL/FMemoryManager.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanTexture.h"
#include "Platform/Vulkan/FVulkanMemoryManager.h"
#include "Renderer/FTextureStreamingManager.h"
#include <algorithm>
#include <cstring>

// STB Image library integration
#define STB_IMAGE_IMPLEMENTATION
#define STBI_MALLOC(sz)           MonsterRender::FMemoryManager::Malloc(sz, 16)
#define STBI_REALLOC(p,newsz)     MonsterRender::FMemoryManager::Realloc(p, newsz, 16)
#define STBI_FREE(p)              MonsterRender::FMemoryManager::Free(p)
#define STBI_ASSERT(x)            // Disable assert, use our own error handling
#define STB_IMAGE_STATIC
#include "../../3rd-party/stb/stb_image.h"

namespace MonsterRender {

// ============================================================================
// FTextureData Implementation
// ============================================================================

void FTextureData::Release() {
    if (Pixels) {
        // Free main pixel data using engine memory manager
        FMemoryManager::Free(Pixels);
        Pixels = nullptr;
    }
    
    // Free mipmap data
    for (uint8* MipPtr : MipData) {
        if (MipPtr && MipPtr != Pixels) {  // Don't double-free base level
            FMemoryManager::Free(MipPtr);
        }
    }
    
    MipData.clear();
    MipSizes.clear();
    Width = 0;
    Height = 0;
    Channels = 0;
    MipLevels = 1;
}

SIZE_T FTextureData::GetTotalSize() const {
    SIZE_T TotalSize = 0;
    for (uint32 Size : MipSizes) {
        TotalSize += Size;
    }
    return TotalSize;
}

// ============================================================================
// FTextureLoader Implementation
// ============================================================================

TSharedPtr<RHI::IRHITexture> FTextureLoader::LoadFromFile(
    RHI::IRHIDevice* Device,
    const FTextureLoadInfo& LoadInfo) {
    
    if (!Device) {
        MR_LOG_ERROR("FTextureLoader::LoadFromFile - Device is null");
        return nullptr;
    }
    
    MR_LOG_INFO("Loading texture from file: " + LoadInfo.FilePath);
    
    // Load texture data from file
    FTextureData TextureData;
    if (!LoadTextureData(LoadInfo, TextureData)) {
        MR_LOG_ERROR("Failed to load texture data from: " + LoadInfo.FilePath);
        return nullptr;
    }
    
    MR_LOG_INFO("Texture loaded successfully: " + 
                std::to_string(TextureData.Width) + "x" + 
                std::to_string(TextureData.Height) + " " +
                std::to_string(TextureData.Channels) + " channels, " +
                std::to_string(TextureData.MipLevels) + " mip levels");
    
    // Create RHI texture
    TSharedPtr<RHI::IRHITexture> Texture = CreateTexture(Device, TextureData, LoadInfo.bGenerateMips);
    if (!Texture) {
        MR_LOG_ERROR("Failed to create RHI texture");
        return nullptr;
    }
    
    MR_LOG_INFO("Texture created successfully");
    return Texture;
}

TSharedPtr<RHI::IRHITexture> FTextureLoader::LoadFromFileWithUpload(
    RHI::IRHIDevice* Device,
    RHI::IRHICommandList* CommandList,
    const FTextureLoadInfo& LoadInfo) {
    
    if (!Device || !CommandList) {
        MR_LOG_ERROR("FTextureLoader::LoadFromFileWithUpload - Invalid parameters");
        return nullptr;
    }
    
    // Load texture data
    FTextureData TextureData;
    if (!LoadTextureData(LoadInfo, TextureData)) {
        return nullptr;
    }
    
    // Create texture
    TSharedPtr<RHI::IRHITexture> Texture = CreateTexture(Device, TextureData, LoadInfo.bGenerateMips);
    if (!Texture) {
        return nullptr;
    }
    
    // Upload data to GPU
    if (!UploadTextureData(Device, CommandList, Texture, TextureData)) {
        MR_LOG_ERROR("Failed to upload texture data to GPU");
        return nullptr;
    }
    
    return Texture;
}

bool FTextureLoader::LoadTextureData(
    const FTextureLoadInfo& LoadInfo,
    FTextureData& OutData) {
    
    MR_LOG_INFO("Loading texture data from: " + LoadInfo.FilePath);
    
    // Set vertical flip flag for STB Image
    stbi_set_flip_vertically_on_load(LoadInfo.bFlipVertical ? 1 : 0);
    
    // Load image using STB Image
    int32 Width, Height, Channels;
    uint8* Pixels = LoadImageFromFile(
        LoadInfo.FilePath,
        Width,
        Height,
        Channels,
        LoadInfo.DesiredChannels,
        LoadInfo.bFlipVertical
    );
    
    if (!Pixels) {
        MR_LOG_ERROR("Failed to load image: " + LoadInfo.FilePath);
        MR_LOG_ERROR("STB Error: " + String(stbi_failure_reason()));
        return false;
    }
    
    // Fill texture data
    OutData.Pixels = Pixels;
    OutData.Width = static_cast<uint32>(Width);
    OutData.Height = static_cast<uint32>(Height);
    OutData.Channels = static_cast<uint32>(LoadInfo.DesiredChannels > 0 ? LoadInfo.DesiredChannels : Channels);
    OutData.Format = GetPixelFormat(OutData.Channels, LoadInfo.bSRGB);
    
    // Calculate base level size
    uint32 BaseSize = OutData.Width * OutData.Height * OutData.Channels;
    OutData.MipSizes.push_back(BaseSize);
    OutData.MipData.push_back(OutData.Pixels);
    
    // Generate mipmaps if requested
    if (LoadInfo.bGenerateMips) {
        MR_LOG_INFO("Generating mipmaps...");
        
        FTextureData MippedData;
        if (GenerateMipmaps(OutData, MippedData)) {
            // Replace data with mipped version
            OutData.Release();
            OutData.Pixels = MippedData.Pixels;
            OutData.MipLevels = MippedData.MipLevels;
            OutData.MipData = std::move(MippedData.MipData);
            OutData.MipSizes = std::move(MippedData.MipSizes);
            
            // Prevent double-free
            MippedData.Pixels = nullptr;
            
            MR_LOG_INFO("Generated " + std::to_string(OutData.MipLevels) + " mip levels");
        } else {
            MR_LOG_WARN("Failed to generate mipmaps, using base level only");
        }
    } else {
        OutData.MipLevels = 1;
    }
    
    MR_LOG_INFO("Texture data loaded: " + 
                std::to_string(OutData.Width) + "x" + 
                std::to_string(OutData.Height));
    
    return true;
}

bool FTextureLoader::GenerateMipmaps(
    const FTextureData& SourceData,
    FTextureData& OutData) {
    
    if (SourceData.Channels != 4) {
        MR_LOG_ERROR("Mipmap generation only supports RGBA (4 channel) textures");
        return false;
    }
    
    // Calculate number of mip levels
    uint32 MipLevels = CalculateMipLevels(SourceData.Width, SourceData.Height);
    
    // Calculate total size needed for all mips
    SIZE_T TotalSize = 0;
    uint32 MipWidth = SourceData.Width;
    uint32 MipHeight = SourceData.Height;
    
    TArray<uint32> MipSizes;
    MipSizes.reserve(MipLevels);
    
    for (uint32 MipLevel = 0; MipLevel < MipLevels; ++MipLevel) {
        uint32 MipSize = MipWidth * MipHeight * SourceData.Channels;
        MipSizes.push_back(MipSize);
        TotalSize += MipSize;
        
        MipWidth = std::max(1u, MipWidth / 2);
        MipHeight = std::max(1u, MipHeight / 2);
    }
    
    // Allocate memory for all mip levels
    uint8* AllMipsData = static_cast<uint8*>(FMemoryManager::Malloc(TotalSize, 16));
    if (!AllMipsData) {
        MR_LOG_ERROR("Failed to allocate memory for mipmaps");
        return false;
    }
    
    // Copy base level
    std::memcpy(AllMipsData, SourceData.Pixels, MipSizes[0]);
    
    // Generate each mip level
    uint8* CurrentMipPtr = AllMipsData;
    MipWidth = SourceData.Width;
    MipHeight = SourceData.Height;
    
    OutData.MipData.push_back(CurrentMipPtr);
    CurrentMipPtr += MipSizes[0];
    
    for (uint32 MipLevel = 1; MipLevel < MipLevels; ++MipLevel) {
        uint32 SourceMipWidth = std::max(1u, SourceData.Width >> (MipLevel - 1));
        uint32 SourceMipHeight = std::max(1u, SourceData.Height >> (MipLevel - 1));
        
        const uint8* SourceMipData = OutData.MipData[MipLevel - 1];
        
        // Generate mip level using box filter
        GenerateMipLevel(SourceMipData, SourceMipWidth, SourceMipHeight, CurrentMipPtr);
        
        OutData.MipData.push_back(CurrentMipPtr);
        CurrentMipPtr += MipSizes[MipLevel];
    }
    
    // Fill output data
    OutData.Pixels = AllMipsData;
    OutData.Width = SourceData.Width;
    OutData.Height = SourceData.Height;
    OutData.Channels = SourceData.Channels;
    OutData.Format = SourceData.Format;
    OutData.MipLevels = MipLevels;
    OutData.MipSizes = std::move(MipSizes);
    
    return true;
}

TSharedPtr<RHI::IRHITexture> FTextureLoader::CreateTexture(
    RHI::IRHIDevice* Device,
    const FTextureData& TextureData,
    bool bGenerateMips) {
    
    // Create texture descriptor
    RHI::TextureDesc Desc;
    Desc.width = TextureData.Width;
    Desc.height = TextureData.Height;
    Desc.depth = 1;
    Desc.mipLevels = TextureData.MipLevels;
    Desc.arraySize = 1;
    Desc.format = TextureData.Format;
    Desc.usage = RHI::EResourceUsage::ShaderResource;
    Desc.debugName = "Loaded Texture";
    
    // Create RHI texture
    TSharedPtr<RHI::IRHITexture> Texture = Device->createTexture(Desc);
    if (!Texture) {
        MR_LOG_ERROR("Failed to create RHI texture");
        return nullptr;
    }
    
    MR_LOG_INFO("RHI Texture created: " + 
                std::to_string(Desc.width) + "x" + std::to_string(Desc.height) +
                " with " + std::to_string(Desc.mipLevels) + " mip levels");
    
    return Texture;
}

bool FTextureLoader::UploadTextureData(
    RHI::IRHIDevice* Device,
    RHI::IRHICommandList* CommandList,
    TSharedPtr<RHI::IRHITexture> Texture,
    const FTextureData& TextureData) {
    
    // TODO: Implement GPU upload using staging buffer and command list
    // This requires:
    // 1. Create staging buffer
    // 2. Copy texture data to staging buffer
    // 3. Record copy command from staging buffer to texture
    // 4. Submit command list
    // 5. Wait for completion
    // 6. Clean up staging buffer
    
    MR_LOG_WARN("Texture GPU upload not yet implemented - using placeholder");
    return true;
}

TArray<String> FTextureLoader::GetSupportedExtensions() {
    return {
        "jpg", "jpeg",  // JPEG
        "png",          // PNG
        "bmp",          // BMP
        "tga",          // TGA
        "psd",          // PSD
        "gif",          // GIF (first frame only)
        "hdr",          // HDR
        "pic",          // PIC
        "pnm", "pgm", "ppm"  // PNM family
    };
}

bool FTextureLoader::IsSupportedExtension(const String& Extension) {
    TArray<String> Supported = GetSupportedExtensions();
    
    // Convert to lowercase for comparison
    String LowerExt = Extension;
    std::transform(LowerExt.begin(), LowerExt.end(), LowerExt.begin(), ::tolower);
    
    for (const String& SupportedExt : Supported) {
        if (LowerExt == SupportedExt) {
            return true;
        }
    }
    
    return false;
}

// ============================================================================
// Private Helper Methods
// ============================================================================

uint8* FTextureLoader::LoadImageFromFile(
    const String& FilePath,
    int32& OutWidth,
    int32& OutHeight,
    int32& OutChannels,
    int32 DesiredChannels,
    bool bFlipVertical) {
    
    // STB Image automatically uses our custom memory allocator (defined above)
    uint8* Data = stbi_load(
        FilePath.c_str(),
        &OutWidth,
        &OutHeight,
        &OutChannels,
        DesiredChannels
    );
    
    if (!Data) {
        MR_LOG_ERROR("STB Image failed to load: " + FilePath);
        return nullptr;
    }
    
    return Data;
}

void FTextureLoader::GenerateMipLevel(
    const uint8* SourceData,
    uint32 SourceWidth,
    uint32 SourceHeight,
    uint8* OutData) {
    
    // Box filter: average 2x2 pixels into 1 pixel
    uint32 DestWidth = std::max(1u, SourceWidth / 2);
    uint32 DestHeight = std::max(1u, SourceHeight / 2);
    
    constexpr uint32 Channels = 4;  // RGBA
    
    for (uint32 y = 0; y < DestHeight; ++y) {
        for (uint32 x = 0; x < DestWidth; ++x) {
            uint32 sx = x * 2;
            uint32 sy = y * 2;
            
            // Sample 4 source pixels
            const uint8* P00 = &SourceData[(sy * SourceWidth + sx) * Channels];
            const uint8* P10 = &SourceData[(sy * SourceWidth + std::min(sx + 1, SourceWidth - 1)) * Channels];
            const uint8* P01 = &SourceData[(std::min(sy + 1, SourceHeight - 1) * SourceWidth + sx) * Channels];
            const uint8* P11 = &SourceData[(std::min(sy + 1, SourceHeight - 1) * SourceWidth + std::min(sx + 1, SourceWidth - 1)) * Channels];
            
            // Average all channels
            uint8* Dest = &OutData[(y * DestWidth + x) * Channels];
            for (uint32 c = 0; c < Channels; ++c) {
                uint32 Sum = P00[c] + P10[c] + P01[c] + P11[c];
                Dest[c] = static_cast<uint8>(Sum / 4);
            }
        }
    }
}

uint32 FTextureLoader::CalculateMipLevels(uint32 Width, uint32 Height) {
    uint32 MaxDimension = std::max(Width, Height);
    uint32 MipLevels = 1;
    
    while (MaxDimension > 1) {
        MaxDimension /= 2;
        ++MipLevels;
    }
    
    return MipLevels;
}

RHI::EPixelFormat FTextureLoader::GetPixelFormat(uint32 Channels, bool bSRGB) {
    switch (Channels) {
        case 1:
            return bSRGB ? RHI::EPixelFormat::R8_SRGB : RHI::EPixelFormat::R8_UNORM;
        case 2:
            return bSRGB ? RHI::EPixelFormat::R8G8_SRGB : RHI::EPixelFormat::R8G8_UNORM;
        case 3:
            // Most GPUs don't support RGB8, so we'll use RGBA8 and waste alpha channel
            return bSRGB ? RHI::EPixelFormat::R8G8B8A8_SRGB : RHI::EPixelFormat::R8G8B8A8_UNORM;
        case 4:
            return bSRGB ? RHI::EPixelFormat::R8G8B8A8_SRGB : RHI::EPixelFormat::R8G8B8A8_UNORM;
        default:
            MR_LOG_ERROR("Unsupported channel count: " + std::to_string(Channels));
            return RHI::EPixelFormat::Unknown;
    }
}

} // namespace MonsterRender
