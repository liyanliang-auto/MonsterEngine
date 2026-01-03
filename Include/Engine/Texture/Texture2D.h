// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file Texture2D.h
 * @brief 2D Texture class for engine-level texture management
 * 
 * FTexture2D wraps RHI texture resources and provides engine-level
 * functionality like streaming, LOD management, and resource tracking.
 * 
 * Reference:
 * - UE5: Engine/Source/Runtime/Engine/Classes/Engine/Texture2D.h
 * - Filament: Texture.h
 */

#include "Core/CoreTypes.h"
#include "Core/CoreMinimal.h"
#include "Containers/Name.h"
#include "RHI/RHIDefinitions.h"

// Forward declarations
namespace MonsterRender { namespace RHI {
    class IRHIDevice;
    class IRHITexture;
    class IRHISampler;
}}

namespace MonsterEngine
{

/**
 * @enum ETextureType
 * @brief Type of texture resource
 */
enum class ETextureType : uint8
{
    Unknown = 0,
    Texture2D,
    TextureCube,
    Texture3D,
    Texture2DArray
};

/**
 * @enum ETextureSourceFormat
 * @brief Source format of texture data
 */
enum class ETextureSourceFormat : uint8
{
    Unknown = 0,
    RGBA8,
    RGBA16F,
    RGBA32F,
    R8,
    RG8,
    BC1,    // DXT1
    BC3,    // DXT5
    BC5,    // Normal maps
    BC7     // High quality
};

/**
 * @struct FTexture2DDesc
 * @brief Description for creating a FTexture2D
 */
struct FTexture2DDesc
{
    uint32 Width = 1;
    uint32 Height = 1;
    uint32 MipLevels = 1;
    MonsterRender::RHI::EPixelFormat Format = MonsterRender::RHI::EPixelFormat::R8G8B8A8_UNORM;
    bool bSRGB = true;
    bool bGenerateMips = false;
    FName DebugName;
    
    FTexture2DDesc() = default;
    
    FTexture2DDesc(uint32 inWidth, uint32 inHeight, MonsterRender::RHI::EPixelFormat inFormat)
        : Width(inWidth)
        , Height(inHeight)
        , MipLevels(1)
        , Format(inFormat)
        , bSRGB(true)
        , bGenerateMips(false)
    {}
};

/**
 * @class FTexture2D
 * @brief Engine-level 2D texture class
 * 
 * Provides high-level texture management including:
 * - RHI texture resource wrapping
 * - Texture streaming support
 * - LOD management
 * - Default sampler management
 * 
 * Reference: UE5 UTexture2D
 */
class FTexture2D
{
public:
    FTexture2D();
    explicit FTexture2D(const FName& name);
    ~FTexture2D();
    
    // Non-copyable
    FTexture2D(const FTexture2D&) = delete;
    FTexture2D& operator=(const FTexture2D&) = delete;
    
    // ========================================================================
    // Initialization
    // ========================================================================
    
    /**
     * Initialize texture with description and device
     * @param device RHI device for resource creation
     * @param desc Texture description
     * @return true if initialization succeeded
     */
    bool initialize(MonsterRender::RHI::IRHIDevice* device, const FTexture2DDesc& desc);
    
    /**
     * Initialize from existing RHI texture
     * @param device RHI device
     * @param rhiTexture Existing RHI texture to wrap
     * @return true if initialization succeeded
     */
    bool initializeFromRHI(MonsterRender::RHI::IRHIDevice* device,
                           TSharedPtr<MonsterRender::RHI::IRHITexture> rhiTexture);
    
    /**
     * Release all resources
     */
    void shutdown();
    
    /**
     * Check if texture is valid and initialized
     */
    bool isValid() const { return m_rhiTexture != nullptr; }
    
    // ========================================================================
    // Resource Access
    // ========================================================================
    
    /**
     * Get the underlying RHI texture resource
     * @return RHI texture, nullptr if not initialized
     */
    TSharedPtr<MonsterRender::RHI::IRHITexture> getRHITexture() const { return m_rhiTexture; }
    
    /**
     * Get the default sampler for this texture
     * @return Default sampler, nullptr if not available
     */
    TSharedPtr<MonsterRender::RHI::IRHISampler> getDefaultSampler() const { return m_defaultSampler; }
    
    /**
     * Set custom sampler for this texture
     * @param sampler Sampler to use
     */
    void setDefaultSampler(TSharedPtr<MonsterRender::RHI::IRHISampler> sampler) { m_defaultSampler = sampler; }
    
    // ========================================================================
    // Properties
    // ========================================================================
    
    /** Get texture name */
    const FName& getName() const { return m_name; }
    
    /** Set texture name */
    void setName(const FName& name) { m_name = name; }
    
    /** Get texture width */
    uint32 getWidth() const { return m_width; }
    
    /** Get texture height */
    uint32 getHeight() const { return m_height; }
    
    /** Get number of mip levels */
    uint32 getMipLevels() const { return m_mipLevels; }
    
    /** Get pixel format */
    MonsterRender::RHI::EPixelFormat getFormat() const { return m_format; }
    
    /** Check if texture uses sRGB color space */
    bool isSRGB() const { return m_bSRGB; }
    
    // ========================================================================
    // Factory Methods
    // ========================================================================
    
    /**
     * Create a solid color texture
     * @param device RHI device
     * @param color RGBA color (0-255 per channel)
     * @param name Debug name
     * @return Created texture, nullptr on failure
     */
    static TSharedPtr<FTexture2D> createSolidColor(
        MonsterRender::RHI::IRHIDevice* device,
        uint8 r, uint8 g, uint8 b, uint8 a,
        const FName& name);
    
    /**
     * Create a white texture (1x1 white pixel)
     */
    static TSharedPtr<FTexture2D> createWhite(MonsterRender::RHI::IRHIDevice* device);
    
    /**
     * Create a black texture (1x1 black pixel)
     */
    static TSharedPtr<FTexture2D> createBlack(MonsterRender::RHI::IRHIDevice* device);
    
    /**
     * Create a default normal map (flat normal pointing up)
     */
    static TSharedPtr<FTexture2D> createDefaultNormal(MonsterRender::RHI::IRHIDevice* device);
    
    /**
     * Create a checkerboard texture for debugging
     * @param device RHI device
     * @param size Texture size (power of 2)
     * @param checkSize Size of each checker square
     * @return Created texture, nullptr on failure
     */
    static TSharedPtr<FTexture2D> createCheckerboard(
        MonsterRender::RHI::IRHIDevice* device,
        uint32 size = 256,
        uint32 checkSize = 32);
    
private:
    // Texture identification
    FName m_name;
    
    // Texture properties
    uint32 m_width = 0;
    uint32 m_height = 0;
    uint32 m_mipLevels = 1;
    MonsterRender::RHI::EPixelFormat m_format = MonsterRender::RHI::EPixelFormat::Unknown;
    bool m_bSRGB = true;
    
    // RHI resources
    MonsterRender::RHI::IRHIDevice* m_device = nullptr;
    TSharedPtr<MonsterRender::RHI::IRHITexture> m_rhiTexture;
    TSharedPtr<MonsterRender::RHI::IRHISampler> m_defaultSampler;
    
    /**
     * Create default sampler for this texture
     */
    bool _createDefaultSampler();
};

} // namespace MonsterEngine
