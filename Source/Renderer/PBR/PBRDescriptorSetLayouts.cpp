// Copyright Monster Engine. All Rights Reserved.

/**
 * @file PBRDescriptorSetLayouts.cpp
 * @brief Implementation of PBR descriptor set layouts and manager
 */

#include "Renderer/PBR/PBRDescriptorSetLayouts.h"
#include "Renderer/PBR/PBRUniformBuffers.h"
#include "RHI/IRHIDevice.h"
#include "RHI/IRHIDescriptorSet.h"
#include "RHI/RHIDefinitions.h"
#include "RHI/RHIResources.h"
#include "Core/Log.h"

DEFINE_LOG_CATEGORY_STATIC(LogPBRDescriptor, Log, All);

namespace MonsterEngine
{
namespace Renderer
{

using namespace MonsterRender::RHI;

// ============================================================================
// FPBRDescriptorSetLayoutFactory Implementation
// ============================================================================

TSharedPtr<IRHIDescriptorSetLayout> FPBRDescriptorSetLayoutFactory::createPerFrameLayout(
    IRHIDevice* device)
{
    if (!device) {
        MR_LOG(LogPBRDescriptor, Error, "createPerFrameLayout: Invalid device");
        return nullptr;
    }
    
    FDescriptorSetLayoutDesc desc;
    desc.setIndex = static_cast<uint32>(EPBRDescriptorSet::PerFrame);
    desc.debugName = "PBR_PerFrame_Layout";
    
    // Binding 0: View uniform buffer
    FDescriptorSetLayoutBinding viewBinding;
    viewBinding.binding = static_cast<uint32>(EPBRPerFrameBinding::ViewUBO);
    viewBinding.descriptorType = EDescriptorType::UniformBuffer;
    viewBinding.descriptorCount = 1;
    viewBinding.shaderStages = EShaderStage::Vertex | EShaderStage::Fragment;
    desc.bindings.push_back(viewBinding);
    
    // Binding 1: Light uniform buffer
    FDescriptorSetLayoutBinding lightBinding;
    lightBinding.binding = static_cast<uint32>(EPBRPerFrameBinding::LightUBO);
    lightBinding.descriptorType = EDescriptorType::UniformBuffer;
    lightBinding.descriptorCount = 1;
    lightBinding.shaderStages = EShaderStage::Fragment;
    desc.bindings.push_back(lightBinding);
    
    auto layout = device->createDescriptorSetLayout(desc);
    if (!layout) {
        MR_LOG(LogPBRDescriptor, Error, "Failed to create Per-Frame descriptor set layout");
        return nullptr;
    }
    
    MR_LOG(LogPBRDescriptor, Log, "Created Per-Frame descriptor set layout (Set 0)");
    return layout;
}

TSharedPtr<IRHIDescriptorSetLayout> FPBRDescriptorSetLayoutFactory::createPerMaterialLayout(
    IRHIDevice* device)
{
    if (!device) {
        MR_LOG(LogPBRDescriptor, Error, "createPerMaterialLayout: Invalid device");
        return nullptr;
    }
    
    FDescriptorSetLayoutDesc desc;
    desc.setIndex = static_cast<uint32>(EPBRDescriptorSet::PerMaterial);
    desc.debugName = "PBR_PerMaterial_Layout";
    
    // Binding 0: Material parameters uniform buffer
    FDescriptorSetLayoutBinding materialBinding;
    materialBinding.binding = static_cast<uint32>(EPBRPerMaterialBinding::MaterialUBO);
    materialBinding.descriptorType = EDescriptorType::UniformBuffer;
    materialBinding.descriptorCount = 1;
    materialBinding.shaderStages = EShaderStage::Fragment;
    desc.bindings.push_back(materialBinding);
    
    // Binding 1-5: PBR textures (combined image samplers)
    const char* textureNames[] = {
        "BaseColor",
        "MetallicRoughness",
        "Normal",
        "Occlusion",
        "Emissive"
    };
    
    for (uint32 i = 0; i < 5; ++i) {
        FDescriptorSetLayoutBinding texBinding;
        texBinding.binding = static_cast<uint32>(EPBRPerMaterialBinding::BaseColorTexture) + i;
        texBinding.descriptorType = EDescriptorType::CombinedTextureSampler;
        texBinding.descriptorCount = 1;
        texBinding.shaderStages = EShaderStage::Fragment;
        desc.bindings.push_back(texBinding);
    }
    
    auto layout = device->createDescriptorSetLayout(desc);
    if (!layout) {
        MR_LOG(LogPBRDescriptor, Error, "Failed to create Per-Material descriptor set layout");
        return nullptr;
    }
    
    MR_LOG(LogPBRDescriptor, Log, "Created Per-Material descriptor set layout (Set 1) with %u bindings",
           static_cast<uint32>(desc.bindings.size()));
    return layout;
}

TSharedPtr<IRHIDescriptorSetLayout> FPBRDescriptorSetLayoutFactory::createPerObjectLayout(
    IRHIDevice* device)
{
    if (!device) {
        MR_LOG(LogPBRDescriptor, Error, "createPerObjectLayout: Invalid device");
        return nullptr;
    }
    
    FDescriptorSetLayoutDesc desc;
    desc.setIndex = static_cast<uint32>(EPBRDescriptorSet::PerObject);
    desc.debugName = "PBR_PerObject_Layout";
    
    // Binding 0: Object transform uniform buffer
    FDescriptorSetLayoutBinding objectBinding;
    objectBinding.binding = static_cast<uint32>(EPBRPerObjectBinding::ObjectUBO);
    objectBinding.descriptorType = EDescriptorType::UniformBuffer;
    objectBinding.descriptorCount = 1;
    objectBinding.shaderStages = EShaderStage::Vertex;
    desc.bindings.push_back(objectBinding);
    
    auto layout = device->createDescriptorSetLayout(desc);
    if (!layout) {
        MR_LOG(LogPBRDescriptor, Error, "Failed to create Per-Object descriptor set layout");
        return nullptr;
    }
    
    MR_LOG(LogPBRDescriptor, Log, "Created Per-Object descriptor set layout (Set 2)");
    return layout;
}

TSharedPtr<IRHIPipelineLayout> FPBRDescriptorSetLayoutFactory::createPBRPipelineLayout(
    IRHIDevice* device,
    TSharedPtr<IRHIDescriptorSetLayout> perFrameLayout,
    TSharedPtr<IRHIDescriptorSetLayout> perMaterialLayout,
    TSharedPtr<IRHIDescriptorSetLayout> perObjectLayout)
{
    if (!device || !perFrameLayout || !perMaterialLayout || !perObjectLayout) {
        MR_LOG(LogPBRDescriptor, Error, "createPBRPipelineLayout: Invalid parameters");
        return nullptr;
    }
    
    FPipelineLayoutDesc desc;
    desc.debugName = "PBR_PipelineLayout";
    
    // Add descriptor set layouts in order
    desc.setLayouts.push_back(perFrameLayout);
    desc.setLayouts.push_back(perMaterialLayout);
    desc.setLayouts.push_back(perObjectLayout);
    
    // No push constants for now
    // Future: Add push constants for per-draw data
    
    auto layout = device->createPipelineLayout(desc);
    if (!layout) {
        MR_LOG(LogPBRDescriptor, Error, "Failed to create PBR pipeline layout");
        return nullptr;
    }
    
    MR_LOG(LogPBRDescriptor, Log, "Created PBR pipeline layout with 3 descriptor sets");
    return layout;
}

// ============================================================================
// FPBRDescriptorSetManager Implementation
// ============================================================================

FPBRDescriptorSetManager::FPBRDescriptorSetManager()
    : m_device(nullptr)
    , m_currentFrame(0)
{
}

FPBRDescriptorSetManager::~FPBRDescriptorSetManager()
{
    shutdown();
}

bool FPBRDescriptorSetManager::initialize(IRHIDevice* device)
{
    if (!device) {
        MR_LOG(LogPBRDescriptor, Error, "FPBRDescriptorSetManager::initialize: Invalid device");
        return false;
    }
    
    m_device = device;
    
    // Create descriptor set layouts
    m_perFrameLayout = FPBRDescriptorSetLayoutFactory::createPerFrameLayout(device);
    if (!m_perFrameLayout) {
        MR_LOG(LogPBRDescriptor, Error, "Failed to create Per-Frame layout");
        return false;
    }
    
    m_perMaterialLayout = FPBRDescriptorSetLayoutFactory::createPerMaterialLayout(device);
    if (!m_perMaterialLayout) {
        MR_LOG(LogPBRDescriptor, Error, "Failed to create Per-Material layout");
        return false;
    }
    
    m_perObjectLayout = FPBRDescriptorSetLayoutFactory::createPerObjectLayout(device);
    if (!m_perObjectLayout) {
        MR_LOG(LogPBRDescriptor, Error, "Failed to create Per-Object layout");
        return false;
    }
    
    // Create pipeline layout
    m_pipelineLayout = FPBRDescriptorSetLayoutFactory::createPBRPipelineLayout(
        device, m_perFrameLayout, m_perMaterialLayout, m_perObjectLayout);
    if (!m_pipelineLayout) {
        MR_LOG(LogPBRDescriptor, Error, "Failed to create pipeline layout");
        return false;
    }
    
    // Create default sampler
    if (!_createDefaultSampler()) {
        MR_LOG(LogPBRDescriptor, Error, "Failed to create default sampler");
        return false;
    }
    
    MR_LOG(LogPBRDescriptor, Log, "FPBRDescriptorSetManager initialized successfully");
    return true;
}

void FPBRDescriptorSetManager::shutdown()
{
    m_defaultSampler.reset();
    m_pipelineLayout.reset();
    m_perObjectLayout.reset();
    m_perMaterialLayout.reset();
    m_perFrameLayout.reset();
    m_device = nullptr;
    
    MR_LOG(LogPBRDescriptor, Log, "FPBRDescriptorSetManager shutdown");
}

TSharedPtr<IRHIDescriptorSet> FPBRDescriptorSetManager::getPerFrameDescriptorSet()
{
    if (!m_device || !m_perFrameLayout) {
        return nullptr;
    }
    
    return m_device->allocateDescriptorSet(m_perFrameLayout);
}

TSharedPtr<IRHIDescriptorSet> FPBRDescriptorSetManager::getPerMaterialDescriptorSet(
    const FPBRMaterialParams& params,
    const FPBRMaterialTextures& textures)
{
    if (!m_device || !m_perMaterialLayout) {
        return nullptr;
    }
    
    // TODO: Implement material descriptor set caching based on material hash
    return m_device->allocateDescriptorSet(m_perMaterialLayout);
}

TSharedPtr<IRHIDescriptorSet> FPBRDescriptorSetManager::getPerObjectDescriptorSet()
{
    if (!m_device || !m_perObjectLayout) {
        return nullptr;
    }
    
    return m_device->allocateDescriptorSet(m_perObjectLayout);
}

void FPBRDescriptorSetManager::beginFrame(uint32 frameIndex)
{
    m_currentFrame = frameIndex;
    // TODO: Reset per-frame descriptor set pools
}

bool FPBRDescriptorSetManager::_createDefaultSampler()
{
    if (!m_device) {
        return false;
    }
    
    SamplerDesc desc;
    desc.filter = ESamplerFilter::Anisotropic;
    desc.addressU = ESamplerAddressMode::Wrap;
    desc.addressV = ESamplerAddressMode::Wrap;
    desc.addressW = ESamplerAddressMode::Wrap;
    desc.mipLODBias = 0.0f;
    desc.maxAnisotropy = 16;
    desc.minLOD = 0.0f;
    desc.maxLOD = 12.0f;
    desc.debugName = "PBR_DefaultSampler";
    
    m_defaultSampler = m_device->createSampler(desc);
    if (!m_defaultSampler) {
        MR_LOG(LogPBRDescriptor, Error, "Failed to create default PBR sampler");
        return false;
    }
    
    MR_LOG(LogPBRDescriptor, Log, "Created default PBR sampler (trilinear, 16x anisotropic)");
    return true;
}

} // namespace Renderer
} // namespace MonsterEngine
