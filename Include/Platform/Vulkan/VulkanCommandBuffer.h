#pragma once

#include "Core/CoreMinimal.h"
#include "Platform/Vulkan/VulkanRHI.h"

namespace MonsterRender::RHI::Vulkan {
    
    // Forward declarations
    class VulkanDevice;
    class FVulkanCommandBufferManager;
    
    /**
     * FVulkanCmdBuffer - Per-frame Command Buffer (UE5 pattern)
     * Encapsulates a single Vulkan command buffer with its lifecycle management
     * Each frame in the ring buffer has its own FVulkanCmdBuffer instance
     * 
     * Reference: UE5 Engine/Source/Runtime/VulkanRHI/Private/VulkanCommandBuffer.h
     */
    class FVulkanCmdBuffer {
    public:
        FVulkanCmdBuffer(VulkanDevice* device, FVulkanCommandBufferManager* manager);
        ~FVulkanCmdBuffer();
        
        // Non-copyable
        FVulkanCmdBuffer(const FVulkanCmdBuffer&) = delete;
        FVulkanCmdBuffer& operator=(const FVulkanCmdBuffer&) = delete;
        
        /**
         * Initialize the command buffer
         * Allocates the Vulkan command buffer from the command pool
         */
        bool initialize();
        
        /**
         * Begin recording commands (UE5: Begin())
         * Puts the command buffer into recording state
         */
        void begin();
        
        /**
         * End recording commands (UE5: End())
         * Finalizes the command buffer for submission
         */
        void end();
        
        /**
         * Check if the command buffer has begun recording (UE5: HasBegun())
         */
        bool hasBegun() const { return m_state == EState::Recording; }
        
        /**
         * Check if the command buffer has ended and is ready for submission (UE5: HasEnded())
         */
        bool hasEnded() const { return m_state == EState::Ended; }
        
        /**
         * Check if the command buffer is submitted to GPU (UE5: IsSubmitted())
         */
        bool isSubmitted() const { return m_state == EState::Submitted; }
        
        /**
         * Mark as submitted (called by command buffer manager after queue submit)
         */
        void markSubmitted() { m_state = EState::Submitted; }
        
        /**
         * Mark as ready for begin (called after vkResetCommandBuffer)
         */
        void markAsReadyForBegin();
        
        /**
         * Refresh the fence state (UE5: RefreshFenceStatus())
         * Checks if the GPU has finished executing this command buffer
         */
        void refreshFenceStatus();
        
        /**
         * Get the native Vulkan command buffer handle
         */
        VkCommandBuffer getHandle() const { return m_commandBuffer; }
        
        /**
         * Get the fence associated with this command buffer (UE5: GetFence())
         */
        VkFence getFence() const { return m_fence; }
        
        /**
         * Check if GPU has finished executing (UE5: IsInsideRenderPass())
         */
        bool isInsideRenderPass() const { return m_insideRenderPass; }
        
        /**
         * Mark render pass state
         */
        void beginRenderPass() { m_insideRenderPass = true; }
        void endRenderPass() { m_insideRenderPass = false; }
        
        /**
         * Get fence creation counter (for debugging)
         */
        uint64 getFenceSignaledCounter() const { return m_fenceSignaledCounter; }
        
    private:
        // Command buffer state machine (UE5 pattern)
        enum class EState : uint8 {
            ReadyForBegin,  // Initial state, can call Begin()
            Recording,      // Begin() called, can record commands
            Ended,          // End() called, ready for submission
            Submitted,      // Submitted to GPU queue
            NotAllocated    // Command buffer not allocated yet
        };
        
        VulkanDevice* m_device;
        FVulkanCommandBufferManager* m_manager;
        
        VkCommandBuffer m_commandBuffer;
        VkFence m_fence;
        
        EState m_state;
        bool m_insideRenderPass;
        
        // Statistics (UE5 pattern)
        uint64 m_fenceSignaledCounter;
        uint64 m_submittedCounter;
    };
    
    /**
     * FVulkanCommandBufferManager - Manages per-frame command buffers (UE5 pattern)
     * Maintains a ring buffer of command buffers (typically 3 for triple buffering)
     * Ensures thread-safe allocation and submission
     * 
     * Reference: UE5 Engine/Source/Runtime/VulkanRHI/Private/VulkanCommandBuffer.h
     */
    class FVulkanCommandBufferManager {
    public:
        FVulkanCommandBufferManager(VulkanDevice* device);
        ~FVulkanCommandBufferManager();
        
        // Non-copyable
        FVulkanCommandBufferManager(const FVulkanCommandBufferManager&) = delete;
        FVulkanCommandBufferManager& operator=(const FVulkanCommandBufferManager&) = delete;
        
        /**
         * Initialize the command buffer manager
         * Creates the command pool and initial command buffers
         */
        bool initialize();
        
        /**
         * Get or create a command buffer for the current frame (UE5: GetActiveCmdBuffer())
         * Returns a ready-to-use command buffer
         */
        FVulkanCmdBuffer* getActiveCmdBuffer();
        
        /**
         * Submit the active command buffer to the GPU queue (UE5: SubmitActiveCmdBuffer())
         */
        void submitActiveCmdBuffer(VkSemaphore* waitSemaphores, uint32 waitSemaphoreCount,
                                   VkSemaphore* signalSemaphores, uint32 signalSemaphoreCount);
        
        /**
         * Submit the active command buffer with a specific fence for frame synchronization
         * @param fence External fence to signal on completion (e.g., in-flight fence)
         */
        void submitActiveCmdBufferWithFence(VkSemaphore* waitSemaphores, uint32 waitSemaphoreCount,
                                            VkSemaphore* signalSemaphores, uint32 signalSemaphoreCount,
                                            VkFence fence);
        
        /**
         * Wait for a specific command buffer to complete (UE5: WaitForCmdBuffer())
         */
        void waitForCmdBuffer(FVulkanCmdBuffer* cmdBuffer, float timeInSecondsToWait = 1.0f);
        
        /**
         * Prepare for next frame (UE5: PrepareForNewActiveCommandBuffer())
         * Advances to the next frame in the ring buffer
         */
        void prepareForNewActiveCommandBuffer();
        
        /**
         * Get the command pool handle
         */
        VkCommandPool getCommandPool() const { return m_commandPool; }
        
        /**
         * Get queue for submission
         */
        VkQueue getQueue() const;
        
    private:
        /**
         * Create command pool for this manager
         */
        bool createCommandPool();
        
        /**
         * Create ring buffer of command buffers
         */
        bool createCommandBuffers();
        
    private:
        VulkanDevice* m_device;
        VkCommandPool m_commandPool;
        
        // Ring buffer of command buffers (UE5: CmdBuffers)
        static constexpr uint32 NUM_FRAMES_IN_FLIGHT = 3;
        TArray<TUniquePtr<FVulkanCmdBuffer>> m_cmdBuffers;
        
        // Current frame index in the ring buffer
        uint32 m_currentBufferIndex;
        
        // Active command buffer being recorded
        FVulkanCmdBuffer* m_activeCmdBuffer;
    };
}
