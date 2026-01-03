// Copyright Monster Engine. All Rights Reserved.

/**
 * @file PBRMaterial.cpp
 * @brief Implementation of FPBRMaterial class
 */

#include "Renderer/PBR/PBRMaterial.h"
#include "Renderer/PBR/PBRDescriptorSetLayouts.h"
#include "RHI/IRHIDevice.h"
#include "RHI/IRHIResource.h"
#include "RHI/IRHIDescriptorSet.h"
#include "RHI/RHIDefinitions.h"
#include "RHI/RHIResources.h"
#include "Core/Log.h"
#include <cmath>
#include <cstring>

DEFINE_LOG_CATEGORY_STATIC(LogPBRMaterial, Log, All);

namespace MonsterEngine
{
namespace Renderer
{

using namespace MonsterRender::RHI;

// ============================================================================
// Constructor / Destructor
// ============================================================================

FPBRMaterial::FPBRMaterial()
    : m_name(TEXT("DefaultPBRMaterial"))
    , m_params()
    , m_textures()
    , m_device(nullptr)
    , m_descriptorManager(nullptr)
    , m_initialized(false)
    , m_dirty(true)
{
}

FPBRMaterial::FPBRMaterial(const FName& name)
    : m_name(name)
    , m_params()
    , m_textures()
    , m_device(nullptr)
    , m_descriptorManager(nullptr)
    , m_initialized(false)
    , m_dirty(true)
{
}

FPBRMaterial::~FPBRMaterial()
{
    shutdown();
}

// ============================================================================
// Initialization
// ============================================================================

bool FPBRMaterial::initialize(IRHIDevice* device, FPBRDescriptorSetManager* descriptorManager)
{
    if (m_initialized) {
        MR_LOG(LogPBRMaterial, Warning, "Material already initialized: %s", *m_name.ToString());
        return true;
    }
    
    if (!device || !descriptorManager) {
        MR_LOG(LogPBRMaterial, Error, "Invalid device or descriptor manager");
        return false;
    }
    
    m_device = device;
    m_descriptorManager = descriptorManager;
    
    // Create material uniform buffer
    if (!_createMaterialBuffer()) {
        MR_LOG(LogPBRMaterial, Error, "Failed to create material buffer for: %s", *m_name.ToString());
        return false;
    }
    
    // Create descriptor set
    if (!_createDescriptorSet()) {
        MR_LOG(LogPBRMaterial, Error, "Failed to create descriptor set for: %s", *m_name.ToString());
        return false;
    }
    
    m_initialized = true;
    m_dirty = true;
    
    MR_LOG(LogPBRMaterial, Log, "Initialized PBR material: %s", *m_name.ToString());
    return true;
}

void FPBRMaterial::shutdown()
{
    m_descriptorSet.reset();
    m_materialBuffer.reset();
    m_device = nullptr;
    m_descriptorManager = nullptr;
    m_initialized = false;
    
    MR_LOG(LogPBRMaterial, Verbose, "Shutdown PBR material: %s", *m_name.ToString());
}

// ============================================================================
// Material Parameters
// ============================================================================

void FPBRMaterial::setBaseColor(const FVector4f& color)
{
    m_params.BaseColorFactor = color;
    m_dirty = true;
}

void FPBRMaterial::setBaseColor(const FVector3f& color)
{
    m_params.BaseColorFactor = FVector4f(color.X, color.Y, color.Z, 1.0f);
    m_dirty = true;
}

void FPBRMaterial::setMetallic(float metallic)
{
    m_params.MetallicFactor = (metallic < 0.0f) ? 0.0f : ((metallic > 1.0f) ? 1.0f : metallic);
    m_dirty = true;
}

void FPBRMaterial::setRoughness(float roughness)
{
    float minR = PBRConstants::MIN_PERCEPTUAL_ROUGHNESS;
    m_params.RoughnessFactor = (roughness < minR) ? minR : ((roughness > 1.0f) ? 1.0f : roughness);
    m_dirty = true;
}

void FPBRMaterial::setReflectance(float reflectance)
{
    m_params.Reflectance = (reflectance < 0.0f) ? 0.0f : ((reflectance > 1.0f) ? 1.0f : reflectance);
    m_dirty = true;
}

void FPBRMaterial::setAmbientOcclusion(float ao)
{
    m_params.AmbientOcclusion = (ao < 0.0f) ? 0.0f : ((ao > 1.0f) ? 1.0f : ao);
    m_dirty = true;
}

void FPBRMaterial::setEmissive(const FVector3f& color, float intensity)
{
    m_params.EmissiveFactor = color;
    m_params.EmissiveIntensity = (intensity > 0.0f) ? intensity : 0.0f;
    m_dirty = true;
}

void FPBRMaterial::setClearCoat(float intensity, float roughness)
{
    m_params.ClearCoat = (intensity < 0.0f) ? 0.0f : ((intensity > 1.0f) ? 1.0f : intensity);
    m_params.ClearCoatRoughness = (roughness < 0.0f) ? 0.0f : ((roughness > 1.0f) ? 1.0f : roughness);
    m_params.setHasClearCoat(intensity > 0.0f);
    m_dirty = true;
}

void FPBRMaterial::setAlphaCutoff(float cutoff)
{
    m_params.AlphaCutoff = (cutoff < 0.0f) ? 0.0f : ((cutoff > 1.0f) ? 1.0f : cutoff);
    m_dirty = true;
}

void FPBRMaterial::setDoubleSided(bool doubleSided)
{
    m_params.setDoubleSided(doubleSided);
    m_dirty = true;
}

// ============================================================================
// Textures
// ============================================================================

void FPBRMaterial::setBaseColorTexture(FTexture2D* texture)
{
    m_textures.BaseColorTexture = texture;
    m_params.setHasBaseColorTexture(texture != nullptr);
    m_dirty = true;
}

void FPBRMaterial::setMetallicRoughnessTexture(FTexture2D* texture)
{
    m_textures.MetallicRoughnessTexture = texture;
    m_params.setHasMetallicRoughnessTexture(texture != nullptr);
    m_dirty = true;
}

void FPBRMaterial::setNormalTexture(FTexture2D* texture)
{
    m_textures.NormalTexture = texture;
    m_params.setHasNormalTexture(texture != nullptr);
    m_dirty = true;
}

void FPBRMaterial::setOcclusionTexture(FTexture2D* texture)
{
    m_textures.OcclusionTexture = texture;
    m_params.setHasOcclusionTexture(texture != nullptr);
    m_dirty = true;
}

void FPBRMaterial::setEmissiveTexture(FTexture2D* texture)
{
    m_textures.EmissiveTexture = texture;
    m_params.setHasEmissiveTexture(texture != nullptr);
    m_dirty = true;
}

void FPBRMaterial::setClearCoatTexture(FTexture2D* texture)
{
    m_textures.ClearCoatTexture = texture;
    m_dirty = true;
}

// ============================================================================
// GPU Resources
// ============================================================================

void FPBRMaterial::updateGPUResources()
{
    if (!m_initialized || !m_dirty) {
        return;
    }
    
    // Update material uniform buffer
    if (m_materialBuffer) {
        void* mappedData = m_materialBuffer->map();
        if (mappedData) {
            memcpy(mappedData, &m_params, sizeof(FPBRMaterialParams));
            m_materialBuffer->unmap();
        }
    }
    
    // Update descriptor set with textures
    _updateDescriptorSet();
    
    m_dirty = false;
}

bool FPBRMaterial::_createMaterialBuffer()
{
    if (!m_device) {
        return false;
    }
    
    BufferDesc desc;
    desc.size = sizeof(FPBRMaterialParams);
    desc.usage = EResourceUsage::UniformBuffer;
    desc.memoryUsage = EMemoryUsage::Dynamic;
    desc.cpuAccessible = true;
    desc.debugName = "PBR_MaterialUBO";
    
    m_materialBuffer = m_device->createBuffer(desc);
    if (!m_materialBuffer) {
        MR_LOG(LogPBRMaterial, Error, "Failed to create material uniform buffer");
        return false;
    }
    
    return true;
}

bool FPBRMaterial::_createDescriptorSet()
{
    if (!m_device || !m_descriptorManager) {
        return false;
    }
    
    m_descriptorSet = m_descriptorManager->getPerMaterialDescriptorSet(m_params, m_textures);
    if (!m_descriptorSet) {
        MR_LOG(LogPBRMaterial, Error, "Failed to allocate material descriptor set");
        return false;
    }
    
    return true;
}

void FPBRMaterial::_updateDescriptorSet()
{
    if (!m_descriptorSet || !m_materialBuffer) {
        return;
    }
    
    // Update material UBO binding
    m_descriptorSet->updateUniformBuffer(
        static_cast<uint32>(EPBRPerMaterialBinding::MaterialUBO),
        m_materialBuffer);
    
    // TODO: Texture binding will be implemented when FTexture2D class is available
    // For now, materials work with uniform buffer parameters only
    // Texture support requires:
    // 1. FTexture2D class with getRHITexture() method
    // 2. Default fallback textures (white, normal, black)
    // 3. Sampler binding
    
    MR_LOG(LogPBRMaterial, VeryVerbose, "Updated material descriptor set (textures pending)");
}

// ============================================================================
// Factory Methods
// ============================================================================

TSharedPtr<FPBRMaterial> FPBRMaterial::createDefault(
    IRHIDevice* device,
    FPBRDescriptorSetManager* descriptorManager)
{
    auto material = MakeShared<FPBRMaterial>(FName(TEXT("DefaultPBR")));
    
    // Default: white, non-metallic, rough
    material->m_params.BaseColorFactor = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
    material->m_params.MetallicFactor = 0.0f;
    material->m_params.RoughnessFactor = 0.8f;
    material->m_params.Reflectance = 0.5f;
    
    if (!material->initialize(device, descriptorManager)) {
        MR_LOG(LogPBRMaterial, Error, "Failed to create default PBR material");
        return nullptr;
    }
    
    return material;
}

TSharedPtr<FPBRMaterial> FPBRMaterial::createMetallic(
    IRHIDevice* device,
    FPBRDescriptorSetManager* descriptorManager,
    const FVector3f& baseColor,
    float roughness)
{
    auto material = MakeShared<FPBRMaterial>(FName(TEXT("MetallicPBR")));
    
    material->m_params.BaseColorFactor = FVector4f(baseColor.X, baseColor.Y, baseColor.Z, 1.0f);
    material->m_params.MetallicFactor = 1.0f;
    material->m_params.RoughnessFactor = roughness;
    material->m_params.Reflectance = 0.5f;
    
    if (!material->initialize(device, descriptorManager)) {
        MR_LOG(LogPBRMaterial, Error, "Failed to create metallic PBR material");
        return nullptr;
    }
    
    return material;
}

TSharedPtr<FPBRMaterial> FPBRMaterial::createDielectric(
    IRHIDevice* device,
    FPBRDescriptorSetManager* descriptorManager,
    const FVector3f& baseColor,
    float roughness)
{
    auto material = MakeShared<FPBRMaterial>(FName(TEXT("DielectricPBR")));
    
    material->m_params.BaseColorFactor = FVector4f(baseColor.X, baseColor.Y, baseColor.Z, 1.0f);
    material->m_params.MetallicFactor = 0.0f;
    material->m_params.RoughnessFactor = roughness;
    material->m_params.Reflectance = 0.5f;
    
    if (!material->initialize(device, descriptorManager)) {
        MR_LOG(LogPBRMaterial, Error, "Failed to create dielectric PBR material");
        return nullptr;
    }
    
    return material;
}

} // namespace Renderer
} // namespace MonsterEngine
