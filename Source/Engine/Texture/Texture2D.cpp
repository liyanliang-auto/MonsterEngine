// Copyright Monster Engine. All Rights Reserved.

/**
 * @file Texture2D.cpp
 * @brief Implementation of FTexture2D class
 */

#include "Engine/Texture/Texture2D.h"
#include "RHI/IRHIDevice.h"
#include "RHI/IRHIResource.h"
#include "RHI/RHIResources.h"
#include "Core/Log.h"
#include "Core/HAL/FMemory.h"
#include <cstring>

DEFINE_LOG_CATEGORY_STATIC(LogTexture2D, Log, All);

namespace MonsterEngine
{

using namespace MonsterRender::RHI;

// ============================================================================
// Constructor / Destructor
// ============================================================================

FTexture2D::FTexture2D()
    : m_name(TEXT("UnnamedTexture"))
    , m_width(0)
    , m_height(0)
    , m_mipLevels(1)
    , m_format(EPixelFormat::Unknown)
    , m_bSRGB(true)
    , m_device(nullptr)
{
}

FTexture2D::FTexture2D(const FName& name)
    : m_name(name)
    , m_width(0)
    , m_height(0)
    , m_mipLevels(1)
    , m_format(EPixelFormat::Unknown)
    , m_bSRGB(true)
    , m_device(nullptr)
{
}

FTexture2D::~FTexture2D()
{
    shutdown();
}

// ============================================================================
// Initialization
// ============================================================================

bool FTexture2D::initialize(IRHIDevice* device, const FTexture2DDesc& desc)
{
    if (!device) {
        MR_LOG(LogTexture2D, Error, "Invalid device for texture creation");
        return false;
    }
    
    m_device = device;
    m_width = desc.Width;
    m_height = desc.Height;
    m_mipLevels = desc.MipLevels;
    m_format = desc.Format;
    m_bSRGB = desc.bSRGB;
    m_name = desc.DebugName;
    
    // Create RHI texture
    TextureDesc rhiDesc;
    rhiDesc.width = desc.Width;
    rhiDesc.height = desc.Height;
    rhiDesc.depth = 1;
    rhiDesc.mipLevels = desc.MipLevels;
    rhiDesc.format = desc.Format;
    rhiDesc.usage = EResourceUsage::ShaderResource;
    rhiDesc.debugName = desc.DebugName.ToString().ToAnsiString();
    
    m_rhiTexture = device->createTexture(rhiDesc);
    if (!m_rhiTexture) {
        MR_LOG(LogTexture2D, Error, "Failed to create RHI texture: %s", *m_name.ToString());
        return false;
    }
    
    // Create default sampler
    if (!_createDefaultSampler()) {
        MR_LOG(LogTexture2D, Warning, "Failed to create default sampler for: %s", *m_name.ToString());
        // Continue without sampler - not fatal
    }
    
    MR_LOG(LogTexture2D, Verbose, "Created texture: %s (%ux%u)", *m_name.ToString(), m_width, m_height);
    return true;
}

bool FTexture2D::initializeFromRHI(IRHIDevice* device, TSharedPtr<IRHITexture> rhiTexture)
{
    if (!device || !rhiTexture) {
        MR_LOG(LogTexture2D, Error, "Invalid device or RHI texture");
        return false;
    }
    
    m_device = device;
    m_rhiTexture = rhiTexture;
    
    // Extract properties from RHI texture
    const TextureDesc& desc = rhiTexture->getDesc();
    m_width = desc.width;
    m_height = desc.height;
    m_mipLevels = desc.mipLevels;
    m_format = desc.format;
    m_name = FName(desc.debugName.c_str());
    
    // Create default sampler
    if (!_createDefaultSampler()) {
        MR_LOG(LogTexture2D, Warning, "Failed to create default sampler for: %s", *m_name.ToString());
    }
    
    return true;
}

void FTexture2D::shutdown()
{
    m_rhiTexture.reset();
    m_defaultSampler.reset();
    m_device = nullptr;
    m_width = 0;
    m_height = 0;
    m_mipLevels = 1;
    m_format = EPixelFormat::Unknown;
}

bool FTexture2D::_createDefaultSampler()
{
    if (!m_device) {
        return false;
    }
    
    SamplerDesc samplerDesc;
    samplerDesc.filter = ESamplerFilter::Trilinear;
    samplerDesc.addressU = ESamplerAddressMode::Wrap;
    samplerDesc.addressV = ESamplerAddressMode::Wrap;
    samplerDesc.addressW = ESamplerAddressMode::Wrap;
    samplerDesc.maxAnisotropy = 16;
    samplerDesc.debugName = (m_name.ToString() + TEXT("_Sampler")).ToAnsiString();
    
    m_defaultSampler = m_device->createSampler(samplerDesc);
    return m_defaultSampler != nullptr;
}

// ============================================================================
// Factory Methods
// ============================================================================

TSharedPtr<FTexture2D> FTexture2D::createSolidColor(
    IRHIDevice* device,
    uint8 r, uint8 g, uint8 b, uint8 a,
    const FName& name)
{
    if (!device) {
        MR_LOG(LogTexture2D, Error, "Invalid device for solid color texture");
        return nullptr;
    }
    
    auto texture = MakeShared<FTexture2D>(name);
    
    // Create 1x1 texture
    FTexture2DDesc desc;
    desc.Width = 1;
    desc.Height = 1;
    desc.MipLevels = 1;
    desc.Format = EPixelFormat::R8G8B8A8_UNORM;
    desc.bSRGB = true;
    desc.DebugName = name;
    
    // Create texture description for RHI
    TextureDesc rhiDesc;
    rhiDesc.width = 1;
    rhiDesc.height = 1;
    rhiDesc.depth = 1;
    rhiDesc.mipLevels = 1;
    rhiDesc.format = EPixelFormat::R8G8B8A8_UNORM;
    rhiDesc.usage = EResourceUsage::ShaderResource;
    rhiDesc.debugName = name.ToString().ToAnsiString();
    
    // Create texture with initial data
    uint8 pixelData[4] = { r, g, b, a };
    rhiDesc.initialData = pixelData;
    rhiDesc.initialDataSize = 4;
    
    texture->m_device = device;
    texture->m_width = 1;
    texture->m_height = 1;
    texture->m_mipLevels = 1;
    texture->m_format = EPixelFormat::R8G8B8A8_UNORM;
    texture->m_bSRGB = true;
    texture->m_name = name;
    
    texture->m_rhiTexture = device->createTexture(rhiDesc);
    if (!texture->m_rhiTexture) {
        MR_LOG(LogTexture2D, Error, "Failed to create solid color texture: %s", *name.ToString());
        return nullptr;
    }
    
    texture->_createDefaultSampler();
    
    MR_LOG(LogTexture2D, Verbose, "Created solid color texture: %s (RGBA: %u,%u,%u,%u)", 
           *name.ToString(), r, g, b, a);
    return texture;
}

TSharedPtr<FTexture2D> FTexture2D::createWhite(IRHIDevice* device)
{
    return createSolidColor(device, 255, 255, 255, 255, FName(TEXT("DefaultWhite")));
}

TSharedPtr<FTexture2D> FTexture2D::createBlack(IRHIDevice* device)
{
    return createSolidColor(device, 0, 0, 0, 255, FName(TEXT("DefaultBlack")));
}

TSharedPtr<FTexture2D> FTexture2D::createDefaultNormal(IRHIDevice* device)
{
    // Default normal map: (0.5, 0.5, 1.0) in tangent space = flat surface pointing up
    // In 8-bit: (128, 128, 255)
    return createSolidColor(device, 128, 128, 255, 255, FName(TEXT("DefaultNormal")));
}

TSharedPtr<FTexture2D> FTexture2D::createCheckerboard(
    IRHIDevice* device,
    uint32 size,
    uint32 checkSize)
{
    if (!device) {
        MR_LOG(LogTexture2D, Error, "Invalid device for checkerboard texture");
        return nullptr;
    }
    
    if (size == 0 || checkSize == 0) {
        MR_LOG(LogTexture2D, Error, "Invalid size for checkerboard texture");
        return nullptr;
    }
    
    auto texture = MakeShared<FTexture2D>(FName(TEXT("Checkerboard")));
    
    // Allocate pixel data
    uint32 dataSize = size * size * 4;
    uint8* pixelData = static_cast<uint8*>(MonsterRender::FMemory::Malloc(dataSize));
    if (!pixelData) {
        MR_LOG(LogTexture2D, Error, "Failed to allocate memory for checkerboard texture");
        return nullptr;
    }
    
    // Generate checkerboard pattern
    for (uint32 y = 0; y < size; ++y) {
        for (uint32 x = 0; x < size; ++x) {
            uint32 index = (y * size + x) * 4;
            bool isWhite = ((x / checkSize) + (y / checkSize)) % 2 == 0;
            uint8 color = isWhite ? 255 : 64;
            pixelData[index + 0] = color;     // R
            pixelData[index + 1] = color;     // G
            pixelData[index + 2] = color;     // B
            pixelData[index + 3] = 255;       // A
        }
    }
    
    // Create RHI texture
    TextureDesc rhiDesc;
    rhiDesc.width = size;
    rhiDesc.height = size;
    rhiDesc.depth = 1;
    rhiDesc.mipLevels = 1;
    rhiDesc.format = EPixelFormat::R8G8B8A8_UNORM;
    rhiDesc.usage = EResourceUsage::ShaderResource;
    rhiDesc.debugName = "Checkerboard";
    rhiDesc.initialData = pixelData;
    rhiDesc.initialDataSize = dataSize;
    
    texture->m_device = device;
    texture->m_width = size;
    texture->m_height = size;
    texture->m_mipLevels = 1;
    texture->m_format = EPixelFormat::R8G8B8A8_UNORM;
    texture->m_bSRGB = true;
    texture->m_name = FName(TEXT("Checkerboard"));
    
    texture->m_rhiTexture = device->createTexture(rhiDesc);
    
    // Free temporary pixel data
    MonsterRender::FMemory::Free(pixelData);
    
    if (!texture->m_rhiTexture) {
        MR_LOG(LogTexture2D, Error, "Failed to create checkerboard texture");
        return nullptr;
    }
    
    texture->_createDefaultSampler();
    
    MR_LOG(LogTexture2D, Verbose, "Created checkerboard texture: %ux%u", size, size);
    return texture;
}

} // namespace MonsterEngine
