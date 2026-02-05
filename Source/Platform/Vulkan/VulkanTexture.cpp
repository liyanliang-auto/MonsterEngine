#include "Platform/Vulkan/VulkanTexture.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanUtils.h"
#include "Core/Log.h"

namespace MonsterRender::RHI::Vulkan {
    
    VulkanTexture::VulkanTexture(VulkanDevice* device, const TextureDesc& desc)
        : IRHITexture(desc), m_device(device) {
        
        MR_ASSERT(m_device != nullptr);
        MR_ASSERT(desc.width > 0 && desc.height > 0 && desc.depth > 0);
        
        MR_LOG_DEBUG("Creating Vulkan texture: " + desc.debugName + 
                     " (" + std::to_string(desc.width) + "x" + std::to_string(desc.height) + ")");
        
        m_format = VulkanUtils::getRHIFormatToVulkan(desc.format);
        
        if (!initialize()) {
            MR_LOG_ERROR("Failed to create Vulkan texture: " + desc.debugName);
        }
    }
    
    VulkanTexture::~VulkanTexture() {
        destroy();
    }
    
    bool VulkanTexture::initialize() {
        const auto& functions = VulkanAPI::getFunctions();
        VkDevice device = m_device->getDevice();
        
        // Determine image type
        VkImageType imageType = VK_IMAGE_TYPE_2D;
        if (m_desc.height == 1 && m_desc.depth == 1) {
            imageType = VK_IMAGE_TYPE_1D;
        } else if (m_desc.depth > 1) {
            imageType = VK_IMAGE_TYPE_3D;
        }
        
        // Setup image create info
        m_imageCreateInfo = {};
        m_imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        m_imageCreateInfo.imageType = imageType;
        m_imageCreateInfo.format = m_format;
        m_imageCreateInfo.extent = {m_desc.width, m_desc.height, m_desc.depth};
        m_imageCreateInfo.mipLevels = m_desc.mipLevels;
        m_imageCreateInfo.arrayLayers = m_desc.arraySize;
        m_imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        m_imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        m_imageCreateInfo.usage = VulkanUtils::getImageUsageFlags(m_desc.usage);
        m_imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        m_imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        
        // Create the image
        VkResult result = functions.vkCreateImage(device, &m_imageCreateInfo, nullptr, &m_image);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to create Vulkan image: " + std::to_string(result));
            return false;
        }
        
        // Get memory requirements
        VkMemoryRequirements memRequirements;
        functions.vkGetImageMemoryRequirements(device, m_image, &memRequirements);
        
        // Use FVulkanMemoryManager for allocation (UE5-style sub-allocation)
        auto* memoryManager = m_device->getMemoryManager();
        if (memoryManager) {
            FVulkanMemoryManager::FAllocationRequest request{};
            request.Size = memRequirements.size;
            request.Alignment = memRequirements.alignment;
            request.MemoryTypeBits = memRequirements.memoryTypeBits;
            request.RequiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            request.PreferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            request.bDedicated = (memRequirements.size >= 16 * 1024 * 1024);  // 16MB+ use dedicated
            
            if (memoryManager->Allocate(request, m_allocation)) {
                m_deviceMemory = m_allocation.DeviceMemory;
                m_usesMemoryManager = true;
                
                // Bind image to allocated memory
                result = functions.vkBindImageMemory(device, m_image, m_deviceMemory, m_allocation.Offset);
                if (result != VK_SUCCESS) {
                    MR_LOG_ERROR("Failed to bind image memory: " + std::to_string(result));
                    memoryManager->Free(m_allocation);
                    m_usesMemoryManager = false;
                    return false;
                }
                
                MR_LOG_DEBUG("Successfully allocated image memory via memory manager: " + m_desc.debugName);
            } else {
                MR_LOG_WARNING("Memory manager allocation failed, falling back to direct allocation");
            }
        }
        
        // Fallback to direct allocation
        if (!m_usesMemoryManager) {
            m_memoryAllocateInfo = {};
            m_memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            m_memoryAllocateInfo.allocationSize = memRequirements.size;
            m_memoryAllocateInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            
            result = functions.vkAllocateMemory(device, &m_memoryAllocateInfo, nullptr, &m_deviceMemory);
            if (result != VK_SUCCESS) {
                MR_LOG_ERROR("Failed to allocate image memory: " + std::to_string(result));
                return false;
            }
            
            result = functions.vkBindImageMemory(device, m_image, m_deviceMemory, 0);
            if (result != VK_SUCCESS) {
                MR_LOG_ERROR("Failed to bind image memory: " + std::to_string(result));
                return false;
            }
        }
        
        // Create image view
        if (!createImageView()) {
            return false;
        }
        
        // Create default sampler for this texture
        if (!createDefaultSampler()) {
            MR_LOG_WARNING("Failed to create default sampler for texture: " + m_desc.debugName);
            // Not fatal - sampler can be provided externally
        }
        
        // Upload initial data and transition layout if provided
        if (m_desc.initialData && m_desc.initialDataSize > 0) {
            if (!uploadInitialData()) {
                MR_LOG_ERROR("Failed to upload initial data for texture: " + m_desc.debugName);
                return false;
            }
        } else {
            // Check if this is a depth texture - skip layout transition for depth textures
            bool isDepthTexture = (static_cast<uint32>(m_desc.usage) & static_cast<uint32>(EResourceUsage::DepthStencil)) != 0;
            
            if (!isDepthTexture) {
                // No initial data, just transition layout to SHADER_READ_ONLY_OPTIMAL
                if (!transitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)) {
                    MR_LOG_ERROR("Failed to transition image layout for texture: " + m_desc.debugName);
                    return false;
                }
            } else {
                // Depth textures stay in UNDEFINED layout until first use
                MR_LOG_DEBUG("Skipping layout transition for depth texture: " + m_desc.debugName);
            }
        }
        
        MR_LOG_DEBUG("Successfully created Vulkan texture: " + m_desc.debugName);
        return true;
    }
    
    void VulkanTexture::destroy() {
        if (m_device) {
            const auto& functions = VulkanAPI::getFunctions();
            VkDevice device = m_device->getDevice();
            
            // Destroy default sampler
            if (m_defaultSampler != VK_NULL_HANDLE) {
                functions.vkDestroySampler(device, m_defaultSampler, nullptr);
                m_defaultSampler = VK_NULL_HANDLE;
            }
            
            // Destroy image view
            if (m_imageView != VK_NULL_HANDLE) {
                functions.vkDestroyImageView(device, m_imageView, nullptr);
                m_imageView = VK_NULL_HANDLE;
            }
            
            // Destroy image
            if (m_image != VK_NULL_HANDLE) {
                functions.vkDestroyImage(device, m_image, nullptr);
                m_image = VK_NULL_HANDLE;
            }
            
            // Free memory using memory manager if applicable
            if (m_usesMemoryManager) {
                auto* memoryManager = m_device->getMemoryManager();
                if (memoryManager && m_allocation.DeviceMemory != VK_NULL_HANDLE) {
                    memoryManager->Free(m_allocation);
                    m_usesMemoryManager = false;
                }
            } else {
                // Direct allocation path
                if (m_deviceMemory != VK_NULL_HANDLE) {
                    functions.vkFreeMemory(device, m_deviceMemory, nullptr);
                    m_deviceMemory = VK_NULL_HANDLE;
                }
            }
        }
    }
    
    bool VulkanTexture::createImageView() {
        const auto& functions = VulkanAPI::getFunctions();
        VkDevice device = m_device->getDevice();
        
        // Determine view type
        VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
        if (m_desc.arraySize == 6) {
            viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        } else if (m_desc.arraySize > 1) {
            if (m_desc.height == 1 && m_desc.depth == 1) {
                viewType = VK_IMAGE_VIEW_TYPE_1D_ARRAY;
            } else if (m_desc.depth == 1) {
                viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            }
        } else {
            if (m_desc.height == 1 && m_desc.depth == 1) {
                viewType = VK_IMAGE_VIEW_TYPE_1D;
            } else if (m_desc.depth > 1) {
                viewType = VK_IMAGE_VIEW_TYPE_3D;
            }
        }
        
        // Determine aspect mask based on format
        VkImageAspectFlags aspectMask = VulkanUtils::getImageAspectMask(m_format);
        
        // Setup image view create info
        m_imageViewCreateInfo = {};
        m_imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        m_imageViewCreateInfo.image = m_image;
        m_imageViewCreateInfo.viewType = viewType;
        m_imageViewCreateInfo.format = m_format;
        m_imageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        m_imageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        m_imageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        m_imageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        m_imageViewCreateInfo.subresourceRange.aspectMask = aspectMask;
        m_imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
        m_imageViewCreateInfo.subresourceRange.levelCount = m_desc.mipLevels;
        m_imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
        m_imageViewCreateInfo.subresourceRange.layerCount = m_desc.arraySize;
        
        // Create the image view
        VkResult result = functions.vkCreateImageView(device, &m_imageViewCreateInfo, nullptr, &m_imageView);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to create image view: " + std::to_string(result));
            return false;
        }
        
        MR_LOG_INFO("VulkanTexture: Created imageView=" + std::to_string(reinterpret_cast<uint64>(m_imageView)) +
                   ", image=" + std::to_string(reinterpret_cast<uint64>(m_image)) +
                   ", format=" + std::to_string(static_cast<int>(m_format)) +
                   ", size=" + std::to_string(m_desc.width) + "x" + std::to_string(m_desc.height));
        
        return true;
    }
    
    uint32 VulkanTexture::findMemoryType(uint32 typeFilter, VkMemoryPropertyFlags properties) {
        return VulkanUtils::findMemoryType(m_device->getMemoryProperties(), typeFilter, properties);
    }
    
    bool VulkanTexture::createDefaultSampler() {
        const auto& functions = VulkanAPI::getFunctions();
        VkDevice device = m_device->getDevice();
        
        // Create default sampler with trilinear filtering
        VkSamplerCreateInfo samplerInfo = {};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = 16.0f;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_NEVER;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = static_cast<float>(m_desc.mipLevels);
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        
        VkResult result = functions.vkCreateSampler(device, &samplerInfo, nullptr, &m_defaultSampler);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to create default sampler: " + std::to_string(result));
            return false;
        }
        
        MR_LOG_DEBUG("Created default sampler for texture: " + m_desc.debugName);
        return true;
    }
    
    bool VulkanTexture::transitionImageLayout(VkImageLayout oldLayout, VkImageLayout newLayout) {
        const auto& functions = VulkanAPI::getFunctions();
        VkDevice device = m_device->getDevice();
        
        // Create a one-time command buffer for layout transition
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = m_device->getCommandPool();
        allocInfo.commandBufferCount = 1;
        
        VkCommandBuffer commandBuffer;
        VkResult result = functions.vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to allocate command buffer for layout transition");
            return false;
        }
        
        // Begin command buffer
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        
        functions.vkBeginCommandBuffer(commandBuffer, &beginInfo);
        
        // Setup image memory barrier for layout transition
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = m_desc.mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = m_desc.arraySize;
        
        // Determine pipeline stages and access masks based on layouts
        VkPipelineStageFlags sourceStage;
        VkPipelineStageFlags destinationStage;
        
        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        } else {
            MR_LOG_ERROR("Unsupported layout transition");
            functions.vkFreeCommandBuffers(device, m_device->getCommandPool(), 1, &commandBuffer);
            return false;
        }
        
        functions.vkCmdPipelineBarrier(
            commandBuffer,
            sourceStage, destinationStage,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier
        );
        
        // End command buffer
        functions.vkEndCommandBuffer(commandBuffer);
        
        // Submit command buffer
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        
        result = functions.vkQueueSubmit(m_device->getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to submit layout transition command buffer");
            functions.vkFreeCommandBuffers(device, m_device->getCommandPool(), 1, &commandBuffer);
            return false;
        }
        
        // Wait for completion
        functions.vkQueueWaitIdle(m_device->getGraphicsQueue());
        
        // Free command buffer
        functions.vkFreeCommandBuffers(device, m_device->getCommandPool(), 1, &commandBuffer);
        
        // Update current layout
        m_currentLayout = newLayout;
        
        MR_LOG_DEBUG("Transitioned texture layout: " + m_desc.debugName);
        return true;
    }
    
    bool VulkanTexture::uploadInitialData() {
        const auto& functions = VulkanAPI::getFunctions();
        VkDevice device = m_device->getDevice();
        
        // Create staging buffer
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = m_desc.initialDataSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        VkBuffer stagingBuffer;
        VkResult result = functions.vkCreateBuffer(device, &bufferInfo, nullptr, &stagingBuffer);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to create staging buffer");
            return false;
        }
        
        // Allocate staging buffer memory
        VkMemoryRequirements memRequirements;
        functions.vkGetBufferMemoryRequirements(device, stagingBuffer, &memRequirements);
        
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, 
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        
        VkDeviceMemory stagingMemory;
        result = functions.vkAllocateMemory(device, &allocInfo, nullptr, &stagingMemory);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to allocate staging buffer memory");
            functions.vkDestroyBuffer(device, stagingBuffer, nullptr);
            return false;
        }
        
        functions.vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0);
        
        // Copy data to staging buffer
        void* data;
        functions.vkMapMemory(device, stagingMemory, 0, m_desc.initialDataSize, 0, &data);
        std::memcpy(data, m_desc.initialData, m_desc.initialDataSize);
        functions.vkUnmapMemory(device, stagingMemory);
        
        // Transition image layout: UNDEFINED -> TRANSFER_DST_OPTIMAL
        if (!transitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)) {
            functions.vkFreeMemory(device, stagingMemory, nullptr);
            functions.vkDestroyBuffer(device, stagingBuffer, nullptr);
            return false;
        }
        
        // Copy buffer to image
        VkCommandBufferAllocateInfo cmdAllocInfo{};
        cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAllocInfo.commandPool = m_device->getCommandPool();
        cmdAllocInfo.commandBufferCount = 1;
        
        VkCommandBuffer commandBuffer;
        functions.vkAllocateCommandBuffers(device, &cmdAllocInfo, &commandBuffer);
        
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        functions.vkBeginCommandBuffer(commandBuffer, &beginInfo);
        
        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {m_desc.width, m_desc.height, m_desc.depth};
        
        functions.vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, m_image, 
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        
        functions.vkEndCommandBuffer(commandBuffer);
        
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        
        functions.vkQueueSubmit(m_device->getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
        functions.vkQueueWaitIdle(m_device->getGraphicsQueue());
        
        functions.vkFreeCommandBuffers(device, m_device->getCommandPool(), 1, &commandBuffer);
        
        // Cleanup staging resources
        functions.vkFreeMemory(device, stagingMemory, nullptr);
        functions.vkDestroyBuffer(device, stagingBuffer, nullptr);
        
        // Generate mipmaps if needed (this also transitions to SHADER_READ_ONLY)
        if (m_desc.mipLevels > 1) {
            if (!generateMipmaps()) {
                MR_LOG_WARNING("Failed to generate mipmaps for texture: " + m_desc.debugName);
                // Fall back to single-level transition
                if (!transitionImageLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)) {
                    return false;
                }
            }
        } else {
            // No mipmaps, just transition to shader read
            if (!transitionImageLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)) {
                return false;
            }
        }
        
        MR_LOG_DEBUG("Uploaded initial data for texture: " + m_desc.debugName);
        return true;
    }
    
    bool VulkanTexture::generateMipmaps() {
        if (m_desc.mipLevels <= 1) {
            return true;  // No mipmaps to generate
        }
        
        const auto& functions = VulkanAPI::getFunctions();
        VkDevice device = m_device->getDevice();
        
        // Check if image format supports linear blitting
        VkFormatProperties formatProperties;
        functions.vkGetPhysicalDeviceFormatProperties(m_device->getPhysicalDevice(), m_format, &formatProperties);
        
        if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
            MR_LOG_WARNING("Texture format does not support linear blitting for mipmap generation: " + m_desc.debugName);
            return false;
        }
        
        // Allocate command buffer
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = m_device->getCommandPool();
        allocInfo.commandBufferCount = 1;
        
        VkCommandBuffer commandBuffer;
        functions.vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);
        
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        functions.vkBeginCommandBuffer(commandBuffer, &beginInfo);
        
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = m_image;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.subresourceRange.levelCount = 1;
        
        int32_t mipWidth = static_cast<int32_t>(m_desc.width);
        int32_t mipHeight = static_cast<int32_t>(m_desc.height);
        
        for (uint32 i = 1; i < m_desc.mipLevels; i++) {
            // Transition previous mip level to TRANSFER_SRC
            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            
            functions.vkCmdPipelineBarrier(commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                0, nullptr, 0, nullptr, 1, &barrier);
            
            // Blit from previous level to current level
            VkImageBlit blit{};
            blit.srcOffsets[0] = {0, 0, 0};
            blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel = i - 1;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount = 1;
            blit.dstOffsets[0] = {0, 0, 0};
            blit.dstOffsets[1] = {mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1};
            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel = i;
            blit.dstSubresource.baseArrayLayer = 0;
            blit.dstSubresource.layerCount = 1;
            
            functions.vkCmdBlitImage(commandBuffer,
                m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &blit, VK_FILTER_LINEAR);
            
            // Transition previous mip level to SHADER_READ_ONLY
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            
            functions.vkCmdPipelineBarrier(commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                0, nullptr, 0, nullptr, 1, &barrier);
            
            // Update dimensions for next level
            if (mipWidth > 1) mipWidth /= 2;
            if (mipHeight > 1) mipHeight /= 2;
        }
        
        // Transition last mip level to SHADER_READ_ONLY
        barrier.subresourceRange.baseMipLevel = m_desc.mipLevels - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        
        functions.vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
            0, nullptr, 0, nullptr, 1, &barrier);
        
        functions.vkEndCommandBuffer(commandBuffer);
        
        // Submit and wait
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        
        functions.vkQueueSubmit(m_device->getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
        functions.vkQueueWaitIdle(m_device->getGraphicsQueue());
        
        functions.vkFreeCommandBuffers(device, m_device->getCommandPool(), 1, &commandBuffer);
        
        m_currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        
        MR_LOG_DEBUG("Generated " + std::to_string(m_desc.mipLevels) + " mipmap levels for texture: " + m_desc.debugName);
        return true;
    }
}
