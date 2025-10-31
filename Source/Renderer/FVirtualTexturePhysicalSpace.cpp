// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Virtual Texture Physical Space Implementation

#include "Renderer/FVirtualTextureSystem.h"
#include "Core/Log.h"
#include <algorithm>

namespace MonsterRender {

// ===== FVirtualTexturePhysicalSpace Implementation =====

FVirtualTexturePhysicalSpace::FVirtualTexturePhysicalSpace(uint32 InPageSize, uint32 InNumPages)
    : PageSize(InPageSize)
    , NumPages(InNumPages)
    , CurrentFrame(0)
{
    // Allocate all physical pages
    Pages.resize(NumPages);
    
    for (uint32 i = 0; i < NumPages; ++i) {
        Pages[i].PhysicalAddress = i;
        Pages[i].PageData = FMemory::Malloc(PageSize * PageSize * 4);  // RGBA8
        
        if (!Pages[i].PageData) {
            MR_LOG_ERROR("Failed to allocate physical page " + std::to_string(i));
        }
        
        // Add to free list
        FreeList.push_back(i);
    }
    
    MR_LOG_INFO("FVirtualTexturePhysicalSpace created: " +
                std::to_string(NumPages) + " pages x " +
                std::to_string(PageSize) + "x" + std::to_string(PageSize) +
                " (" + std::to_string((NumPages * PageSize * PageSize * 4) / 1024 / 1024) + "MB)");
}

FVirtualTexturePhysicalSpace::~FVirtualTexturePhysicalSpace() {
    // Free all page data
    for (auto& page : Pages) {
        if (page.PageData) {
            FMemory::Free(page.PageData);
            page.PageData = nullptr;
        }
    }
    
    MR_LOG_INFO("FVirtualTexturePhysicalSpace destroyed");
}

uint32 FVirtualTexturePhysicalSpace::AllocatePage() {
    std::scoped_lock lock(Mutex);
    
    // Try to allocate from free list first
    if (!FreeList.empty()) {
        uint32 pageIndex = FreeList.back();
        FreeList.pop_back();
        
        Pages[pageIndex].FrameLastUsed = CurrentFrame;
        
        MR_LOG_DEBUG("Allocated physical page " + std::to_string(pageIndex) +
                     " from free list (" + std::to_string(FreeList.size()) + " free pages remaining)");
        return pageIndex;
    }
    
    // No free pages, evict LRU page
    uint32 evictedPage = EvictLRUPage();
    if (evictedPage != 0xFFFFFFFF) {
        MR_LOG_DEBUG("Allocated physical page " + std::to_string(evictedPage) + " after eviction");
        return evictedPage;
    }
    
    MR_LOG_WARNING("Failed to allocate physical page - all pages locked");
    return 0xFFFFFFFF;
}

void FVirtualTexturePhysicalSpace::FreePage(uint32 PageIndex) {
    if (PageIndex >= NumPages) {
        MR_LOG_ERROR("FreePage: invalid page index " + std::to_string(PageIndex));
        return;
    }
    
    std::scoped_lock lock(Mutex);
    
    auto& page = Pages[PageIndex];
    
    // Unmap from virtual address
    if (page.VirtualAddress != 0xFFFFFFFF) {
        VirtualToPhysicalMap.erase(page.VirtualAddress);
        page.VirtualAddress = 0xFFFFFFFF;
    }
    
    page.bLocked = false;
    page.MipLevel = 0;
    
    // Add back to free list
    FreeList.push_back(PageIndex);
    
    MR_LOG_DEBUG("Freed physical page " + std::to_string(PageIndex));
}

bool FVirtualTexturePhysicalSpace::MapPage(uint32 VirtualAddress, uint32 MipLevel, uint32& OutPhysicalAddress) {
    std::scoped_lock lock(Mutex);
    
    // Check if already mapped
    auto it = VirtualToPhysicalMap.find(VirtualAddress);
    if (it != VirtualToPhysicalMap.end()) {
        OutPhysicalAddress = it->second;
        TouchPage(OutPhysicalAddress);
        return true;
    }
    
    // Allocate new physical page
    uint32 physicalPage = AllocatePage();
    if (physicalPage == 0xFFFFFFFF) {
        return false;
    }
    
    // Map virtual to physical
    Pages[physicalPage].VirtualAddress = VirtualAddress;
    Pages[physicalPage].MipLevel = MipLevel;
    Pages[physicalPage].FrameLastUsed = CurrentFrame;
    
    VirtualToPhysicalMap[VirtualAddress] = physicalPage;
    
    OutPhysicalAddress = physicalPage;
    
    MR_LOG_DEBUG("Mapped virtual address " + std::to_string(VirtualAddress) +
                 " to physical page " + std::to_string(physicalPage));
    return true;
}

void FVirtualTexturePhysicalSpace::UnmapPage(uint32 PhysicalAddress) {
    if (PhysicalAddress >= NumPages) {
        return;
    }
    
    std::scoped_lock lock(Mutex);
    
    auto& page = Pages[PhysicalAddress];
    if (page.VirtualAddress != 0xFFFFFFFF) {
        VirtualToPhysicalMap.erase(page.VirtualAddress);
        page.VirtualAddress = 0xFFFFFFFF;
    }
}

void* FVirtualTexturePhysicalSpace::GetPageData(uint32 PhysicalAddress) {
    if (PhysicalAddress >= NumPages) {
        return nullptr;
    }
    
    return Pages[PhysicalAddress].PageData;
}

void FVirtualTexturePhysicalSpace::TouchPage(uint32 PhysicalAddress) {
    if (PhysicalAddress >= NumPages) {
        return;
    }
    
    std::scoped_lock lock(Mutex);
    Pages[PhysicalAddress].FrameLastUsed = CurrentFrame;
}

uint32 FVirtualTexturePhysicalSpace::EvictLRUPage() {
    // Find LRU candidate (must be called with Mutex locked)
    uint32 lruPage = FindLRUCandidate();
    
    if (lruPage == 0xFFFFFFFF) {
        return 0xFFFFFFFF;  // All pages are locked
    }
    
    // Unmap the page
    auto& page = Pages[lruPage];
    if (page.VirtualAddress != 0xFFFFFFFF) {
        VirtualToPhysicalMap.erase(page.VirtualAddress);
        page.VirtualAddress = 0xFFFFFFFF;
    }
    
    MR_LOG_DEBUG("Evicted LRU page " + std::to_string(lruPage) +
                 " (last used frame: " + std::to_string(page.FrameLastUsed) + ")");
    
    return lruPage;
}

void FVirtualTexturePhysicalSpace::LockPage(uint32 PhysicalAddress) {
    if (PhysicalAddress >= NumPages) {
        return;
    }
    
    std::scoped_lock lock(Mutex);
    Pages[PhysicalAddress].bLocked = true;
}

void FVirtualTexturePhysicalSpace::UnlockPage(uint32 PhysicalAddress) {
    if (PhysicalAddress >= NumPages) {
        return;
    }
    
    std::scoped_lock lock(Mutex);
    Pages[PhysicalAddress].bLocked = false;
}

uint32 FVirtualTexturePhysicalSpace::GetNumFreePages() const {
    std::scoped_lock lock(const_cast<std::mutex&>(Mutex));
    return static_cast<uint32>(FreeList.size());
}

uint32 FVirtualTexturePhysicalSpace::GetNumAllocatedPages() const {
    return NumPages - GetNumFreePages();
}

uint32 FVirtualTexturePhysicalSpace::FindLRUCandidate() {
    // Must be called with Mutex locked
    
    uint32 lruPage = 0xFFFFFFFF;
    uint32 oldestFrame = CurrentFrame + 1;
    
    for (uint32 i = 0; i < NumPages; ++i) {
        const auto& page = Pages[i];
        
        // Skip locked pages and free pages
        if (page.bLocked || page.VirtualAddress == 0xFFFFFFFF) {
            continue;
        }
        
        // Find oldest (least recently used)
        if (page.FrameLastUsed < oldestFrame) {
            oldestFrame = page.FrameLastUsed;
            lruPage = i;
        }
    }
    
    return lruPage;
}

} // namespace MonsterRender

