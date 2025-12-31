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
}
