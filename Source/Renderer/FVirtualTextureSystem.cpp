// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Virtual Texture System Implementation

#include "Renderer/FVirtualTextureSystem.h"
#include "Core/Log.h"
#include "Core/IO/FAsyncFileIO.h"
#include <algorithm>
#include <cmath>

namespace MonsterRender {

// ===== FVirtualTexture Implementation =====

FVirtualTexture::FVirtualTexture(uint32 InVirtualWidth, uint32 InVirtualHeight, uint32 InTileSize, uint32 InNumMipLevels)
    : VirtualWidth(InVirtualWidth)
    , VirtualHeight(InVirtualHeight)
    , TileSize(InTileSize)
    , NumMipLevels(InNumMipLevels)
{
    // Initialize page table for all mip levels
    PageTable.resize(NumMipLevels);
    
    for (uint32 mip = 0; mip < NumMipLevels; ++mip) {
        uint32 numPagesX = GetNumPagesX(mip);
        uint32 numPagesY = GetNumPagesY(mip);
        uint32 totalPages = numPagesX * numPagesY;
        
        PageTable[mip].resize(totalPages);
        
        MR_LOG_DEBUG("Virtual Texture Mip " + std::to_string(mip) + ": " +
                     std::to_string(numPagesX) + "x" + std::to_string(numPagesY) + " pages");
    }
    
    MR_LOG_INFO("FVirtualTexture created: " +
                std::to_string(VirtualWidth) + "x" + std::to_string(VirtualHeight) +
                ", " + std::to_string(NumMipLevels) + " mips, tile size " + std::to_string(TileSize));
}

FVirtualTexture::~FVirtualTexture() {
    MR_LOG_INFO("FVirtualTexture destroyed");
}

bool FVirtualTexture::IsPageResident(uint32 PageX, uint32 PageY, uint32 MipLevel) const {
    if (MipLevel >= NumMipLevels) return false;
    
    uint32 numPagesX = GetNumPagesX(MipLevel);
    if (PageX >= numPagesX || PageY >= GetNumPagesY(MipLevel)) {
        return false;
    }
    
    uint32 pageIndex = PageY * numPagesX + PageX;
    return PageTable[MipLevel][pageIndex].bResident;
}

uint32 FVirtualTexture::GetPhysicalPageIndex(uint32 PageX, uint32 PageY, uint32 MipLevel) const {
    if (MipLevel >= NumMipLevels) return 0xFFFFFFFF;
    
    uint32 numPagesX = GetNumPagesX(MipLevel);
    if (PageX >= numPagesX || PageY >= GetNumPagesY(MipLevel)) {
        return 0xFFFFFFFF;
    }
    
    uint32 pageIndex = PageY * numPagesX + PageX;
    return PageTable[MipLevel][pageIndex].PhysicalPageIndex;
}

uint32 FVirtualTexture::CalculateVirtualAddress(uint32 PageX, uint32 PageY, uint32 MipLevel) const {
    // Encode page coordinates and mip level into a single uint32
    // Format: [Mip:4][PageY:14][PageX:14]
    return (MipLevel << 28) | (PageY << 14) | PageX;
}

FVirtualTexturePageTableEntry* FVirtualTexture::GetPageTableEntry(uint32 PageX, uint32 PageY, uint32 MipLevel) {
    if (MipLevel >= NumMipLevels) return nullptr;
    
    uint32 numPagesX = GetNumPagesX(MipLevel);
    if (PageX >= numPagesX || PageY >= GetNumPagesY(MipLevel)) {
        return nullptr;
    }
    
    uint32 pageIndex = PageY * numPagesX + PageX;
    return &PageTable[MipLevel][pageIndex];
}

uint32 FVirtualTexture::GetNumPagesX(uint32 MipLevel) const {
    uint32 mipWidth = VirtualWidth >> MipLevel;
    return (mipWidth + TileSize - 1) / TileSize;
}

uint32 FVirtualTexture::GetNumPagesY(uint32 MipLevel) const {
    uint32 mipHeight = VirtualHeight >> MipLevel;
    return (mipHeight + TileSize - 1) / TileSize;
}

// ===== FVirtualTextureSystem Implementation =====

FVirtualTextureSystem& FVirtualTextureSystem::Get() {
    static FVirtualTextureSystem Instance;
    return Instance;
}

FVirtualTextureSystem::FVirtualTextureSystem()
    : NumPageFaults(0)
    , NumPageEvictions(0)
    , TotalPageRequests(0)
    , bInitialized(false)
{
}

FVirtualTextureSystem::~FVirtualTextureSystem() {
    Shutdown();
}

bool FVirtualTextureSystem::Initialize(uint32 PhysicalPageSize, uint32 NumPhysicalPages) {
    if (bInitialized) {
        MR_LOG_WARNING("FVirtualTextureSystem already initialized");
        return true;
    }
    
    MR_LOG_INFO("Initializing FVirtualTextureSystem...");
    MR_LOG_INFO("  Physical Page Size: " + std::to_string(PhysicalPageSize) + "x" + std::to_string(PhysicalPageSize));
    MR_LOG_INFO("  Num Physical Pages: " + std::to_string(NumPhysicalPages));
    
    // Create physical space
    PhysicalSpace = MakeUnique<FVirtualTexturePhysicalSpace>(PhysicalPageSize, NumPhysicalPages);
    
    bInitialized = true;
    MR_LOG_INFO("FVirtualTextureSystem initialized successfully");
    return true;
}

void FVirtualTextureSystem::Shutdown() {
    if (!bInitialized) return;
    
    MR_LOG_INFO("Shutting down FVirtualTextureSystem...");
    
    // Clear all virtual textures
    VirtualTextures.clear();
    
    // Release physical space
    PhysicalSpace.reset();
    
    bInitialized = false;
    MR_LOG_INFO("FVirtualTextureSystem shut down");
}

TSharedPtr<FVirtualTexture> FVirtualTextureSystem::CreateVirtualTexture(
    uint32 VirtualWidth,
    uint32 VirtualHeight,
    uint32 NumMipLevels)
{
    if (!bInitialized) {
        MR_LOG_ERROR("FVirtualTextureSystem not initialized");
        return nullptr;
    }
    
    // Calculate tile size (use physical page size)
    uint32 tileSize = PhysicalSpace->GetPageSize();
    
    // Create virtual texture
    auto virtualTexture = MakeShared<FVirtualTexture>(VirtualWidth, VirtualHeight, tileSize, NumMipLevels);
    VirtualTextures.push_back(virtualTexture);
    
    MR_LOG_INFO("Created virtual texture: " +
                std::to_string(VirtualWidth) + "x" + std::to_string(VirtualHeight) +
                " (" + std::to_string(NumMipLevels) + " mips)");
    
    return virtualTexture;
}

void FVirtualTextureSystem::RequestPage(FVirtualTexture* VirtualTexture, uint32 PageX, uint32 PageY, uint32 MipLevel) {
    if (!VirtualTexture) return;
    
    // Check if page is already resident
    if (VirtualTexture->IsPageResident(PageX, PageY, MipLevel)) {
        // Update LRU
        uint32 physicalPageIndex = VirtualTexture->GetPhysicalPageIndex(PageX, PageY, MipLevel);
        if (physicalPageIndex != 0xFFFFFFFF) {
            PhysicalSpace->TouchPage(physicalPageIndex);
        }
        return;
    }
    
    // Add to pending requests
    std::scoped_lock lock(RequestMutex);
    
    FPageRequest request;
    request.VirtualTexture = VirtualTexture;
    request.PageX = PageX;
    request.PageY = PageY;
    request.MipLevel = MipLevel;
    request.Priority = 100 - MipLevel * 10;  // Higher mips have higher priority
    
    PendingRequests.push_back(request);
    TotalPageRequests++;
    NumPageFaults++;
    
    MR_LOG_DEBUG("Page request: (" + std::to_string(PageX) + "," + std::to_string(PageY) +
                 ") Mip " + std::to_string(MipLevel) +
                 " (Total requests: " + std::to_string(PendingRequests.size()) + ")");
}

void FVirtualTextureSystem::Update(float DeltaTime) {
    if (!bInitialized) return;
    
    // Process pending requests (sorted by priority)
    std::scoped_lock lock(RequestMutex);
    
    // Sort by priority (highest first)
    std::sort(PendingRequests.begin(), PendingRequests.end(),
        [](const FPageRequest& a, const FPageRequest& b) {
            return a.Priority > b.Priority;
        });
    
    // Process up to N requests per frame to avoid spikes
    const uint32 MaxRequestsPerFrame = 32;
    uint32 numProcessed = 0;
    
    auto it = PendingRequests.begin();
    while (it != PendingRequests.end() && numProcessed < MaxRequestsPerFrame) {
        const auto& request = *it;
        
        bool success = ProcessPageRequest(
            request.VirtualTexture,
            request.PageX,
            request.PageY,
            request.MipLevel
        );
        
        if (success) {
            it = PendingRequests.erase(it);
            numProcessed++;
        } else {
            ++it;  // Keep in queue, try again next frame
        }
    }
    
    if (numProcessed > 0) {
        MR_LOG_DEBUG("Processed " + std::to_string(numProcessed) + " page requests (" +
                     std::to_string(PendingRequests.size()) + " remaining)");
    }
}

void FVirtualTextureSystem::RecordPageAccess(FVirtualTexture* VirtualTexture, uint32 PageX, uint32 PageY, uint32 MipLevel) {
    if (!VirtualTexture) return;
    
    // Check if page is resident
    if (VirtualTexture->IsPageResident(PageX, PageY, MipLevel)) {
        // Touch page to update LRU
        uint32 physicalPageIndex = VirtualTexture->GetPhysicalPageIndex(PageX, PageY, MipLevel);
        if (physicalPageIndex != 0xFFFFFFFF) {
            PhysicalSpace->TouchPage(physicalPageIndex);
        }
    } else {
        // Page fault - request the page
        RequestPage(VirtualTexture, PageX, PageY, MipLevel);
    }
}

void FVirtualTextureSystem::GetStats(FVTStats& OutStats) {
    OutStats.NumVirtualTextures = static_cast<uint32>(VirtualTextures.size());
    OutStats.NumPhysicalPages = PhysicalSpace ? PhysicalSpace->GetNumPages() : 0;
    OutStats.NumFreePages = PhysicalSpace ? PhysicalSpace->GetNumFreePages() : 0;
    OutStats.NumPageFaults = NumPageFaults;
    OutStats.NumPageEvictions = NumPageEvictions;
    OutStats.TotalPageRequests = TotalPageRequests;
}

// Private methods

bool FVirtualTextureSystem::ProcessPageRequest(FVirtualTexture* VirtualTexture, uint32 PageX, uint32 PageY, uint32 MipLevel) {
    if (!VirtualTexture || !PhysicalSpace) return false;
    
    // Calculate virtual address
    uint32 virtualAddress = VirtualTexture->CalculateVirtualAddress(PageX, PageY, MipLevel);
    
    // Try to map to physical page
    uint32 physicalPageIndex = 0xFFFFFFFF;
    bool mapped = PhysicalSpace->MapPage(virtualAddress, MipLevel, physicalPageIndex);
    
    if (!mapped) {
        MR_LOG_WARNING("Failed to map page - all physical pages in use");
        NumPageEvictions++;
        return false;
    }
    
    // Get page table entry
    auto* pageEntry = VirtualTexture->GetPageTableEntry(PageX, PageY, MipLevel);
    if (!pageEntry) {
        PhysicalSpace->FreePage(physicalPageIndex);
        return false;
    }
    
    // Update page table
    pageEntry->PhysicalPageIndex = physicalPageIndex;
    pageEntry->MipLevel = MipLevel;
    pageEntry->bResident = true;
    
    // Load page data (async)
    void* pageData = PhysicalSpace->GetPageData(physicalPageIndex);
    if (pageData) {
        LoadPageData(VirtualTexture, PageX, PageY, MipLevel, pageData);
    }
    
    MR_LOG_DEBUG("Mapped page (" + std::to_string(PageX) + "," + std::to_string(PageY) +
                 ") Mip " + std::to_string(MipLevel) +
                 " to physical page " + std::to_string(physicalPageIndex));
    
    return true;
}

void FVirtualTextureSystem::LoadPageData(FVirtualTexture* VirtualTexture, uint32 PageX, uint32 PageY, uint32 MipLevel, void* DestBuffer) {
    // TODO: Load actual texture data from disk
    // For now, fill with a debug pattern
    
    uint32 tileSize = PhysicalSpace->GetPageSize();
    uint32* pixels = static_cast<uint32*>(DestBuffer);
    
    // Generate debug pattern (checkerboard based on page coordinates)
    for (uint32 y = 0; y < tileSize; ++y) {
        for (uint32 x = 0; x < tileSize; ++x) {
            bool checker = ((x / 16) + (y / 16)) % 2 == 0;
            bool pagechecker = ((PageX + PageY) % 2 == 0);
            
            uint8 r = checker ? 255 : 128;
            uint8 g = pagechecker ? 255 : 64;
            uint8 b = (MipLevel * 32) % 256;
            uint8 a = 255;
            
            pixels[y * tileSize + x] = (a << 24) | (b << 16) | (g << 8) | r;
        }
    }
    
    MR_LOG_DEBUG("Loaded page data (" + std::to_string(PageX) + "," + std::to_string(PageY) +
                 ") Mip " + std::to_string(MipLevel) + " (debug pattern)");
}

} // namespace MonsterRender

