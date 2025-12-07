// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Virtual Texture System

#pragma once

#include "Core/CoreTypes.h"
#include "Core/HAL/FMemory.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include <mutex>

namespace MonsterRender {

// Use MonsterEngine containers
using MonsterEngine::TArray;
using MonsterEngine::TMap;

// Forward declarations
class FVirtualTexturePhysicalSpace;
class FVirtualTexture;

/**
 * FVirtualTexturePhysicalPage - Physical page in cache
 * 
 * Reference: UE5 Engine/Source/Runtime/Renderer/Private/VT/VirtualTexturePhysicalSpace.h
 */
struct FVirtualTexturePhysicalPage {
    uint32 PhysicalAddress;    // Address in physical space (page index)
    uint32 VirtualAddress;     // Virtual address this page maps to
    uint32 MipLevel;           // Mip level of this page
    bool bLocked;              // Is page locked (cannot be evicted)
    uint32 FrameLastUsed;      // Frame number when last used (for LRU)
    void* PageData;            // Actual page data (e.g., 128x128 tile)
    
    FVirtualTexturePhysicalPage()
        : PhysicalAddress(0xFFFFFFFF)
        , VirtualAddress(0xFFFFFFFF)
        , MipLevel(0)
        , bLocked(false)
        , FrameLastUsed(0)
        , PageData(nullptr)
    {}
};

/**
 * FVirtualTexturePhysicalSpace - Physical page cache
 * 
 * Manages a pool of physical pages (like a page table in virtual memory)
 * 
 * Reference: UE5 FVirtualTexturePhysicalSpace
 */
class FVirtualTexturePhysicalSpace {
public:
    FVirtualTexturePhysicalSpace(uint32 PageSize, uint32 NumPages);
    ~FVirtualTexturePhysicalSpace();

    // Allocate a physical page (returns page index, or 0xFFFFFFFF if failed)
    uint32 AllocatePage();
    
    // Free a physical page
    void FreePage(uint32 PageIndex);
    
    // Map virtual address to physical page
    bool MapPage(uint32 VirtualAddress, uint32 MipLevel, uint32& OutPhysicalAddress);
    
    // Unmap a page
    void UnmapPage(uint32 PhysicalAddress);
    
    // Get physical page data
    void* GetPageData(uint32 PhysicalAddress);
    
    // Update LRU (call every frame for accessed pages)
    void TouchPage(uint32 PhysicalAddress);
    
    // Evict least recently used page (LRU algorithm)
    uint32 EvictLRUPage();
    
    // Lock/Unlock page (locked pages cannot be evicted)
    void LockPage(uint32 PhysicalAddress);
    void UnlockPage(uint32 PhysicalAddress);
    
    // Statistics
    uint32 GetPageSize() const { return PageSize; }
    uint32 GetNumPages() const { return NumPages; }
    uint32 GetNumFreePages() const;
    uint32 GetNumAllocatedPages() const;

private:
    uint32 PageSize;           // Size of each page (e.g., 128x128x4 = 64KB)
    uint32 NumPages;           // Total number of physical pages
    uint32 CurrentFrame;       // Current frame number for LRU
    
    TArray<FVirtualTexturePhysicalPage> Pages;  // All physical pages
    TArray<uint32> FreeList;                    // Indices of free pages
    
    // Mapping: VirtualAddress -> PhysicalAddress
    TMap<uint32, uint32> VirtualToPhysicalMap;
    
    std::mutex Mutex;          // Thread safety
    
    // Helper: Find LRU candidate
    uint32 FindLRUCandidate();
};

/**
 * FVirtualTexturePageTable - Maps virtual addresses to physical pages
 * 
 * Reference: UE5 FPageTableEntry
 */
struct FVirtualTexturePageTableEntry {
    uint32 PhysicalPageIndex;  // Physical page this virtual page maps to
    uint32 MipLevel;           // Mip level
    bool bResident;            // Is this page currently resident in physical space?
    
    FVirtualTexturePageTableEntry()
        : PhysicalPageIndex(0xFFFFFFFF)
        , MipLevel(0)
        , bResident(false)
    {}
};

/**
 * FVirtualTexture - Virtual texture resource
 * 
 * Represents a large texture that is paged on demand
 */
class FVirtualTexture {
public:
    FVirtualTexture(uint32 VirtualWidth, uint32 VirtualHeight, uint32 TileSize, uint32 NumMipLevels);
    ~FVirtualTexture();
    
    // Query page table
    bool IsPageResident(uint32 PageX, uint32 PageY, uint32 MipLevel) const;
    uint32 GetPhysicalPageIndex(uint32 PageX, uint32 PageY, uint32 MipLevel) const;
    
    // Calculate virtual address from page coordinates
    uint32 CalculateVirtualAddress(uint32 PageX, uint32 PageY, uint32 MipLevel) const;
    
    // Get page table entry
    FVirtualTexturePageTableEntry* GetPageTableEntry(uint32 PageX, uint32 PageY, uint32 MipLevel);
    
    // Getters
    uint32 GetVirtualWidth() const { return VirtualWidth; }
    uint32 GetVirtualHeight() const { return VirtualHeight; }
    uint32 GetTileSize() const { return TileSize; }
    uint32 GetNumMipLevels() const { return NumMipLevels; }
    uint32 GetNumPagesX(uint32 MipLevel) const;
    uint32 GetNumPagesY(uint32 MipLevel) const;

private:
    uint32 VirtualWidth;       // Virtual texture width (e.g., 16384)
    uint32 VirtualHeight;      // Virtual texture height
    uint32 TileSize;           // Size of each tile/page (e.g., 128x128)
    uint32 NumMipLevels;       // Number of mip levels
    
    // Page table (one entry per virtual page)
    TArray<TArray<FVirtualTexturePageTableEntry>> PageTable;  // [MipLevel][PageIndex]
};

/**
 * FVirtualTextureSystem - Main virtual texture system
 * 
 * Reference: UE5 Engine/Source/Runtime/Renderer/Private/VT/VirtualTextureSystem.cpp
 */
class FVirtualTextureSystem {
public:
    static FVirtualTextureSystem& Get();
    
    // Initialize the system
    bool Initialize(uint32 PhysicalPageSize = 128, uint32 NumPhysicalPages = 1024);
    void Shutdown();
    
    // Create virtual texture
    TSharedPtr<FVirtualTexture> CreateVirtualTexture(
        uint32 VirtualWidth,
        uint32 VirtualHeight,
        uint32 NumMipLevels
    );
    
    // Request page (call from feedback system when page fault occurs)
    void RequestPage(FVirtualTexture* VirtualTexture, uint32 PageX, uint32 PageY, uint32 MipLevel);
    
    // Process pending page requests (call every frame)
    void Update(float DeltaTime);
    
    // Feedback system: record page access
    void RecordPageAccess(FVirtualTexture* VirtualTexture, uint32 PageX, uint32 PageY, uint32 MipLevel);
    
    // Statistics
    struct FVTStats {
        uint32 NumVirtualTextures;
        uint32 NumPhysicalPages;
        uint32 NumFreePages;
        uint32 NumPageFaults;
        uint32 NumPageEvictions;
        uint32 TotalPageRequests;
    };
    
    void GetStats(FVTStats& OutStats);

private:
    FVirtualTextureSystem();
    ~FVirtualTextureSystem();
    
    FVirtualTextureSystem(const FVirtualTextureSystem&) = delete;
    FVirtualTextureSystem& operator=(const FVirtualTextureSystem&) = delete;
    
    // Process a single page request
    bool ProcessPageRequest(FVirtualTexture* VirtualTexture, uint32 PageX, uint32 PageY, uint32 MipLevel);
    
    // Load page data from disk (async)
    void LoadPageData(FVirtualTexture* VirtualTexture, uint32 PageX, uint32 PageY, uint32 MipLevel, void* DestBuffer);
    
    TUniquePtr<FVirtualTexturePhysicalSpace> PhysicalSpace;
    TArray<TSharedPtr<FVirtualTexture>> VirtualTextures;
    
    // Pending page requests (from feedback system)
    struct FPageRequest {
        FVirtualTexture* VirtualTexture;
        uint32 PageX;
        uint32 PageY;
        uint32 MipLevel;
        uint32 Priority;  // Higher = more urgent
    };
    
    TArray<FPageRequest> PendingRequests;
    std::mutex RequestMutex;
    
    // Statistics
    uint32 NumPageFaults;
    uint32 NumPageEvictions;
    uint32 TotalPageRequests;
    
    bool bInitialized;
};

} // namespace MonsterRender
