#include "Platform/Vulkan/VulkanPendingState.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanCommandBuffer.h"
#include "Platform/Vulkan/VulkanPipelineState.h"
#include "Core/Log.h"

namespace MonsterRender::RHI::Vulkan {
    
    // ========================================================================
    // FVulkanPendingState Implementation
    // ========================================================================
    
    FVulkanPendingState::FVulkanPendingState(VulkanDevice* device, FVulkanCmdBuffer* cmdBuffer)
        : m_device(device)
        , m_cmdBuffer(cmdBuffer)
        , m_currentPipeline(nullptr)
        , m_pendingPipeline(nullptr)
        , m_viewportDirty(false)
        , m_scissorDirty(false)
        , m_vertexBuffersDirty(false)
        , m_indexBuffer(VK_NULL_HANDLE)
        , m_indexBufferOffset(0)
        , m_indexType(VK_INDEX_TYPE_UINT32)
        , m_indexBufferDirty(false)
        , m_insideRenderPass(false) {
        
        // Initialize vertex buffer bindings
        m_vertexBuffers.resize(8); // Support up to 8 vertex buffer bindings
    }
    
    FVulkanPendingState::~FVulkanPendingState() {
    }
    
    void FVulkanPendingState::reset() {
        m_currentPipeline = nullptr;
        m_pendingPipeline = nullptr;
        m_viewportDirty = false;
        m_scissorDirty = false;
        m_vertexBuffersDirty = false;
        m_indexBufferDirty = false;
        m_insideRenderPass = false;
        
        // Clear vertex buffers
        for (auto& binding : m_vertexBuffers) {
            binding.buffer = VK_NULL_HANDLE;
            binding.offset = 0;
        }
        
        m_indexBuffer = VK_NULL_HANDLE;
    }
    
    void FVulkanPendingState::setGraphicsPipeline(VulkanPipelineState* pipeline) {
        if (!pipeline) {
            MR_LOG_ERROR("FVulkanPendingState::setGraphicsPipeline: Null pipeline");
            return;
        }
        
        VkPipeline vkPipeline = pipeline->getPipeline();
        if (vkPipeline == VK_NULL_HANDLE) {
            MR_LOG_ERROR("FVulkanPendingState::setGraphicsPipeline: Pipeline handle is NULL");
            return;
        }
        
        if (m_pendingPipeline != pipeline) {
            m_pendingPipeline = pipeline;
            MR_LOG_DEBUG("FVulkanPendingState::setGraphicsPipeline: Set pipeline (handle: " + 
                        std::to_string(reinterpret_cast<uint64>(vkPipeline)) + ")");
        }
    }
    
    void FVulkanPendingState::setViewport(const Viewport& viewport) {
        m_pendingViewport = viewport;
        m_viewportDirty = true;
    }
    
    void FVulkanPendingState::setScissor(const ScissorRect& scissor) {
        m_pendingScissor = scissor;
        m_scissorDirty = true;
    }
    
    void FVulkanPendingState::setVertexBuffer(uint32 binding, VkBuffer buffer, VkDeviceSize offset) {
        if (binding < m_vertexBuffers.size()) {
            auto& vb = m_vertexBuffers[binding];
            if (vb.buffer != buffer || vb.offset != offset) {
                vb.buffer = buffer;
                vb.offset = offset;
                m_vertexBuffersDirty = true;
            }
        }
    }
    
    void FVulkanPendingState::setIndexBuffer(VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType) {
        if (m_indexBuffer != buffer || m_indexBufferOffset != offset || m_indexType != indexType) {
            m_indexBuffer = buffer;
            m_indexBufferOffset = offset;
            m_indexType = indexType;
            m_indexBufferDirty = true;
        }
    }
    
    void FVulkanPendingState::prepareForDraw() {
        if (!m_cmdBuffer) {
            MR_LOG_ERROR("prepareForDraw: No command buffer");
            return;
        }
        
        VkCommandBuffer cmdBuffer = m_cmdBuffer->getHandle();
        if (cmdBuffer == VK_NULL_HANDLE) {
            MR_LOG_ERROR("prepareForDraw: Command buffer handle is NULL");
            return;
        }
        
        const auto& functions = VulkanAPI::getFunctions();
        
        // Apply pipeline state if changed
        if (m_pendingPipeline && m_pendingPipeline != m_currentPipeline) {
            VkPipeline pipeline = m_pendingPipeline->getPipeline();
            if (pipeline != VK_NULL_HANDLE) {
                MR_LOG_DEBUG("prepareForDraw: Binding pipeline");
                functions.vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                m_currentPipeline = m_pendingPipeline;
            } else {
                MR_LOG_ERROR("prepareForDraw: Pipeline handle is NULL!");
                return; // Cannot draw without valid pipeline
            }
        } else if (!m_pendingPipeline) {
            MR_LOG_ERROR("prepareForDraw: No pending pipeline set!");
            return; // Cannot draw without pipeline
        } else if (m_currentPipeline) {
            MR_LOG_DEBUG("prepareForDraw: Using cached pipeline (no change)");
        }
        
        // Apply viewport if dirty
        if (m_viewportDirty) {
            VkViewport vkViewport{};
            vkViewport.x = m_pendingViewport.x;
            vkViewport.y = m_pendingViewport.y;
            vkViewport.width = m_pendingViewport.width;
            vkViewport.height = m_pendingViewport.height;
            vkViewport.minDepth = m_pendingViewport.minDepth;
            vkViewport.maxDepth = m_pendingViewport.maxDepth;
            
            functions.vkCmdSetViewport(cmdBuffer, 0, 1, &vkViewport);
            m_viewportDirty = false;
        }
        
        // Apply scissor if dirty
        if (m_scissorDirty) {
            VkRect2D scissor{};
            scissor.offset.x = m_pendingScissor.left;
            scissor.offset.y = m_pendingScissor.top;
            scissor.extent.width = m_pendingScissor.right - m_pendingScissor.left;
            scissor.extent.height = m_pendingScissor.bottom - m_pendingScissor.top;
            
            functions.vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);
            m_scissorDirty = false;
        }
        
        // Apply vertex buffers if dirty
        if (m_vertexBuffersDirty) {
            TArray<VkBuffer> buffers;
            TArray<VkDeviceSize> offsets;
            
            for (const auto& vb : m_vertexBuffers) {
                if (vb.buffer != VK_NULL_HANDLE) {
                    buffers.push_back(vb.buffer);
                    offsets.push_back(vb.offset);
                }
            }
            
            if (!buffers.empty()) {
                functions.vkCmdBindVertexBuffers(cmdBuffer, 0, 
                                                static_cast<uint32>(buffers.size()),
                                                buffers.data(), offsets.data());
            }
            m_vertexBuffersDirty = false;
        }
        
        // Apply index buffer if dirty
        if (m_indexBufferDirty && m_indexBuffer != VK_NULL_HANDLE) {
            functions.vkCmdBindIndexBuffer(cmdBuffer, m_indexBuffer, m_indexBufferOffset, m_indexType);
            m_indexBufferDirty = false;
        }
    }
    
    // ========================================================================
    // FVulkanDescriptorPoolSetContainer Implementation
    // ========================================================================
    
    FVulkanDescriptorPoolSetContainer::FVulkanDescriptorPoolSetContainer(VulkanDevice* device)
        : m_device(device)
        , m_descriptorPool(VK_NULL_HANDLE)
        , m_maxSets(1024)
        , m_allocatedSets(0) {
        
        // Configure pool sizes (UE5 typical configuration)
        m_poolSizes.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 512});
        m_poolSizes.push_back({VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 512});
        m_poolSizes.push_back({VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 256});
        m_poolSizes.push_back({VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 256});
    }
    
    FVulkanDescriptorPoolSetContainer::~FVulkanDescriptorPoolSetContainer() {
        if (m_descriptorPool != VK_NULL_HANDLE) {
            const auto& functions = VulkanAPI::getFunctions();
            VkDevice device = m_device->getLogicalDevice();
            functions.vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
            m_descriptorPool = VK_NULL_HANDLE;
        }
    }
    
    bool FVulkanDescriptorPoolSetContainer::initialize() {
        return createDescriptorPool();
    }
    
    bool FVulkanDescriptorPoolSetContainer::createDescriptorPool() {
        const auto& functions = VulkanAPI::getFunctions();
        VkDevice device = m_device->getLogicalDevice();
        
        // Prepare pool sizes
        TArray<VkDescriptorPoolSize> poolSizes;
        poolSizes.reserve(m_poolSizes.size());
        
        for (const auto& sizeInfo : m_poolSizes) {
            VkDescriptorPoolSize poolSize{};
            poolSize.type = sizeInfo.type;
            poolSize.descriptorCount = sizeInfo.count;
            poolSizes.push_back(poolSize);
        }
        
        // Create descriptor pool
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = m_maxSets;
        poolInfo.poolSizeCount = static_cast<uint32>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        
        VkResult result = functions.vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to create descriptor pool: " + std::to_string(result));
            return false;
        }
        
        MR_LOG_DEBUG("FVulkanDescriptorPoolSetContainer initialized");
        return true;
    }
    
    VkDescriptorSet FVulkanDescriptorPoolSetContainer::allocateDescriptorSet(VkDescriptorSetLayout layout) {
        if (m_descriptorPool == VK_NULL_HANDLE) {
            MR_LOG_ERROR("Descriptor pool not initialized");
            return VK_NULL_HANDLE;
        }
        
        const auto& functions = VulkanAPI::getFunctions();
        VkDevice device = m_device->getLogicalDevice();
        
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &layout;
        
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        VkResult result = functions.vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to allocate descriptor set: " + std::to_string(result));
            return VK_NULL_HANDLE;
        }
        
        m_allocatedSets++;
        return descriptorSet;
    }
    
    void FVulkanDescriptorPoolSetContainer::reset() {
        if (m_descriptorPool != VK_NULL_HANDLE) {
            const auto& functions = VulkanAPI::getFunctions();
            VkDevice device = m_device->getLogicalDevice();
            
            // Reset pool - returns all descriptor sets
            functions.vkResetDescriptorPool(device, m_descriptorPool, 0);
            m_allocatedSets = 0;
            
            MR_LOG_DEBUG("Descriptor pool reset for new frame");
        }
    }
}
