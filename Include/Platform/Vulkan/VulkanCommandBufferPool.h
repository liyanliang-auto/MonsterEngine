// Copyright Monster Engine. All Rights Reserved.

#pragma once

#include "Core/CoreMinimal.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include <mutex>
#include <atomic>
#include <thread>

namespace MonsterRender::RHI::Vulkan {

// Forward declarations
class VulkanDevice;

/**
 * FVulkanCommandBufferPool
 * 
 * Thread-local command buffer pool for efficient multi-threaded command recording.
 * Each worker thread gets its own command pool to avoid synchronization overhead.
 * 
 * Based on UE5's FVulkanCommandBufferPool design pattern.
 * Reference: UE5 Engine/Source/Runtime/VulkanRHI/Private/VulkanCommandBuffer.h
 * 
 * Key features:
 * - Thread-local command pools (VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT)
 * - Support for both primary and secondary command buffers
 * - Automatic command buffer recycling
 * - Lock-free allocation from thread-local pool
 */
class FVulkanCommandBufferPool {
public:
    /**
     * Command buffer level
     */
    enum class ELevel : uint8 {
        Primary,    // VK_COMMAND_BUFFER_LEVEL_PRIMARY
        Secondary   // VK_COMMAND_BUFFER_LEVEL_SECONDARY
    };
    
    /**
     * Pooled command buffer entry
     */
    struct FPooledCommandBuffer {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        ELevel level = ELevel::Primary;
        bool isInUse = false;
        uint64 lastUsedFrame = 0;
        
        FPooledCommandBuffer() = default;
        FPooledCommandBuffer(VkCommandBuffer cb, ELevel lvl)
            : commandBuffer(cb), level(lvl), isInUse(false), lastUsedFrame(0) {}
    };
    
    FVulkanCommandBufferPool(VulkanDevice* device, uint32 queueFamilyIndex);
    ~FVulkanCommandBufferPool();
    
    // Non-copyable
    FVulkanCommandBufferPool(const FVulkanCommandBufferPool&) = delete;
    FVulkanCommandBufferPool& operator=(const FVulkanCommandBufferPool&) = delete;
    
    /**
     * Initialize the command buffer pool
     * Creates the Vulkan command pool with appropriate flags
     */
    bool Initialize();
    
    /**
     * Allocate a command buffer from the pool
     * Reuses existing buffers if available, otherwise allocates new ones
     * 
     * @param level Primary or Secondary command buffer
     * @return Allocated command buffer handle
     */
    VkCommandBuffer AllocateCommandBuffer(ELevel level = ELevel::Primary);
    
    /**
     * Recycle a command buffer back to the pool
     * Resets the command buffer and marks it as available for reuse
     * 
     * @param commandBuffer Command buffer to recycle
     */
    void RecycleCommandBuffer(VkCommandBuffer commandBuffer);
    
    /**
     * Reset all command buffers in the pool
     * Called at the end of frame to prepare for next frame
     */
    void ResetPool();
    
    /**
     * Trim the pool by removing unused command buffers
     * Frees command buffers that haven't been used for several frames
     * 
     * @param currentFrame Current frame number
     * @param framesToKeep Number of frames to keep unused buffers
     */
    void TrimPool(uint64 currentFrame, uint32 framesToKeep = 60);
    
    /**
     * Get the Vulkan command pool handle
     */
    VkCommandPool GetHandle() const { return m_commandPool; }
    
    /**
     * Get statistics
     */
    struct FStats {
        uint32 totalAllocated = 0;
        uint32 activeBuffers = 0;
        uint32 pooledBuffers = 0;
        uint32 peakActive = 0;
    };
    
    FStats GetStats() const;
    
private:
    /**
     * Create the Vulkan command pool
     */
    bool CreateCommandPool();
    
    /**
     * Allocate a new command buffer from Vulkan
     */
    VkCommandBuffer AllocateNewCommandBuffer(ELevel level);
    
private:
    VulkanDevice* m_device;
    uint32 m_queueFamilyIndex;
    VkCommandPool m_commandPool;
    
    // Pool of available command buffers
    TArray<FPooledCommandBuffer> m_pooledBuffers;
    
    // Mutex for thread-safe access
    mutable std::mutex m_poolMutex;
    
    // Statistics
    std::atomic<uint32> m_totalAllocated{0};
    std::atomic<uint32> m_activeBuffers{0};
    std::atomic<uint32> m_peakActive{0};
};

/**
 * FVulkanThreadLocalCommandPool
 * 
 * Thread-local command pool manager.
 * Each thread gets its own command pool to avoid lock contention.
 * 
 * Based on UE5's thread-local command buffer allocation pattern.
 */
class FVulkanThreadLocalCommandPool {
public:
    /**
     * Get or create command pool for current thread
     * 
     * @param device Vulkan device
     * @param queueFamilyIndex Queue family index for the pool
     * @return Thread-local command buffer pool
     */
    static FVulkanCommandBufferPool* GetOrCreateForCurrentThread(
        VulkanDevice* device,
        uint32 queueFamilyIndex
    );
    
    /**
     * Destroy all thread-local pools
     * Called during shutdown
     */
    static void DestroyAllPools();
    
    /**
     * Reset all thread-local pools
     * Called at end of frame
     */
    static void ResetAllPools();
    
    /**
     * Trim all thread-local pools
     */
    static void TrimAllPools(uint64 currentFrame, uint32 framesToKeep = 60);
    
private:
    // Thread-local storage for command pools
    static thread_local TUniquePtr<FVulkanCommandBufferPool> s_threadLocalPool;
    
    // Global registry of all thread pools (for cleanup)
    static TArray<FVulkanCommandBufferPool*> s_allPools;
    static std::mutex s_registryMutex;
    
    // Device and queue family for pool creation
    static VulkanDevice* s_device;
    static uint32 s_queueFamilyIndex;
};

/**
 * FVulkanSecondaryCommandBuffer
 * 
 * Wrapper for secondary command buffers with inheritance info.
 * Secondary command buffers can be recorded in parallel and executed
 * within a primary command buffer.
 * 
 * Based on UE5's secondary command buffer pattern.
 */
class FVulkanSecondaryCommandBuffer {
public:
    FVulkanSecondaryCommandBuffer(VulkanDevice* device, VkCommandBuffer commandBuffer);
    ~FVulkanSecondaryCommandBuffer();
    
    /**
     * Begin recording with render pass inheritance
     * 
     * @param renderPass Render pass to inherit from
     * @param subpass Subpass index
     * @param framebuffer Framebuffer to inherit from (optional)
     */
    void Begin(VkRenderPass renderPass, uint32 subpass, VkFramebuffer framebuffer = VK_NULL_HANDLE);
    
    /**
     * End recording
     */
    void End();
    
    /**
     * Get the command buffer handle
     */
    VkCommandBuffer GetHandle() const { return m_commandBuffer; }
    
    /**
     * Check if recording has begun
     */
    bool HasBegun() const { return m_hasBegun; }
    
private:
    VulkanDevice* m_device;
    VkCommandBuffer m_commandBuffer;
    bool m_hasBegun;
};

} // namespace MonsterRender::RHI::Vulkan
