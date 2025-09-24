#include "Platform/Vulkan/VulkanCommandList.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Core/Log.h"

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
        
        // Free command buffer back to command pool
        if (m_commandBuffer != VK_NULL_HANDLE && m_device) {
            const auto& functions = VulkanAPI::getFunctions();
            VkDevice device = m_device->getLogicalDevice();
            VkCommandPool commandPool = m_device->getCommandPool();
            
            if (device && commandPool) {
                functions.vkFreeCommandBuffers(device, commandPool, 1, &m_commandBuffer);
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
    
    void VulkanCommandList::begin() {
        ensureNotRecording("begin");
        
        if (m_commandBuffer == VK_NULL_HANDLE) {
            MR_LOG_ERROR("Command buffer is null, cannot begin recording");
            return;
        }
        
        MR_LOG_DEBUG("Beginning command list recording");
        
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
        
        MR_LOG_DEBUG("Setting render targets");
        
        // Store bound render targets for state tracking
        m_boundRenderTargets.clear();
        for (auto& rt : renderTargets) {
            m_boundRenderTargets.push_back(rt);
        }
        m_boundDepthStencil = depthStencil;
        
        // TODO: Implement Vulkan render pass begin/framebuffer binding
        // This involves creating/caching render pass objects and framebuffers
        
        m_inRenderPass = true;
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
        
        // TODO: Set Vulkan scissor rect
        // VkRect2D scissor{};
        // scissor.offset.x = scissorRect.left;
        // scissor.offset.y = scissorRect.top;
        // scissor.extent.width = scissorRect.right - scissorRect.left;
        // scissor.extent.height = scissorRect.bottom - scissorRect.top;
        // vkCmdSetScissor(m_commandBuffer, 0, 1, &scissor);
    }
    
    void VulkanCommandList::setPipelineState(TSharedPtr<IRHIPipelineState> pipelineState) {
        ensureRecording("setPipelineState");
        
        if (!pipelineState) {
            MR_LOG_WARNING("Attempting to set null pipeline state");
            return;
        }
        
        if (m_commandBuffer == VK_NULL_HANDLE) {
            MR_LOG_ERROR("Command buffer is null, cannot set pipeline state");
            return;
        }
        
        m_currentPipelineState = pipelineState;
        
        // TODO: Bind Vulkan pipeline when VulkanPipelineState is implemented
        // For now, just log the operation
        MR_LOG_DEBUG("Pipeline state set (actual binding deferred until VulkanPipelineState is implemented)");
        
        // Future implementation:
        // auto* vulkanPipeline = static_cast<VulkanPipelineState*>(pipelineState.get());
        // VkPipeline vkPipeline = vulkanPipeline->getPipeline();
        // const auto& functions = VulkanAPI::getFunctions();
        // functions.vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline);
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
            vkBuffers.push_back(vulkanBuffer->getBuffer());
            vkOffsets.push_back(0); // Start at beginning of buffer
        }
        
        if (!vkBuffers.empty()) {
            const auto& functions = VulkanAPI::getFunctions();
            functions.vkCmdBindVertexBuffers(m_commandBuffer, startSlot, 
                                           static_cast<uint32>(vkBuffers.size()), 
                                           vkBuffers.data(), vkOffsets.data());
            
            MR_LOG_DEBUG("Bound " + std::to_string(vkBuffers.size()) + " vertex buffer(s) starting at slot " + std::to_string(startSlot));
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
        
        if (vertexCount == 0) {
            MR_LOG_WARNING("Draw called with 0 vertices");
            return;
        }
        
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
        // TODO: Use VK_EXT_debug_utils for debug markers
        ++m_eventDepth;
    }
    
    void VulkanCommandList::endEvent() {
        if (m_eventDepth > 0) {
            --m_eventDepth;
        }
        // TODO: Use VK_EXT_debug_utils for debug markers
    }
    
    void VulkanCommandList::setMarker(const String& name) {
        // TODO: Use VK_EXT_debug_utils for debug markers
    }
    
    void VulkanCommandList::ensureNotRecording(const char* operation) const {
        if (m_isRecording) {
            MR_LOG_ERROR(String("Cannot call ") + operation + " while command list is recording");
        }
    }
    
    void VulkanCommandList::ensureRecording(const char* operation) const {
        if (!m_isRecording) {
            MR_LOG_ERROR(String("Cannot call ") + operation + " while command list is not recording");
        }
    }
}
