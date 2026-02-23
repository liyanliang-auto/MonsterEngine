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

                

                // Bind buffer to allocated memory at the sub-allocation offset.

                // After binding, the buffer starts at m_allocation.Offset in device memory.

                // When calling vkCmdBindVertexBuffers, offset should be 0 relative to buffer.

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

                

                // Upload initial data if provided

                if (m_desc.initialData && m_desc.initialDataSize > 0) {

                    if (m_desc.cpuAccessible && m_mappedData) {

                        // Direct copy for CPU-accessible buffers

                        memcpy(m_mappedData, m_desc.initialData, m_desc.initialDataSize);

                        MR_LOG_DEBUG("Copied initial data directly to mapped buffer: " + m_desc.debugName);

                    } else {

                        // Use staging buffer for GPU-only buffers

                        if (!uploadInitialData(m_desc.initialData, m_desc.initialDataSize)) {

                            MR_LOG_ERROR("Failed to upload initial data to buffer: " + m_desc.debugName);

                            return false;

                        }

                    }

                }

                

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

        

        // Upload initial data if provided (fallback path)

        if (m_desc.initialData && m_desc.initialDataSize > 0) {

            if (m_desc.cpuAccessible && m_mappedData) {

                // Direct copy for CPU-accessible buffers

                memcpy(m_mappedData, m_desc.initialData, m_desc.initialDataSize);

                MR_LOG_DEBUG("Copied initial data directly to mapped buffer: " + m_desc.debugName);

            } else {

                // Use staging buffer for GPU-only buffers

                if (!uploadInitialData(m_desc.initialData, m_desc.initialDataSize)) {

                    MR_LOG_ERROR("Failed to upload initial data to buffer: " + m_desc.debugName);

                    return false;

                }

            }

        }

        

        return true;

    }

    

    void VulkanBuffer::destroy() {

        if (m_device) {

            const auto& functions = VulkanAPI::getFunctions();

            VkDevice device = m_device->getDevice();

            

            // Unmap memory first if mapped

            if (m_mappedData != nullptr) {

                if (m_usesMemoryManager) {

                    if (!m_allocation.MappedPointer) {

                        functions.vkUnmapMemory(device, m_deviceMemory);

                    }

                } else {

                    functions.vkUnmapMemory(device, m_deviceMemory);

                }

                m_mappedData = nullptr;

                m_persistentMapped = false;

            }

            

            // Use deferred destruction to avoid destroying resources still in use by GPU

            if (m_usesMemoryManager) {

                auto* memoryManager = m_device->getMemoryManager();

                if (memoryManager && m_allocation.DeviceMemory != VK_NULL_HANDLE) {

                    // For memory manager allocations, defer buffer destruction only

                    // Memory manager handles its own memory lifecycle

                    if (m_buffer != VK_NULL_HANDLE) {

                        m_device->deferBufferDestruction(m_buffer, VK_NULL_HANDLE);

                        m_buffer = VK_NULL_HANDLE;

                    }

                    memoryManager->Free(m_allocation);

                    m_usesMemoryManager = false;

                }

            } else {

                // Direct allocation path - defer both buffer and memory destruction

                if (m_buffer != VK_NULL_HANDLE || m_deviceMemory != VK_NULL_HANDLE) {

                    m_device->deferBufferDestruction(m_buffer, m_deviceMemory);

                    m_buffer = VK_NULL_HANDLE;

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

    

    bool VulkanBuffer::uploadInitialData(const void* data, uint32 size) {

        if (!data || size == 0) {

            return true;  // Nothing to upload

        }

        

        if (!isValid()) {

            MR_LOG_ERROR("Cannot upload data to invalid buffer: " + m_desc.debugName);

            return false;

        }

        

        MR_LOG_DEBUG("Uploading " + std::to_string(size) + " bytes to buffer: " + m_desc.debugName);

        

        // Create staging buffer

        FVulkanStagingBuffer stagingBuffer(m_device, size);

        if (!stagingBuffer.IsValid()) {

            MR_LOG_ERROR("Failed to create staging buffer for: " + m_desc.debugName);

            return false;

        }

        

        // Map staging buffer and copy data

        void* mappedData = stagingBuffer.Map();

        if (!mappedData) {

            MR_LOG_ERROR("Failed to map staging buffer for: " + m_desc.debugName);

            return false;

        }

        

        memcpy(mappedData, data, size);

        stagingBuffer.Unmap();

        

        // Copy from staging buffer to GPU buffer

        if (!stagingBuffer.CopyToBuffer(m_buffer, 0, 0, size)) {

            MR_LOG_ERROR("Failed to copy staging buffer to GPU buffer: " + m_desc.debugName);

            return false;

        }

        

        MR_LOG_DEBUG("Successfully uploaded initial data to buffer: " + m_desc.debugName);

        return true;

    }

    

    // ============================================================================

    // FVulkanStagingBuffer Implementation

    // ============================================================================

    

    FVulkanStagingBuffer::FVulkanStagingBuffer(VulkanDevice* InDevice, uint32 InSize)

        : m_device(InDevice)

        , m_size(InSize)

    {

        MR_ASSERT(m_device != nullptr);

        MR_ASSERT(InSize > 0);

        

        if (!CreateBuffer()) {

            MR_LOG_ERROR("FVulkanStagingBuffer: Failed to create staging buffer");

        }

    }

    

    FVulkanStagingBuffer::~FVulkanStagingBuffer()

    {

        DestroyBuffer();

    }

    

    bool FVulkanStagingBuffer::CreateBuffer()

    {

        const auto& functions = VulkanAPI::getFunctions();

        VkDevice device = m_device->getDevice();

        

        // Create staging buffer with transfer source usage

        VkBufferCreateInfo bufferCreateInfo{};

        bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;

        bufferCreateInfo.size = m_size;

        bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        

        VkResult result = functions.vkCreateBuffer(device, &bufferCreateInfo, nullptr, &m_buffer);

        if (result != VK_SUCCESS) {

            MR_LOG_ERROR("FVulkanStagingBuffer: Failed to create buffer: " + std::to_string(result));

            return false;

        }

        

        // Get memory requirements

        VkMemoryRequirements memRequirements;

        functions.vkGetBufferMemoryRequirements(device, m_buffer, &memRequirements);

        

        // Staging buffer requires host-visible, host-coherent memory

        VkMemoryPropertyFlags memoryProperties = 

            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

        

        // Try memory manager first

        auto* memoryManager = m_device->getMemoryManager();

        if (memoryManager) {

            FVulkanMemoryManager::FAllocationRequest request{};

            request.Size = memRequirements.size;

            request.Alignment = memRequirements.alignment;

            request.MemoryTypeBits = memRequirements.memoryTypeBits;

            request.RequiredFlags = memoryProperties;

            request.PreferredFlags = memoryProperties;

            request.bDedicated = false;

            

            if (memoryManager->Allocate(request, m_allocation)) {

                m_deviceMemory = m_allocation.DeviceMemory;

                m_usesMemoryManager = true;

                

                result = functions.vkBindBufferMemory(device, m_buffer, m_deviceMemory, m_allocation.Offset);

                if (result != VK_SUCCESS) {

                    MR_LOG_ERROR("FVulkanStagingBuffer: Failed to bind memory: " + std::to_string(result));

                    memoryManager->Free(m_allocation);

                    m_usesMemoryManager = false;

                    return false;

                }

                

                MR_LOG_DEBUG("FVulkanStagingBuffer: Created with managed memory, size: " + std::to_string(m_size));

                return true;

            }

        }

        

        // Fallback to direct allocation

        VkMemoryAllocateInfo allocInfo{};

        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;

        allocInfo.allocationSize = memRequirements.size;

        allocInfo.memoryTypeIndex = VulkanUtils::findMemoryType(

            m_device->getMemoryProperties(), 

            memRequirements.memoryTypeBits, 

            memoryProperties);

        

        result = functions.vkAllocateMemory(device, &allocInfo, nullptr, &m_deviceMemory);

        if (result != VK_SUCCESS) {

            MR_LOG_ERROR("FVulkanStagingBuffer: Failed to allocate memory: " + std::to_string(result));

            return false;

        }

        

        result = functions.vkBindBufferMemory(device, m_buffer, m_deviceMemory, 0);

        if (result != VK_SUCCESS) {

            MR_LOG_ERROR("FVulkanStagingBuffer: Failed to bind memory: " + std::to_string(result));

            return false;

        }

        

        MR_LOG_DEBUG("FVulkanStagingBuffer: Created with direct allocation, size: " + std::to_string(m_size));

        return true;

    }

    

    void FVulkanStagingBuffer::DestroyBuffer()

    {

        if (m_device) {

            const auto& functions = VulkanAPI::getFunctions();

            VkDevice device = m_device->getDevice();

            

            if (m_mappedData) {

                Unmap();

            }

            

            if (m_buffer != VK_NULL_HANDLE) {

                functions.vkDestroyBuffer(device, m_buffer, nullptr);

                m_buffer = VK_NULL_HANDLE;

            }

            

            if (m_usesMemoryManager) {

                auto* memoryManager = m_device->getMemoryManager();

                if (memoryManager && m_allocation.DeviceMemory != VK_NULL_HANDLE) {

                    memoryManager->Free(m_allocation);

                }

            } else if (m_deviceMemory != VK_NULL_HANDLE) {

                functions.vkFreeMemory(device, m_deviceMemory, nullptr);

            }

            

            m_deviceMemory = VK_NULL_HANDLE;

        }

    }

    

    void* FVulkanStagingBuffer::Map()

    {

        if (m_mappedData) {

            return m_mappedData;

        }

        

        const auto& functions = VulkanAPI::getFunctions();

        VkDevice device = m_device->getDevice();

        

        VkDeviceSize offset = m_usesMemoryManager ? m_allocation.Offset : 0;

        VkResult result = functions.vkMapMemory(device, m_deviceMemory, offset, m_size, 0, &m_mappedData);

        

        if (result != VK_SUCCESS) {

            MR_LOG_ERROR("FVulkanStagingBuffer: Failed to map memory: " + std::to_string(result));

            return nullptr;

        }

        

        return m_mappedData;

    }

    

    void FVulkanStagingBuffer::Unmap()

    {

        if (!m_mappedData) {

            return;

        }

        

        const auto& functions = VulkanAPI::getFunctions();

        VkDevice device = m_device->getDevice();

        

        functions.vkUnmapMemory(device, m_deviceMemory);

        m_mappedData = nullptr;

    }

    

    bool FVulkanStagingBuffer::CopyToBuffer(VkBuffer DstBuffer, uint32 SrcOffset, uint32 DstOffset, uint32 Size)

    {

        if (!IsValid() || DstBuffer == VK_NULL_HANDLE) {

            return false;

        }

        

        const auto& functions = VulkanAPI::getFunctions();

        VkDevice device = m_device->getDevice();

        

        // Get command pool and create one-time command buffer

        VkCommandPool commandPool = m_device->getCommandPool();

        

        VkCommandBufferAllocateInfo allocInfo{};

        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;

        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

        allocInfo.commandPool = commandPool;

        allocInfo.commandBufferCount = 1;

        

        VkCommandBuffer commandBuffer;

        VkResult result = functions.vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

        if (result != VK_SUCCESS) {

            MR_LOG_ERROR("FVulkanStagingBuffer: Failed to allocate command buffer");

            return false;

        }

        

        // Begin command buffer

        VkCommandBufferBeginInfo beginInfo{};

        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        

        functions.vkBeginCommandBuffer(commandBuffer, &beginInfo);

        

        // Copy buffer

        VkBufferCopy copyRegion{};

        copyRegion.srcOffset = SrcOffset;

        copyRegion.dstOffset = DstOffset;

        copyRegion.size = Size;

        

        functions.vkCmdCopyBuffer(commandBuffer, m_buffer, DstBuffer, 1, &copyRegion);

        

        // End command buffer

        functions.vkEndCommandBuffer(commandBuffer);

        

        // Submit and wait

        VkSubmitInfo submitInfo{};

        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        submitInfo.commandBufferCount = 1;

        submitInfo.pCommandBuffers = &commandBuffer;

        

        VkQueue graphicsQueue = m_device->getGraphicsQueue();

        functions.vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);

        functions.vkQueueWaitIdle(graphicsQueue);

        

        // Free command buffer

        functions.vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);

        

        MR_LOG_DEBUG("FVulkanStagingBuffer: Copied " + std::to_string(Size) + " bytes to buffer");

        return true;

    }

    

    bool FVulkanStagingBuffer::CopyToImage(VkImage DstImage, uint32 Width, uint32 Height, uint32 MipLevel)

    {

        if (!IsValid() || DstImage == VK_NULL_HANDLE) {

            return false;

        }

        

        const auto& functions = VulkanAPI::getFunctions();

        VkDevice device = m_device->getDevice();

        

        // Get command pool and create one-time command buffer

        VkCommandPool commandPool = m_device->getCommandPool();

        

        VkCommandBufferAllocateInfo allocInfo{};

        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;

        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

        allocInfo.commandPool = commandPool;

        allocInfo.commandBufferCount = 1;

        

        VkCommandBuffer commandBuffer;

        VkResult result = functions.vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

        if (result != VK_SUCCESS) {

            MR_LOG_ERROR("FVulkanStagingBuffer: Failed to allocate command buffer for image copy");

            return false;

        }

        

        // Begin command buffer

        VkCommandBufferBeginInfo beginInfo{};

        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        

        functions.vkBeginCommandBuffer(commandBuffer, &beginInfo);

        

        // Copy buffer to image

        VkBufferImageCopy region{};

        region.bufferOffset = 0;

        region.bufferRowLength = 0;

        region.bufferImageHeight = 0;

        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

        region.imageSubresource.mipLevel = MipLevel;

        region.imageSubresource.baseArrayLayer = 0;

        region.imageSubresource.layerCount = 1;

        region.imageOffset = {0, 0, 0};

        region.imageExtent = {Width, Height, 1};

        

        functions.vkCmdCopyBufferToImage(

            commandBuffer, m_buffer, DstImage, 

            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        

        // End command buffer

        functions.vkEndCommandBuffer(commandBuffer);

        

        // Submit and wait

        VkSubmitInfo submitInfo{};

        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        submitInfo.commandBufferCount = 1;

        submitInfo.pCommandBuffers = &commandBuffer;

        

        VkQueue graphicsQueue = m_device->getGraphicsQueue();

        functions.vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);

        functions.vkQueueWaitIdle(graphicsQueue);

        

        // Free command buffer

        functions.vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);

        

        MR_LOG_DEBUG("FVulkanStagingBuffer: Copied to image " + std::to_string(Width) + "x" + std::to_string(Height));

        return true;

    }

    

    // ============================================================================

    // FVulkanVertexBuffer Implementation

    // ============================================================================

    

    FVulkanVertexBuffer::FVulkanVertexBuffer(VulkanDevice* InDevice, uint32 InSize, uint32 InStride, EBufferUsageFlags InUsage)

        : FRHIVertexBuffer(InSize, InStride)

        , m_device(InDevice)

        , m_usageFlags(InUsage)

    {

        MR_ASSERT(m_device != nullptr);

        MR_ASSERT(InSize > 0);

    }

    

    FVulkanVertexBuffer::~FVulkanVertexBuffer()

    {

        DestroyBuffer();

    }

    

    bool FVulkanVertexBuffer::Initialize(const void* InitialData, uint32 DataSize)

    {

        // Create the Vulkan buffer

        if (!CreateBuffer()) {

            return false;

        }

        

        // Upload initial data if provided

        if (InitialData && DataSize > 0) {

            void* MappedData = Lock(0, DataSize);

            if (MappedData) {

                memcpy(MappedData, InitialData, DataSize);

                Unlock();

                MR_LOG_DEBUG("Uploaded " + std::to_string(DataSize) + " bytes to vertex buffer");

            } else {

                MR_LOG_ERROR("Failed to map vertex buffer for initial data upload");

                return false;

            }

        }

        

        return true;

    }

    

    bool FVulkanVertexBuffer::CreateBuffer()

    {

        const auto& functions = VulkanAPI::getFunctions();

        VkDevice device = m_device->getDevice();

        

        // Setup buffer create info

        VkBufferCreateInfo bufferCreateInfo{};

        bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;

        bufferCreateInfo.size = Size;

        bufferCreateInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        

        // Create the buffer

        VkResult result = functions.vkCreateBuffer(device, &bufferCreateInfo, nullptr, &m_buffer);

        if (result != VK_SUCCESS) {

            MR_LOG_ERROR("Failed to create vertex buffer: " + std::to_string(result));

            return false;

        }

        

        // Get memory requirements

        VkMemoryRequirements memRequirements;

        functions.vkGetBufferMemoryRequirements(device, m_buffer, &memRequirements);

        

        // Determine memory properties based on usage flags

        VkMemoryPropertyFlags memoryProperties;

        bool isDynamic = EnumHasAnyFlags(m_usageFlags, EBufferUsageFlags::Dynamic | EBufferUsageFlags::Volatile);

        

        if (isDynamic || EnumHasAnyFlags(m_usageFlags, EBufferUsageFlags::KeepCPUAccessible)) {

            // Dynamic buffers need CPU access

            memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

        } else {

            // Static buffers prefer device local memory

            memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

            // TODO: For truly static buffers, use staging buffer and device local memory

        }

        

        // Use memory manager for allocation

        auto* memoryManager = m_device->getMemoryManager();

        if (memoryManager) {

            FVulkanMemoryManager::FAllocationRequest request{};

            request.Size = memRequirements.size;

            request.Alignment = memRequirements.alignment;

            request.MemoryTypeBits = memRequirements.memoryTypeBits;

            request.RequiredFlags = memoryProperties;

            request.PreferredFlags = memoryProperties;

            request.bDedicated = false;

            

            if (memoryManager->Allocate(request, m_allocation)) {

                m_deviceMemory = m_allocation.DeviceMemory;

                m_usesMemoryManager = true;

                

                // Bind buffer to allocated memory

                result = functions.vkBindBufferMemory(device, m_buffer, m_deviceMemory, m_allocation.Offset);

                if (result != VK_SUCCESS) {

                    MR_LOG_ERROR("Failed to bind vertex buffer memory: " + std::to_string(result));

                    memoryManager->Free(m_allocation);

                    m_usesMemoryManager = false;

                    return false;

                }

                

                MR_LOG_DEBUG("Created vertex buffer with managed memory, size: " + std::to_string(Size));

                return true;

            }

        }

        

        // Fallback to direct allocation

        MR_LOG_WARNING("Memory manager not available, using direct allocation for vertex buffer");

        

        VkMemoryAllocateInfo allocInfo{};

        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;

        allocInfo.allocationSize = memRequirements.size;

        allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, memoryProperties);

        

        result = functions.vkAllocateMemory(device, &allocInfo, nullptr, &m_deviceMemory);

        if (result != VK_SUCCESS) {

            MR_LOG_ERROR("Failed to allocate vertex buffer memory: " + std::to_string(result));

            return false;

        }

        

        result = functions.vkBindBufferMemory(device, m_buffer, m_deviceMemory, 0);

        if (result != VK_SUCCESS) {

            MR_LOG_ERROR("Failed to bind vertex buffer memory: " + std::to_string(result));

            return false;

        }

        

        MR_LOG_DEBUG("Created vertex buffer with direct allocation, size: " + std::to_string(Size));

        return true;

    }

    

    void FVulkanVertexBuffer::DestroyBuffer()

    {

        if (m_device) {

            const auto& functions = VulkanAPI::getFunctions();

            VkDevice device = m_device->getDevice();

            

            if (m_buffer != VK_NULL_HANDLE) {

                functions.vkDestroyBuffer(device, m_buffer, nullptr);

                m_buffer = VK_NULL_HANDLE;

            }

            

            if (m_usesMemoryManager) {

                auto* memoryManager = m_device->getMemoryManager();

                if (memoryManager && m_allocation.DeviceMemory != VK_NULL_HANDLE) {

                    memoryManager->Free(m_allocation);

                    m_usesMemoryManager = false;

                }

            } else if (m_deviceMemory != VK_NULL_HANDLE) {

                functions.vkFreeMemory(device, m_deviceMemory, nullptr);

                m_deviceMemory = VK_NULL_HANDLE;

            }

        }

    }

    

    void* FVulkanVertexBuffer::Lock(uint32 Offset, uint32 InSize)

    {

        const auto& functions = VulkanAPI::getFunctions();

        VkDevice device = m_device->getDevice();

        

        void* data = nullptr;

        VkDeviceSize mapOffset = m_usesMemoryManager ? (m_allocation.Offset + Offset) : Offset;

        

        VkResult result = functions.vkMapMemory(device, m_deviceMemory, mapOffset, InSize, 0, &data);

        if (result != VK_SUCCESS) {

            MR_LOG_ERROR("Failed to map vertex buffer memory: " + std::to_string(result));

            return nullptr;

        }

        

        m_mappedData = data;

        return data;

    }

    

    void FVulkanVertexBuffer::Unlock()

    {

        if (m_mappedData) {

            const auto& functions = VulkanAPI::getFunctions();

            VkDevice device = m_device->getDevice();

            functions.vkUnmapMemory(device, m_deviceMemory);

            m_mappedData = nullptr;

        }

    }

    

    uint32 FVulkanVertexBuffer::FindMemoryType(uint32 TypeFilter, VkMemoryPropertyFlags Properties)

    {

        return VulkanUtils::findMemoryType(m_device->getMemoryProperties(), TypeFilter, Properties);

    }

    

    // ============================================================================

    // FVulkanIndexBuffer Implementation

    // ============================================================================

    

    FVulkanIndexBuffer::FVulkanIndexBuffer(VulkanDevice* InDevice, uint32 InStride, uint32 InSize, EBufferUsageFlags InUsage)

        : FRHIIndexBuffer(InSize, InStride == 4)

        , m_device(InDevice)

        , m_usageFlags(InUsage)

    {

        MR_ASSERT(m_device != nullptr);

        MR_ASSERT(InSize > 0);

        MR_ASSERT(InStride == 2 || InStride == 4);

    }

    

    FVulkanIndexBuffer::~FVulkanIndexBuffer()

    {

        DestroyBuffer();

    }

    

    bool FVulkanIndexBuffer::Initialize(const void* InitialData, uint32 DataSize)

    {

        // Create the Vulkan buffer

        if (!CreateBuffer()) {

            return false;

        }

        

        // Upload initial data if provided

        if (InitialData && DataSize > 0) {

            void* MappedData = Lock(0, DataSize);

            if (MappedData) {

                memcpy(MappedData, InitialData, DataSize);

                Unlock();

                MR_LOG_DEBUG("Uploaded " + std::to_string(DataSize) + " bytes to index buffer");

            } else {

                MR_LOG_ERROR("Failed to map index buffer for initial data upload");

                return false;

            }

        }

        

        return true;

    }

    

    bool FVulkanIndexBuffer::CreateBuffer()

    {

        const auto& functions = VulkanAPI::getFunctions();

        VkDevice device = m_device->getDevice();

        

        // Setup buffer create info

        VkBufferCreateInfo bufferCreateInfo{};

        bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;

        bufferCreateInfo.size = Size;

        bufferCreateInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        

        // Create the buffer

        VkResult result = functions.vkCreateBuffer(device, &bufferCreateInfo, nullptr, &m_buffer);

        if (result != VK_SUCCESS) {

            MR_LOG_ERROR("Failed to create index buffer: " + std::to_string(result));

            return false;

        }

        

        // Get memory requirements

        VkMemoryRequirements memRequirements;

        functions.vkGetBufferMemoryRequirements(device, m_buffer, &memRequirements);

        

        // Determine memory properties based on usage flags

        VkMemoryPropertyFlags memoryProperties;

        bool isDynamic = EnumHasAnyFlags(m_usageFlags, EBufferUsageFlags::Dynamic | EBufferUsageFlags::Volatile);

        

        if (isDynamic || EnumHasAnyFlags(m_usageFlags, EBufferUsageFlags::KeepCPUAccessible)) {

            memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

        } else {

            memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

        }

        

        // Use memory manager for allocation

        auto* memoryManager = m_device->getMemoryManager();

        if (memoryManager) {

            FVulkanMemoryManager::FAllocationRequest request{};

            request.Size = memRequirements.size;

            request.Alignment = memRequirements.alignment;

            request.MemoryTypeBits = memRequirements.memoryTypeBits;

            request.RequiredFlags = memoryProperties;

            request.PreferredFlags = memoryProperties;

            request.bDedicated = false;

            

            if (memoryManager->Allocate(request, m_allocation)) {

                m_deviceMemory = m_allocation.DeviceMemory;

                m_usesMemoryManager = true;

                

                result = functions.vkBindBufferMemory(device, m_buffer, m_deviceMemory, m_allocation.Offset);

                if (result != VK_SUCCESS) {

                    MR_LOG_ERROR("Failed to bind index buffer memory: " + std::to_string(result));

                    memoryManager->Free(m_allocation);

                    m_usesMemoryManager = false;

                    return false;

                }

                

                MR_LOG_DEBUG("Created index buffer with managed memory, size: " + std::to_string(Size) + 

                            ", " + (Is32Bit() ? "32-bit" : "16-bit") + " indices");

                return true;

            }

        }

        

        // Fallback to direct allocation

        MR_LOG_WARNING("Memory manager not available, using direct allocation for index buffer");

        

        VkMemoryAllocateInfo allocInfo{};

        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;

        allocInfo.allocationSize = memRequirements.size;

        allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, memoryProperties);

        

        result = functions.vkAllocateMemory(device, &allocInfo, nullptr, &m_deviceMemory);

        if (result != VK_SUCCESS) {

            MR_LOG_ERROR("Failed to allocate index buffer memory: " + std::to_string(result));

            return false;

        }

        

        result = functions.vkBindBufferMemory(device, m_buffer, m_deviceMemory, 0);

        if (result != VK_SUCCESS) {

            MR_LOG_ERROR("Failed to bind index buffer memory: " + std::to_string(result));

            return false;

        }

        

        MR_LOG_DEBUG("Created index buffer with direct allocation, size: " + std::to_string(Size) +

                    ", " + (Is32Bit() ? "32-bit" : "16-bit") + " indices");

        return true;

    }

    

    void FVulkanIndexBuffer::DestroyBuffer()

    {

        if (m_device) {

            const auto& functions = VulkanAPI::getFunctions();

            VkDevice device = m_device->getDevice();

            

            if (m_buffer != VK_NULL_HANDLE) {

                functions.vkDestroyBuffer(device, m_buffer, nullptr);

                m_buffer = VK_NULL_HANDLE;

            }

            

            if (m_usesMemoryManager) {

                auto* memoryManager = m_device->getMemoryManager();

                if (memoryManager && m_allocation.DeviceMemory != VK_NULL_HANDLE) {

                    memoryManager->Free(m_allocation);

                    m_usesMemoryManager = false;

                }

            } else if (m_deviceMemory != VK_NULL_HANDLE) {

                functions.vkFreeMemory(device, m_deviceMemory, nullptr);

                m_deviceMemory = VK_NULL_HANDLE;

            }

        }

    }

    

    void* FVulkanIndexBuffer::Lock(uint32 Offset, uint32 InSize)

    {

        const auto& functions = VulkanAPI::getFunctions();

        VkDevice device = m_device->getDevice();

        

        void* data = nullptr;

        VkDeviceSize mapOffset = m_usesMemoryManager ? (m_allocation.Offset + Offset) : Offset;

        

        VkResult result = functions.vkMapMemory(device, m_deviceMemory, mapOffset, InSize, 0, &data);

        if (result != VK_SUCCESS) {

            MR_LOG_ERROR("Failed to map index buffer memory: " + std::to_string(result));

            return nullptr;

        }

        

        m_mappedData = data;

        return data;

    }

    

    void FVulkanIndexBuffer::Unlock()

    {

        if (m_mappedData) {

            const auto& functions = VulkanAPI::getFunctions();

            VkDevice device = m_device->getDevice();

            functions.vkUnmapMemory(device, m_deviceMemory);

            m_mappedData = nullptr;

        }

    }

    

    uint32 FVulkanIndexBuffer::FindMemoryType(uint32 TypeFilter, VkMemoryPropertyFlags Properties)

    {

        return VulkanUtils::findMemoryType(m_device->getMemoryProperties(), TypeFilter, Properties);

    }

}

