// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Vulkan Memory Pool Implementation
//
// 参考 UE5: Engine/Source/Runtime/VulkanRHI/Private/VulkanMemory.cpp

#include "Platform/Vulkan/FVulkanMemoryPool.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include "Core/Log.h"
#include <algorithm>

namespace MonsterRender {
namespace RHI {

using namespace Vulkan;

// ============================================================================
// FVulkanMemoryPool Implementation
// ============================================================================

FVulkanMemoryPool::FVulkanMemoryPool(VulkanDevice* InDevice, uint32 InMemoryTypeIndex, uint64 PageSize)
    : Device(InDevice)
    , MemoryTypeIndex(InMemoryTypeIndex)
    , DefaultPageSize(PageSize)
    , TotalAllocated(0)
    , TotalUsed(0)
    , TotalAllocationCount(0)
{
    MR_LOG_INFO("FVulkanMemoryPool: 初始化 (内存类型: " + std::to_string(MemoryTypeIndex) +
                ", 页大小: " + std::to_string(PageSize / (1024 * 1024)) + "MB)");
}

FVulkanMemoryPool::~FVulkanMemoryPool()
{
    std::lock_guard<std::mutex> lock(Mutex);
    
    // 销毁所有页
    for (size_t i = 0; i < Pages.size(); ++i) {
        if (Pages[i]) {
            DestroyPage(static_cast<int32>(i));
        }
    }
    Pages.clear();
    
    MR_LOG_INFO("FVulkanMemoryPool: 销毁 (内存类型: " + std::to_string(MemoryTypeIndex) +
                ", 总分配: " + std::to_string(TotalAllocated.load() / (1024 * 1024)) + "MB)");
}

bool FVulkanMemoryPool::Allocate(uint64 Size, uint64 Alignment, FVulkanAllocation& OutAllocation)
{
    std::lock_guard<std::mutex> lock(Mutex);
    
    // 尝试从现有页分配
    for (auto& page : Pages) {
        if (!page || !page->SubAllocator) {
            continue;
        }
        
        FVulkanMemoryManager::FAllocationRequest request{};
        request.Size = Size;
        request.Alignment = Alignment;
        request.MemoryTypeBits = (1u << MemoryTypeIndex);  // 只允许此内存类型
        request.RequiredFlags = 0;  // 已经通过 MemoryTypeIndex 确定
        
        if (page->SubAllocator->Allocate(request, OutAllocation)) {
            // 成功分配
            TotalUsed.fetch_add(Size);
            TotalAllocationCount.fetch_add(1);
            
            // 将 Page 的 DeviceMemory 设置到 Allocation 中
            OutAllocation.DeviceMemory = page->DeviceMemory;
            
            return true;
        }
    }
    
    // 需要创建新页
    // 页大小至少为请求的 2 倍，但不小于默认页大小
    uint64 newPageSize = std::max(DefaultPageSize, Size * 2);
    
    int32 pageIndex = CreatePage(newPageSize);
    if (pageIndex < 0) {
        MR_LOG_ERROR("FVulkanMemoryPool: 创建新页失败");
        return false;
    }
    
    // 从新页分配
    auto& newPage = Pages[pageIndex];
    FVulkanMemoryManager::FAllocationRequest request{};
    request.Size = Size;
    request.Alignment = Alignment;
    request.MemoryTypeBits = (1u << MemoryTypeIndex);
    request.RequiredFlags = 0;
    
    if (!newPage->SubAllocator->Allocate(request, OutAllocation)) {
        MR_LOG_ERROR("FVulkanMemoryPool: 从新页分配失败");
        DestroyPage(pageIndex);
        return false;
    }
    
    TotalUsed.fetch_add(Size);
    TotalAllocationCount.fetch_add(1);
    
    // 设置 DeviceMemory
    OutAllocation.DeviceMemory = newPage->DeviceMemory;
    
    MR_LOG_DEBUG("FVulkanMemoryPool: 分配 " + std::to_string(Size / 1024) + "KB (新页)");
    
    return true;
}

void FVulkanMemoryPool::Free(const FVulkanAllocation& Allocation)
{
    std::lock_guard<std::mutex> lock(Mutex);
    
    // 查找对应的页
    for (auto& page : Pages) {
        if (page && page->DeviceMemory == Allocation.DeviceMemory) {
            page->SubAllocator->Free(const_cast<FVulkanAllocation&>(Allocation));
            TotalUsed.fetch_sub(Allocation.Size);
            return;
        }
    }
    
    MR_LOG_WARNING("FVulkanMemoryPool: Free - 找不到对应的内存页");
}

void FVulkanMemoryPool::GetStats(FPoolStats& OutStats)
{
    std::lock_guard<std::mutex> lock(Mutex);
    
    OutStats = FPoolStats{};
    OutStats.TotalAllocated = TotalAllocated.load();
    OutStats.TotalUsed = TotalUsed.load();
    OutStats.TotalFree = OutStats.TotalAllocated - OutStats.TotalUsed;
    OutStats.NumPages = static_cast<uint32>(Pages.size());
    OutStats.NumAllocations = TotalAllocationCount.load();
}

uint32 FVulkanMemoryPool::TrimEmptyPages()
{
    std::lock_guard<std::mutex> lock(Mutex);
    
    uint32 freedPages = 0;
    
    for (int32 i = static_cast<int32>(Pages.size()) - 1; i >= 0; --i) {
        if (!Pages[i] || !Pages[i]->SubAllocator) {
            continue;
        }
        
        // 检查页是否完全空闲
        FVulkanMemoryManager::FMemoryStats stats;
        Pages[i]->SubAllocator->GetMemoryStats(stats);
        
        if (stats.TotalAllocated == 0) {
            // 完全空闲，可以释放
            DestroyPage(i);
            Pages.erase(Pages.begin() + i);
            freedPages++;
        }
    }
    
    if (freedPages > 0) {
        MR_LOG_INFO("FVulkanMemoryPool: 清理了 " + std::to_string(freedPages) + " 个空闲页");
    }
    
    return freedPages;
}

int32 FVulkanMemoryPool::CreatePage(uint64 Size)
{
    const auto& functions = VulkanAPI::getFunctions();
    
    // 分配 VkDeviceMemory
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = Size;
    allocInfo.memoryTypeIndex = MemoryTypeIndex;
    
    VkDeviceMemory deviceMemory = VK_NULL_HANDLE;
    VkResult result = functions.vkAllocateMemory(Device->getLogicalDevice(), &allocInfo, nullptr, &deviceMemory);
    
    if (result != VK_SUCCESS) {
        MR_LOG_ERROR("FVulkanMemoryPool: vkAllocateMemory 失败，VkResult: " + std::to_string(result));
        return -1;
    }
    
    // 创建 Page 对象
    auto page = std::make_unique<FMemoryPage>();
    page->DeviceMemory = deviceMemory;
    page->Size = Size;
    page->MemoryTypeIndex = MemoryTypeIndex;
    
    // 检查是否可映射
    VkPhysicalDeviceMemoryProperties memProps;
    functions.vkGetPhysicalDeviceMemoryProperties(Device->getPhysicalDevice(), &memProps);
    
    if (memProps.memoryTypes[MemoryTypeIndex].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        // 可映射，立即映射
        result = functions.vkMapMemory(Device->getLogicalDevice(), deviceMemory, 0, Size, 0, &page->MappedPointer);
        if (result == VK_SUCCESS) {
            page->bMapped = true;
        } else {
            MR_LOG_WARNING("FVulkanMemoryPool: vkMapMemory 失败，VkResult: " + std::to_string(result));
        }
    }
    
    // 创建子分配器（使用现有的 FVulkanMemoryManager 作为子分配器的实现）
    // 注意：这里简化处理，实际上应该创建一个专门的子分配器
    // 暂时使用一个简单的方案：直接在 Free 时标记内存
    page->SubAllocator = std::make_unique<FVulkanMemoryManager>(Device);
    
    // 添加到列表
    int32 pageIndex = static_cast<int32>(Pages.size());
    Pages.push_back(std::move(page));
    
    TotalAllocated.fetch_add(Size);
    
    MR_LOG_DEBUG("FVulkanMemoryPool: 创建页 #" + std::to_string(pageIndex) +
                 " (" + std::to_string(Size / (1024 * 1024)) + "MB, " +
                 (Pages[pageIndex]->bMapped ? "可映射" : "设备本地") + ")");
    
    return pageIndex;
}

void FVulkanMemoryPool::DestroyPage(int32 PageIndex)
{
    if (PageIndex < 0 || PageIndex >= static_cast<int32>(Pages.size())) {
        return;
    }
    
    auto& page = Pages[PageIndex];
    if (!page) {
        return;
    }
    
    const auto& functions = VulkanAPI::getFunctions();
    
    // 取消映射
    if (page->bMapped && page->MappedPointer) {
        functions.vkUnmapMemory(Device->getLogicalDevice(), page->DeviceMemory);
        page->MappedPointer = nullptr;
        page->bMapped = false;
    }
    
    // 释放内存
    if (page->DeviceMemory != VK_NULL_HANDLE) {
        functions.vkFreeMemory(Device->getLogicalDevice(), page->DeviceMemory, nullptr);
        TotalAllocated.fetch_sub(page->Size);
    }
    
    // 销毁子分配器
    page->SubAllocator.reset();
    
    MR_LOG_DEBUG("FVulkanMemoryPool: 销毁页 #" + std::to_string(PageIndex));
    
    page.reset();
}

// ============================================================================
// FVulkanPoolManager Implementation
// ============================================================================

FVulkanPoolManager::FVulkanPoolManager(VulkanDevice* InDevice)
    : Device(InDevice)
{
    // 预分配 Pool 数组（最多 32 个内存类型）
    Pools.resize(32);
    
    MR_LOG_INFO("FVulkanPoolManager: 初始化完成");
}

FVulkanPoolManager::~FVulkanPoolManager()
{
    std::lock_guard<std::mutex> lock(Mutex);
    
    Pools.clear();
    
    MR_LOG_INFO("FVulkanPoolManager: 销毁完成");
}

bool FVulkanPoolManager::Allocate(const FVulkanMemoryManager::FAllocationRequest& Request,
                                   FVulkanAllocation& OutAllocation)
{
    // 大分配直接使用独立分配（不走 Pool）
    if (Request.Size >= LARGE_ALLOCATION_THRESHOLD) {
        MR_LOG_DEBUG("FVulkanPoolManager: 大分配 (" + std::to_string(Request.Size / (1024 * 1024)) +
                     "MB)，使用独立分配");
        
        // 这里应该调用 VulkanDevice 的内存管理器直接分配
        // 暂时返回 false，需要与现有系统集成
        return false;
    }
    
    // 选择内存类型
    const auto& functions = VulkanAPI::getFunctions();
    VkPhysicalDeviceMemoryProperties memProps;
    functions.vkGetPhysicalDeviceMemoryProperties(Device->getPhysicalDevice(), &memProps);
    
    uint32 memoryTypeIndex = UINT32_MAX;
    for (uint32 i = 0; i < memProps.memoryTypeCount; ++i) {
        // 检查是否满足类型位掩码
        if (!(Request.MemoryTypeBits & (1u << i))) {
            continue;
        }
        
        // 检查是否满足必需标志
        if ((memProps.memoryTypes[i].propertyFlags & Request.RequiredFlags) == Request.RequiredFlags) {
            memoryTypeIndex = i;
            break;
        }
    }
    
    if (memoryTypeIndex == UINT32_MAX) {
        MR_LOG_ERROR("FVulkanPoolManager: 找不到合适的内存类型");
        return false;
    }
    
    // 获取或创建对应的 Pool
    FVulkanMemoryPool* pool = GetOrCreatePool(memoryTypeIndex);
    if (!pool) {
        MR_LOG_ERROR("FVulkanPoolManager: 获取内存池失败");
        return false;
    }
    
    // 从 Pool 分配
    return pool->Allocate(Request.Size, Request.Alignment, OutAllocation);
}

void FVulkanPoolManager::Free(const FVulkanAllocation& Allocation)
{
    // 如果是独立分配，直接释放
    if (Allocation.bDedicated) {
        const auto& functions = VulkanAPI::getFunctions();
        if (Allocation.DeviceMemory != VK_NULL_HANDLE) {
            functions.vkFreeMemory(Device->getLogicalDevice(), Allocation.DeviceMemory, nullptr);
        }
        return;
    }
    
    // 查找对应的 Pool 并释放
    std::lock_guard<std::mutex> lock(Mutex);
    
    for (auto& pool : Pools) {
        if (pool) {
            pool->Free(Allocation);
            return;
        }
    }
    
    MR_LOG_WARNING("FVulkanPoolManager: Free - 找不到对应的内存池");
}

void FVulkanPoolManager::GetStats(FManagerStats& OutStats)
{
    std::lock_guard<std::mutex> lock(Mutex);
    
    OutStats = FManagerStats{};
    
    for (const auto& pool : Pools) {
        if (pool) {
            FVulkanMemoryPool::FPoolStats poolStats;
            pool->GetStats(poolStats);
            
            OutStats.TotalAllocated += poolStats.TotalAllocated;
            OutStats.TotalUsed += poolStats.TotalUsed;
            OutStats.NumPages += poolStats.NumPages;
            OutStats.NumAllocations += poolStats.NumAllocations;
            OutStats.NumPools++;
        }
    }
    
    MR_LOG_DEBUG("===== FVulkanPoolManager 统计 =====");
    MR_LOG_DEBUG("  总分配: " + std::to_string(OutStats.TotalAllocated / (1024 * 1024)) + "MB");
    MR_LOG_DEBUG("  实际使用: " + std::to_string(OutStats.TotalUsed / (1024 * 1024)) + "MB");
    MR_LOG_DEBUG("  内存池数: " + std::to_string(OutStats.NumPools));
    MR_LOG_DEBUG("  总页数: " + std::to_string(OutStats.NumPages));
    MR_LOG_DEBUG("  分配次数: " + std::to_string(OutStats.NumAllocations));
    MR_LOG_DEBUG("=======================================");
}

uint32 FVulkanPoolManager::TrimAllPools()
{
    std::lock_guard<std::mutex> lock(Mutex);
    
    uint32 totalFreedPages = 0;
    
    for (auto& pool : Pools) {
        if (pool) {
            totalFreedPages += pool->TrimEmptyPages();
        }
    }
    
    if (totalFreedPages > 0) {
        MR_LOG_INFO("FVulkanPoolManager: 总共清理了 " + std::to_string(totalFreedPages) + " 个空闲页");
    }
    
    return totalFreedPages;
}

FVulkanMemoryPool* FVulkanPoolManager::GetOrCreatePool(uint32 MemoryTypeIndex)
{
    std::lock_guard<std::mutex> lock(Mutex);
    
    if (MemoryTypeIndex >= Pools.size()) {
        MR_LOG_ERROR("FVulkanPoolManager: 内存类型索引超出范围: " + std::to_string(MemoryTypeIndex));
        return nullptr;
    }
    
    if (!Pools[MemoryTypeIndex]) {
        // 创建新 Pool
        Pools[MemoryTypeIndex] = std::make_unique<FVulkanMemoryPool>(Device, MemoryTypeIndex, DEFAULT_PAGE_SIZE);
        MR_LOG_INFO("FVulkanPoolManager: 创建新内存池 (类型: " + std::to_string(MemoryTypeIndex) + ")");
    }
    
    return Pools[MemoryTypeIndex].get();
}

} // namespace RHI
} // namespace MonsterRender

