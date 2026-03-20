#pragma once

#include "Core/CoreMinimal.h"
#include "Platform/Vulkan/VulkanRHI.h"

namespace MonsterRender::RHI::Vulkan {
    
    // Forward declarations
    class VulkanDevice;
    class FVulkanCmdBuffer;
    class FVulkanCommandBufferManager;
    class FVulkanPendingState;
    class FVulkanDescriptorPoolSetContainer;
    
    /**
     * FVulkanCommandListContext - Per-frame command list context (UE5 pattern)
     * Integrates command buffer, pending state, and descriptor pool for each frame
     * This is the main class that manages the lifecycle of a frame's rendering commands
     * 
     * Reference: UE5 Engine/Source/Runtime/VulkanRHI/Private/VulkanCommandListContext.h
     */
    class FVulkanCommandListContext {
    public:
        FVulkanCommandListContext(VulkanDevice* device, FVulkanCommandBufferManager* manager);
        ~FVulkanCommandListContext();
        
        // Non-copyable
        FVulkanCommandListContext(const FVulkanCommandListContext&) = delete;
        FVulkanCommandListContext& operator=(const FVulkanCommandListContext&) = delete;
        
        /**
         * Initialize the context
         */
        bool initialize();
        
        /**
         * Prepare for new frame (UE5: PrepareForNewActiveCommandBuffer())
         * Called at the beginning of each frame
         */
        void prepareForNewFrame();
        
        /**
         * Refresh command buffer after synchronous operations
         * Unlike prepareForNewFrame(), this does NOT acquire swapchain image
         */
        void refreshCommandBuffer();
        
        /**
         * Get command buffer for this context
         */
        FVulkanCmdBuffer* getCmdBuffer() const { return m_cmdBuffer; }
        
        /**
         * Get pending state for this context
         */
        FVulkanPendingState* getPendingState() const { return m_pendingState.get(); }
        
        /**
         * Get descriptor pool for this context
         */
        FVulkanDescriptorPoolSetContainer* getDescriptorPool() const { return m_descriptorPool.get(); }
        
        /**
         * Get command buffer manager for this context
         */
        FVulkanCommandBufferManager* getCommandBufferManager() const { return m_manager; }
        
        /**
         * Allocate a secondary command buffer for parallel recording
         * Secondary command buffers can be recorded in parallel and executed within a primary buffer
         * 
         * @param renderPass Render pass to inherit from (optional)
         * @param subpass Subpass index (default 0)
         * @param framebuffer Framebuffer to inherit from (optional)
         * @return Secondary command buffer, nullptr on failure
         */
        FVulkanCmdBuffer* AllocateSecondaryCommandBuffer(
            VkRenderPass renderPass = VK_NULL_HANDLE,
            uint32 subpass = 0,
            VkFramebuffer framebuffer = VK_NULL_HANDLE
        );
        
        /**
         * Free a secondary command buffer back to the pool
         * 
         * @param cmdBuffer Secondary command buffer to free
         */
        void FreeSecondaryCommandBuffer(FVulkanCmdBuffer* cmdBuffer);
        
        /**
         * Reset all secondary command buffers in the pool
         * Called at the end of frame to recycle buffers
         */
        void ResetSecondaryCommandBuffers();
        
        /**
         * Begin recording commands
         */
        void beginRecording();
        
        /**
         * End recording commands
         */
        void endRecording();
        
        /**
         * End render pass (transition from rendering to next phase)
         */
        void endRenderPass();
        
        /**
         * Submit commands to GPU (UE5: SubmitCommandsAndFlushGPU())
         */
        void submitCommands(VkSemaphore* waitSemaphores, uint32 waitSemaphoreCount,
                          VkSemaphore* signalSemaphores, uint32 signalSemaphoreCount);
        
        // Command recording delegations
        void setRenderTargets(TSpan<TSharedPtr<RHI::IRHITexture>> renderTargets,
                            TSharedPtr<RHI::IRHITexture> depthStencil = nullptr);
        
        void draw(uint32 vertexCount, uint32 startVertexLocation = 0);
        void drawIndexed(uint32 indexCount, uint32 startIndexLocation = 0, int32 baseVertexLocation = 0);
        void drawInstanced(uint32 vertexCountPerInstance, uint32 instanceCount,
                         uint32 startVertexLocation = 0, uint32 startInstanceLocation = 0);
        void drawIndexedInstanced(uint32 indexCountPerInstance, uint32 instanceCount,
                                uint32 startIndexLocation = 0, int32 baseVertexLocation = 0,
                                uint32 startInstanceLocation = 0);
        
        void clearRenderTarget(TSharedPtr<RHI::IRHITexture> renderTarget, const float32 clearColor[4]);
        void clearDepthStencil(TSharedPtr<RHI::IRHITexture> depthStencil, 
                             bool clearDepth = true, bool clearStencil = false,
                             float32 depth = 1.0f, uint8 stencil = 0);
        
    private:
        /**
         * Acquire next swapchain image for rendering
         * Called at the beginning of each frame to ensure correct framebuffer is used
         * Reference: UE5 FVulkanDynamicRHI::RHIBeginDrawingViewport
         */
        bool acquireNextSwapchainImage();
        
    private:
        VulkanDevice* m_device;
        FVulkanCommandBufferManager* m_manager;
        
        // Command buffer for this frame
        FVulkanCmdBuffer* m_cmdBuffer;
        
        // Pending state management
        TUniquePtr<FVulkanPendingState> m_pendingState;
        
        // Per-frame descriptor pool
        TUniquePtr<FVulkanDescriptorPoolSetContainer> m_descriptorPool;
        
        // ========================================================================
        // Secondary Command Buffer Pool
        // ========================================================================
        
        /**
         * Pool of secondary command buffers for parallel recording
         * These buffers are allocated on demand and recycled at the end of frame
         */
        struct FSecondaryCommandBufferEntry {
            TUniquePtr<FVulkanCmdBuffer> cmdBuffer;
            bool isInUse = false;
            VkRenderPass inheritedRenderPass = VK_NULL_HANDLE;
            uint32 inheritedSubpass = 0;
            VkFramebuffer inheritedFramebuffer = VK_NULL_HANDLE;
        };
        
        TArray<FSecondaryCommandBufferEntry> m_secondaryCommandBuffers;
        VkCommandPool m_secondaryCommandPool = VK_NULL_HANDLE;
    };
}
