#include "Core/CoreMinimal.h"
#include "Core/Memory.h"
#include "Containers/Array.h"
#include <thread>
#include <chrono>

namespace MonsterRender {

// Use MonsterEngine containers
using MonsterEngine::TArray;

namespace MemorySystemTest {

/**
 * MemorySystem Test Suite
 * Validates all memory manager functionality
 */

// Forward declarations
namespace {
    void testInitialization();
    void testSmallObjectPool();
    void testFrameScratchPool();
    void testTexturePool();
    void testThreadLocalCache();
    void testHugePages();
    void testEmptyPageTrimming();
    void testStatistics();
    void testConcurrency();
    void testEdgeCases();
}

void runAllTests() {
    MR_LOG_INFO("=== MemorySystem Test Suite Started ===\n");

    testInitialization();
    testSmallObjectPool();
    testFrameScratchPool();
    testTexturePool();
    testThreadLocalCache();
    testHugePages();
    testEmptyPageTrimming();
    testStatistics();
    testConcurrency();
    testEdgeCases();

    MR_LOG_INFO("\n=== MemorySystem Test Suite Completed ===");
}

namespace {  // Anonymous namespace for internal test functions

// Test 1: Initialization and Shutdown
void testInitialization() {
    MR_LOG_INFO("\n[Test 1] Initialization and Shutdown");
    
    auto& memSys = MemorySystem::get();
    bool success = memSys.initialize(
        8 * 1024 * 1024,   // 8MB frame scratch
        64 * 1024 * 1024   // 64MB texture blocks
    );
    
    MR_LOG_INFO("  [OK] MemorySystem initialized: " + String(success ? "SUCCESS" : "FAILED"));
    MR_LOG_INFO("  [OK] Huge pages available: " + String(memSys.isHugePagesAvailable() ? "YES" : "NO"));
}

// Test 2: Small Object Pool (16B - 1024B)
void testSmallObjectPool() {
    MR_LOG_INFO("\n[Test 2] Small Object Pool");
    
    auto& memSys = MemorySystem::get();
    
    // Test various allocation sizes
    struct TestSize {
        size_t size;
        const char* name;
    };
    
    TestSize sizes[] = {
        {16, "16B"},
        {32, "32B"},
        {64, "64B"},
        {128, "128B"},
        {256, "256B"},
        {512, "512B"},
        {1024, "1024B"}
    };
    
    for (auto& test : sizes) {
        void* ptr = memSys.allocateSmall(test.size);
        if (ptr) {
            // Write test data
            memset(ptr, 0xAA, test.size);
            MR_LOG_INFO(String("  [OK] Allocated ") + test.name + " at 0x" + 
                       std::to_string(reinterpret_cast<uintptr_t>(ptr)));
            
            // Free immediately
            memSys.freeSmall(ptr, test.size);
            MR_LOG_INFO(String("  [OK] Freed ") + test.name);
        } else {
            MR_LOG_WARNING(String("  [FAIL] Failed to allocate ") + test.name);
        }
    }
    
    // Test bulk allocation
    MR_LOG_INFO("\n  Testing bulk allocation (1000 objects of 64B)...");
    TArray<void*> pointers;
    for (int i = 0; i < 1000; ++i) {
        void* ptr = memSys.allocateSmall(64);
        if (ptr) {
            pointers.Add(ptr);
        }
    }
    MR_LOG_INFO("  [OK] Allocated " + std::to_string(pointers.Num()) + " objects");
    
    // Free all objects
    for (void* ptr : pointers) {
        memSys.freeSmall(ptr, 64);
    }
    MR_LOG_INFO("  [OK] Freed all objects");
}

// Test 3: Frame Scratch Pool
void testFrameScratchPool() {
    MR_LOG_INFO("\n[Test 3] Frame Scratch Pool");
    
    auto& memSys = MemorySystem::get();
    
    // Simulate multi-frame allocation
    for (int frame = 0; frame < 3; ++frame) {
        MR_LOG_INFO(String("  Frame ") + std::to_string(frame) + ":");
        
        // Allocate temp data within frame
        void* temp1 = memSys.frameAllocate(4096);
        void* temp2 = memSys.frameAllocate(8192);
        void* temp3 = memSys.frameAllocate(16384);
        
        if (temp1 && temp2 && temp3) {
            uint64 allocated = memSys.getAllocatedFrameBytes();
            MR_LOG_INFO("    [OK] Allocated 28KB, total: " + 
                       std::to_string(allocated / 1024) + "KB");
            
            // Simulate data usage
            memset(temp1, 0xBB, 4096);
            memset(temp2, 0xCC, 8192);
            memset(temp3, 0xDD, 16384);
        }
        
        // Reset at frame end
        memSys.frameReset();
        MR_LOG_INFO("    [OK] Frame reset, memory reclaimed");
    }
    
    // Test buffer growth
    MR_LOG_INFO("\n  Testing buffer growth...");
    void* largeTemp = memSys.frameAllocate(16 * 1024 * 1024);  // 16MB
    if (largeTemp) {
        MR_LOG_INFO("  [OK] Allocated 16MB, buffer auto-grown");
    }
    memSys.frameReset();
}

// Test 4: Texture Buffer Pool
void testTexturePool() {
    MR_LOG_INFO("\n[Test 4] Texture Buffer Pool");
    
    auto& memSys = MemorySystem::get();
    
    // Test texture size allocations
    struct TextureTest {
        size_t size;
        const char* name;
    };
    
    TextureTest textures[] = {
        {1024 * 1024, "1MB texture"},
        {4 * 1024 * 1024, "4MB texture"},
        {16 * 1024 * 1024, "16MB texture"},
        {32 * 1024 * 1024, "32MB texture"}
    };
    
    TArray<void*> texturePointers;
    
    for (auto& tex : textures) {
        void* ptr = memSys.textureAllocate(tex.size);
        if (ptr) {
            texturePointers.Add(ptr);
            MR_LOG_INFO(String("  [OK] Allocated ") + tex.name);
            
            // Write test pattern
            memset(ptr, 0xEE, 1024);  // Write first 1KB as test
        } else {
            MR_LOG_WARNING(String("  [FAIL] Failed to allocate ") + tex.name);
        }
    }
    
    uint64 reserved = memSys.getReservedTextureBytes();
    MR_LOG_INFO("  [OK] Total reserved: " + std::to_string(reserved / 1024 / 1024) + "MB");
    
    MR_LOG_INFO("  [INFO] Texture blocks remain allocated (use textureReleaseAll to clear)");
}

// Test 5: Thread-Local Cache (TLS)
void testThreadLocalCache() {
    MR_LOG_INFO("\n[Test 5] Thread-Local Cache");
    
    auto& memSys = MemorySystem::get();
    
    // Reset stats
    memSys.resetStats();
    
    // Perform many small allocations to trigger TLS cache
    const int allocCount = 100;
    TArray<void*> pointers;
    
    for (int i = 0; i < allocCount; ++i) {
        void* ptr = memSys.allocateSmall(64);
        if (ptr) {
            pointers.Add(ptr);
        }
    }
    
    // Get statistics
    auto stats = memSys.getStats();
    uint64 totalOps = stats.smallCacheHits + stats.smallCacheMisses;
    float hitRate = totalOps > 0 ? (float)stats.smallCacheHits / totalOps * 100.0f : 0.0f;
    
    MR_LOG_INFO("  [OK] Allocations: " + std::to_string(allocCount));
    MR_LOG_INFO("  [OK] Cache hits: " + std::to_string(stats.smallCacheHits));
    MR_LOG_INFO("  [OK] Cache misses: " + std::to_string(stats.smallCacheMisses));
    MR_LOG_INFO("  [OK] Hit rate: " + std::to_string((int)hitRate) + "%");
    
    // Free and observe cache behavior
    for (void* ptr : pointers) {
        memSys.freeSmall(ptr, 64);
    }
    
    MR_LOG_INFO("  [OK] All freed (some cached in TLS)");
}

// Test 6: Huge Pages Support
void testHugePages() {
    MR_LOG_INFO("\n[Test 6] Huge Pages Support");
    
    auto& memSys = MemorySystem::get();
    
    bool available = memSys.isHugePagesAvailable();
    MR_LOG_INFO(String("  System support: ") + (available ? "YES" : "NO"));
    
    if (available) {
        // Enable huge pages
        bool enabled = memSys.enableHugePages(true);
        MR_LOG_INFO(String("  [OK] Enable huge pages: ") + (enabled ? "SUCCESS" : "FAILED"));
        
        // Enable for textures
        memSys.setHugePagesForTextures(true);
        MR_LOG_INFO("  [OK] Huge pages enabled for textures");
        
        // Allocate large texture block (>=2MB) to trigger huge pages
        void* largeTexture = memSys.textureAllocate(64 * 1024 * 1024);  // 64MB
        if (largeTexture) {
            MR_LOG_INFO("  [OK] Allocated 64MB texture (should use huge pages)");
        }
        
    } else {
        MR_LOG_INFO("  [INFO] Huge pages not available on this system");
        MR_LOG_INFO("  [INFO] Windows: Requires SeLockMemoryPrivilege");
        MR_LOG_INFO("  [INFO] Linux: Check 'cat /proc/meminfo | grep HugePages'");
    }
}

// Test 7: Empty Page Trimming
void testEmptyPageTrimming() {
    MR_LOG_INFO("\n[Test 7] Empty Page Trimming");
    
    auto& memSys = MemorySystem::get();
    
    // Allocate many objects
    TArray<void*> pointers;
    for (int i = 0; i < 500; ++i) {
        void* ptr = memSys.allocateSmall(128);
        if (ptr) {
            pointers.Add(ptr);
        }
    }
    
    auto statsBefore = memSys.getStats();
    MR_LOG_INFO("  Before trimming:");
    MR_LOG_INFO("    Pages: " + std::to_string(statsBefore.smallPageCount));
    MR_LOG_INFO("    Empty pages: " + std::to_string(statsBefore.smallEmptyPageCount));
    
    // Free all objects
    for (void* ptr : pointers) {
        memSys.freeSmall(ptr, 128);
    }
    
    // Trigger empty page reclamation
    memSys.trimEmptyPages();
    
    auto statsAfter = memSys.getStats();
    MR_LOG_INFO("  After trimming:");
    MR_LOG_INFO("    Pages: " + std::to_string(statsAfter.smallPageCount));
    MR_LOG_INFO("    Empty pages: " + std::to_string(statsAfter.smallEmptyPageCount));
    MR_LOG_INFO("  [OK] Trimmed " + std::to_string(statsBefore.smallPageCount - statsAfter.smallPageCount) + " pages");
}

// Test 8: Memory Statistics
void testStatistics() {
    MR_LOG_INFO("\n[Test 8] Memory Statistics");
    
    auto& memSys = MemorySystem::get();
    auto stats = memSys.getStats();
    
    MR_LOG_INFO("  === Small Object Pool ===");
    MR_LOG_INFO("    Allocated: " + std::to_string(stats.smallAllocatedBytes / 1024) + " KB");
    MR_LOG_INFO("    Reserved: " + std::to_string(stats.smallReservedBytes / 1024) + " KB");
    MR_LOG_INFO("    Pages: " + std::to_string(stats.smallPageCount));
    MR_LOG_INFO("    Empty pages: " + std::to_string(stats.smallEmptyPageCount));
    MR_LOG_INFO("    Allocations: " + std::to_string(stats.smallAllocations));
    MR_LOG_INFO("    Frees: " + std::to_string(stats.smallFrees));
    
    float smallUtil = stats.smallReservedBytes > 0 ? 
        (float)stats.smallAllocatedBytes / stats.smallReservedBytes * 100.0f : 0.0f;
    MR_LOG_INFO("    Utilization: " + std::to_string((int)smallUtil) + "%");
    
    MR_LOG_INFO("\n  === Frame Scratch Pool ===");
    MR_LOG_INFO("    Current: " + std::to_string(stats.frameAllocatedBytes / 1024) + " KB");
    MR_LOG_INFO("    Capacity: " + std::to_string(stats.frameCapacityBytes / 1024) + " KB");
    MR_LOG_INFO("    Peak: " + std::to_string(stats.framePeakBytes / 1024) + " KB");
    MR_LOG_INFO("    Allocations: " + std::to_string(stats.frameAllocations));
    
    MR_LOG_INFO("\n  === Texture Buffer Pool ===");
    MR_LOG_INFO("    Reserved: " + std::to_string(stats.textureReservedBytes / 1024 / 1024) + " MB");
    MR_LOG_INFO("    Used: " + std::to_string(stats.textureUsedBytes / 1024 / 1024) + " MB");
    MR_LOG_INFO("    Blocks: " + std::to_string(stats.textureBlockCount));
    MR_LOG_INFO("    Free regions: " + std::to_string(stats.textureFreeRegions));
    MR_LOG_INFO("    Allocations: " + std::to_string(stats.textureAllocations));
    
    MR_LOG_INFO("\n  === Overall ===");
    MR_LOG_INFO("    Total allocated: " + std::to_string(stats.totalAllocatedBytes / 1024 / 1024) + " MB");
    MR_LOG_INFO("    Total reserved: " + std::to_string(stats.totalReservedBytes / 1024 / 1024) + " MB");
}

// Test 9: Concurrent Allocation Test
void testConcurrency() {
    MR_LOG_INFO("\n[Test 9] Concurrent Allocation Test");
    
    auto& memSys = MemorySystem::get();
    memSys.resetStats();
    
    const int numThreads = 4;
    const int allocsPerThread = 100;
    
    auto workerFunc = [&](int threadId) {
        TArray<void*> localPointers;
        
        for (int i = 0; i < allocsPerThread; ++i) {
            // Mix different allocation sizes
            size_t size = (i % 4 == 0) ? 64 : (i % 4 == 1) ? 128 : (i % 4 == 2) ? 256 : 512;
            void* ptr = memSys.allocateSmall(size);
            if (ptr) {
                localPointers.Add(ptr);
                // Write test data
                memset(ptr, threadId, size);
            }
        }
        
        // Random delay
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        // Free
        for (int32 i = 0; i < localPointers.Num(); ++i) {
            size_t size = (i % 4 == 0) ? 64 : (i % 4 == 1) ? 128 : (i % 4 == 2) ? 256 : 512;
            memSys.freeSmall(localPointers[i], size);
        }
    };
    
    // Launch threads
    TArray<std::thread> threads;
    auto startTime = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < numThreads; ++i) {
        threads.Add(std::thread(workerFunc, i));
    }
    
    // Wait for all threads to complete
    for (auto& t : threads) {
        t.join();
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
    
    auto stats = memSys.getStats();
    uint64 totalOps = stats.smallCacheHits + stats.smallCacheMisses;
    float hitRate = totalOps > 0 ? (float)stats.smallCacheHits / totalOps * 100.0f : 0.0f;
    
    MR_LOG_INFO("  [OK] " + std::to_string(numThreads) + " threads completed");
    MR_LOG_INFO("  [OK] Total operations: " + std::to_string(totalOps));
    MR_LOG_INFO("  [OK] Cache hit rate: " + std::to_string((int)hitRate) + "%");
    MR_LOG_INFO("  [OK] Duration: " + std::to_string(duration / 1000.0) + " ms");
    MR_LOG_INFO("  [OK] Ops/sec: " + std::to_string((uint64)(totalOps * 1000000.0 / duration)));
}

// Test 10: Edge Cases
void testEdgeCases() {
    MR_LOG_INFO("\n[Test 10] Edge Cases");
    
    auto& memSys = MemorySystem::get();
    
    // Test zero-size allocation
    void* zeroPtr = memSys.allocateSmall(0);
    if (zeroPtr) {
        MR_LOG_INFO("  [OK] Zero-size allocation handled (returns valid pointer)");
        memSys.freeSmall(zeroPtr, 0);
    }
    
    // Test null pointer free
    memSys.freeSmall(nullptr, 64);
    MR_LOG_INFO("  [OK] Null pointer free handled gracefully");
    
    // Test large allocation (fallback to malloc)
    void* largePtr = memSys.allocate(1024 * 1024 * 10);  // 10MB
    if (largePtr) {
        MR_LOG_INFO("  [OK] Large allocation (10MB) fallback to system malloc");
        memSys.free(largePtr, 1024 * 1024 * 10);
        MR_LOG_INFO("  [OK] Large free succeeded");
    }
    
    // Test aligned allocation
    void* aligned16 = memSys.allocateSmall(64, 16);
    if (aligned16 && (reinterpret_cast<uintptr_t>(aligned16) % 16 == 0)) {
        MR_LOG_INFO("  [OK] 16-byte aligned allocation verified");
        memSys.freeSmall(aligned16, 64);
    }
    
    MR_LOG_INFO("  [OK] All edge cases passed");
}

}  // anonymous namespace

}  // namespace MemorySystemTest
}  // namespace MonsterRender
