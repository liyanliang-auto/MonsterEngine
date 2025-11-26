#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanUtils.h"
#include "Core/Log.h"

namespace MonsterRender::RHI::Vulkan {
    
    VulkanBuffer::VulkanBuffer(VulkanDevice* device, const BufferDesc& desc)
        : IRHIBuffer(desc), m_device(device) {
        
        MR_ASSERT(m_device != nullptr);
        MR_ASSERT(desc.size > 0);
        
        MR_LOG_DEBUG("Creating Vulkan buffer: " + desc.debugName + 
                     " (size: " + std::to_string(desc.size) + " bytes)");
        
        if (!initialize()) {
            MR_LOG_ERROR("Failed to create Vulkan buffer: " + desc.debugName);
        }
    }
    
    VulkanBuffer::~VulkanBuffer() {
        destroy();
    }
    
    bool VulkanBuffer::initialize() {
        const auto& functions = VulkanAPI::getFunctions();
        VkDevice device = m_device->getDevice();
        
        // Setup buffer create info
        m_bufferCreateInfo = {};
        m_bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        m_bufferCreateInfo.size = m_desc.size;
        m_bufferCreateInfo.usage = VulkanUtils::getBufferUsageFlags(m_desc.usage);
        m_bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        // Create the buffer
        VkResult result = functions.vkCreateBuffer(device, &m_bufferCreateInfo, nullptr, &m_buffer);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to create Vulkan buffer: " + std::to_string(result));
            return false;
        }
        
        // Get memory requirements
        VkMemoryRequirements memRequirements;
        functions.vkGetBufferMemoryRequirements(device, m_buffer, &memRequirements);
        
        // Determine memory properties based on memory usage (UE5-style)
        // Reference: UE5 FVulkanResourceMultiBuffer::CreateBuffer
        // EMemoryUsage defines the intended memory access pattern:
        //   - Default: GPU-only, fastest for GPU access
        //   - Upload:  CPU-write, GPU-read (staging buffers, dynamic uniforms)
        //   - Readback: GPU-write, CPU-read (query results, screenshots)
        
        m_memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        bool needsCpuAccess = m_desc.cpuAccessible;
        
        // Infer CPU accessibility from memory usage
        switch (m_desc.memoryUsage) {
            case RHI::EMemoryUsage::Upload:
                // Upload buffers need HOST_VISIBLE for CPU writes
                m_memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
                needsCpuAccess = true;
                MR_LOG_DEBUG("Buffer using Upload memory: " + m_desc.debugName);
                break;
                
            case RHI::EMemoryUsage::Readback:
                // Readback buffers need HOST_VISIBLE and cached for efficient CPU reads
                m_memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | 
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                    VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
                needsCpuAccess = true;
                MR_LOG_DEBUG("Buffer using Readback memory: " + m_desc.debugName);
                break;
                
            case RHI::EMemoryUsage::Default:
            default:
                // Default is GPU-only unless cpuAccessible is explicitly set
                if (m_desc.cpuAccessible) {
                    m_memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
                } else {
                    m_memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                }
                break;
        }
        
        // Update descriptor to reflect actual CPU accessibility
        // This ensures map() will work correctly
        m_desc.cpuAccessible = needsCpuAccess;
        
        // Use FVulkanMemoryManager for allocation (UE5-style sub-allocation)
        auto* memoryManager = m_device->getMemoryManager();
        if (memoryManager) {
            FVulkanMemoryManager::FAllocationRequest request{};
            request.Size = memRequirements.size;
            request.Alignment = memRequirements.alignment;
            request.MemoryTypeBits = memRequirements.memoryTypeBits;
            request.RequiredFlags = m_memoryProperties;
            request.PreferredFlags = m_memoryProperties;
            request.bDedicated = false;  // Allow sub-allocation
            
            if (memoryManager->Allocate(request, m_allocation)) {
                m_deviceMemory = m_allocation.DeviceMemory;
                m_usesMemoryManager = true;
                
                // Bind buffer to allocated memory
                result = functions.vkBindBufferMemory(device, m_buffer, m_deviceMemory, m_allocation.Offset);
                if (result != VK_SUCCESS) {
                    MR_LOG_ERROR("Failed to bind buffer memory: " + std::to_string(result));
                    memoryManager->Free(m_allocation);
                    m_usesMemoryManager = false;
                    return false;
                }
                
                // Map memory if CPU accessible
                if (m_desc.cpuAccessible && !m_allocation.MappedPointer) {
                    result = functions.vkMapMemory(device, m_deviceMemory, m_allocation.Offset, m_allocation.Size, 0, &m_mappedData);
                    if (result != VK_SUCCESS) {
                        MR_LOG_ERROR("Failed to map buffer memory: " + std::to_string(result));
                        return false;
                    }
                    m_persistentMapped = true;
                } else if (m_allocation.MappedPointer) {
                    m_mappedData = m_allocation.MappedPointer;
                    m_persistentMapped = true;
                }
                
                MR_LOG_DEBUG("Successfully created Vulkan buffer with managed memory: " + m_desc.debugName);
                return true;
            }
        }
        
        // Fallback to direct allocation (old path)
        MR_LOG_WARNING("FVulkanMemoryManager not available, using direct allocation for: " + m_desc.debugName);
        
        m_memoryAllocateInfo = {};
        m_memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        m_memoryAllocateInfo.allocationSize = memRequirements.size;
        m_memoryAllocateInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, m_memoryProperties);
        
        result = functions.vkAllocateMemory(device, &m_memoryAllocateInfo, nullptr, &m_deviceMemory);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to allocate buffer memory: " + std::to_string(result));
            return false;
        }
        
        result = functions.vkBindBufferMemory(device, m_buffer, m_deviceMemory, 0);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to bind buffer memory: " + std::to_string(result));
            return false;
        }
        
        if (m_desc.cpuAccessible) {
            result = functions.vkMapMemory(device, m_deviceMemory, 0, m_desc.size, 0, &m_mappedData);
            if (result != VK_SUCCESS) {
                MR_LOG_ERROR("Failed to map buffer memory: " + std::to_string(result));
                return false;
            }
            m_persistentMapped = true;
        }
        
        MR_LOG_DEBUG("Successfully created Vulkan buffer: " + m_desc.debugName);
        return true;
    }
    
    void VulkanBuffer::destroy() {
        if (m_device) {
            const auto& functions = VulkanAPI::getFunctions();
            VkDevice device = m_device->getDevice();
            
            // Destroy buffer first
            if (m_buffer != VK_NULL_HANDLE) {
                functions.vkDestroyBuffer(device, m_buffer, nullptr);
                m_buffer = VK_NULL_HANDLE;
            }
            
            // Free memory using memory manager if applicable
            if (m_usesMemoryManager) {
                auto* memoryManager = m_device->getMemoryManager();
                if (memoryManager && m_allocation.DeviceMemory != VK_NULL_HANDLE) {
                    // Unmap if manually mapped (manager doesn't track this)
                    if (m_mappedData != nullptr && !m_allocation.MappedPointer) {
                        functions.vkUnmapMemory(device, m_deviceMemory);
                    }
                    
                    memoryManager->Free(m_allocation);
                    m_mappedData = nullptr;
                    m_persistentMapped = false;
                    m_usesMemoryManager = false;
                }
            } else {
                // Direct allocation path
                if (m_mappedData != nullptr) {
                    functions.vkUnmapMemory(device, m_deviceMemory);
                    m_mappedData = nullptr;
                    m_persistentMapped = false;
                }
                
                if (m_deviceMemory != VK_NULL_HANDLE) {
                    functions.vkFreeMemory(device, m_deviceMemory, nullptr);
                    m_deviceMemory = VK_NULL_HANDLE;
                }
            }
        }
    }
    
    void* VulkanBuffer::map() {
        if (!m_desc.cpuAccessible) {
            MR_LOG_ERROR("Buffer is not CPU accessible: " + m_desc.debugName);
            return nullptr;
        }
        
        if (m_persistentMapped) {
            return m_mappedData;
        }
        
        const auto& functions = VulkanAPI::getFunctions();
        VkDevice device = m_device->getDevice();
        
        void* data = nullptr;
        VkResult result = functions.vkMapMemory(device, m_deviceMemory, 0, m_desc.size, 0, &data);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to map buffer memory: " + std::to_string(result));
            return nullptr;
        }
        
        return data;
    }
    
    void VulkanBuffer::unmap() {
        if (!m_desc.cpuAccessible) {
            MR_LOG_ERROR("Buffer is not CPU accessible: " + m_desc.debugName);
            return;
        }
        
        // Don't unmap persistent mappings
        if (m_persistentMapped) {
            return;
        }
        
        const auto& functions = VulkanAPI::getFunctions();
        VkDevice device = m_device->getDevice();
        functions.vkUnmapMemory(device, m_deviceMemory);
    }
    
    uint32 VulkanBuffer::findMemoryType(uint32 typeFilter, VkMemoryPropertyFlags properties) {
        return VulkanUtils::findMemoryType(m_device->getMemoryProperties(), typeFilter, properties);
    }
}
