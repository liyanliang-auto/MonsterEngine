// Copyright Monster Engine. All Rights Reserved.
// Vulkan Async Texture Update Implementation

#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanTexture.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Core/Log.h"
#include "Core/HAL/FMemory.h"

DEFINE_LOG_CATEGORY_STATIC(LogVulkanAsyncTextureUpdate, Log, All);

namespace MonsterRender::RHI::Vulkan {

/**
 * Update texture subresource asynchronously
 * Reference: UE5 FVulkanDynamicRHI::RHIUpdateTexture2D with async support
 */
bool VulkanDevice::updateTextureSubresourceAsync(
    TSharedPtr<IRHITexture> texture,
    uint32 mipLevel,
    const void* data,
    SIZE_T dataSize,
    uint64* outFenceValue)
{
    if (!texture || !data || dataSize == 0) {
        MR_LOG(LogVulkanAsyncTextureUpdate, Error, "Invalid parameters for async texture update");
        return false;
    }
    
    // Cast to Vulkan texture
    VulkanTexture* vulkanTexture = dynamic_cast<VulkanTexture*>(texture.get());
    if (!vulkanTexture) {
        MR_LOG(LogVulkanAsyncTextureUpdate, Error, "Texture is not a Vulkan texture");
        return false;
    }
    
    VkImage image = vulkanTexture->getImage();
    if (image == VK_NULL_HANDLE) {
        MR_LOG(LogVulkanAsyncTextureUpdate, Error, "Invalid Vulkan image handle");
        return false;
    }
    
    const TextureDesc& desc = texture->getDesc();
    
    // Validate mip level
    if (mipLevel >= desc.mipLevels) {
        MR_LOG(LogVulkanAsyncTextureUpdate, Error, "Mip level %u exceeds texture mip count %u", 
               mipLevel, desc.mipLevels);
        return false;
    }
    
    // Calculate mip dimensions
    uint32 mipWidth = std::max(1u, desc.width >> mipLevel);
    uint32 mipHeight = std::max(1u, desc.height >> mipLevel);
    
    MR_LOG(LogVulkanAsyncTextureUpdate, VeryVerbose, "Async updating texture mip %u: %ux%u (%llu bytes)",
           mipLevel, mipWidth, mipHeight, static_cast<uint64>(dataSize));
    
    // Create staging buffer
    BufferDesc stagingDesc;
    stagingDesc.size = dataSize;
    stagingDesc.usage = EResourceUsage::TransferSrc;
    stagingDesc.memoryUsage = EMemoryUsage::Upload;
    stagingDesc.debugName = "AsyncTextureUpdateStagingBuffer";
    
    TSharedPtr<IRHIBuffer> stagingBuffer = createBuffer(stagingDesc);
    if (!stagingBuffer) {
        MR_LOG(LogVulkanAsyncTextureUpdate, Error, "Failed to create staging buffer");
        return false;
    }
    
    // Map and copy data to staging buffer
    void* mappedData = stagingBuffer->map();
    if (!mappedData) {
        MR_LOG(LogVulkanAsyncTextureUpdate, Error, "Failed to map staging buffer");
        return false;
    }
    
    FMemory::Memcpy(mappedData, data, dataSize);
    stagingBuffer->unmap();
    
    // Get Vulkan buffer handle
    VulkanBuffer* vulkanStagingBuffer = dynamic_cast<VulkanBuffer*>(stagingBuffer.get());
    if (!vulkanStagingBuffer) {
        MR_LOG(LogVulkanAsyncTextureUpdate, Error, "Failed to cast staging buffer to Vulkan buffer");
        return false;
    }
    
    VkBuffer vkStagingBuffer = vulkanStagingBuffer->getBuffer();
    VkFormat format = vulkanTexture->getVulkanFormat();
    
    // Begin async upload command buffer
    VkCommandBuffer cmdBuffer = beginAsyncUploadCommands();
    if (cmdBuffer == VK_NULL_HANDLE) {
        MR_LOG(LogVulkanAsyncTextureUpdate, Error, "Failed to begin async upload command buffer");
        return false;
    }
    
    // Transition image layout to TRANSFER_DST_OPTIMAL
    VkImageLayout oldLayout = vulkanTexture->getCurrentLayout();
    
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = mipLevel;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    
    VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    
    if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        barrier.srcAccessMask = 0;
    }
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    
    vkCmdPipelineBarrier(
        cmdBuffer,
        sourceStage, destinationStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
    
    // Copy buffer to image
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    
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
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    
    vkCmdPipelineBarrier(
        cmdBuffer,
        sourceStage, destinationStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
    
    // Update texture's current layout
    vulkanTexture->setCurrentLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    
    // Submit async command buffer and get fence
    VkFence fence = submitAsyncUploadCommands(cmdBuffer);
    if (fence == VK_NULL_HANDLE) {
        MR_LOG(LogVulkanAsyncTextureUpdate, Error, "Failed to submit async upload commands");
        return false;
    }
    
    // Return fence value if requested
    if (outFenceValue) {
        *outFenceValue = reinterpret_cast<uint64>(fence);
    }
    
    MR_LOG(LogVulkanAsyncTextureUpdate, Verbose, "Successfully submitted async texture mip %u upload", mipLevel);
    return true;
}

} // namespace MonsterRender::RHI::Vulkan
