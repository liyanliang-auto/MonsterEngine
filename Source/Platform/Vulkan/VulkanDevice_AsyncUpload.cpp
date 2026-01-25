// Copyright Monster Engine. All Rights Reserved.
// Vulkan Async Texture Upload Implementation

#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanTexture.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Core/Log.h"
#include "Core/HAL/FMemory.h"

DEFINE_LOG_CATEGORY_STATIC(LogVulkanAsyncUpload, Log, All);

namespace MonsterRender::RHI::Vulkan {

/**
 * Begin async upload command buffer
 * Reference: UE5 FVulkanCommandBufferManager::GetUploadCmdBuffer
 */
VkCommandBuffer VulkanDevice::beginAsyncUploadCommands()
{
    std::lock_guard<std::mutex> lock(m_asyncUploadMutex);
    
    // Create async upload command pool if not exists
    if (m_asyncUploadCommandPool == VK_NULL_HANDLE) {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = m_graphicsQueueFamily.familyIndex;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        
        VkResult result = vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_asyncUploadCommandPool);
        if (result != VK_SUCCESS) {
            MR_LOG(LogVulkanAsyncUpload, Error, "Failed to create async upload command pool: %d", result);
            return VK_NULL_HANDLE;
        }
        
        MR_LOG(LogVulkanAsyncUpload, Log, "Created async upload command pool");
    }
    
    // Allocate command buffer
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_asyncUploadCommandPool;
    allocInfo.commandBufferCount = 1;
    
    VkCommandBuffer commandBuffer;
    VkResult result = vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer);
    if (result != VK_SUCCESS) {
        MR_LOG(LogVulkanAsyncUpload, Error, "Failed to allocate async upload command buffer: %d", result);
        return VK_NULL_HANDLE;
    }
    
    // Begin command buffer
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    result = vkBeginCommandBuffer(commandBuffer, &beginInfo);
    if (result != VK_SUCCESS) {
        MR_LOG(LogVulkanAsyncUpload, Error, "Failed to begin async upload command buffer: %d", result);
        vkFreeCommandBuffers(m_device, m_asyncUploadCommandPool, 1, &commandBuffer);
        return VK_NULL_HANDLE;
    }
    
    // Track command buffer
    m_asyncUploadCommandBuffers.push_back(commandBuffer);
    
    MR_LOG(LogVulkanAsyncUpload, VeryVerbose, "Began async upload command buffer");
    return commandBuffer;
}

/**
 * Submit async upload commands and return fence
 * Reference: UE5 FVulkanCommandBufferManager::SubmitUploadCmdBuffer
 */
VkFence VulkanDevice::submitAsyncUploadCommands(VkCommandBuffer commandBuffer)
{
    if (commandBuffer == VK_NULL_HANDLE) {
        MR_LOG(LogVulkanAsyncUpload, Error, "Cannot submit null command buffer");
        return VK_NULL_HANDLE;
    }
    
    std::lock_guard<std::mutex> lock(m_asyncUploadMutex);
    
    // End command buffer
    VkResult result = vkEndCommandBuffer(commandBuffer);
    if (result != VK_SUCCESS) {
        MR_LOG(LogVulkanAsyncUpload, Error, "Failed to end async upload command buffer: %d", result);
        return VK_NULL_HANDLE;
    }
    
    // Create fence for this upload
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = 0; // Not signaled initially
    
    VkFence fence;
    result = vkCreateFence(m_device, &fenceInfo, nullptr, &fence);
    if (result != VK_SUCCESS) {
        MR_LOG(LogVulkanAsyncUpload, Error, "Failed to create async upload fence: %d", result);
        return VK_NULL_HANDLE;
    }
    
    // Submit command buffer
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    
    result = vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, fence);
    if (result != VK_SUCCESS) {
        MR_LOG(LogVulkanAsyncUpload, Error, "Failed to submit async upload command buffer: %d", result);
        vkDestroyFence(m_device, fence, nullptr);
        return VK_NULL_HANDLE;
    }
    
    // Track fence
    m_asyncUploadFences.push_back(fence);
    
    MR_LOG(LogVulkanAsyncUpload, VeryVerbose, "Submitted async upload command buffer with fence");
    return fence;
}

/**
 * Check if async upload is complete
 */
bool VulkanDevice::isAsyncUploadComplete(VkFence fence)
{
    if (fence == VK_NULL_HANDLE) {
        return true;
    }
    
    VkResult result = vkGetFenceStatus(m_device, fence);
    return (result == VK_SUCCESS);
}

/**
 * Wait for async upload to complete
 */
void VulkanDevice::waitForAsyncUpload(VkFence fence)
{
    if (fence == VK_NULL_HANDLE) {
        return;
    }
    
    VkResult result = vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
        MR_LOG(LogVulkanAsyncUpload, Error, "Failed to wait for async upload fence: %d", result);
    }
}

/**
 * Destroy async upload fence
 */
void VulkanDevice::destroyAsyncUploadFence(VkFence fence)
{
    if (fence == VK_NULL_HANDLE) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(m_asyncUploadMutex);
    
    // Wait for fence to complete before destroying
    vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX);
    
    // Destroy fence
    vkDestroyFence(m_device, fence, nullptr);
    
    // Remove from tracking
    for (size_t i = 0; i < m_asyncUploadFences.size(); ++i) {
        if (m_asyncUploadFences[i] == fence) {
            m_asyncUploadFences.erase(m_asyncUploadFences.begin() + i);
            break;
        }
    }
}

} // namespace MonsterRender::RHI::Vulkan
