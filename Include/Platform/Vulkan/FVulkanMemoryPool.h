// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Vulkan Memory Pool (UE5-style)
//
// 参考 UE5: Engine/Source/Runtime/VulkanRHI/Private/VulkanMemory.h

#pragma once

#include "Core/CoreTypes.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include "Platform/Vulkan/FVulkanMemoryManager.h"
#include <vector>
#include <mutex>
#include <memory>

namespace MonsterRender {
namespace RHI {

// 前向声明
class VulkanDevice;

/**
 * FVulkanMemoryPool - Vulkan 内存池
 * 
 * 管理特定内存类型的大块分配（Pages）
 * 每个 Pool 负责一个特定的内存类型（Device Local、Host Visible 等）
 * 使用 FVulkanMemoryManager 进行底层子分配
 * 
 * 参考 UE5: FDeviceMemoryBlock + FDeviceMemoryManager
 */
class FVulkanMemoryPool {
public:
    /**
     * 内存页（Memory Page）
     * 
     * 代表一个大块的 VkDeviceMemory 分配（通常 64MB - 256MB）
     * 内部使用 FVulkanMemoryManager 进行子分配
     */
    struct FMemoryPage {
        VkDeviceMemory DeviceMemory;            // Vulkan 内存句柄
        uint64 Size;                            // 页大小
        uint32 MemoryTypeIndex;                 // 内存类型索引
        bool bMapped;                           // 是否可映射
        void* MappedPointer;                    // 映射指针（如果可映射）
        
        // 子分配器（使用 FVulkanMemoryManager）
        std::unique_ptr<FVulkanMemoryManager> SubAllocator;
        
        FMemoryPage()
            : DeviceMemory(VK_NULL_HANDLE)
            , Size(0)
            , MemoryTypeIndex(0)
            , bMapped(false)
            , MappedPointer(nullptr)
        {}
    };
    
    /**
     * 构造函数
     * @param Device Vulkan 设备
     * @param MemoryTypeIndex 内存类型索引
     * @param PageSize 每页大小（默认 64MB）
     */
    FVulkanMemoryPool(VulkanDevice* Device, uint32 MemoryTypeIndex, uint64 PageSize = 64 * 1024 * 1024);
    ~FVulkanMemoryPool();
    
    // 不可拷贝
    FVulkanMemoryPool(const FVulkanMemoryPool&) = delete;
    FVulkanMemoryPool& operator=(const FVulkanMemoryPool&) = delete;
    
    /**
     * 分配内存
     * @param Size 大小
     * @param Alignment 对齐
     * @param OutAllocation 输出分配信息
     * @return 是否成功
     */
    bool Allocate(uint64 Size, uint64 Alignment, FVulkanAllocation& OutAllocation);
    
    /**
     * 释放内存
     * @param Allocation 分配信息
     */
    void Free(const FVulkanAllocation& Allocation);
    
    /**
     * 获取内存类型索引
     */
    uint32 GetMemoryTypeIndex() const { return MemoryTypeIndex; }
    
    /**
     * 获取统计信息
     */
    struct FPoolStats {
        uint64 TotalAllocated;          // 总分配（从驱动）
        uint64 TotalUsed;               // 实际使用
        uint64 TotalFree;               // 空闲
        uint32 NumPages;                // 页数量
        uint32 NumAllocations;          // 分配次数
        
        FPoolStats()
            : TotalAllocated(0), TotalUsed(0), TotalFree(0)
            , NumPages(0), NumAllocations(0) {}
    };
    
    void GetStats(FPoolStats& OutStats);
    
    /**
     * 清理空闲页（释放完全未使用的页）
     * @return 释放的页数
     */
    uint32 TrimEmptyPages();

private:
    /**
     * 创建新的内存页
     * @param Size 页大小
     * @return 页索引，失败返回 -1
     */
    int32 CreatePage(uint64 Size);
    
    /**
     * 销毁内存页
     * @param PageIndex 页索引
     */
    void DestroyPage(int32 PageIndex);
    
    VulkanDevice* Device;                       // 所属设备
    uint32 MemoryTypeIndex;                     // 内存类型索引
    uint64 DefaultPageSize;                     // 默认页大小
    
    std::vector<std::unique_ptr<FMemoryPage>> Pages;  // 内存页列表
    std::mutex Mutex;                                 // 线程安全锁
    
    // 统计
    std::atomic<uint64> TotalAllocated;
    std::atomic<uint64> TotalUsed;
    std::atomic<uint32> TotalAllocationCount;
};

/**
 * FVulkanPoolManager - Vulkan 内存池管理器
 * 
 * 管理多个 FVulkanMemoryPool，每个内存类型一个 Pool
 * 提供统一的分配接口
 * 
 * 参考 UE5: FVulkanDeviceMemoryManager
 */
class FVulkanPoolManager {
public:
    /**
     * 构造函数
     * @param Device Vulkan 设备
     */
    explicit FVulkanPoolManager(VulkanDevice* Device);
    ~FVulkanPoolManager();
    
    // 不可拷贝
    FVulkanPoolManager(const FVulkanPoolManager&) = delete;
    FVulkanPoolManager& operator=(const FVulkanPoolManager&) = delete;
    
    /**
     * 分配内存
     * @param Request 分配请求
     * @param OutAllocation 输出分配信息
     * @return 是否成功
     */
    bool Allocate(const FVulkanMemoryManager::FAllocationRequest& Request,
                  FVulkanAllocation& OutAllocation);
    
    /**
     * 释放内存
     * @param Allocation 分配信息
     */
    void Free(const FVulkanAllocation& Allocation);
    
    /**
     * 获取统计信息
     */
    struct FManagerStats {
        uint64 TotalAllocated;          // 总分配
        uint64 TotalUsed;               // 实际使用
        uint32 NumPools;                // Pool 数量
        uint32 NumPages;                // 总页数
        uint32 NumAllocations;          // 总分配次数
        
        FManagerStats()
            : TotalAllocated(0), TotalUsed(0), NumPools(0)
            , NumPages(0), NumAllocations(0) {}
    };
    
    void GetStats(FManagerStats& OutStats);
    
    /**
     * 清理所有池的空闲页
     * @return 释放的总页数
     */
    uint32 TrimAllPools();

private:
    /**
     * 获取或创建指定内存类型的 Pool
     * @param MemoryTypeIndex 内存类型索引
     * @return Pool 指针
     */
    FVulkanMemoryPool* GetOrCreatePool(uint32 MemoryTypeIndex);
    
    VulkanDevice* Device;                                                   // Vulkan 设备
    std::vector<std::unique_ptr<FVulkanMemoryPool>> Pools;                  // Pool 列表（索引对应内存类型）
    std::mutex Mutex;                                                        // 线程安全锁
    
    // 配置
    static constexpr uint64 DEFAULT_PAGE_SIZE = 64 * 1024 * 1024;           // 64MB
    static constexpr uint64 LARGE_ALLOCATION_THRESHOLD = 16 * 1024 * 1024;  // 16MB 以上使用独立分配
};

} // namespace RHI
} // namespace MonsterRender

