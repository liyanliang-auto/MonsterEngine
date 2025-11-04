// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Vulkan Memory Manager Implementation
//
// 参考 UE5: Engine/Source/Runtime/VulkanRHI/Private/VulkanMemory.cpp

#include "Platform/Vulkan/FVulkanMemoryManager.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include "Core/Log.h"
#include <algorithm>

namespace MonsterRender {
namespace RHI {

using namespace Vulkan;

// ============================================================================
// FVulkanMemoryPool Implementation (内存池实现)
// ============================================================================

FVulkanMemoryPool::FVulkanMemoryPool(VkDevice InDevice, VkDeviceSize InPoolSize, 
                                     uint32 InMemoryTypeIndex, bool bInHostVisible)
    : Device(InDevice)
    , DeviceMemory(VK_NULL_HANDLE)
    , PersistentMappedPtr(nullptr)
    , PoolSize(InPoolSize)
    , MemoryTypeIndex(InMemoryTypeIndex)
    , bHostVisible(bInHostVisible)
    , UsedSize(0)
    , FreeList(nullptr)
{
    const auto& functions = VulkanAPI::getFunctions();
    
    // 从 Vulkan 分配大块内存
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = PoolSize;
    allocInfo.memoryTypeIndex = MemoryTypeIndex;
    
    VkResult result = functions.vkAllocateMemory(Device, &allocInfo, nullptr, &DeviceMemory);
    if (result != VK_SUCCESS) {
        MR_LOG_ERROR("FVulkanMemoryPool: 分配 " + std::to_string(PoolSize / (1024 * 1024)) + 
                     "MB 池失败，VkResult: " + std::to_string(result));
        return;
    }
    
    // 如果是 Host Visible 内存，进行持久映射
    if (bHostVisible) {
        result = functions.vkMapMemory(Device, DeviceMemory, 0, PoolSize, 0, &PersistentMappedPtr);
        if (result != VK_SUCCESS) {
            MR_LOG_WARNING("FVulkanMemoryPool: 持久映射失败，将使用按需映射");
            PersistentMappedPtr = nullptr;
        }
    }
    
    // 初始化 Free-List：整个池作为一个大空闲块
    FreeList = new FMemoryBlock(0, PoolSize, true);
    
    MR_LOG_INFO("FVulkanMemoryPool: 创建 " + std::to_string(PoolSize / (1024 * 1024)) + 
                "MB 池 (类型: " + std::to_string(MemoryTypeIndex) + 
                (bHostVisible ? ", Host可见" : ", Device专用") + ")");
}

FVulkanMemoryPool::~FVulkanMemoryPool()
{
    const auto& functions = VulkanAPI::getFunctions();
    
    // 释放所有块节点
    FMemoryBlock* current = FreeList;
    while (current) {
        FMemoryBlock* next = current->Next;
        delete current;
        current = next;
    }
    FreeList = nullptr;
    
    // 取消映射
    if (PersistentMappedPtr) {
        functions.vkUnmapMemory(Device, DeviceMemory);
        PersistentMappedPtr = nullptr;
    }
    
    // 释放 Vulkan 内存
    if (DeviceMemory != VK_NULL_HANDLE) {
        functions.vkFreeMemory(Device, DeviceMemory, nullptr);
        DeviceMemory = VK_NULL_HANDLE;
    }
    
    MR_LOG_DEBUG("FVulkanMemoryPool: 销毁 " + std::to_string(PoolSize / (1024 * 1024)) + "MB 池");
}

bool FVulkanMemoryPool::Allocate(VkDeviceSize Size, VkDeviceSize Alignment, FVulkanAllocation& OutAllocation)
{
    std::lock_guard<std::mutex> lock(PoolMutex);
    
    // 使用 First-Fit 算法查找合适的空闲块
    FMemoryBlock* current = FreeList;
    FMemoryBlock* prev = nullptr;
    
    while (current) {
        if (current->bFree) {
            // 计算对齐后的偏移
            VkDeviceSize alignedOffset = (current->Offset + Alignment - 1) & ~(Alignment - 1);
            VkDeviceSize padding = alignedOffset - current->Offset;
            VkDeviceSize requiredSize = padding + Size;
            
            if (requiredSize <= current->Size) {
                // 找到合适的块！
                
                // 步骤1: 如果需要，创建 padding 块
                if (padding > 0) {
                    FMemoryBlock* paddingBlock = new FMemoryBlock(current->Offset, padding, true);
                    paddingBlock->Next = current;
                    paddingBlock->Prev = prev;
                    
                    if (prev) {
                        prev->Next = paddingBlock;
                    } else {
                        FreeList = paddingBlock;
                    }
                    
                    current->Offset += padding;
                    current->Size -= padding;
                    prev = paddingBlock;
                }
                
                // 步骤2: 如果剩余空间足够大，分割块
                if (current->Size > Size) {
                    FMemoryBlock* remainder = new FMemoryBlock(
                        current->Offset + Size,
                        current->Size - Size,
                        true
                    );
                    remainder->Next = current->Next;
                    remainder->Prev = current;
                    
                    if (current->Next) {
                        current->Next->Prev = remainder;
                    }
                    current->Next = remainder;
                    
                    current->Size = Size;
                }
                
                // 步骤3: 标记为已分配
                current->bFree = false;
                UsedSize.fetch_add(current->Size);
                
                // 步骤4: 填充输出结果
                OutAllocation.DeviceMemory = DeviceMemory;
                OutAllocation.Offset = current->Offset;
                OutAllocation.Size = current->Size;
                OutAllocation.MemoryTypeIndex = MemoryTypeIndex;
                OutAllocation.MappedPointer = nullptr;
                OutAllocation.bDedicated = false;
                OutAllocation.bMapped = false;
                OutAllocation.Pool = this;
                OutAllocation.AllocationHandle = current;
                
                // 如果有持久映射，设置映射指针
                if (PersistentMappedPtr) {
                    OutAllocation.MappedPointer = static_cast<uint8*>(PersistentMappedPtr) + current->Offset;
                    OutAllocation.bMapped = true;
                }
                
                MR_LOG_DEBUG("FVulkanMemoryPool: 子分配 " + std::to_string(Size / 1024) + 
                             "KB (偏移: " + std::to_string(current->Offset) + 
                             ", 利用率: " + std::to_string(UsedSize.load() * 100 / PoolSize) + "%)");
                
                return true;
            }
        }
        
        prev = current;
        current = current->Next;
    }
    
    // 没有找到合适的块
    MR_LOG_DEBUG("FVulkanMemoryPool: 无法分配 " + std::to_string(Size / 1024) + "KB (池已满)");
    return false;
}

void FVulkanMemoryPool::Free(const FVulkanAllocation& Allocation)
{
    std::lock_guard<std::mutex> lock(PoolMutex);
    
    FMemoryBlock* block = static_cast<FMemoryBlock*>(Allocation.AllocationHandle);
    if (!block) {
        MR_LOG_ERROR("FVulkanMemoryPool::Free: 无效的块指针");
        return;
    }
    
    if (block->bFree) {
        MR_LOG_WARNING("FVulkanMemoryPool::Free: 尝试释放已经空闲的块");
        return;
    }
    
    // 标记为空闲
    block->bFree = true;
    UsedSize.fetch_sub(block->Size);
    
    MR_LOG_DEBUG("FVulkanMemoryPool: 释放 " + std::to_string(block->Size / 1024) + 
                 "KB (利用率: " + std::to_string(UsedSize.load() * 100 / PoolSize) + "%)");
    
    // 合并相邻的空闲块
    MergeFreeBlocks();
}

bool FVulkanMemoryPool::Map(FVulkanAllocation& Allocation, void** OutMappedPtr)
{
    if (!bHostVisible) {
        MR_LOG_ERROR("FVulkanMemoryPool::Map: 尝试映射 Device专用 内存");
        return false;
    }
    
    // 如果有持久映射，直接返回
    if (PersistentMappedPtr) {
        *OutMappedPtr = static_cast<uint8*>(PersistentMappedPtr) + Allocation.Offset;
        Allocation.MappedPointer = *OutMappedPtr;
        Allocation.bMapped = true;
        return true;
    }
    
    // 按需映射
    const auto& functions = VulkanAPI::getFunctions();
    void* mappedPtr = nullptr;
    VkResult result = functions.vkMapMemory(Device, DeviceMemory, Allocation.Offset, Allocation.Size, 0, &mappedPtr);
    
    if (result != VK_SUCCESS) {
        MR_LOG_ERROR("FVulkanMemoryPool::Map: 映射失败，VkResult: " + std::to_string(result));
        return false;
    }
    
    *OutMappedPtr = mappedPtr;
    Allocation.MappedPointer = mappedPtr;
    Allocation.bMapped = true;
    return true;
}

void FVulkanMemoryPool::Unmap(FVulkanAllocation& Allocation)
{
    if (!Allocation.bMapped) {
        return;
    }
    
    // 如果是持久映射，不需要 unmap
    if (PersistentMappedPtr) {
        Allocation.MappedPointer = nullptr;
        Allocation.bMapped = false;
        return;
    }
    
    // 取消按需映射
    const auto& functions = VulkanAPI::getFunctions();
    functions.vkUnmapMemory(Device, DeviceMemory);
    Allocation.MappedPointer = nullptr;
    Allocation.bMapped = false;
}

void FVulkanMemoryPool::Defragment()
{
    std::lock_guard<std::mutex> lock(PoolMutex);
    MergeFreeBlocks();
}

void FVulkanMemoryPool::MergeFreeBlocks()
{
    // 合并相邻的空闲块，减少碎片
    FMemoryBlock* current = FreeList;
    
    while (current && current->Next) {
        if (current->bFree && current->Next->bFree) {
            // 合并 current 和 Next
            FMemoryBlock* next = current->Next;
            current->Size += next->Size;
            current->Next = next->Next;
            
            if (next->Next) {
                next->Next->Prev = current;
            }
            
            delete next;
            // 继续从 current 检查 (可能连续合并多个块)
        } else {
            current = current->Next;
        }
    }
}

FVulkanMemoryPool::FMemoryBlock* FVulkanMemoryPool::FindFirstFit(VkDeviceSize Size, VkDeviceSize Alignment)
{
    FMemoryBlock* current = FreeList;
    
    while (current) {
        if (current->bFree) {
            VkDeviceSize alignedOffset = (current->Offset + Alignment - 1) & ~(Alignment - 1);
            VkDeviceSize padding = alignedOffset - current->Offset;
            VkDeviceSize requiredSize = padding + Size;
            
            if (requiredSize <= current->Size) {
                return current;
            }
        }
        current = current->Next;
    }
    
    return nullptr;
}

// ============================================================================
// FVulkanMemoryManager Implementation (内存管理器实现)
// ============================================================================

FVulkanMemoryManager::FVulkanMemoryManager(VkDevice InDevice, VkPhysicalDevice InPhysicalDevice)
    : Device(InDevice)
    , PhysicalDevice(InPhysicalDevice)
    , TotalAllocationCount(0)
    , DedicatedAllocationCount(0)
    , TotalAllocatedMemory(0)
{
    const auto& functions = VulkanAPI::getFunctions();
    
    // 查询物理设备内存属性
    functions.vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &MemoryProperties);
    
    MR_LOG_INFO("=====================================");
    MR_LOG_INFO("FVulkanMemoryManager: 初始化完成");
    MR_LOG_INFO("  内存类型数量: " + std::to_string(MemoryProperties.memoryTypeCount));
    MR_LOG_INFO("  内存堆数量: " + std::to_string(MemoryProperties.memoryHeapCount));
    MR_LOG_INFO("-------------------------------------");
    
    // 打印所有内存堆信息
    for (uint32 i = 0; i < MemoryProperties.memoryHeapCount; ++i) {
        const auto& heap = MemoryProperties.memoryHeaps[i];
        String heapType = (heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) ? "Device专用" : "Host可见";
        MR_LOG_INFO("  堆 " + std::to_string(i) + ": " + 
                    std::to_string(heap.size / (1024 * 1024)) + "MB [" + heapType + "]");
    }
    
    MR_LOG_INFO("-------------------------------------");
    
    // 打印所有内存类型
    for (uint32 i = 0; i < MemoryProperties.memoryTypeCount; ++i) {
        const auto& memType = MemoryProperties.memoryTypes[i];
        String properties;
        
        if (memType.propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
            properties += "DeviceLocal ";
        if (memType.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
            properties += "HostVisible ";
        if (memType.propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
            properties += "HostCoherent ";
        if (memType.propertyFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT)
            properties += "HostCached ";
            
        MR_LOG_INFO("  类型 " + std::to_string(i) + " (堆 " + 
                    std::to_string(memType.heapIndex) + "): " + properties);
    }
    
    MR_LOG_INFO("=====================================");
}

FVulkanMemoryManager::~FVulkanMemoryManager()
{
    // 释放所有池
    for (uint32 i = 0; i < VK_MAX_MEMORY_TYPES; ++i) {
        std::lock_guard<std::mutex> lock(PoolsMutex[i]);
        Pools[i].clear();
    }
    
    MR_LOG_INFO("FVulkanMemoryManager: 销毁完成");
    MR_LOG_INFO("  总分配次数: " + std::to_string(TotalAllocationCount.load()));
    MR_LOG_INFO("  独立分配次数: " + std::to_string(DedicatedAllocationCount.load()));
    MR_LOG_INFO("  总分配内存: " + std::to_string(TotalAllocatedMemory.load() / (1024 * 1024)) + "MB");
}

bool FVulkanMemoryManager::Allocate(const FAllocationRequest& Request, FVulkanAllocation& OutAllocation)
{
    // 查找合适的内存类型
    uint32 memoryTypeIndex = FindMemoryType(Request.MemoryTypeBits, Request.RequiredFlags);
    
    // 决定是否使用独立分配
    // 条件: 1. 显式请求  2. 大分配 (>= 16MB)  3. 需要可映射但该类型不支持
    bool bUseDedicated = Request.bDedicated || (Request.Size >= LARGE_ALLOCATION_THRESHOLD);
    
    if (bUseDedicated) {
        MR_LOG_DEBUG("FVulkanMemoryManager: 使用独立分配 (大小: " + 
                     std::to_string(Request.Size / (1024 * 1024)) + "MB)");
        
        bool result = AllocateDedicated(Request, OutAllocation);
        if (result) {
            TotalAllocationCount.fetch_add(1);
            TotalAllocatedMemory.fetch_add(Request.Size);
        }
        return result;
    }
    
    // 尝试从现有池子分配
    {
        std::lock_guard<std::mutex> lock(PoolsMutex[memoryTypeIndex]);
        
        for (auto& pool : Pools[memoryTypeIndex]) {
            if (pool->Allocate(Request.Size, Request.Alignment, OutAllocation)) {
                TotalAllocationCount.fetch_add(1);
                TotalAllocatedMemory.fetch_add(Request.Size);
                
                MR_LOG_DEBUG("FVulkanMemoryManager: 从现有池分配 " + 
                             std::to_string(Request.Size / 1024) + "KB");
                return true;
            }
        }
    }
    
    // 创建新池
    VkDeviceSize poolSize = std::max<VkDeviceSize>(DEFAULT_POOL_SIZE, Request.Size * 2);
    FVulkanMemoryPool* newPool = FindOrCreatePool(memoryTypeIndex, poolSize);
    
    if (!newPool) {
        MR_LOG_ERROR("FVulkanMemoryManager: 创建新池失败");
        return false;
    }
    
    // 从新池分配
    if (newPool->Allocate(Request.Size, Request.Alignment, OutAllocation)) {
        TotalAllocationCount.fetch_add(1);
        TotalAllocatedMemory.fetch_add(Request.Size);
        
        MR_LOG_DEBUG("FVulkanMemoryManager: 从新池分配 " + 
                     std::to_string(Request.Size / 1024) + "KB");
        return true;
    }
    
    MR_LOG_ERROR("FVulkanMemoryManager: 分配失败");
    return false;
}

void FVulkanMemoryManager::Free(FVulkanAllocation& Allocation)
{
    if (!Allocation.IsValid()) {
        MR_LOG_WARNING("FVulkanMemoryManager::Free: 尝试释放无效分配");
        return;
    }
    
    TotalAllocatedMemory.fetch_sub(Allocation.Size);
    
    if (Allocation.bDedicated) {
        FreeDedicated(Allocation);
    } else {
        if (Allocation.Pool) {
            Allocation.Pool->Free(Allocation);
        } else {
            MR_LOG_ERROR("FVulkanMemoryManager::Free: 无效的池指针");
        }
    }
    
    // 清空分配
    Allocation = FVulkanAllocation{};
}

bool FVulkanMemoryManager::MapMemory(FVulkanAllocation& Allocation, void** OutMappedPtr)
{
    if (!Allocation.IsValid()) {
        MR_LOG_ERROR("FVulkanMemoryManager::MapMemory: 无效分配");
        return false;
    }
    
    if (Allocation.bMapped) {
        *OutMappedPtr = Allocation.MappedPointer;
        return true;
    }
    
    if (Allocation.bDedicated) {
        // 独立分配的映射
        const auto& functions = VulkanAPI::getFunctions();
        VkResult result = functions.vkMapMemory(Device, Allocation.DeviceMemory, 
                                                Allocation.Offset, Allocation.Size, 0, OutMappedPtr);
        
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("FVulkanMemoryManager::MapMemory: 映射失败，VkResult: " + std::to_string(result));
            return false;
        }
        
        Allocation.MappedPointer = *OutMappedPtr;
        Allocation.bMapped = true;
        return true;
    } else {
        // 池子分配的映射
        if (Allocation.Pool) {
            return Allocation.Pool->Map(Allocation, OutMappedPtr);
        }
    }
    
    return false;
}

void FVulkanMemoryManager::UnmapMemory(FVulkanAllocation& Allocation)
{
    if (!Allocation.bMapped) {
        return;
    }
    
    if (Allocation.bDedicated) {
        const auto& functions = VulkanAPI::getFunctions();
        functions.vkUnmapMemory(Device, Allocation.DeviceMemory);
        Allocation.MappedPointer = nullptr;
        Allocation.bMapped = false;
    } else {
        if (Allocation.Pool) {
            Allocation.Pool->Unmap(Allocation);
        }
    }
}

uint32 FVulkanMemoryManager::FindMemoryType(uint32 TypeFilter, VkMemoryPropertyFlags Properties)
{
    for (uint32 i = 0; i < MemoryProperties.memoryTypeCount; ++i) {
        if ((TypeFilter & (1 << i)) && 
            (MemoryProperties.memoryTypes[i].propertyFlags & Properties) == Properties) {
            return i;
        }
    }
    
    MR_LOG_ERROR("FVulkanMemoryManager: 找不到合适的内存类型");
    return 0;
}

void FVulkanMemoryManager::GetMemoryStats(FMemoryStats& OutStats)
{
    OutStats = FMemoryStats{};
    
    OutStats.AllocationCount = TotalAllocationCount.load();
    OutStats.DedicatedAllocationCount = DedicatedAllocationCount.load();
    OutStats.TotalAllocated = TotalAllocatedMemory.load();
    
    // 遍历所有池统计
    for (uint32 i = 0; i < VK_MAX_MEMORY_TYPES; ++i) {
        std::lock_guard<std::mutex> lock(PoolsMutex[i]);
        
        for (const auto& pool : Pools[i]) {
            OutStats.TotalReserved += pool->GetPoolSize();
            OutStats.PoolCount++;
            
            VkDeviceSize freeSize = pool->GetFreeSize();
            if (freeSize > OutStats.LargestFreeBlock) {
                OutStats.LargestFreeBlock = freeSize;
            }
            
            // 分类统计
            if (pool->IsHostVisible()) {
                OutStats.HostVisibleAllocated += pool->GetUsedSize();
            } else {
                OutStats.DeviceLocalAllocated += pool->GetUsedSize();
            }
        }
    }
    
    MR_LOG_DEBUG("===== FVulkanMemoryManager 统计 =====");
    MR_LOG_DEBUG("  总分配: " + std::to_string(OutStats.TotalAllocated / (1024 * 1024)) + "MB");
    MR_LOG_DEBUG("  总保留: " + std::to_string(OutStats.TotalReserved / (1024 * 1024)) + "MB");
    MR_LOG_DEBUG("  池数量: " + std::to_string(OutStats.PoolCount));
    MR_LOG_DEBUG("  独立分配数: " + std::to_string(OutStats.DedicatedAllocationCount));
    MR_LOG_DEBUG("  DeviceLocal: " + std::to_string(OutStats.DeviceLocalAllocated / (1024 * 1024)) + "MB");
    MR_LOG_DEBUG("  HostVisible: " + std::to_string(OutStats.HostVisibleAllocated / (1024 * 1024)) + "MB");
    MR_LOG_DEBUG("=====================================");
}

void FVulkanMemoryManager::DefragmentAll()
{
    MR_LOG_INFO("FVulkanMemoryManager: 开始碎片整理...");
    
    for (uint32 i = 0; i < VK_MAX_MEMORY_TYPES; ++i) {
        std::lock_guard<std::mutex> lock(PoolsMutex[i]);
        
        for (auto& pool : Pools[i]) {
            pool->Defragment();
        }
    }
    
    MR_LOG_INFO("FVulkanMemoryManager: 碎片整理完成");
}

void FVulkanMemoryManager::TrimUnusedPools()
{
    MR_LOG_INFO("FVulkanMemoryManager: 清理未使用的池...");
    
    uint32 trimmedCount = 0;
    
    for (uint32 i = 0; i < VK_MAX_MEMORY_TYPES; ++i) {
        std::lock_guard<std::mutex> lock(PoolsMutex[i]);
        
        // 移除完全未使用的池
        auto it = Pools[i].begin();
        while (it != Pools[i].end()) {
            if ((*it)->GetUsedSize() == 0) {
                it = Pools[i].erase(it);
                trimmedCount++;
            } else {
                ++it;
            }
        }
    }
    
    MR_LOG_INFO("FVulkanMemoryManager: 清理了 " + std::to_string(trimmedCount) + " 个未使用的池");
}

FVulkanMemoryPool* FVulkanMemoryManager::FindOrCreatePool(uint32 MemoryTypeIndex, VkDeviceSize RequiredSize)
{
    std::lock_guard<std::mutex> lock(PoolsMutex[MemoryTypeIndex]);
    
    // 检查是否达到池数量上限
    if (Pools[MemoryTypeIndex].size() >= MAX_POOLS_PER_TYPE) {
        MR_LOG_WARNING("FVulkanMemoryManager: 内存类型 " + std::to_string(MemoryTypeIndex) + 
                       " 的池数量已达上限 (" + std::to_string(MAX_POOLS_PER_TYPE) + ")");
        return nullptr;
    }
    
    // 创建新池
    return CreatePool(MemoryTypeIndex, RequiredSize);
}

FVulkanMemoryPool* FVulkanMemoryManager::CreatePool(uint32 MemoryTypeIndex, VkDeviceSize PoolSize)
{
    bool bHostVisible = IsHostVisibleMemoryType(MemoryTypeIndex);
    
    auto newPool = MakeUnique<FVulkanMemoryPool>(Device, PoolSize, MemoryTypeIndex, bHostVisible);
    
    if (newPool->GetDeviceMemory() == VK_NULL_HANDLE) {
        MR_LOG_ERROR("FVulkanMemoryManager: 创建池失败");
        return nullptr;
    }
    
    Pools[MemoryTypeIndex].push_back(std::move(newPool));
    return Pools[MemoryTypeIndex].back().get();
}

bool FVulkanMemoryManager::AllocateDedicated(const FAllocationRequest& Request, FVulkanAllocation& OutAllocation)
{
    const auto& functions = VulkanAPI::getFunctions();
    
    uint32 memoryTypeIndex = FindMemoryType(Request.MemoryTypeBits, Request.RequiredFlags);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = Request.Size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;
    
    VkDeviceMemory dedicatedMemory = VK_NULL_HANDLE;
    VkResult result = functions.vkAllocateMemory(Device, &allocInfo, nullptr, &dedicatedMemory);
    
    if (result != VK_SUCCESS) {
        MR_LOG_ERROR("FVulkanMemoryManager: 独立分配失败，VkResult: " + std::to_string(result));
        return false;
    }
    
    OutAllocation.DeviceMemory = dedicatedMemory;
    OutAllocation.Offset = 0;
    OutAllocation.Size = Request.Size;
    OutAllocation.MemoryTypeIndex = memoryTypeIndex;
    OutAllocation.MappedPointer = nullptr;
    OutAllocation.bDedicated = true;
    OutAllocation.bMapped = false;
    OutAllocation.Pool = nullptr;
    OutAllocation.AllocationHandle = nullptr;
    
    DedicatedAllocationCount.fetch_add(1);
    
    MR_LOG_INFO("FVulkanMemoryManager: 独立分配 " + 
                std::to_string(Request.Size / (1024 * 1024)) + "MB");
    
    return true;
}

void FVulkanMemoryManager::FreeDedicated(FVulkanAllocation& Allocation)
{
    const auto& functions = VulkanAPI::getFunctions();
    
    if (Allocation.bMapped) {
        functions.vkUnmapMemory(Device, Allocation.DeviceMemory);
    }
    
    functions.vkFreeMemory(Device, Allocation.DeviceMemory, nullptr);
    
    MR_LOG_DEBUG("FVulkanMemoryManager: 释放独立分配 (" + 
                 std::to_string(Allocation.Size / (1024 * 1024)) + "MB)");
}

bool FVulkanMemoryManager::IsHostVisibleMemoryType(uint32 MemoryTypeIndex) const
{
    if (MemoryTypeIndex >= MemoryProperties.memoryTypeCount) {
        return false;
    }
    
    const auto& memType = MemoryProperties.memoryTypes[MemoryTypeIndex];
    return (memType.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
}

} // namespace RHI
} // namespace MonsterRender
