// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Vulkan RHI Command List Implementation
//
// Reference: UE5 Engine/Source/Runtime/VulkanRHI/Private/VulkanCommandList.cpp
//            UE5 Engine/Source/Runtime/VulkanRHI/Private/VulkanCommands.cpp

#include "Platform/Vulkan/VulkanRHICommandList.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanCommandListContext.h"
#include "Platform/Vulkan/VulkanPendingState.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanPipelineState.h"
#include "Platform/Vulkan/VulkanTexture.h"
#include "Core/Log.h"

namespace MonsterRender::RHI::Vulkan {
    
    // ============================================================================
    // FVulkanRHICommandListImmediate Implementation
    // ============================================================================
    
    FVulkanRHICommandListImmediate::FVulkanRHICommandListImmediate(VulkanDevice* device)
        : m_device(device)
        , m_context(nullptr) {
        MR_LOG_DEBUG("FVulkanRHICommandListImmediate: Created command list for device");
    }
    
    FVulkanRHICommandListImmediate::~FVulkanRHICommandListImmediate() {
        MR_LOG_DEBUG("FVulkanRHICommandListImmediate: Destroyed command list");
        // Context is owned by device, we don't delete it
        m_context = nullptr;
    }
    
    FVulkanCommandListContext* FVulkanRHICommandListImmediate::getCurrentContext() const {
        // Get the current active context from the device
        // This is set by the device before using the command list
        return m_context;
    }
    
    // ============================================================================
    // Command Buffer Lifecycle (UE5: FVulkanCommandListContext)
    // ============================================================================
    
    void FVulkanRHICommandListImmediate::begin() {
        // Get the command list context from device
        // UE5 Pattern: Device manages context lifecycle
        m_context = m_device->getCommandListContext();
        if (!m_context) {
            MR_LOG_ERROR("FVulkanRHICommandListImmediate::begin: Failed to get command list context");
            return;
        }
        
        // Begin recording into the current frame's command buffer
        // UE5: VulkanRHI::BeginDrawingViewport() -> GetCommandContext()->Begin()
        m_context->beginRecording();
        
        MR_LOG_DEBUG("FVulkanRHICommandListImmediate::begin: Command buffer recording started");
    }
    
    void FVulkanRHICommandListImmediate::end() {
        if (!m_context) {
            MR_LOG_WARNING("FVulkanRHICommandListImmediate::end: No active context");
            return;
        }
        
        // End recording the command buffer
        // UE5: FVulkanCommandListContext::EndRecording()
        m_context->endRecording();
        
        MR_LOG_DEBUG("FVulkanRHICommandListImmediate::end: Command buffer recording ended");
    }
    
    void FVulkanRHICommandListImmediate::reset() {
        // In UE5, reset doesn't do much for immediate command lists
        // The context handles the per-frame recycling automatically
        // UE5: FVulkanCommandListContext handles reset internally during frame begin
        
        if (m_context) {
            // Optional: Reset pending state if needed
            // m_context->getPendingState()->reset();
        }
        
        MR_LOG_DEBUG("FVulkanRHICommandListImmediate::reset: Command list reset (no-op for immediate)");
    }
    
    // ============================================================================
    // Pipeline State Binding (UE5: RHISetGraphicsPipelineState)
    // ============================================================================
    
    void FVulkanRHICommandListImmediate::setPipelineState(TSharedPtr<IRHIPipelineState> pipelineState) {
        if (!m_context) {
            MR_LOG_ERROR("FVulkanRHICommandListImmediate::setPipelineState: No active context");
            return;
        }
        
        if (!pipelineState) {
            MR_LOG_ERROR("FVulkanRHICommandListImmediate::setPipelineState: Null pipeline state");
            return;
        }
        
        // Cast to Vulkan-specific pipeline state
        // UE5 Pattern: Dynamic cast to platform-specific implementation
        VulkanPipelineState* vulkanPipeline = dynamic_cast<VulkanPipelineState*>(pipelineState.get());
        if (!vulkanPipeline) {
            MR_LOG_ERROR("FVulkanRHICommandListImmediate::setPipelineState: Invalid pipeline state type");
            return;
        }
        
        // Set the graphics pipeline in pending state
        // UE5: FVulkanPendingState::SetGraphicsPipeline()
        if (m_context->getPendingState()) {
            m_context->getPendingState()->setGraphicsPipeline(vulkanPipeline);
            MR_LOG_DEBUG("FVulkanRHICommandListImmediate::setPipelineState: Pipeline state set");
        } else {
            MR_LOG_ERROR("FVulkanRHICommandListImmediate::setPipelineState: No pending state available");
        }
    }
    
    // ============================================================================
    // Vertex/Index Buffer Binding (UE5: RHISetStreamSource, RHISetIndexBuffer)
    // ============================================================================
    
    void FVulkanRHICommandListImmediate::setVertexBuffers(uint32 startSlot, 
                                                         TSpan<TSharedPtr<IRHIBuffer>> vertexBuffers) {
        if (!m_context || !m_context->getPendingState()) {
            MR_LOG_ERROR("FVulkanRHICommandListImmediate::setVertexBuffers: No active context or pending state");
            return;
        }
        
        if (vertexBuffers.empty()) {
            MR_LOG_WARNING("FVulkanRHICommandListImmediate::setVertexBuffers: Empty buffer span");
            return;
        }
        
        // UE5 Pattern: Bind vertex buffers to stream sources
        // UE5: FVulkanPendingState::SetStreamSource()
        for (uint32 i = 0; i < vertexBuffers.size(); ++i) {
            auto& buffer = vertexBuffers[i];
            if (!buffer) {
                MR_LOG_WARNING("FVulkanRHICommandListImmediate::setVertexBuffers: Null buffer at slot " + 
                             std::to_string(startSlot + i));
                continue;
            }
            
            // Cast to Vulkan buffer to get VkBuffer handle
            VulkanBuffer* vulkanBuffer = dynamic_cast<VulkanBuffer*>(buffer.get());
            if (!vulkanBuffer) {
                MR_LOG_ERROR("FVulkanRHICommandListImmediate::setVertexBuffers: Invalid buffer type at slot " + 
                           std::to_string(startSlot + i));
                continue;
            }
            
            // Set vertex buffer in pending state
            VkBuffer vkBuffer = vulkanBuffer->getBuffer();
            VkDeviceSize offset = vulkanBuffer->getOffset();
            m_context->getPendingState()->setVertexBuffer(startSlot + i, vkBuffer, offset);
        }
        
        MR_LOG_DEBUG("FVulkanRHICommandListImmediate::setVertexBuffers: Set " + 
                    std::to_string(vertexBuffers.size()) + " vertex buffers starting at slot " + 
                    std::to_string(startSlot));
    }
    
    void FVulkanRHICommandListImmediate::setIndexBuffer(TSharedPtr<IRHIBuffer> indexBuffer, bool is32Bit) {
        if (!m_context || !m_context->getPendingState()) {
            MR_LOG_ERROR("FVulkanRHICommandListImmediate::setIndexBuffer: No active context or pending state");
            return;
        }
        
        if (!indexBuffer) {
            MR_LOG_ERROR("FVulkanRHICommandListImmediate::setIndexBuffer: Null index buffer");
            return;
        }
        
        // Cast to Vulkan buffer
        // UE5 Pattern: Extract platform-specific handle
        VulkanBuffer* vulkanBuffer = dynamic_cast<VulkanBuffer*>(indexBuffer.get());
        if (!vulkanBuffer) {
            MR_LOG_ERROR("FVulkanRHICommandListImmediate::setIndexBuffer: Invalid buffer type");
            return;
        }
        
        // Determine index type based on size
        // UE5: Supports both 16-bit and 32-bit indices
        VkIndexType indexType = is32Bit ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
        
        // Set index buffer in pending state
        // UE5: FVulkanPendingState::SetIndexBuffer()
        VkBuffer vkBuffer = vulkanBuffer->getBuffer();
        VkDeviceSize offset = vulkanBuffer->getOffset();
        m_context->getPendingState()->setIndexBuffer(vkBuffer, offset, indexType);
        
        MR_LOG_DEBUG("FVulkanRHICommandListImmediate::setIndexBuffer: Index buffer set (" + 
                    String(is32Bit ? "32-bit" : "16-bit") + ")");
    }
    
    
    // ============================================================================
    // Viewport and Scissor State (UE5: RHISetViewport, RHISetScissorRect)
    // ============================================================================
    
    void FVulkanRHICommandListImmediate::setViewport(const Viewport& viewport) {
        if (!m_context) {
            MR_LOG_ERROR("FVulkanRHICommandListImmediate::setViewport: No active context");
            return;
        }
        
        // Set viewport in pending state
        // UE5 Pattern: FVulkanPendingState::SetViewport()
        // Viewport is applied during prepareForDraw() before actual draw call
        m_context->getPendingState()->setViewport(viewport);
        
        MR_LOG_DEBUG("FVulkanRHICommandListImmediate::setViewport: Set viewport (" + 
                    std::to_string(viewport.width) + "x" + std::to_string(viewport.height) + ")");
    }
    
    void FVulkanRHICommandListImmediate::setScissorRect(const ScissorRect& scissorRect) {
        if (!m_context || !m_context->getPendingState()) {
            MR_LOG_ERROR("FVulkanRHICommandListImmediate::setScissorRect: No active context or pending state");
            return;
        }
        
        // Set scissor rectangle in pending state
        // UE5 Pattern: FVulkanPendingState::SetScissor()
        // Scissor is applied during prepareForDraw() before actual draw call
        m_context->getPendingState()->setScissor(scissorRect);
        
        MR_LOG_DEBUG("FVulkanRHICommandListImmediate::setScissorRect: Set scissor rect (" + 
                    std::to_string(scissorRect.right - scissorRect.left) + "x" + 
                    std::to_string(scissorRect.bottom - scissorRect.top) + ")");
    }
    
    // ============================================================================
    // Render Target Management (UE5: RHISetRenderTargets)
    // ============================================================================
    
    void FVulkanRHICommandListImmediate::setRenderTargets(TSpan<TSharedPtr<IRHITexture>> renderTargets, 
                                                         TSharedPtr<IRHITexture> depthStencil) {
        if (!m_context) {
            MR_LOG_ERROR("FVulkanRHICommandListImmediate::setRenderTargets: No active context");
            return;
        }
        
        // Begin render pass with specified render targets
        // UE5 Pattern: FVulkanCommandListContext::RHISetRenderTargets()
        // This creates/begins a VkRenderPass with the specified attachments
        m_context->setRenderTargets(renderTargets, depthStencil);
        
        MR_LOG_DEBUG("FVulkanRHICommandListImmediate::setRenderTargets: Set " + 
                    std::to_string(renderTargets.size()) + " render target(s)" + 
                    (depthStencil ? " with depth/stencil" : ""));
    }
    
    void FVulkanRHICommandListImmediate::endRenderPass() {
        if (!m_context) {
            MR_LOG_WARNING("FVulkanRHICommandListImmediate::endRenderPass: No active context");
            return;
        }
        
        // End the current render pass
        // UE5 Pattern: FVulkanCommandListContext::RHIEndRenderPass()
        // Calls vkCmdEndRenderPass
        m_context->endRenderPass();
        
        MR_LOG_DEBUG("FVulkanRHICommandListImmediate::endRenderPass: Render pass ended");
    }
    
    // ============================================================================
    // Draw Commands (UE5: RHIDrawPrimitive, RHIDrawIndexedPrimitive)
    // ============================================================================
    
    void FVulkanRHICommandListImmediate::draw(uint32 vertexCount, uint32 startVertexLocation) {
        if (!m_context) {
            MR_LOG_ERROR("FVulkanRHICommandListImmediate::draw: No active context");
            return;
        }
        
        // Draw non-indexed primitives
        // UE5 Pattern: FVulkanCommandListContext::RHIDrawPrimitive()
        // Prepares pending state and calls vkCmdDraw
        m_context->draw(vertexCount, startVertexLocation);
        
        MR_LOG_DEBUG("FVulkanRHICommandListImmediate::draw: Drew " + std::to_string(vertexCount) + 
                    " vertices starting at " + std::to_string(startVertexLocation));
    }
    
    void FVulkanRHICommandListImmediate::drawIndexed(uint32 indexCount, uint32 startIndexLocation, 
                                                     int32 baseVertexLocation) {
        if (!m_context) {
            MR_LOG_ERROR("FVulkanRHICommandListImmediate::drawIndexed: No active context");
            return;
        }
        
        // Draw indexed primitives
        // UE5 Pattern: FVulkanCommandListContext::RHIDrawIndexedPrimitive()
        // Prepares pending state and calls vkCmdDrawIndexed
        m_context->drawIndexed(indexCount, startIndexLocation, baseVertexLocation);
        
        MR_LOG_DEBUG("FVulkanRHICommandListImmediate::drawIndexed: Drew " + std::to_string(indexCount) + 
                    " indices starting at " + std::to_string(startIndexLocation) + 
                    " with base vertex " + std::to_string(baseVertexLocation));
    }
    
    void FVulkanRHICommandListImmediate::drawInstanced(uint32 vertexCountPerInstance, uint32 instanceCount,
                                                       uint32 startVertexLocation, uint32 startInstanceLocation) {
        if (!m_context) {
            MR_LOG_ERROR("FVulkanRHICommandListImmediate::drawInstanced: No active context");
            return;
        }
        
        // Draw instanced non-indexed primitives
        // UE5 Pattern: FVulkanCommandListContext::RHIDrawPrimitiveInstanced()
        // Calls vkCmdDraw with instance count
        m_context->drawInstanced(vertexCountPerInstance, instanceCount, 
                                 startVertexLocation, startInstanceLocation);
        
        MR_LOG_DEBUG("FVulkanRHICommandListImmediate::drawInstanced: Drew " + 
                    std::to_string(instanceCount) + " instances of " + 
                    std::to_string(vertexCountPerInstance) + " vertices");
    }
    
    void FVulkanRHICommandListImmediate::drawIndexedInstanced(uint32 indexCountPerInstance, uint32 instanceCount,
                                                              uint32 startIndexLocation, int32 baseVertexLocation,
                                                              uint32 startInstanceLocation) {
        if (!m_context) {
            MR_LOG_ERROR("FVulkanRHICommandListImmediate::drawIndexedInstanced: No active context");
            return;
        }
        
        // Draw instanced indexed primitives
        // UE5 Pattern: FVulkanCommandListContext::RHIDrawIndexedPrimitiveInstanced()
        // Calls vkCmdDrawIndexed with instance count
        m_context->drawIndexedInstanced(indexCountPerInstance, instanceCount, 
                                       startIndexLocation, baseVertexLocation, startInstanceLocation);
        
        MR_LOG_DEBUG("FVulkanRHICommandListImmediate::drawIndexedInstanced: Drew " + 
                    std::to_string(instanceCount) + " instances of " + 
                    std::to_string(indexCountPerInstance) + " indices");
    }
    
    // ============================================================================
    // Clear Operations (UE5: RHIClearMRT, RHIClearDepthStencilImage)
    // ============================================================================
    
    void FVulkanRHICommandListImmediate::clearRenderTarget(TSharedPtr<IRHITexture> renderTarget, 
                                                          const float32 clearColor[4]) {
        if (!m_context) {
            MR_LOG_ERROR("FVulkanRHICommandListImmediate::clearRenderTarget: No active context");
            return;
        }
        
        // UE5 Pattern: Clear is typically handled by render pass load operations
        // For explicit clear, use vkCmdClearColorImage
        // Reference: FVulkanCommandListContext::RHIClearMRT()
        
        if (!renderTarget) {
            MR_LOG_ERROR("FVulkanRHICommandListImmediate::clearRenderTarget: Null render target");
            return;
        }
        
        // TODO: Implement explicit clear using vkCmdClearColorImage
        // For now, clear should be handled by render pass LoadOp = CLEAR
        MR_LOG_DEBUG("FVulkanRHICommandListImmediate::clearRenderTarget: Clear requested (handled by render pass)");
    }
    
    void FVulkanRHICommandListImmediate::clearDepthStencil(TSharedPtr<IRHITexture> depthStencil, 
                                                          bool clearDepth, bool clearStencil,
                                                          float32 depth, uint8 stencil) {
        if (!m_context) {
            MR_LOG_ERROR("FVulkanRHICommandListImmediate::clearDepthStencil: No active context");
            return;
        }
        
        // UE5 Pattern: Depth/stencil clear is handled by render pass load operations
        // For explicit clear, use vkCmdClearDepthStencilImage
        // Reference: FVulkanCommandListContext::RHIClearDepthStencilImage()
        
        if (!depthStencil) {
            MR_LOG_ERROR("FVulkanRHICommandListImmediate::clearDepthStencil: Null depth/stencil texture");
            return;
        }
        
        // TODO: Implement explicit clear using vkCmdClearDepthStencilImage
        // For now, clear should be handled by render pass LoadOp = CLEAR
        MR_LOG_DEBUG("FVulkanRHICommandListImmediate::clearDepthStencil: Clear requested (depth=" + 
                    std::to_string(depth) + ", stencil=" + std::to_string(stencil) + ")");
    }
    
    // ============================================================================
    // Resource Transitions and Barriers (UE5: RHITransitionResources)
    // ============================================================================
    
    void FVulkanRHICommandListImmediate::transitionResource(TSharedPtr<IRHIResource> resource, 
                                                           EResourceUsage stateBefore, EResourceUsage stateAfter) {
        if (!m_context) {
            MR_LOG_ERROR("FVulkanRHICommandListImmediate::transitionResource: No active context");
            return;
        }
        
        // UE5 Pattern: Resource transitions are handled automatically by render graph
        // or explicitly via pipeline barriers
        // Reference: FVulkanCommandListContext::RHITransitionResources()
        
        if (!resource) {
            MR_LOG_WARNING("FVulkanRHICommandListImmediate::transitionResource: Null resource");
            return;
        }
        
        // TODO: Implement pipeline barrier for resource transition
        // UE5 uses VkImageMemoryBarrier and VkBufferMemoryBarrier
        // For simple triangle rendering, transitions are implicit
        
        MR_LOG_DEBUG("FVulkanRHICommandListImmediate::transitionResource: Transition requested (implicit)");
    }
    
    void FVulkanRHICommandListImmediate::resourceBarrier() {
        if (!m_context) {
            MR_LOG_WARNING("FVulkanRHICommandListImmediate::resourceBarrier: No active context");
            return;
        }
        
        // UE5 Pattern: Resource barriers are inserted automatically by the command list
        // or explicitly via vkCmdPipelineBarrier
        // Reference: FVulkanCommandListContext::RHISubmitCommandsHint()
        
        // TODO: Insert pipeline barrier if needed
        // For now, Vulkan synchronization is handled implicitly by render passes
        
        MR_LOG_DEBUG("FVulkanRHICommandListImmediate::resourceBarrier: Barrier inserted (implicit)");
    }
    
    // ============================================================================
    // Debug Markers (UE5: RHIPushEvent, RHIPopEvent)
    // ============================================================================
    
    void FVulkanRHICommandListImmediate::beginEvent(const String& name) {
        if (!m_context) {
            return;
        }
        
        // UE5 Pattern: Debug markers using VK_EXT_debug_utils or VK_EXT_debug_marker
        // Reference: FVulkanCommandListContext::RHIPushEvent()
        
        #if defined(_DEBUG) || defined(MR_ENABLE_DEBUG_MARKERS)
            // TODO: Implement debug markers
            // VkDebugUtilsLabelEXT or vkCmdDebugMarkerBeginEXT
            MR_LOG_DEBUG("FVulkanRHICommandListImmediate::beginEvent: " + name);
        #else
            (void)name; // Suppress unused warning
        #endif
    }
    
    void FVulkanRHICommandListImmediate::endEvent() {
        if (!m_context) {
            return;
        }
        
        // UE5 Pattern: Pop debug event marker
        // Reference: FVulkanCommandListContext::RHIPopEvent()
        
        #if defined(_DEBUG) || defined(MR_ENABLE_DEBUG_MARKERS)
            // TODO: Implement debug markers
            // VkDebugUtilsLabelEXT or vkCmdDebugMarkerEndEXT
            MR_LOG_DEBUG("FVulkanRHICommandListImmediate::endEvent");
        #endif
    }
    
    void FVulkanRHICommandListImmediate::setMarker(const String& name) {
        if (!m_context) {
            return;
        }
        
        // UE5 Pattern: Insert single debug marker
        // Reference: FVulkanCommandListContext::RHISetMarker()
        
        #if defined(_DEBUG) || defined(MR_ENABLE_DEBUG_MARKERS)
            // TODO: Implement debug markers
            // vkCmdDebugMarkerInsertEXT or vkCmdInsertDebugUtilsLabelEXT
            MR_LOG_DEBUG("FVulkanRHICommandListImmediate::setMarker: " + name);
        #else
            (void)name; // Suppress unused warning
        #endif
    }
}
