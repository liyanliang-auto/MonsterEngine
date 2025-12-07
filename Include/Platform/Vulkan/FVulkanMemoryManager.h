// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Vulkan Memory Manager (UE5-style)
//
// Reference: UE5 Engine/Source/Runtime/VulkanRHI/Private/VulkanMemory.h
// Three-tier architecture: FVulkanMemoryManager -> FVulkanMemoryPool -> FVulkanAllocation

#pragma once

#include "Core/CoreTypes.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include "Containers/Array.h"
#include <mutex>
#include <atomic>

namespace MonsterRender {
namespace RHI {

// Forward declaration
class FVulkanMemoryPool;

/**
 * FVulkanAllocation - GPU Memory Allocation Result
 * 
 * Represents a specific memory allocation, which can be:
 * - Sub-allocation: Block allocated from memory pool
 * - Dedicated: Independent VkDeviceMemory object
 * 
 * Reference: UE5 FVulkanResourceAllocation
 */
struct FVulkanAllocation {
    VkDeviceMemory DeviceMemory;        // Vulkan memory handle
    VkDeviceSize Offset;                // Offset in DeviceMemory
    VkDeviceSize Size;                  // Allocation size
    uint32 MemoryTypeIndex;             // Memory type index
    void* MappedPointer;                // Mapped CPU pointer (if mappable)
    bool bDedicated;                    // Whether dedicated allocation
    bool bMapped;                       // Whether mapped
    
    // Internal tracking (for sub-allocation)
    FVulkanMemoryPool* Pool;            // Owning memory pool
    void* AllocationHandle;             // Internal allocation handle (points to FMemoryBlock)             
    
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
    
    // Check if allocation is valid
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
 * ?
 * - ?(< 16MB): ?
 * 
 */
class FVulkanMemoryManager {
public:
    /**
     */
    FVulkanMemoryManager(VkDevice Device, VkPhysicalDevice PhysicalDevice);
    ~FVulkanMemoryManager();
    
    // 
    FVulkanMemoryManager(const FVulkanMemoryManager&) = delete;
    FVulkanMemoryManager& operator=(const FVulkanMemoryManager&) = delete;
    
    /**
     * Memory allocation request parameters
     */
    struct FAllocationRequest {
        VkDeviceSize Size;
        VkDeviceSize Alignment;
        uint32 MemoryTypeBits;
        VkMemoryPropertyFlags RequiredFlags;
        VkMemoryPropertyFlags PreferredFlags;
        bool bDedicated;
        bool bMappable;
        
        FAllocationRequest()
            : Size(0)
            , Alignment(1)
            , MemoryTypeBits(~0u)
            , RequiredFlags(0)
            , PreferredFlags(0)
            , bDedicated(false)
            , bMappable(false)
        {}
    };
    
    /**
     *  GPU 
     * @param Request 
     * @param OutAllocation 
     */
    bool Allocate(const FAllocationRequest& Request, FVulkanAllocation& OutAllocation);
    
    /**
     *  GPU 
     * @param Allocation 
     */
    void Free(FVulkanAllocation& Allocation);
    
    /**
     * 
     * @param Allocation 
     * @param OutMappedPtr 
     */
    bool MapMemory(FVulkanAllocation& Allocation, void** OutMappedPtr);
    
    /**
     * 
     * @param Allocation 
     */
    void UnmapMemory(FVulkanAllocation& Allocation);
    
    /**
     * @param TypeFilter ?
     */
    uint32 FindMemoryType(uint32 TypeFilter, VkMemoryPropertyFlags Properties);
    
    /**
     * Memory usage statistics
     */
    struct FMemoryStats {
        VkDeviceSize TotalAllocated;
        VkDeviceSize TotalReserved;
        uint32 AllocationCount;
        uint32 PoolCount;
        VkDeviceSize LargestFreeBlock;
        VkDeviceSize DeviceLocalAllocated;
        VkDeviceSize HostVisibleAllocated;
        uint32 DedicatedAllocationCount;
        
        FMemoryStats()
            : TotalAllocated(0)
            , TotalReserved(0)
            , AllocationCount(0)
            , PoolCount(0)
            , LargestFreeBlock(0)
            , DeviceLocalAllocated(0)
            , HostVisibleAllocated(0)
            , DedicatedAllocationCount(0)
        {}
    };
    
    /**
     */
    void GetMemoryStats(FMemoryStats& OutStats);
    
    /**
     */
    void DefragmentAll();
    
    /**
     */
    void TrimUnusedPools();

private:
    VkDevice Device;
    VkPhysicalDevice PhysicalDevice;
    VkPhysicalDeviceMemoryProperties MemoryProperties;
    
    // Heaps[MemoryTypeIndex] = array of pools for that type
    TArray<TUniquePtr<FVulkanMemoryPool>> Pools[VK_MAX_MEMORY_TYPES];
    
    std::atomic<uint32> TotalAllocationCount;
    std::atomic<uint32> DedicatedAllocationCount;
    std::atomic<VkDeviceSize> TotalAllocatedMemory;
    
    // Per-type pool mutex
    std::mutex PoolsMutex[VK_MAX_MEMORY_TYPES];
    
    // Configuration constants
    static constexpr VkDeviceSize DEFAULT_POOL_SIZE = 64 * 1024 * 1024;  // 64MB
    static constexpr VkDeviceSize LARGE_ALLOCATION_THRESHOLD = 16 * 1024 * 1024;  // 16MB
    static constexpr uint32 MAX_POOLS_PER_TYPE = 32;  // Maximum pools per memory type
    
    // Helper methods
    
    /**
     * @param MemoryTypeIndex 
     * @return  nullptr
     */
    FVulkanMemoryPool* FindOrCreatePool(uint32 MemoryTypeIndex, VkDeviceSize RequiredSize);
    
    /**
     * ?
     * @param MemoryTypeIndex 
     * @return ?
     */
    FVulkanMemoryPool* CreatePool(uint32 MemoryTypeIndex, VkDeviceSize PoolSize);
    
    /**
     * @param Request 
     * @param OutAllocation 
     */
    bool AllocateDedicated(const FAllocationRequest& Request, FVulkanAllocation& OutAllocation);
    
    /**
     * @param Allocation 
     */
    void FreeDedicated(FVulkanAllocation& Allocation);
    
    /**
     */
    bool IsHostVisibleMemoryType(uint32 MemoryTypeIndex) const;
};

} // namespace RHI
} // namespace MonsterRender
