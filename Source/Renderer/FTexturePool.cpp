// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Texture Pool Implementation

#include "Renderer/FTextureStreamingManager.h"
#include "Core/HAL/FMemory.h"
#include "Core/Log.h"
#include <algorithm>

namespace MonsterRender {

// ===== FTexturePool Implementation =====

FTexturePool::FTexturePool(SIZE_T PoolSizeBytes)
    : PoolMemory(nullptr)
    , TotalSize(PoolSizeBytes)
    , UsedSize(0)
    , FreeList(nullptr)
{
    // Allocate pool memory
    PoolMemory = FMemory::Malloc(PoolSizeBytes, 256);  // 256-byte alignment for GPU
    if (!PoolMemory) {
        MR_LOG_ERROR("Failed to allocate texture pool: " + std::to_string(PoolSizeBytes / 1024 / 1024) + "MB");
        return;
    }

    // Initialize with one large free region
    FreeList = new FFreeRegion();
    FreeList->Offset = 0;
    FreeList->Size = PoolSizeBytes;
    FreeList->Next = nullptr;

    MR_LOG_INFO("FTexturePool created: " + std::to_string(PoolSizeBytes / 1024 / 1024) + "MB");
}

FTexturePool::~FTexturePool() {
    // Free all regions in free list
    FFreeRegion* region = FreeList;
    while (region) {
        FFreeRegion* next = region->Next;
        delete region;
        region = next;
    }

    // Free pool memory
    if (PoolMemory) {
        FMemory::Free(PoolMemory);
        PoolMemory = nullptr;
    }

    MR_LOG_INFO("FTexturePool destroyed");
}

void* FTexturePool::Allocate(SIZE_T Size, SIZE_T Alignment) {
    if (Size == 0) return nullptr;

    std::scoped_lock lock(PoolMutex);

    // Align size up
    Size = (Size + Alignment - 1) & ~(Alignment - 1);

    // Try to allocate from free list (First-Fit algorithm)
    void* ptr = AllocateFromFreeList(Size, Alignment);
    if (ptr) {
        UsedSize += Size;
        return ptr;
    }

    MR_LOG_WARNING("FTexturePool::Allocate failed: out of memory (requested " + 
                   std::to_string(Size / 1024) + "KB, available " + 
                   std::to_string((TotalSize - UsedSize) / 1024) + "KB)");
    return nullptr;
}

void FTexturePool::Free(void* Ptr) {
    if (!Ptr) return;

    std::scoped_lock lock(PoolMutex);

    // Find allocation in map
    auto it = Allocations.find(Ptr);
    if (it == Allocations.end()) {
        MR_LOG_WARNING("FTexturePool::Free: pointer not found in allocations");
        return;
    }

    FAllocation alloc = it->second;
    Allocations.erase(it);

    // Add back to free list
    AddToFreeList(alloc.Offset, alloc.Size);
    UsedSize -= alloc.Size;

    MR_LOG_DEBUG("FTexturePool::Free: " + std::to_string(alloc.Size / 1024) + "KB freed");
}

SIZE_T FTexturePool::GetAllocationSize(void* Ptr) {
    if (!Ptr) return 0;

    std::scoped_lock lock(PoolMutex);

    auto it = Allocations.find(Ptr);
    if (it == Allocations.end()) {
        return 0;
    }

    return it->second.Size;
}

void FTexturePool::Compact() {
    std::scoped_lock lock(PoolMutex);
    
    MergeFreeRegions();
    
    MR_LOG_INFO("FTexturePool::Compact completed");
}

// Private methods

void* FTexturePool::AllocateFromFreeList(SIZE_T Size, SIZE_T Alignment) {
    FFreeRegion* prev = nullptr;
    FFreeRegion* region = FreeList;

    while (region) {
        // Calculate aligned offset
        SIZE_T alignedOffset = (region->Offset + Alignment - 1) & ~(Alignment - 1);
        SIZE_T padding = alignedOffset - region->Offset;

        // Check if region is large enough
        if (region->Size >= Size + padding) {
            // Calculate allocation pointer
            void* ptr = static_cast<uint8*>(PoolMemory) + alignedOffset;

            // Store allocation info
            FAllocation alloc;
            alloc.Offset = alignedOffset;
            alloc.Size = Size;
            Allocations[ptr] = alloc;

            // Update or remove free region
            SIZE_T remainingSize = region->Size - (Size + padding);
            
            if (remainingSize > 64) {  // Keep region if >64 bytes remain
                region->Offset = alignedOffset + Size;
                region->Size = remainingSize;
            } else {
                // Remove region from list
                if (prev) {
                    prev->Next = region->Next;
                } else {
                    FreeList = region->Next;
                }
                delete region;
            }

            return ptr;
        }

        prev = region;
        region = region->Next;
    }

    return nullptr;  // No suitable region found
}

void FTexturePool::AddToFreeList(SIZE_T Offset, SIZE_T Size) {
    // Create new free region
    FFreeRegion* newRegion = new FFreeRegion();
    newRegion->Offset = Offset;
    newRegion->Size = Size;
    newRegion->Next = nullptr;

    // Insert sorted by offset
    if (!FreeList || FreeList->Offset > Offset) {
        // Insert at head
        newRegion->Next = FreeList;
        FreeList = newRegion;
    } else {
        // Find insertion point
        FFreeRegion* current = FreeList;
        while (current->Next && current->Next->Offset < Offset) {
            current = current->Next;
        }
        newRegion->Next = current->Next;
        current->Next = newRegion;
    }

    // Try to merge with adjacent regions
    MergeFreeRegions();
}

void FTexturePool::MergeFreeRegions() {
    FFreeRegion* current = FreeList;

    while (current && current->Next) {
        // Check if current and next are adjacent
        if (current->Offset + current->Size == current->Next->Offset) {
            // Merge with next region
            FFreeRegion* next = current->Next;
            current->Size += next->Size;
            current->Next = next->Next;
            delete next;
            // Don't advance - check if we can merge again
        } else {
            current = current->Next;
        }
    }
}

} // namespace MonsterRender

