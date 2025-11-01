// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Texture Streaming System Test Suite

#include "Core/CoreMinimal.h"
#include "Core/HAL/FMemory.h"
#include "Core/IO/FAsyncFileIO.h"
#include "Renderer/FTextureStreamingManager.h"
#include "Core/Log.h"
#include "Core/Assert.h"

#include <vector>
#include <thread>
#include <chrono>
#include <random>
#include <fstream>

namespace MonsterRender {
namespace TextureStreamingSystemTest {

// ============================================================================
// Test Infrastructure
// ============================================================================

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
        MR_LOG_INFO("  Texture Streaming Test Summary");
        MR_LOG_INFO("======================================");
        MR_LOG_INFO("Total Tests: " + std::to_string(results.size()));
        MR_LOG_INFO("Passed: " + std::to_string(passedCount));
        MR_LOG_INFO("Failed: " + std::to_string(failedCount));
        
        if (failedCount == 0) {
            MR_LOG_INFO("\nAll texture streaming tests passed!");
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

// Mock FTexture class for testing
class FTexture {
public:
    FTexture(const String& name, uint32 width, uint32 height, uint32 mipLevels)
        : Name(name), Width(width), Height(height), TotalMipLevels(mipLevels), ResidentMips(1) {
        
        // Calculate mip sizes (each mip is 1/4 the size of previous)
        for (uint32 i = 0; i < TotalMipLevels && i < 16; ++i) {
            uint32 mipWidth = std::max(1u, Width >> i);
            uint32 mipHeight = std::max(1u, Height >> i);
            SizePerMip[i] = mipWidth * mipHeight * 4; // 4 bytes per pixel (RGBA8)
        }
        
        FilePath = "Textures/" + name + ".dds";
    }

    String Name;
    String FilePath;
    uint32 Width;
    uint32 Height;
    uint32 TotalMipLevels;
    uint32 ResidentMips;
    SIZE_T SizePerMip[16] = {};
    void* MipData[16] = {};

    SIZE_T GetTotalSize() const {
        SIZE_T total = 0;
        for (uint32 i = 0; i < ResidentMips; ++i) {
            total += SizePerMip[i];
        }
        return total;
    }
};

// ============================================================================
// FTexturePool Tests
// ============================================================================

void TestTexturePoolBasicAllocation() {
    ScopedTestTimer timer("FTexturePool::Basic Allocation");

    try {
        const SIZE_T poolSize = 64 * 1024 * 1024; // 64MB
        FTexturePool pool(poolSize);

        // Test basic allocation
        void* ptr1 = pool.Allocate(1 * 1024 * 1024); // 1MB
        if (!ptr1) {
            timer.Failure("Failed to allocate 1MB");
            return;
        }

        void* ptr2 = pool.Allocate(4 * 1024 * 1024); // 4MB
        if (!ptr2) {
            pool.Free(ptr1);
            timer.Failure("Failed to allocate 4MB");
            return;
        }

        void* ptr3 = pool.Allocate(16 * 1024 * 1024); // 16MB
        if (!ptr3) {
            pool.Free(ptr1);
            pool.Free(ptr2);
            timer.Failure("Failed to allocate 16MB");
            return;
        }

        SIZE_T usedSize = pool.GetUsedSize();
        MR_LOG_DEBUG("  Used: " + std::to_string(usedSize / 1024 / 1024) + " MB");
        MR_LOG_DEBUG("  Free: " + std::to_string(pool.GetFreeSize() / 1024 / 1024) + " MB");

        // Free allocations
        pool.Free(ptr1);
        pool.Free(ptr2);
        pool.Free(ptr3);

        timer.Success();
    }
    catch (const std::exception& e) {
        timer.Failure(String("Exception: ") + e.what());
    }
}

void TestTexturePoolFragmentation() {
    ScopedTestTimer timer("FTexturePool::Fragmentation Handling");

    try {
        const SIZE_T poolSize = 32 * 1024 * 1024; // 32MB
        FTexturePool pool(poolSize);

        std::vector<void*> allocations;
        
        // Allocate multiple blocks
        for (int i = 0; i < 10; ++i) {
            void* ptr = pool.Allocate(2 * 1024 * 1024); // 2MB each
            if (ptr) {
                allocations.push_back(ptr);
            }
        }

        MR_LOG_DEBUG("  Allocated " + std::to_string(allocations.size()) + " blocks");

        // Free every other allocation (create fragmentation)
        for (size_t i = 1; i < allocations.size(); i += 2) {
            pool.Free(allocations[i]);
        }

        SIZE_T usedBefore = pool.GetUsedSize();
        MR_LOG_DEBUG("  Before compact: " + std::to_string(usedBefore / 1024 / 1024) + " MB used");

        // Try to allocate large block (should fail due to fragmentation)
        void* largeBlock = pool.Allocate(8 * 1024 * 1024);
        
        // Compact and try again
        pool.Compact();
        SIZE_T usedAfter = pool.GetUsedSize();
        MR_LOG_DEBUG("  After compact: " + std::to_string(usedAfter / 1024 / 1024) + " MB used");

        // Cleanup
        for (size_t i = 0; i < allocations.size(); i += 2) {
            pool.Free(allocations[i]);
        }

        timer.Success();
    }
    catch (const std::exception& e) {
        timer.Failure(String("Exception: ") + e.what());
    }
}

void TestTexturePoolAlignment() {
    ScopedTestTimer timer("FTexturePool::Memory Alignment");

    try {
        const SIZE_T poolSize = 16 * 1024 * 1024; // 16MB
        FTexturePool pool(poolSize);

        // Test 256-byte alignment (GPU texture requirement)
        void* ptr1 = pool.Allocate(1024, 256);
        if (!ptr1 || (reinterpret_cast<uintptr_t>(ptr1) % 256 != 0)) {
            timer.Failure("256-byte alignment failed");
            return;
        }

        void* ptr2 = pool.Allocate(2048, 256);
        if (!ptr2 || (reinterpret_cast<uintptr_t>(ptr2) % 256 != 0)) {
            pool.Free(ptr1);
            timer.Failure("Second 256-byte alignment failed");
            return;
        }

        MR_LOG_DEBUG("  ptr1 aligned: " + std::to_string(reinterpret_cast<uintptr_t>(ptr1) % 256 == 0));
        MR_LOG_DEBUG("  ptr2 aligned: " + std::to_string(reinterpret_cast<uintptr_t>(ptr2) % 256 == 0));

        pool.Free(ptr1);
        pool.Free(ptr2);

        timer.Success();
    }
    catch (const std::exception& e) {
        timer.Failure(String("Exception: ") + e.what());
    }
}

// ============================================================================
// FAsyncFileIO Tests
// ============================================================================

void TestAsyncFileIOInitialization() {
    ScopedTestTimer timer("FAsyncFileIO::Initialization");

    try {
        FAsyncFileIO& fileIO = FAsyncFileIO::Get();
        
        if (!fileIO.Initialize(2)) {
            timer.Failure("Failed to initialize FAsyncFileIO");
            return;
        }

        FAsyncFileIO::FIOStats stats;
        fileIO.GetStats(stats);

        MR_LOG_DEBUG("  Worker threads initialized");
        MR_LOG_DEBUG("  Total requests: " + std::to_string(stats.TotalRequests));

        timer.Success();
    }
    catch (const std::exception& e) {
        timer.Failure(String("Exception: ") + e.what());
    }
}

void TestAsyncFileIOBasicRead() {
    ScopedTestTimer timer("FAsyncFileIO::Basic Read");

    try {
        FAsyncFileIO& fileIO = FAsyncFileIO::Get();
        
        // Create test file
        String testFilePath = "test_texture_data.bin";
        {
            std::ofstream file(testFilePath, std::ios::binary);
            std::vector<uint8> testData(4096, 0xAB);
            file.write(reinterpret_cast<char*>(testData.data()), testData.size());
        }

        // Prepare read buffer
        std::vector<uint8> readBuffer(4096, 0);
        bool readCompleted = false;
        SIZE_T bytesReadTotal = 0;

        // Submit async read
        FAsyncFileIO::FReadRequest request;
        request.FilePath = testFilePath;
        request.Offset = 0;
        request.Size = 4096;
        request.DestBuffer = readBuffer.data();
        request.OnComplete = [&](bool success, SIZE_T bytesRead) {
            readCompleted = true;
            bytesReadTotal = bytesRead;
            MR_LOG_DEBUG("  Async read completed: " + std::to_string(bytesRead) + " bytes");
        };

        uint64 requestID = fileIO.ReadAsync(request);
        
        // Wait for completion
        fileIO.WaitForRequest(requestID);

        if (!readCompleted) {
            timer.Failure("Read did not complete");
            return;
        }

        if (bytesReadTotal != 4096) {
            timer.Failure("Read size mismatch");
            return;
        }

        // Verify data
        bool dataValid = true;
        for (size_t i = 0; i < readBuffer.size(); ++i) {
            if (readBuffer[i] != 0xAB) {
                dataValid = false;
                break;
            }
        }

        if (!dataValid) {
            timer.Failure("Read data verification failed");
            return;
        }

        // Cleanup test file
        std::remove(testFilePath.c_str());

        timer.Success();
    }
    catch (const std::exception& e) {
        timer.Failure(String("Exception: ") + e.what());
    }
}

void TestAsyncFileIOConcurrentReads() {
    ScopedTestTimer timer("FAsyncFileIO::Concurrent Reads");

    try {
        FAsyncFileIO& fileIO = FAsyncFileIO::Get();
        
        const int numFiles = 4;
        std::vector<String> testFiles;
        std::vector<std::vector<uint8>> readBuffers(numFiles);
        std::atomic<int> completedReads{0};

        // Create test files
        for (int i = 0; i < numFiles; ++i) {
            String filename = "test_concurrent_" + std::to_string(i) + ".bin";
            testFiles.push_back(filename);
            
            std::ofstream file(filename, std::ios::binary);
            std::vector<uint8> testData(1024, static_cast<uint8>(i + 1));
            file.write(reinterpret_cast<char*>(testData.data()), testData.size());
        }

        // Submit multiple concurrent reads
        std::vector<uint64> requestIDs;
        for (int i = 0; i < numFiles; ++i) {
            readBuffers[i].resize(1024, 0);
            
            FAsyncFileIO::FReadRequest request;
            request.FilePath = testFiles[i];
            request.Offset = 0;
            request.Size = 1024;
            request.DestBuffer = readBuffers[i].data();
            request.OnComplete = [&completedReads, i](bool success, SIZE_T bytesRead) {
                completedReads++;
                MR_LOG_DEBUG("  File " + std::to_string(i) + " completed");
            };

            uint64 requestID = fileIO.ReadAsync(request);
            requestIDs.push_back(requestID);
        }

        // Wait for all requests
        fileIO.WaitForAll();

        if (completedReads != numFiles) {
            timer.Failure("Not all reads completed");
            return;
        }

        // Cleanup
        for (const auto& filename : testFiles) {
            std::remove(filename.c_str());
        }

        timer.Success();
    }
    catch (const std::exception& e) {
        timer.Failure(String("Exception: ") + e.what());
    }
}

// ============================================================================
// FTextureStreamingManager Tests
// ============================================================================

void TestStreamingManagerInitialization() {
    ScopedTestTimer timer("FTextureStreamingManager::Initialization");

    try {
        FTextureStreamingManager& manager = FTextureStreamingManager::Get();
        
        const SIZE_T poolSize = 128 * 1024 * 1024; // 128MB
        if (!manager.Initialize(poolSize)) {
            timer.Failure("Failed to initialize streaming manager");
            return;
        }

        SIZE_T actualPoolSize = manager.GetPoolSize();
        MR_LOG_DEBUG("  Pool size: " + std::to_string(actualPoolSize / 1024 / 1024) + " MB");

        FTextureStreamingManager::FStreamingStats stats;
        manager.GetStreamingStats(stats);
        MR_LOG_DEBUG("  Initial stats:");
        MR_LOG_DEBUG("    Streaming textures: " + std::to_string(stats.NumStreamingTextures));
        MR_LOG_DEBUG("    Resident textures: " + std::to_string(stats.NumResidentTextures));

        timer.Success();
    }
    catch (const std::exception& e) {
        timer.Failure(String("Exception: ") + e.what());
    }
}

void TestTextureRegistration() {
    ScopedTestTimer timer("FTextureStreamingManager::Texture Registration");

    try {
        FTextureStreamingManager& manager = FTextureStreamingManager::Get();
        
        // Create mock textures
        FTexture texture1("Terrain_Diffuse", 2048, 2048, 11);
        FTexture texture2("Character_Skin", 1024, 1024, 10);
        FTexture texture3("UI_Background", 512, 512, 9);

        // Register textures
        manager.RegisterTexture(&texture1);
        manager.RegisterTexture(&texture2);
        manager.RegisterTexture(&texture3);

        FTextureStreamingManager::FStreamingStats stats;
        manager.GetStreamingStats(stats);

        MR_LOG_DEBUG("  Registered 3 textures");
        MR_LOG_DEBUG("  Streaming textures: " + std::to_string(stats.NumStreamingTextures));

        // Unregister
        manager.UnregisterTexture(&texture1);
        manager.UnregisterTexture(&texture2);
        manager.UnregisterTexture(&texture3);

        timer.Success();
    }
    catch (const std::exception& e) {
        timer.Failure(String("Exception: ") + e.what());
    }
}

void TestStreamingPrioritization() {
    ScopedTestTimer timer("FTextureStreamingManager::Priority-based Streaming");

    try {
        FTextureStreamingManager& manager = FTextureStreamingManager::Get();
        
        // Create textures with different priorities
        std::vector<FTexture*> textures;
        textures.push_back(new FTexture("HighPriority_Near", 2048, 2048, 11));
        textures.push_back(new FTexture("MediumPriority_Mid", 1024, 1024, 10));
        textures.push_back(new FTexture("LowPriority_Far", 512, 512, 9));

        // Register all textures
        for (auto* texture : textures) {
            manager.RegisterTexture(texture);
        }

        // Update streaming (simulate frame update)
        for (int frame = 0; frame < 5; ++frame) {
            manager.UpdateResourceStreaming(0.016f); // 16ms per frame
            
            FTextureStreamingManager::FStreamingStats stats;
            manager.GetStreamingStats(stats);
            
            MR_LOG_DEBUG("  Frame " + std::to_string(frame) + ":");
            MR_LOG_DEBUG("    Allocated: " + std::to_string(stats.AllocatedMemory / 1024) + " KB");
            MR_LOG_DEBUG("    Pending stream in: " + std::to_string(stats.PendingStreamIn / 1024) + " KB");
            
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Cleanup
        for (auto* texture : textures) {
            manager.UnregisterTexture(texture);
            delete texture;
        }

        timer.Success();
    }
    catch (const std::exception& e) {
        timer.Failure(String("Exception: ") + e.what());
    }
}

void TestMemoryBudgetManagement() {
    ScopedTestTimer timer("FTextureStreamingManager::Memory Budget Management");

    try {
        FTextureStreamingManager& manager = FTextureStreamingManager::Get();
        
        // Set a small budget to test eviction
        const SIZE_T smallBudget = 16 * 1024 * 1024; // 16MB
        manager.SetPoolSize(smallBudget);

        // Create textures that exceed budget
        std::vector<FTexture*> textures;
        for (int i = 0; i < 10; ++i) {
            textures.push_back(new FTexture(
                "Texture_" + std::to_string(i), 
                1024, 1024, 10
            ));
            manager.RegisterTexture(textures.back());
        }

        // Force streaming updates
        for (int i = 0; i < 10; ++i) {
            manager.UpdateResourceStreaming(0.016f);
        }

        FTextureStreamingManager::FStreamingStats stats;
        manager.GetStreamingStats(stats);

        MR_LOG_DEBUG("  Memory budget: " + std::to_string(smallBudget / 1024 / 1024) + " MB");
        MR_LOG_DEBUG("  Allocated: " + std::to_string(stats.AllocatedMemory / 1024 / 1024) + " MB");
        MR_LOG_DEBUG("  Budget respected: " + std::to_string(stats.AllocatedMemory <= smallBudget));

        // Cleanup
        for (auto* texture : textures) {
            manager.UnregisterTexture(texture);
            delete texture;
        }

        timer.Success();
    }
    catch (const std::exception& e) {
        timer.Failure(String("Exception: ") + e.what());
    }
}

// ============================================================================
// Real-world Scenario Tests (UE5-inspired)
// ============================================================================

void TestScenarioOpenWorldStreaming() {
    ScopedTestTimer timer("Scenario::Open World Terrain Streaming");

    try {
        FTextureStreamingManager& manager = FTextureStreamingManager::Get();
        
        MR_LOG_DEBUG("  Simulating open world with terrain tiles...");

        // Simulate 3x3 terrain grid
        std::vector<FTexture*> terrainTextures;
        for (int y = 0; y < 3; ++y) {
            for (int x = 0; x < 3; ++x) {
                String name = "Terrain_" + std::to_string(x) + "_" + std::to_string(y);
                terrainTextures.push_back(new FTexture(name, 2048, 2048, 11));
                manager.RegisterTexture(terrainTextures.back());
            }
        }

        // Simulate camera movement through terrain
        MR_LOG_DEBUG("  Simulating camera movement...");
        for (int frame = 0; frame < 10; ++frame) {
            manager.UpdateResourceStreaming(0.033f); // 30 FPS
            
            if (frame % 3 == 0) {
                FTextureStreamingManager::FStreamingStats stats;
                manager.GetStreamingStats(stats);
                MR_LOG_DEBUG("  Frame " + std::to_string(frame) + 
                           ": " + std::to_string(stats.NumStreamingTextures) + " streaming");
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        // Cleanup
        for (auto* texture : terrainTextures) {
            manager.UnregisterTexture(texture);
            delete texture;
        }

        timer.Success();
    }
    catch (const std::exception& e) {
        timer.Failure(String("Exception: ") + e.what());
    }
}

void TestScenarioCharacterLODStreaming() {
    ScopedTestTimer timer("Scenario::Character LOD Texture Streaming");

    try {
        FTextureStreamingManager& manager = FTextureStreamingManager::Get();
        
        MR_LOG_DEBUG("  Simulating multiple character LODs...");

        struct Character {
            FTexture* Diffuse;
            FTexture* Normal;
            FTexture* Specular;
        };

        std::vector<Character> characters;
        
        // Create 5 characters with 3 textures each
        for (int i = 0; i < 5; ++i) {
            Character ch;
            ch.Diffuse = new FTexture("Char" + std::to_string(i) + "_Diffuse", 2048, 2048, 11);
            ch.Normal = new FTexture("Char" + std::to_string(i) + "_Normal", 2048, 2048, 11);
            ch.Specular = new FTexture("Char" + std::to_string(i) + "_Specular", 1024, 1024, 10);
            
            manager.RegisterTexture(ch.Diffuse);
            manager.RegisterTexture(ch.Normal);
            manager.RegisterTexture(ch.Specular);
            
            characters.push_back(ch);
        }

        MR_LOG_DEBUG("  Registered " + std::to_string(characters.size() * 3) + " character textures");

        // Simulate distance-based LOD changes
        for (int frame = 0; frame < 15; ++frame) {
            manager.UpdateResourceStreaming(0.016f);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        FTextureStreamingManager::FStreamingStats stats;
        manager.GetStreamingStats(stats);
        MR_LOG_DEBUG("  Final allocated memory: " + 
                    std::to_string(stats.AllocatedMemory / 1024 / 1024) + " MB");

        // Cleanup
        for (auto& ch : characters) {
            manager.UnregisterTexture(ch.Diffuse);
            manager.UnregisterTexture(ch.Normal);
            manager.UnregisterTexture(ch.Specular);
            delete ch.Diffuse;
            delete ch.Normal;
            delete ch.Specular;
        }

        timer.Success();
    }
    catch (const std::exception& e) {
        timer.Failure(String("Exception: ") + e.what());
    }
}

void TestScenarioLevelTransition() {
    ScopedTestTimer timer("Scenario::Level Transition Streaming");

    try {
        FTextureStreamingManager& manager = FTextureStreamingManager::Get();
        
        MR_LOG_DEBUG("  Simulating level transition...");

        // Old level textures
        std::vector<FTexture*> oldLevelTextures;
        for (int i = 0; i < 5; ++i) {
            oldLevelTextures.push_back(new FTexture("OldLevel_" + std::to_string(i), 1024, 1024, 10));
            manager.RegisterTexture(oldLevelTextures.back());
        }

        // Load old level
        for (int i = 0; i < 5; ++i) {
            manager.UpdateResourceStreaming(0.016f);
        }

        FTextureStreamingManager::FStreamingStats stats1;
        manager.GetStreamingStats(stats1);
        MR_LOG_DEBUG("  Old level loaded: " + std::to_string(stats1.AllocatedMemory / 1024) + " KB");

        // Unload old level
        for (auto* texture : oldLevelTextures) {
            manager.UnregisterTexture(texture);
            delete texture;
        }
        oldLevelTextures.clear();

        // New level textures
        std::vector<FTexture*> newLevelTextures;
        for (int i = 0; i < 7; ++i) {
            newLevelTextures.push_back(new FTexture("NewLevel_" + std::to_string(i), 2048, 2048, 11));
            manager.RegisterTexture(newLevelTextures.back());
        }

        // Load new level
        for (int i = 0; i < 10; ++i) {
            manager.UpdateResourceStreaming(0.016f);
        }

        FTextureStreamingManager::FStreamingStats stats2;
        manager.GetStreamingStats(stats2);
        MR_LOG_DEBUG("  New level loaded: " + std::to_string(stats2.AllocatedMemory / 1024) + " KB");

        // Cleanup
        for (auto* texture : newLevelTextures) {
            manager.UnregisterTexture(texture);
            delete texture;
        }

        timer.Success();
    }
    catch (const std::exception& e) {
        timer.Failure(String("Exception: ") + e.what());
    }
}

void TestScenarioCutscenePreloading() {
    ScopedTestTimer timer("Scenario::Cutscene Texture Preloading");

    try {
        FTextureStreamingManager& manager = FTextureStreamingManager::Get();
        
        MR_LOG_DEBUG("  Simulating cutscene preload...");

        // High-quality cutscene textures
        std::vector<FTexture*> cutsceneTextures;
        cutsceneTextures.push_back(new FTexture("Cutscene_Character_4K", 4096, 4096, 12));
        cutsceneTextures.push_back(new FTexture("Cutscene_Environment_4K", 4096, 4096, 12));
        cutsceneTextures.push_back(new FTexture("Cutscene_Props_2K", 2048, 2048, 11));

        // Register all at once (preload scenario)
        auto startTime = std::chrono::high_resolution_clock::now();
        
        for (auto* texture : cutsceneTextures) {
            manager.RegisterTexture(texture);
        }

        // Force immediate loading
        for (int i = 0; i < 20; ++i) {
            manager.UpdateResourceStreaming(0.016f);
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

        FTextureStreamingManager::FStreamingStats stats;
        manager.GetStreamingStats(stats);
        
        MR_LOG_DEBUG("  Preload time: " + std::to_string(duration.count()) + " ms");
        MR_LOG_DEBUG("  Loaded memory: " + std::to_string(stats.AllocatedMemory / 1024 / 1024) + " MB");

        // Cleanup
        for (auto* texture : cutsceneTextures) {
            manager.UnregisterTexture(texture);
            delete texture;
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

void runTextureStreamingTests() {
    TestRunner::Get().Reset();

    MR_LOG_INFO("Starting Texture Streaming System Tests...\n");

    // Initialize systems
    FAsyncFileIO::Get().Initialize(2);
    FTextureStreamingManager::Get().Initialize(256 * 1024 * 1024); // 256MB

    // FTexturePool Tests
    MR_LOG_INFO("--- FTexturePool Tests ---");
    TestTexturePoolBasicAllocation();
    TestTexturePoolFragmentation();
    TestTexturePoolAlignment();

    // FAsyncFileIO Tests
    MR_LOG_INFO("\n--- FAsyncFileIO Tests ---");
    TestAsyncFileIOInitialization();
    TestAsyncFileIOBasicRead();
    TestAsyncFileIOConcurrentReads();

    // FTextureStreamingManager Tests
    MR_LOG_INFO("\n--- FTextureStreamingManager Tests ---");
    TestStreamingManagerInitialization();
    TestTextureRegistration();
    TestStreamingPrioritization();
    TestMemoryBudgetManagement();

    // Real-world Scenario Tests
    MR_LOG_INFO("\n--- Real-world Scenario Tests ---");
    TestScenarioOpenWorldStreaming();
    TestScenarioCharacterLODStreaming();
    TestScenarioLevelTransition();
    TestScenarioCutscenePreloading();

    // Cleanup
    FAsyncFileIO::Get().Shutdown();
    FTextureStreamingManager::Get().Shutdown();

    // Print summary
    TestRunner::Get().PrintSummary();
}

} // namespace TextureStreamingSystemTest
} // namespace MonsterRender

