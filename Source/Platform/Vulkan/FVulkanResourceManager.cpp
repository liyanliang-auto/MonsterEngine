// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Vulkan Resource Manager Implementation
//
// 参考 UE5: Engine/Source/Runtime/VulkanRHI/Private/VulkanResources.cpp

#include "Platform/Vulkan/FVulkanResourceManager.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include "Core/Log.h"
#include <algorithm>

namespace MonsterRender {
namespace RHI {

using namespace Vulkan;

// ============================================================================
// FVulkanBuffer Implementation (Vulkan 缓冲区实现)
// ============================================================================

FVulkanBuffer::FVulkanBuffer(VulkanDevice* InDevice, uint32 InSize, EResourceUsage InUsage,
                             VkMemoryPropertyFlags InMemoryFlags, uint32 InStride)
    : FRHIBuffer(InSize, InUsage, InStride)
    , Device(InDevice)
    , Buffer(VK_NULL_HANDLE)
    , MemoryFlags(InMemoryFlags)
    , MappedPtr(nullptr)
    , bPersistentMapped(false)
{
}

FVulkanBuffer::~FVulkanBuffer()
{
    Destroy();
}

bool FVulkanBuffer::Initialize()
{
    const auto& functions = VulkanAPI::getFunctions();
    
    // 转换 RHI Usage 到 Vulkan Usage
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
    
    // 创建 VkBuffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = Size;
    bufferInfo.usage = vulkanUsage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    VkResult result = functions.vkCreateBuffer(Device->getLogicalDevice(), &bufferInfo, nullptr, &Buffer);
    if (result != VK_SUCCESS) {
        MR_LOG_ERROR("FVulkanBuffer: 创建 VkBuffer 失败，VkResult: " + std::to_string(result));
        return false;
    }
    
    // 查询内存需求
    VkMemoryRequirements memReqs;
    functions.vkGetBufferMemoryRequirements(Device->getLogicalDevice(), Buffer, &memReqs);
    
    // 从内存管理器分配内存
    FVulkanMemoryManager* memMgr = Device->GetMemoryManager();
    FVulkanMemoryManager::FAllocationRequest request{};
    request.Size = memReqs.size;
    request.Alignment = memReqs.alignment;
    request.MemoryTypeBits = memReqs.memoryTypeBits;
    request.RequiredFlags = MemoryFlags;
    
    if (!memMgr->Allocate(request, Allocation)) {
        MR_LOG_ERROR("FVulkanBuffer: 内存分配失败");
        functions.vkDestroyBuffer(Device->getLogicalDevice(), Buffer, nullptr);
        Buffer = VK_NULL_HANDLE;
        return false;
    }
    
    // 绑定内存
    result = functions.vkBindBufferMemory(Device->getLogicalDevice(), Buffer,
                                          Allocation.DeviceMemory, Allocation.Offset);
    if (result != VK_SUCCESS) {
        MR_LOG_ERROR("FVulkanBuffer: 绑定内存失败，VkResult: " + std::to_string(result));
        memMgr->Free(Allocation);
        functions.vkDestroyBuffer(Device->getLogicalDevice(), Buffer, nullptr);
        Buffer = VK_NULL_HANDLE;
        return false;
    }
    
    // 如果是持久映射，自动映射
    if (Allocation.bMapped && Allocation.MappedPointer) {
        MappedPtr = Allocation.MappedPointer;
        bPersistentMapped = true;
    }
    
    MR_LOG_DEBUG("FVulkanBuffer: 创建成功 (" + std::to_string(Size / 1024) + "KB, " +
                 (Allocation.bDedicated ? "独立" : "子分配") + ")");
    
    return true;
}

void FVulkanBuffer::Destroy()
{
    if (Buffer == VK_NULL_HANDLE) {
        return;
    }
    
    const auto& functions = VulkanAPI::getFunctions();
    
    // 取消映射
    if (MappedPtr && !bPersistentMapped) {
        FVulkanMemoryManager* memMgr = Device->GetMemoryManager();
        memMgr->UnmapMemory(Allocation);
        MappedPtr = nullptr;
    }
    
    // 销毁 Buffer
    functions.vkDestroyBuffer(Device->getLogicalDevice(), Buffer, nullptr);
    Buffer = VK_NULL_HANDLE;
    
    // 释放内存
    FVulkanMemoryManager* memMgr = Device->GetMemoryManager();
    memMgr->Free(Allocation);
    
    MR_LOG_DEBUG("FVulkanBuffer: 销毁 (" + std::to_string(Size / 1024) + "KB)");
}

void* FVulkanBuffer::Map(uint32 Offset, uint32 MapSize)
{
    // 如果已经持久映射，直接返回偏移后的指针
    if (bPersistentMapped && MappedPtr) {
        return static_cast<uint8*>(MappedPtr) + Offset;
    }
    
    // 按需映射
    if (!MappedPtr) {
        FVulkanMemoryManager* memMgr = Device->GetMemoryManager();
        if (!memMgr->MapMemory(Allocation, &MappedPtr)) {
            MR_LOG_ERROR("FVulkanBuffer::Map: 映射失败");
            return nullptr;
        }
    }
    
    return static_cast<uint8*>(MappedPtr) + Offset;
}

void FVulkanBuffer::Unmap()
{
    // 持久映射不需要 unmap
    if (bPersistentMapped) {
        return;
    }
    
    if (MappedPtr) {
        FVulkanMemoryManager* memMgr = Device->GetMemoryManager();
        memMgr->UnmapMemory(Allocation);
        MappedPtr = nullptr;
    }
}

uint64 FVulkanBuffer::GetGPUVirtualAddress() const
{
    // Vulkan 不直接提供 GPU 虚拟地址（需要 VK_EXT_buffer_device_address）
    return 0;
}

// ============================================================================
// FVulkanTexture Implementation (Vulkan 纹理实现)
// ============================================================================

FVulkanTexture::FVulkanTexture(VulkanDevice* InDevice, const TextureDesc& InDesc,
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
    
    // 转换 RHI Format 到 Vulkan Format
    VkFormat vulkanFormat = VK_FORMAT_R8G8B8A8_UNORM;  // 默认
    // TODO: 完整的格式映射
    
    // 确定图像类型
    VkImageType imageType = VK_IMAGE_TYPE_2D;
    if (Desc.depth > 1) {
        imageType = VK_IMAGE_TYPE_3D;
    } else if (Desc.height == 1) {
        imageType = VK_IMAGE_TYPE_1D;
    }
    
    // 转换 Usage
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
    
    // 创建 VkImage
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
    
    // Cube 纹理需要特殊标志
    if (Desc.arraySize == 6) {
        imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }
    
    VkResult result = functions.vkCreateImage(Device->getLogicalDevice(), &imageInfo, nullptr, &Image);
    if (result != VK_SUCCESS) {
        MR_LOG_ERROR("FVulkanTexture: 创建 VkImage 失败，VkResult: " + std::to_string(result));
        return false;
    }
    
    // 查询内存需求
    VkMemoryRequirements memReqs;
    functions.vkGetImageMemoryRequirements(Device->getLogicalDevice(), Image, &memReqs);
    
    // 分配内存
    FVulkanMemoryManager* memMgr = Device->GetMemoryManager();
    FVulkanMemoryManager::FAllocationRequest request{};
    request.Size = memReqs.size;
    request.Alignment = memReqs.alignment;
    request.MemoryTypeBits = memReqs.memoryTypeBits;
    request.RequiredFlags = MemoryFlags;
    
    if (!memMgr->Allocate(request, Allocation)) {
        MR_LOG_ERROR("FVulkanTexture: 内存分配失败");
        functions.vkDestroyImage(Device->getLogicalDevice(), Image, nullptr);
        Image = VK_NULL_HANDLE;
        return false;
    }
    
    // 绑定内存
    result = functions.vkBindImageMemory(Device->getLogicalDevice(), Image,
                                         Allocation.DeviceMemory, Allocation.Offset);
    if (result != VK_SUCCESS) {
        MR_LOG_ERROR("FVulkanTexture: 绑定内存失败，VkResult: " + std::to_string(result));
        memMgr->Free(Allocation);
        functions.vkDestroyImage(Device->getLogicalDevice(), Image, nullptr);
        Image = VK_NULL_HANDLE;
        return false;
    }
    
    // 创建 Image View
    if (!CreateImageView()) {
        memMgr->Free(Allocation);
        functions.vkDestroyImage(Device->getLogicalDevice(), Image, nullptr);
        Image = VK_NULL_HANDLE;
        return false;
    }
    
    MR_LOG_DEBUG("FVulkanTexture: 创建成功 (" + std::to_string(Desc.width) + "x" +
                 std::to_string(Desc.height) + ", " +
                 std::to_string(Allocation.Size / (1024 * 1024)) + "MB, " +
                 (Allocation.bDedicated ? "独立" : "子分配") + ")");
    
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
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;  // TODO: 从 Desc.format 映射
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = Desc.mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = Desc.arraySize;
    
    VkResult result = functions.vkCreateImageView(Device->getLogicalDevice(), &viewInfo, nullptr, &ImageView);
    if (result != VK_SUCCESS) {
        MR_LOG_ERROR("FVulkanTexture: 创建 ImageView 失败，VkResult: " + std::to_string(result));
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
    
    // 销毁 ImageView
    if (ImageView != VK_NULL_HANDLE) {
        functions.vkDestroyImageView(Device->getLogicalDevice(), ImageView, nullptr);
        ImageView = VK_NULL_HANDLE;
    }
    
    // 销毁 Image
    functions.vkDestroyImage(Device->getLogicalDevice(), Image, nullptr);
    Image = VK_NULL_HANDLE;
    
    // 释放内存
    FVulkanMemoryManager* memMgr = Device->GetMemoryManager();
    memMgr->Free(Allocation);
    
    MR_LOG_DEBUG("FVulkanTexture: 销毁 (" + std::to_string(Desc.width) + "x" +
                 std::to_string(Desc.height) + ")");
}

// ============================================================================
// FVulkanResourceManager Implementation (Vulkan 资源管理器实现)
// ============================================================================

FVulkanResourceManager::FVulkanResourceManager(VulkanDevice* InDevice, FVulkanMemoryManager* InMemoryManager)
    : Device(InDevice)
    , MemoryManager(InMemoryManager)
    , TotalBufferCount(0)
    , TotalTextureCount(0)
    , TotalBufferMemory(0)
    , TotalTextureMemory(0)
{
    MR_LOG_INFO("FVulkanResourceManager: 初始化完成");
}

FVulkanResourceManager::~FVulkanResourceManager()
{
    ReleaseUnusedResources();
    
    MR_LOG_INFO("FVulkanResourceManager: 销毁完成");
    MR_LOG_INFO("  总缓冲区: " + std::to_string(TotalBufferCount.load()));
    MR_LOG_INFO("  总纹理: " + std::to_string(TotalTextureCount.load()));
}

FRHIBufferRef FVulkanResourceManager::CreateBuffer(uint32 Size, EResourceUsage Usage,
                                                    VkMemoryPropertyFlags MemoryFlags, uint32 Stride)
{
    FVulkanBuffer* buffer = new FVulkanBuffer(Device, Size, Usage, MemoryFlags, Stride);
    
    if (!buffer->Initialize()) {
        MR_LOG_ERROR("FVulkanResourceManager: Buffer 初始化失败");
        delete buffer;
        return nullptr;
    }
    
    // 追踪
    {
        std::lock_guard<std::mutex> lock(ResourcesMutex);
        ActiveBuffers.push_back(buffer);
    }
    
    TotalBufferCount.fetch_add(1);
    TotalBufferMemory.fetch_add(Size);
    
    return FRHIBufferRef(buffer);
}

FRHITextureRef FVulkanResourceManager::CreateTexture(const TextureDesc& Desc,
                                                      VkMemoryPropertyFlags MemoryFlags)
{
    FVulkanTexture* texture = new FVulkanTexture(Device, Desc, MemoryFlags);
    
    if (!texture->Initialize()) {
        MR_LOG_ERROR("FVulkanResourceManager: Texture 初始化失败");
        delete texture;
        return nullptr;
    }
    
    // 追踪
    {
        std::lock_guard<std::mutex> lock(ResourcesMutex);
        ActiveTextures.push_back(texture);
    }
    
    TotalTextureCount.fetch_add(1);
    // 纹理大小从 Allocation 获取
    TotalTextureMemory.fetch_add(texture->GetAllocation().Size);
    
    return FRHITextureRef(texture);
}

void FVulkanResourceManager::DeferredRelease(FRHIResource* Resource, uint64 FrameNumber)
{
    std::lock_guard<std::mutex> lock(DeferredReleasesMutex);
    DeferredReleases.emplace_back(Resource, FrameNumber);
}

void FVulkanResourceManager::ProcessDeferredReleases(uint64 CompletedFrameNumber)
{
    std::lock_guard<std::mutex> lock(DeferredReleasesMutex);
    
    auto it = DeferredReleases.begin();
    while (it != DeferredReleases.end()) {
        // 如果资源提交的帧已经完成 + DEFERRED_RELEASE_FRAMES 帧，可以安全释放
        if (CompletedFrameNumber >= it->FrameNumber + DEFERRED_RELEASE_FRAMES) {
            it->Resource->Release();
            it = DeferredReleases.erase(it);
        } else {
            ++it;
        }
    }
}

void FVulkanResourceManager::GetResourceStats(FResourceStats& OutStats)
{
    OutStats = FResourceStats{};
    
    {
        std::lock_guard<std::mutex> lock(ResourcesMutex);
        OutStats.NumBuffers = static_cast<uint32>(ActiveBuffers.size());
        OutStats.NumTextures = static_cast<uint32>(ActiveTextures.size());
    }
    
    {
        std::lock_guard<std::mutex> lock(DeferredReleasesMutex);
        OutStats.PendingReleases = static_cast<uint32>(DeferredReleases.size());
    }
    
    OutStats.BufferMemory = TotalBufferMemory.load();
    OutStats.TextureMemory = TotalTextureMemory.load();
    
    MR_LOG_DEBUG("===== FVulkanResourceManager 统计 =====");
    MR_LOG_DEBUG("  缓冲区: " + std::to_string(OutStats.NumBuffers) + 
                 " (" + std::to_string(OutStats.BufferMemory / (1024 * 1024)) + "MB)");
    MR_LOG_DEBUG("  纹理: " + std::to_string(OutStats.NumTextures) +
                 " (" + std::to_string(OutStats.TextureMemory / (1024 * 1024)) + "MB)");
    MR_LOG_DEBUG("  待释放: " + std::to_string(OutStats.PendingReleases));
    MR_LOG_DEBUG("=======================================");
}

void FVulkanResourceManager::ReleaseUnusedResources()
{
    // 立即释放所有待释放资源
    {
        std::lock_guard<std::mutex> lock(DeferredReleasesMutex);
        for (auto& entry : DeferredReleases) {
            entry.Resource->Release();
        }
        DeferredReleases.clear();
    }
    
    // 清空活动资源列表（资源已经通过引用计数管理）
    {
        std::lock_guard<std::mutex> lock(ResourcesMutex);
        ActiveBuffers.clear();
        ActiveTextures.clear();
    }
}

} // namespace RHI
} // namespace MonsterRender


