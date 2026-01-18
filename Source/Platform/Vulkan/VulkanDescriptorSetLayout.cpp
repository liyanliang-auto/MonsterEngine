#include "Platform/Vulkan/VulkanDescriptorSetLayout.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanTexture.h"
#include "Platform/Vulkan/VulkanSampler.h"
#include "Core/Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogVulkanRHI, Log, All);

namespace MonsterRender::RHI::Vulkan {
    
    // ========================================================================
    // VulkanDescriptorSetLayout Implementation
    // ========================================================================
    
    VulkanDescriptorSetLayout::VulkanDescriptorSetLayout(VulkanDevice* device, 
                                                         const FDescriptorSetLayoutDesc& desc)
        : m_device(device)
        , m_setIndex(desc.setIndex)
        , m_bindings(desc.bindings)
    {
        if (!_createLayout()) {
            MR_LOG(LogVulkanRHI, Error, "Failed to create Vulkan descriptor set layout for set %u", m_setIndex);
        }
    }
    
    VulkanDescriptorSetLayout::~VulkanDescriptorSetLayout() {
        _destroyLayout();
    }
    
    bool VulkanDescriptorSetLayout::_createLayout() {
        // Convert RHI bindings to Vulkan bindings
        m_vulkanBindings.clear();
        m_vulkanBindings.reserve(m_bindings.size());
        
        for (const auto& binding : m_bindings) {
            VkDescriptorSetLayoutBinding vkBinding = {};
            vkBinding.binding = binding.binding;
            vkBinding.descriptorType = _convertDescriptorType(binding.descriptorType);
            vkBinding.descriptorCount = binding.descriptorCount;
            vkBinding.stageFlags = _convertShaderStages(binding.shaderStages);
            vkBinding.pImmutableSamplers = nullptr;
            
            m_vulkanBindings.push_back(vkBinding);
        }
        
        // Create Vulkan descriptor set layout
        VkDescriptorSetLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32>(m_vulkanBindings.size());
        layoutInfo.pBindings = m_vulkanBindings.data();
        
        VkResult result = vkCreateDescriptorSetLayout(m_device->getDevice(), &layoutInfo, 
                                                     nullptr, &m_layout);
        
        if (result != VK_SUCCESS) {
            MR_LOG(LogVulkanRHI, Error, "vkCreateDescriptorSetLayout failed with result %d", result);
            return false;
        }
        
        MR_LOG(LogVulkanRHI, Verbose, "Created descriptor set layout for set %u with %llu bindings",
               m_setIndex, static_cast<uint64>(m_bindings.size()));
        
        return true;
    }
    
    void VulkanDescriptorSetLayout::_destroyLayout() {
        if (m_layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(m_device->getDevice(), m_layout, nullptr);
            m_layout = VK_NULL_HANDLE;
        }
    }
    
    VkDescriptorType VulkanDescriptorSetLayout::_convertDescriptorType(EDescriptorType type) const {
        switch (type) {
            case EDescriptorType::UniformBuffer:
                return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            case EDescriptorType::StorageBuffer:
                return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            case EDescriptorType::Texture:
                return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            case EDescriptorType::StorageTexture:
                return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            case EDescriptorType::Sampler:
                return VK_DESCRIPTOR_TYPE_SAMPLER;
            case EDescriptorType::CombinedTextureSampler:
                return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            case EDescriptorType::InputAttachment:
                return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
            default:
                MR_LOG(LogVulkanRHI, Warning, "Unknown descriptor type, defaulting to uniform buffer");
                return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        }
    }
    
    VkShaderStageFlags VulkanDescriptorSetLayout::_convertShaderStages(EShaderStage stages) const {
        VkShaderStageFlags flags = 0;
        
        if (static_cast<uint32>(stages) & static_cast<uint32>(EShaderStage::Vertex)) {
            flags |= VK_SHADER_STAGE_VERTEX_BIT;
        }
        if (static_cast<uint32>(stages) & static_cast<uint32>(EShaderStage::Fragment)) {
            flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        if (static_cast<uint32>(stages) & static_cast<uint32>(EShaderStage::Compute)) {
            flags |= VK_SHADER_STAGE_COMPUTE_BIT;
        }
        if (static_cast<uint32>(stages) & static_cast<uint32>(EShaderStage::Geometry)) {
            flags |= VK_SHADER_STAGE_GEOMETRY_BIT;
        }
        if (static_cast<uint32>(stages) & static_cast<uint32>(EShaderStage::TessellationControl)) {
            flags |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        }
        if (static_cast<uint32>(stages) & static_cast<uint32>(EShaderStage::TessellationEvaluation)) {
            flags |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        }
        
        return flags;
    }
    
    // ========================================================================
    // VulkanPipelineLayout Implementation
    // ========================================================================
    
    VulkanPipelineLayout::VulkanPipelineLayout(VulkanDevice* device, 
                                               const FPipelineLayoutDesc& desc)
        : m_device(device)
        , m_setLayouts(desc.setLayouts)
        , m_pushConstantRanges(desc.pushConstantRanges)
    {
        if (!_createLayout()) {
            MR_LOG(LogVulkanRHI, Error, "Failed to create Vulkan pipeline layout");
        }
    }
    
    VulkanPipelineLayout::~VulkanPipelineLayout() {
        _destroyLayout();
    }
    
    bool VulkanPipelineLayout::_createLayout() {
        // Convert descriptor set layouts to Vulkan handles
        m_vulkanSetLayouts.clear();
        m_vulkanSetLayouts.reserve(m_setLayouts.size());
        
        for (const auto& setLayout : m_setLayouts) {
            auto vulkanLayout = dynamic_cast<VulkanDescriptorSetLayout*>(setLayout.get());
            if (vulkanLayout && vulkanLayout->isValid()) {
                m_vulkanSetLayouts.push_back(vulkanLayout->getHandle());
            } else {
                MR_LOG(LogVulkanRHI, Error, "Invalid descriptor set layout in pipeline layout");
                return false;
            }
        }
        
        // Convert push constant ranges
        m_vulkanPushConstantRanges.clear();
        m_vulkanPushConstantRanges.reserve(m_pushConstantRanges.size());
        
        for (const auto& range : m_pushConstantRanges) {
            VkPushConstantRange vkRange = {};
            vkRange.stageFlags = _convertShaderStages(range.shaderStages);
            vkRange.offset = range.offset;
            vkRange.size = range.size;
            
            m_vulkanPushConstantRanges.push_back(vkRange);
        }
        
        // Create Vulkan pipeline layout
        VkPipelineLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = static_cast<uint32>(m_vulkanSetLayouts.size());
        layoutInfo.pSetLayouts = m_vulkanSetLayouts.data();
        layoutInfo.pushConstantRangeCount = static_cast<uint32>(m_vulkanPushConstantRanges.size());
        layoutInfo.pPushConstantRanges = m_vulkanPushConstantRanges.data();
        
        VkResult result = vkCreatePipelineLayout(m_device->getDevice(), &layoutInfo, 
                                                nullptr, &m_layout);
        
        if (result != VK_SUCCESS) {
            MR_LOG(LogVulkanRHI, Error, "vkCreatePipelineLayout failed with result %d", result);
            return false;
        }
        
        MR_LOG(LogVulkanRHI, Verbose, "Created pipeline layout with %llu descriptor sets and %llu push constant ranges",
               static_cast<uint64>(m_setLayouts.size()), 
               static_cast<uint64>(m_pushConstantRanges.size()));
        
        return true;
    }
    
    void VulkanPipelineLayout::_destroyLayout() {
        if (m_layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_device->getDevice(), m_layout, nullptr);
            m_layout = VK_NULL_HANDLE;
        }
    }
    
    VkShaderStageFlags VulkanPipelineLayout::_convertShaderStages(EShaderStage stages) const {
        VkShaderStageFlags flags = 0;
        
        if (static_cast<uint32>(stages) & static_cast<uint32>(EShaderStage::Vertex)) {
            flags |= VK_SHADER_STAGE_VERTEX_BIT;
        }
        if (static_cast<uint32>(stages) & static_cast<uint32>(EShaderStage::Fragment)) {
            flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        if (static_cast<uint32>(stages) & static_cast<uint32>(EShaderStage::Compute)) {
            flags |= VK_SHADER_STAGE_COMPUTE_BIT;
        }
        if (static_cast<uint32>(stages) & static_cast<uint32>(EShaderStage::Geometry)) {
            flags |= VK_SHADER_STAGE_GEOMETRY_BIT;
        }
        if (static_cast<uint32>(stages) & static_cast<uint32>(EShaderStage::TessellationControl)) {
            flags |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        }
        if (static_cast<uint32>(stages) & static_cast<uint32>(EShaderStage::TessellationEvaluation)) {
            flags |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        }
        
        return flags;
    }
    
    // ========================================================================
    // VulkanDescriptorSet Implementation
    // ========================================================================
    
    VulkanDescriptorSet::VulkanDescriptorSet(VulkanDevice* device,
                                             TSharedPtr<VulkanDescriptorSetLayout> layout,
                                             VkDescriptorSet descriptorSet)
        : m_device(device)
        , m_descriptorSet(descriptorSet)
        , m_layout(layout)
    {
        MR_LOG(LogVulkanRHI, VeryVerbose, "Created descriptor set for set %u", 
               m_layout->getSetIndex());
    }
    
    VulkanDescriptorSet::~VulkanDescriptorSet() {
        // Descriptor sets are freed when the pool is reset, no explicit cleanup needed
        m_boundBuffers.clear();
        m_boundTextures.clear();
        m_boundSamplers.clear();
    }
    
    void VulkanDescriptorSet::updateUniformBuffer(uint32 binding, TSharedPtr<IRHIBuffer> buffer,
                                                  uint32 offset, uint32 range) {
        auto vulkanBuffer = dynamic_cast<VulkanBuffer*>(buffer.get());
        if (!vulkanBuffer) {
            MR_LOG(LogVulkanRHI, Error, "Invalid buffer for descriptor set update");
            return;
        }
        
        VkDescriptorBufferInfo bufferInfo = {};
        bufferInfo.buffer = vulkanBuffer->getBuffer();
        bufferInfo.offset = offset;
        bufferInfo.range = (range == 0) ? vulkanBuffer->getSize() : range;
        
        _writeDescriptor(binding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &bufferInfo, nullptr);
        
        // Cache the buffer to keep it alive
        m_boundBuffers[binding] = buffer;
    }
    
    void VulkanDescriptorSet::updateTexture(uint32 binding, TSharedPtr<IRHITexture> texture) {
        auto vulkanTexture = dynamic_cast<VulkanTexture*>(texture.get());
        if (!vulkanTexture) {
            MR_LOG(LogVulkanRHI, Error, "Invalid texture for descriptor set update");
            return;
        }
        
        VkDescriptorImageInfo imageInfo = {};
        imageInfo.imageView = vulkanTexture->getImageView();
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.sampler = VK_NULL_HANDLE; // Separate sampler
        
        _writeDescriptor(binding, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, nullptr, &imageInfo);
        
        // Cache the texture to keep it alive
        m_boundTextures[binding] = texture;
    }
    
    void VulkanDescriptorSet::updateSampler(uint32 binding, TSharedPtr<IRHISampler> sampler) {
        auto vulkanSampler = dynamic_cast<VulkanSampler*>(sampler.get());
        if (!vulkanSampler) {
            MR_LOG(LogVulkanRHI, Error, "Invalid sampler for descriptor set update");
            return;
        }
        
        VkDescriptorImageInfo imageInfo = {};
        imageInfo.sampler = vulkanSampler->getSampler();
        imageInfo.imageView = VK_NULL_HANDLE;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        
        _writeDescriptor(binding, VK_DESCRIPTOR_TYPE_SAMPLER, nullptr, &imageInfo);
        
        // Cache the sampler to keep it alive
        m_boundSamplers[binding] = sampler;
    }
    
    void VulkanDescriptorSet::updateCombinedTextureSampler(uint32 binding, 
                                                           TSharedPtr<IRHITexture> texture,
                                                           TSharedPtr<IRHISampler> sampler) {
        auto vulkanTexture = dynamic_cast<VulkanTexture*>(texture.get());
        auto vulkanSampler = dynamic_cast<VulkanSampler*>(sampler.get());
        
        if (!vulkanTexture || !vulkanSampler) {
            MR_LOG(LogVulkanRHI, Error, "Invalid texture or sampler for combined descriptor set update");
            return;
        }
        
        VkDescriptorImageInfo imageInfo = {};
        imageInfo.imageView = vulkanTexture->getImageView();
        imageInfo.sampler = vulkanSampler->getSampler();
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        
        _writeDescriptor(binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &imageInfo);
        
        // Cache resources to keep them alive
        m_boundTextures[binding] = texture;
        m_boundSamplers[binding] = sampler;
    }
    
    void VulkanDescriptorSet::_writeDescriptor(uint32 binding, VkDescriptorType type,
                                               const VkDescriptorBufferInfo* bufferInfo,
                                               const VkDescriptorImageInfo* imageInfo) {
        VkWriteDescriptorSet write = {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_descriptorSet;
        write.dstBinding = binding;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = type;
        write.pBufferInfo = bufferInfo;
        write.pImageInfo = imageInfo;
        
        MR_LOG(LogVulkanRHI, Error, "DEBUG: About to call vkUpdateDescriptorSets for binding %u, descriptorSet=0x%llx", 
               binding, (uint64)m_descriptorSet);
        
        vkUpdateDescriptorSets(m_device->getDevice(), 1, &write, 0, nullptr);
        
        MR_LOG(LogVulkanRHI, Error, "DEBUG: vkUpdateDescriptorSets completed for binding %u", binding);
    }
}
