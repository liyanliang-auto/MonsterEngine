#include "Platform/Vulkan/VulkanCommandListContext.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanCommandBuffer.h"
#include "Platform/Vulkan/VulkanPendingState.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include "Core/Log.h"

namespace MonsterRender::RHI::Vulkan {
    
    FVulkanCommandListContext::FVulkanCommandListContext(
        VulkanDevice* device, 
        FVulkanCommandBufferManager* manager)
        : m_device(device)
        , m_manager(manager)
        , m_cmdBuffer(nullptr) {
    }
    
    FVulkanCommandListContext::~FVulkanCommandListContext() {
        // Cleanup is handled by unique pointers
    }
    
    bool FVulkanCommandListContext::initialize() {
        // Get command buffer from manager
        m_cmdBuffer = m_manager->getActiveCmdBuffer();
        if (!m_cmdBuffer) {
            MR_LOG_ERROR("Failed to get command buffer from manager");
            return false;
        }
        
        // Create pending state
        m_pendingState = MakeUnique<FVulkanPendingState>(m_device, m_cmdBuffer);
        
        // Create descriptor pool
        m_descriptorPool = MakeUnique<FVulkanDescriptorPoolSetContainer>(m_device);
        if (!m_descriptorPool->initialize()) {
            MR_LOG_ERROR("Failed to initialize descriptor pool");
            return false;
        }
        
        MR_LOG_DEBUG("FVulkanCommandListContext initialized");
        return true;
    }
    
    void FVulkanCommandListContext::prepareForNewFrame() {
        // Get next command buffer from ring buffer
        m_manager->prepareForNewActiveCommandBuffer();
        m_cmdBuffer = m_manager->getActiveCmdBuffer();
        
        // Reset pending state
        if (m_pendingState) {
            m_pendingState->reset();
        }
        
        // Reset descriptor pool
        if (m_descriptorPool) {
            m_descriptorPool->reset();
        }
        
        MR_LOG_DEBUG("Prepared for new frame");
    }
    
    void FVulkanCommandListContext::beginRecording() {
        if (m_cmdBuffer) {
            m_cmdBuffer->begin();
        }
    }
    
    void FVulkanCommandListContext::endRecording() {
        if (m_cmdBuffer) {
            m_cmdBuffer->end();
        }
    }
    
    void FVulkanCommandListContext::submitCommands(
        VkSemaphore* waitSemaphores, uint32 waitSemaphoreCount,
        VkSemaphore* signalSemaphores, uint32 signalSemaphoreCount) {
        
        if (m_manager) {
            m_manager->submitActiveCmdBuffer(
                waitSemaphores, waitSemaphoreCount,
                signalSemaphores, signalSemaphoreCount
            );
        }
    }
    
    void FVulkanCommandListContext::endRenderPass() {
        if (m_cmdBuffer && m_cmdBuffer->getHandle() != VK_NULL_HANDLE) {
            const auto& functions = VulkanAPI::getFunctions();
            functions.vkCmdEndRenderPass(m_cmdBuffer->getHandle());
        }
    }
    
    void FVulkanCommandListContext::setRenderTargets(TSpan<TSharedPtr<RHI::IRHITexture>> renderTargets,
                                                     TSharedPtr<RHI::IRHITexture> depthStencil) {
        // This would start a render pass and set up framebuffer
        // For now, simplified implementation
        if (m_device) {
            VkRenderPass renderPass = m_device->getRenderPass();
            VkFramebuffer framebuffer = m_device->getCurrentFramebuffer();
            
            if (renderPass != VK_NULL_HANDLE && framebuffer != VK_NULL_HANDLE && m_cmdBuffer) {
                VkRenderPassBeginInfo renderPassBeginInfo{};
                renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                renderPassBeginInfo.renderPass = renderPass;
                renderPassBeginInfo.framebuffer = framebuffer;
                renderPassBeginInfo.renderArea.offset = {0, 0};
                renderPassBeginInfo.renderArea.extent = m_device->getSwapchainExtent();
                
                VkClearValue clearValues[2]{};
                clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
                clearValues[1].depthStencil = {1.0f, 0};
                renderPassBeginInfo.clearValueCount = 2;
                renderPassBeginInfo.pClearValues = clearValues;
                
                const auto& functions = VulkanAPI::getFunctions();
                functions.vkCmdBeginRenderPass(m_cmdBuffer->getHandle(), &renderPassBeginInfo,
                                             VK_SUBPASS_CONTENTS_INLINE);
            }
        }
    }
    
    void FVulkanCommandListContext::draw(uint32 vertexCount, uint32 startVertexLocation) {
        if (m_cmdBuffer && m_cmdBuffer->getHandle() != VK_NULL_HANDLE) {
            const auto& functions = VulkanAPI::getFunctions();
            functions.vkCmdDraw(m_cmdBuffer->getHandle(), vertexCount, 1,
                              startVertexLocation, 0);
        }
    }
    
    void FVulkanCommandListContext::drawIndexed(uint32 indexCount, uint32 startIndexLocation,
                                               int32 baseVertexLocation) {
        if (m_cmdBuffer && m_cmdBuffer->getHandle() != VK_NULL_HANDLE) {
            const auto& functions = VulkanAPI::getFunctions();
            functions.vkCmdDrawIndexed(m_cmdBuffer->getHandle(), indexCount, 1,
                                     startIndexLocation, baseVertexLocation, 0);
        }
    }
    
    void FVulkanCommandListContext::drawInstanced(uint32 vertexCountPerInstance, uint32 instanceCount,
                                                 uint32 startVertexLocation, uint32 startInstanceLocation) {
        if (m_cmdBuffer && m_cmdBuffer->getHandle() != VK_NULL_HANDLE) {
            const auto& functions = VulkanAPI::getFunctions();
            functions.vkCmdDraw(m_cmdBuffer->getHandle(), vertexCountPerInstance, instanceCount,
                              startVertexLocation, startInstanceLocation);
        }
    }
    
    void FVulkanCommandListContext::drawIndexedInstanced(uint32 indexCountPerInstance, uint32 instanceCount,
                                                        uint32 startIndexLocation, int32 baseVertexLocation,
                                                        uint32 startInstanceLocation) {
        if (m_cmdBuffer && m_cmdBuffer->getHandle() != VK_NULL_HANDLE) {
            const auto& functions = VulkanAPI::getFunctions();
            functions.vkCmdDrawIndexed(m_cmdBuffer->getHandle(), indexCountPerInstance, instanceCount,
                                     startIndexLocation, baseVertexLocation, startInstanceLocation);
        }
    }
    
    void FVulkanCommandListContext::clearRenderTarget(TSharedPtr<RHI::IRHITexture> renderTarget,
                                                     const float32 clearColor[4]) {
        // Implemented via render pass load operations
    }
    
    void FVulkanCommandListContext::clearDepthStencil(TSharedPtr<RHI::IRHITexture> depthStencil,
                                                     bool clearDepth, bool clearStencil,
                                                     float32 depth, uint8 stencil) {
        // Implemented via render pass load operations
    }
}
