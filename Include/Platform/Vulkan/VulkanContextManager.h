// Copyright Monster Engine. All Rights Reserved.

#pragma once

#include "Core/CoreMinimal.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include <mutex>
#include <thread>
#include <atomic>

namespace MonsterRender::RHI::Vulkan {

// Forward declarations
class VulkanDevice;
class FVulkanCommandListContext;
class FVulkanCommandBufferPool;

/**
 * FVulkanContextManager
 * 
 * Manages multiple Vulkan command list contexts for multi-threaded rendering.
 * Each worker thread gets its own context to avoid synchronization overhead.
 * 
 * Based on UE5's multi-threaded rendering context management pattern.
 * Reference: UE5 Engine/Source/Runtime/VulkanRHI/Private/VulkanContext.h
 * 
 * Key features:
 * - Thread-local context allocation
 * - Context pooling and reuse
 * - Automatic context lifecycle management
 * - Thread-safe context access
 * 
 * Usage pattern:
 * ```cpp
 * // Get context for current thread
 * auto* context = FVulkanContextManager::GetOrCreateForCurrentThread(device);
 * 
 * // Record commands
 * context->beginRecording();
 * context->draw(...);
 * context->endRecording();
 * 
 * // Submit to GPU
 * context->submitCommands(...);
 * 
 * // Recycle when done
 * FVulkanContextManager::RecycleContext(context);
 * ```
 */
class FVulkanContextManager {
public:
    /**
     * Context pool entry
     */
    struct FPooledContext {
        TUniquePtr<FVulkanCommandListContext> context;
        std::thread::id ownerThreadId;
        bool isInUse = false;
        uint64 lastUsedFrame = 0;
        
        FPooledContext() = default;
        FPooledContext(TUniquePtr<FVulkanCommandListContext>&& ctx, std::thread::id threadId)
            : context(std::move(ctx))
            , ownerThreadId(threadId)
            , isInUse(false)
            , lastUsedFrame(0)
        {}
    };
    
    /**
     * Initialize the context manager
     * Must be called before any context allocation
     * 
     * @param device Vulkan device
     * @param maxContexts Maximum number of contexts to pool (0 = unlimited)
     */
    static bool Initialize(VulkanDevice* device, uint32 maxContexts = 0);
    
    /**
     * Shutdown the context manager
     * Destroys all pooled contexts
     */
    static void Shutdown();
    
    /**
     * Check if the context manager is initialized
     */
    static bool IsInitialized();
    
    /**
     * Get or create a context for the current thread
     * 
     * This function is thread-safe and will:
     * 1. Check if current thread already has a context
     * 2. Try to reuse an available context from the pool
     * 3. Create a new context if none available
     * 
     * @return Context for current thread, nullptr on failure
     */
    static FVulkanCommandListContext* GetOrCreateForCurrentThread();
    
    /**
     * Recycle a context back to the pool
     * 
     * The context will be reset and marked as available for reuse.
     * This should be called when a thread finishes using a context.
     * 
     * @param context Context to recycle
     */
    static void RecycleContext(FVulkanCommandListContext* context);
    
    /**
     * Wait for all active contexts to complete
     * 
     * This will block until all contexts have finished their GPU work.
     * Useful for synchronization points like end of frame.
     */
    static void WaitForAllContexts();
    
    /**
     * Reset all contexts
     * 
     * Called at the end of frame to prepare contexts for next frame.
     * This resets command buffers and descriptor pools.
     */
    static void ResetAllContexts();
    
    /**
     * Trim the context pool
     * 
     * Removes contexts that haven't been used for several frames.
     * Helps reduce memory usage during low activity periods.
     * 
     * @param currentFrame Current frame number
     * @param framesToKeep Number of frames to keep unused contexts
     */
    static void TrimPool(uint64 currentFrame, uint32 framesToKeep = 60);
    
    /**
     * Get statistics about context usage
     */
    struct FStats {
        uint32 totalContexts = 0;      // Total contexts in pool
        uint32 activeContexts = 0;     // Currently in use
        uint32 availableContexts = 0;  // Available for reuse
        uint32 peakActive = 0;         // Peak concurrent contexts
    };
    
    static FStats GetStats();
    
private:
    /**
     * Create a new context
     */
    static FVulkanCommandListContext* CreateNewContext();
    
    /**
     * Find an available context in the pool
     */
    static FVulkanCommandListContext* FindAvailableContext();
    
private:
    // Singleton instance
    static VulkanDevice* s_device;
    static uint32 s_maxContexts;
    static bool s_initialized;
    
    // Context pool
    static TArray<FPooledContext> s_contextPool;
    static std::mutex s_poolMutex;
    
    // Thread-local context mapping
    static thread_local FVulkanCommandListContext* s_threadLocalContext;
    
    // Statistics
    static std::atomic<uint32> s_totalContexts;
    static std::atomic<uint32> s_activeContexts;
    static std::atomic<uint32> s_peakActive;
};

/**
 * FVulkanScopedContext
 * 
 * RAII wrapper for automatic context acquisition and recycling.
 * Ensures contexts are properly recycled even if exceptions occur.
 * 
 * Usage:
 * ```cpp
 * {
 *     FVulkanScopedContext scopedContext;
 *     auto* context = scopedContext.GetContext();
 *     
 *     context->beginRecording();
 *     context->draw(...);
 *     context->endRecording();
 *     context->submitCommands(...);
 * } // Context automatically recycled here
 * ```
 */
class FVulkanScopedContext {
public:
    FVulkanScopedContext();
    ~FVulkanScopedContext();
    
    // Non-copyable
    FVulkanScopedContext(const FVulkanScopedContext&) = delete;
    FVulkanScopedContext& operator=(const FVulkanScopedContext&) = delete;
    
    /**
     * Get the context
     */
    FVulkanCommandListContext* GetContext() const { return m_context; }
    
    /**
     * Check if context is valid
     */
    bool IsValid() const { return m_context != nullptr; }
    
private:
    FVulkanCommandListContext* m_context;
};

} // namespace MonsterRender::RHI::Vulkan
