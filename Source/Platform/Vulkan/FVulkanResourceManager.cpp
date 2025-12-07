// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Vulkan Resource Manager Implementation
//
// Reference UE5: Engine/Source/Runtime/VulkanRHI/Private/VulkanResources.cpp

#include "Platform/Vulkan/FVulkanResourceManager.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Core/HAL/FMemory.h"
#include "Core/Log.h"
#include <algorithm>

namespace MonsterRender {
namespace RHI {

using namespace Vulkan;

// ============================================================================
// FVulkanResourceMultiBuffer Implementation
// ============================================================================

FVulkanResourceMultiBuffer::FVulkanResourceMultiBuffer(
    VulkanDevice* InDevice,
    uint32 InSize,
    EResourceUsage InUsage,
    VkMemoryPropertyFlags InMemoryFlags,
    uint32 InNumBuffers)
    : FRHIBuffer(InSize, InUsage, 0)
    , Device(InDevice)
    , MemoryFlags(InMemoryFlags)
    , NumBuffers(InNumBuffers)
    , CurrentBufferIndex(0)
{
    Buffers.resize(NumBuffers);
}

FVulkanResourceMultiBuffer::~FVulkanResourceMultiBuffer()
{
    Destroy();
}

bool FVulkanResourceMultiBuffer::Initialize()
{
    const auto& functions = VulkanAPI::getFunctions();
    
    // Convert RHI Usage to Vulkan Usage
    VkBufferUsageFlags vulkanUsage = 0;
    if ((uint32)Usage & (uint32)EResourceUsage::VertexBuffer)
        vulkanUsage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if ((uint32)Usage & (uint32)EResourceUsage::IndexBuffer)
        vulkanUsage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if ((uint32)Usage & (uint32)EResourceUsage::UniformBuffer)
        vulkanUsage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if ((uint32)Usage & (uint32)EResourceUsage::StorageBuffer)
        vulkanUsage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if ((uint32)Usage & (uint32)EResourceUsage::TransferSrc)
        vulkanUsage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if ((uint32)Usage & (uint32)EResourceUsage::TransferDst)
        vulkanUsage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    
    // Create all buffer instances
    for (uint32 i = 0; i < NumBuffers; ++i) {
        auto& instance = Buffers[i];
        
        // Create VkBuffer
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = Size;
        bufferInfo.usage = vulkanUsage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        VkResult result = functions.vkCreateBuffer(Device->getLogicalDevice(), &bufferInfo, nullptr, &instance.Buffer);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("FVulkanResourceMultiBuffer: Failed to create VkBuffer #" + std::to_string(i) + 
                        ", VkResult: " + std::to_string(result));
            // Cleanup already created buffers
            for (uint32 j = 0; j < i; ++j) {
                functions.vkDestroyBuffer(Device->getLogicalDevice(), Buffers[j].Buffer, nullptr);
            }
            return false;
        }
        
        // Query memory requirements
        VkMemoryRequirements memReqs;
        functions.vkGetBufferMemoryRequirements(Device->getLogicalDevice(), instance.Buffer, &memReqs);
        
        // Allocate memory from memory manager
        FVulkanMemoryManager* memMgr = Device->GetMemoryManager();
        FVulkanMemoryManager::FAllocationRequest request{};
        request.Size = memReqs.size;
        request.Alignment = memReqs.alignment;
        request.MemoryTypeBits = memReqs.memoryTypeBits;
        request.RequiredFlags = MemoryFlags;
        
        if (!memMgr->Allocate(request, instance.Allocation)) {
            MR_LOG_ERROR("FVulkanResourceMultiBuffer: Memory allocation failed for buffer #" + std::to_string(i));
            // Cleanup
            functions.vkDestroyBuffer(Device->getLogicalDevice(), instance.Buffer, nullptr);
            for (uint32 j = 0; j < i; ++j) {
                functions.vkDestroyBuffer(Device->getLogicalDevice(), Buffers[j].Buffer, nullptr);
                memMgr->Free(Buffers[j].Allocation);
            }
            return false;
        }
        
        // Bind memory
        result = functions.vkBindBufferMemory(Device->getLogicalDevice(), instance.Buffer,
                                              instance.Allocation.DeviceMemory, instance.Allocation.Offset);
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("FVulkanResourceMultiBuffer: Failed to bind memory for buffer #" + std::to_string(i) +
                        ", VkResult: " + std::to_string(result));
            // Cleanup
            memMgr->Free(instance.Allocation);
            functions.vkDestroyBuffer(Device->getLogicalDevice(), instance.Buffer, nullptr);
            for (uint32 j = 0; j < i; ++j) {
                functions.vkDestroyBuffer(Device->getLogicalDevice(), Buffers[j].Buffer, nullptr);
                memMgr->Free(Buffers[j].Allocation);
            }
            return false;
        }
        
        // If persistent mapped, map now
        if (instance.Allocation.bMapped && instance.Allocation.MappedPointer) {
            instance.MappedPtr = instance.Allocation.MappedPointer;
        }
    }
    
    MR_LOG_DEBUG("FVulkanResourceMultiBuffer: Created " + std::to_string(NumBuffers) + " buffers (" +
                 std::to_string(Size / 1024) + "KB each)");
    
    return true;
}

void FVulkanResourceMultiBuffer::Destroy()
{
    const auto& functions = VulkanAPI::getFunctions();
    FVulkanMemoryManager* memMgr = Device->GetMemoryManager();
    
    for (auto& instance : Buffers) {
        if (instance.Buffer != VK_NULL_HANDLE) {
            // Unmap if needed
            if (instance.MappedPtr && !instance.Allocation.bMapped) {
                memMgr->UnmapMemory(instance.Allocation);
                instance.MappedPtr = nullptr;
            }
            
            // Destroy buffer
            functions.vkDestroyBuffer(Device->getLogicalDevice(), instance.Buffer, nullptr);
            instance.Buffer = VK_NULL_HANDLE;
            
            // Free memory
            memMgr->Free(instance.Allocation);
        }
    }
    
    Buffers.clear();
    
    MR_LOG_DEBUG("FVulkanResourceMultiBuffer: Destroyed " + std::to_string(NumBuffers) + " buffers");
}

void* FVulkanResourceMultiBuffer::Lock(uint32 Offset, uint32 InSize)
{
    std::lock_guard<std::mutex> lock(Mutex);
    
    auto& current = Buffers[CurrentBufferIndex];
    
    // If already persistent mapped, return offset pointer
    if (current.Allocation.bMapped && current.MappedPtr) {
        return static_cast<uint8*>(current.MappedPtr) + Offset;
    }
    
    // On-demand mapping
    if (!current.MappedPtr) {
        FVulkanMemoryManager* memMgr = Device->GetMemoryManager();
        if (!memMgr->MapMemory(current.Allocation, &current.MappedPtr)) {
            MR_LOG_ERROR("FVulkanResourceMultiBuffer::Lock: Failed to map buffer #" + 
                        std::to_string(CurrentBufferIndex));
            return nullptr;
        }
    }
    
    return static_cast<uint8*>(current.MappedPtr) + Offset;
}

void FVulkanResourceMultiBuffer::Unlock()
{
    std::lock_guard<std::mutex> lock(Mutex);
    
    auto& current = Buffers[CurrentBufferIndex];
    
    // Don't unmap if persistent
    if (current.Allocation.bMapped) {
        return;
    }
    
    if (current.MappedPtr) {
        FVulkanMemoryManager* memMgr = Device->GetMemoryManager();
        memMgr->UnmapMemory(current.Allocation);
        current.MappedPtr = nullptr;
    }
}

uint64 FVulkanResourceMultiBuffer::GetGPUVirtualAddress() const
{
    // Vulkan doesn't directly provide GPU virtual address (requires VK_EXT_buffer_device_address)
    return 0;
}

void FVulkanResourceMultiBuffer::AdvanceFrame()
{
    std::lock_guard<std::mutex> lock(Mutex);
    CurrentBufferIndex = (CurrentBufferIndex + 1) % NumBuffers;
}

VkBuffer FVulkanResourceMultiBuffer::GetCurrentHandle() const
{
    return Buffers[CurrentBufferIndex].Buffer;
}

const FVulkanAllocation& FVulkanResourceMultiBuffer::GetCurrentAllocation() const
{
    return Buffers[CurrentBufferIndex].Allocation;
}

VkBuffer FVulkanResourceMultiBuffer::GetHandle(uint32 Index) const
{
    if (Index < NumBuffers) {
        return Buffers[Index].Buffer;
    }
    return VK_NULL_HANDLE;
}

// ============================================================================
// FVulkanTexture Implementation
// ============================================================================

FVulkanTexture::FVulkanTexture(
    VulkanDevice* InDevice,
    const TextureDesc& InDesc,
    VkMemoryPropertyFlags InMemoryFlags)
    : FRHITexture(InDesc)
    , Device(InDevice)
    , Image(VK_NULL_HANDLE)
    , ImageView(VK_NULL_HANDLE)
    , MemoryFlags(InMemoryFlags)
    , CurrentLayout(VK_IMAGE_LAYOUT_UNDEFINED)
{
}

FVulkanTexture::~FVulkanTexture()
{
    Destroy();
}

bool FVulkanTexture::Initialize()
{
    const auto& functions = VulkanAPI::getFunctions();
    
    // Convert RHI Format to Vulkan Format (simplified)
    VkFormat vulkanFormat = VK_FORMAT_R8G8B8A8_UNORM;  // Default
    // TODO: Complete format mapping
    
    // Determine image type
    VkImageType imageType = VK_IMAGE_TYPE_2D;
    if (Desc.depth > 1) {
        imageType = VK_IMAGE_TYPE_3D;
    } else if (Desc.height == 1) {
        imageType = VK_IMAGE_TYPE_1D;
    }
    
    // Convert Usage
    VkImageUsageFlags vulkanUsage = 0;
    if ((uint32)Desc.usage & (uint32)EResourceUsage::RenderTarget)
        vulkanUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if ((uint32)Desc.usage & (uint32)EResourceUsage::DepthStencil)
        vulkanUsage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if ((uint32)Desc.usage & (uint32)EResourceUsage::ShaderResource)
        vulkanUsage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if ((uint32)Desc.usage & (uint32)EResourceUsage::UnorderedAccess)
        vulkanUsage |= VK_IMAGE_USAGE_STORAGE_BIT;
    if ((uint32)Desc.usage & (uint32)EResourceUsage::TransferSrc)
        vulkanUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if ((uint32)Desc.usage & (uint32)EResourceUsage::TransferDst)
        vulkanUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    
    // Create VkImage
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = imageType;
    imageInfo.format = vulkanFormat;
    imageInfo.extent.width = Desc.width;
    imageInfo.extent.height = Desc.height;
    imageInfo.extent.depth = Desc.depth;
    imageInfo.mipLevels = Desc.mipLevels;
    imageInfo.arrayLayers = Desc.arraySize;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = vulkanUsage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    // Cube texture requires special flag
    if (Desc.arraySize == 6) {
        imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }
    
    VkResult result = functions.vkCreateImage(Device->getLogicalDevice(), &imageInfo, nullptr, &Image);
    if (result != VK_SUCCESS) {
        MR_LOG_ERROR("FVulkanTexture: Failed to create VkImage, VkResult: " + std::to_string(result));
        return false;
    }
    
    // Query memory requirements
    VkMemoryRequirements memReqs;
    functions.vkGetImageMemoryRequirements(Device->getLogicalDevice(), Image, &memReqs);
    
    // Allocate memory
    FVulkanMemoryManager* memMgr = Device->GetMemoryManager();
    FVulkanMemoryManager::FAllocationRequest request{};
    request.Size = memReqs.size;
    request.Alignment = memReqs.alignment;
    request.MemoryTypeBits = memReqs.memoryTypeBits;
    request.RequiredFlags = MemoryFlags;
    
    if (!memMgr->Allocate(request, Allocation)) {
        MR_LOG_ERROR("FVulkanTexture: Memory allocation failed");
        functions.vkDestroyImage(Device->getLogicalDevice(), Image, nullptr);
        Image = VK_NULL_HANDLE;
        return false;
    }
    
    // Bind memory
    result = functions.vkBindImageMemory(Device->getLogicalDevice(), Image,
                                         Allocation.DeviceMemory, Allocation.Offset);
    if (result != VK_SUCCESS) {
        MR_LOG_ERROR("FVulkanTexture: Failed to bind memory, VkResult: " + std::to_string(result));
        memMgr->Free(Allocation);
        functions.vkDestroyImage(Device->getLogicalDevice(), Image, nullptr);
        Image = VK_NULL_HANDLE;
        return false;
    }
    
    // Create Image View
    if (!CreateImageView()) {
        memMgr->Free(Allocation);
        functions.vkDestroyImage(Device->getLogicalDevice(), Image, nullptr);
        Image = VK_NULL_HANDLE;
        return false;
    }
    
    MR_LOG_DEBUG("FVulkanTexture: Created successfully (" + std::to_string(Desc.width) + "x" +
                 std::to_string(Desc.height) + ", " +
                 std::to_string(Allocation.Size / (1024 * 1024)) + "MB, " +
                 (Allocation.bDedicated ? "dedicated" : "sub-allocated") + ")");
    
    return true;
}

bool FVulkanTexture::CreateImageView()
{
    const auto& functions = VulkanAPI::getFunctions();
    
    VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
    if (Desc.arraySize == 6) {
        viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    } else if (Desc.depth > 1) {
        viewType = VK_IMAGE_VIEW_TYPE_3D;
    } else if (Desc.height == 1) {
        viewType = VK_IMAGE_VIEW_TYPE_1D;
    } else if (Desc.arraySize > 1) {
        viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    }
    
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = Image;
    viewInfo.viewType = viewType;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;  // TODO: Map from Desc.format
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = Desc.mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = Desc.arraySize;
    
    VkResult result = functions.vkCreateImageView(Device->getLogicalDevice(), &viewInfo, nullptr, &ImageView);
    if (result != VK_SUCCESS) {
        MR_LOG_ERROR("FVulkanTexture: Failed to create ImageView, VkResult: " + std::to_string(result));
        return false;
    }
    
    return true;
}

void FVulkanTexture::Destroy()
{
    if (Image == VK_NULL_HANDLE) {
        return;
    }
    
    const auto& functions = VulkanAPI::getFunctions();
    
    // Destroy ImageView
    if (ImageView != VK_NULL_HANDLE) {
        functions.vkDestroyImageView(Device->getLogicalDevice(), ImageView, nullptr);
        ImageView = VK_NULL_HANDLE;
    }
    
    // Destroy Image
    functions.vkDestroyImage(Device->getLogicalDevice(), Image, nullptr);
    Image = VK_NULL_HANDLE;
    
    // Free memory
    FVulkanMemoryManager* memMgr = Device->GetMemoryManager();
    memMgr->Free(Allocation);
    
    MR_LOG_DEBUG("FVulkanTexture: Destroyed (" + std::to_string(Desc.width) + "x" +
                 std::to_string(Desc.height) + ")");
}

// ============================================================================
// FVulkanResourceManager Implementation
// ============================================================================

FVulkanResourceManager::FVulkanResourceManager(VulkanDevice* InDevice, FVulkanMemoryManager* InMemoryManager)
    : Device(InDevice)
    , MemoryManager(InMemoryManager)
    , TotalBufferCount(0)
    , TotalMultiBufferCount(0)
    , TotalTextureCount(0)
    , TotalBufferMemory(0)
    , TotalTextureMemory(0)
{
    MR_LOG_INFO("FVulkanResourceManager: Initialized");
}

FVulkanResourceManager::~FVulkanResourceManager()
{
    ReleaseUnusedResources();
    
    MR_LOG_INFO("FVulkanResourceManager: Destroyed");
    MR_LOG_INFO("  Total buffers: " + std::to_string(TotalBufferCount.load()));
    MR_LOG_INFO("  Total multi-buffers: " + std::to_string(TotalMultiBufferCount.load()));
    MR_LOG_INFO("  Total textures: " + std::to_string(TotalTextureCount.load()));
}

FRHIBufferRef FVulkanResourceManager::CreateBuffer(
    uint32 Size,
    EResourceUsage Usage,
    VkMemoryPropertyFlags MemoryFlags,
    uint32 Stride)
{
    // TODO: Implement FRHIBuffer creation
    // For now, use CreateMultiBuffer for dynamic buffers
    MR_LOG_WARNING("FVulkanResourceManager::CreateBuffer: Not yet implemented, use CreateMultiBuffer instead");
    return nullptr;
}

TRefCountPtr<FVulkanResourceMultiBuffer> FVulkanResourceManager::CreateMultiBuffer(
    uint32 Size,
    EResourceUsage Usage,
    VkMemoryPropertyFlags MemoryFlags,
    uint32 NumBuffers)
{
    FVulkanResourceMultiBuffer* multiBuffer = FMemory::New<FVulkanResourceMultiBuffer>(
        Device, Size, Usage, MemoryFlags, NumBuffers
    );
    
    if (!multiBuffer->Initialize()) {
        MR_LOG_ERROR("FVulkanResourceManager: Multi-buffer initialization failed");
        FMemory::Delete(multiBuffer);
        return nullptr;
    }
    
    // Track
    {
        std::lock_guard<std::mutex> lock(ResourcesMutex);
        ActiveMultiBuffers.Add(multiBuffer);
    }
    
    TotalMultiBufferCount.fetch_add(1);
    TotalBufferMemory.fetch_add(Size * NumBuffers);
    
    return TRefCountPtr<FVulkanResourceMultiBuffer>(multiBuffer);
}

FRHITextureRef FVulkanResourceManager::CreateTexture(
    const TextureDesc& Desc,
    VkMemoryPropertyFlags MemoryFlags)
{
    FVulkanTexture* texture = FMemory::New<FVulkanTexture>(Device, Desc, MemoryFlags);
    
    if (!texture->Initialize()) {
        MR_LOG_ERROR("FVulkanResourceManager: Texture initialization failed");
        FMemory::Delete(texture);
        return nullptr;
    }
    
    // Track
    {
        std::lock_guard<std::mutex> lock(ResourcesMutex);
        ActiveTextures.Add(texture);
    }
    
    TotalTextureCount.fetch_add(1);
    // Texture size from Allocation
    TotalTextureMemory.fetch_add(texture->GetAllocation().Size);
    
    return FRHITextureRef(texture);
}

void FVulkanResourceManager::DeferredRelease(FRHIResource* Resource, uint64 FrameNumber)
{
    std::lock_guard<std::mutex> lock(DeferredReleasesMutex);
    DeferredReleases.PushBack(FDeferredReleaseEntry(Resource, FrameNumber));
}

void FVulkanResourceManager::ProcessDeferredReleases(uint64 CompletedFrameNumber)
{
    std::lock_guard<std::mutex> lock(DeferredReleasesMutex);
    
    while (!DeferredReleases.IsEmpty()) {
        auto& entry = DeferredReleases.First();
        
        // If resource submission frame completed + DEFERRED_RELEASE_FRAMES, safe to release
        if (CompletedFrameNumber >= entry.FrameNumber + DEFERRED_RELEASE_FRAMES) {
            entry.Resource->Release();
            DeferredReleases.PopFront();
        } else {
            // Queue is sorted by frame number, so we can break here
            break;
        }
    }
}

void FVulkanResourceManager::AdvanceFrame()
{
    std::lock_guard<std::mutex> lock(ResourcesMutex);
    
    // Advance all multi-buffers
    for (auto* multiBuffer : ActiveMultiBuffers) {
        multiBuffer->AdvanceFrame();
    }
}

void FVulkanResourceManager::GetResourceStats(FResourceStats& OutStats)
{
    OutStats = FResourceStats{};
    
    {
        std::lock_guard<std::mutex> lock(ResourcesMutex);
        OutStats.NumBuffers = static_cast<uint32>(ActiveBuffers.Num());
        OutStats.NumMultiBuffers = static_cast<uint32>(ActiveMultiBuffers.Num());
        OutStats.NumTextures = static_cast<uint32>(ActiveTextures.Num());
    }
    
    {
        std::lock_guard<std::mutex> lock(DeferredReleasesMutex);
        OutStats.PendingReleases = static_cast<uint32>(DeferredReleases.Num());
    }
    
    OutStats.BufferMemory = TotalBufferMemory.load();
    OutStats.TextureMemory = TotalTextureMemory.load();
    
    MR_LOG_DEBUG("===== FVulkanResourceManager Stats =====");
    MR_LOG_DEBUG("  Buffers: " + std::to_string(OutStats.NumBuffers) + 
                 " (" + std::to_string(OutStats.BufferMemory / (1024 * 1024)) + "MB)");
    MR_LOG_DEBUG("  Multi-Buffers: " + std::to_string(OutStats.NumMultiBuffers));
    MR_LOG_DEBUG("  Textures: " + std::to_string(OutStats.NumTextures) +
                 " (" + std::to_string(OutStats.TextureMemory / (1024 * 1024)) + "MB)");
    MR_LOG_DEBUG("  Pending Releases: " + std::to_string(OutStats.PendingReleases));
    MR_LOG_DEBUG("=======================================");
}

void FVulkanResourceManager::ReleaseUnusedResources()
{
    // Release all deferred resources immediately
    {
        std::lock_guard<std::mutex> lock(DeferredReleasesMutex);
        for (auto& entry : DeferredReleases) {
            entry.Resource->Release();
        }
        DeferredReleases.clear();
    }
    
    // Clear active resource lists (resources already managed by reference counting)
    {
        std::lock_guard<std::mutex> lock(ResourcesMutex);
        ActiveBuffers.clear();
        ActiveMultiBuffers.clear();
        ActiveTextures.clear();
    }
}

} // namespace RHI
} // namespace MonsterRender

