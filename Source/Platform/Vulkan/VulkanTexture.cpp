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
        
        // Setup memory allocation info
        m_memoryAllocateInfo = {};
        m_memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        m_memoryAllocateInfo.allocationSize = memRequirements.size;
        m_memoryAllocateInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        
        // Allocate memory
        result = functions.vkAllocateMemory(device, &m_memoryAllocateInfo, nullptr, &m_deviceMemory);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to allocate image memory: " + std::to_string(result));
            return false;
        }
        
        // Bind image to memory
        result = functions.vkBindImageMemory(device, m_image, m_deviceMemory, 0);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to bind image memory: " + std::to_string(result));
            return false;
        }
        
        // Create image view
        if (!createImageView()) {
            return false;
        }
        
        MR_LOG_DEBUG("Successfully created Vulkan texture: " + m_desc.debugName);
        return true;
    }
    
    void VulkanTexture::destroy() {
        if (m_device) {
            const auto& functions = VulkanAPI::getFunctions();
            VkDevice device = m_device->getDevice();
            
            // Destroy image view
            if (m_imageView != VK_NULL_HANDLE) {
                functions.vkDestroyImageView(device, m_imageView, nullptr);
                m_imageView = VK_NULL_HANDLE;
            }
            
            // Free device memory
            if (m_deviceMemory != VK_NULL_HANDLE) {
                functions.vkFreeMemory(device, m_deviceMemory, nullptr);
                m_deviceMemory = VK_NULL_HANDLE;
            }
            
            // Destroy image
            if (m_image != VK_NULL_HANDLE) {
                functions.vkDestroyImage(device, m_image, nullptr);
                m_image = VK_NULL_HANDLE;
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
        
        // Determine aspect mask
        VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        if (hasResourceUsage(m_desc.usage, EResourceUsage::DepthStencil)) {
            aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            // Add stencil aspect for formats that have it
            if (m_desc.format == EPixelFormat::D24_UNORM_S8_UINT) {
                aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
            }
        }
        
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
        
        return true;
    }
    
    uint32 VulkanTexture::findMemoryType(uint32 typeFilter, VkMemoryPropertyFlags properties) {
        return VulkanUtils::findMemoryType(m_device->getMemoryProperties(), typeFilter, properties);
    }
}
