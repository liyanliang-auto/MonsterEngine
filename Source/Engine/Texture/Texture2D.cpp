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
#include "Renderer/FTextureFileReader.h"
#include "Containers/String.h"
#include <cstring>
#include <algorithm>

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
    
    // Clear streaming data
    m_bIsStreamable = false;
    m_filePath.clear();
    m_residentMips = 0;
    for (uint32 i = 0; i < 16; ++i) {
        m_mipSizes[i] = 0;
        m_mipDataPointers[i] = nullptr;
    }
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

// ============================================================================
// Texture Streaming Support
// ============================================================================

bool FTexture2D::initializeForStreaming(
    IRHIDevice* device,
    const MonsterEngine::String& filePath,
    uint32 initialMips)
{
    if (!device) {
        MR_LOG(LogTexture2D, Error, "Invalid device for streaming texture");
        return false;
    }
    
    if (filePath.empty()) {
        MR_LOG(LogTexture2D, Error, "Empty file path for streaming texture");
        return false;
    }
    
    m_device = device;
    m_filePath = filePath;
    m_bIsStreamable = true;
    
    // Load texture file metadata and initial mips
    if (!_loadInitialMips(filePath, initialMips)) {
        MR_LOG(LogTexture2D, Error, "Failed to load initial mips from: %s", filePath.c_str());
        m_bIsStreamable = false;
        return false;
    }
    
    // Create default sampler
    if (!_createDefaultSampler()) {
        MR_LOG(LogTexture2D, Warning, "Failed to create default sampler for: %s", filePath.c_str());
    }
    
    MR_LOG(LogTexture2D, Log, "Initialized streaming texture: %s (%ux%u, %u mips, %u resident)", 
           filePath.c_str(), m_width, m_height, m_mipLevels, m_residentMips);
    return true;
}

bool FTexture2D::_loadInitialMips(const MonsterEngine::String& filePath, uint32 numMips)
{
    using namespace MonsterRender;
    
    // Load texture file
    FTextureFileData fileData;
    if (!FTextureFileReaderFactory::LoadTextureFromFile(filePath.c_str(), fileData)) {
        MR_LOG(LogTexture2D, Error, "Failed to load texture file: %s", filePath.c_str());
        return false;
    }
    
    // Validate file data
    if (fileData.MipCount == 0 || fileData.Mips.Num() == 0) {
        MR_LOG(LogTexture2D, Error, "Texture file has no mip data: %s", filePath.c_str());
        fileData.FreeData();
        return false;
    }
    
    // Store texture properties
    m_width = fileData.Width;
    m_height = fileData.Height;
    m_mipLevels = fileData.MipCount;
    m_format = _convertPixelFormat(fileData.PixelFormat);
    
    // Store mip sizes
    for (uint32 i = 0; i < fileData.MipCount && i < 16; ++i) {
        m_mipSizes[i] = fileData.Mips[i].DataSize;
    }
    
    // Determine how many mips to load initially
    uint32 mipsToLoad = (numMips == 0) ? 1 : std::min(numMips, fileData.MipCount);
    m_residentMips = mipsToLoad;
    
    // Create RHI texture with initial mip data
    TextureDesc rhiDesc;
    rhiDesc.width = m_width;
    rhiDesc.height = m_height;
    rhiDesc.depth = 1;
    rhiDesc.mipLevels = m_mipLevels;
    rhiDesc.format = m_format;
    rhiDesc.usage = EResourceUsage::ShaderResource;
    rhiDesc.debugName = filePath.c_str();
    
    // Upload initial mip(s)
    if (mipsToLoad > 0 && fileData.Mips[0].Data) {
        rhiDesc.initialData = fileData.Mips[0].Data;
        rhiDesc.initialDataSize = fileData.Mips[0].DataSize;
    }
    
    m_rhiTexture = device->createTexture(rhiDesc);
    if (!m_rhiTexture) {
        MR_LOG(LogTexture2D, Error, "Failed to create RHI texture for streaming: %s", filePath.c_str());
        fileData.FreeData();
        return false;
    }
    
    // Upload additional initial mips if requested
    if (mipsToLoad > 1) {
        void** mipDataArray = static_cast<void**>(MonsterRender::FMemory::Malloc(sizeof(void*) * mipsToLoad));
        for (uint32 i = 0; i < mipsToLoad; ++i) {
            mipDataArray[i] = fileData.Mips[i].Data;
            m_mipDataPointers[i] = nullptr; // Will be managed by streaming system
        }
        
        uploadMipData(1, mipsToLoad, mipDataArray + 1);
        MonsterRender::FMemory::Free(mipDataArray);
    }
    
    // Free file data (we don't keep it in memory for streaming textures)
    fileData.FreeData();
    
    return true;
}

SIZE_T FTexture2D::getMipSize(uint32 mipLevel) const
{
    if (mipLevel >= 16) {
        return 0;
    }
    return m_mipSizes[mipLevel];
}

void* FTexture2D::getMipData(uint32 mipLevel) const
{
    if (mipLevel >= 16) {
        return nullptr;
    }
    return m_mipDataPointers[mipLevel];
}

bool FTexture2D::updateResidentMips(uint32 newResidentMips, void** mipData)
{
    if (!m_bIsStreamable) {
        MR_LOG(LogTexture2D, Warning, "Cannot update resident mips: texture is not streamable");
        return false;
    }
    
    if (newResidentMips > m_mipLevels) {
        MR_LOG(LogTexture2D, Error, "Invalid resident mip count: %u (max: %u)", newResidentMips, m_mipLevels);
        return false;
    }
    
    // Update mip data pointers
    for (uint32 i = 0; i < newResidentMips && i < 16; ++i) {
        if (mipData && mipData[i]) {
            m_mipDataPointers[i] = mipData[i];
        }
    }
    
    m_residentMips = newResidentMips;
    
    MR_LOG(LogTexture2D, Verbose, "Updated resident mips: %s (%u mips)", 
           m_filePath.c_str(), m_residentMips);
    return true;
}

bool FTexture2D::uploadMipData(uint32 startMip, uint32 endMip, void** mipData)
{
    if (!m_rhiTexture || !m_device) {
        MR_LOG(LogTexture2D, Error, "Cannot upload mip data: texture not initialized");
        return false;
    }
    
    if (!mipData) {
        MR_LOG(LogTexture2D, Error, "Cannot upload mip data: null data pointer");
        return false;
    }
    
    if (startMip >= endMip || endMip > m_mipLevels) {
        MR_LOG(LogTexture2D, Error, "Invalid mip range: %u-%u (total: %u)", startMip, endMip, m_mipLevels);
        return false;
    }
    
    // Upload each mip level to GPU via RHI
    bool allSucceeded = true;
    for (uint32 mipLevel = startMip; mipLevel < endMip; ++mipLevel) {
        if (!mipData[mipLevel - startMip]) {
            MR_LOG(LogTexture2D, Warning, "Null mip data at level %u", mipLevel);
            allSucceeded = false;
            continue;
        }
        
        // Calculate mip dimensions
        uint32 mipWidth = std::max(1u, m_width >> mipLevel);
        uint32 mipHeight = std::max(1u, m_height >> mipLevel);
        SIZE_T mipSize = m_mipSizes[mipLevel];
        
        if (mipSize == 0) {
            MR_LOG(LogTexture2D, Warning, "Mip level %u has zero size", mipLevel);
            allSucceeded = false;
            continue;
        }
        
        MR_LOG(LogTexture2D, VeryVerbose, "Uploading mip %u: %ux%u (%llu bytes)", 
               mipLevel, mipWidth, mipHeight, static_cast<uint64>(mipSize));
        
        // Call RHI updateTextureSubresource
        bool success = m_device->updateTextureSubresource(
            m_rhiTexture, 
            mipLevel, 
            mipData[mipLevel - startMip], 
            mipSize);
        
        if (!success) {
            MR_LOG(LogTexture2D, Error, "Failed to upload mip level %u", mipLevel);
            allSucceeded = false;
        }
    }
    
    if (allSucceeded) {
        MR_LOG(LogTexture2D, Verbose, "Successfully uploaded mips %u-%u for texture: %s", 
               startMip, endMip - 1, m_filePath.c_str());
    } else {
        MR_LOG(LogTexture2D, Warning, "Some mip levels failed to upload for texture: %s", 
               m_filePath.c_str());
    }
    
    return allSucceeded;
}

MonsterRender::RHI::EPixelFormat FTexture2D::_convertPixelFormat(MonsterRender::ETexturePixelFormat format)
{
    using namespace MonsterRender;
    
    switch (format) {
        case ETexturePixelFormat::R8G8B8A8_UNORM:
            return EPixelFormat::R8G8B8A8_UNORM;
        case ETexturePixelFormat::R8G8B8_UNORM:
            return EPixelFormat::R8G8B8A8_UNORM; // Convert RGB to RGBA
        case ETexturePixelFormat::BC1_UNORM:
            return EPixelFormat::BC1_UNORM;
        case ETexturePixelFormat::BC3_UNORM:
            return EPixelFormat::BC3_UNORM;
        case ETexturePixelFormat::BC7_UNORM:
            return EPixelFormat::BC7_UNORM;
        default:
            return EPixelFormat::R8G8B8A8_UNORM;
    }
}

} // namespace MonsterEngine
