// Copyright Monster Engine. All Rights Reserved.
// Vulkan Texture Update Implementation

#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanTexture.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Core/Log.h"
#include "Core/HAL/FMemory.h"

DEFINE_LOG_CATEGORY_STATIC(LogVulkanTextureUpdate, Log, All);

namespace MonsterRender::RHI::Vulkan {

/**
 * Update texture subresource data
 * Reference: UE5 FVulkanDynamicRHI::RHIUpdateTexture2D
 */
bool VulkanDevice::updateTextureSubresource(
    TSharedPtr<IRHITexture> texture,
    uint32 mipLevel,
    const void* data,
    SIZE_T dataSize)
{
    if (!texture || !data || dataSize == 0) {
        MR_LOG(LogVulkanTextureUpdate, Error, "Invalid parameters for texture update");
        return false;
    }
    
    // Cast to Vulkan texture
    VulkanTexture* vulkanTexture = dynamic_cast<VulkanTexture*>(texture.get());
    if (!vulkanTexture) {
        MR_LOG(LogVulkanTextureUpdate, Error, "Texture is not a Vulkan texture");
        return false;
    }
    
    VkImage image = vulkanTexture->getImage();
    if (image == VK_NULL_HANDLE) {
        MR_LOG(LogVulkanTextureUpdate, Error, "Invalid Vulkan image handle");
        return false;
    }
    
    const TextureDesc& desc = texture->getDesc();
    
    // Validate mip level
    if (mipLevel >= desc.mipLevels) {
        MR_LOG(LogVulkanTextureUpdate, Error, "Mip level %u exceeds texture mip count %u", 
               mipLevel, desc.mipLevels);
        return false;
    }
    
    // Calculate mip dimensions
    uint32 mipWidth = std::max(1u, desc.width >> mipLevel);
    uint32 mipHeight = std::max(1u, desc.height >> mipLevel);
    
    MR_LOG(LogVulkanTextureUpdate, VeryVerbose, "Updating texture mip %u: %ux%u (%llu bytes)",
           mipLevel, mipWidth, mipHeight, static_cast<uint64>(dataSize));
    
    // Create staging buffer (UE5-style)
    BufferDesc stagingDesc;
    stagingDesc.size = dataSize;
    stagingDesc.usage = EResourceUsage::TransferSrc;
    stagingDesc.memoryUsage = EMemoryUsage::Upload;
    stagingDesc.debugName = "TextureUpdateStagingBuffer";
    
    TSharedPtr<IRHIBuffer> stagingBuffer = createBuffer(stagingDesc);
    if (!stagingBuffer) {
        MR_LOG(LogVulkanTextureUpdate, Error, "Failed to create staging buffer");
        return false;
    }
    
    // Map and copy data to staging buffer
    void* mappedData = stagingBuffer->map();
    if (!mappedData) {
        MR_LOG(LogVulkanTextureUpdate, Error, "Failed to map staging buffer");
        return false;
    }
    
    FMemory::Memcpy(mappedData, data, dataSize);
    stagingBuffer->unmap();
    
    // Get Vulkan buffer handle
    VulkanBuffer* vulkanStagingBuffer = dynamic_cast<VulkanBuffer*>(stagingBuffer.get());
    if (!vulkanStagingBuffer) {
        MR_LOG(LogVulkanTextureUpdate, Error, "Failed to cast staging buffer to Vulkan buffer");
        return false;
    }
    
    VkBuffer vkStagingBuffer = vulkanStagingBuffer->getBuffer();
    VkFormat format = vulkanTexture->getVulkanFormat();
    
    // Begin single-time command buffer
    VkCommandBuffer cmdBuffer = beginSingleTimeCommands();
    if (cmdBuffer == VK_NULL_HANDLE) {
        MR_LOG(LogVulkanTextureUpdate, Error, "Failed to begin command buffer");
        return false;
    }
    
    // Transition image layout to TRANSFER_DST_OPTIMAL
    VkImageLayout oldLayout = vulkanTexture->getCurrentLayout();
    if (!transitionImageLayout(image, format, oldLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
                               mipLevel, 1)) {
        MR_LOG(LogVulkanTextureUpdate, Error, "Failed to transition image layout to TRANSFER_DST");
        endSingleTimeCommands(cmdBuffer);
        return false;
    }
    
    // Copy buffer to image
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;   // Tightly packed
    region.bufferImageHeight = 0; // Tightly packed
    
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = mipLevel;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {mipWidth, mipHeight, 1};
    
    vkCmdCopyBufferToImage(
        cmdBuffer,
        vkStagingBuffer,
        image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );
    
    // Transition image layout to SHADER_READ_ONLY_OPTIMAL
    if (!transitionImageLayout(image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mipLevel, 1)) {
        MR_LOG(LogVulkanTextureUpdate, Error, "Failed to transition image layout to SHADER_READ");
        endSingleTimeCommands(cmdBuffer);
        return false;
    }
    
    // Update texture's current layout
    vulkanTexture->setCurrentLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    
    // End and submit command buffer
    endSingleTimeCommands(cmdBuffer);
    
    MR_LOG(LogVulkanTextureUpdate, Verbose, "Successfully updated texture mip %u", mipLevel);
    return true;
}

/**
 * Transition image layout
 * Reference: UE5 FVulkanCommandListContext::RHITransitionResources
 */
bool VulkanDevice::transitionImageLayout(
    VkImage image,
    VkFormat format,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    uint32 mipLevel,
    uint32 mipLevelCount)
{
    VkCommandBuffer cmdBuffer = beginSingleTimeCommands();
    if (cmdBuffer == VK_NULL_HANDLE) {
        return false;
    }
    
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = mipLevel;
    barrier.subresourceRange.levelCount = mipLevelCount;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    
    // Determine pipeline stages and access masks based on layouts
    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;
    
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && 
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        // Initial transition for upload
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && 
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        // Transition after upload for shader reading
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        // Transition from shader read to transfer for update
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else {
        // Generic transition
        barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }
    
    vkCmdPipelineBarrier(
        cmdBuffer,
        sourceStage, destinationStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
    
    endSingleTimeCommands(cmdBuffer);
    return true;
}

/**
 * Begin single-time command buffer for immediate operations
 * Reference: UE5 FVulkanCommandBufferManager::GetUploadCmdBuffer
 */
VkCommandBuffer VulkanDevice::beginSingleTimeCommands()
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_commandPool;
    allocInfo.commandBufferCount = 1;
    
    VkCommandBuffer commandBuffer;
    VkResult result = vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer);
    if (result != VK_SUCCESS) {
        MR_LOG(LogVulkanTextureUpdate, Error, "Failed to allocate command buffer: %d", result);
        return VK_NULL_HANDLE;
    }
    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    result = vkBeginCommandBuffer(commandBuffer, &beginInfo);
    if (result != VK_SUCCESS) {
        MR_LOG(LogVulkanTextureUpdate, Error, "Failed to begin command buffer: %d", result);
        vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
        return VK_NULL_HANDLE;
    }
    
    return commandBuffer;
}

/**
 * End and submit single-time command buffer
 * Reference: UE5 FVulkanCommandBufferManager::SubmitUploadCmdBuffer
 */
void VulkanDevice::endSingleTimeCommands(VkCommandBuffer commandBuffer)
{
    if (commandBuffer == VK_NULL_HANDLE) {
        return;
    }
    
    VkResult result = vkEndCommandBuffer(commandBuffer);
    if (result != VK_SUCCESS) {
        MR_LOG(LogVulkanTextureUpdate, Error, "Failed to end command buffer: %d", result);
        vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
        return;
    }
    
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    
    result = vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    if (result != VK_SUCCESS) {
        MR_LOG(LogVulkanTextureUpdate, Error, "Failed to submit command buffer: %d", result);
    }
    
    // Wait for completion (synchronous operation)
    vkQueueWaitIdle(m_graphicsQueue);
    
    // Free command buffer
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
}

} // namespace MonsterRender::RHI::Vulkan
