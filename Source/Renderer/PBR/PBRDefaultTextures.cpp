// Copyright Monster Engine. All Rights Reserved.

/**
 * @file PBRDefaultTextures.cpp
 * @brief Implementation of FPBRDefaultTextures class
 */

#include "Renderer/PBR/PBRDefaultTextures.h"
#include "Engine/Texture/Texture2D.h"
#include "RHI/IRHIDevice.h"
#include "RHI/IRHIResource.h"
#include "RHI/RHIResources.h"
#include "Core/Log.h"

DEFINE_LOG_CATEGORY_STATIC(LogPBRDefaultTextures, Log, All);

namespace MonsterEngine
{
namespace Renderer
{

using namespace MonsterRender::RHI;

// ============================================================================
// Singleton
// ============================================================================

FPBRDefaultTextures& FPBRDefaultTextures::get()
{
    static FPBRDefaultTextures instance;
    return instance;
}

FPBRDefaultTextures::FPBRDefaultTextures()
    : m_device(nullptr)
    , m_initialized(false)
{
}

FPBRDefaultTextures::~FPBRDefaultTextures()
{
    shutdown();
}

// ============================================================================
// Initialization
// ============================================================================

bool FPBRDefaultTextures::initialize(IRHIDevice* device)
{
    if (m_initialized) {
        MR_LOG(LogPBRDefaultTextures, Warning, "Default textures already initialized");
        return true;
    }
    
    if (!device) {
        MR_LOG(LogPBRDefaultTextures, Error, "Invalid device for default texture creation");
        return false;
    }
    
    m_device = device;
    
    // Create white texture (default base color)
    m_whiteTexture = FTexture2D::createWhite(device);
    if (!m_whiteTexture) {
        MR_LOG(LogPBRDefaultTextures, Error, "Failed to create white default texture");
        shutdown();
        return false;
    }
    
    // Create black texture (default emissive)
    m_blackTexture = FTexture2D::createBlack(device);
    if (!m_blackTexture) {
        MR_LOG(LogPBRDefaultTextures, Error, "Failed to create black default texture");
        shutdown();
        return false;
    }
    
    // Create normal texture (flat normal)
    m_normalTexture = FTexture2D::createDefaultNormal(device);
    if (!m_normalTexture) {
        MR_LOG(LogPBRDefaultTextures, Error, "Failed to create normal default texture");
        shutdown();
        return false;
    }
    
    // Create metallic-roughness texture
    if (!_createMetallicRoughnessTexture()) {
        MR_LOG(LogPBRDefaultTextures, Error, "Failed to create metallic-roughness default texture");
        shutdown();
        return false;
    }
    
    // Create occlusion texture (white = no occlusion)
    m_occlusionTexture = FTexture2D::createSolidColor(device, 255, 255, 255, 255, 
                                                       FName(TEXT("DefaultOcclusion")));
    if (!m_occlusionTexture) {
        MR_LOG(LogPBRDefaultTextures, Error, "Failed to create occlusion default texture");
        shutdown();
        return false;
    }
    
    // Create default sampler
    if (!_createDefaultSampler()) {
        MR_LOG(LogPBRDefaultTextures, Error, "Failed to create default sampler");
        shutdown();
        return false;
    }
    
    m_initialized = true;
    MR_LOG(LogPBRDefaultTextures, Log, "PBR default textures initialized successfully");
    return true;
}

void FPBRDefaultTextures::shutdown()
{
    m_whiteTexture.reset();
    m_blackTexture.reset();
    m_normalTexture.reset();
    m_metallicRoughnessTexture.reset();
    m_occlusionTexture.reset();
    m_defaultSampler.reset();
    m_device = nullptr;
    m_initialized = false;
    
    MR_LOG(LogPBRDefaultTextures, Verbose, "PBR default textures shutdown");
}

// ============================================================================
// RHI Resource Access
// ============================================================================

TSharedPtr<IRHITexture> FPBRDefaultTextures::getWhiteRHITexture() const
{
    return m_whiteTexture ? m_whiteTexture->getRHITexture() : nullptr;
}

TSharedPtr<IRHITexture> FPBRDefaultTextures::getBlackRHITexture() const
{
    return m_blackTexture ? m_blackTexture->getRHITexture() : nullptr;
}

TSharedPtr<IRHITexture> FPBRDefaultTextures::getNormalRHITexture() const
{
    return m_normalTexture ? m_normalTexture->getRHITexture() : nullptr;
}

TSharedPtr<IRHITexture> FPBRDefaultTextures::getMetallicRoughnessRHITexture() const
{
    return m_metallicRoughnessTexture ? m_metallicRoughnessTexture->getRHITexture() : nullptr;
}

TSharedPtr<IRHITexture> FPBRDefaultTextures::getOcclusionRHITexture() const
{
    return m_occlusionTexture ? m_occlusionTexture->getRHITexture() : nullptr;
}

// ============================================================================
// Private Methods
// ============================================================================

bool FPBRDefaultTextures::_createDefaultSampler()
{
    if (!m_device) {
        return false;
    }
    
    SamplerDesc desc;
    desc.filter = ESamplerFilter::Trilinear;
    desc.addressU = ESamplerAddressMode::Wrap;
    desc.addressV = ESamplerAddressMode::Wrap;
    desc.addressW = ESamplerAddressMode::Wrap;
    desc.maxAnisotropy = 16;
    desc.debugName = "PBR_DefaultSampler";
    
    m_defaultSampler = m_device->createSampler(desc);
    return m_defaultSampler != nullptr;
}

bool FPBRDefaultTextures::_createMetallicRoughnessTexture()
{
    if (!m_device) {
        return false;
    }
    
    // Metallic-Roughness texture format (glTF standard):
    // R: unused (or occlusion in some formats)
    // G: Roughness
    // B: Metallic
    // A: unused
    // Default: non-metallic (B=0), medium roughness (G=128 = 0.5)
    m_metallicRoughnessTexture = FTexture2D::createSolidColor(
        m_device, 
        0,      // R: unused
        128,    // G: roughness = 0.5
        0,      // B: metallic = 0
        255,    // A: unused
        FName(TEXT("DefaultMetallicRoughness")));
    
    return m_metallicRoughnessTexture != nullptr;
}

} // namespace Renderer
} // namespace MonsterEngine
