// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Vulkan Memory Manager Implementation
//
// Reference: UE5 Engine/Source/Runtime/VulkanRHI/Private/VulkanMemory.cpp

#include "Platform/Vulkan/FVulkanMemoryManager.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include "Core/HAL/FMemory.h"
#include "Core/Log.h"
#include <algorithm>

namespace MonsterRender {
namespace RHI {

using namespace Vulkan;

// ============================================================================
// FVulkanMemoryPool Implementation
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
    
    // Allocate large block from Vulkan
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = PoolSize;
    allocInfo.memoryTypeIndex = MemoryTypeIndex;
    
    VkResult result = functions.vkAllocateMemory(Device, &allocInfo, nullptr, &DeviceMemory);
    if (result != VK_SUCCESS) {
        MR_LOG_ERROR("FVulkanMemoryPool: Failed to allocate " + std::to_string(PoolSize / (1024 * 1024)) + 
                     "MB VkResult: " + std::to_string(result));
        return;
    }
    
    // For Host Visible memory, perform persistent mapping
    if (bHostVisible) {
        result = functions.vkMapMemory(Device, DeviceMemory, 0, PoolSize, 0, &PersistentMappedPtr);
        if (result != VK_SUCCESS) {
            MR_LOG_WARNING("FVulkanMemoryPool: Failed to map memory");
            PersistentMappedPtr = nullptr;
        }
    }
    
    // Initialize Free-List: entire pool as one large free block using FMemory
    FreeList = FMemory::New<FMemoryBlock>(0, PoolSize, true);
    
    MR_LOG_INFO("FVulkanMemoryPool: Created " + std::to_string(PoolSize / (1024 * 1024)) + 
                "MB pool (Type Index: " + std::to_string(MemoryTypeIndex) + 
                (bHostVisible ? ", Host" : ", Device") + ")");
}

FVulkanMemoryPool::~FVulkanMemoryPool()
{
    const auto& functions = VulkanAPI::getFunctions();
    
    // Free all block nodes
FMemoryBlock* current = FreeList;
    while (current) {
        FMemoryBlock* next = current->Next;
        FMemory::Delete(current);
        current = next;
    }
    FreeList = nullptr;
    
    // Unmap memory
if (PersistentMappedPtr) {
        functions.vkUnmapMemory(Device, DeviceMemory);
        PersistentMappedPtr = nullptr;
    }
    
    // Free Vulkan memory
    if (DeviceMemory != VK_NULL_HANDLE) {
        functions.vkFreeMemory(Device, DeviceMemory, nullptr);
        DeviceMemory = VK_NULL_HANDLE;
    }
    
    MR_LOG_DEBUG("FVulkanMemoryPool: Destroyed " + std::to_string(PoolSize / (1024 * 1024)) + "MB pool");
}

bool FVulkanMemoryPool::Allocate(VkDeviceSize Size, VkDeviceSize Alignment, FVulkanAllocation& OutAllocation)
{
    std::lock_guard<std::mutex> lock(PoolMutex);
    
    // Use First-Fit algorithm to find suitable free block
    FMemoryBlock* current = FreeList;
    FMemoryBlock* prev = nullptr;
    
    while (current) {
        if (current->bFree) {
            // Calculate aligned offset
VkDeviceSize alignedOffset = (current->Offset + Alignment - 1) & ~(Alignment - 1);
            VkDeviceSize padding = alignedOffset - current->Offset;
            VkDeviceSize requiredSize = padding + Size;
            
            if (requiredSize <= current->Size) {
                // Found suitable block, split it if necessary
                
                // Step 1: Create padding block if needed
                if (padding > 0) {
                    FMemoryBlock* paddingBlock = FMemory::New<FMemoryBlock>(current->Offset, padding, true);
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
                
                // Step 2: Split remainder block if allocated size is smaller than free block
                if (current->Size > Size) {
                    FMemoryBlock* remainder = FMemory::New<FMemoryBlock>(
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
                
                // Step 3: Mark block as used
                current->bFree = false;
                UsedSize.fetch_add(current->Size);
                
                // Step 4: Fill allocation structure
                OutAllocation.DeviceMemory = DeviceMemory;
                OutAllocation.Offset = current->Offset;
                OutAllocation.Size = current->Size;
                OutAllocation.MemoryTypeIndex = MemoryTypeIndex;
                OutAllocation.MappedPointer = nullptr;
                OutAllocation.bDedicated = false;
                OutAllocation.bMapped = false;
                OutAllocation.Pool = this;
                OutAllocation.AllocationHandle = current;
                
                // Set mapped pointer for host visible memory
                if (PersistentMappedPtr) {
                    OutAllocation.MappedPointer = static_cast<uint8*>(PersistentMappedPtr) + current->Offset;
                    OutAllocation.bMapped = true;
                }
                
                MR_LOG_DEBUG("FVulkanMemoryPool: Sub-allocated " + std::to_string(Size / 1024) + 
                             "KB (offset: " + std::to_string(current->Offset) + 
                             ", utilization: " + std::to_string(UsedSize.load() * 100 / PoolSize) + "%)");
                
                return true;
            }
        }
        
        prev = current;
        current = current->Next;
    }
    
    // No suitable block found
    MR_LOG_DEBUG("FVulkanMemoryPool: Cannot allocate " + std::to_string(Size / 1024) + "KB (pool full)");
    return false;
}

void FVulkanMemoryPool::Free(const FVulkanAllocation& Allocation)
{
    std::lock_guard<std::mutex> lock(PoolMutex);
    
    FMemoryBlock* block = static_cast<FMemoryBlock*>(Allocation.AllocationHandle);
    if (!block) {
        MR_LOG_ERROR("FVulkanMemoryPool::Free: Invalid block pointer");
        return;
    }
    
    if (block->bFree) {
        MR_LOG_WARNING("FVulkanMemoryPool::Free: Trying to free already free block");
        return;
    }
    
    // Mark block as free
    block->bFree = true;
    UsedSize.fetch_sub(block->Size);
    
    MR_LOG_DEBUG("FVulkanMemoryPool: Freed " + std::to_string(block->Size / 1024) + 
                 "KB (utilization: " + std::to_string(UsedSize.load() * 100 / PoolSize) + "%)");
    
    // Merge adjacent free blocks to reduce fragmentation
    MergeFreeBlocks();
}

bool FVulkanMemoryPool::Map(FVulkanAllocation& Allocation, void** OutMappedPtr)
{
    if (!bHostVisible) {
        MR_LOG_ERROR("FVulkanMemoryPool::Map: Trying to map Device-local memory");
        return false;
    }
    
    // Use persistent mapping if available
    if (PersistentMappedPtr) {
        *OutMappedPtr = static_cast<uint8*>(PersistentMappedPtr) + Allocation.Offset;
        Allocation.MappedPointer = *OutMappedPtr;
        Allocation.bMapped = true;
        return true;
    }
    
    // Fallback to one-time mapping
    const auto& functions = VulkanAPI::getFunctions();
    void* mappedPtr = nullptr;
    VkResult result = functions.vkMapMemory(Device, DeviceMemory, Allocation.Offset, Allocation.Size, 0, &mappedPtr);
    
    if (result != VK_SUCCESS) {
        MR_LOG_ERROR("FVulkanMemoryPool::Map: Map failed, VkResult: " + std::to_string(result));
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
    
    // Persistent mapping: don't actually unmap
    if (PersistentMappedPtr) {
        Allocation.MappedPointer = nullptr;
        Allocation.bMapped = false;
        return;
    }
    
    // One-time mapping: actually unmap from Vulkan
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
    // Iterate through free list and merge adjacent free blocks
    FMemoryBlock* current = FreeList;
    
    while (current && current->Next) {
        if (current->bFree && current->Next->bFree) {
            // Merge current and Next blocks
            FMemoryBlock* next = current->Next;
            current->Size += next->Size;
            current->Next = next->Next;
            
            if (next->Next) {
                next->Next->Prev = current;
            }
            
            FMemory::Delete(next);
            // Continue from current block (don't advance)
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
// FVulkanMemoryManager Implementation (Main Manager)
// ============================================================================

FVulkanMemoryManager::FVulkanMemoryManager(VkDevice InDevice, VkPhysicalDevice InPhysicalDevice)
    : Device(InDevice)
    , PhysicalDevice(InPhysicalDevice)
    , TotalAllocationCount(0)
    , DedicatedAllocationCount(0)
    , TotalAllocatedMemory(0)
{
    const auto& functions = VulkanAPI::getFunctions();
    
    // Query physical device memory properties
    functions.vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &MemoryProperties);
    
    MR_LOG_INFO("=====================================");
    MR_LOG_INFO("FVulkanMemoryManager: Initialized");
    MR_LOG_INFO("  Memory types: " + std::to_string(MemoryProperties.memoryTypeCount));
    MR_LOG_INFO("  Memory heaps: " + std::to_string(MemoryProperties.memoryHeapCount));
    MR_LOG_INFO("-------------------------------------");
    
    // 
    for (uint32 i = 0; i < MemoryProperties.memoryHeapCount; ++i) {
        const auto& heap = MemoryProperties.memoryHeaps[i];
        String heapType = (heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) ? "DeviceLocal" : "HostVisible";
        MR_LOG_INFO("  Heap " + std::to_string(i) + ": " + 
                    std::to_string(heap.size / (1024 * 1024)) + "MB [" + heapType + "]");
    }
    
    MR_LOG_INFO("-------------------------------------");
    
    // 
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
            
        MR_LOG_INFO("  Type " + std::to_string(i) + " (heap " + 
                    std::to_string(memType.heapIndex) + "): " + properties);
    }
    
    MR_LOG_INFO("=====================================");
}

FVulkanMemoryManager::~FVulkanMemoryManager()
{
    // 
    for (uint32 i = 0; i < VK_MAX_MEMORY_TYPES; ++i) {
        std::lock_guard<std::mutex> lock(PoolsMutex[i]);
        Pools[i].clear();
    }
    
    MR_LOG_INFO("FVulkanMemoryManager: Destroyed");
    MR_LOG_INFO("  Total allocations: " + std::to_string(TotalAllocationCount.load()));
    MR_LOG_INFO("  Dedicated allocations: " + std::to_string(DedicatedAllocationCount.load()));
    MR_LOG_INFO("  Total memory: " + std::to_string(TotalAllocatedMemory.load() / (1024 * 1024)) + "MB");
}

bool FVulkanMemoryManager::Allocate(const FAllocationRequest& Request, FVulkanAllocation& OutAllocation)
{
    // Find suitable memory type index
    uint32 memoryTypeIndex = FindMemoryType(Request.MemoryTypeBits, Request.RequiredFlags);
    
    // Using dedicated allocation
    // : 1.   2.  (>= 16MB)  3. 
    bool bUseDedicated = Request.bDedicated || (Request.Size >= LARGE_ALLOCATION_THRESHOLD);
    
    if (bUseDedicated) {
        MR_LOG_DEBUG("FVulkanMemoryManager: Using dedicated allocation (size: " + 
                     std::to_string(Request.Size / (1024 * 1024)) + "MB)");
        
        bool result = AllocateDedicated(Request, OutAllocation);
        if (result) {
            TotalAllocationCount.fetch_add(1);
            TotalAllocatedMemory.fetch_add(Request.Size);
        }
        return result;
    }
    
    // Try to allocate from existing pools
    {
        std::lock_guard<std::mutex> lock(PoolsMutex[memoryTypeIndex]);
        
        for (auto& pool : Pools[memoryTypeIndex]) {
            if (pool->Allocate(Request.Size, Request.Alignment, OutAllocation)) {
                TotalAllocationCount.fetch_add(1);
                TotalAllocatedMemory.fetch_add(Request.Size);
                
                MR_LOG_DEBUG("FVulkanMemoryManager: Allocated from existing pool " + 
                             std::to_string(Request.Size / 1024) + "KB");
                return true;
            }
        }
    }
    
    // No existing pool can accommodate: create new pool
    VkDeviceSize poolSize = std::max<VkDeviceSize>(DEFAULT_POOL_SIZE, Request.Size * 2);
    FVulkanMemoryPool* newPool = FindOrCreatePool(memoryTypeIndex, poolSize);
    
    if (!newPool) {
        MR_LOG_ERROR("FVulkanMemoryManager: Failed to create new pool");
        return false;
    }
    
    // Allocated from new pool
    if (newPool->Allocate(Request.Size, Request.Alignment, OutAllocation)) {
        TotalAllocationCount.fetch_add(1);
        TotalAllocatedMemory.fetch_add(Request.Size);
        
        MR_LOG_DEBUG("FVulkanMemoryManager: Allocated from new pool " + 
                     std::to_string(Request.Size / 1024) + "KB");
        return true;
    }
    
    MR_LOG_ERROR("FVulkanMemoryManager: Allocation failed");
    return false;
}

void FVulkanMemoryManager::Free(FVulkanAllocation& Allocation)
{
    if (!Allocation.IsValid()) {
        MR_LOG_WARNING("FVulkanMemoryManager::Free: Trying to free invalid allocation");
        return;
    }
    
    TotalAllocatedMemory.fetch_sub(Allocation.Size);
    
    if (Allocation.bDedicated) {
        FreeDedicated(Allocation);
    } else {
        if (Allocation.Pool) {
            Allocation.Pool->Free(Allocation);
        } else {
            MR_LOG_ERROR("FVulkanMemoryManager::Free: Invalid pool pointer");
        }
    }
    
    // Clear allocation structure
    Allocation = FVulkanAllocation{};
}

bool FVulkanMemoryManager::MapMemory(FVulkanAllocation& Allocation, void** OutMappedPtr)
{
    if (!Allocation.IsValid()) {
        MR_LOG_ERROR("FVulkanMemoryManager::MapMemory: Invalid allocation");
        return false;
    }
    
    if (Allocation.bMapped) {
        *OutMappedPtr = Allocation.MappedPointer;
        return true;
    }
    
    if (Allocation.bDedicated) {
        // 
        const auto& functions = VulkanAPI::getFunctions();
        VkResult result = functions.vkMapMemory(Device, Allocation.DeviceMemory, 
                                                Allocation.Offset, Allocation.Size, 0, OutMappedPtr);
        
        if (result != VK_SUCCESS) {
            MR_LOG_ERROR("FVulkanMemoryManager::MapMemory: Map failed, VkResult: " + std::to_string(result));
            return false;
        }
        
        Allocation.MappedPointer = *OutMappedPtr;
        Allocation.bMapped = true;
        return true;
    } else {
        // Delegate to pool for pooled allocations
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
    
    MR_LOG_ERROR("FVulkanMemoryManager: Cannot find suitable memory type");
    return 0;
}

void FVulkanMemoryManager::GetMemoryStats(FMemoryStats& OutStats)
{
    OutStats = FMemoryStats{};
    
    OutStats.AllocationCount = TotalAllocationCount.load();
    OutStats.DedicatedAllocationCount = DedicatedAllocationCount.load();
    OutStats.TotalAllocated = TotalAllocatedMemory.load();
    
    // Calculate pool statistics
    for (uint32 i = 0; i < VK_MAX_MEMORY_TYPES; ++i) {
        std::lock_guard<std::mutex> lock(PoolsMutex[i]);
        
        for (const auto& pool : Pools[i]) {
            OutStats.TotalReserved += pool->GetPoolSize();
            OutStats.PoolCount++;
            
            VkDeviceSize freeSize = pool->GetFreeSize();
            if (freeSize > OutStats.LargestFreeBlock) {
                OutStats.LargestFreeBlock = freeSize;
            }
            
            // Categorize by memory type
            if (pool->IsHostVisible()) {
                OutStats.HostVisibleAllocated += pool->GetUsedSize();
            } else {
                OutStats.DeviceLocalAllocated += pool->GetUsedSize();
            }
        }
    }
    
    MR_LOG_DEBUG("===== FVulkanMemoryManager Stats =====");
    MR_LOG_DEBUG("  Total allocated: " + std::to_string(OutStats.TotalAllocated / (1024 * 1024)) + "MB");
    MR_LOG_DEBUG("  Total reserved: " + std::to_string(OutStats.TotalReserved / (1024 * 1024)) + "MB");
    MR_LOG_DEBUG("  Pool count: " + std::to_string(OutStats.PoolCount));
    MR_LOG_DEBUG("  Dedicated allocations: " + std::to_string(OutStats.DedicatedAllocationCount));
    MR_LOG_DEBUG("  DeviceLocal: " + std::to_string(OutStats.DeviceLocalAllocated / (1024 * 1024)) + "MB");
    MR_LOG_DEBUG("  HostVisible: " + std::to_string(OutStats.HostVisibleAllocated / (1024 * 1024)) + "MB");
    MR_LOG_DEBUG("=======================================");
}

void FVulkanMemoryManager::DefragmentAll()
{
    MR_LOG_INFO("FVulkanMemoryManager: Starting defragmentation...");
    
    for (uint32 i = 0; i < VK_MAX_MEMORY_TYPES; ++i) {
        std::lock_guard<std::mutex> lock(PoolsMutex[i]);
        
        for (auto& pool : Pools[i]) {
            pool->Defragment();
        }
    }
    
    MR_LOG_INFO("FVulkanMemoryManager: Defragmentation completed");
}

void FVulkanMemoryManager::TrimUnusedPools()
{
    MR_LOG_INFO("FVulkanMemoryManager: Trimming unused pools...");
    
    uint32 trimmedCount = 0;
    
    for (uint32 i = 0; i < VK_MAX_MEMORY_TYPES; ++i) {
        std::lock_guard<std::mutex> lock(PoolsMutex[i]);
        
        // Remove completely unused pools
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
    
    MR_LOG_INFO("FVulkanMemoryManager: Trimmed " + std::to_string(trimmedCount) + " unused pools");
}

FVulkanMemoryPool* FVulkanMemoryManager::FindOrCreatePool(uint32 MemoryTypeIndex, VkDeviceSize RequiredSize)
{
    std::lock_guard<std::mutex> lock(PoolsMutex[MemoryTypeIndex]);
    
    // Check if pool count exceeds limit
    if (static_cast<uint32>(Pools[MemoryTypeIndex].Num()) >= MAX_POOLS_PER_TYPE) {
        MR_LOG_WARNING("FVulkanMemoryManager: Memory type " + std::to_string(MemoryTypeIndex) + 
                       " pool count reached limit (" + std::to_string(MAX_POOLS_PER_TYPE) + ")");
        return nullptr;
    }
    
    // Create new pool
    return CreatePool(MemoryTypeIndex, RequiredSize);
}

FVulkanMemoryPool* FVulkanMemoryManager::CreatePool(uint32 MemoryTypeIndex, VkDeviceSize PoolSize)
{
    bool bHostVisible = IsHostVisibleMemoryType(MemoryTypeIndex);
    
    auto newPool = MakeUnique<FVulkanMemoryPool>(Device, PoolSize, MemoryTypeIndex, bHostVisible);
    
    if (newPool->GetDeviceMemory() == VK_NULL_HANDLE) {
        MR_LOG_ERROR("FVulkanMemoryManager: Failed to create pool");
        return nullptr;
    }
    
    Pools[MemoryTypeIndex].Add(std::move(newPool));
    return Pools[MemoryTypeIndex].Last().get();
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
        MR_LOG_ERROR("FVulkanMemoryManager: Dedicated allocation failed, VkResult: " + std::to_string(result));
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
    
    MR_LOG_INFO("FVulkanMemoryManager: Dedicated allocation " + 
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
    
    MR_LOG_DEBUG("FVulkanMemoryManager: Freed dedicated allocation (" + 
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
