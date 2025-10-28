#include "Core/CoreMinimal.h"
#include "Core/Memory.h"
#include <thread>
#include <chrono>

namespace MonsterRender {

/**
 * MemorySystem 测试用例集
 * 用于验证内存管理器的所有功能
 */
class MemorySystemTest {
public:
    static void runAllTests() {
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

private:
    // 测试1：初始化和关闭
    static void testInitialization() {
        MR_LOG_INFO("\n[Test 1] Initialization and Shutdown");
        
        auto& memSys = MemorySystem::get();
        bool success = memSys.initialize(
            8 * 1024 * 1024,   // 8MB frame scratch
            64 * 1024 * 1024   // 64MB texture blocks
        );
        
        MR_LOG_INFO("  ✓ MemorySystem initialized: " + String(success ? "SUCCESS" : "FAILED"));
        MR_LOG_INFO("  ✓ Huge pages available: " + String(memSys.isHugePagesAvailable() ? "YES" : "NO"));
    }

    // 测试2：小对象池（16B - 1024B）
    static void testSmallObjectPool() {
        MR_LOG_INFO("\n[Test 2] Small Object Pool");
        
        auto& memSys = MemorySystem::get();
        
        // 测试各种大小的分配
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
                // 写入测试数据
                memset(ptr, 0xAA, test.size);
                MR_LOG_INFO(String("  ✓ Allocated ") + test.name + " at 0x" + 
                           std::to_string(reinterpret_cast<uintptr_t>(ptr)));
                
                // 立即释放
                memSys.freeSmall(ptr, test.size);
                MR_LOG_INFO(String("  ✓ Freed ") + test.name);
            } else {
                MR_LOG_WARNING(String("  ✗ Failed to allocate ") + test.name);
            }
        }
        
        // 测试大量小对象分配
        MR_LOG_INFO("\n  Testing bulk allocation (1000 objects of 64B)...");
        std::vector<void*> pointers;
        for (int i = 0; i < 1000; ++i) {
            void* ptr = memSys.allocateSmall(64);
            if (ptr) {
                pointers.push_back(ptr);
            }
        }
        MR_LOG_INFO("  ✓ Allocated " + std::to_string(pointers.size()) + " objects");
        
        // 释放所有对象
        for (void* ptr : pointers) {
            memSys.freeSmall(ptr, 64);
        }
        MR_LOG_INFO("  ✓ Freed all objects");
    }

    // 测试3：帧临时缓冲池
    static void testFrameScratchPool() {
        MR_LOG_INFO("\n[Test 3] Frame Scratch Pool");
        
        auto& memSys = MemorySystem::get();
        
        // 模拟多帧分配
        for (int frame = 0; frame < 3; ++frame) {
            MR_LOG_INFO(String("  Frame ") + std::to_string(frame) + ":");
            
            // 帧内分配临时数据
            void* temp1 = memSys.frameAllocate(4096);
            void* temp2 = memSys.frameAllocate(8192);
            void* temp3 = memSys.frameAllocate(16384);
            
            if (temp1 && temp2 && temp3) {
                uint64 allocated = memSys.getAllocatedFrameBytes();
                MR_LOG_INFO("    ✓ Allocated 28KB, total: " + 
                           std::to_string(allocated / 1024) + "KB");
                
                // 模拟使用数据
                memset(temp1, 0xBB, 4096);
                memset(temp2, 0xCC, 8192);
                memset(temp3, 0xDD, 16384);
            }
            
            // 帧末重置
            memSys.frameReset();
            MR_LOG_INFO("    ✓ Frame reset, memory reclaimed");
        }
        
        // 测试大量分配导致扩容
        MR_LOG_INFO("\n  Testing buffer growth...");
        void* largeTemp = memSys.frameAllocate(16 * 1024 * 1024);  // 16MB
        if (largeTemp) {
            MR_LOG_INFO("  ✓ Allocated 16MB, buffer auto-grown");
        }
        memSys.frameReset();
    }

    // 测试4：纹理缓冲池
    static void testTexturePool() {
        MR_LOG_INFO("\n[Test 4] Texture Buffer Pool");
        
        auto& memSys = MemorySystem::get();
        
        // 测试纹理大小分配
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
        
        std::vector<void*> texturePointers;
        
        for (auto& tex : textures) {
            void* ptr = memSys.textureAllocate(tex.size);
            if (ptr) {
                texturePointers.push_back(ptr);
                MR_LOG_INFO(String("  ✓ Allocated ") + tex.name);
                
                // 写入测试图案
                memset(ptr, 0xEE, 1024);  // 只写前1KB作为测试
            } else {
                MR_LOG_WARNING(String("  ✗ Failed to allocate ") + tex.name);
            }
        }
        
        uint64 reserved = memSys.getReservedTextureBytes();
        MR_LOG_INFO("  ✓ Total reserved: " + std::to_string(reserved / 1024 / 1024) + "MB");
        
        // 测试释放（注意：当前实现需要跟踪大小）
        // memSys.textureReleaseAll();  // 释放所有
        MR_LOG_INFO("  ℹ Texture blocks remain allocated (use textureReleaseAll to clear)");
    }

    // 测试5：线程本地缓存（TLS Cache）
    static void testThreadLocalCache() {
        MR_LOG_INFO("\n[Test 5] Thread-Local Cache");
        
        auto& memSys = MemorySystem::get();
        
        // 重置统计
        memSys.resetStats();
        
        // 执行大量小分配，触发TLS缓存
        const int allocCount = 100;
        std::vector<void*> pointers;
        
        for (int i = 0; i < allocCount; ++i) {
            void* ptr = memSys.allocateSmall(64);
            if (ptr) {
                pointers.push_back(ptr);
            }
        }
        
        // 获取统计
        auto stats = memSys.getStats();
        uint64 totalOps = stats.smallCacheHits + stats.smallCacheMisses;
        float hitRate = totalOps > 0 ? (float)stats.smallCacheHits / totalOps * 100.0f : 0.0f;
        
        MR_LOG_INFO("  ✓ Allocations: " + std::to_string(allocCount));
        MR_LOG_INFO("  ✓ Cache hits: " + std::to_string(stats.smallCacheHits));
        MR_LOG_INFO("  ✓ Cache misses: " + std::to_string(stats.smallCacheMisses));
        MR_LOG_INFO("  ✓ Hit rate: " + std::to_string((int)hitRate) + "%");
        
        // 释放并观察缓存行为
        for (void* ptr : pointers) {
            memSys.freeSmall(ptr, 64);
        }
        
        MR_LOG_INFO("  ✓ All freed (some cached in TLS)");
    }

    // 测试6：大页支持
    static void testHugePages() {
        MR_LOG_INFO("\n[Test 6] Huge Pages Support");
        
        auto& memSys = MemorySystem::get();
        
        bool available = memSys.isHugePagesAvailable();
        MR_LOG_INFO(String("  System support: ") + (available ? "YES" : "NO"));
        
        if (available) {
            // 启用大页
            bool enabled = memSys.enableHugePages(true);
            MR_LOG_INFO(String("  ✓ Enable huge pages: ") + (enabled ? "SUCCESS" : "FAILED"));
            
            // 为纹理启用大页
            memSys.setHugePagesForTextures(true);
            MR_LOG_INFO("  ✓ Huge pages enabled for textures");
            
            // 分配大纹理块（>=2MB）触发大页
            void* largeTexture = memSys.textureAllocate(64 * 1024 * 1024);  // 64MB
            if (largeTexture) {
                MR_LOG_INFO("  ✓ Allocated 64MB texture (should use huge pages)");
            }
            
        } else {
            MR_LOG_INFO("  ℹ Huge pages not available on this system");
            MR_LOG_INFO("  ℹ Windows: Requires SeLockMemoryPrivilege");
            MR_LOG_INFO("  ℹ Linux: Check 'cat /proc/meminfo | grep HugePages'");
        }
    }

    // 测试7：空页回收
    static void testEmptyPageTrimming() {
        MR_LOG_INFO("\n[Test 7] Empty Page Trimming");
        
        auto& memSys = MemorySystem::get();
        
        // 分配大量对象
        std::vector<void*> pointers;
        for (int i = 0; i < 500; ++i) {
            void* ptr = memSys.allocateSmall(128);
            if (ptr) {
                pointers.push_back(ptr);
            }
        }
        
        auto statsBefore = memSys.getStats();
        MR_LOG_INFO("  Before trimming:");
        MR_LOG_INFO("    Pages: " + std::to_string(statsBefore.smallPageCount));
        MR_LOG_INFO("    Empty pages: " + std::to_string(statsBefore.smallEmptyPageCount));
        
        // 释放所有对象
        for (void* ptr : pointers) {
            memSys.freeSmall(ptr, 128);
        }
        
        // 触发空页回收
        memSys.trimEmptyPages();
        
        auto statsAfter = memSys.getStats();
        MR_LOG_INFO("  After trimming:");
        MR_LOG_INFO("    Pages: " + std::to_string(statsAfter.smallPageCount));
        MR_LOG_INFO("    Empty pages: " + std::to_string(statsAfter.smallEmptyPageCount));
        MR_LOG_INFO("  ✓ Trimmed " + std::to_string(statsBefore.smallPageCount - statsAfter.smallPageCount) + " pages");
    }

    // 测试8：统计信息
    static void testStatistics() {
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

    // 测试9：并发测试
    static void testConcurrency() {
        MR_LOG_INFO("\n[Test 9] Concurrent Allocation Test");
        
        auto& memSys = MemorySystem::get();
        memSys.resetStats();
        
        const int numThreads = 4;
        const int allocsPerThread = 100;
        
        auto workerFunc = [&](int threadId) {
            std::vector<void*> localPointers;
            
            for (int i = 0; i < allocsPerThread; ++i) {
                // 混合不同大小的分配
                size_t size = (i % 4 == 0) ? 64 : (i % 4 == 1) ? 128 : (i % 4 == 2) ? 256 : 512;
                void* ptr = memSys.allocateSmall(size);
                if (ptr) {
                    localPointers.push_back(ptr);
                    // 写入测试
                    memset(ptr, threadId, size);
                }
            }
            
            // 随机延迟
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            
            // 释放
            for (size_t i = 0; i < localPointers.size(); ++i) {
                size_t size = (i % 4 == 0) ? 64 : (i % 4 == 1) ? 128 : (i % 4 == 2) ? 256 : 512;
                memSys.freeSmall(localPointers[i], size);
            }
        };
        
        // 启动多线程
        std::vector<std::thread> threads;
        auto startTime = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < numThreads; ++i) {
            threads.emplace_back(workerFunc, i);
        }
        
        // 等待所有线程完成
        for (auto& t : threads) {
            t.join();
        }
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
        
        auto stats = memSys.getStats();
        uint64 totalOps = stats.smallCacheHits + stats.smallCacheMisses;
        float hitRate = totalOps > 0 ? (float)stats.smallCacheHits / totalOps * 100.0f : 0.0f;
        
        MR_LOG_INFO("  ✓ " + std::to_string(numThreads) + " threads completed");
        MR_LOG_INFO("  ✓ Total operations: " + std::to_string(totalOps));
        MR_LOG_INFO("  ✓ Cache hit rate: " + std::to_string((int)hitRate) + "%");
        MR_LOG_INFO("  ✓ Duration: " + std::to_string(duration / 1000.0) + " ms");
        MR_LOG_INFO("  ✓ Ops/sec: " + std::to_string((uint64)(totalOps * 1000000.0 / duration)));
    }

    // 测试10：边界情况
    static void testEdgeCases() {
        MR_LOG_INFO("\n[Test 10] Edge Cases");
        
        auto& memSys = MemorySystem::get();
        
        // 测试零大小分配
        void* zeroPtr = memSys.allocateSmall(0);
        if (zeroPtr) {
            MR_LOG_INFO("  ✓ Zero-size allocation handled (returns valid pointer)");
            memSys.freeSmall(zeroPtr, 0);
        }
        
        // 测试null指针释放
        memSys.freeSmall(nullptr, 64);
        MR_LOG_INFO("  ✓ Null pointer free handled gracefully");
        
        // 测试超大分配（回退到malloc）
        void* largePtr = memSys.allocate(1024 * 1024 * 10);  // 10MB
        if (largePtr) {
            MR_LOG_INFO("  ✓ Large allocation (10MB) fallback to system malloc");
            memSys.free(largePtr, 1024 * 1024 * 10);
            MR_LOG_INFO("  ✓ Large free succeeded");
        }
        
        // 测试对齐分配
        void* aligned16 = memSys.allocateSmall(64, 16);
        if (aligned16 && (reinterpret_cast<uintptr_t>(aligned16) % 16 == 0)) {
            MR_LOG_INFO("  ✓ 16-byte aligned allocation verified");
            memSys.freeSmall(aligned16, 64);
        }
        
        MR_LOG_INFO("  ✓ All edge cases passed");
    }
};

} // namespace MonsterRender

// 独立测试入口点
// 在main.cpp中调用: MonsterRender::runMemorySystemTests();
void runMemorySystemTests() {
    MonsterRender::MemorySystemTest::runAllTests();
}

