// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Vulkan RHI Command List Implementation
//
// Reference: UE5 Engine/Source/Runtime/VulkanRHI/Private/VulkanCommandList.cpp
//            UE5 Engine/Source/Runtime/VulkanRHI/Private/VulkanCommands.cpp

#include "Platform/Vulkan/VulkanRHICommandList.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanCommandListContext.h"
#include "Platform/Vulkan/VulkanCommandBuffer.h"
#include "Platform/Vulkan/VulkanPendingState.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanPipelineState.h"
#include "Platform/Vulkan/VulkanTexture.h"
#include "Platform/Vulkan/VulkanUtils.h"
#include "RHI/RHIBarriers.h"
#include "RHI/RHIDefinitions.h"
#include "RHI/IRHISwapChain.h"
#include "Core/Log.h"

DEFINE_LOG_CATEGORY_STATIC(LogVulkanRHI, Log, All);

namespace MonsterRender::RHI::Vulkan {
    
    using namespace MonsterRender::RHI;
    
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
        MR_LOG_INFO("===== FVulkanRHICommandListImmediate::begin() START =====");
        
        // Get or reuse the command list context from device
        // UE5 Pattern: Device manages context lifecycle
        if (!m_context) {
            m_context = m_device->getCommandListContext();
            if (!m_context) {
                MR_LOG_ERROR("FVulkanRHICommandListImmediate::begin: Failed to get command list context");
                return;
            }
            MR_LOG_INFO("  Got new context: " + std::to_string(reinterpret_cast<uint64>(m_context)));
        } else {
            MR_LOG_INFO("  Using existing context: " + std::to_string(reinterpret_cast<uint64>(m_context)));
        }
        
        // Begin recording into the current frame's command buffer
        // UE5: VulkanRHI::BeginDrawingViewport() -> GetCommandContext()->Begin()
        MR_LOG_INFO("  Calling context->beginRecording()...");
        m_context->beginRecording();
        
        MR_LOG_INFO("FVulkanRHICommandListImmediate::begin: Command buffer recording started");
        MR_LOG_INFO("===== FVulkanRHICommandListImmediate::begin() END =====");
    }
    
    void FVulkanRHICommandListImmediate::end() {
        MR_LOG_INFO("===== FVulkanRHICommandListImmediate::end() START =====");
        if (!m_context) {
            MR_LOG_WARNING("FVulkanRHICommandListImmediate::end: No active context");
            return;
        }
        
        // End recording the command buffer
        // UE5: FVulkanCommandListContext::EndRecording()
        MR_LOG_INFO("  Calling m_context->endRecording()...");
        m_context->endRecording();
        
        MR_LOG_INFO("===== FVulkanRHICommandListImmediate::end() END =====");
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
        
        // Validate pipeline handle
        VkPipeline pipeline = vulkanPipeline->getPipeline();
        if (pipeline == VK_NULL_HANDLE) {
            MR_LOG_ERROR("FVulkanRHICommandListImmediate::setPipelineState: Pipeline handle is NULL!");
            MR_LOG_ERROR("  Pipeline name: " + vulkanPipeline->getDesc().debugName);
            return;
        }
        
        // Set the graphics pipeline in pending state
        // UE5: FVulkanPendingState::SetGraphicsPipeline()
        if (m_context->getPendingState()) {
            m_context->getPendingState()->setGraphicsPipeline(vulkanPipeline);
            MR_LOG_INFO("setPipelineState: Set pipeline '" + vulkanPipeline->getDesc().debugName + 
                        "' (handle: " + std::to_string(reinterpret_cast<uint64>(pipeline)) + ")");
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
    // Resource Binding (UE5: RHISetShaderUniformBuffer, RHISetShaderTexture)
    // ============================================================================
    
    void FVulkanRHICommandListImmediate::setConstantBuffer(uint32 slot, TSharedPtr<IRHIBuffer> buffer) {
        if (!buffer) {
            MR_LOG_WARNING("FVulkanRHICommandListImmediate::setConstantBuffer: Null buffer for slot " + 
                          std::to_string(slot));
            return;
        }
        
        // Track bound resource for descriptor set management
        auto& resource = m_boundResources[slot];
        resource.buffer = buffer;
        resource.texture.reset();
        resource.sampler.reset();
        resource.isDirty = true;
        m_descriptorsDirty = true;
        
        // Pass to pending state for descriptor set binding
        if (m_context && m_context->getPendingState()) {
            VulkanBuffer* vulkanBuffer = dynamic_cast<VulkanBuffer*>(buffer.get());
            if (vulkanBuffer) {
                m_context->getPendingState()->setUniformBuffer(
                    slot, 
                    vulkanBuffer->getBuffer(), 
                    vulkanBuffer->getOffset(), 
                    vulkanBuffer->getSize()
                );
            }
        }
        
        MR_LOG_DEBUG("FVulkanRHICommandListImmediate::setConstantBuffer: Bound to slot " + 
                    std::to_string(slot));
    }
    
    void FVulkanRHICommandListImmediate::setShaderResource(uint32 slot, TSharedPtr<IRHITexture> texture) {
        if (!texture) {
            MR_LOG_WARNING("FVulkanRHICommandListImmediate::setShaderResource: Null texture for slot " + 
                          std::to_string(slot));
            return;
        }
        
        // Track bound resource for descriptor set management
        auto& resource = m_boundResources[slot];
        resource.texture = texture;
        resource.buffer.reset();
        resource.isDirty = true;
        m_descriptorsDirty = true;
        
        // Pass to pending state for descriptor set binding
        if (m_context && m_context->getPendingState()) {
            VulkanTexture* vulkanTexture = dynamic_cast<VulkanTexture*>(texture.get());
            if (vulkanTexture) {
                // Get image view and default sampler from texture
                VkImageView imageView = vulkanTexture->getImageView();
                VkSampler sampler = vulkanTexture->getDefaultSampler();
                
                // Validate handles before binding
                if (imageView == VK_NULL_HANDLE) {
                    MR_LOG_ERROR("setShaderResource: ImageView is VK_NULL_HANDLE for slot " + std::to_string(slot));
                }
                if (sampler == VK_NULL_HANDLE) {
                    MR_LOG_ERROR("setShaderResource: Sampler is VK_NULL_HANDLE for slot " + std::to_string(slot));
                }
                
                VkImageLayout currentLayout = vulkanTexture->getCurrentLayout();
                MR_LOG_DEBUG("setShaderResource: slot=" + std::to_string(slot) + 
                            ", imageView=" + std::to_string(reinterpret_cast<uint64>(imageView)) +
                            ", sampler=" + std::to_string(reinterpret_cast<uint64>(sampler)) +
                            ", currentLayout=" + std::to_string(static_cast<int>(currentLayout)));
                
                // Pass VkImage handle for layout transitions before render pass
                VkImage image = vulkanTexture->getImage();
                VkFormat format = vulkanTexture->getVulkanFormat();
                uint32 mipLevels = texture->getDesc().mipLevels;
                uint32 arrayLayers = texture->getDesc().arraySize;
                
                m_context->getPendingState()->setTexture(slot, imageView, sampler, image, format, mipLevels, arrayLayers);
            } else {
                MR_LOG_ERROR("setShaderResource: Failed to cast texture to VulkanTexture for slot " + std::to_string(slot));
            }
        }
        
        MR_LOG_DEBUG("FVulkanRHICommandListImmediate::setShaderResource: Bound to slot " + 
                    std::to_string(slot));
    }
    
    void FVulkanRHICommandListImmediate::setSampler(uint32 slot, TSharedPtr<IRHISampler> sampler) {
        // Track bound sampler
        if (m_boundResources.Contains(slot)) {
            m_boundResources[slot].sampler = sampler;
            m_boundResources[slot].isDirty = true;
        } else {
            auto& resource = m_boundResources[slot];
            resource.sampler = sampler;
            resource.isDirty = true;
        }
        m_descriptorsDirty = true;
        
        MR_LOG_DEBUG("FVulkanRHICommandListImmediate::setSampler: Bound to slot " + 
                    std::to_string(slot));
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
        
        MR_LOG_INFO("FVulkanRHICommandListImmediate::setScissorRect: Set scissor rect (" + 
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
        MR_LOG_INFO("===== FVulkanRHICommandListImmediate::endRenderPass() START =====");
        if (!m_context) {
            MR_LOG_WARNING("FVulkanRHICommandListImmediate::endRenderPass: No active context");
            return;
        }
        
        // End the current render pass
        // UE5 Pattern: FVulkanCommandListContext::RHIEndRenderPass()
        // Calls vkCmdEndRenderPass
        MR_LOG_INFO("  Calling m_context->endRenderPass()...");
        m_context->endRenderPass();
        
        MR_LOG_INFO("===== FVulkanRHICommandListImmediate::endRenderPass() END =====");
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
            MR_LOG(LogVulkanRHI, Error, "transitionResource: No active context");
            return;
        }
        
        if (!resource) {
            MR_LOG(LogVulkanRHI, Warning, "transitionResource: Null resource");
            return;
        }
        
        // Get command buffer
        FVulkanCmdBuffer* cmdBufferWrapper = m_context->getCmdBuffer();
        if (!cmdBufferWrapper) {
            MR_LOG(LogVulkanRHI, Error, "transitionResource: No active command buffer wrapper");
            return;
        }
        
        VkCommandBuffer cmdBuffer = cmdBufferWrapper->getHandle();
        if (!cmdBuffer) {
            MR_LOG(LogVulkanRHI, Error, "transitionResource: No active command buffer");
            return;
        }
        
        // Convert EResourceUsage to ERHIAccess for barrier logic
        // Note: This is a simplified mapping, full implementation would need more granular control
        ERHIAccess accessBefore = ERHIAccess::Unknown;
        ERHIAccess accessAfter = ERHIAccess::Unknown;
        
        // Map EResourceUsage to ERHIAccess
        if (hasResourceUsage(stateBefore, EResourceUsage::RenderTarget)) {
            accessBefore = ERHIAccess::RTV;
        } else if (hasResourceUsage(stateBefore, EResourceUsage::DepthStencil)) {
            accessBefore = ERHIAccess::DSVWrite;
        } else if (hasResourceUsage(stateBefore, EResourceUsage::ShaderResource)) {
            accessBefore = ERHIAccess::SRVGraphics;
        } else if (hasResourceUsage(stateBefore, EResourceUsage::UnorderedAccess)) {
            accessBefore = ERHIAccess::UAVGraphics;
        } else if (hasResourceUsage(stateBefore, EResourceUsage::TransferSrc)) {
            accessBefore = ERHIAccess::CopySrc;
        } else if (hasResourceUsage(stateBefore, EResourceUsage::TransferDst)) {
            accessBefore = ERHIAccess::CopyDest;
        }
        
        if (hasResourceUsage(stateAfter, EResourceUsage::RenderTarget)) {
            accessAfter = ERHIAccess::RTV;
        } else if (hasResourceUsage(stateAfter, EResourceUsage::DepthStencil)) {
            accessAfter = ERHIAccess::DSVWrite;
        } else if (hasResourceUsage(stateAfter, EResourceUsage::ShaderResource)) {
            accessAfter = ERHIAccess::SRVGraphics;
        } else if (hasResourceUsage(stateAfter, EResourceUsage::UnorderedAccess)) {
            accessAfter = ERHIAccess::UAVGraphics;
        } else if (hasResourceUsage(stateAfter, EResourceUsage::TransferSrc)) {
            accessAfter = ERHIAccess::CopySrc;
        } else if (hasResourceUsage(stateAfter, EResourceUsage::TransferDst)) {
            accessAfter = ERHIAccess::CopyDest;
        }
        
        // Check if this is a texture or buffer
        IRHITexture* texture = dynamic_cast<IRHITexture*>(resource.Get());
        IRHIBuffer* buffer = dynamic_cast<IRHIBuffer*>(resource.Get());
        
        if (texture) {
            // Texture transition - use image memory barrier
            VulkanTexture* vkTexture = static_cast<VulkanTexture*>(texture);
            
            bool bIsDepthStencil = hasResourceUsage(stateAfter, EResourceUsage::DepthStencil) ||
                                   hasResourceUsage(stateBefore, EResourceUsage::DepthStencil);
            
            // Get Vulkan flags using RHI barrier utilities
            VkPipelineStageFlags srcStage = getVulkanStageFlags(accessBefore, true);
            VkPipelineStageFlags dstStage = getVulkanStageFlags(accessAfter, false);
            VkAccessFlags srcAccess = getVulkanAccessFlags(accessBefore);
            VkAccessFlags dstAccess = getVulkanAccessFlags(accessAfter);
            VkImageLayout oldLayout = static_cast<VkImageLayout>(getVulkanImageLayout(accessBefore, bIsDepthStencil, true));
            VkImageLayout newLayout = static_cast<VkImageLayout>(getVulkanImageLayout(accessAfter, bIsDepthStencil, false));
            
            // Setup image memory barrier
            VkImageMemoryBarrier barrier = {};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcAccessMask = srcAccess;
            barrier.dstAccessMask = dstAccess;
            barrier.oldLayout = oldLayout;
            barrier.newLayout = newLayout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = vkTexture->getImage();
            
            // Determine aspect mask based on texture format
            VkFormat vkFormat = vkTexture->getVulkanFormat();
            barrier.subresourceRange.aspectMask = VulkanUtils::getImageAspectMask(vkFormat);
            
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
            
            // Insert pipeline barrier
            vkCmdPipelineBarrier(
                cmdBuffer,
                srcStage,
                dstStage,
                0,
                0, nullptr,
                0, nullptr,
                1, &barrier
            );
            
            MR_LOG(LogVulkanRHI, Verbose, "Texture transition: layout %d -> %d", oldLayout, newLayout);
        }
        else if (buffer) {
            // Buffer transition - use buffer memory barrier
            VulkanBuffer* vkBuffer = static_cast<VulkanBuffer*>(buffer);
            
            // Get Vulkan flags
            VkPipelineStageFlags srcStage = getVulkanStageFlags(accessBefore, true);
            VkPipelineStageFlags dstStage = getVulkanStageFlags(accessAfter, false);
            VkAccessFlags srcAccess = getVulkanAccessFlags(accessBefore);
            VkAccessFlags dstAccess = getVulkanAccessFlags(accessAfter);
            
            // Setup buffer memory barrier
            VkBufferMemoryBarrier barrier = {};
            barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            barrier.srcAccessMask = srcAccess;
            barrier.dstAccessMask = dstAccess;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = vkBuffer->getBuffer();
            barrier.offset = 0;
            barrier.size = VK_WHOLE_SIZE;
            
            // Insert pipeline barrier
            vkCmdPipelineBarrier(
                cmdBuffer,
                srcStage,
                dstStage,
                0,
                0, nullptr,
                1, &barrier,
                0, nullptr
            );
            
            MR_LOG(LogVulkanRHI, Verbose, "Buffer transition: access 0x%x -> 0x%x", srcAccess, dstAccess);
        }
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
    
    // ============================================================================
    // Texture Operations Implementation
    // Reference: UE5 FVulkanDynamicRHI texture operations
    // ============================================================================
    
    bool FVulkanRHICommandListImmediate::isRecording() const {
        if (!m_context) return false;
        auto* cmdBuffer = m_context->getCmdBuffer();
        return cmdBuffer && cmdBuffer->hasBegun() && !cmdBuffer->hasEnded();
    }
    
    void FVulkanRHICommandListImmediate::transitionTextureLayoutSimple(
        TSharedPtr<IRHITexture> texture,
        VkImageLayout oldLayout,
        VkImageLayout newLayout) {
        
        // Determine access masks and pipeline stages based on layouts
        // Reference: UE5 VulkanRHI::GetVkStageFlagsForLayout
        VkAccessFlags srcAccessMask = 0;
        VkAccessFlags dstAccessMask = 0;
        VkPipelineStageFlags srcStageMask = 0;
        VkPipelineStageFlags dstStageMask = 0;
        
        // Source layout -> access mask and stage
        switch (oldLayout) {
            case VK_IMAGE_LAYOUT_UNDEFINED:
                srcAccessMask = 0;
                srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                break;
            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
                break;
            case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
                srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
                break;
            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                break;
            case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
                srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                break;
            default:
                srcAccessMask = 0;
                srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                break;
        }
        
        // Destination layout -> access mask and stage
        switch (newLayout) {
            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
                break;
            case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
                dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
                break;
            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                break;
            case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
                dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                break;
            case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
                dstAccessMask = 0;
                dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
                break;
            default:
                dstAccessMask = 0;
                dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                break;
        }
        
        transitionTextureLayout(texture, oldLayout, newLayout, 
                               srcAccessMask, dstAccessMask, srcStageMask, dstStageMask);
    }
    
    void FVulkanRHICommandListImmediate::transitionTextureLayout(
        TSharedPtr<IRHITexture> texture,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        VkAccessFlags srcAccessMask,
        VkAccessFlags dstAccessMask,
        VkPipelineStageFlags srcStageMask,
        VkPipelineStageFlags dstStageMask) {
        
        if (!texture) {
            MR_LOG_ERROR("transitionTextureLayout: texture is null");
            return;
        }
        
        // Backend type validation - ensure texture is from Vulkan backend
        // 后端类型验证 - 确保纹理来自 Vulkan 后端
        if (texture->getBackendType() != ERHIBackend::Vulkan) {
            MR_LOG_ERROR("transitionTextureLayout: texture is not a Vulkan resource");
            return;
        }
        
        if (!m_context) {
            MR_LOG_ERROR("transitionTextureLayout: No active context");
            return;
        }
        
        auto* cmdBuffer = m_context->getCmdBuffer();
        if (!cmdBuffer || cmdBuffer->getHandle() == VK_NULL_HANDLE) {
            MR_LOG_ERROR("transitionTextureLayout: No valid command buffer");
            return;
        }
        
        // Get Vulkan texture (safe after backend validation)
        auto* vulkanTexture = static_cast<VulkanTexture*>(texture.get());
        VkImage image = vulkanTexture->getImage();
        
        if (image == VK_NULL_HANDLE) {
            MR_LOG_ERROR("transitionTextureLayout: Invalid VkImage");
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
        
        // Determine aspect mask based on texture format
        VkFormat vkFormat = vulkanTexture->getVulkanFormat();
        barrier.subresourceRange.aspectMask = VulkanUtils::getImageAspectMask(vkFormat);
        
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = texture->getDesc().mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = texture->getDesc().arraySize;
        barrier.srcAccessMask = srcAccessMask;
        barrier.dstAccessMask = dstAccessMask;
        
        // Insert pipeline barrier
        const auto& functions = VulkanAPI::getFunctions();
        functions.vkCmdPipelineBarrier(
            cmdBuffer->getHandle(),
            srcStageMask,
            dstStageMask,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier
        );
        
        // Update texture's current layout
        vulkanTexture->setCurrentLayout(newLayout);
        
        MR_LOG(LogTemp, Log, "transitionTextureLayout: %d -> %d", (int32)oldLayout, (int32)newLayout);
    }
    
    void FVulkanRHICommandListImmediate::copyBufferToTexture(
        TSharedPtr<IRHIBuffer> srcBuffer,
        SIZE_T srcOffset,
        TSharedPtr<IRHITexture> dstTexture,
        uint32 mipLevel,
        uint32 arrayLayer,
        uint32 width,
        uint32 height,
        uint32 depth) {
        
        if (!srcBuffer || !dstTexture) {
            MR_LOG_ERROR("copyBufferToTexture: Invalid buffer or texture");
            return;
        }
        
        // Backend type validation - ensure resources are from Vulkan backend
        // 后端类型验证 - 确保资源来自 Vulkan 后端
        if (srcBuffer->getBackendType() != ERHIBackend::Vulkan) {
            MR_LOG_ERROR("copyBufferToTexture: srcBuffer is not a Vulkan resource");
            return;
        }
        if (dstTexture->getBackendType() != ERHIBackend::Vulkan) {
            MR_LOG_ERROR("copyBufferToTexture: dstTexture is not a Vulkan resource");
            return;
        }
        
        if (!m_context) {
            MR_LOG_ERROR("copyBufferToTexture: No active context");
            return;
        }
        
        auto* cmdBuffer = m_context->getCmdBuffer();
        if (!cmdBuffer || cmdBuffer->getHandle() == VK_NULL_HANDLE) {
            MR_LOG_ERROR("copyBufferToTexture: No valid command buffer");
            return;
        }
        
        // Debug validation: check mip/layer/extent bounds
        // Debug 校验：检查 mip/layer/extent 边界
#ifdef _DEBUG
        const TextureDesc& texDesc = dstTexture->getDesc();
        if (mipLevel >= texDesc.mipLevels) {
            MR_LOG(LogTemp, Error, "copyBufferToTexture: mipLevel %u >= mipLevels %u", mipLevel, texDesc.mipLevels);
            return;
        }
        if (arrayLayer >= texDesc.arraySize) {
            MR_LOG(LogTemp, Error, "copyBufferToTexture: arrayLayer %u >= arraySize %u", arrayLayer, texDesc.arraySize);
            return;
        }
        // Calculate expected mip dimensions
        uint32 mipWidth = (std::max)(1u, texDesc.width >> mipLevel);
        uint32 mipHeight = (std::max)(1u, texDesc.height >> mipLevel);
        uint32 mipDepth = (std::max)(1u, texDesc.depth >> mipLevel);
        if (width > mipWidth || height > mipHeight || depth > mipDepth) {
            MR_LOG(LogTemp, Error, "copyBufferToTexture: extent (%ux%ux%u) exceeds mip%u dimensions (%ux%ux%u)",
                   width, height, depth, mipLevel, mipWidth, mipHeight, mipDepth);
            return;
        }
#endif
        
        // Get Vulkan handles (safe after backend validation)
        auto* vulkanBuffer = static_cast<VulkanBuffer*>(srcBuffer.get());
        auto* vulkanTexture = static_cast<VulkanTexture*>(dstTexture.get());
        
        VkBuffer buffer = vulkanBuffer->getBuffer();
        VkImage image = vulkanTexture->getImage();
        
        if (buffer == VK_NULL_HANDLE || image == VK_NULL_HANDLE) {
            MR_LOG_ERROR("copyBufferToTexture: Invalid Vulkan handles");
            return;
        }
        
        // Setup buffer image copy region
        VkBufferImageCopy region{};
        region.bufferOffset = srcOffset;
        region.bufferRowLength = 0;   // Tightly packed
        region.bufferImageHeight = 0; // Tightly packed
        
        // Determine aspect mask based on texture format
        VulkanTexture* vulkanDstTexture = static_cast<VulkanTexture*>(dstTexture.get());
        VkFormat vkFormat = vulkanDstTexture->getVulkanFormat();
        region.imageSubresource.aspectMask = VulkanUtils::getImageAspectMask(vkFormat);
        
        region.imageSubresource.mipLevel = mipLevel;
        region.imageSubresource.baseArrayLayer = arrayLayer;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {width, height, depth};
        
        // Execute copy
        const auto& functions = VulkanAPI::getFunctions();
        functions.vkCmdCopyBufferToImage(
            cmdBuffer->getHandle(),
            buffer,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &region
        );
        
        MR_LOG(LogTemp, Verbose, "copyBufferToTexture: mip %u, %ux%ux%u", mipLevel, width, height, depth);
    }
}
