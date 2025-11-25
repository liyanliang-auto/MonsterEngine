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
        MR_LOG_INFO("===== FVulkanCommandListContext::prepareForNewFrame() START =====");
        
        // Get next command buffer from ring buffer
        MR_LOG_INFO("  Calling prepareForNewActiveCommandBuffer()...");
        m_manager->prepareForNewActiveCommandBuffer();
        
        MR_LOG_INFO("  Getting active cmd buffer...");
        m_cmdBuffer = m_manager->getActiveCmdBuffer();
        
        MR_LOG_INFO("  m_cmdBuffer: " + std::string(m_cmdBuffer ? "VALID" : "NULL"));
        if (m_cmdBuffer) {
            MR_LOG_INFO("    Handle: " + std::to_string(reinterpret_cast<uint64>(m_cmdBuffer->getHandle())));
            MR_LOG_INFO("    hasBegun: " + std::string(m_cmdBuffer->hasBegun() ? "YES" : "NO"));
            MR_LOG_INFO("    hasEnded: " + std::string(m_cmdBuffer->hasEnded() ? "YES" : "NO"));
            MR_LOG_INFO("    isSubmitted: " + std::string(m_cmdBuffer->isSubmitted() ? "YES" : "NO"));
        }
        
        // Update pending state's command buffer reference
        if (m_pendingState && m_cmdBuffer) {
            m_pendingState->updateCommandBuffer(m_cmdBuffer);
        }
        
        // Reset pending state
        if (m_pendingState) {
            m_pendingState->reset();
        }
        
        // Reset descriptor pool
        if (m_descriptorPool) {
            m_descriptorPool->reset();
        }
        
        // Acquire next swapchain image BEFORE any rendering begins
        // This ensures getCurrentFramebuffer() returns the correct framebuffer
        if (m_device) {
            acquireNextSwapchainImage();
        }
        
        MR_LOG_INFO("===== FVulkanCommandListContext::prepareForNewFrame() END =====");
    }
    
    bool FVulkanCommandListContext::acquireNextSwapchainImage() {
        // Reference: UE5 FVulkanDynamicRHI::RHIBeginDrawingViewport
        // Acquire swapchain image at the beginning of frame, not during present
        
        const auto& functions = VulkanAPI::getFunctions();
        VkDevice device = m_device->getLogicalDevice();
        VkSwapchainKHR swapchain = m_device->getSwapchain();
        
        if (swapchain == VK_NULL_HANDLE) {
            MR_LOG_WARNING("acquireNextSwapchainImage: No swapchain");
            return false;
        }
        
        // Use frame-specific semaphore for synchronization
        uint32 currentFrame = m_device->getCurrentFrame();
        VkSemaphore imageAvailableSemaphore = m_device->getImageAvailableSemaphore(currentFrame);
        
        uint32 imageIndex = 0;
        VkResult result = functions.vkAcquireNextImageKHR(
            device,
            swapchain,
            UINT64_MAX,
            imageAvailableSemaphore,
            VK_NULL_HANDLE,
            &imageIndex
        );
        
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            MR_LOG_WARNING("acquireNextSwapchainImage: Swapchain out of date");
            return false;
        } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            MR_LOG_ERROR("acquireNextSwapchainImage: Failed to acquire image: " + std::to_string(result));
            return false;
        }
        
        m_device->setCurrentImageIndex(imageIndex);
        MR_LOG_DEBUG("acquireNextSwapchainImage: Acquired image index " + std::to_string(imageIndex));
        return true;
    }
    
    void FVulkanCommandListContext::beginRecording() {
        MR_LOG_INFO("  FVulkanCommandListContext::beginRecording() called");
        MR_LOG_INFO("    m_cmdBuffer: " + std::string(m_cmdBuffer ? "VALID" : "NULL"));
        
        if (m_cmdBuffer) {
            MR_LOG_INFO("    Calling m_cmdBuffer->begin()...");
            m_cmdBuffer->begin();
            MR_LOG_INFO("    m_cmdBuffer->begin() returned");
        } else {
            MR_LOG_ERROR("    m_cmdBuffer is NULL!");
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
            
            // Mark that we are no longer inside a render pass
            if (m_pendingState) {
                m_pendingState->setInsideRenderPass(false);
            }
            
            MR_LOG_DEBUG("FVulkanCommandListContext::endRenderPass: Render pass ended");
        }
    }
    
    void FVulkanCommandListContext::setRenderTargets(TSpan<TSharedPtr<RHI::IRHITexture>> renderTargets,
                                                     TSharedPtr<RHI::IRHITexture> depthStencil) {
        // This would start a render pass and set up framebuffer
        // Reference: UE5 FVulkanCommandListContext::RHISetRenderTargets
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
                clearValues[0].color = {{0.2f, 0.3f, 0.3f, 1.0f}}; // LearnOpenGL-style background color
                clearValues[1].depthStencil = {1.0f, 0};
                renderPassBeginInfo.clearValueCount = 2;
                renderPassBeginInfo.pClearValues = clearValues;
                
                const auto& functions = VulkanAPI::getFunctions();
                functions.vkCmdBeginRenderPass(m_cmdBuffer->getHandle(), &renderPassBeginInfo,
                                             VK_SUBPASS_CONTENTS_INLINE);
                
                // CRITICAL: Mark that we are now inside a render pass
                if (m_pendingState) {
                    m_pendingState->setInsideRenderPass(true);
                }
                
                MR_LOG_DEBUG("FVulkanCommandListContext::setRenderTargets: Render pass started");
            } else {
                MR_LOG_WARNING("setRenderTargets: Invalid render pass or framebuffer - renderPass=" +
                              std::to_string(reinterpret_cast<uint64>(renderPass)) + 
                              ", framebuffer=" + std::to_string(reinterpret_cast<uint64>(framebuffer)));
            }
        }
    }
    
    void FVulkanCommandListContext::draw(uint32 vertexCount, uint32 startVertexLocation) {
        MR_LOG_INFO("===== FVulkanCommandListContext::draw() START =====");
        MR_LOG_INFO("  vertexCount: " + std::to_string(vertexCount));
        MR_LOG_INFO("  startVertexLocation: " + std::to_string(startVertexLocation));
        
        if (!m_cmdBuffer) {
            MR_LOG_ERROR("draw: m_cmdBuffer is nullptr!");
            return;
        }
        
        VkCommandBuffer cmdBufferHandle = m_cmdBuffer->getHandle();
        if (cmdBufferHandle == VK_NULL_HANDLE) {
            MR_LOG_ERROR("draw: Command buffer handle is VK_NULL_HANDLE!");
            return;
        }
        
        MR_LOG_INFO("  CommandBuffer handle: " + std::to_string(reinterpret_cast<uint64>(cmdBufferHandle)));
        
        if (!m_pendingState) {
            MR_LOG_ERROR("draw: m_pendingState is nullptr!");
            return;
        }
        
        MR_LOG_INFO("  Calling prepareForDraw()...");
        if (!m_pendingState->prepareForDraw()) {
            MR_LOG_ERROR("draw: prepareForDraw() returned false - ABORTING");
            return;
        }
        
        MR_LOG_INFO("  prepareForDraw() succeeded");
        MR_LOG_INFO("  About to call vkCmdDraw...");
        
        const auto& functions = VulkanAPI::getFunctions();
        
        // Log function pointer
        MR_LOG_INFO("  vkCmdDraw function pointer: " + 
                   std::to_string(reinterpret_cast<uint64>(functions.vkCmdDraw)));
        
        // Call vkCmdDraw
        functions.vkCmdDraw(cmdBufferHandle, vertexCount, 1, startVertexLocation, 0);
        
        MR_LOG_INFO("  vkCmdDraw completed");
        MR_LOG_INFO("===== FVulkanCommandListContext::draw() END =====");
    }
    
    void FVulkanCommandListContext::drawIndexed(uint32 indexCount, uint32 startIndexLocation,
                                               int32 baseVertexLocation) {
        if (m_cmdBuffer && m_cmdBuffer->getHandle() != VK_NULL_HANDLE) {
            // UE5 Pattern: Prepare pending state before draw
            if (!m_pendingState) {
                MR_LOG_ERROR("drawIndexed: No pending state");
                return;
            }
            
            if (!m_pendingState->prepareForDraw()) {
                MR_LOG_ERROR("drawIndexed: Failed to prepare for draw - aborting draw call");
                return;
            }
            
            const auto& functions = VulkanAPI::getFunctions();
            functions.vkCmdDrawIndexed(m_cmdBuffer->getHandle(), indexCount, 1,
                                     startIndexLocation, baseVertexLocation, 0);
        }
    }
    
    void FVulkanCommandListContext::drawInstanced(uint32 vertexCountPerInstance, uint32 instanceCount,
                                                 uint32 startVertexLocation, uint32 startInstanceLocation) {
        if (m_cmdBuffer && m_cmdBuffer->getHandle() != VK_NULL_HANDLE) {
            // UE5 Pattern: Prepare pending state before draw
            if (!m_pendingState) {
                MR_LOG_ERROR("drawInstanced: No pending state");
                return;
            }
            
            if (!m_pendingState->prepareForDraw()) {
                MR_LOG_ERROR("drawInstanced: Failed to prepare for draw - aborting draw call");
                return;
            }
            
            const auto& functions = VulkanAPI::getFunctions();
            functions.vkCmdDraw(m_cmdBuffer->getHandle(), vertexCountPerInstance, instanceCount,
                              startVertexLocation, startInstanceLocation);
        }
    }
    
    void FVulkanCommandListContext::drawIndexedInstanced(uint32 indexCountPerInstance, uint32 instanceCount,
                                                        uint32 startIndexLocation, int32 baseVertexLocation,
                                                        uint32 startInstanceLocation) {
        if (m_cmdBuffer && m_cmdBuffer->getHandle() != VK_NULL_HANDLE) {
            // UE5 Pattern: Prepare pending state before draw
            if (!m_pendingState) {
                MR_LOG_ERROR("drawIndexedInstanced: No pending state");
                return;
            }
            
            if (!m_pendingState->prepareForDraw()) {
                MR_LOG_ERROR("drawIndexedInstanced: Failed to prepare for draw - aborting draw call");
                return;
            }
            
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
