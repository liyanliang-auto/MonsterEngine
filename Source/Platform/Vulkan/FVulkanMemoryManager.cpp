// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Vulkan Memory Manager Implementation

#include "Platform/Vulkan/FVulkanMemoryManager.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include "Core/Log.h"
#include <algorithm>

namespace MonsterRender {
namespace RHI {

using namespace Vulkan;

// ============================================================================
// FMemoryHeap Implementation (Free-List Allocator)
// ============================================================================

FVulkanMemoryManager::FMemoryHeap::FMemoryHeap(VkDevice InDevice, VkDeviceSize Size, uint32 InMemoryTypeIndex)
    : Device(InDevice)
    , DeviceMemory(VK_NULL_HANDLE)
    , HeapSize(Size)
    , UsedSize(0)
    , MemoryTypeIndex(InMemoryTypeIndex)
    , FreeList(nullptr)
{
    const auto& functions = VulkanAPI::getFunctions();
    
    // Allocate large block from Vulkan
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = HeapSize;
    allocInfo.memoryTypeIndex = MemoryTypeIndex;
    
    VkResult result = functions.vkAllocateMemory(Device, &allocInfo, nullptr, &DeviceMemory);
    if (result != VK_SUCCESS) {
        MR_LOG_ERROR("FMemoryHeap: Failed to allocate " + std::to_string(HeapSize / (1024 * 1024)) + 
                     "MB heap, VkResult: " + std::to_string(result));
        return;
    }
    
    // Initialize free list with entire heap
    FreeList = new FMemoryBlock(0, HeapSize, true);
    
    MR_LOG_INFO("FMemoryHeap: Allocated " + std::to_string(HeapSize / (1024 * 1024)) + 
                "MB heap (Type: " + std::to_string(MemoryTypeIndex) + ")");
}

FVulkanMemoryManager::FMemoryHeap::~FMemoryHeap()
{
    const auto& functions = VulkanAPI::getFunctions();
    
    // Free all blocks
    FMemoryBlock* current = FreeList;
    while (current) {
        FMemoryBlock* next = current->Next;
        delete current;
        current = next;
    }
    FreeList = nullptr;
    
    // Free Vulkan memory
    if (DeviceMemory != VK_NULL_HANDLE) {
        functions.vkFreeMemory(Device, DeviceMemory, nullptr);
        DeviceMemory = VK_NULL_HANDLE;
    }
    
    MR_LOG_DEBUG("FMemoryHeap: Freed " + std::to_string(HeapSize / (1024 * 1024)) + "MB heap");
}

bool FVulkanMemoryManager::FMemoryHeap::Allocate(VkDeviceSize Size, VkDeviceSize Alignment, FAllocation& OutAllocation)
{
    std::lock_guard<std::mutex> lock(HeapMutex);
    
    // Find first fit free block
    FMemoryBlock* current = FreeList;
    FMemoryBlock* prev = nullptr;
    
    while (current) {
        if (current->bFree) {
            // Align offset
            VkDeviceSize alignedOffset = (current->Offset + Alignment - 1) & ~(Alignment - 1);
            VkDeviceSize padding = alignedOffset - current->Offset;
            VkDeviceSize requiredSize = padding + Size;
            
            if (requiredSize <= current->Size) {
                // Found suitable block!
                
                // Create padding block if needed
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
                
                // Split block if remainder is large enough
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
                
                // Mark as allocated
                current->bFree = false;
                UsedSize += current->Size;
                
                // Fill output
                OutAllocation.DeviceMemory = DeviceMemory;
                OutAllocation.Offset = current->Offset;
                OutAllocation.Size = current->Size;
                OutAllocation.MemoryTypeIndex = MemoryTypeIndex;
                OutAllocation.MappedPointer = nullptr;
                OutAllocation.bDedicated = false;
                OutAllocation.Heap = reinterpret_cast<FMemoryHeap*>(this);
                OutAllocation.Block = reinterpret_cast<FMemoryBlock*>(current);
                
                return true;
            }
        }
        
        prev = current;
        current = current->Next;
    }
    
    // No suitable block found
    return false;
}

void FVulkanMemoryManager::FMemoryHeap::Free(const FAllocation& Allocation)
{
    std::lock_guard<std::mutex> lock(HeapMutex);
    
    FMemoryBlock* block = reinterpret_cast<FMemoryBlock*>(Allocation.Block);
    if (!block) {
        MR_LOG_ERROR("FMemoryHeap::Free: Invalid block pointer");
        return;
    }
    
    // Mark as free
    block->bFree = true;
    UsedSize -= block->Size;
    
    // Merge with adjacent free blocks
    MergeFreeBlocks();
}

void FVulkanMemoryManager::FMemoryHeap::MergeFreeBlocks()
{
    FMemoryBlock* current = FreeList;
    
    while (current && current->Next) {
        if (current->bFree && current->Next->bFree) {
            // Merge with next block
            FMemoryBlock* next = current->Next;
            current->Size += next->Size;
            current->Next = next->Next;
            
            if (next->Next) {
                next->Next->Prev = current;
            }
            
            delete next;
            // Continue from current (might merge with multiple blocks)
        } else {
            current = current->Next;
        }
    }
}

// ============================================================================
// FVulkanMemoryManager Implementation
// ============================================================================

FVulkanMemoryManager::FVulkanMemoryManager(VkDevice InDevice, VkPhysicalDevice InPhysicalDevice)
    : Device(InDevice)
    , PhysicalDevice(InPhysicalDevice)
{
    const auto& functions = VulkanAPI::getFunctions();
    
    // Query physical device memory properties
    functions.vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &MemoryProperties);
    
    MR_LOG_INFO("FVulkanMemoryManager: Initialized");
    MR_LOG_INFO("  Memory Types: " + std::to_string(MemoryProperties.memoryTypeCount));
    MR_LOG_INFO("  Memory Heaps: " + std::to_string(MemoryProperties.memoryHeapCount));
    
    // Log memory heaps
    for (uint32 i = 0; i < MemoryProperties.memoryHeapCount; ++i) {
        const auto& heap = MemoryProperties.memoryHeaps[i];
        MR_LOG_INFO("  Heap " + std::to_string(i) + ": " + 
                    std::to_string(heap.size / (1024 * 1024)) + "MB " +
                    ((heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) ? "[DEVICE_LOCAL]" : "[HOST]"));
    }
}

FVulkanMemoryManager::~FVulkanMemoryManager()
{
    // Free all heaps
    for (uint32 i = 0; i < VK_MAX_MEMORY_TYPES; ++i) {
        std::lock_guard<std::mutex> lock(HeapsMutex[i]);
        Heaps[i].clear();
    }
    
    MR_LOG_INFO("FVulkanMemoryManager: Shutdown complete");
}

bool FVulkanMemoryManager::Allocate(const FAllocationRequest& Request, FAllocation& OutAllocation)
{
    // Find suitable memory type
    uint32 memoryTypeIndex = FindMemoryType(Request.MemoryTypeBits, Request.RequiredFlags);
    
    // Check if dedicated allocation is required
    bool bUseDedicated = Request.bDedicated || (Request.Size >= LARGE_ALLOCATION_THRESHOLD);
    
    if (bUseDedicated) {
        MR_LOG_DEBUG("FVulkanMemoryManager: Using dedicated allocation for " + 
                     std::to_string(Request.Size / (1024 * 1024)) + "MB");
        return AllocateDedicated(Request, OutAllocation);
    }
    
    // Try sub-allocation from existing heaps
    {
        std::lock_guard<std::mutex> lock(HeapsMutex[memoryTypeIndex]);
        
        for (auto& heap : Heaps[memoryTypeIndex]) {
            if (heap->Allocate(Request.Size, Request.Alignment, OutAllocation)) {
                MR_LOG_DEBUG("FVulkanMemoryManager: Sub-allocated " + 
                             std::to_string(Request.Size / 1024) + "KB from existing heap");
                return true;
            }
        }
    }
    
    // Create new heap
    VkDeviceSize heapSize = std::max(DEFAULT_HEAP_SIZE, Request.Size * 2);
    FMemoryHeap* newHeap = FindOrCreateHeap(memoryTypeIndex, heapSize);
    
    if (!newHeap) {
        MR_LOG_ERROR("FVulkanMemoryManager: Failed to create new heap");
        return false;
    }
    
    // Try allocation again
    if (newHeap->Allocate(Request.Size, Request.Alignment, OutAllocation)) {
        MR_LOG_DEBUG("FVulkanMemoryManager: Allocated " + 
                     std::to_string(Request.Size / 1024) + "KB from new heap");
        return true;
    }
    
    MR_LOG_ERROR("FVulkanMemoryManager: Allocation failed");
    return false;
}

void FVulkanMemoryManager::Free(FAllocation& Allocation)
{
    if (Allocation.bDedicated) {
        // Free dedicated allocation
        const auto& functions = VulkanAPI::getFunctions();
        
        if (Allocation.MappedPointer) {
            functions.vkUnmapMemory(Device, Allocation.DeviceMemory);
            Allocation.MappedPointer = nullptr;
        }
        
        functions.vkFreeMemory(Device, Allocation.DeviceMemory, nullptr);
        
        MR_LOG_DEBUG("FVulkanMemoryManager: Freed dedicated allocation (" + 
                     std::to_string(Allocation.Size / (1024 * 1024)) + "MB)");
    } else {
        // Free from heap
        if (Allocation.Heap) {
            reinterpret_cast<FMemoryHeap*>(Allocation.Heap)->Free(Allocation);
            MR_LOG_DEBUG("FVulkanMemoryManager: Freed " + 
                         std::to_string(Allocation.Size / 1024) + "KB from heap");
        } else {
            MR_LOG_ERROR("FVulkanMemoryManager::Free: Invalid heap pointer");
        }
    }
    
    // Clear allocation
    Allocation = FAllocation{};
}

uint32 FVulkanMemoryManager::FindMemoryType(uint32 TypeFilter, VkMemoryPropertyFlags Properties)
{
    for (uint32 i = 0; i < MemoryProperties.memoryTypeCount; ++i) {
        if ((TypeFilter & (1 << i)) && 
            (MemoryProperties.memoryTypes[i].propertyFlags & Properties) == Properties) {
            return i;
        }
    }
    
    MR_LOG_ERROR("FVulkanMemoryManager: Failed to find suitable memory type");
    return 0;
}

void FVulkanMemoryManager::GetMemoryStats(FMemoryStats& OutStats)
{
    OutStats = FMemoryStats{};
    
    for (uint32 i = 0; i < VK_MAX_MEMORY_TYPES; ++i) {
        std::lock_guard<std::mutex> lock(HeapsMutex[i]);
        
        for (const auto& heap : Heaps[i]) {
            OutStats.TotalReserved += heap->GetSize();
            OutStats.TotalAllocated += heap->GetUsedSize();
            OutStats.HeapCount++;
            
            // Find largest free block (simplified - would need to traverse free list)
            VkDeviceSize freeSpace = heap->GetSize() - heap->GetUsedSize();
            if (freeSpace > OutStats.LargestFreeBlock) {
                OutStats.LargestFreeBlock = freeSpace;
            }
        }
    }
    
    MR_LOG_DEBUG("FVulkanMemoryManager Stats:");
    MR_LOG_DEBUG("  Total Reserved: " + std::to_string(OutStats.TotalReserved / (1024 * 1024)) + "MB");
    MR_LOG_DEBUG("  Total Allocated: " + std::to_string(OutStats.TotalAllocated / (1024 * 1024)) + "MB");
    MR_LOG_DEBUG("  Heaps: " + std::to_string(OutStats.HeapCount));
}

void FVulkanMemoryManager::Compact()
{
    MR_LOG_INFO("FVulkanMemoryManager: Compaction requested (not yet implemented)");
    // TODO: Implement defragmentation
    // - Identify sparse heaps
    // - Move allocations
    // - Merge heaps
    // - Release empty heaps
}

FVulkanMemoryManager::FMemoryHeap* FVulkanMemoryManager::FindOrCreateHeap(uint32 MemoryTypeIndex, VkDeviceSize RequiredSize)
{
    std::lock_guard<std::mutex> lock(HeapsMutex[MemoryTypeIndex]);
    
    // Create new heap
    auto newHeap = MakeUnique<FMemoryHeap>(Device, RequiredSize, MemoryTypeIndex);
    
    if (newHeap->GetDeviceMemory() == VK_NULL_HANDLE) {
        MR_LOG_ERROR("FVulkanMemoryManager: Failed to create heap");
        return nullptr;
    }
    
    Heaps[MemoryTypeIndex].push_back(std::move(newHeap));
    return Heaps[MemoryTypeIndex].back().get();
}

bool FVulkanMemoryManager::AllocateDedicated(const FAllocationRequest& Request, FAllocation& OutAllocation)
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
        MR_LOG_ERROR("FVulkanMemoryManager: Dedicated allocation failed, VkResult: " + std::to_string(result));
        return false;
    }
    
    OutAllocation.DeviceMemory = dedicatedMemory;
    OutAllocation.Offset = 0;
    OutAllocation.Size = Request.Size;
    OutAllocation.MemoryTypeIndex = memoryTypeIndex;
    OutAllocation.MappedPointer = nullptr;
    OutAllocation.bDedicated = true;
    OutAllocation.Heap = nullptr;
    OutAllocation.Block = nullptr;
    
    MR_LOG_INFO("FVulkanMemoryManager: Dedicated allocation " + 
                std::to_string(Request.Size / (1024 * 1024)) + "MB");
    
    return true;
}

} // namespace RHI
} // namespace MonsterRender

