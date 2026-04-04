// Copyright Monster Engine. All Rights Reserved.
// FTranslatedCommandBufferCollection - Implementation

#include "RHI/FTranslatedCommandBufferCollection.h"
#include "Platform/Vulkan/VulkanCommandBuffer.h"
#include "Platform/Vulkan/VulkanCommandListContext.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include "Core/Log.h"

namespace MonsterRender::RHI {

using namespace MonsterRender::RHI::Vulkan;

FTranslatedCommandBufferCollection::FTranslatedCommandBufferCollection() {
    MR_LOG_DEBUG("FTranslatedCommandBufferCollection: Created");
}

FTranslatedCommandBufferCollection::~FTranslatedCommandBufferCollection() {
    Reset();
    MR_LOG_DEBUG("FTranslatedCommandBufferCollection: Destroyed");
}

void FTranslatedCommandBufferCollection::AddSecondaryCommandBuffer(
    FVulkanCmdBuffer* cmdBuffer,
    FVulkanCommandListContext* ownerContext,
    uint32 taskIndex,
    uint32 commandCount,
    uint32 drawCallCount) {
    
    if (!cmdBuffer) {
        MR_LOG_ERROR("FTranslatedCommandBufferCollection::AddSecondaryCommandBuffer - "
                   "Null command buffer");
        return;
    }
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Create entry
    FSecondaryCommandBufferEntry entry(cmdBuffer, ownerContext, taskIndex, commandCount, drawCallCount);
    
    // Add to collection
    m_secondaryBuffers.push_back(entry);
    
    // Update statistics
    m_totalCommandCount.fetch_add(commandCount, std::memory_order_relaxed);
    m_totalDrawCallCount.fetch_add(drawCallCount, std::memory_order_relaxed);
    
    MR_LOG_DEBUG("FTranslatedCommandBufferCollection::AddSecondaryCommandBuffer - "
               "Added buffer for task " + std::to_string(taskIndex) + 
               ". Total buffers: " + std::to_string(m_secondaryBuffers.size()) +
               ", Commands: " + std::to_string(commandCount) +
               ", Draws: " + std::to_string(drawCallCount));
}

bool FTranslatedCommandBufferCollection::ExecuteInPrimaryCommandBuffer(FVulkanCmdBuffer* primaryCmdBuffer) {
    if (!primaryCmdBuffer) {
        MR_LOG_ERROR("FTranslatedCommandBufferCollection::ExecuteInPrimaryCommandBuffer - "
                   "Null primary command buffer");
        return false;
    }
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_secondaryBuffers.empty()) {
        MR_LOG_DEBUG("FTranslatedCommandBufferCollection::ExecuteInPrimaryCommandBuffer - "
                   "No secondary buffers to execute");
        return true;
    }
    
    // Build array of Vulkan command buffer handles
    // We need to extract the VkCommandBuffer handles from our wrapper objects
    MonsterEngine::TArray<VkCommandBuffer> vkCommandBuffers;
    vkCommandBuffers.reserve(m_secondaryBuffers.size());
    
    for (const auto& entry : m_secondaryBuffers) {
        if (entry.cmdBuffer && entry.cmdBuffer->getHandle() != VK_NULL_HANDLE) {
            vkCommandBuffers.push_back(entry.cmdBuffer->getHandle());
        } else {
            MR_LOG_WARNING("FTranslatedCommandBufferCollection::ExecuteInPrimaryCommandBuffer - "
                         "Invalid command buffer in collection for task " + 
                         std::to_string(entry.taskIndex));
        }
    }
    
    if (vkCommandBuffers.empty()) {
        MR_LOG_ERROR("FTranslatedCommandBufferCollection::ExecuteInPrimaryCommandBuffer - "
                   "No valid command buffers to execute");
        return false;
    }
    
    // Execute all secondary command buffers in the primary buffer
    // This is the key Vulkan API call for parallel command buffer execution
    // Reference: UE5 FVulkanCommandListContext::RHIExecuteCommandList
    const auto& functions = VulkanAPI::getFunctions();
    functions.vkCmdExecuteCommands(
        primaryCmdBuffer->getHandle(),
        static_cast<uint32>(vkCommandBuffers.size()),
        vkCommandBuffers.data()
    );
    
    // Update execution count
    m_executionCount.fetch_add(1, std::memory_order_relaxed);
    
    MR_LOG_DEBUG("FTranslatedCommandBufferCollection::ExecuteInPrimaryCommandBuffer - "
               "Executed " + std::to_string(vkCommandBuffers.size()) + " secondary buffers. "
               "Total commands: " + std::to_string(m_totalCommandCount.load()) + 
               ", Total draws: " + std::to_string(m_totalDrawCallCount.load()));
    
    return true;
}

void FTranslatedCommandBufferCollection::Reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Free all secondary command buffers back to their owner contexts
    for (auto& entry : m_secondaryBuffers) {
        if (entry.cmdBuffer && entry.ownerContext) {
            entry.ownerContext->FreeSecondaryCommandBuffer(entry.cmdBuffer);
        }
    }
    
    // Clear the collection
    uint32 bufferCount = static_cast<uint32>(m_secondaryBuffers.size());
    m_secondaryBuffers.clear();
    
    // Reset statistics
    m_totalCommandCount.store(0, std::memory_order_relaxed);
    m_totalDrawCallCount.store(0, std::memory_order_relaxed);
    
    MR_LOG_DEBUG("FTranslatedCommandBufferCollection::Reset - "
               "Freed " + std::to_string(bufferCount) + " secondary buffers");
}

} // namespace MonsterRender::RHI
