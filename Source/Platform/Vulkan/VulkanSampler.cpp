// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Vulkan Sampler Implementation
//
// Reference UE5: Engine/Source/Runtime/VulkanRHI/Private/VulkanResources.cpp

#include "Platform/Vulkan/VulkanSampler.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Core/Log.h"

namespace MonsterRender::RHI::Vulkan {

VulkanSampler::VulkanSampler(VulkanDevice* device, const SamplerDesc& desc)
    : m_device(device)
    , m_desc(desc)
{
    MR_ASSERT(m_device != nullptr);
    
    if (!initialize()) {
        MR_LOG_ERROR("Failed to create Vulkan sampler: " + desc.debugName);
    }
}

VulkanSampler::~VulkanSampler() {
    if (m_sampler != VK_NULL_HANDLE && m_device) {
        const auto& functions = VulkanAPI::getFunctions();
        VkDevice device = m_device->getDevice();
        
        if (functions.vkDestroySampler) {
            functions.vkDestroySampler(device, m_sampler, nullptr);
        }
        
        m_sampler = VK_NULL_HANDLE;
        MR_LOG_DEBUG("Destroyed Vulkan sampler: " + m_desc.debugName);
    }
}

bool VulkanSampler::initialize() {
    const auto& functions = VulkanAPI::getFunctions();
    VkDevice device = m_device->getDevice();
    
    // Setup sampler create info
    VkSamplerCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    createInfo.pNext = nullptr;
    createInfo.flags = 0;
    
    // Filter modes
    createInfo.magFilter = getVulkanFilter(m_desc.filter);
    createInfo.minFilter = getVulkanFilter(m_desc.filter);
    createInfo.mipmapMode = getVulkanMipmapMode(m_desc.filter);
    
    // Address modes
    createInfo.addressModeU = getVulkanAddressMode(m_desc.addressU);
    createInfo.addressModeV = getVulkanAddressMode(m_desc.addressV);
    createInfo.addressModeW = getVulkanAddressMode(m_desc.addressW);
    
    // LOD settings
    createInfo.mipLodBias = m_desc.mipLODBias;
    createInfo.minLod = m_desc.minLOD;
    createInfo.maxLod = m_desc.maxLOD;
    
    // Anisotropic filtering
    if (m_desc.filter == ESamplerFilter::Anisotropic && m_desc.maxAnisotropy > 1) {
        createInfo.anisotropyEnable = VK_TRUE;
        createInfo.maxAnisotropy = static_cast<float>(m_desc.maxAnisotropy);
    } else {
        createInfo.anisotropyEnable = VK_FALSE;
        createInfo.maxAnisotropy = 1.0f;
    }
    
    // Comparison function (for shadow mapping)
    if (m_desc.comparisonFunc != EComparisonFunc::Never) {
        createInfo.compareEnable = VK_TRUE;
        createInfo.compareOp = getVulkanCompareOp(m_desc.comparisonFunc);
    } else {
        createInfo.compareEnable = VK_FALSE;
        createInfo.compareOp = VK_COMPARE_OP_NEVER;
    }
    
    // Border color
    createInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    
    // Unnormalized coordinates (usually false)
    createInfo.unnormalizedCoordinates = VK_FALSE;
    
    // Create the sampler
    VkResult result = functions.vkCreateSampler(device, &createInfo, nullptr, &m_sampler);
    if (result != VK_SUCCESS) {
        MR_LOG_ERROR("vkCreateSampler failed with error: " + std::to_string(result));
        return false;
    }
    
    MR_LOG_DEBUG("Created Vulkan sampler: " + m_desc.debugName + 
                 " (filter=" + std::to_string(static_cast<int>(m_desc.filter)) + ")");
    
    return true;
}

VkFilter VulkanSampler::getVulkanFilter(ESamplerFilter filter) {
    switch (filter) {
        case ESamplerFilter::Point:
            return VK_FILTER_NEAREST;
        case ESamplerFilter::Bilinear:
        case ESamplerFilter::Trilinear:
        case ESamplerFilter::Anisotropic:
        default:
            return VK_FILTER_LINEAR;
    }
}

VkSamplerMipmapMode VulkanSampler::getVulkanMipmapMode(ESamplerFilter filter) {
    switch (filter) {
        case ESamplerFilter::Point:
            return VK_SAMPLER_MIPMAP_MODE_NEAREST;
        case ESamplerFilter::Bilinear:
            return VK_SAMPLER_MIPMAP_MODE_NEAREST;
        case ESamplerFilter::Trilinear:
        case ESamplerFilter::Anisotropic:
        default:
            return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }
}

VkSamplerAddressMode VulkanSampler::getVulkanAddressMode(ESamplerAddressMode mode) {
    switch (mode) {
        case ESamplerAddressMode::Wrap:
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case ESamplerAddressMode::Clamp:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case ESamplerAddressMode::Mirror:
            return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case ESamplerAddressMode::Border:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        default:
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

VkCompareOp VulkanSampler::getVulkanCompareOp(EComparisonFunc func) {
    switch (func) {
        case EComparisonFunc::Never:
            return VK_COMPARE_OP_NEVER;
        case EComparisonFunc::Less:
            return VK_COMPARE_OP_LESS;
        case EComparisonFunc::Equal:
            return VK_COMPARE_OP_EQUAL;
        case EComparisonFunc::LessEqual:
            return VK_COMPARE_OP_LESS_OR_EQUAL;
        case EComparisonFunc::Greater:
            return VK_COMPARE_OP_GREATER;
        case EComparisonFunc::NotEqual:
            return VK_COMPARE_OP_NOT_EQUAL;
        case EComparisonFunc::GreaterEqual:
            return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case EComparisonFunc::Always:
            return VK_COMPARE_OP_ALWAYS;
        default:
            return VK_COMPARE_OP_NEVER;
    }
}

} // namespace MonsterRender::RHI::Vulkan
