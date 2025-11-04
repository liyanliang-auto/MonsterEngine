#pragma once

#include "Core/CoreMinimal.h"
#include "RHI/IRHIResource.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include "Platform/Vulkan/FVulkanMemoryManager.h"

namespace MonsterRender::RHI::Vulkan {
    
    // Forward declarations
    class VulkanDevice;
    
    /**
     * Vulkan implementation of texture resource
     */
    class VulkanTexture : public IRHITexture {
    public:
        VulkanTexture(VulkanDevice* device, const TextureDesc& desc);
        virtual ~VulkanTexture();
        
        // Non-copyable, non-movable
        VulkanTexture(const VulkanTexture&) = delete;
        VulkanTexture& operator=(const VulkanTexture&) = delete;
        VulkanTexture(VulkanTexture&&) = delete;
        VulkanTexture& operator=(VulkanTexture&&) = delete;
        
        // Vulkan-specific interface
        VkImage getImage() const { return m_image; }
        VkImageView getImageView() const { return m_imageView; }
        VkDeviceMemory getDeviceMemory() const { return m_deviceMemory; }
        VkFormat getVulkanFormat() const { return m_format; }
        VkImageLayout getCurrentLayout() const { return m_currentLayout; }
        
        // Layout management
        void setCurrentLayout(VkImageLayout layout) { m_currentLayout = layout; }
        
        // Validation and debugging
        bool isValid() const { return m_image != VK_NULL_HANDLE; }
        
    private:
        bool initialize();
        void destroy();
        uint32 findMemoryType(uint32 typeFilter, VkMemoryPropertyFlags properties);
        bool createImageView();
        
    private:
        VulkanDevice* m_device;
        VkImage m_image = VK_NULL_HANDLE;
        VkImageView m_imageView = VK_NULL_HANDLE;
        VkDeviceMemory m_deviceMemory = VK_NULL_HANDLE;
        VkFormat m_format = VK_FORMAT_UNDEFINED;
        VkImageLayout m_currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        
        // Memory manager allocation (UE5-style)
        FVulkanAllocation m_allocation{};
        bool m_usesMemoryManager = false;
        
        // Creation info
        VkImageCreateInfo m_imageCreateInfo{};
        VkMemoryAllocateInfo m_memoryAllocateInfo{};
        VkImageViewCreateInfo m_imageViewCreateInfo{};
    };
}
