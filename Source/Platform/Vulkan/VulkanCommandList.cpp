#include "Platform/Vulkan/VulkanCommandList.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanPipelineState.h"
#include "Platform/Vulkan/VulkanDescriptorSet.h"
#include "Platform/Vulkan/VulkanShader.h"
#include "Core/Log.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanTexture.h"

namespace MonsterRender::RHI::Vulkan {
    
    VulkanCommandList::VulkanCommandList(VulkanDevice* device) 
        : m_device(device) {
        MR_LOG_INFO("Creating Vulkan command list...");
        
        // TODO: Allocate actual command buffer from device command pool
        // For now, just initialize to null - will be implemented later
        m_commandBuffer = VK_NULL_HANDLE;
    }
    
    VulkanCommandList::~VulkanCommandList() {
        MR_LOG_INFO("Destroying Vulkan command list...");
        
        // Don't free external command buffers - they're managed elsewhere
        if (m_externalCommandBuffer) {
            m_commandBuffer = VK_NULL_HANDLE;
            return;
        }
        
        // Free command buffer back to command pool
        // Note: If the command pool has already been destroyed, the command buffer
        // will be implicitly freed, so we only need to free it if the pool still exists
        if (m_commandBuffer != VK_NULL_HANDLE && m_device) {
            const auto& functions = VulkanAPI::getFunctions();
            VkDevice device = m_device->getLogicalDevice();
            VkCommandPool commandPool = m_device->getCommandPool();
            
            // Only free if both device and command pool are still valid
            // If command pool is destroyed first, command buffers are implicitly freed
            if (device != VK_NULL_HANDLE && commandPool != VK_NULL_HANDLE && 
                functions.vkFreeCommandBuffers != nullptr) {
                functions.vkFreeCommandBuffers(device, commandPool, 1, &m_commandBuffer);
                m_commandBuffer = VK_NULL_HANDLE;
            } else {
                // Command pool already destroyed, just clear the handle
                m_commandBuffer = VK_NULL_HANDLE;
            }
        }
    }
    
    bool VulkanCommandList::initialize() {
        MR_LOG_INFO("Initializing Vulkan command list...");
        
        if (!m_device) {
            MR_LOG_ERROR("VulkanDevice is null during command list initialization");
            return false;
        }
        
        // Get Vulkan functions and device handles
        const auto& functions = VulkanAPI::getFunctions();
        VkDevice device = m_device->getLogicalDevice();
        VkCommandPool commandPool = m_device->getCommandPool();
        
        if (!device || !commandPool) {
            MR_LOG_ERROR("Invalid Vulkan device or command pool");
            return false;
        }
        
        // Check if VK_EXT_debug_utils is available (for debug builds)
#if defined(_DEBUG) || defined(DEBUG)
        m_debugUtilsAvailable = (functions.vkCmdBeginDebugUtilsLabelEXT != nullptr &&
                                 functions.vkCmdEndDebugUtilsLabelEXT != nullptr &&
                                 functions.vkCmdInsertDebugUtilsLabelEXT != nullptr);
        if (m_debugUtilsAvailable) {
            MR_LOG_INFO("VK_EXT_debug_utils is available - debug markers enabled");
        } else {
            MR_LOG_WARNING("VK_EXT_debug_utils is not available - debug markers disabled");
        }
#else
        m_debugUtilsAvailable = false;
#endif
        
        // Allocate command buffer from device command pool
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        
        VkResult result = functions.vkAllocateCommandBuffers(device, &allocInfo, &m_commandBuffer);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to allocate Vulkan command buffer: " + std::to_string(result));
            return false;
        }
        
        MR_LOG_INFO("Vulkan command list initialized successfully");
        return true;
    }
    
    void VulkanCommandList::bindExternalCommandBuffer(VkCommandBuffer cmdBuffer, bool externallyManaged) {
        MR_LOG_DEBUG("Binding external command buffer to VulkanCommandList");
        m_commandBuffer = cmdBuffer;
        m_externalCommandBuffer = externallyManaged;
        // Don't set m_isRecording - it should be managed by caller
    }
    
    void VulkanCommandList::begin() {
        ensureNotRecording("begin");
        
        if (m_commandBuffer == VK_NULL_HANDLE) {
            MR_LOG_ERROR("Command buffer is null, cannot begin recording");
            return;
        }
        
        MR_LOG_DEBUG("Beginning command list recording");
        
        // When using external command buffer, don't actually call vkBeginCommandBuffer
        // (it's already being recorded by the external context)
        if (m_externalCommandBuffer) {
            MR_LOG_DEBUG("External command buffer - skipping vkBeginCommandBuffer");
            return;
        }
        
        // Begin command buffer recording
        const auto& functions = VulkanAPI::getFunctions();
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; // Following UE5 pattern
        beginInfo.pInheritanceInfo = nullptr;
        
        VkResult result = functions.vkBeginCommandBuffer(m_commandBuffer, &beginInfo);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to begin command buffer recording: " + std::to_string(result));
            return;
        }
        
        m_isRecording = true;
        m_inRenderPass = false;
    }
    
    void VulkanCommandList::end() {
        ensureRecording("end");
        
        if (m_commandBuffer == VK_NULL_HANDLE) {
            MR_LOG_ERROR("Command buffer is null, cannot end recording");
            return;
        }
        
        MR_LOG_DEBUG("Ending command list recording");
        
        // When using external command buffer, don't call vkEndCommandBuffer
        if (m_externalCommandBuffer) {
            MR_LOG_DEBUG("External command buffer - skipping vkEndCommandBuffer");
            return;
        }
        
        // End render pass if still active
        if (m_inRenderPass) {
            const auto& functions = VulkanAPI::getFunctions();
            functions.vkCmdEndRenderPass(m_commandBuffer);
            m_inRenderPass = false;
            MR_LOG_DEBUG("Ended active render pass");
        }
        
        // End command buffer recording
        const auto& functions = VulkanAPI::getFunctions();
        VkResult result = functions.vkEndCommandBuffer(m_commandBuffer);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to end command buffer recording: " + std::to_string(result));
            return;
        }
        
        m_isRecording = false;
    }
    
    void VulkanCommandList::reset() {
        ensureNotRecording("reset");
        
        if (m_commandBuffer == VK_NULL_HANDLE) {
            MR_LOG_ERROR("Command buffer is null, cannot reset");
            return;
        }
        
        MR_LOG_DEBUG("Resetting command list");
        
        // Reset command buffer - allows reuse
        const auto& functions = VulkanAPI::getFunctions();
        VkResult result = functions.vkResetCommandBuffer(m_commandBuffer, 0);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to reset command buffer: " + std::to_string(result));
            return;
        }
        
        // Reset state tracking
        m_isRecording = false;
        m_inRenderPass = false;
        m_currentPipelineState.reset();
        m_boundRenderTargets.clear();
        m_boundDepthStencil.reset();
        m_eventDepth = 0;
    }
    
    void VulkanCommandList::setRenderTargets(TSpan<TSharedPtr<IRHITexture>> renderTargets, 
                                            TSharedPtr<IRHITexture> depthStencil) {
        ensureRecording("setRenderTargets");
        
        if (m_commandBuffer == VK_NULL_HANDLE) {
            MR_LOG_ERROR("Command buffer is null, cannot set render targets");
            return;
        }
        
        MR_LOG_DEBUG("Setting render targets and beginning render pass");
        
        // Store bound render targets for state tracking
        m_boundRenderTargets.clear();
        for (auto& rt : renderTargets) {
            m_boundRenderTargets.push_back(rt);
        }
        m_boundDepthStencil = depthStencil;
        
        // For now, support rendering to the default swapchain framebuffer
        // In the future, this should handle custom render targets
        VkRenderPass renderPass = m_device->getRenderPass();
        VkFramebuffer framebuffer = m_device->getCurrentFramebuffer();
        const VkExtent2D& extent = m_device->getSwapchainExtent();
        
        if (renderPass == VK_NULL_HANDLE || framebuffer == VK_NULL_HANDLE) {
            MR_LOG_ERROR("Invalid render pass or framebuffer");
            return;
        }
        
        // Set up clear values
        VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}}; // Black with full alpha
        
        // Begin render pass
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = renderPass;
        renderPassInfo.framebuffer = framebuffer;
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = extent;
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearColor;
        
        const auto& functions = VulkanAPI::getFunctions();
        functions.vkCmdBeginRenderPass(m_commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        
        m_inRenderPass = true;
        MR_LOG_DEBUG("Render pass began successfully");
    }
    
    void VulkanCommandList::endRenderPass() {
        ensureRecording("endRenderPass");
        
        if (!m_inRenderPass) {
            MR_LOG_WARNING("No active render pass to end");
            return;
        }
        
        const auto& functions = VulkanAPI::getFunctions();
        functions.vkCmdEndRenderPass(m_commandBuffer);
        m_inRenderPass = false;
        MR_LOG_DEBUG("Render pass ended successfully");
    }
    
    void VulkanCommandList::setViewport(const Viewport& viewport) {
        ensureRecording("setViewport");
        
        if (m_commandBuffer == VK_NULL_HANDLE) {
            MR_LOG_ERROR("Command buffer is null, cannot set viewport");
            return;
        }
        
        // Set Vulkan viewport
        VkViewport vkViewport{};
        vkViewport.x = viewport.x;
        vkViewport.y = viewport.y;
        vkViewport.width = viewport.width;
        vkViewport.height = viewport.height;
        vkViewport.minDepth = viewport.minDepth;
        vkViewport.maxDepth = viewport.maxDepth;
        
        const auto& functions = VulkanAPI::getFunctions();
        functions.vkCmdSetViewport(m_commandBuffer, 0, 1, &vkViewport);
        
        MR_LOG_DEBUG("Viewport set: " + std::to_string(viewport.width) + "x" + std::to_string(viewport.height) + 
                    " at (" + std::to_string(viewport.x) + "," + std::to_string(viewport.y) + ")");
    }
    
    void VulkanCommandList::setScissorRect(const ScissorRect& scissorRect) {
        ensureRecording("setScissorRect");
        
        if (m_commandBuffer == VK_NULL_HANDLE) {
            MR_LOG_ERROR("Command buffer is null, cannot set scissor rect");
            return;
        }
        
        // Set Vulkan scissor rect
        VkRect2D scissor{};
        scissor.offset.x = scissorRect.left;
        scissor.offset.y = scissorRect.top;
        scissor.extent.width = scissorRect.right - scissorRect.left;
        scissor.extent.height = scissorRect.bottom - scissorRect.top;
        
        const auto& functions = VulkanAPI::getFunctions();
        functions.vkCmdSetScissor(m_commandBuffer, 0, 1, &scissor);
        
        MR_LOG_DEBUG("Scissor rect set: " + std::to_string(scissor.extent.width) + "x" + std::to_string(scissor.extent.height) + 
                    " at (" + std::to_string(scissor.offset.x) + "," + std::to_string(scissor.offset.y) + ")");
    }
    
    void VulkanCommandList::setPipelineState(TSharedPtr<IRHIPipelineState> pipelineState) {
        ensureRecording("setPipelineState");
        
        if (!pipelineState) {
            MR_LOG_ERROR("Attempting to set null pipeline state");
            return;
        }
        
        if (m_commandBuffer == VK_NULL_HANDLE) {
            MR_LOG_ERROR("Command buffer is null, cannot set pipeline state");
            return;
        }
        
        m_currentPipelineState = pipelineState;
        
        // Cast to VulkanPipelineState and bind the pipeline
        auto* vulkanPipeline = static_cast<VulkanPipelineState*>(pipelineState.get());
        if (!vulkanPipeline) {
            MR_LOG_ERROR("Failed to cast pipeline state to VulkanPipelineState");
            return;
        }
        
        if (!vulkanPipeline->isValid()) {
            MR_LOG_ERROR("Pipeline state is not valid - initialization may have failed");
            MR_LOG_ERROR("Pipeline: " + pipelineState->getDebugName());
            return;
        }
        
        VkPipeline vkPipeline = vulkanPipeline->getPipeline();
        if (vkPipeline == VK_NULL_HANDLE) {
            MR_LOG_ERROR("Vulkan pipeline handle is null");
            MR_LOG_ERROR("Pipeline: " + pipelineState->getDebugName());
            return;
        }
        
        VkPipelineLayout pipelineLayout = vulkanPipeline->getPipelineLayout();
        if (pipelineLayout == VK_NULL_HANDLE) {
            MR_LOG_WARNING("Pipeline layout is null - this may cause issues with resource binding");
        }
        
        const auto& functions = VulkanAPI::getFunctions();
        functions.vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline);
        
        MR_LOG_DEBUG("Pipeline state bound successfully: " + pipelineState->getDebugName());
    }
    
    void VulkanCommandList::setVertexBuffers(uint32 startSlot, TSpan<TSharedPtr<IRHIBuffer>> vertexBuffers) {
        ensureRecording("setVertexBuffers");
        
        if (vertexBuffers.empty()) {
            MR_LOG_WARNING("Attempting to bind empty vertex buffer array");
            return;
        }
        
        if (m_commandBuffer == VK_NULL_HANDLE) {
            MR_LOG_ERROR("Command buffer is null, cannot set vertex buffers");
            return;
        }
        
        // Prepare Vulkan buffer arrays
        TArray<VkBuffer> vkBuffers;
        TArray<VkDeviceSize> vkOffsets;
        
        vkBuffers.reserve(vertexBuffers.size());
        vkOffsets.reserve(vertexBuffers.size());
        
        for (const auto& buffer : vertexBuffers) {
            if (!buffer) {
                MR_LOG_WARNING("Null vertex buffer detected, skipping");
                continue;
            }
            
            // Cast to VulkanBuffer and get the native handle
            auto* vulkanBuffer = static_cast<VulkanBuffer*>(buffer.get());
            VkBuffer vkBuffer = vulkanBuffer->getBuffer();
            
            if (vkBuffer == VK_NULL_HANDLE) {
                MR_LOG_ERROR("Vertex buffer has null Vulkan handle");
                continue;
            }
            
            vkBuffers.push_back(vkBuffer);
            vkOffsets.push_back(0); // Start at beginning of buffer
        }
        
        if (!vkBuffers.empty()) {
            const auto& functions = VulkanAPI::getFunctions();
            functions.vkCmdBindVertexBuffers(m_commandBuffer, startSlot, 
                                           static_cast<uint32>(vkBuffers.size()), 
                                           vkBuffers.data(), vkOffsets.data());
            
            MR_LOG_DEBUG("Bound " + std::to_string(vkBuffers.size()) + " vertex buffer(s) starting at slot " + std::to_string(startSlot));
        } else {
            MR_LOG_ERROR("No valid vertex buffers to bind");
        }
    }
    
    void VulkanCommandList::setIndexBuffer(TSharedPtr<IRHIBuffer> indexBuffer, bool is32Bit) {
        ensureRecording("setIndexBuffer");
        
        if (!indexBuffer) {
            MR_LOG_WARNING("Attempting to bind null index buffer");
            return;
        }
        
        if (m_commandBuffer == VK_NULL_HANDLE) {
            MR_LOG_ERROR("Command buffer is null, cannot set index buffer");
            return;
        }
        
        // Cast to VulkanBuffer and get the native handle
        auto* vulkanBuffer = static_cast<VulkanBuffer*>(indexBuffer.get());
        VkBuffer vkBuffer = vulkanBuffer->getBuffer();
        VkIndexType indexType = is32Bit ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
        
        const auto& functions = VulkanAPI::getFunctions();
        functions.vkCmdBindIndexBuffer(m_commandBuffer, vkBuffer, 0, indexType);
        
        MR_LOG_DEBUG("Index buffer bound with " + std::string(is32Bit ? "32-bit" : "16-bit") + " indices");
    }
    
    void VulkanCommandList::draw(uint32 vertexCount, uint32 startVertexLocation) {
        ensureRecording("draw");
        
        if (m_commandBuffer == VK_NULL_HANDLE) {
            MR_LOG_ERROR("Command buffer is null, cannot draw");
            return;
        }
        
        if (!m_inRenderPass) {
            MR_LOG_ERROR("Cannot draw without an active render pass. Call setRenderTargets first.");
            return;
        }
        
        if (vertexCount == 0) {
            MR_LOG_WARNING("Draw called with 0 vertices");
            return;
        }
        
        // Validate pipeline state is bound (UE5 validation pattern)
        if (!m_currentPipelineState) {
            MR_LOG_ERROR("No pipeline state bound. Call setPipelineState before drawing.");
            return;
        }
        
        // Verify pipeline state is valid
        auto* vulkanPipeline = static_cast<VulkanPipelineState*>(m_currentPipelineState.get());
        if (!vulkanPipeline || !vulkanPipeline->isValid()) {
            MR_LOG_ERROR("Pipeline state is invalid or not properly initialized");
            return;
        }
        
        VkPipeline vkPipeline = vulkanPipeline->getPipeline();
        if (vkPipeline == VK_NULL_HANDLE) {
            MR_LOG_ERROR("Vulkan pipeline handle is null - pipeline not created properly");
            return;
        }
        
        // Update and bind descriptor sets if needed
        updateAndBindDescriptorSets();
        
        // Issue Vulkan draw command
        const auto& functions = VulkanAPI::getFunctions();
        functions.vkCmdDraw(m_commandBuffer, vertexCount, 1, startVertexLocation, 0);
        
        MR_LOG_DEBUG("Draw: " + std::to_string(vertexCount) + " vertices starting at " + std::to_string(startVertexLocation));
    }
    
    void VulkanCommandList::drawIndexed(uint32 indexCount, uint32 startIndexLocation, int32 baseVertexLocation) {
        ensureRecording("drawIndexed");
        
        if (m_commandBuffer == VK_NULL_HANDLE) {
            MR_LOG_ERROR("Command buffer is null, cannot draw indexed");
            return;
        }
        
        if (!m_inRenderPass) {
            MR_LOG_ERROR("Cannot draw without an active render pass. Call setRenderTargets first.");
            return;
        }
        
        if (indexCount == 0) {
            MR_LOG_WARNING("DrawIndexed called with 0 indices");
            return;
        }
        
        // Issue Vulkan indexed draw command
        const auto& functions = VulkanAPI::getFunctions();
        functions.vkCmdDrawIndexed(m_commandBuffer, indexCount, 1, startIndexLocation, baseVertexLocation, 0);
        
        MR_LOG_DEBUG("DrawIndexed: " + std::to_string(indexCount) + " indices starting at " + 
                    std::to_string(startIndexLocation) + " with base vertex " + std::to_string(baseVertexLocation));
    }
    
    void VulkanCommandList::drawInstanced(uint32 vertexCountPerInstance, uint32 instanceCount,
                                        uint32 startVertexLocation, uint32 startInstanceLocation) {
        ensureRecording("drawInstanced");
        
        // TODO: Issue Vulkan instanced draw command
        // vkCmdDraw(m_commandBuffer, vertexCountPerInstance, instanceCount, startVertexLocation, startInstanceLocation);
    }
    
    void VulkanCommandList::drawIndexedInstanced(uint32 indexCountPerInstance, uint32 instanceCount,
                                               uint32 startIndexLocation, int32 baseVertexLocation,
                                               uint32 startInstanceLocation) {
        ensureRecording("drawIndexedInstanced");
        
        // TODO: Issue Vulkan indexed instanced draw command
        // vkCmdDrawIndexed(m_commandBuffer, indexCountPerInstance, instanceCount, startIndexLocation, baseVertexLocation, startInstanceLocation);
    }
    
    void VulkanCommandList::clearRenderTarget(TSharedPtr<IRHITexture> renderTarget, 
                                             const float32 clearColor[4]) {
        ensureRecording("clearRenderTarget");
        
        // TODO: Clear Vulkan render target
        // This requires render pass management or vkCmdClearAttachments
        MR_LOG_ERROR("VulkanCommandList::clearRenderTarget not implemented yet");
    }
    
    void VulkanCommandList::clearDepthStencil(TSharedPtr<IRHITexture> depthStencil, 
                                             bool clearDepth, bool clearStencil,
                                             float32 depth, uint8 stencil) {
        ensureRecording("clearDepthStencil");
        
        // TODO: Clear Vulkan depth/stencil buffer
        // This requires render pass management or vkCmdClearAttachments
        MR_LOG_ERROR("VulkanCommandList::clearDepthStencil not implemented yet");
    }
    
    void VulkanCommandList::transitionResource(TSharedPtr<IRHIResource> resource,
                                              EResourceUsage stateBefore, EResourceUsage stateAfter) {
        ensureRecording("transitionResource");
        
        // TODO: Insert Vulkan pipeline barrier for resource state transition
        MR_LOG_ERROR("VulkanCommandList::transitionResource not implemented yet");
    }
    
    void VulkanCommandList::resourceBarrier() {
        ensureRecording("resourceBarrier");
        
        // TODO: Insert general Vulkan memory barrier
        MR_LOG_ERROR("VulkanCommandList::resourceBarrier not implemented yet");
    }
    
    void VulkanCommandList::beginEvent(const String& name) {
#if defined(_DEBUG) || defined(DEBUG)
        if (m_debugUtilsAvailable && m_commandBuffer != VK_NULL_HANDLE) {
            const auto& functions = VulkanAPI::getFunctions();
            if (functions.vkCmdBeginDebugUtilsLabelEXT) {
                VkDebugUtilsLabelEXT label{};
                label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
                label.pLabelName = name.c_str();
                label.color[0] = 1.0f;
                label.color[1] = 1.0f;
                label.color[2] = 1.0f;
                label.color[3] = 1.0f;
                functions.vkCmdBeginDebugUtilsLabelEXT(m_commandBuffer, &label);
            }
        }
#endif
        ++m_eventDepth;
    }
    
    void VulkanCommandList::endEvent() {
        if (m_eventDepth > 0) {
            --m_eventDepth;
        }
#if defined(_DEBUG) || defined(DEBUG)
        if (m_debugUtilsAvailable && m_commandBuffer != VK_NULL_HANDLE) {
            const auto& functions = VulkanAPI::getFunctions();
            if (functions.vkCmdEndDebugUtilsLabelEXT) {
                functions.vkCmdEndDebugUtilsLabelEXT(m_commandBuffer);
            }
        }
#endif
    }
    
    void VulkanCommandList::setMarker(const String& name) {
#if defined(_DEBUG) || defined(DEBUG)
        if (m_debugUtilsAvailable && m_commandBuffer != VK_NULL_HANDLE) {
            const auto& functions = VulkanAPI::getFunctions();
            if (functions.vkCmdInsertDebugUtilsLabelEXT) {
                VkDebugUtilsLabelEXT label{};
                label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
                label.pLabelName = name.c_str();
                label.color[0] = 1.0f;
                label.color[1] = 0.5f;
                label.color[2] = 0.0f;
                label.color[3] = 1.0f;
                functions.vkCmdInsertDebugUtilsLabelEXT(m_commandBuffer, &label);
            }
        }
#endif
    }
    
    void VulkanCommandList::setShaderUniformBuffer(uint32 slot, TSharedPtr<IRHIBuffer> buffer) {
        ensureRecording("setShaderUniformBuffer");
        
        if (!buffer) {
            MR_LOG_WARNING("Attempting to bind null uniform buffer to slot " + std::to_string(slot));
#if defined(_DEBUG) || defined(DEBUG)
            if (m_debugUtilsAvailable) {
                setMarker("ERROR: Missing uniform buffer binding at slot " + std::to_string(slot));
            }
#endif
            return;
        }
        
        // Track bound resource
        auto& resource = m_boundResources[slot];
        resource.buffer = buffer;
        resource.texture.reset();
        resource.isDirty = true;
        
        MR_LOG_DEBUG("Uniform buffer bound to slot " + std::to_string(slot));
        
        // TODO: Allocate and update descriptor set when pipeline is set
        // For now, this is a placeholder for future descriptor set implementation
    }
    
    void VulkanCommandList::setShaderTexture(uint32 slot, TSharedPtr<IRHITexture> texture) {
        ensureRecording("setShaderTexture");
        
        if (!texture) {
            MR_LOG_WARNING("Attempting to bind null texture to slot " + std::to_string(slot));
#if defined(_DEBUG) || defined(DEBUG)
            if (m_debugUtilsAvailable) {
                setMarker("ERROR: Missing texture binding at slot " + std::to_string(slot));
            }
#endif
            return;
        }
        
        // Track bound resource
        auto& resource = m_boundResources[slot];
        resource.texture = texture;
        resource.buffer.reset();
        resource.isDirty = true;
        
        MR_LOG_DEBUG("Texture bound to slot " + std::to_string(slot));
        
        // TODO: Allocate and update descriptor set when pipeline is set
        // For now, this is a placeholder for future descriptor set implementation
    }
    
    void VulkanCommandList::setShaderSampler(uint32 slot, TSharedPtr<IRHITexture> texture) {
        // For Vulkan, samplers are often combined with textures (combined image sampler)
        // This is a simplified implementation
        setShaderTexture(slot, texture);
    }
    
    // ============================================================================
    // IRHICommandList interface implementations for resource binding
    // ============================================================================
    
    void VulkanCommandList::setConstantBuffer(uint32 slot, TSharedPtr<IRHIBuffer> buffer) {
        // Implementation delegates to UE5-style method
        // setConstantBuffer is the D3D12-style name, setShaderUniformBuffer is UE5-style
        ensureRecording("setConstantBuffer");
        
        if (!buffer) {
            MR_LOG_WARNING("Attempting to bind null constant buffer to slot " + std::to_string(slot));
            return;
        }
        
        // Track bound resource
        auto& resource = m_boundResources[slot];
        resource.buffer = buffer;
        resource.texture.reset();
        resource.sampler.reset();
        resource.isDirty = true;
        m_descriptorsDirty = true;
        
        MR_LOG_DEBUG("Constant buffer bound to slot " + std::to_string(slot));
    }
    
    void VulkanCommandList::setShaderResource(uint32 slot, TSharedPtr<IRHITexture> texture) {
        // setShaderResource binds texture as shader resource view (SRV)
        ensureRecording("setShaderResource");
        
        if (!texture) {
            MR_LOG_WARNING("Attempting to bind null shader resource to slot " + std::to_string(slot));
            return;
        }
        
        // Track bound resource
        auto& resource = m_boundResources[slot];
        resource.texture = texture;
        resource.buffer.reset();
        resource.isDirty = true;
        m_descriptorsDirty = true;
        
        MR_LOG_DEBUG("Shader resource (texture) bound to slot " + std::to_string(slot));
    }
    
    void VulkanCommandList::setSampler(uint32 slot, TSharedPtr<IRHISampler> sampler) {
        // setSampler binds a sampler state object
        ensureRecording("setSampler");
        
        // Track bound sampler (samplers are stored separately in Vulkan)
        m_boundSamplers[slot] = sampler;
        
        // Also update the resource entry if it exists
        if (m_boundResources.find(slot) != m_boundResources.end()) {
            m_boundResources[slot].sampler = sampler;
            m_boundResources[slot].isDirty = true;
        }
        m_descriptorsDirty = true;
        
        MR_LOG_DEBUG("Sampler bound to slot " + std::to_string(slot));
    }
    
    void VulkanCommandList::ensureNotRecording(const char* operation) const {
        if (m_isRecording) {
            MR_LOG_ERROR(String("Cannot call ") + operation + " while command list is recording");
        }
    }
    
    void VulkanCommandList::ensureRecording(const char* operation) const {
        // When using external command buffer, skip recording check - it's managed externally
        if (m_externalCommandBuffer) {
            return;
        }
        
        if (!m_isRecording) {
            MR_LOG_ERROR(String("Cannot call ") + operation + " while command list is not recording");
        }
    }
    
    void VulkanCommandList::updateAndBindDescriptorSets() {
        // Check if we have a pipeline state with descriptor set layouts
        if (!m_currentPipelineState) {
            return;
        }
        
        // Check if any resources are bound
        if (m_boundResources.empty()) {
            return;
        }
        
        auto* vulkanPipeline = static_cast<VulkanPipelineState*>(m_currentPipelineState.get());
        VkPipelineLayout pipelineLayout = vulkanPipeline->getPipelineLayout();
        
        if (pipelineLayout == VK_NULL_HANDLE) {
            return;
        }
        
        // Get descriptor set allocator
        auto* allocator = m_device->getDescriptorSetAllocator();
        if (!allocator) {
            MR_LOG_WARNING("Descriptor set allocator not available");
            return;
        }
        
        // Check if any resources are dirty (need update)
        bool needsUpdate = false;
        for (const auto& [slot, resource] : m_boundResources) {
            if (resource.isDirty) {
                needsUpdate = true;
                break;
            }
        }
        
        if (!needsUpdate && m_currentDescriptorSet != VK_NULL_HANDLE) {
            // Descriptor set is still valid, just bind it
            const auto& functions = VulkanAPI::getFunctions();
            functions.vkCmdBindDescriptorSets(
                m_commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelineLayout,
                0, // first set
                1, // descriptor set count
                &m_currentDescriptorSet,
                0, // dynamic offset count
                nullptr // dynamic offsets
            );
            return;
        }
        
        // Get bindings from shaders
        TArray<VkDescriptorSetLayoutBinding> allBindings;
        if (m_currentPipelineState->getDesc().vertexShader) {
            auto* vulkanVS = static_cast<VulkanVertexShader*>(m_currentPipelineState->getDesc().vertexShader.get());
            const auto& bindings = vulkanVS->getDescriptorBindings();
            for (const auto& binding : bindings) {
                allBindings.push_back(binding);
            }
        }
        if (m_currentPipelineState->getDesc().pixelShader) {
            auto* vulkanPS = static_cast<VulkanPixelShader*>(m_currentPipelineState->getDesc().pixelShader.get());
            const auto& bindings = vulkanPS->getDescriptorBindings();
            for (const auto& binding : bindings) {
                // Merge with existing
                bool found = false;
                for (auto& existing : allBindings) {
                    if (existing.binding == binding.binding) {
                        existing.stageFlags |= binding.stageFlags;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    allBindings.push_back(binding);
                }
            }
        }
        
        if (allBindings.empty()) {
            return; // No bindings needed
        }
        
        // Get descriptor set layout from pipeline (assume set 0)
        auto& descriptorLayouts = vulkanPipeline->getDescriptorSetLayouts();
        if (descriptorLayouts.empty()) {
            return;
        }
        
        // Allocate descriptor set
        m_currentDescriptorSet = allocator->allocate(descriptorLayouts[0], allBindings);
        if (m_currentDescriptorSet == VK_NULL_HANDLE) {
            MR_LOG_ERROR("Failed to allocate descriptor set");
            return;
        }
        
        // Collect bound buffers and textures
        TMap<uint32, TSharedPtr<IRHIBuffer>> buffers;
        TMap<uint32, TSharedPtr<IRHITexture>> textures;
        for (const auto& [slot, resource] : m_boundResources) {
            if (resource.buffer) {
                buffers[slot] = resource.buffer;
            }
            if (resource.texture) {
                textures[slot] = resource.texture;
            }
        }
        
        // Update descriptor set
        allocator->updateDescriptorSet(m_currentDescriptorSet, allBindings, buffers, textures);
        
        // Bind descriptor set
        const auto& functions = VulkanAPI::getFunctions();
        functions.vkCmdBindDescriptorSets(
            m_commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout,
            0, // first set
            1, // descriptor set count
            &m_currentDescriptorSet,
            0, // dynamic offset count
            nullptr // dynamic offsets
        );
        
        // Clear dirty flags
        for (auto& [slot, resource] : m_boundResources) {
            resource.isDirty = false;
        }
        
        MR_LOG_DEBUG("Descriptor set allocated, updated, and bound successfully");
    }
    
    // ============================================================================
    // Texture Upload Operations Implementation
    // ============================================================================
    
    void VulkanCommandList::copyBufferToTexture(
        TSharedPtr<IRHIBuffer> srcBuffer,
        uint64 srcOffset,
        TSharedPtr<IRHITexture> dstTexture,
        uint32 mipLevel,
        uint32 arrayLayer,
        uint32 width,
        uint32 height,
        uint32 depth) {
        
        ensureRecording("copyBufferToTexture");
        
        if (!srcBuffer || !dstTexture) {
            MR_LOG_ERROR("Invalid buffer or texture in copyBufferToTexture");
            return;
        }
        
        // Cast to Vulkan types
        auto* vulkanBuffer = static_cast<VulkanBuffer*>(srcBuffer.get());
        auto* vulkanTexture = static_cast<VulkanTexture*>(dstTexture.get());
        
        VkBuffer buffer = vulkanBuffer->getBuffer();
        VkImage image = vulkanTexture->getImage();
        
        if (buffer == VK_NULL_HANDLE || image == VK_NULL_HANDLE) {
            MR_LOG_ERROR("Invalid Vulkan handles in copyBufferToTexture");
            return;
        }
        
        // Setup buffer image copy region
        VkBufferImageCopy region{};
        region.bufferOffset = srcOffset;
        region.bufferRowLength = 0;    // Tightly packed
        region.bufferImageHeight = 0;  // Tightly packed
        
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = mipLevel;
        region.imageSubresource.baseArrayLayer = arrayLayer;
        region.imageSubresource.layerCount = 1;
        
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {width, height, depth};
        
        // Execute copy command
        const auto& functions = VulkanAPI::getFunctions();
        functions.vkCmdCopyBufferToImage(
            m_commandBuffer,
            buffer,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, // Image must be in this layout
            1,
            &region
        );
        
        MR_LOG_DEBUG("Recorded buffer to texture copy command: " +
                     std::to_string(width) + "x" + std::to_string(height) + 
                     " mip=" + std::to_string(mipLevel));
    }
    
    void VulkanCommandList::transitionTextureLayout(
        TSharedPtr<IRHITexture> texture,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        VkAccessFlags srcAccessMask,
        VkAccessFlags dstAccessMask,
        VkPipelineStageFlags srcStageMask,
        VkPipelineStageFlags dstStageMask) {
        
        ensureRecording("transitionTextureLayout");
        
        if (!texture) {
            MR_LOG_ERROR("Invalid texture in transitionTextureLayout");
            return;
        }
        
        auto* vulkanTexture = static_cast<VulkanTexture*>(texture.get());
        VkImage image = vulkanTexture->getImage();
        
        if (image == VK_NULL_HANDLE) {
            MR_LOG_ERROR("Invalid Vulkan image in transitionTextureLayout");
            return;
        }
        
        // Setup image memory barrier
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = texture->getDesc().mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = texture->getDesc().arraySize;
        barrier.srcAccessMask = srcAccessMask;
        barrier.dstAccessMask = dstAccessMask;
        
        // Insert pipeline barrier
        const auto& functions = VulkanAPI::getFunctions();
        functions.vkCmdPipelineBarrier(
            m_commandBuffer,
            srcStageMask,
            dstStageMask,
            0, // dependency flags
            0, nullptr, // memory barriers
            0, nullptr, // buffer memory barriers
            1, &barrier // image memory barriers
        );
        
        // Update texture's current layout
        vulkanTexture->setCurrentLayout(newLayout);
        
        MR_LOG_DEBUG("Transitioned texture layout from " + 
                     std::to_string(oldLayout) + " to " + std::to_string(newLayout));
    }
    
    void VulkanCommandList::transitionTextureLayoutSimple(
        TSharedPtr<IRHITexture> texture,
        VkImageLayout oldLayout,
        VkImageLayout newLayout) {
        
        // Determine access masks and pipeline stages based on layouts
        VkAccessFlags srcAccessMask = 0;
        VkAccessFlags dstAccessMask = 0;
        VkPipelineStageFlags srcStageMask = 0;
        VkPipelineStageFlags dstStageMask = 0;
        
        // Source layout
        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
            srcAccessMask = 0;
            srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        
        // Destination layout
        if (newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        } else if (newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        } else if (newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
            dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        
        // Call full version with determined parameters
        transitionTextureLayout(
            texture,
            oldLayout,
            newLayout,
            srcAccessMask,
            dstAccessMask,
            srcStageMask,
            dstStageMask
        );
    }
}
