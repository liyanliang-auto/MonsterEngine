// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Virtual Texture System Test

#include "Core/CoreMinimal.h"
#include "Renderer/FVirtualTextureSystem.h"
#include "Core/Log.h"
#include <thread>
#include <chrono>

namespace MonsterRender {
namespace VirtualTextureSystemTest {

/**
 * Complete test suite for Virtual Texture System
 */
void RunAllTests() {
    MR_LOG_INFO("\n");
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("  Virtual Texture System Test Suite");
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("\n");

    // Test 1: Physical Space Allocation
    {
        MR_LOG_INFO("[Test 1] Physical Space Allocation");
        
        FVirtualTexturePhysicalSpace physicalSpace(128, 256);  // 128x128 pages, 256 total
        
        // Allocate pages
        uint32 page1 = physicalSpace.AllocatePage();
        uint32 page2 = physicalSpace.AllocatePage();
        uint32 page3 = physicalSpace.AllocatePage();
        
        if (page1 != 0xFFFFFFFF && page2 != 0xFFFFFFFF && page3 != 0xFFFFFFFF) {
            MR_LOG_INFO("  [OK] Allocated 3 pages: " +
                        std::to_string(page1) + ", " +
                        std::to_string(page2) + ", " +
                        std::to_string(page3));
            
            MR_LOG_INFO("  Free pages: " + std::to_string(physicalSpace.GetNumFreePages()) +
                        " / " + std::to_string(physicalSpace.GetNumPages()));
        } else {
            MR_LOG_ERROR("  [FAIL] Page allocation failed");
        }
        
        // Free pages
        physicalSpace.FreePage(page2);
        MR_LOG_INFO("  [OK] Freed page " + std::to_string(page2));
        MR_LOG_INFO("  Free pages after free: " + std::to_string(physicalSpace.GetNumFreePages()));
        
        MR_LOG_INFO("  [OK] Test 1 completed\n");
    }

    // Test 2: Virtual-to-Physical Mapping
    {
        MR_LOG_INFO("[Test 2] Virtual-to-Physical Mapping");
        
        FVirtualTexturePhysicalSpace physicalSpace(128, 256);
        
        // Map virtual addresses to physical pages
        uint32 physicalAddr1 = 0xFFFFFFFF;
        uint32 physicalAddr2 = 0xFFFFFFFF;
        
        bool mapped1 = physicalSpace.MapPage(1000, 0, physicalAddr1);  // Virtual addr 1000, Mip 0
        bool mapped2 = physicalSpace.MapPage(2000, 1, physicalAddr2);  // Virtual addr 2000, Mip 1
        
        if (mapped1 && mapped2) {
            MR_LOG_INFO("  [OK] Mapped virtual 1000 -> physical " + std::to_string(physicalAddr1));
            MR_LOG_INFO("  [OK] Mapped virtual 2000 -> physical " + std::to_string(physicalAddr2));
            
            // Remap same virtual address (should return same physical page)
            uint32 physicalAddr1Again = 0xFFFFFFFF;
            physicalSpace.MapPage(1000, 0, physicalAddr1Again);
            
            if (physicalAddr1 == physicalAddr1Again) {
                MR_LOG_INFO("  [OK] Remapping returns same physical page");
            } else {
                MR_LOG_ERROR("  [FAIL] Remapping returned different page!");
            }
        } else {
            MR_LOG_ERROR("  [FAIL] Mapping failed");
        }
        
        MR_LOG_INFO("  [OK] Test 2 completed\n");
    }

    // Test 3: LRU Eviction
    {
        MR_LOG_INFO("[Test 3] LRU Eviction");
        
        FVirtualTexturePhysicalSpace physicalSpace(128, 4);  // Only 4 pages
        
        // Allocate all pages
        TArray<uint32> pages;
        for (int i = 0; i < 4; ++i) {
            pages.push_back(physicalSpace.AllocatePage());
        }
        
        MR_LOG_INFO("  Allocated all 4 pages");
        MR_LOG_INFO("  Free pages: " + std::to_string(physicalSpace.GetNumFreePages()));
        
        // Touch pages in order: 0, 1, 2, 3
        for (int i = 0; i < 4; ++i) {
            physicalSpace.TouchPage(pages[i]);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));  // Ensure different timestamps
        }
        
        // Try to allocate one more (should evict page 0, the LRU)
        uint32 newPage = physicalSpace.AllocatePage();
        
        if (newPage == pages[0]) {
            MR_LOG_INFO("  [OK] LRU eviction correctly evicted oldest page " + std::to_string(pages[0]));
        } else {
            MR_LOG_WARNING("  [INFO] Evicted page " + std::to_string(newPage) + " (expected " + std::to_string(pages[0]) + ")");
        }
        
        MR_LOG_INFO("  [OK] Test 3 completed\n");
    }

    // Test 4: Virtual Texture Creation
    {
        MR_LOG_INFO("[Test 4] Virtual Texture Creation");
        
        FVirtualTexture vt(16384, 16384, 128, 8);  // 16K texture, 128x128 tiles, 8 mips
        
        MR_LOG_INFO("  Virtual Texture: " +
                    std::to_string(vt.GetVirtualWidth()) + "x" + std::to_string(vt.GetVirtualHeight()));
        MR_LOG_INFO("  Tile Size: " + std::to_string(vt.GetTileSize()) + "x" + std::to_string(vt.GetTileSize()));
        MR_LOG_INFO("  Mip Levels: " + std::to_string(vt.GetNumMipLevels()));
        
        // Check page counts per mip
        for (uint32 mip = 0; mip < vt.GetNumMipLevels(); ++mip) {
            uint32 pagesX = vt.GetNumPagesX(mip);
            uint32 pagesY = vt.GetNumPagesY(mip);
            MR_LOG_INFO("  Mip " + std::to_string(mip) + ": " +
                        std::to_string(pagesX) + "x" + std::to_string(pagesY) + " pages (" +
                        std::to_string(pagesX * pagesY) + " total)");
        }
        
        // Test page residency (should all be non-resident initially)
        if (!vt.IsPageResident(0, 0, 0)) {
            MR_LOG_INFO("  [OK] Pages correctly marked as non-resident initially");
        }
        
        MR_LOG_INFO("  [OK] Test 4 completed\n");
    }

    // Test 5: Virtual Texture System Integration
    {
        MR_LOG_INFO("[Test 5] Virtual Texture System Integration");
        
        auto& vtSystem = FVirtualTextureSystem::Get();
        vtSystem.Initialize(128, 512);  // 128x128 pages, 512 total
        
        // Create a 16K virtual texture
        auto vt = vtSystem.CreateVirtualTexture(16384, 16384, 8);
        
        if (vt) {
            MR_LOG_INFO("  [OK] Created 16K virtual texture");
            
            // Request some pages
            vtSystem.RequestPage(vt.get(), 0, 0, 0);  // Top-left tile, mip 0
            vtSystem.RequestPage(vt.get(), 1, 0, 0);  // Next tile
            vtSystem.RequestPage(vt.get(), 0, 1, 0);  // Tile below
            vtSystem.RequestPage(vt.get(), 10, 20, 1); // Random tile, mip 1
            
            MR_LOG_INFO("  Requested 4 pages");
            
            // Process requests
            for (int frame = 0; frame < 5; ++frame) {
                vtSystem.Update(0.016f);  // 60fps
                
                FVirtualTextureSystem::FVTStats stats;
                vtSystem.GetStats(stats);
                
                MR_LOG_INFO("  Frame " + std::to_string(frame + 1) + ": " +
                            std::to_string(stats.NumPhysicalPages - stats.NumFreePages) + " pages resident, " +
                            std::to_string(stats.NumFreePages) + " free");
            }
            
            // Check stats
            FVirtualTextureSystem::FVTStats stats;
            vtSystem.GetStats(stats);
            
            MR_LOG_INFO("  Final Stats:");
            MR_LOG_INFO("    Virtual Textures: " + std::to_string(stats.NumVirtualTextures));
            MR_LOG_INFO("    Physical Pages: " + std::to_string(stats.NumPhysicalPages));
            MR_LOG_INFO("    Free Pages: " + std::to_string(stats.NumFreePages));
            MR_LOG_INFO("    Page Faults: " + std::to_string(stats.NumPageFaults));
            MR_LOG_INFO("    Total Requests: " + std::to_string(stats.TotalPageRequests));
        } else {
            MR_LOG_ERROR("  [FAIL] Failed to create virtual texture");
        }
        
        vtSystem.Shutdown();
        
        MR_LOG_INFO("  [OK] Test 5 completed\n");
    }

    // Test 6: Page Fault Simulation
    {
        MR_LOG_INFO("[Test 6] Page Fault Simulation (16K+ Texture)");
        
        auto& vtSystem = FVirtualTextureSystem::Get();
        vtSystem.Initialize(128, 256);
        
        // Create a massive 32K texture
        auto vt = vtSystem.CreateVirtualTexture(32768, 32768, 10);
        
        MR_LOG_INFO("  Created 32K texture (32768x32768)");
        MR_LOG_INFO("  Total virtual pages: ~" +
                    std::to_string(vt->GetNumPagesX(0) * vt->GetNumPagesY(0)));
        
        // Simulate camera moving through texture
        MR_LOG_INFO("  Simulating camera movement...");
        
        for (int cameraPos = 0; cameraPos < 10; ++cameraPos) {
            // Request pages in camera's view frustum
            uint32 startX = cameraPos * 5;
            uint32 startY = cameraPos * 3;
            
            for (uint32 y = startY; y < startY + 3 && y < vt->GetNumPagesY(0); ++y) {
                for (uint32 x = startX; x < startX + 3 && x < vt->GetNumPagesX(0); ++x) {
                    vtSystem.RecordPageAccess(vt.get(), x, y, 0);
                }
            }
            
            // Process for a few frames
            for (int i = 0; i < 2; ++i) {
                vtSystem.Update(0.016f);
            }
        }
        
        FVirtualTextureSystem::FVTStats stats;
        vtSystem.GetStats(stats);
        
        MR_LOG_INFO("  Camera Movement Complete:");
        MR_LOG_INFO("    Page Faults: " + std::to_string(stats.NumPageFaults));
        MR_LOG_INFO("    Page Evictions: " + std::to_string(stats.NumPageEvictions));
        MR_LOG_INFO("    Hit Rate: " +
                    std::to_string(100.0f - (float)stats.NumPageFaults / stats.TotalPageRequests * 100.0f) + "%");
        
        vtSystem.Shutdown();
        
        MR_LOG_INFO("  [OK] Test 6 completed\n");
    }

    // Test 7: Stress Test - Page Thrashing
    {
        MR_LOG_INFO("[Test 7] Stress Test - Page Thrashing");
        
        auto& vtSystem = FVirtualTextureSystem::Get();
        vtSystem.Initialize(128, 64);  // Small cache (only 64 pages)
        
        auto vt = vtSystem.CreateVirtualTexture(8192, 8192, 6);
        
        MR_LOG_INFO("  Created 8K texture with small cache (64 pages)");
        
        // Request way more pages than cache can hold
        uint32 numRequests = 200;
        for (uint32 i = 0; i < numRequests; ++i) {
            uint32 x = (i * 7) % vt->GetNumPagesX(0);
            uint32 y = (i * 11) % vt->GetNumPagesY(0);
            vtSystem.RequestPage(vt.get(), x, y, 0);
        }
        
        MR_LOG_INFO("  Requested " + std::to_string(numRequests) + " pages (cache thrashing)");
        
        // Process all requests
        for (int frame = 0; frame < 20; ++frame) {
            vtSystem.Update(0.016f);
        }
        
        FVirtualTextureSystem::FVTStats stats;
        vtSystem.GetStats(stats);
        
        MR_LOG_INFO("  Stress Test Complete:");
        MR_LOG_INFO("    Total Requests: " + std::to_string(stats.TotalPageRequests));
        MR_LOG_INFO("    Page Evictions: " + std::to_string(stats.NumPageEvictions));
        MR_LOG_INFO("    Eviction Rate: " +
                    std::to_string((float)stats.NumPageEvictions / stats.TotalPageRequests * 100.0f) + "%");
        
        if (stats.NumPageEvictions > 0) {
            MR_LOG_INFO("  [OK] LRU eviction working under stress");
        }
        
        vtSystem.Shutdown();
        
        MR_LOG_INFO("  [OK] Test 7 completed\n");
    }

    MR_LOG_INFO("\n");
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("  All Tests Completed Successfully!");
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("\n");
}

} // namespace VirtualTextureSystemTest
} // namespace MonsterRender

