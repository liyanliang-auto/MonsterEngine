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
    // VulkanDescriptorSetAllocator
    // ============================================================================
    
    VulkanDescriptorSetAllocator::VulkanDescriptorSetAllocator(VulkanDevice* device)
        : m_device(device)
        , m_currentFrame(0) {
        
        MR_LOG_INFO("Initializing descriptor set allocator (ring buffer)");
        
        // Pre-allocate initial pool
        m_pools.push_back(MakeUnique<VulkanDescriptorPool>(m_device, MAX_SETS_PER_POOL, getDefaultPoolSizes()));
        m_poolFrameNumbers.push_back(0);
        
        m_stats.totalPools = 1;
        m_stats.totalSetsAllocated = 0;
        m_stats.totalSetsRecycled = 0;
        m_stats.currentFrameAllocations = 0;
    }
    
    VulkanDescriptorSetAllocator::~VulkanDescriptorSetAllocator() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pools.clear();
        MR_LOG_INFO("Descriptor set allocator destroyed");
    }
    
    VkDescriptorSet VulkanDescriptorSetAllocator::allocate(VkDescriptorSetLayout layout, const TArray<VkDescriptorSetLayoutBinding>& bindings) {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        // Get current pool
        VulkanDescriptorPool* pool = getCurrentPool();
        if (!pool) {
            MR_LOG_ERROR("Failed to get descriptor pool");
            return VK_NULL_HANDLE;
        }
        
        // Try to allocate from current pool
        VkDescriptorSet descriptorSet = pool->allocate(layout);
        
        // If pool is full, create new pool and retry
        if (descriptorSet == VK_NULL_HANDLE && pool->isFull()) {
            MR_LOG_DEBUG("Current pool full, creating new pool");
            pool = createNewPool();
            if (pool) {
                descriptorSet = pool->allocate(layout);
            }
        }
        
        if (descriptorSet != VK_NULL_HANDLE) {
            ++m_stats.totalSetsAllocated;
            ++m_stats.currentFrameAllocations;
        }
        
        return descriptorSet;
    }
    
    VkDescriptorSet VulkanDescriptorSetAllocator::allocate(VkDescriptorSetLayout layout) {
        // Simplified allocate without bindings - delegates to full version with empty bindings
        TArray<VkDescriptorSetLayoutBinding> emptyBindings;
        return allocate(layout, emptyBindings);
    }
    
    void VulkanDescriptorSetAllocator::updateDescriptorSet(VkDescriptorSet descriptorSet,
                                                           const TArray<VkDescriptorSetLayoutBinding>& bindings,
                                                           const TMap<uint32, TSharedPtr<IRHIBuffer>>& buffers,
                                                           const TMap<uint32, TSharedPtr<IRHITexture>>& textures) {
        
        const auto& functions = VulkanAPI::getFunctions();
        VkDevice vkDevice = m_device->getLogicalDevice();
        
        TArray<VkWriteDescriptorSet> writes;
        TArray<VkDescriptorBufferInfo> bufferInfos;
        TArray<VkDescriptorImageInfo> imageInfos;
        
        // Build write descriptor sets
        for (const auto& binding : bindings) {
            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = descriptorSet;
            write.dstBinding = binding.binding;
            write.dstArrayElement = 0;
            write.descriptorCount = 1;
            write.descriptorType = binding.descriptorType;
            
            // Check if this binding is a buffer
            const TSharedPtr<IRHIBuffer>* bufferPtr = buffers.Find(binding.binding);
            if (bufferPtr && *bufferPtr) {
                auto* vulkanBuffer = static_cast<VulkanBuffer*>(bufferPtr->get());
                
                VkDescriptorBufferInfo bufferInfo{};
                bufferInfo.buffer = vulkanBuffer->getBuffer();
                bufferInfo.offset = 0;
                bufferInfo.range = vulkanBuffer->getSize();
                
                bufferInfos.Add(bufferInfo);
                write.pBufferInfo = &bufferInfos.Last();
                writes.Add(write);
                
                MR_LOG_DEBUG("Update descriptor set: uniform buffer at binding " + std::to_string(binding.binding));
            }
            
            // Check if this binding is a texture
            const TSharedPtr<IRHITexture>* texturePtr = textures.Find(binding.binding);
            if (texturePtr && *texturePtr) {
                auto* vulkanTexture = static_cast<VulkanTexture*>(texturePtr->get());
                
                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageView = vulkanTexture->getImageView();
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.sampler = vulkanTexture->getDefaultSampler();
                
                imageInfos.Add(imageInfo);
                write.pImageInfo = &imageInfos.Last();
                writes.Add(write);
                
                MR_LOG_DEBUG("Update descriptor set: texture at binding " + std::to_string(binding.binding) +
                            ", sampler=" + std::to_string(reinterpret_cast<uint64>(imageInfo.sampler)));
            }
        }
        
        // Apply updates
        if (!writes.empty()) {
            functions.vkUpdateDescriptorSets(vkDevice, static_cast<uint32>(writes.size()), writes.data(), 0, nullptr);
            MR_LOG_DEBUG("Updated descriptor set with " + std::to_string(writes.size()) + " writes");
        }
    }
    
    void VulkanDescriptorSetAllocator::beginFrame(uint64 frameNumber) {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        m_currentFrame = frameNumber;
        m_stats.currentFrameAllocations = 0;
        
        // Reset pools that are old enough (FRAME_LAG frames old)
        for (size_t i = 0; i < m_pools.size(); ++i) {
            if (m_poolFrameNumbers[i] + FRAME_LAG < frameNumber) {
                m_pools[i]->reset();
                m_poolFrameNumbers[i] = frameNumber;
                ++m_stats.totalSetsRecycled;
                
                MR_LOG_DEBUG("Recycled descriptor pool " + std::to_string(i) + " for frame " + std::to_string(frameNumber));
            }
        }
    }
    
    VulkanDescriptorPool* VulkanDescriptorSetAllocator::getCurrentPool() {
        if (m_currentPoolIndex >= m_pools.size()) {
            return createNewPool();
        }
        return m_pools[m_currentPoolIndex].get();
    }
    
    VulkanDescriptorPool* VulkanDescriptorSetAllocator::createNewPool() {
        auto pool = MakeUnique<VulkanDescriptorPool>(m_device, MAX_SETS_PER_POOL, getDefaultPoolSizes());
        if (!pool) {
            MR_LOG_ERROR("Failed to create new descriptor pool");
            return nullptr;
        }
        
        m_pools.push_back(std::move(pool));
        m_poolFrameNumbers.push_back(m_currentFrame);
        m_currentPoolIndex = static_cast<uint32>(m_pools.size() - 1);
        ++m_stats.totalPools;
        
        MR_LOG_INFO("Created new descriptor pool #" + std::to_string(m_currentPoolIndex));
        return m_pools[m_currentPoolIndex].get();
    }
    
    TArray<VkDescriptorPoolSize> VulkanDescriptorSetAllocator::getDefaultPoolSizes() const {
        // Based on UE5's typical usage patterns
        TArray<VkDescriptorPoolSize> poolSizes;
        
        // Uniform buffers (most common)
        VkDescriptorPoolSize uniformBufferSize{};
        uniformBufferSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uniformBufferSize.descriptorCount = MAX_SETS_PER_POOL * 8; // Average 8 UBOs per set
        poolSizes.push_back(uniformBufferSize);
        
        // Combined image samplers
        VkDescriptorPoolSize samplerSize{};
        samplerSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerSize.descriptorCount = MAX_SETS_PER_POOL * 4; // Average 4 textures per set
        poolSizes.push_back(samplerSize);
        
        // Storage buffers (for compute shaders)
        VkDescriptorPoolSize storageBufferSize{};
        storageBufferSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        storageBufferSize.descriptorCount = MAX_SETS_PER_POOL * 2;
        poolSizes.push_back(storageBufferSize);
        
        // Storage images (for compute shaders)
        VkDescriptorPoolSize storageImageSize{};
        storageImageSize.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        storageImageSize.descriptorCount = MAX_SETS_PER_POOL * 2;
        poolSizes.push_back(storageImageSize);
        
        return poolSizes;
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

