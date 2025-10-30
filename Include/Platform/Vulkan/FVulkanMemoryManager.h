// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Vulkan Memory Manager (UE5-style)

#pragma once

#include "Core/CoreTypes.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include <mutex>
#include <vector>

namespace MonsterRender {
namespace RHI {

/**
 * FVulkanMemoryManager - GPU memory allocator for Vulkan
 * 
 * Reference: UE5 Engine/Source/Runtime/VulkanRHI/Private/VulkanMemory.h
 * 
 * Manages Vulkan device memory allocation using buddy allocator or free lists
 * Provides sub-allocation to reduce allocation overhead
 */
class FVulkanMemoryManager {
public:
    FVulkanMemoryManager(VkDevice Device, VkPhysicalDevice PhysicalDevice);
    ~FVulkanMemoryManager();

    /**
     * Allocation request structure
     */
    struct FAllocationRequest {
        VkDeviceSize Size;
        VkDeviceSize Alignment;
        uint32 MemoryTypeBits;
        VkMemoryPropertyFlags RequiredFlags;
        VkMemoryPropertyFlags PreferredFlags;
        bool bDedicated;  // Request dedicated allocation for large resources
    };

    /**
     * Allocated memory handle
     */
    struct FAllocation {
        VkDeviceMemory DeviceMemory;
        VkDeviceSize Offset;
        VkDeviceSize Size;
        uint32 MemoryTypeIndex;
        void* MappedPointer;  // Non-null if memory is mappable and mapped
        bool bDedicated;
        
        // Internal tracking
        class FMemoryHeap* Heap;
        class FMemoryBlock* Block;
    };

    // Allocation/Free
    bool Allocate(const FAllocationRequest& Request, FAllocation& OutAllocation);
    void Free(FAllocation& Allocation);

    // Memory type selection
    uint32 FindMemoryType(uint32 TypeFilter, VkMemoryPropertyFlags Properties);

    // Statistics
    struct FMemoryStats {
        VkDeviceSize TotalAllocated;     // Total GPU memory allocated
        VkDeviceSize TotalReserved;      // Total reserved from driver
        uint32 AllocationCount;          // Number of allocations
        uint32 HeapCount;                // Number of heaps
        VkDeviceSize LargestFreeBlock;   // Largest free block
    };

    void GetMemoryStats(FMemoryStats& OutStats);

    // Defragmentation hint
    void Compact();

private:
    /**
     * Memory block within a heap (for sub-allocation)
     */
    class FMemoryBlock {
    public:
        FMemoryBlock(VkDeviceSize Offset, VkDeviceSize Size, bool bFree)
            : Offset(Offset), Size(Size), bFree(bFree), Next(nullptr), Prev(nullptr) {}

        VkDeviceSize Offset;
        VkDeviceSize Size;
        bool bFree;
        FMemoryBlock* Next;
        FMemoryBlock* Prev;
    };

    /**
     * Memory heap (VkDeviceMemory + sub-allocation list)
     */
    class FMemoryHeap {
    public:
        FMemoryHeap(VkDevice Device, VkDeviceSize Size, uint32 MemoryTypeIndex);
        ~FMemoryHeap();

        bool Allocate(VkDeviceSize Size, VkDeviceSize Alignment, FAllocation& OutAllocation);
        void Free(const FAllocation& Allocation);

        VkDeviceMemory GetDeviceMemory() const { return DeviceMemory; }
        VkDeviceSize GetSize() const { return HeapSize; }
        VkDeviceSize GetUsedSize() const { return UsedSize; }

    private:
        VkDevice Device;
        VkDeviceMemory DeviceMemory;
        VkDeviceSize HeapSize;
        VkDeviceSize UsedSize;
        uint32 MemoryTypeIndex;

        FMemoryBlock* FreeList;
        std::mutex HeapMutex;

        void MergeFreeBlocks();
    };

    // Device handles
    VkDevice Device;
    VkPhysicalDevice PhysicalDevice;
    VkPhysicalDeviceMemoryProperties MemoryProperties;

    // Heaps per memory type
    std::vector<TUniquePtr<FMemoryHeap>> Heaps[VK_MAX_MEMORY_TYPES];
    std::mutex HeapsMutex[VK_MAX_MEMORY_TYPES];

    // Configuration
    static constexpr VkDeviceSize DEFAULT_HEAP_SIZE = 64 * 1024 * 1024;  // 64MB
    static constexpr VkDeviceSize LARGE_ALLOCATION_THRESHOLD = 16 * 1024 * 1024;  // 16MB

    // Helpers
    FMemoryHeap* FindOrCreateHeap(uint32 MemoryTypeIndex, VkDeviceSize RequiredSize);
    bool AllocateDedicated(const FAllocationRequest& Request, FAllocation& OutAllocation);
};

} // namespace RHI
} // namespace MonsterRender

