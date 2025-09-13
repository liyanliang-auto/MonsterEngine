#pragma once

#include "Core/CoreMinimal.h"
#include "RHI/IRHIResource.h"
#include "Platform/Vulkan/VulkanRHI.h"

namespace MonsterRender::RHI::Vulkan {
    
    // Forward declarations
    class VulkanDevice;
    
    /**
     * Vulkan implementation of buffer resource
     */
    class VulkanBuffer : public IRHIBuffer {
    public:
        VulkanBuffer(VulkanDevice* device, const BufferDesc& desc);
        virtual ~VulkanBuffer();
        
        // Non-copyable, non-movable
        VulkanBuffer(const VulkanBuffer&) = delete;
        VulkanBuffer& operator=(const VulkanBuffer&) = delete;
        VulkanBuffer(VulkanBuffer&&) = delete;
        VulkanBuffer& operator=(VulkanBuffer&&) = delete;
        
        // IRHIBuffer interface
        void* map() override;
        void unmap() override;
        
        // Vulkan-specific interface
        VkBuffer getBuffer() const { return m_buffer; }
        VkDeviceMemory getDeviceMemory() const { return m_deviceMemory; }
        VkDeviceSize getOffset() const { return m_offset; }
        
        // Validation and debugging
        bool isValid() const { return m_buffer != VK_NULL_HANDLE; }
        
    private:
        bool initialize();
        void destroy();
        uint32 findMemoryType(uint32 typeFilter, VkMemoryPropertyFlags properties);
        
    private:
        VulkanDevice* m_device;
        VkBuffer m_buffer = VK_NULL_HANDLE;
        VkDeviceMemory m_deviceMemory = VK_NULL_HANDLE;
        VkDeviceSize m_offset = 0;
        void* m_mappedData = nullptr;
        bool m_persistentMapped = false;
        
        // Creation info
        VkBufferCreateInfo m_bufferCreateInfo{};
        VkMemoryAllocateInfo m_memoryAllocateInfo{};
        VkMemoryPropertyFlags m_memoryProperties = 0;
    };
}
