#pragma once

#include "Core/CoreMinimal.h"
#include "RHI/IRHIResource.h"
#include "RHI/RHIResources.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include "Platform/Vulkan/FVulkanMemoryManager.h"

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
        
        // IRHIResource interface - backend identification
        ERHIBackend getBackendType() const override { return ERHIBackend::Vulkan; }
        
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
        
        // Memory manager allocation (UE5-style)
        FVulkanAllocation m_allocation{};
        bool m_usesMemoryManager = false;
        
        // Creation info
        VkBufferCreateInfo m_bufferCreateInfo{};
        VkMemoryAllocateInfo m_memoryAllocateInfo{};
        VkMemoryPropertyFlags m_memoryProperties = 0;
    };
    
    // ============================================================================
    // FVulkanStagingBuffer - Staging Buffer for GPU Upload
    // ============================================================================
    
    /**
     * @class FVulkanStagingBuffer
     * @brief Staging buffer for uploading data to device-local memory
     * 
     * Used for efficient data transfer from CPU to GPU.
     * Creates a host-visible buffer for staging, then copies to device-local memory.
     * 
     * Reference UE5: FVulkanStagingBuffer
     */
    class FVulkanStagingBuffer {
    public:
        /**
         * Constructor
         * @param InDevice Vulkan device
         * @param InSize Buffer size in bytes
         */
        FVulkanStagingBuffer(VulkanDevice* InDevice, uint32 InSize);
        
        ~FVulkanStagingBuffer();
        
        // Non-copyable, non-movable
        FVulkanStagingBuffer(const FVulkanStagingBuffer&) = delete;
        FVulkanStagingBuffer& operator=(const FVulkanStagingBuffer&) = delete;
        
        /**
         * Map the staging buffer for writing
         * @return Pointer to mapped memory, or nullptr on failure
         */
        void* Map();
        
        /**
         * Unmap the staging buffer
         */
        void Unmap();
        
        /**
         * Copy data from staging buffer to destination buffer
         * @param DstBuffer Destination buffer
         * @param SrcOffset Offset in staging buffer
         * @param DstOffset Offset in destination buffer
         * @param Size Size of data to copy
         * @return true if successful
         */
        bool CopyToBuffer(VkBuffer DstBuffer, uint32 SrcOffset, uint32 DstOffset, uint32 Size);
        
        /**
         * Copy data from staging buffer to destination image
         * @param DstImage Destination image
         * @param Width Image width
         * @param Height Image height
         * @param MipLevel Mip level
         * @return true if successful
         */
        bool CopyToImage(VkImage DstImage, uint32 Width, uint32 Height, uint32 MipLevel = 0);
        
        // Accessors
        VkBuffer GetBuffer() const { return m_buffer; }
        uint32 GetSize() const { return m_size; }
        bool IsValid() const { return m_buffer != VK_NULL_HANDLE; }
        
    private:
        bool CreateBuffer();
        void DestroyBuffer();
        
    private:
        VulkanDevice* m_device;
        uint32 m_size;
        VkBuffer m_buffer = VK_NULL_HANDLE;
        VkDeviceMemory m_deviceMemory = VK_NULL_HANDLE;
        void* m_mappedData = nullptr;
        
        // Memory manager allocation
        FVulkanAllocation m_allocation{};
        bool m_usesMemoryManager = false;
    };
    
    // ============================================================================
    // FVulkanVertexBuffer - Vulkan Vertex Buffer Implementation
    // ============================================================================
    
    /**
     * @class FVulkanVertexBuffer
     * @brief Vulkan implementation of vertex buffer
     * 
     * Extends FRHIVertexBuffer with Vulkan-specific functionality.
     * Manages GPU memory allocation and data upload for vertex data.
     * 
     * Reference UE5: FVulkanVertexBuffer
     */
    class FVulkanVertexBuffer : public FRHIVertexBuffer {
    public:
        /**
         * Constructor
         * @param InDevice Vulkan device
         * @param InSize Buffer size in bytes
         * @param InStride Vertex stride in bytes
         * @param InUsage Buffer usage flags
         */
        FVulkanVertexBuffer(VulkanDevice* InDevice, uint32 InSize, uint32 InStride, EBufferUsageFlags InUsage);
        
        virtual ~FVulkanVertexBuffer();
        
        // Non-copyable, non-movable
        FVulkanVertexBuffer(const FVulkanVertexBuffer&) = delete;
        FVulkanVertexBuffer& operator=(const FVulkanVertexBuffer&) = delete;
        
        // FRHIBuffer interface
        virtual void* Lock(uint32 Offset, uint32 InSize) override;
        virtual void Unlock() override;
        
        /**
         * Initialize the buffer with optional initial data
         * @param InitialData Initial data to upload (can be nullptr)
         * @param DataSize Size of initial data
         * @return true if successful
         */
        bool Initialize(const void* InitialData = nullptr, uint32 DataSize = 0);
        
        // Vulkan-specific accessors
        VkBuffer GetVkBuffer() const { return m_buffer; }
        VkDeviceMemory GetDeviceMemory() const { return m_deviceMemory; }
        bool IsValid() const { return m_buffer != VK_NULL_HANDLE; }
        
        /** Get buffer usage flags */
        EBufferUsageFlags GetUsageFlags() const { return m_usageFlags; }
        
    private:
        bool CreateBuffer();
        void DestroyBuffer();
        uint32 FindMemoryType(uint32 TypeFilter, VkMemoryPropertyFlags Properties);
        
    private:
        VulkanDevice* m_device;
        VkBuffer m_buffer = VK_NULL_HANDLE;
        VkDeviceMemory m_deviceMemory = VK_NULL_HANDLE;
        void* m_mappedData = nullptr;
        EBufferUsageFlags m_usageFlags;
        
        // Memory manager allocation (UE5-style)
        FVulkanAllocation m_allocation{};
        bool m_usesMemoryManager = false;
    };
    
    // ============================================================================
    // FVulkanIndexBuffer - Vulkan Index Buffer Implementation
    // ============================================================================
    
    /**
     * @class FVulkanIndexBuffer
     * @brief Vulkan implementation of index buffer
     * 
     * Extends FRHIIndexBuffer with Vulkan-specific functionality.
     * Supports both 16-bit and 32-bit indices.
     * 
     * Reference UE5: FVulkanIndexBuffer
     */
    class FVulkanIndexBuffer : public FRHIIndexBuffer {
    public:
        /**
         * Constructor
         * @param InDevice Vulkan device
         * @param InStride Index stride (2 for 16-bit, 4 for 32-bit)
         * @param InSize Buffer size in bytes
         * @param InUsage Buffer usage flags
         */
        FVulkanIndexBuffer(VulkanDevice* InDevice, uint32 InStride, uint32 InSize, EBufferUsageFlags InUsage);
        
        virtual ~FVulkanIndexBuffer();
        
        // Non-copyable, non-movable
        FVulkanIndexBuffer(const FVulkanIndexBuffer&) = delete;
        FVulkanIndexBuffer& operator=(const FVulkanIndexBuffer&) = delete;
        
        // FRHIBuffer interface
        virtual void* Lock(uint32 Offset, uint32 InSize) override;
        virtual void Unlock() override;
        
        /**
         * Initialize the buffer with optional initial data
         * @param InitialData Initial data to upload (can be nullptr)
         * @param DataSize Size of initial data
         * @return true if successful
         */
        bool Initialize(const void* InitialData = nullptr, uint32 DataSize = 0);
        
        // Vulkan-specific accessors
        VkBuffer GetVkBuffer() const { return m_buffer; }
        VkDeviceMemory GetDeviceMemory() const { return m_deviceMemory; }
        bool IsValid() const { return m_buffer != VK_NULL_HANDLE; }
        
        /** Get the Vulkan index type */
        VkIndexType GetVkIndexType() const { return Is32Bit() ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16; }
        
        /** Get buffer usage flags */
        EBufferUsageFlags GetUsageFlags() const { return m_usageFlags; }
        
    private:
        bool CreateBuffer();
        void DestroyBuffer();
        uint32 FindMemoryType(uint32 TypeFilter, VkMemoryPropertyFlags Properties);
        
    private:
        VulkanDevice* m_device;
        VkBuffer m_buffer = VK_NULL_HANDLE;
        VkDeviceMemory m_deviceMemory = VK_NULL_HANDLE;
        void* m_mappedData = nullptr;
        EBufferUsageFlags m_usageFlags;
        
        // Memory manager allocation (UE5-style)
        FVulkanAllocation m_allocation{};
        bool m_usesMemoryManager = false;
    };
}
