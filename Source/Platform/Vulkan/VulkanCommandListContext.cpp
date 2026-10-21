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
        // Destroy secondary command pool
        if (m_secondaryCommandPool != VK_NULL_HANDLE && m_device) {
            vkDestroyCommandPool(m_device->getDevice(), m_secondaryCommandPool, nullptr);
            m_secondaryCommandPool = VK_NULL_HANDLE;
        }
        
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

        // Create secondary command pool for parallel recording
        // Use RESET_COMMAND_BUFFER_BIT to allow individual buffer resets
        VkCommandPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = m_device->getGraphicsQueueFamilyIndex();
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        
        VkResult result = vkCreateCommandPool(m_device->getDevice(), &poolInfo, nullptr, &m_secondaryCommandPool);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("Failed to create secondary command pool");
            return false;
        }

        MR_LOG_DEBUG("FVulkanCommandListContext initialized with secondary command pool");
        return true;
    }

    bool FVulkanCommandListContext::prepareForNewFrame() {
        if (!m_device || !acquireNextSwapchainImage()) { m_cmdBuffer = nullptr; return false; }
        if (!m_manager->prepareForNewActiveCommandBuffer()) { m_cmdBuffer = nullptr; return false; }
        m_cmdBuffer = m_manager->getActiveCmdBuffer();
        if (!m_cmdBuffer) return false;
        if (m_descriptorPool) m_descriptorPool->reset();
        if (m_pendingState) {
            m_pendingState->updateCommandBuffer(m_cmdBuffer);
            m_pendingState->reset();
        }
        return true;
    }

    void FVulkanCommandListContext::refreshCommandBuffer() {
        // Refresh command buffer after synchronous operations (e.g., texture upload)
        // Unlike prepareForNewFrame(), this does NOT acquire swapchain image
        MR_LOG_DEBUG("FVulkanCommandListContext::refreshCommandBuffer()");
        // Get next command buffer from ring buffer
        if (!m_manager->prepareForNewActiveCommandBuffer()) { m_cmdBuffer = nullptr; return; }
        m_cmdBuffer = m_manager->getActiveCmdBuffer();
        // Update pending state's command buffer reference
        if (m_pendingState && m_cmdBuffer) {
            m_pendingState->updateCommandBuffer(m_cmdBuffer);
        }

    }

    bool FVulkanCommandListContext::acquireNextSwapchainImage() {
        if (!m_device) return false;
        if (!m_device->getSwapchain()) return true; // CPU/headless resource operations.
        const auto result = m_device->acquireFrame();
        return result.status == EFrameResult::Success || result.status == EFrameResult::Suboptimal;
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
            if (m_device && m_cmdBuffer->hasBegun() && m_cmdBuffer->getHandle() != VK_NULL_HANDLE) {
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

    bool FVulkanCommandListContext::submitCommands(
        VkSemaphore* waitSemaphores, uint32 waitSemaphoreCount,
        VkSemaphore* signalSemaphores, uint32 signalSemaphoreCount) {
        MonsterEngine::Profiling::Scope scope("vulkan.submit_dispatch", m_device ? m_device->GetProfilingContext() : MonsterEngine::Profiling::Context{});
        scope.SetValue(0);
        if (m_manager && m_device) {
            // Use the in-flight fence for this frame to enable proper frame synchronization
            uint32 currentFrame = m_device->getCurrentFrame();
            VkFence inFlightFence = m_device->getInFlightFence(currentFrame);
            const bool submitted = m_manager->submitActiveCmdBufferWithFence(
                waitSemaphores, waitSemaphoreCount,
                signalSemaphores, signalSemaphoreCount,
                inFlightFence
            );
            scope.SetValue(submitted ? 1 : 0);
            return submitted;
        }
        return false;
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
            // Explicit depth and no explicit colors is always a depth-only pass.
            if (renderTargets.empty() && depthStencil)
            {
                uint32 depthWidth = depthStencil->getWidth();
                uint32 depthHeight = depthStencil->getHeight();
                MR_LOG(LogTemp, Verbose, "setRenderTargets: Depth-only mode, size=%ux%u", depthWidth, depthHeight);
            }

            {
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
            if (depthStencil) {
                auto depthTexture = StaticCastSharedPtr<VulkanTexture>(depthStencil);
                depthTexture->setCurrentLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            }
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
        MR_LOG(LogTemp, Log, "[DRAW_TRACE] draw() called: vertexCount=%u, startVertex=%u",
               vertexCount, startVertexLocation);
        if (!m_cmdBuffer) {
            MR_LOG(LogTemp, Log, "[DRAW_TRACE] draw: m_cmdBuffer is nullptr!");
            return;
        }

        VkCommandBuffer cmdBufferHandle = m_cmdBuffer->getHandle();
        if (cmdBufferHandle == VK_NULL_HANDLE) {
            MR_LOG(LogTemp, Log, "[DRAW_TRACE] draw: Command buffer handle is VK_NULL_HANDLE!");
            return;
        }

        if (!m_pendingState) {
            MR_LOG(LogTemp, Log, "[DRAW_TRACE] draw: m_pendingState is nullptr!");
            return;
        }

        MR_LOG(LogTemp, Log, "[DRAW_TRACE] draw: About to call prepareForDraw(), insideRenderPass=%d",
               (int)m_pendingState->isInsideRenderPass());
        if (!m_pendingState->prepareForDraw()) {
            MR_LOG(LogTemp, Log, "[DRAW_TRACE] draw: prepareForDraw() returned false - ABORTING");
            return;
        }

        MR_LOG(LogTemp, Log, "[DRAW_TRACE] draw: prepareForDraw() succeeded, calling vkCmdDraw(%u, 1, %u, 0)",
               vertexCount, startVertexLocation);
        const auto& functions = VulkanAPI::getFunctions();
        functions.vkCmdDraw(cmdBufferHandle, vertexCount, 1, startVertexLocation, 0);
        MR_LOG(LogTemp, Log, "[DRAW_TRACE] draw: vkCmdDraw completed");
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

    // ========================================================================
    // Secondary Command Buffer Management
    // Reference: UE5 parallel command buffer recording pattern
    // ========================================================================

    FVulkanCmdBuffer* FVulkanCommandListContext::AllocateSecondaryCommandBuffer(
        VkRenderPass renderPass,
        uint32 subpass,
        VkFramebuffer framebuffer) {
        
        MR_LOG_DEBUG("FVulkanCommandListContext::AllocateSecondaryCommandBuffer - "
                   "Allocating secondary command buffer");
        
        // Try to find an available buffer in the pool
        for (auto& entry : m_secondaryCommandBuffers) {
            if (!entry.isInUse) {
                // Reuse existing buffer
                entry.isInUse = true;
                entry.inheritedRenderPass = renderPass;
                entry.inheritedSubpass = subpass;
                entry.inheritedFramebuffer = framebuffer;
                
                // Reset the command buffer for reuse
                vkResetCommandBuffer(entry.cmdBuffer->getHandle(), 0);
                
                MR_LOG_DEBUG("FVulkanCommandListContext::AllocateSecondaryCommandBuffer - "
                           "Reused existing buffer from pool");
                return entry.cmdBuffer.get();
            }
        }
        
        // No available buffer, allocate a new one
        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_secondaryCommandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
        allocInfo.commandBufferCount = 1;
        
        VkCommandBuffer vkCmdBuffer = VK_NULL_HANDLE;
        VkResult result = vkAllocateCommandBuffers(m_device->getDevice(), &allocInfo, &vkCmdBuffer);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("FVulkanCommandListContext::AllocateSecondaryCommandBuffer - "
                       "Failed to allocate secondary command buffer");
            return nullptr;
        }
        
        // Create FVulkanCmdBuffer wrapper
        // Note: We need to create a temporary manager for the secondary buffer
        // In a full implementation, this would use a dedicated secondary buffer manager
        auto cmdBuffer = MakeUnique<FVulkanCmdBuffer>(m_device, m_manager);
        
        // Store the Vulkan handle in the wrapper
        // This is a simplified approach - in UE5, secondary buffers have their own wrapper
        // For now, we'll use the existing FVulkanCmdBuffer structure
        
        // Add to pool
        FSecondaryCommandBufferEntry entry;
        entry.cmdBuffer = std::move(cmdBuffer);
        entry.isInUse = true;
        entry.inheritedRenderPass = renderPass;
        entry.inheritedSubpass = subpass;
        entry.inheritedFramebuffer = framebuffer;
        
        m_secondaryCommandBuffers.push_back(std::move(entry));
        
        MR_LOG_DEBUG("FVulkanCommandListContext::AllocateSecondaryCommandBuffer - "
                   "Allocated new secondary buffer. Pool size: " + 
                   std::to_string(m_secondaryCommandBuffers.size()));
        
        return m_secondaryCommandBuffers[m_secondaryCommandBuffers.size() - 1].cmdBuffer.get();
    }

    void FVulkanCommandListContext::FreeSecondaryCommandBuffer(FVulkanCmdBuffer* cmdBuffer) {
        if (!cmdBuffer) {
            return;
        }
        
        // Find the buffer in the pool and mark it as available
        for (auto& entry : m_secondaryCommandBuffers) {
            if (entry.cmdBuffer.get() == cmdBuffer) {
                entry.isInUse = false;
                entry.inheritedRenderPass = VK_NULL_HANDLE;
                entry.inheritedSubpass = 0;
                entry.inheritedFramebuffer = VK_NULL_HANDLE;
                
                MR_LOG_DEBUG("FVulkanCommandListContext::FreeSecondaryCommandBuffer - "
                           "Freed secondary command buffer");
                return;
            }
        }
        
        MR_LOG_WARNING("FVulkanCommandListContext::FreeSecondaryCommandBuffer - "
                     "Buffer not found in pool");
    }

    void FVulkanCommandListContext::ResetSecondaryCommandBuffers() {
        // Reset all secondary command buffers at the end of frame
        // This allows them to be reused in the next frame
        
        uint32 resetCount = 0;
        for (auto& entry : m_secondaryCommandBuffers) {
            if (entry.isInUse) {
                entry.isInUse = false;
                entry.inheritedRenderPass = VK_NULL_HANDLE;
                entry.inheritedSubpass = 0;
                entry.inheritedFramebuffer = VK_NULL_HANDLE;
                resetCount++;
            }
        }
        
        // Optionally, reset the entire command pool for efficiency
        // This resets all command buffers in the pool at once
        if (m_secondaryCommandPool != VK_NULL_HANDLE) {
            vkResetCommandPool(m_device->getDevice(), m_secondaryCommandPool, 0);
        }
        
        MR_LOG_DEBUG("FVulkanCommandListContext::ResetSecondaryCommandBuffers - "
                   "Reset " + std::to_string(resetCount) + " secondary buffers. "
                   "Pool size: " + std::to_string(m_secondaryCommandBuffers.size()));
    }

}
