// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - FMemory System Test Suite

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
namespace FMemorySystemTest {

// Test result tracking
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
            MR_LOG_INFO("PASSED: " + result.testName + 
                       " (" + std::to_string(result.durationMs) + "ms)");
        } else {
            failedCount++;
            MR_LOG_ERROR("FAILED: " + result.testName + 
                        " - " + result.errorMessage);
        }
    }

    void PrintSummary() {
        MR_LOG_INFO("\n======================================");
        MR_LOG_INFO("  FMemory System Test Summary");
        MR_LOG_INFO("======================================");
        MR_LOG_INFO("Total Tests: " + std::to_string(results.size()));
        MR_LOG_INFO("Passed: " + std::to_string(passedCount));
        MR_LOG_INFO("Failed: " + std::to_string(failedCount));
        
        if (failedCount == 0) {
            MR_LOG_INFO("\nAll FMemory tests passed!");
        } else {
            MR_LOG_ERROR("\n" + std::to_string(failedCount) + " test(s) failed");
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

// Test Functions
void TestFMemoryBasicOperations() {
    ScopedTestTimer timer("FMemory::Basic Operations");

    try {
        char src[100] = "Hello, MonsterEngine!";
        char dst[100] = {};
        FMemory::Memcpy(dst, src, strlen(src) + 1);
        if (strcmp(dst, src) != 0) {
            timer.Failure("Memcpy failed");
            return;
        }

        char buffer[100];
        FMemory::Memset(buffer, 0xAB, 100);
        for (int i = 0; i < 100; ++i) {
            if ((uint8)buffer[i] != 0xAB) {
                timer.Failure("Memset failed");
                return;
            }
        }

        FMemory::Memzero(buffer, 100);
        for (int i = 0; i < 100; ++i) {
            if (buffer[i] != 0) {
                timer.Failure("Memzero failed");
                return;
            }
        }

        timer.Success();
    }
    catch (const std::exception& e) {
        timer.Failure(String("Exception: ") + e.what());
    }
}

void TestFMemoryManagerInit() {
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

        FMemoryManager::FGlobalMemoryStats stats;
        memMgr.GetGlobalMemoryStats(stats);

        MR_LOG_DEBUG("  Total Physical Memory: " + 
                    std::to_string(stats.TotalPhysicalMemory / (1024 * 1024)) + " MB");

        timer.Success();
    }
    catch (const std::exception& e) {
        timer.Failure(String("Exception: ") + e.what());
    }
}

void TestFMemoryAllocation() {
    ScopedTestTimer timer("FMemory::Basic Allocation");

    try {
        void* ptr1 = FMemory::Malloc(1024);
        if (!ptr1) {
            timer.Failure("Failed to allocate 1024 bytes");
            return;
        }

        FMemory::Memset(ptr1, 0xCC, 1024);

        void* ptr2 = FMemory::Realloc(ptr1, 2048);
        if (!ptr2) {
            FMemory::Free(ptr1);
            timer.Failure("Failed to reallocate to 2048 bytes");
            return;
        }

        FMemory::Free(ptr2);
        timer.Success();
    }
    catch (const std::exception& e) {
        timer.Failure(String("Exception: ") + e.what());
    }
}

void TestFMallocBinned2Small() {
    ScopedTestTimer timer("FMallocBinned2::Small Allocations");

    try {
        FMalloc* allocator = FMemoryManager::Get().GetAllocator();

        const SIZE_T sizes[] = {16, 32, 64, 128, 256, 512, 1024};
        void* pointers[7];

        for (size_t i = 0; i < 7; ++i) {
            pointers[i] = allocator->Malloc(sizes[i]);
            if (!pointers[i]) {
                timer.Failure("Failed to allocate " + std::to_string(sizes[i]) + " bytes");
                return;
            }
            FMemory::Memset(pointers[i], static_cast<uint8>(i + 1), sizes[i]);
        }

        for (size_t i = 0; i < 7; ++i) {
            allocator->Free(pointers[i]);
        }

        timer.Success();
    }
    catch (const std::exception& e) {
        timer.Failure(String("Exception: ") + e.what());
    }
}

void TestMultithreaded() {
    ScopedTestTimer timer("Multi-threaded Allocations");

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
                        errorMsg = "Thread " + std::to_string(threadId) + " failed";
                        return;
                    }

                    FMemory::Memset(ptr, static_cast<uint8>(threadId + 1), size);
                    localAllocations.push_back(ptr);

                    if (i % 10 == 0 && !localAllocations.empty()) {
                        allocator->Free(localAllocations.back());
                        localAllocations.pop_back();
                    }
                }

                for (void* ptr : localAllocations) {
                    allocator->Free(ptr);
                }
            }
            catch (const std::exception& e) {
                failed = true;
                errorMsg = "Thread exception: " + String(e.what());
            }
        };

        std::vector<std::thread> threads;
        for (int i = 0; i < numThreads; ++i) {
            threads.emplace_back(workerFunc, i);
        }

        for (auto& thread : threads) {
            thread.join();
        }

        if (failed) {
            timer.Failure(errorMsg);
            return;
        }

        timer.Success();
    }
    catch (const std::exception& e) {
        timer.Failure(String("Exception: ") + e.what());
    }
}

void runFMemoryTests() {
    TestRunner::Get().Reset();

    MR_LOG_INFO("Starting FMemory System Tests...\n");

    MR_LOG_INFO("--- FMemory Basic Tests ---");
    TestFMemoryBasicOperations();

    MR_LOG_INFO("\n--- FMemoryManager Tests ---");
    TestFMemoryManagerInit();
    TestFMemoryAllocation();

    MR_LOG_INFO("\n--- FMallocBinned2 Tests ---");
    TestFMallocBinned2Small();

    MR_LOG_INFO("\n--- Stress Tests ---");
    TestMultithreaded();

    TestRunner::Get().PrintSummary();
}

} // namespace FMemorySystemTest
} // namespace MonsterRender

