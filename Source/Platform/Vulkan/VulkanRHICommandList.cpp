#include "Platform/Vulkan/VulkanRHICommandList.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanCommandListContext.h"
#include "Platform/Vulkan/VulkanPendingState.h"

namespace MonsterRender::RHI::Vulkan {
    
    FVulkanRHICommandListImmediate::FVulkanRHICommandListImmediate(VulkanDevice* device)
        : m_device(device), m_context(nullptr) {
        // Context will be set by device when needed
    }
    
    FVulkanRHICommandListImmediate::~FVulkanRHICommandListImmediate() {
        // Context is owned by device, we don't delete it
    }
    
    FVulkanCommandListContext* FVulkanRHICommandListImmediate::getCurrentContext() const {
        // Get the current active context from the device
        // This is set by the device before using the command list
        return m_context;
    }
    
    void FVulkanRHICommandListImmediate::begin() {
        m_context = m_device->getCommandListContext();
        if (m_context) {
            m_context->beginRecording();
        }
    }
    
    void FVulkanRHICommandListImmediate::end() {
        if (m_context) {
            m_context->endRecording();
        }
    }
    
    void FVulkanRHICommandListImmediate::reset() {
        // In UE5, reset doesn't do much for immediate command lists
        // The context handles the per-frame recycling
    }
    
    void FVulkanRHICommandListImmediate::setPipelineState(TSharedPtr<IRHIPipelineState> pipelineState) {
        if (m_context) {
            // Delegate to context's pending state
            // In real implementation, would also bind the pipeline
        }
    }
    
    void FVulkanRHICommandListImmediate::setVertexBuffers(uint32 startSlot, 
                                                         TSpan<TSharedPtr<IRHIBuffer>> vertexBuffers) {
        if (m_context && m_context->getPendingState()) {
            // In a full implementation, would bind all buffers in the span
            // For now, this is a placeholder
        }
    }
    
    void FVulkanRHICommandListImmediate::setIndexBuffer(TSharedPtr<IRHIBuffer> indexBuffer, 
                                                       bool is32Bit) {
        if (m_context && m_context->getPendingState()) {
            // In a full implementation, would extract VkBuffer from IRHIBuffer
            // and call setIndexBuffer with proper VkIndexType
        }
    }
    
    void FVulkanRHICommandListImmediate::setViewport(const Viewport& viewport) {
        if (m_context) {
            m_context->getPendingState()->setViewport(viewport);
        }
    }
    
    void FVulkanRHICommandListImmediate::setScissorRect(const ScissorRect& scissorRect) {
        if (m_context && m_context->getPendingState()) {
            m_context->getPendingState()->setScissor(scissorRect);
        }
    }
    
    void FVulkanRHICommandListImmediate::setRenderTargets(TSpan<TSharedPtr<IRHITexture>> renderTargets, 
                                                         TSharedPtr<IRHITexture> depthStencil) {
        if (m_context) {
            m_context->setRenderTargets(renderTargets, depthStencil);
        }
    }
    
    void FVulkanRHICommandListImmediate::endRenderPass() {
        if (m_context) {
            m_context->endRenderPass();
        }
    }
    
    void FVulkanRHICommandListImmediate::draw(uint32 vertexCount, uint32 startVertexLocation) {
        if (m_context) {
            m_context->draw(vertexCount, startVertexLocation);
        }
    }
    
    void FVulkanRHICommandListImmediate::drawIndexed(uint32 indexCount, uint32 startIndexLocation, 
                                                     int32 baseVertexLocation) {
        if (m_context) {
            m_context->drawIndexed(indexCount, startIndexLocation, baseVertexLocation);
        }
    }
    
    void FVulkanRHICommandListImmediate::drawInstanced(uint32 vertexCountPerInstance, uint32 instanceCount,
                                                       uint32 startVertexLocation, uint32 startInstanceLocation) {
        if (m_context) {
            m_context->drawInstanced(vertexCountPerInstance, instanceCount, 
                                     startVertexLocation, startInstanceLocation);
        }
    }
    
    void FVulkanRHICommandListImmediate::drawIndexedInstanced(uint32 indexCountPerInstance, uint32 instanceCount,
                                                              uint32 startIndexLocation, int32 baseVertexLocation,
                                                              uint32 startInstanceLocation) {
        if (m_context) {
            m_context->drawIndexedInstanced(indexCountPerInstance, instanceCount, 
                                           startIndexLocation, baseVertexLocation, startInstanceLocation);
        }
    }
    
    void FVulkanRHICommandListImmediate::clearRenderTarget(TSharedPtr<IRHITexture> renderTarget, 
                                                          const float32 clearColor[4]) {
        // Typically handled by render pass load operations in Vulkan
        // Could implement with vkCmdClearColorImage if needed
    }
    
    void FVulkanRHICommandListImmediate::clearDepthStencil(TSharedPtr<IRHITexture> depthStencil, 
                                                          bool clearDepth, bool clearStencil,
                                                          float32 depth, uint8 stencil) {
        // Typically handled by render pass load operations in Vulkan
        // Could implement with vkCmdClearDepthStencilImage if needed
    }
    
    void FVulkanRHICommandListImmediate::transitionResource(TSharedPtr<IRHIResource> resource, 
                                                           EResourceUsage stateBefore, EResourceUsage stateAfter) {
        // Vulkan handles this with pipeline barriers, not needed for our simple case
    }
    
    void FVulkanRHICommandListImmediate::resourceBarrier() {
        // In Vulkan, barriers are implicit in synchronization
    }
    
    void FVulkanRHICommandListImmediate::beginEvent(const String& name) {
        // Could use VK_EXT_debug_utils for debug markers
    }
    
    void FVulkanRHICommandListImmediate::endEvent() {
        // Pair with beginEvent
    }
    
    void FVulkanRHICommandListImmediate::setMarker(const String& name) {
        // Debug marker
    }
}

