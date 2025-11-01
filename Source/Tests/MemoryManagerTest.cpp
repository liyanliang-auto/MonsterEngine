// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Memory Management System Test Suite

#include "Core/CoreMinimal.h"
#include "Core/HAL/FMemory.h"
#include "Core/HAL/FMalloc.h"
#include "Core/HAL/FMallocBinned2.h"
#include "Core/HAL/FMemoryManager.h"
#include "Core/Log.h"
#include "Core/Assert.h"

#include <vector>
#include <thread>
#include <chrono>
#include <random>

namespace MonsterRender {
namespace MemorySystemTest {

// ============================================================================
// Test Helper Classes
// ============================================================================

/**
 * Test result tracking
 */
struct TestResult {
    String testName;
    bool passed;
    String errorMessage;
    double durationMs;
};

class TestRunner {
public:
    static TestRunner& Get() {
        static TestRunner instance;
        return instance;
    }

    void AddResult(const TestResult& result) {
        results.push_back(result);
        if (result.passed) {
            passedCount++;
            MR_LOG_INFO("✓ PASSED: " + result.testName + 
                       " (" + std::to_string(result.durationMs) + "ms)");
        } else {
            failedCount++;
            MR_LOG_ERROR("✗ FAILED: " + result.testName + 
                        " - " + result.errorMessage);
        }
    }

    void PrintSummary() {
        MR_LOG_INFO("\n======================================");
        MR_LOG_INFO("  Test Summary");
        MR_LOG_INFO("======================================");
        MR_LOG_INFO("Total Tests: " + std::to_string(results.size()));
        MR_LOG_INFO("Passed: " + std::to_string(passedCount));
        MR_LOG_INFO("Failed: " + std::to_string(failedCount));
        
        if (failedCount == 0) {
            MR_LOG_INFO("\n🎉 All tests passed!");
        } else {
            MR_LOG_ERROR("\n⚠️ " + std::to_string(failedCount) + " test(s) failed");
        }
        MR_LOG_INFO("======================================\n");
    }

    void Reset() {
        results.clear();
        passedCount = 0;
        failedCount = 0;
    }

private:
    std::vector<TestResult> results;
    uint32 passedCount = 0;
    uint32 failedCount = 0;
};

// Test timing helper
class ScopedTestTimer {
public:
    ScopedTestTimer(const String& name) 
        : testName(name)
        , startTime(std::chrono::high_resolution_clock::now()) {}

    ~ScopedTestTimer() {
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
        durationMs = duration.count() / 1000.0;
    }

    void Success() {
        TestResult result;
        result.testName = testName;
        result.passed = true;
        result.durationMs = durationMs;
        TestRunner::Get().AddResult(result);
    }

    void Failure(const String& error) {
        TestResult result;
        result.testName = testName;
        result.passed = false;
        result.errorMessage = error;
        result.durationMs = durationMs;
        TestRunner::Get().AddResult(result);
    }

private:
    String testName;
    std::chrono::high_resolution_clock::time_point startTime;
    double durationMs = 0.0;
};

// ============================================================================
// FMemory Basic Operations Tests
// ============================================================================

void TestFMemoryBasicOperations() {
    ScopedTestTimer timer("FMemory::Basic Operations");

    try {
        // Test Memcpy
        char src[100] = "Hello, MonsterEngine!";
        char dst[100] = {};
        FMemory::Memcpy(dst, src, strlen(src) + 1);
        if (strcmp(dst, src) != 0) {
            timer.Failure("Memcpy failed");
            return;
        }

        // Test Memset
        char buffer[100];
        FMemory::Memset(buffer, 0xAB, 100);
        for (int i = 0; i < 100; ++i) {
            if ((uint8)buffer[i] != 0xAB) {
                timer.Failure("Memset failed");
                return;
            }
        }

        // Test Memzero
        FMemory::Memzero(buffer, 100);
        for (int i = 0; i < 100; ++i) {
            if (buffer[i] != 0) {
                timer.Failure("Memzero failed");
                return;
            }
        }

        // Test Memcmp
        char buf1[50] = "TestData";
        char buf2[50] = "TestData";
        if (FMemory::Memcmp(buf1, buf2, 8) != 0) {
            timer.Failure("Memcmp failed");
            return;
        }

        timer.Success();
    }
    catch (const std::exception& e) {
        timer.Failure(String("Exception: ") + e.what());
    }
}

void TestFMemoryAlignment() {
    ScopedTestTimer timer("FMemory::Alignment Check");

    try {
        // Test alignment checking
        alignas(16) char buffer[256];
        if (!FMemory::IsAligned(buffer, 16)) {
            timer.Failure("IsAligned failed for 16-byte aligned buffer");
            return;
        }

        // Test unaligned pointer
        char* unaligned = buffer + 1;
        if (FMemory::IsAligned(unaligned, 16)) {
            timer.Failure("IsAligned incorrectly reported unaligned pointer as aligned");
            return;
        }

        timer.Success();
    }
    catch (const std::exception& e) {
        timer.Failure(String("Exception: ") + e.what());
    }
}

// ============================================================================
// FMemoryManager Tests
// ============================================================================

void TestFMemoryManagerInitialization() {
    ScopedTestTimer timer("FMemoryManager::Initialization");

    try {
        FMemoryManager& memMgr = FMemoryManager::Get();
        
        if (!memMgr.Initialize()) {
            timer.Failure("FMemoryManager initialization failed");
            return;
        }

        FMalloc* allocator = memMgr.GetAllocator();
        if (!allocator) {
            timer.Failure("FMemoryManager has no allocator");
            return;
        }

        // Test system capabilities
        FMemoryManager::FGlobalMemoryStats stats;
        memMgr.GetGlobalMemoryStats(stats);

        MR_LOG_DEBUG("  Total Physical Memory: " + 
                    std::to_string(stats.TotalPhysicalMemory / (1024 * 1024)) + " MB");
        MR_LOG_DEBUG("  Available Physical Memory: " + 
                    std::to_string(stats.AvailablePhysicalMemory / (1024 * 1024)) + " MB");
        MR_LOG_DEBUG("  Page Size: " + std::to_string(stats.PageSize) + " bytes");

        timer.Success();
    }
    catch (const std::exception& e) {
        timer.Failure(String("Exception: ") + e.what());
    }
}

void TestFMemoryManagerAllocation() {
    ScopedTestTimer timer("FMemoryManager::Basic Allocation");

    try {
        // Test allocation through FMemory
        void* ptr1 = FMemory::Malloc(1024);
        if (!ptr1) {
            timer.Failure("Failed to allocate 1024 bytes");
            return;
        }

        // Test writing to allocated memory
        FMemory::Memset(ptr1, 0xCC, 1024);

        // Test reallocation
        void* ptr2 = FMemory::Realloc(ptr1, 2048);
        if (!ptr2) {
            FMemory::Free(ptr1);
            timer.Failure("Failed to reallocate to 2048 bytes");
            return;
        }

        // Verify data is preserved
        uint8* data = static_cast<uint8*>(ptr2);
        for (SIZE_T i = 0; i < 1024; ++i) {
            if (data[i] != 0xCC) {
                FMemory::Free(ptr2);
                timer.Failure("Data corrupted after reallocation");
                return;
            }
        }

        FMemory::Free(ptr2);

        timer.Success();
    }
    catch (const std::exception& e) {
        timer.Failure(String("Exception: ") + e.what());
    }
}

// ============================================================================
// FMallocBinned2 Specific Tests
// ============================================================================

void TestFMallocBinned2SmallAllocations() {
    ScopedTestTimer timer("FMallocBinned2::Small Allocations (16-1024 bytes)");

    try {
        FMalloc* allocator = FMemoryManager::Get().GetAllocator();

        // Test various small sizes
        const SIZE_T sizes[] = {16, 32, 64, 128, 256, 512, 1024};
        void* pointers[7];

        for (int i = 0; i < 7; ++i) {
            pointers[i] = allocator->Malloc(sizes[i]);
            if (!pointers[i]) {
                timer.Failure("Failed to allocate " + std::to_string(sizes[i]) + " bytes");
                return;
            }

            // Fill with pattern
            FMemory::Memset(pointers[i], i + 1, sizes[i]);
        }

        // Verify all allocations
        for (int i = 0; i < 7; ++i) {
            uint8* data = static_cast<uint8*>(pointers[i]);
            for (SIZE_T j = 0; j < sizes[i]; ++j) {
                if (data[j] != (uint8)(i + 1)) {
                    timer.Failure("Data corruption in allocation " + std::to_string(i));
                    return;
                }
            }
        }

        // Free all
        for (int i = 0; i < 7; ++i) {
            allocator->Free(pointers[i]);
        }

        timer.Success();
    }
    catch (const std::exception& e) {
        timer.Failure(String("Exception: ") + e.what());
    }
}

void TestFMallocBinned2LargeAllocations() {
    ScopedTestTimer timer("FMallocBinned2::Large Allocations (>1024 bytes)");

    try {
        FMalloc* allocator = FMemoryManager::Get().GetAllocator();

        // Test large allocations (should bypass binned allocator)
        const SIZE_T sizes[] = {2048, 4096, 8192, 16384, 65536};
        void* pointers[5];

        for (int i = 0; i < 5; ++i) {
            pointers[i] = allocator->Malloc(sizes[i]);
            if (!pointers[i]) {
                timer.Failure("Failed to allocate " + std::to_string(sizes[i]) + " bytes");
                return;
            }

            // Fill with pattern
            uint32* data = static_cast<uint32*>(pointers[i]);
            for (SIZE_T j = 0; j < sizes[i] / sizeof(uint32); ++j) {
                data[j] = 0xDEADBEEF + i;
            }
        }

        // Verify
        for (int i = 0; i < 5; ++i) {
            uint32* data = static_cast<uint32*>(pointers[i]);
            for (SIZE_T j = 0; j < sizes[i] / sizeof(uint32); ++j) {
                if (data[j] != (uint32)(0xDEADBEEF + i)) {
                    timer.Failure("Data corruption in large allocation " + std::to_string(i));
                    return;
                }
            }
        }

        // Free all
        for (int i = 0; i < 5; ++i) {
            allocator->Free(pointers[i]);
        }

        timer.Success();
    }
    catch (const std::exception& e) {
        timer.Failure(String("Exception: ") + e.what());
    }
}

void TestFMallocBinned2AlignedAllocations() {
    ScopedTestTimer timer("FMallocBinned2::Aligned Allocations");

    try {
        FMalloc* allocator = FMemoryManager::Get().GetAllocator();

        // Test various alignments
        const uint32 alignments[] = {16, 32, 64, 128, 256};
        void* pointers[5];

        for (int i = 0; i < 5; ++i) {
            pointers[i] = allocator->Malloc(512, alignments[i]);
            if (!pointers[i]) {
                timer.Failure("Failed to allocate with " + std::to_string(alignments[i]) + 
                            " byte alignment");
                return;
            }

            // Check alignment
            if (!FMemory::IsAligned(pointers[i], alignments[i])) {
                timer.Failure("Allocation not properly aligned to " + 
                            std::to_string(alignments[i]) + " bytes");
                return;
            }
        }

        // Free all
        for (int i = 0; i < 5; ++i) {
            allocator->Free(pointers[i]);
        }

        timer.Success();
    }
    catch (const std::exception& e) {
        timer.Failure(String("Exception: ") + e.what());
    }
}

void TestFMallocBinned2Statistics() {
    ScopedTestTimer timer("FMallocBinned2::Statistics Tracking");

    try {
        FMalloc* allocator = FMemoryManager::Get().GetAllocator();

        // Get baseline stats
        FMalloc::FMemoryStats statsBefore;
        allocator->GetMemoryStats(statsBefore);

        // Allocate some memory
        std::vector<void*> allocations;
        for (int i = 0; i < 100; ++i) {
            void* ptr = allocator->Malloc(64);
            if (ptr) {
                allocations.push_back(ptr);
            }
        }

        // Get new stats
        FMalloc::FMemoryStats statsAfter;
        allocator->GetMemoryStats(statsAfter);

        MR_LOG_DEBUG("  Total Allocated: " + 
                    std::to_string(statsAfter.TotalAllocated / 1024) + " KB");
        MR_LOG_DEBUG("  Total Reserved: " + 
                    std::to_string(statsAfter.TotalReserved / 1024) + " KB");
        MR_LOG_DEBUG("  Allocation Count: " + std::to_string(statsAfter.AllocationCount));

        // Cleanup
        for (void* ptr : allocations) {
            allocator->Free(ptr);
        }

        // Validate heap
        if (!allocator->ValidateHeap()) {
            timer.Failure("Heap validation failed");
            return;
        }

        timer.Success();
    }
    catch (const std::exception& e) {
        timer.Failure(String("Exception: ") + e.what());
    }
}

// ============================================================================
// Stress Tests
// ============================================================================

void TestRandomAllocationPattern() {
    ScopedTestTimer timer("Stress Test::Random Allocation Pattern");

    try {
        FMalloc* allocator = FMemoryManager::Get().GetAllocator();
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> sizeDist(16, 4096);
        
        std::vector<void*> allocations;
        const int numIterations = 1000;

        for (int i = 0; i < numIterations; ++i) {
            SIZE_T size = sizeDist(gen);
            void* ptr = allocator->Malloc(size);
            
            if (!ptr) {
                timer.Failure("Allocation failed at iteration " + std::to_string(i));
                return;
            }

            allocations.push_back(ptr);

            // Randomly free some allocations
            if (allocations.size() > 100 && (gen() % 3 == 0)) {
                std::uniform_int_distribution<> indexDist(0, allocations.size() - 1);
                int indexToFree = indexDist(gen);
                allocator->Free(allocations[indexToFree]);
                allocations.erase(allocations.begin() + indexToFree);
            }
        }

        // Free remaining allocations
        for (void* ptr : allocations) {
            allocator->Free(ptr);
        }

        if (!allocator->ValidateHeap()) {
            timer.Failure("Heap validation failed after stress test");
            return;
        }

        timer.Success();
    }
    catch (const std::exception& e) {
        timer.Failure(String("Exception: ") + e.what());
    }
}

void TestMultithreadedAllocations() {
    ScopedTestTimer timer("Stress Test::Multi-threaded Allocations");

    try {
        const int numThreads = 4;
        const int allocationsPerThread = 500;
        std::atomic<bool> failed{false};
        String errorMsg;

        auto workerFunc = [&](int threadId) {
            try {
                FMalloc* allocator = FMemoryManager::Get().GetAllocator();
                std::vector<void*> localAllocations;

                for (int i = 0; i < allocationsPerThread; ++i) {
                    SIZE_T size = 16 + (threadId * 16) + (i % 512);
                    void* ptr = allocator->Malloc(size);
                    
                    if (!ptr) {
                        failed = true;
                        errorMsg = "Thread " + std::to_string(threadId) + 
                                 " failed allocation at iteration " + std::to_string(i);
                        return;
                    }

                    // Write pattern
                    FMemory::Memset(ptr, threadId + 1, size);
                    localAllocations.push_back(ptr);

                    // Occasionally free
                    if (i % 10 == 0 && !localAllocations.empty()) {
                        allocator->Free(localAllocations.back());
                        localAllocations.pop_back();
                    }
                }

                // Free all remaining
                for (void* ptr : localAllocations) {
                    allocator->Free(ptr);
                }
            }
            catch (const std::exception& e) {
                failed = true;
                errorMsg = "Thread " + std::to_string(threadId) + 
                         " exception: " + e.what();
            }
        };

        // Launch threads
        std::vector<std::thread> threads;
        for (int i = 0; i < numThreads; ++i) {
            threads.emplace_back(workerFunc, i);
        }

        // Wait for completion
        for (auto& thread : threads) {
            thread.join();
        }

        if (failed) {
            timer.Failure(errorMsg);
            return;
        }

        // Validate heap after all threads complete
        FMalloc* allocator = FMemoryManager::Get().GetAllocator();
        if (!allocator->ValidateHeap()) {
            timer.Failure("Heap validation failed after multi-threaded test");
            return;
        }

        timer.Success();
    }
    catch (const std::exception& e) {
        timer.Failure(String("Exception: ") + e.what());
    }
}

// ============================================================================
// Edge Cases and Error Handling
// ============================================================================

void TestEdgeCases() {
    ScopedTestTimer timer("Edge Cases::Null and Zero Size");

    try {
        FMalloc* allocator = FMemoryManager::Get().GetAllocator();

        // Test zero-size allocation
        void* ptr = allocator->Malloc(0);
        // Should either return nullptr or valid pointer (implementation-defined)
        if (ptr) {
            allocator->Free(ptr);
        }

        // Test freeing nullptr (should not crash)
        allocator->Free(nullptr);

        // Test realloc with nullptr (should act like malloc)
        void* ptr2 = allocator->Realloc(nullptr, 256);
        if (!ptr2) {
            timer.Failure("Realloc(nullptr, 256) failed");
            return;
        }
        allocator->Free(ptr2);

        // Test realloc to zero size (should act like free)
        void* ptr3 = allocator->Malloc(128);
        if (!ptr3) {
            timer.Failure("Malloc(128) failed");
            return;
        }
        void* ptr4 = allocator->Realloc(ptr3, 0);
        // ptr4 should be nullptr or valid (implementation-defined)
        if (ptr4) {
            allocator->Free(ptr4);
        }

        timer.Success();
    }
    catch (const std::exception& e) {
        timer.Failure(String("Exception: ") + e.what());
    }
}

// ============================================================================
// Main Test Runner
// ============================================================================

void runAllTests() {
    TestRunner::Get().Reset();

    MR_LOG_INFO("Starting Memory Management System Tests...\n");

    // Basic FMemory tests
    MR_LOG_INFO("--- FMemory Basic Tests ---");
    TestFMemoryBasicOperations();
    TestFMemoryAlignment();

    // FMemoryManager tests
    MR_LOG_INFO("\n--- FMemoryManager Tests ---");
    TestFMemoryManagerInitialization();
    TestFMemoryManagerAllocation();

    // FMallocBinned2 tests
    MR_LOG_INFO("\n--- FMallocBinned2 Tests ---");
    TestFMallocBinned2SmallAllocations();
    TestFMallocBinned2LargeAllocations();
    TestFMallocBinned2AlignedAllocations();
    TestFMallocBinned2Statistics();

    // Stress tests
    MR_LOG_INFO("\n--- Stress Tests ---");
    TestRandomAllocationPattern();
    TestMultithreadedAllocations();

    // Edge cases
    MR_LOG_INFO("\n--- Edge Cases ---");
    TestEdgeCases();

    // Print summary
    TestRunner::Get().PrintSummary();
}

} // namespace MemorySystemTest
} // namespace MonsterRender

