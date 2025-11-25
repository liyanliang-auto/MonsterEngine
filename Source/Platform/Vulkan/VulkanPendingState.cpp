#include "Platform/Vulkan/VulkanPendingState.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanCommandBuffer.h"
#include "Platform/Vulkan/VulkanPipelineState.h"
#include "Platform/Vulkan/VulkanDescriptorSet.h"
#include "Platform/Vulkan/VulkanDescriptorSetLayoutCache.h"
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
        
        // Initialize viewport with default values (will be overwritten when set)
        m_pendingViewport.x = 0.0f;
        m_pendingViewport.y = 0.0f;
        m_pendingViewport.width = 0.0f; // Will be set by setViewport()
        m_pendingViewport.height = 0.0f;
        m_pendingViewport.minDepth = 0.0f;
        m_pendingViewport.maxDepth = 1.0f;
        
        // Initialize scissor with default values (will be overwritten when set)
        m_pendingScissor.left = 0;
        m_pendingScissor.top = 0;
        m_pendingScissor.right = 0; // Will be set by setScissor()
        m_pendingScissor.bottom = 0;
    }
    
    FVulkanPendingState::~FVulkanPendingState() {
    }
    
    void FVulkanPendingState::updateCommandBuffer(FVulkanCmdBuffer* cmdBuffer) {
        m_cmdBuffer = cmdBuffer;
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
        
        // Clear resource bindings
        m_uniformBuffers.clear();
        m_textures.clear();
        m_descriptorsDirty = true;
        m_currentDescriptorSet = VK_NULL_HANDLE;
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
    
    void FVulkanPendingState::setUniformBuffer(uint32 slot, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range) {
        auto& binding = m_uniformBuffers[slot];
        if (binding.buffer != buffer || binding.offset != offset || binding.range != range) {
            binding.buffer = buffer;
            binding.offset = offset;
            binding.range = range;
            m_descriptorsDirty = true;
            MR_LOG_DEBUG("FVulkanPendingState::setUniformBuffer: slot=" + std::to_string(slot));
        }
    }
    
    void FVulkanPendingState::setTexture(uint32 slot, VkImageView imageView, VkSampler sampler) {
        auto& binding = m_textures[slot];
        if (binding.imageView != imageView || binding.sampler != sampler) {
            binding.imageView = imageView;
            binding.sampler = sampler;
            m_descriptorsDirty = true;
            MR_LOG_DEBUG("FVulkanPendingState::setTexture: slot=" + std::to_string(slot));
        }
    }
    
    bool FVulkanPendingState::prepareForDraw() {
        MR_LOG_INFO("===== prepareForDraw() START =====");
        
        if (!m_cmdBuffer) {
            MR_LOG_ERROR("prepareForDraw: No command buffer");
            return false;
        }
        
        VkCommandBuffer cmdBuffer = m_cmdBuffer->getHandle();
        if (cmdBuffer == VK_NULL_HANDLE) {
            MR_LOG_ERROR("prepareForDraw: Command buffer handle is NULL");
            return false;
        }
        
        MR_LOG_INFO("  CmdBuffer: " + std::to_string(reinterpret_cast<uint64>(cmdBuffer)));
        MR_LOG_INFO("  Inside render pass: " + std::string(m_insideRenderPass ? "YES" : "NO"));
        
        const auto& functions = VulkanAPI::getFunctions();
        
        MR_LOG_DEBUG("prepareForDraw: Starting state preparation");
        
        // Apply pipeline state if changed
        if (m_pendingPipeline && m_pendingPipeline != m_currentPipeline) {
            VkPipeline pipeline = m_pendingPipeline->getPipeline();
            if (pipeline != VK_NULL_HANDLE) {
                MR_LOG_DEBUG("prepareForDraw: Binding pipeline (handle: " + 
                            std::to_string(reinterpret_cast<uint64>(pipeline)) + ")");
                functions.vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                m_currentPipeline = m_pendingPipeline;
            } else {
                MR_LOG_ERROR("prepareForDraw: Pipeline handle is NULL!");
                return false; // Cannot draw without valid pipeline
            }
        } else if (!m_pendingPipeline && !m_currentPipeline) {
            MR_LOG_ERROR("prepareForDraw: No pipeline set at all!");
            return false; // Cannot draw without pipeline
        } else if (m_currentPipeline) {
            MR_LOG_DEBUG("prepareForDraw: Using cached pipeline");
        }
        
        // CRITICAL: Viewport and Scissor are REQUIRED for drawing
        // Apply viewport (force set if not already set, even if not dirty)
        if (m_viewportDirty || m_pendingViewport.width == 0) {
            if (m_pendingViewport.width == 0 || m_pendingViewport.height == 0) {
                MR_LOG_ERROR("prepareForDraw: Invalid viewport dimensions (" + 
                            std::to_string(m_pendingViewport.width) + "x" + 
                            std::to_string(m_pendingViewport.height) + ")");
                return false;
            }
            
            VkViewport vkViewport{};
            vkViewport.x = m_pendingViewport.x;
            vkViewport.y = m_pendingViewport.y;
            vkViewport.width = m_pendingViewport.width;
            vkViewport.height = m_pendingViewport.height;
            vkViewport.minDepth = m_pendingViewport.minDepth;
            vkViewport.maxDepth = m_pendingViewport.maxDepth;
            
            MR_LOG_DEBUG("prepareForDraw: Setting viewport (" + 
                        std::to_string(m_pendingViewport.width) + "x" + 
                        std::to_string(m_pendingViewport.height) + ")");
            functions.vkCmdSetViewport(cmdBuffer, 0, 1, &vkViewport);
            m_viewportDirty = false;
        }
        
        // Apply scissor (force set if not already set, even if not dirty)
        if (m_scissorDirty || m_pendingScissor.right == 0) {
            if (m_pendingScissor.right <= m_pendingScissor.left || 
                m_pendingScissor.bottom <= m_pendingScissor.top) {
                MR_LOG_ERROR("prepareForDraw: Invalid scissor rect");
                return false;
            }
            
            VkRect2D scissor{};
            scissor.offset.x = m_pendingScissor.left;
            scissor.offset.y = m_pendingScissor.top;
            scissor.extent.width = m_pendingScissor.right - m_pendingScissor.left;
            scissor.extent.height = m_pendingScissor.bottom - m_pendingScissor.top;
            
            MR_LOG_DEBUG("prepareForDraw: Setting scissor rect");
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
                MR_LOG_DEBUG("prepareForDraw: Binding " + std::to_string(buffers.size()) + " vertex buffer(s)");
                functions.vkCmdBindVertexBuffers(cmdBuffer, 0, 
                                                static_cast<uint32>(buffers.size()),
                                                buffers.data(), offsets.data());
                m_vertexBuffersDirty = false;
            } else {
                MR_LOG_WARNING("prepareForDraw: Vertex buffers marked dirty but no valid buffers found");
            }
        }
        
        // Apply index buffer if dirty
        if (m_indexBufferDirty && m_indexBuffer != VK_NULL_HANDLE) {
            MR_LOG_DEBUG("prepareForDraw: Binding index buffer");
            functions.vkCmdBindIndexBuffer(cmdBuffer, m_indexBuffer, m_indexBufferOffset, m_indexType);
            m_indexBufferDirty = false;
        }
        
        // Bind descriptor sets using UE5-style caching mechanism
        // Reference: UE5 FVulkanPendingGfxState::PrepareForDraw()
        if (m_descriptorsDirty && m_currentPipeline) {
            VkDescriptorSet descriptorSet = updateAndBindDescriptorSets(cmdBuffer, functions);
            if (descriptorSet != VK_NULL_HANDLE) {
                m_currentDescriptorSet = descriptorSet;
            }
            m_descriptorsDirty = false;
        } else if (m_currentDescriptorSet != VK_NULL_HANDLE && m_currentPipeline) {
            // Re-bind existing descriptor set if pipeline changed but resources didn't
            VkPipelineLayout pipelineLayout = m_currentPipeline->getPipelineLayout();
            if (pipelineLayout != VK_NULL_HANDLE) {
                functions.vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipelineLayout, 0, 1, &m_currentDescriptorSet, 0, nullptr);
            }
        }
        
        MR_LOG_INFO("prepareForDraw: State preparation completed successfully");
        MR_LOG_INFO("===== prepareForDraw() END =====");
        return true; // Successfully prepared for draw
    }
    
    VkDescriptorSet FVulkanPendingState::updateAndBindDescriptorSets(VkCommandBuffer CmdBuffer,
                                                                      const VulkanFunctions& Functions) {
        // UE5-style descriptor set binding with caching
        // Reference: UE5 FVulkanPendingGfxState::UpdateAndBindDescriptorSets()
        
        auto& DescriptorLayouts = m_currentPipeline->getDescriptorSetLayouts();
        VkPipelineLayout PipelineLayout = m_currentPipeline->getPipelineLayout();
        
        MR_LOG_DEBUG("updateAndBindDescriptorSets: DescriptorLayouts.size()=" + 
                    std::to_string(DescriptorLayouts.size()) + 
                    ", PipelineLayout=" + std::to_string(reinterpret_cast<uint64>(PipelineLayout)) +
                    ", UniformBuffers=" + std::to_string(m_uniformBuffers.size()) +
                    ", Textures=" + std::to_string(m_textures.size()));
        
        if (DescriptorLayouts.empty() || PipelineLayout == VK_NULL_HANDLE) {
            MR_LOG_WARNING("updateAndBindDescriptorSets: No descriptor layouts (size=" + 
                          std::to_string(DescriptorLayouts.size()) + 
                          ") or invalid pipeline layout - SKIPPING descriptor binding!");
            return VK_NULL_HANDLE;
        }
        
        // Try to use descriptor set cache first (UE5-style optimization)
        auto* Cache = m_device->GetDescriptorSetCache();
        if (Cache) {
            // Build key from current bindings
            FVulkanDescriptorSetKey Key = buildDescriptorSetKey();
            Key.Layout = DescriptorLayouts[0];
            
            // Get or allocate from cache
            VkDescriptorSet CachedSet = Cache->GetOrAllocate(Key);
            if (CachedSet != VK_NULL_HANDLE) {
                // Bind the cached descriptor set
                Functions.vkCmdBindDescriptorSets(CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    PipelineLayout, 0, 1, &CachedSet, 0, nullptr);
                MR_LOG_DEBUG("updateAndBindDescriptorSets: Used cached descriptor set");
                return CachedSet;
            }
        }
        
        // Fallback to direct allocation if cache is not available
        auto* Allocator = m_device->getDescriptorSetAllocator();
        if (!Allocator) {
            MR_LOG_WARNING("updateAndBindDescriptorSets: No descriptor set allocator available");
            return VK_NULL_HANDLE;
        }
        
        // Allocate descriptor set
        VkDescriptorSet DescriptorSet = Allocator->allocate(DescriptorLayouts[0]);
        if (DescriptorSet == VK_NULL_HANDLE) {
            MR_LOG_ERROR("updateAndBindDescriptorSets: Failed to allocate descriptor set");
            return VK_NULL_HANDLE;
        }
        
        // Update descriptor set with bound resources
        TArray<VkWriteDescriptorSet> Writes;
        TArray<VkDescriptorBufferInfo> BufferInfos;
        TArray<VkDescriptorImageInfo> ImageInfos;
        
        // Reserve space to prevent reallocation invalidating pointers
        BufferInfos.reserve(m_uniformBuffers.size());
        ImageInfos.reserve(m_textures.size());
        
        // Add uniform buffers
        for (const auto& [Slot, Binding] : m_uniformBuffers) {
            if (Binding.buffer != VK_NULL_HANDLE) {
                VkDescriptorBufferInfo& BufferInfo = BufferInfos.emplace_back();
                BufferInfo.buffer = Binding.buffer;
                BufferInfo.offset = Binding.offset;
                BufferInfo.range = Binding.range > 0 ? Binding.range : VK_WHOLE_SIZE;
                
                VkWriteDescriptorSet Write = {};
                Write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                Write.dstSet = DescriptorSet;
                Write.dstBinding = Slot;
                Write.dstArrayElement = 0;
                Write.descriptorCount = 1;
                Write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                Write.pBufferInfo = &BufferInfos.back();
                Writes.push_back(Write);
            }
        }
        
        // Add textures (combined image samplers)
        for (const auto& [Slot, Binding] : m_textures) {
            if (Binding.imageView != VK_NULL_HANDLE) {
                VkDescriptorImageInfo& ImageInfo = ImageInfos.emplace_back();
                ImageInfo.imageView = Binding.imageView;
                ImageInfo.sampler = Binding.sampler;
                ImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                
                VkWriteDescriptorSet Write = {};
                Write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                Write.dstSet = DescriptorSet;
                Write.dstBinding = Slot;
                Write.dstArrayElement = 0;
                Write.descriptorCount = 1;
                Write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                Write.pImageInfo = &ImageInfos.back();
                Writes.push_back(Write);
            }
        }
        
        // Update descriptor set
        if (!Writes.empty()) {
            Functions.vkUpdateDescriptorSets(m_device->getLogicalDevice(),
                static_cast<uint32>(Writes.size()), Writes.data(), 0, nullptr);
            
            // Bind descriptor set
            Functions.vkCmdBindDescriptorSets(CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                PipelineLayout, 0, 1, &DescriptorSet, 0, nullptr);
            
            MR_LOG_DEBUG("updateAndBindDescriptorSets: Bound descriptor set with " +
                std::to_string(Writes.size()) + " bindings (fallback path)");
        }
        
        return DescriptorSet;
    }
    
    FVulkanDescriptorSetKey FVulkanPendingState::buildDescriptorSetKey() const {
        // Build a key for descriptor set cache lookup
        // Reference: UE5 descriptor set key generation
        
        FVulkanDescriptorSetKey Key;
        
        // Add buffer bindings
        for (const auto& [Slot, Binding] : m_uniformBuffers) {
            if (Binding.buffer != VK_NULL_HANDLE) {
                FVulkanDescriptorSetKey::FBufferBinding BufferBinding;
                BufferBinding.Buffer = Binding.buffer;
                BufferBinding.Offset = Binding.offset;
                BufferBinding.Range = Binding.range;
                Key.BufferBindings[Slot] = BufferBinding;
            }
        }
        
        // Add image bindings
        for (const auto& [Slot, Binding] : m_textures) {
            if (Binding.imageView != VK_NULL_HANDLE) {
                FVulkanDescriptorSetKey::FImageBinding ImageBinding;
                ImageBinding.ImageView = Binding.imageView;
                ImageBinding.Sampler = Binding.sampler;
                ImageBinding.ImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                Key.ImageBindings[Slot] = ImageBinding;
            }
        }
        
        return Key;
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
