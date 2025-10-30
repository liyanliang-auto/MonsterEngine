// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Virtual Texture System (UE5-style)

#pragma once

#include "Core/CoreTypes.h"
#include <mutex>
#include <vector>
#include <unordered_map>

namespace MonsterRender {

/**
 * FVirtualTexturePhysicalSpace - Manages physical page cache for virtual textures
 * 
 * Reference: UE5 Engine/Source/Runtime/Renderer/Private/VT/VirtualTexturePhysicalSpace.h
 * 
 * Virtual texturing allows very large textures by only keeping visible pages in memory.
 * Physical space is the actual GPU memory that holds cached pages.
 */
class FVirtualTexturePhysicalSpace {
public:
    FVirtualTexturePhysicalSpace(uint32 TileSize, uint32 NumTilesX, uint32 NumTilesY);
    ~FVirtualTexturePhysicalSpace();

    // Page allocation
    uint32 AllocatePage();
    void FreePage(uint32 PageIndex);

    // Page mapping
    struct FPageMapping {
        uint32 VirtualPageX;
        uint32 VirtualPageY;
        uint32 MipLevel;
        uint32 PhysicalPageIndex;
    };

    bool MapPage(const FPageMapping& Mapping);
    void UnmapPage(uint32 PhysicalPageIndex);

    // Query
    uint32 GetTileSize() const { return TileSize; }
    uint32 GetNumTilesX() const { return NumTilesX; }
    uint32 GetNumTilesY() const { return NumTilesY; }
    uint32 GetTotalPages() const { return NumTilesX * NumTilesY; }

    // Stats
    struct FPhysicalSpaceStats {
        uint32 TotalPages;
        uint32 AllocatedPages;
        uint32 FreePages;
        SIZE_T TotalMemory;
        SIZE_T UsedMemory;
    };

    void GetStats(FPhysicalSpaceStats& OutStats);

private:
    // Free list for page allocation
    struct FFreePageNode {
        uint32 PageIndex;
        FFreePageNode* Next;
    };

    uint32 TileSize;     // Tile size in pixels (e.g., 128x128)
    uint32 NumTilesX;    // Number of tiles horizontally
    uint32 NumTilesY;    // Number of tiles vertically
    
    FFreePageNode* FreeList;
    std::vector<FPageMapping> PageMappings;
    std::mutex SpaceMutex;

    uint32 AllocatedPageCount;
};

/**
 * FVirtualTexture - Virtual texture representation
 * 
 * Reference: UE5 Engine/Source/Runtime/Renderer/Private/VT/VirtualTexture.h
 */
class FVirtualTexture {
public:
    FVirtualTexture(uint32 WidthInPages, uint32 HeightInPages, uint32 NumMipLevels);
    ~FVirtualTexture();

    // Request pages for rendering
    void RequestPages(const TArray<uint32>& PageIndices);

    // Check if page is resident
    bool IsPageResident(uint32 PageIndex) const;

    // Get physical page index for virtual page
    uint32 GetPhysicalPageIndex(uint32 VirtualPageIndex) const;

    // Properties
    uint32 GetWidthInPages() const { return WidthInPages; }
    uint32 GetHeightInPages() const { return HeightInPages; }
    uint32 GetNumMipLevels() const { return NumMipLevels; }

private:
    uint32 WidthInPages;
    uint32 HeightInPages;
    uint32 NumMipLevels;

    // Page table: maps virtual page index to physical page index
    std::unordered_map<uint32, uint32> PageTable;
    std::mutex VTMutex;
};

/**
 * FVirtualTextureSystem - Global virtual texture system manager
 * 
 * Coordinates multiple physical spaces and virtual textures
 */
class FVirtualTextureSystem {
public:
    static FVirtualTextureSystem& Get();

    bool Initialize(uint32 PhysicalCacheSize = 128 * 1024 * 1024); // 128MB default
    void Shutdown();

    // Create/destroy physical spaces
    FVirtualTexturePhysicalSpace* CreatePhysicalSpace(uint32 TileSize, uint32 NumTilesX, uint32 NumTilesY);
    void DestroyPhysicalSpace(FVirtualTexturePhysicalSpace* Space);

    // Create/destroy virtual textures
    FVirtualTexture* CreateVirtualTexture(uint32 WidthInPages, uint32 HeightInPages, uint32 NumMips);
    void DestroyVirtualTexture(FVirtualTexture* VT);

    // Per-frame update
    void Update(float DeltaTime);

    // Statistics
    struct FVTSystemStats {
        uint32 NumPhysicalSpaces;
        uint32 NumVirtualTextures;
        SIZE_T TotalPhysicalMemory;
        SIZE_T UsedPhysicalMemory;
        uint32 PageRequests;
        uint32 PageEvictions;
    };

    void GetStats(FVTSystemStats& OutStats);

private:
    FVirtualTextureSystem();
    ~FVirtualTextureSystem();

    FVirtualTextureSystem(const FVirtualTextureSystem&) = delete;
    FVirtualTextureSystem& operator=(const FVirtualTextureSystem&) = delete;

    std::vector<TUniquePtr<FVirtualTexturePhysicalSpace>> PhysicalSpaces;
    std::vector<TUniquePtr<FVirtualTexture>> VirtualTextures;
    std::mutex SystemMutex;

    bool bInitialized = false;
};

} // namespace MonsterRender

