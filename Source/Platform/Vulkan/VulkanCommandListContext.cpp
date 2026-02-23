#include "Platform/Vulkan/VulkanCommandListContext.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanCommandBuffer.h"
#include "Platform/Vulkan/VulkanPendingState.h"
#include "Platform/Vulkan/VulkanRenderTargetCache.h"
#include "Platform/Vulkan/VulkanTexture.h"
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

    void FVulkanCommandListContext::refreshCommandBuffer() {
        // Refresh command buffer after synchronous operations (e.g., texture upload)
        // Unlike prepareForNewFrame(), this does NOT acquire swapchain image
        MR_LOG_DEBUG("FVulkanCommandListContext::refreshCommandBuffer()");
        // Get next command buffer from ring buffer
        m_manager->prepareForNewActiveCommandBuffer();
        m_cmdBuffer = m_manager->getActiveCmdBuffer();
        // Update pending state's command buffer reference
        if (m_pendingState && m_cmdBuffer) {
            m_pendingState->updateCommandBuffer(m_cmdBuffer);
        }

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
        // Wait for this frame's in-flight fence before reusing its resources
        // This ensures the previous frame using this slot has completed GPU work
        VkFence inFlightFence = m_device->getInFlightFence(currentFrame);
        if (inFlightFence != VK_NULL_HANDLE) {
            VkResult waitResult = functions.vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);
            if (waitResult != VK_SUCCESS && waitResult != VK_TIMEOUT) {
                MR_LOG_ERROR("acquireNextSwapchainImage: Failed to wait for in-flight fence: " + std::to_string(waitResult));
                return false;
            }

        }

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

        // Check if a previous frame is still using this image
        // If so, wait for that frame's fence to complete
        VkFence imageInFlightFence = m_device->getImageInFlightFence(imageIndex);
        if (imageInFlightFence != VK_NULL_HANDLE) {
            VkResult waitResult = functions.vkWaitForFences(device, 1, &imageInFlightFence, VK_TRUE, UINT64_MAX);
            if (waitResult != VK_SUCCESS && waitResult != VK_TIMEOUT) {
                MR_LOG_WARNING("acquireNextSwapchainImage: Failed to wait for image-in-flight fence");
            }

        }

        // Mark this image as being used by the current frame's fence
        m_device->setImageInFlightFence(imageIndex, inFlightFence);
        // Reset the in-flight fence for this frame's use (after all waits are done)
        if (inFlightFence != VK_NULL_HANDLE) {
            functions.vkResetFences(device, 1, &inFlightFence);
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
            // Execute texture layout transitions for this command buffer
            // This is needed because Vulkan validation layer tracks layout per-command-buffer
            if (m_device && m_cmdBuffer->getHandle() != VK_NULL_HANDLE) {
                m_device->executeTextureLayoutTransitions(m_cmdBuffer->getHandle());
            }

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
        if (m_manager && m_device) {
            // Use the in-flight fence for this frame to enable proper frame synchronization
            uint32 currentFrame = m_device->getCurrentFrame();
            VkFence inFlightFence = m_device->getInFlightFence(currentFrame);
            m_manager->submitActiveCmdBufferWithFence(
                waitSemaphores, waitSemaphoreCount,
                signalSemaphores, signalSemaphoreCount,
                inFlightFence
            );
        }

    }

    void FVulkanCommandListContext::endRenderPass() {
        // Check if we are actually inside a render pass before ending it
        // This prevents Vulkan validation errors when endRenderPass is called without a matching beginRenderPass
        if (!m_pendingState || !m_pendingState->isInsideRenderPass()) {
            MR_LOG_DEBUG("FVulkanCommandListContext::endRenderPass: Not inside render pass, skipping");
            return;
        }

        if (m_cmdBuffer && m_cmdBuffer->getHandle() != VK_NULL_HANDLE) {
            const auto& functions = VulkanAPI::getFunctions();
            functions.vkCmdEndRenderPass(m_cmdBuffer->getHandle());
            // Mark that we are no longer inside a render pass
            m_pendingState->setInsideRenderPass(false);
            MR_LOG_DEBUG("FVulkanCommandListContext::endRenderPass: Render pass ended");
        }

    }

    void FVulkanCommandListContext::setRenderTargets(TSpan<TSharedPtr<RHI::IRHITexture>> renderTargets,
                                                     TSharedPtr<RHI::IRHITexture> depthStencil) {

        // ============================================================================

        // setRenderTargets - Begin render pass with specified render targets
        // Reference: UE5 FVulkanCommandListContext::RHISetRenderTargets
        //
        // Supports two modes:
        // 1. Swapchain rendering (empty renderTargets array) - uses device's swapchain framebuffer
        // 2. Render-to-texture (RTT) - uses provided textures with cached render pass/framebuffer

        // ============================================================================

        if (!m_device || !m_cmdBuffer) {
            MR_LOG_WARNING("setRenderTargets: Invalid device or command buffer");
            return;
        }

        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkExtent2D renderExtent = m_device->getSwapchainExtent();
        uint32 numClearValues = 2; // color + depth
        // Determine if this is swapchain rendering or RTT
        // Note: If custom depthStencil is provided, we still need to use RTT mode
        // to properly use the custom depth buffer instead of device's default
        bool bIsSwapchain = renderTargets.empty() && !depthStencil;
        if (bIsSwapchain) {

            // ================================================================

            // Swapchain rendering mode - use device's default render pass/framebuffer
            // Only used when NO custom depth buffer is provided

            // ================================================================

            renderPass = m_device->getRenderPass();
            framebuffer = m_device->getCurrentFramebuffer();
            MR_LOG_DEBUG("setRenderTargets: Using swapchain rendering (default depth)");
        } else {

            // ================================================================

            // RTT mode - use cached render pass and framebuffer
            // Reference: UE5 FVulkanDynamicRHI::RHISetRenderTargets for custom targets

            // ================================================================

            FVulkanRenderTargetInfo rtInfo;
            rtInfo.bIsSwapchain = false;
            rtInfo.bClearDepth = true;
            rtInfo.bClearStencil = false;
            // Check if we need to use swapchain as color target (empty renderTargets but custom depth)
            // IMPORTANT: Only use swapchain color if depth target matches swapchain size
            // If depth target has different size (e.g., shadow map), use depth-only render pass
            bool bUseSwapchainColor = false;
            if (renderTargets.empty() && depthStencil)
            {
                // Check if depth target size matches swapchain size
                uint32 depthWidth = depthStencil->getWidth();
                uint32 depthHeight = depthStencil->getHeight();
                VkExtent2D swapchainExtent = m_device->getSwapchainExtent();
                if (depthWidth == swapchainExtent.width && depthHeight == swapchainExtent.height)
                {
                    // Depth matches swapchain size - use swapchain as color target
                    bUseSwapchainColor = true;
                    MR_LOG_DEBUG("setRenderTargets: Depth matches swapchain size, using swapchain color");
                }

                else
                {
                    // Depth has different size (e.g., shadow map) - depth-only render pass
                    bUseSwapchainColor = false;
                    MR_LOG_DEBUG("setRenderTargets: Depth-only mode (shadow map or RTT), size=%ux%u",
                                depthWidth, depthHeight);
                }

            }

            if (bUseSwapchainColor) {
                // Use swapchain image view as color target with custom depth
                rtInfo.bIsSwapchain = true;  // Mark as swapchain for proper handling
                rtInfo.SwapchainImageView = m_device->getCurrentSwapchainImageView();
                rtInfo.NumColorTargets = 1;
                rtInfo.bClearColor[0] = true;  // Clear to ensure render pass compatibility
                // Set render area from swapchain extent
                rtInfo.RenderAreaWidth = renderExtent.width;
                rtInfo.RenderAreaHeight = renderExtent.height;
                MR_LOG_DEBUG("setRenderTargets: Using swapchain color + custom depth, extent=" +
                            std::to_string(renderExtent.width) + "x" + std::to_string(renderExtent.height));
            } else {
                // Build color targets from provided textures
                for (uint32 i = 0; i < renderTargets.size() && i < FVulkanRenderTargetInfo::MaxColorTargets; ++i) {
                    if (renderTargets[i]) {
                        rtInfo.ColorTargets[i] = StaticCastSharedPtr<VulkanTexture>(renderTargets[i]);
                        rtInfo.bClearColor[i] = true;
                        rtInfo.ClearColors[i] = {{0.0f, 0.0f, 0.0f, 1.0f}};
                        rtInfo.NumColorTargets++;
                    }

                }

            }

            // Add depth target if provided, or use device's depth buffer
            if (depthStencil) {
                MR_LOG(LogTemp, Log, "setRenderTargets: depthStencil provided, ptr=0x%llx",
                       reinterpret_cast<uint64>(depthStencil.Get()));
                rtInfo.DepthStencilTarget = StaticCastSharedPtr<VulkanTexture>(depthStencil);
                if (rtInfo.DepthStencilTarget) {
                    MR_LOG(LogTemp, Log, "setRenderTargets: StaticCast SUCCESS, DepthStencilTarget=0x%llx, imageView=0x%llx",
                           reinterpret_cast<uint64>(rtInfo.DepthStencilTarget.Get()),
                           reinterpret_cast<uint64>(rtInfo.DepthStencilTarget->getImageView()));
                } else {
                    MR_LOG(LogTemp, Error, "setRenderTargets: StaticCast FAILED! DepthStencilTarget is null after cast");
                }

            }

            // Set render area from first color target, or from depth target if no color targets
            if (rtInfo.NumColorTargets > 0 && rtInfo.ColorTargets[0]) {
                auto& desc = rtInfo.ColorTargets[0]->getDesc();
                renderExtent.width = desc.width;
                renderExtent.height = desc.height;
                rtInfo.RenderAreaWidth = desc.width;
                rtInfo.RenderAreaHeight = desc.height;
            } else if (rtInfo.DepthStencilTarget) {
                // No color targets but have depth - use depth target dimensions
                auto& desc = rtInfo.DepthStencilTarget->getDesc();
                renderExtent.width = desc.width;
                renderExtent.height = desc.height;
                rtInfo.RenderAreaWidth = desc.width;
                rtInfo.RenderAreaHeight = desc.height;
            }

            // Get or create render pass from cache
            FVulkanRenderTargetLayout layout = rtInfo.BuildLayout(m_device);
            // Add depth format if using device depth buffer without custom depth target
            if (!rtInfo.DepthStencilTarget && m_device->hasDepthBuffer()) {
                layout.bHasDepthStencil = true;
                layout.DepthStencilFormat = m_device->getVulkanDepthFormat();
            }

            // UE5 Pattern: Use render pass cache to dynamically create render pass
            // This supports depth-only, color-only, and color+depth render passes
            auto* rpCache = m_device->GetRenderPassCache();
            if (rpCache) {
                renderPass = rpCache->GetOrCreateRenderPass(layout);
                MR_LOG_INFO("RTT: Using cached render pass=0x" +

                           std::to_string(reinterpret_cast<uint64>(renderPass)) +

                           ", NumColorTargets=" + std::to_string(rtInfo.NumColorTargets) +

                           ", HasDepth=" + std::string(layout.bHasDepthStencil ? "true" : "false") +
                           ", extent=" + std::to_string(renderExtent.width) + "x" + std::to_string(renderExtent.height));
            } else {
                MR_LOG_ERROR("RTT: Render pass cache is null!");
            }

            // Get or create framebuffer from cache
            if (renderPass != VK_NULL_HANDLE) {
                VkImageView depthView = VK_NULL_HANDLE;
                if (rtInfo.DepthStencilTarget) {
                    depthView = rtInfo.DepthStencilTarget->getImageView();
                    MR_LOG_INFO("RTT: Using custom depth target, view=" +
                               std::to_string(reinterpret_cast<uint64>(depthView)));
                } else if (m_device->hasDepthBuffer()) {
                    // WARNING: Device depth buffer may have different size than RTT!
                    depthView = m_device->getDepthImageView();
                    MR_LOG_WARNING("RTT: Using device depth buffer (may cause size mismatch!)");
                }

                FVulkanFramebufferKey fbKey = rtInfo.BuildFramebufferKey(renderPass, depthView);
                fbKey.Width = renderExtent.width;
                fbKey.Height = renderExtent.height;
                auto* fbCache = m_device->GetFramebufferCache();
                if (fbCache) {
                    framebuffer = fbCache->GetOrCreateFramebuffer(fbKey);
                }

            }

            numClearValues = rtInfo.NumColorTargets + (layout.bHasDepthStencil ? 1 : 0);

            MR_LOG_DEBUG("setRenderTargets: RTT mode with " + std::to_string(rtInfo.NumColorTargets) +

                        " color target(s), extent=" + std::to_string(renderExtent.width) + "x" +
                        std::to_string(renderExtent.height));
        }

        // ================================================================

        // Pre-render pass texture layout transitions
        // Must be done BEFORE vkCmdBeginRenderPass

        // ================================================================

        if (m_pendingState) {
            m_pendingState->transitionTexturesForShaderRead();
        }

        // ================================================================

        // Begin render pass

        // ================================================================

        if (renderPass != VK_NULL_HANDLE && framebuffer != VK_NULL_HANDLE) {

            MR_LOG_INFO("beginRenderPass: renderPass=" + std::to_string(reinterpret_cast<uint64>(renderPass)) +

                       ", framebuffer=" + std::to_string(reinterpret_cast<uint64>(framebuffer)) +
                       ", extent=" + std::to_string(renderExtent.width) + "x" + std::to_string(renderExtent.height));
            VkRenderPassBeginInfo renderPassBeginInfo{};
            renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassBeginInfo.renderPass = renderPass;
            renderPassBeginInfo.framebuffer = framebuffer;
            renderPassBeginInfo.renderArea.offset = {0, 0};
            renderPassBeginInfo.renderArea.extent = renderExtent;
            // Clear values: must match attachment order in render pass
            // For depth-only pass: clearValues[0] = depth
            // For color+depth pass: clearValues[0] = color, clearValues[1] = depth
            TArray<VkClearValue> clearValues(numClearValues);
            // Depth-only pass: numClearValues == 1 and not swapchain mode
            bool bIsDepthOnlyPass = (numClearValues == 1) && !bIsSwapchain;
            if (bIsDepthOnlyPass) {
                // Depth-only pass: first (and only) attachment is depth
                clearValues[0].depthStencil = {1.0f, 0}; // Clear depth to 1.0 (far)
                MR_LOG(LogTemp, Log, "beginRenderPass: Depth-only clear values configured");
            } else {
                // Color pass or color+depth pass
                clearValues[0].color = {{0.1f, 0.1f, 0.15f, 1.0f}}; // Dark gray background
                if (numClearValues > 1) {
                    clearValues[1].depthStencil = {1.0f, 0}; // Clear depth to 1.0 (far)
                }

            }

            renderPassBeginInfo.clearValueCount = static_cast<uint32>(clearValues.size());
            renderPassBeginInfo.pClearValues = clearValues.data();
            const auto& functions = VulkanAPI::getFunctions();
            functions.vkCmdBeginRenderPass(m_cmdBuffer->getHandle(), &renderPassBeginInfo,
                                         VK_SUBPASS_CONTENTS_INLINE);
            // CRITICAL: Mark that we are now inside a render pass
            if (m_pendingState) {
                m_pendingState->setInsideRenderPass(true);
            }

            MR_LOG_DEBUG("setRenderTargets: Render pass started successfully");
        } else {
            MR_LOG_WARNING("setRenderTargets: Invalid render pass or framebuffer - renderPass=" +

                          std::to_string(reinterpret_cast<uint64>(renderPass)) +
                          ", framebuffer=" + std::to_string(reinterpret_cast<uint64>(framebuffer)));
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

        MR_LOG_INFO("  vkCmdDraw(vertexCount=" + std::to_string(vertexCount) +
                   ", startVertex=" + std::to_string(startVertexLocation) + ")");
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

            // Check if we are inside a render pass
            if (!m_pendingState->isInsideRenderPass()) {
                MR_LOG_ERROR("drawIndexed: Not inside render pass! Draw call will be ignored by GPU.");
            }

            if (!m_pendingState->prepareForDraw()) {
                MR_LOG_ERROR("drawIndexed: Failed to prepare for draw - aborting draw call");
                return;
            }

            const auto& functions = VulkanAPI::getFunctions();
            functions.vkCmdDrawIndexed(m_cmdBuffer->getHandle(), indexCount, 1,
                                     startIndexLocation, baseVertexLocation, 0);
            MR_LOG_INFO("drawIndexed: vkCmdDrawIndexed called with " + std::to_string(indexCount) + " indices");
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
