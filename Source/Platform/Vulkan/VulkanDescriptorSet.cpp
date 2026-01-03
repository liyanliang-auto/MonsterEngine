#include "Platform/Vulkan/VulkanDescriptorSet.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanTexture.h"
#include "Core/Log.h"

namespace MonsterRender::RHI::Vulkan {
    
    // ============================================================================
    // VulkanDescriptorPool
    // ============================================================================
    
    VulkanDescriptorPool::VulkanDescriptorPool(VulkanDevice* device, uint32 maxSets, const TArray<VkDescriptorPoolSize>& poolSizes)
        : m_device(device)
        , m_maxSets(maxSets)
        , m_allocatedSets(0) {
        
        const auto& functions = VulkanAPI::getFunctions();
        VkDevice vkDevice = m_device->getLogicalDevice();
        
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; // Allow individual free
        poolInfo.maxSets = maxSets;
        poolInfo.poolSizeCount = static_cast<uint32>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        
        VkResult result = functions.vkCreateDescriptorPool(vkDevice, &poolInfo, nullptr, &m_pool);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to create descriptor pool: " + std::to_string(result));
            return;
        }
        
        MR_LOG_DEBUG("Created descriptor pool with max " + std::to_string(maxSets) + " sets");
    }
    
    VulkanDescriptorPool::~VulkanDescriptorPool() {
        if (m_pool != VK_NULL_HANDLE && m_device) {
            const auto& functions = VulkanAPI::getFunctions();
            VkDevice vkDevice = m_device->getLogicalDevice();
            functions.vkDestroyDescriptorPool(vkDevice, m_pool, nullptr);
            m_pool = VK_NULL_HANDLE;
        }
    }
    
    VkDescriptorSet VulkanDescriptorPool::allocate(VkDescriptorSetLayout layout) {
        if (isFull()) {
            MR_LOG_WARNING("Descriptor pool is full, cannot allocate");
            return VK_NULL_HANDLE;
        }
        
        const auto& functions = VulkanAPI::getFunctions();
        VkDevice vkDevice = m_device->getLogicalDevice();
        
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_pool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &layout;
        
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        VkResult result = functions.vkAllocateDescriptorSets(vkDevice, &allocInfo, &descriptorSet);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to allocate descriptor set: " + std::to_string(result));
            return VK_NULL_HANDLE;
        }
        
        ++m_allocatedSets;
        return descriptorSet;
    }
    
    void VulkanDescriptorPool::reset() {
        if (m_pool != VK_NULL_HANDLE && m_device) {
            const auto& functions = VulkanAPI::getFunctions();
            VkDevice vkDevice = m_device->getLogicalDevice();
            
            VkResult result = functions.vkResetDescriptorPool(vkDevice, m_pool, 0);
            if (result == VK_SUCCESS) {
                m_allocatedSets = 0;
                MR_LOG_DEBUG("Descriptor pool reset successfully");
            } else {
                MR_LOG_ERROR("Failed to reset descriptor pool: " + std::to_string(result));
            }
        }
    }
    
    // ============================================================================
    // VulkanDescriptorSetWriter
    // ============================================================================
    
    void VulkanDescriptorSetWriter::addUniformBuffer(uint32 binding, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = buffer;
        bufferInfo.offset = offset;
        bufferInfo.range = range;
        m_bufferInfos.push_back(bufferInfo);
        
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstBinding = binding;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &m_bufferInfos.back();
        m_writes.push_back(write);
    }
    
    void VulkanDescriptorSetWriter::addCombinedImageSampler(uint32 binding, VkImageView imageView, VkSampler sampler, VkImageLayout layout) {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageView = imageView;
        imageInfo.sampler = sampler;
        imageInfo.imageLayout = layout;
        m_imageInfos.push_back(imageInfo);
        
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstBinding = binding;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &m_imageInfos.back();
        m_writes.push_back(write);
    }
    
    void VulkanDescriptorSetWriter::applyWrites(VkDescriptorSet descriptorSet) {
        for (auto& write : m_writes) {
            write.dstSet = descriptorSet;
        }
    }
    
    void VulkanDescriptorSetWriter::clear() {
        m_writes.clear();
        m_bufferInfos.clear();
        m_imageInfos.clear();
    }
}

