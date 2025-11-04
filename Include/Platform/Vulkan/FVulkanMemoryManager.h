// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Vulkan Memory Manager (UE5-style)
//
// 参考 UE5: Engine/Source/Runtime/VulkanRHI/Private/VulkanMemory.h
// 三层架构: FVulkanMemoryManager -> FVulkanMemoryPool -> FVulkanAllocation

#pragma once

#include "Core/CoreTypes.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include <mutex>
#include <vector>
#include <atomic>

namespace MonsterRender {
namespace RHI {

// Forward declaration
class FVulkanMemoryPool;

/**
 * FVulkanAllocation - GPU 内存分配结果
 * 
 * 表示一个具体的内存分配，可以是：
 * - Sub-allocation: 从内存池子分配的一块区域
 * - Dedicated: 独立的 VkDeviceMemory 对象
 * 
 * 参考 UE5: FVulkanResourceAllocation
 */
struct FVulkanAllocation {
    VkDeviceMemory DeviceMemory;        // Vulkan 内存句柄
    VkDeviceSize Offset;                // 在 DeviceMemory 中的偏移
    VkDeviceSize Size;                  // 分配的大小
    uint32 MemoryTypeIndex;             // 内存类型索引
    void* MappedPointer;                // 映射的 CPU 指针 (如果可映射)
    bool bDedicated;                    // 是否为独立分配
    bool bMapped;                       // 是否已映射
    
    // 内部追踪 (用于 Sub-allocation)
    FVulkanMemoryPool* Pool;            // 所属的内存池
    void* AllocationHandle;             // 内部分配句柄 (指向 FMemoryBlock)
    
    FVulkanAllocation()
        : DeviceMemory(VK_NULL_HANDLE)
        , Offset(0)
        , Size(0)
        , MemoryTypeIndex(0)
        , MappedPointer(nullptr)
        , bDedicated(false)
        , bMapped(false)
        , Pool(nullptr)
        , AllocationHandle(nullptr)
    {}
    
    // 检查是否有效
    bool IsValid() const { return DeviceMemory != VK_NULL_HANDLE; }
};

/**
 * FVulkanMemoryPool - GPU Memory Pool
 * 
 * Manages VkDeviceMemory large blocks for specific memory type, using Free-List algorithm for sub-allocation.
 * Each Pool corresponds to one VkDeviceMemory object (default 64MB).
 * 
 * Reference UE5: FVulkanResourceHeap
 */
class FVulkanMemoryPool {
public:
    /**
     * Constructor
     * @param Device Vulkan logical device
     * @param PoolSize Pool size (bytes)
     * @param MemoryTypeIndex Memory type index
     * @param bHostVisible Whether it's Host-visible memory (mappable)
     */
    FVulkanMemoryPool(VkDevice Device, VkDeviceSize PoolSize, uint32 MemoryTypeIndex, bool bHostVisible);
    ~FVulkanMemoryPool();
    
    // Non-copyable
    FVulkanMemoryPool(const FVulkanMemoryPool&) = delete;
    FVulkanMemoryPool& operator=(const FVulkanMemoryPool&) = delete;
    
    /**
     * Try to allocate from pool
     * @param Size Requested size
     * @param Alignment Alignment requirement
     * @param OutAllocation Output allocation result
     * @return Whether allocation succeeded
     */
    bool Allocate(VkDeviceSize Size, VkDeviceSize Alignment, FVulkanAllocation& OutAllocation);
    
    /**
     * Free allocation
     * @param Allocation Allocation to free
     */
    void Free(const FVulkanAllocation& Allocation);
    
    /**
     * Map memory to CPU address space
     * @param Allocation Allocation to map
     * @param OutMappedPtr Output mapped pointer
     * @return Whether succeeded
     */
    bool Map(FVulkanAllocation& Allocation, void** OutMappedPtr);
    
    /**
     * Unmap memory
     * @param Allocation Allocation to unmap
     */
    void Unmap(FVulkanAllocation& Allocation);
    
    // Getters
    VkDeviceMemory GetDeviceMemory() const { return DeviceMemory; }
    VkDeviceSize GetPoolSize() const { return PoolSize; }
    VkDeviceSize GetUsedSize() const { return UsedSize.load(); }
    VkDeviceSize GetFreeSize() const { return PoolSize - UsedSize.load(); }
    uint32 GetMemoryTypeIndex() const { return MemoryTypeIndex; }
    bool IsHostVisible() const { return bHostVisible; }
    
    /**
     * Defragmentation: Merge adjacent free blocks
     */
    void Defragment();

private:
    /**
     * FMemoryBlock - Memory block node (Free-List linked list)
     * 
     * Each block represents a memory region in the pool, can be free or allocated.
     */
    struct FMemoryBlock {
        VkDeviceSize Offset;            // Offset in pool
        VkDeviceSize Size;              // Block size
        bool bFree;                     // Whether free
        FMemoryBlock* Next;             // Next node in list
        FMemoryBlock* Prev;             // Previous node in list
        
        FMemoryBlock(VkDeviceSize InOffset, VkDeviceSize InSize, bool InFree)
            : Offset(InOffset), Size(InSize), bFree(InFree), Next(nullptr), Prev(nullptr) {}
    };
    
    // Vulkan objects
    VkDevice Device;                    // Logical device
    VkDeviceMemory DeviceMemory;        // Large block memory handle
    void* PersistentMappedPtr;          // Persistent mapped pointer (if Host-visible)
    
    // Pool properties
    VkDeviceSize PoolSize;              // Total pool size
    uint32 MemoryTypeIndex;             // Memory type
    bool bHostVisible;                  // Whether mappable
    
    // Allocation tracking
    std::atomic<VkDeviceSize> UsedSize; // Used size (atomic operation)
    FMemoryBlock* FreeList;             // Free list head
    std::mutex PoolMutex;               // Thread-safe lock
    
    // Internal helper functions
    void MergeFreeBlocks();             // Merge adjacent free blocks
    FMemoryBlock* FindFirstFit(VkDeviceSize Size, VkDeviceSize Alignment);  // First-Fit search
};

/**
 * FVulkanMemoryManager - Vulkan Memory Manager (Singleton)
 * 
 * 管理所有 GPU 内存分配，为不同内存类型维护独立的内存池。
 * 实现策略：
 * - 小分配 (< 16MB): 从内存池子分配
 * - 大分配 (>= 16MB): 使用独立的 VkDeviceMemory
 * 
 * 参考 UE5: FVulkanResourceHeapManager
 */
class FVulkanMemoryManager {
public:
    /**
     * 构造函数
     * @param Device Vulkan 逻辑设备
     * @param PhysicalDevice Vulkan 物理设备
     */
    FVulkanMemoryManager(VkDevice Device, VkPhysicalDevice PhysicalDevice);
    ~FVulkanMemoryManager();
    
    // 不可拷贝
    FVulkanMemoryManager(const FVulkanMemoryManager&) = delete;
    FVulkanMemoryManager& operator=(const FVulkanMemoryManager&) = delete;
    
    /**
     * 分配请求结构
     */
    struct FAllocationRequest {
        VkDeviceSize Size;                  // 请求的大小
        VkDeviceSize Alignment;             // 对齐要求
        uint32 MemoryTypeBits;              // 内存类型位掩码
        VkMemoryPropertyFlags RequiredFlags; // 必需的内存属性
        VkMemoryPropertyFlags PreferredFlags;// 优选的内存属性
        bool bDedicated;                    // 强制使用独立分配
        bool bMappable;                     // 是否需要可映射
        
        FAllocationRequest()
            : Size(0), Alignment(1), MemoryTypeBits(~0u)
            , RequiredFlags(0), PreferredFlags(0)
            , bDedicated(false), bMappable(false) {}
    };
    
    /**
     * 分配 GPU 内存
     * @param Request 分配请求
     * @param OutAllocation 输出分配结果
     * @return 是否成功
     */
    bool Allocate(const FAllocationRequest& Request, FVulkanAllocation& OutAllocation);
    
    /**
     * 释放 GPU 内存
     * @param Allocation 要释放的分配
     */
    void Free(FVulkanAllocation& Allocation);
    
    /**
     * 映射内存
     * @param Allocation 要映射的分配
     * @param OutMappedPtr 输出映射指针
     * @return 是否成功
     */
    bool MapMemory(FVulkanAllocation& Allocation, void** OutMappedPtr);
    
    /**
     * 取消映射
     * @param Allocation 要取消映射的分配
     */
    void UnmapMemory(FVulkanAllocation& Allocation);
    
    /**
     * 查找合适的内存类型
     * @param TypeFilter 类型过滤位掩码
     * @param Properties 所需属性
     * @return 内存类型索引
     */
    uint32 FindMemoryType(uint32 TypeFilter, VkMemoryPropertyFlags Properties);
    
    /**
     * 内存统计信息
     */
    struct FMemoryStats {
        VkDeviceSize TotalAllocated;        // 总分配大小
        VkDeviceSize TotalReserved;         // 总保留大小 (从驱动)
        uint32 AllocationCount;             // 分配次数
        uint32 PoolCount;                   // 内存池数量
        VkDeviceSize LargestFreeBlock;      // 最大空闲块
        
        // 分类统计
        VkDeviceSize DeviceLocalAllocated;  // Device Local 内存
        VkDeviceSize HostVisibleAllocated;  // Host Visible 内存
        uint32 DedicatedAllocationCount;    // 独立分配数量
        
        FMemoryStats()
            : TotalAllocated(0), TotalReserved(0), AllocationCount(0), PoolCount(0)
            , LargestFreeBlock(0), DeviceLocalAllocated(0), HostVisibleAllocated(0)
            , DedicatedAllocationCount(0) {}
    };
    
    /**
     * 获取内存统计
     * @param OutStats 输出统计信息
     */
    void GetMemoryStats(FMemoryStats& OutStats);
    
    /**
     * 触发碎片整理 (所有池)
     */
    void DefragmentAll();
    
    /**
     * 释放未使用的池
     */
    void TrimUnusedPools();

private:
    // Vulkan 设备
    VkDevice Device;
    VkPhysicalDevice PhysicalDevice;
    VkPhysicalDeviceMemoryProperties MemoryProperties;
    
    // 内存池管理 (按内存类型组织)
    // Heaps[MemoryTypeIndex] = vector of pools for that type
    std::vector<TUniquePtr<FVulkanMemoryPool>> Pools[VK_MAX_MEMORY_TYPES];
    std::mutex PoolsMutex[VK_MAX_MEMORY_TYPES];  // 每个类型独立的锁
    
    // 统计追踪
    std::atomic<uint32> TotalAllocationCount;
    std::atomic<uint32> DedicatedAllocationCount;
    std::atomic<VkDeviceSize> TotalAllocatedMemory;
    
    // 配置常量
    static constexpr VkDeviceSize DEFAULT_POOL_SIZE = 64 * 1024 * 1024;  // 64MB
    static constexpr VkDeviceSize LARGE_ALLOCATION_THRESHOLD = 16 * 1024 * 1024;  // 16MB
    static constexpr uint32 MAX_POOLS_PER_TYPE = 32;  // 每种类型最多 32 个池
    
    // 内部辅助函数
    
    /**
     * 查找或创建内存池
     * @param MemoryTypeIndex 内存类型
     * @param RequiredSize 需要的最小大小
     * @return 池指针，失败返回 nullptr
     */
    FVulkanMemoryPool* FindOrCreatePool(uint32 MemoryTypeIndex, VkDeviceSize RequiredSize);
    
    /**
     * 创建新的内存池
     * @param MemoryTypeIndex 内存类型
     * @param PoolSize 池大小
     * @return 新创建的池
     */
    FVulkanMemoryPool* CreatePool(uint32 MemoryTypeIndex, VkDeviceSize PoolSize);
    
    /**
     * 独立分配 (大对象)
     * @param Request 分配请求
     * @param OutAllocation 输出分配
     * @return 是否成功
     */
    bool AllocateDedicated(const FAllocationRequest& Request, FVulkanAllocation& OutAllocation);
    
    /**
     * 释放独立分配
     * @param Allocation 要释放的分配
     */
    void FreeDedicated(FVulkanAllocation& Allocation);
    
    /**
     * 检查内存类型是否为 Host Visible
     * @param MemoryTypeIndex 内存类型索引
     * @return 是否可映射
     */
    bool IsHostVisibleMemoryType(uint32 MemoryTypeIndex) const;
};

} // namespace RHI
} // namespace MonsterRender
