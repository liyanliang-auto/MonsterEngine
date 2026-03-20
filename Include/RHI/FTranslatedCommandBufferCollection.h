// Copyright Monster Engine. All Rights Reserved.
// FTranslatedCommandBufferCollection - Collection and merging of translated secondary command buffers
//
// Reference: UE5 parallel command buffer execution pattern
// Engine/Source/Runtime/RHI/Public/RHICommandList.h

#pragma once

#include "Core/CoreMinimal.h"
#include "Containers/Array.h"
#include <mutex>
#include <atomic>

// Forward declarations
namespace MonsterRender::RHI::Vulkan {
    class FVulkanCmdBuffer;
    class FVulkanCommandListContext;
}

namespace MonsterEngine::RHI {

/**
 * FTranslatedCommandBufferCollection
 * 
 * Collects secondary command buffers from parallel translation tasks
 * and provides methods to execute them in the primary command buffer.
 * 
 * Architecture:
 * - Thread-safe collection of secondary command buffers
 * - Maintains order of command buffer submission
 * - Supports batch execution via vkCmdExecuteCommands
 * - Automatic cleanup and recycling
 * 
 * Usage pattern:
 * ```cpp
 * // Worker threads add their translated buffers
 * collection.AddSecondaryCommandBuffer(cmdBuffer, taskIndex);
 * 
 * // Main thread executes all collected buffers
 * collection.ExecuteInPrimaryCommandBuffer(primaryCmdBuffer);
 * 
 * // Cleanup after execution
 * collection.Reset();
 * ```
 * 
 * Reference: UE5 FRHICommandListImmediate::ExecuteCommandLists
 */
class FTranslatedCommandBufferCollection {
public:
    /**
     * Entry for a translated secondary command buffer
     */
    struct FSecondaryCommandBufferEntry {
        MonsterRender::RHI::Vulkan::FVulkanCmdBuffer* cmdBuffer = nullptr;
        MonsterRender::RHI::Vulkan::FVulkanCommandListContext* ownerContext = nullptr;
        uint32 taskIndex = 0;
        uint32 commandCount = 0;
        uint32 drawCallCount = 0;
        
        FSecondaryCommandBufferEntry() = default;
        
        FSecondaryCommandBufferEntry(
            MonsterRender::RHI::Vulkan::FVulkanCmdBuffer* buffer,
            MonsterRender::RHI::Vulkan::FVulkanCommandListContext* context,
            uint32 index,
            uint32 cmds,
            uint32 draws)
            : cmdBuffer(buffer)
            , ownerContext(context)
            , taskIndex(index)
            , commandCount(cmds)
            , drawCallCount(draws) {
        }
    };
    
    /**
     * Constructor
     */
    FTranslatedCommandBufferCollection();
    
    /**
     * Destructor
     */
    ~FTranslatedCommandBufferCollection();
    
    /**
     * Add a translated secondary command buffer to the collection
     * This is called by worker threads after successful translation
     * 
     * @param cmdBuffer Secondary command buffer to add
     * @param ownerContext Context that owns the buffer (for cleanup)
     * @param taskIndex Index of the translation task
     * @param commandCount Number of commands in the buffer
     * @param drawCallCount Number of draw calls in the buffer
     */
    void AddSecondaryCommandBuffer(
        MonsterRender::RHI::Vulkan::FVulkanCmdBuffer* cmdBuffer,
        MonsterRender::RHI::Vulkan::FVulkanCommandListContext* ownerContext,
        uint32 taskIndex,
        uint32 commandCount,
        uint32 drawCallCount
    );
    
    /**
     * Execute all collected secondary command buffers in the primary command buffer
     * This is called by the main thread after all translations are complete
     * 
     * Uses vkCmdExecuteCommands to execute all secondary buffers in order
     * 
     * @param primaryCmdBuffer Primary command buffer to execute into
     * @return true if execution succeeded, false otherwise
     */
    bool ExecuteInPrimaryCommandBuffer(MonsterRender::RHI::Vulkan::FVulkanCmdBuffer* primaryCmdBuffer);
    
    /**
     * Reset the collection and free all secondary command buffers
     * This is called after execution to prepare for the next frame
     */
    void Reset();
    
    /**
     * Get the number of collected secondary command buffers
     */
    uint32 GetCount() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return static_cast<uint32>(m_secondaryBuffers.size());
    }
    
    /**
     * Get total command count across all buffers
     */
    uint32 GetTotalCommandCount() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_totalCommandCount.load(std::memory_order_relaxed);
    }
    
    /**
     * Get total draw call count across all buffers
     */
    uint32 GetTotalDrawCallCount() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_totalDrawCallCount.load(std::memory_order_relaxed);
    }
    
    /**
     * Check if the collection is empty
     */
    bool IsEmpty() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_secondaryBuffers.empty();
    }
    
private:
    // Thread-safe collection of secondary command buffers
    mutable std::mutex m_mutex;
    MonsterEngine::TArray<FSecondaryCommandBufferEntry> m_secondaryBuffers;
    
    // Statistics
    std::atomic<uint32> m_totalCommandCount{0};
    std::atomic<uint32> m_totalDrawCallCount{0};
    std::atomic<uint32> m_executionCount{0};
};

} // namespace MonsterEngine::RHI
