#include "Platform/Vulkan/VulkanPendingState.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanCommandBuffer.h"
#include "Platform/Vulkan/VulkanPipelineState.h"
#include "Platform/Vulkan/VulkanDescriptorSet.h"
#include "Platform/Vulkan/VulkanDescriptorSetLayoutCache.h"
#include "Platform/Vulkan/VulkanUtils.h"
#include "Core/Log.h"
#include "Core/Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogVulkanRHI, Log, All);

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
            // Clear all resource bindings when switching pipelines to avoid descriptor set mismatch
            // Different pipelines may have different descriptor set layouts
            m_textures.clear();
            m_uniformBuffers.clear();
            // Also invalidate current descriptor set since pipeline changed
            m_currentDescriptorSet = VK_NULL_HANDLE;

            MR_LOG_DEBUG("FVulkanPendingState::setGraphicsPipeline: Set pipeline (handle: " +
                        std::to_string(reinterpret_cast<uint64>(vkPipeline)) + "), cleared all bindings");
        }

    }

    void FVulkanPendingState::setViewport(const Viewport& viewport) {
        m_pendingViewport = viewport;
        m_viewportDirty = true;
    }

    void FVulkanPendingState::setScissor(const ScissorRect& scissor) {
        m_pendingScissor = scissor;
        m_scissorDirty = true;

        MR_LOG_INFO("FVulkanPendingState::setScissor: Set pending scissor (" +

                   std::to_string(scissor.left) + "," + std::to_string(scissor.top) + ")-(" +
                   std::to_string(scissor.right) + "," + std::to_string(scissor.bottom) + "), dirty=true");
    }

    void FVulkanPendingState::setVertexBuffer(uint32 binding, VkBuffer buffer, VkDeviceSize offset) {
        if (binding < m_vertexBuffers.size()) {
            auto& vb = m_vertexBuffers[binding];
            if (vb.buffer != buffer || vb.offset != offset) {
                vb.buffer = buffer;
                vb.offset = offset;
                m_vertexBuffersDirty = true;
            }

        } else {

            MR_LOG_ERROR("setVertexBuffer: binding " + std::to_string(binding) +
                        " out of range (max=" + std::to_string(m_vertexBuffers.size()) + ")");
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

    void FVulkanPendingState::setTexture(uint32 slot, VkImageView imageView, VkSampler sampler,
                                         VkImage image, VkFormat format, uint32 mipLevels, uint32 arrayLayers) {
        auto& binding = m_textures[slot];
        if (binding.imageView != imageView || binding.sampler != sampler) {
            binding.imageView = imageView;
            binding.sampler = sampler;
            binding.image = image;
            binding.format = format;
            binding.mipLevels = mipLevels;
            binding.arrayLayers = arrayLayers;
            m_descriptorsDirty = true;
            MR_LOG_DEBUG("FVulkanPendingState::setTexture: slot=" + std::to_string(slot));
        }

    }

    void FVulkanPendingState::setSampler(uint32 slot, VkSampler sampler) {
        auto& binding = m_textures[slot];
        if (binding.sampler != sampler) {
            binding.sampler = sampler;
            m_descriptorsDirty = true;
            MR_LOG_DEBUG("FVulkanPendingState::setSampler: slot=" + std::to_string(slot));
        }

    }

    void FVulkanPendingState::transitionTexturesForShaderRead() {
        // Execute layout transitions for all bound textures BEFORE render pass begins
        // This is required because Vulkan validation layer tracks layout per-command-buffer
        // and textures uploaded in a different command buffer need explicit transitions
        if (!m_cmdBuffer || m_cmdBuffer->getHandle() == VK_NULL_HANDLE) {
            return;
        }

        if (m_textures.empty()) {
            return;
        }

        const auto& functions = VulkanAPI::getFunctions();
        VkCommandBuffer cmdBuffer = m_cmdBuffer->getHandle();
        TArray<VkImageMemoryBarrier> barriers;
        barriers.reserve(m_textures.size());
        for (auto& pair : m_textures) {
            auto& binding = pair.second;
            if (binding.image == VK_NULL_HANDLE) {
                continue;
            }

            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            // Use UNDEFINED as oldLayout - tells Vulkan we don't care about previous contents
            // The actual data was already uploaded in a previous command buffer
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            // Determine aspect mask and correct layout based on format
            VkImageAspectFlags aspectMask = VulkanUtils::getImageAspectMask(binding.format);
            barrier.subresourceRange.aspectMask = aspectMask;
            // Depth textures use DEPTH_STENCIL_READ_ONLY_OPTIMAL, color textures use SHADER_READ_ONLY_OPTIMAL
            if (aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) {
                barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            } else {
                barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }

            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = binding.image;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = binding.mipLevels;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = binding.arrayLayers;
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barriers.push_back(barrier);
        }

        if (!barriers.empty()) {
            functions.vkCmdPipelineBarrier(
                cmdBuffer,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                static_cast<uint32>(barriers.size()), barriers.data()
            );
            MR_LOG_DEBUG("transitionTexturesForShaderRead: Transitioned " +
                        std::to_string(barriers.size()) + " textures to SHADER_READ_ONLY_OPTIMAL");
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
                MR_LOG_INFO("prepareForDraw: Binding NEW pipeline (handle: " +
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
            MR_LOG_INFO("prepareForDraw: Using CACHED pipeline (handle: " +
                       std::to_string(reinterpret_cast<uint64>(m_currentPipeline->getPipeline())) + ")");
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

            // UE5 Pattern: Apply Y-flip for Vulkan to match OpenGL/D3D coordinate system
            // Reference: UE5 VulkanPendingState.cpp - InternalUpdateDynamicStates()
            // Vulkan's clip space Y is inverted compared to OpenGL/D3D
            // By using negative height and offsetting Y, we flip the viewport
            // This requires VK_KHR_maintenance1 extension (core in Vulkan 1.1+)
            VkViewport vkViewport{};
            vkViewport.x = m_pendingViewport.x;
            // Y-flip: offset Y by height, then use negative height
            vkViewport.y = m_pendingViewport.y + m_pendingViewport.height;
            vkViewport.width = m_pendingViewport.width;
            vkViewport.height = -m_pendingViewport.height;  // Negative height for Y-flip
            vkViewport.minDepth = m_pendingViewport.minDepth;
            vkViewport.maxDepth = m_pendingViewport.maxDepth;
            MR_LOG_INFO("prepareForDraw: vkCmdSetViewport with Y-flip (x=" +

                        std::to_string(vkViewport.x) + ", y=" + std::to_string(vkViewport.y) +
                        ", w=" + std::to_string(vkViewport.width) + ", h=" + std::to_string(vkViewport.height) + ")");
            functions.vkCmdSetViewport(cmdBuffer, 0, 1, &vkViewport);
            m_viewportDirty = false;
        }

        // Apply scissor (force set if not already set, even if not dirty)

        MR_LOG_INFO("prepareForDraw: m_scissorDirty=" + std::string(m_scissorDirty ? "true" : "false") +
                   ", m_pendingScissor.right=" + std::to_string(m_pendingScissor.right));
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

            MR_LOG_INFO("prepareForDraw: vkCmdSetScissor(x=" + std::to_string(scissor.offset.x) +

                        ", y=" + std::to_string(scissor.offset.y) +

                        ", w=" + std::to_string(scissor.extent.width) +
                        ", h=" + std::to_string(scissor.extent.height) + ")");
            functions.vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);
            m_scissorDirty = false;
        } else {
            MR_LOG_INFO("prepareForDraw: Scissor NOT set (already clean, m_scissorDirty=false, right=" +
                       std::to_string(m_pendingScissor.right) + ")");
        }

        // Apply vertex buffers if dirty - use std::vector for Vulkan API compatibility

        MR_LOG_INFO("prepareForDraw: m_vertexBuffersDirty=" + std::string(m_vertexBuffersDirty ? "true" : "false") +
                   ", m_vertexBuffers.size()=" + std::to_string(m_vertexBuffers.size()));
        if (m_vertexBuffersDirty) {
            std::vector<VkBuffer> buffers;
            std::vector<VkDeviceSize> offsets;
            MR_LOG_INFO("prepareForDraw: Checking vertex buffers...");
            for (size_t i = 0; i < m_vertexBuffers.size(); ++i) {
                const auto& vb = m_vertexBuffers[i];

                MR_LOG_INFO("  VB[" + std::to_string(i) + "]: buffer=" +
                           std::to_string(reinterpret_cast<uint64>(vb.buffer)));
                if (vb.buffer != VK_NULL_HANDLE) {
                    buffers.push_back(vb.buffer);
                    offsets.push_back(vb.offset);
                }

            }

            if (!buffers.empty()) {

                MR_LOG_DEBUG("prepareForDraw: Binding " + std::to_string(buffers.size()) +
                           " vertex buffer(s)");
                functions.vkCmdBindVertexBuffers(cmdBuffer, 0,
                                                static_cast<uint32>(buffers.size()),
                                                buffers.data(), offsets.data());
                m_vertexBuffersDirty = false;
            } else {
                MR_LOG_ERROR("prepareForDraw: Vertex buffers marked dirty but no valid buffers found!");
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
        MR_LOG_INFO("updateAndBindDescriptorSets: DescriptorLayouts.size()=" +

                    std::to_string(DescriptorLayouts.size()) +

                    ", PipelineLayout=" + std::to_string(reinterpret_cast<uint64>(PipelineLayout)) +

                    ", UniformBuffers=" + std::to_string(m_uniformBuffers.size()) +
                    ", Textures=" + std::to_string(m_textures.size()));
        // Log texture details
        for (const auto& [Slot, Binding] : m_textures) {

            MR_LOG_INFO("  Texture slot " + std::to_string(Slot) +

                       ": imageView=" + std::to_string(reinterpret_cast<uint64>(Binding.imageView)) +
                       ", sampler=" + std::to_string(reinterpret_cast<uint64>(Binding.sampler)));
        }

        if (DescriptorLayouts.empty() || PipelineLayout == VK_NULL_HANDLE) {
            MR_LOG_WARNING("updateAndBindDescriptorSets: No descriptor layouts (size=" +

                          std::to_string(DescriptorLayouts.size()) +
                          ") or invalid pipeline layout - SKIPPING descriptor binding!");
            return VK_NULL_HANDLE;
        }

        // Try to use descriptor set cache first (UE5-style optimization)
        // Skip cache if explicitly disabled (e.g., for PBR rendering with pre-updated descriptor sets)
        auto* Cache = m_device->GetDescriptorSetCache();
        if (Cache && m_descriptorSetCacheEnabled) {
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

        // If cache is disabled, skip descriptor set binding from pending state
        if (!m_descriptorSetCacheEnabled) {
            MR_LOG_DEBUG("updateAndBindDescriptorSets: Cache disabled, skipping automatic binding");
            return VK_NULL_HANDLE;
        }

        // Fallback path: descriptor set cache is not available
        // This is a rare case - most descriptor sets should come from the cache
        // For now, we log a warning and return null, forcing the caller to use the cache
        MR_LOG(LogVulkanRHI, Warning,
               "updateAndBindDescriptorSets: Descriptor set cache unavailable, cannot allocate descriptor set");
        return VK_NULL_HANDLE;
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
                // Determine correct image layout based on format
                VkImageAspectFlags aspectMask = VulkanUtils::getImageAspectMask(Binding.format);
                if (aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) {
                    ImageBinding.ImageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
                } else {
                    ImageBinding.ImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                }

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
        m_poolSizes.Add(PoolSizeInfo{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 512});
        m_poolSizes.Add(PoolSizeInfo{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 512});
        m_poolSizes.Add(PoolSizeInfo{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 256});
        m_poolSizes.Add(PoolSizeInfo{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 256});
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
        // Prepare pool sizes - use std::vector for Vulkan API compatibility
        std::vector<VkDescriptorPoolSize> poolSizes;
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
