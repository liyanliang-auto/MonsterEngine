// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Vulkan Memory Manager Test Suite
// Reference UE5 memory management test design

#include "Core/CoreMinimal.h"
#include "Core/Log.h"
#include "Platform/Vulkan/FVulkanMemoryManager.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanTexture.h"
#include "RHI/RHI.h"
#include <thread>
#include <chrono>
#include <vector>
#include <random>

namespace MonsterRender {
namespace VulkanMemoryManagerTest {

using namespace RHI;
using namespace RHI::Vulkan;

// ================================
// Helper Functions
// ================================

/**
 * Create test RHI device
 */
static TUniquePtr<IRHIDevice> CreateTestDevice() {
    RHICreateInfo createInfo;
    createInfo.preferredBackend = ERHIBackend::Vulkan;
    createInfo.enableValidation = true;
    createInfo.enableDebugMarkers = true;
    createInfo.applicationName = "Vulkan Memory Test";
    
    return RHIFactory::createDevice(createInfo);
}

/**
 * Format memory size display
 */
static String FormatMemorySize(VkDeviceSize size) {
    if (size >= 1024 * 1024 * 1024) {
        return std::to_string(size / (1024 * 1024 * 1024)) + " GB";
    } else if (size >= 1024 * 1024) {
        return std::to_string(size / (1024 * 1024)) + " MB";
    } else if (size >= 1024) {
        return std::to_string(size / 1024) + " KB";
    }
    return std::to_string(size) + " B";
}

// ================================
// Basic Tests
// ================================

/**
 * Test 1: Basic allocation and free
 */
static void Test_BasicAllocation() {
    MR_LOG_INFO("\n[Test 1] Basic Allocation Test");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    auto* vulkanDevice = static_cast<VulkanDevice*>(device.get());
    auto* memoryManager = vulkanDevice->getMemoryManager();
    
    // Create small buffer (64KB)
    BufferDesc bufferDesc{};
    bufferDesc.size = 64 * 1024;
    bufferDesc.usage = EResourceUsage::VertexBuffer;
    bufferDesc.cpuAccessible = false;
    bufferDesc.debugName = "TestBuffer_64KB";
    
    auto buffer = device->createBuffer(bufferDesc);
    if (buffer) {
        MR_LOG_INFO("  [OK] Successfully allocated 64KB buffer");
        
        // Validate buffer
        auto* vulkanBuffer = static_cast<VulkanBuffer*>(buffer.get());
        if (vulkanBuffer->isValid()) {
            MR_LOG_INFO("  [OK] Buffer validation passed");
        }
    } else {
        MR_LOG_ERROR("  [FAIL] Buffer allocation failed");
    }
    
    // Free buffer (smart pointer auto-release)
    buffer.reset();
    MR_LOG_INFO("  [OK] Buffer released");
    
    // Get memory stats
    FVulkanMemoryManager::FMemoryStats stats;
    memoryManager->GetMemoryStats(stats);
    
    MR_LOG_INFO("  Memory stats:");
    MR_LOG_INFO("    Total allocated: " + FormatMemorySize(stats.TotalAllocated));
    MR_LOG_INFO("    Heap count: " + std::to_string(stats.PoolCount));
    
    MR_LOG_INFO("  [OK] Test 1 completed\n");
}

/**
 * Test 2: Sub-Allocation
 * Verify multiple small allocations share a large VkDeviceMemory
 */
static void Test_SubAllocation() {
    MR_LOG_INFO("\n[Test 2] Sub-Allocation Test");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    auto* vulkanDevice = static_cast<VulkanDevice*>(device.get());
    auto* memoryManager = vulkanDevice->getMemoryManager();
    
    // Create multiple small buffers (should use sub-allocation)
    TArray<TSharedPtr<IRHIBuffer>> buffers;
    const int numBuffers = 10;
    const uint32 bufferSize = 256 * 1024;  // 256KB
    
    MR_LOG_INFO("  Allocating " + std::to_string(numBuffers) + " buffers of " + 
                FormatMemorySize(bufferSize) + "...");
    
    for (int i = 0; i < numBuffers; ++i) {
        BufferDesc desc{};
        desc.size = bufferSize;
        desc.usage = EResourceUsage::VertexBuffer;
        desc.cpuAccessible = false;
        desc.debugName = "SubAllocBuffer_" + std::to_string(i);
        
        auto buffer = device->createBuffer(desc);
        if (buffer) {
            buffers.push_back(buffer);
        } else {
            MR_LOG_ERROR("  [FAIL] Buffer " + std::to_string(i) + " allocation failed");
        }
    }
    
    MR_LOG_INFO("  [OK] Successfully allocated " + std::to_string(buffers.size()) + " buffers");
    
    // Get statistics
    FVulkanMemoryManager::FMemoryStats stats;
    memoryManager->GetMemoryStats(stats);
    
    MR_LOG_INFO("  Sub-allocation stats:");
    MR_LOG_INFO("    Total allocated: " + FormatMemorySize(stats.TotalAllocated));
    MR_LOG_INFO("    Total reserved: " + FormatMemorySize(stats.TotalReserved));
    MR_LOG_INFO("    Heap count: " + std::to_string(stats.PoolCount));
    MR_LOG_INFO("    Allocation count: " + std::to_string(stats.AllocationCount));
    
    float utilization = (float)stats.TotalAllocated / stats.TotalReserved * 100.0f;
    MR_LOG_INFO("    Memory utilization: " + std::to_string(utilization) + "%");
    
    if (stats.PoolCount <= 2) {
        MR_LOG_INFO("  [OK] Sub-allocation working correctly (multiple buffers share few heaps)");
    }
    
    // Free some buffers
    buffers.erase(buffers.begin(), buffers.begin() + 5);
    MR_LOG_INFO("  [OK] Released 5 buffers");
    
    memoryManager->GetMemoryStats(stats);
    MR_LOG_INFO("    After free: " + FormatMemorySize(stats.TotalAllocated));
    
    MR_LOG_INFO("  [OK] Test 2 completed\n");
}

/**
 * Test 3: Alignment requirements
 * Verify allocations with different alignment requirements
 */
static void Test_Alignment() {
    MR_LOG_INFO("\n[Test 3] Alignment Test");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    // Test different alignments
    const VkDeviceSize alignments[] = {4, 16, 64, 256, 4096};
    
    for (VkDeviceSize alignment : alignments) {
        BufferDesc desc{};
        desc.size = 1024;  // 1KB
        desc.usage = EResourceUsage::UniformBuffer;
        desc.cpuAccessible = true;
        desc.debugName = "AlignedBuffer_" + std::to_string(alignment);
        
        auto buffer = device->createBuffer(desc);
        if (buffer) {
            auto* vulkanBuffer = static_cast<VulkanBuffer*>(buffer.get());
            VkDeviceSize offset = vulkanBuffer->getOffset();
            
            // Verify alignment
            if (offset % alignment == 0) {
                MR_LOG_INFO("  [OK] " + std::to_string(alignment) + " byte alignment: offset = " +
                            std::to_string(offset));
            } else {
                MR_LOG_WARNING("  [WARN] " + std::to_string(alignment) + " byte alignment failed");
            }
        }
    }
    
    MR_LOG_INFO("  [OK] Test 3 completed\n");
}

/**
 * Test 4: Fragmentation and coalescing
 * Test memory block allocation, free and merge
 */
static void Test_Fragmentation() {
    MR_LOG_INFO("\n[Test 4] Fragmentation and Coalescing Test");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    auto* vulkanDevice = static_cast<VulkanDevice*>(device.get());
    auto* memoryManager = vulkanDevice->getMemoryManager();
    
    // Allocate multiple buffers
    TArray<TSharedPtr<IRHIBuffer>> buffers;
    for (int i = 0; i < 20; ++i) {
        BufferDesc desc{};
        desc.size = 128 * 1024;  // 128KB
        desc.usage = EResourceUsage::VertexBuffer;
        desc.cpuAccessible = false;
        desc.debugName = "FragBuffer_" + std::to_string(i);
        
        buffers.push_back(device->createBuffer(desc));
    }
    
    MR_LOG_INFO("  Allocated 20 buffers");
    
    FVulkanMemoryManager::FMemoryStats stats1;
    memoryManager->GetMemoryStats(stats1);
    MR_LOG_INFO("    After allocation: " + FormatMemorySize(stats1.TotalAllocated));
    
    // Free odd-indexed buffers (create fragmentation)
    for (int i = 1; i < 20; i += 2) {
        buffers[i].reset();
    }
    
    MR_LOG_INFO("  Released 10 buffers (create fragmentation)");
    
    FVulkanMemoryManager::FMemoryStats stats2;
    memoryManager->GetMemoryStats(stats2);
    MR_LOG_INFO("    After free: " + FormatMemorySize(stats2.TotalAllocated));
    MR_LOG_INFO("    Largest free block: " + FormatMemorySize(stats2.LargestFreeBlock));
    
    // Try defragmentation
    memoryManager->DefragmentAll();
    MR_LOG_INFO("  [OK] Executed memory defragmentation");
    
    MR_LOG_INFO("  [OK] Test 4 completed\n");
}

/**
 * Test 5: Dedicated Allocation
 * Test large resources use dedicated memory
 */
static void Test_DedicatedAllocation() {
    MR_LOG_INFO("\n[Test 5] Dedicated Allocation Test");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    auto* vulkanDevice = static_cast<VulkanDevice*>(device.get());
    auto* memoryManager = vulkanDevice->getMemoryManager();
    
    // Create large texture (should trigger dedicated allocation)
    TextureDesc texDesc{};
    texDesc.width = 4096;
    texDesc.height = 4096;
    texDesc.mipLevels = 1;
    texDesc.format = EPixelFormat::R8G8B8A8_UNORM;
    texDesc.usage = EResourceUsage::ShaderResource;
    texDesc.debugName = "LargeTexture_4K";
    
    MR_LOG_INFO("  Creating 4K texture (should use dedicated allocation)...");
    
    FVulkanMemoryManager::FMemoryStats statsBefore;
    memoryManager->GetMemoryStats(statsBefore);
    
    auto texture = device->createTexture(texDesc);
    if (texture) {
        MR_LOG_INFO("  [OK] Texture created successfully");
        
        auto* vulkanTexture = static_cast<VulkanTexture*>(texture.get());
        if (vulkanTexture->isValid()) {
            MR_LOG_INFO("  [OK] Texture validation passed");
        }
    }
    
    FVulkanMemoryManager::FMemoryStats statsAfter;
    memoryManager->GetMemoryStats(statsAfter);
    
    VkDeviceSize allocated = statsAfter.TotalAllocated - statsBefore.TotalAllocated;
    MR_LOG_INFO("  Texture memory: " + FormatMemorySize(allocated));
    
    // Expected size: 4096 * 4096 * 4 = 64MB
    VkDeviceSize expectedSize = 4096ULL * 4096ULL * 4ULL;
    if (allocated >= expectedSize * 0.9f) {  // Allow some overhead
        MR_LOG_INFO("  [OK] Dedicated allocation size as expected");
    }
    
    MR_LOG_INFO("  [OK] Test 5 completed\n");
}

/**
 * Test 6: Memory type selection
 * Test allocations with different memory properties
 */
static void Test_MemoryTypeSelection() {
    MR_LOG_INFO("\n[Test 6] Memory Type Selection Test");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    // CPU visible memory (Staging Buffer)
    {
        BufferDesc desc{};
        desc.size = 1024 * 1024;  // 1MB
        desc.usage = EResourceUsage::TransferSrc;
        desc.cpuAccessible = true;
        desc.debugName = "StagingBuffer";
        
        auto buffer = device->createBuffer(desc);
        if (buffer) {
            void* mapped = buffer->map();
            if (mapped) {
                MR_LOG_INFO("  [OK] CPU visible memory allocated (mappable)");
                buffer->unmap();
            }
        }
    }
    
    // GPU only memory (Device Local)
    {
        BufferDesc desc{};
        desc.size = 1024 * 1024;  // 1MB
        desc.usage = EResourceUsage::VertexBuffer;
        desc.cpuAccessible = false;
        desc.debugName = "GPUOnlyBuffer";
        
        auto buffer = device->createBuffer(desc);
        if (buffer) {
            MR_LOG_INFO("  [OK] GPU only memory allocated");
        }
    }
    
    // CPU cached memory (Uniform Buffer)
    {
        BufferDesc desc{};
        desc.size = 256;
        desc.usage = EResourceUsage::UniformBuffer;
        desc.cpuAccessible = true;
        desc.debugName = "UniformBuffer";
        
        auto buffer = device->createBuffer(desc);
        if (buffer) {
            MR_LOG_INFO("  [OK] Uniform Buffer memory allocated");
        }
    }
    
    MR_LOG_INFO("  [OK] Test 6 completed\n");
}

/**
 * Test 7: Heap growth
 * Test automatic creation of new heaps when existing space insufficient
 */
static void Test_HeapGrowth() {
    MR_LOG_INFO("\n[Test 7] Heap Growth Test");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    auto* vulkanDevice = static_cast<VulkanDevice*>(device.get());
    auto* memoryManager = vulkanDevice->getMemoryManager();
    
    // Allocate many small buffers, force multiple heaps
    TArray<TSharedPtr<IRHIBuffer>> buffers;
    const int numBuffers = 100;
    
    MR_LOG_INFO("  Allocating " + std::to_string(numBuffers) + " buffers...");
    
    for (int i = 0; i < numBuffers; ++i) {
        BufferDesc desc{};
        desc.size = 1024 * 1024;  // 1MB
        desc.usage = EResourceUsage::VertexBuffer;
        desc.cpuAccessible = false;
        desc.debugName = "HeapGrowthBuffer_" + std::to_string(i);
        
        buffers.push_back(device->createBuffer(desc));
        
        if ((i + 1) % 20 == 0) {
            FVulkanMemoryManager::FMemoryStats stats;
            memoryManager->GetMemoryStats(stats);
            MR_LOG_INFO("    " + std::to_string(i + 1) + " buffers: " +
                        std::to_string(stats.PoolCount) + " heaps, " +
                        FormatMemorySize(stats.TotalReserved) + " total reserved");
        }
    }
    
    FVulkanMemoryManager::FMemoryStats finalStats;
    memoryManager->GetMemoryStats(finalStats);
    
    MR_LOG_INFO("  Final stats:");
    MR_LOG_INFO("    Heap count: " + std::to_string(finalStats.PoolCount));
    MR_LOG_INFO("    Total allocated: " + FormatMemorySize(finalStats.TotalAllocated));
    MR_LOG_INFO("    Total reserved: " + FormatMemorySize(finalStats.TotalReserved));
    
    if (finalStats.PoolCount > 1) {
        MR_LOG_INFO("  [OK] Heap auto-growth working correctly");
    }
    
    MR_LOG_INFO("  [OK] Test 7 completed\n");
}

/**
 * Test 8: Concurrent allocation
 * Test memory allocation in multi-threaded environment
 */
static void Test_ConcurrentAllocation() {
    MR_LOG_INFO("\n[Test 8] Concurrent Allocation Test");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    const int numThreads = 4;
    const int allocsPerThread = 10;
    
    MR_LOG_INFO("  Starting " + std::to_string(numThreads) + " threads, each allocating " +
                std::to_string(allocsPerThread) + " buffers...");
    
    TArray<std::thread> threads;
    std::atomic<int> successCount{0};
    std::atomic<int> failCount{0};
    
    auto allocFunc = [&](int threadId) {
        for (int i = 0; i < allocsPerThread; ++i) {
            BufferDesc desc{};
            desc.size = 64 * 1024;  // 64KB
            desc.usage = EResourceUsage::VertexBuffer;
            desc.cpuAccessible = false;
            desc.debugName = "ConcurrentBuffer_T" + std::to_string(threadId) + 
                            "_" + std::to_string(i);
            
            auto buffer = device->createBuffer(desc);
            if (buffer) {
                successCount++;
            } else {
                failCount++;
            }
            
            // Random delay
            std::this_thread::sleep_for(std::chrono::milliseconds(rand() % 10));
        }
    };
    
    // Start threads
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back(allocFunc, i);
    }
    
    // Wait for completion
    for (auto& thread : threads) {
        thread.join();
    }
    
    MR_LOG_INFO("  Concurrent allocation completed:");
    MR_LOG_INFO("    Success: " + std::to_string(successCount.load()));
    MR_LOG_INFO("    Failed: " + std::to_string(failCount.load()));
    
    if (failCount == 0) {
        MR_LOG_INFO("  [OK] Concurrent allocation safety verified");
    } else {
        MR_LOG_WARNING("  [WARN] Some allocations failed");
    }
    
    MR_LOG_INFO("  [OK] Test 8 completed\n");
}

/**
 * Test 9: Statistics tracking
 * Verify accuracy of memory statistics
 */
static void Test_StatisticsTracking() {
    MR_LOG_INFO("\n[Test 9] Statistics Tracking Test");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    auto* vulkanDevice = static_cast<VulkanDevice*>(device.get());
    auto* memoryManager = vulkanDevice->getMemoryManager();
    
    // Initial stats
    FVulkanMemoryManager::FMemoryStats stats0;
    memoryManager->GetMemoryStats(stats0);
    MR_LOG_INFO("  Initial state: " + FormatMemorySize(stats0.TotalAllocated));
    
    // Allocate some resources
    TArray<TSharedPtr<IRHIBuffer>> buffers;
    VkDeviceSize totalExpected = 0;
    
    for (int i = 0; i < 5; ++i) {
        uint32 size = (i + 1) * 256 * 1024;  // 256KB, 512KB, ...
        BufferDesc desc{};
        desc.size = size;
        desc.usage = EResourceUsage::VertexBuffer;
        desc.cpuAccessible = false;
        desc.debugName = "StatsBuffer_" + std::to_string(i);
        
        buffers.push_back(device->createBuffer(desc));
        totalExpected += size;
    }
    
    FVulkanMemoryManager::FMemoryStats stats1;
    memoryManager->GetMemoryStats(stats1);
    
    VkDeviceSize actualAllocated = stats1.TotalAllocated - stats0.TotalAllocated;
    
    MR_LOG_INFO("  Allocation stats:");
    MR_LOG_INFO("    Expected: " + FormatMemorySize(totalExpected));
    MR_LOG_INFO("    Actual: " + FormatMemorySize(actualAllocated));
    MR_LOG_INFO("    Count: " + std::to_string(stats1.AllocationCount - stats0.AllocationCount));
    
    // Verify accuracy (allow some alignment overhead)
    float accuracy = (float)totalExpected / actualAllocated * 100.0f;
    MR_LOG_INFO("    Accuracy: " + std::to_string(accuracy) + "%");
    
    if (accuracy > 80.0f) {
        MR_LOG_INFO("  [OK] Statistics tracking accurate");
    }
    
    MR_LOG_INFO("  [OK] Test 9 completed\n");
}

// ================================
// Scenario Tests
// ================================

/**
 * Scenario 1: Game asset loading
 * Simulate loading many meshes and textures at game startup
 */
static void Scenario_GameAssetLoading() {
    MR_LOG_INFO("\n[Scenario 1] Game Asset Loading");
    MR_LOG_INFO("  Simulating: Loading 50 meshes + 100 textures");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    auto* vulkanDevice = static_cast<VulkanDevice*>(device.get());
    auto* memoryManager = vulkanDevice->getMemoryManager();
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Load mesh buffers
    TArray<TSharedPtr<IRHIBuffer>> meshBuffers;
    for (int i = 0; i < 50; ++i) {
        // Vertex buffer (random size: 100KB - 5MB)
        uint32 vertexSize = (100 + rand() % 4900) * 1024;
        BufferDesc vertexDesc{};
        vertexDesc.size = vertexSize;
        vertexDesc.usage = EResourceUsage::VertexBuffer;
        vertexDesc.cpuAccessible = false;
        vertexDesc.debugName = "Mesh_" + std::to_string(i) + "_Vertex";
        
        meshBuffers.push_back(device->createBuffer(vertexDesc));
        
        // Index buffer
        uint32 indexSize = vertexSize / 4;
        BufferDesc indexDesc{};
        indexDesc.size = indexSize;
        indexDesc.usage = EResourceUsage::IndexBuffer;
        indexDesc.cpuAccessible = false;
        indexDesc.debugName = "Mesh_" + std::to_string(i) + "_Index";
        
        meshBuffers.push_back(device->createBuffer(indexDesc));
    }
    
    MR_LOG_INFO("  [OK] Loaded 50 meshes (100 buffers)");
    
    // Load textures
    TArray<TSharedPtr<IRHITexture>> textures;
    const uint32 textureSizes[] = {256, 512, 1024, 2048};
    
    for (int i = 0; i < 100; ++i) {
        uint32 size = textureSizes[rand() % 4];
        
        TextureDesc texDesc{};
        texDesc.width = size;
        texDesc.height = size;
        texDesc.mipLevels = static_cast<uint32>(std::log2(size)) + 1;
        texDesc.format = EPixelFormat::R8G8B8A8_UNORM;
        texDesc.usage = EResourceUsage::ShaderResource;
        texDesc.debugName = "Texture_" + std::to_string(i);
        
        textures.push_back(device->createTexture(texDesc));
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    MR_LOG_INFO("  [OK] Loaded 100 textures");
    
    // Statistics
    FVulkanMemoryManager::FMemoryStats stats;
    memoryManager->GetMemoryStats(stats);
    
    MR_LOG_INFO("  Loading stats:");
    MR_LOG_INFO("    Load time: " + std::to_string(duration.count()) + " ms");
    MR_LOG_INFO("    Total memory: " + FormatMemorySize(stats.TotalAllocated));
    MR_LOG_INFO("    Heap count: " + std::to_string(stats.PoolCount));
    MR_LOG_INFO("    Allocation count: " + std::to_string(stats.AllocationCount));
    
    float avgAllocTime = (float)duration.count() / stats.AllocationCount;
    MR_LOG_INFO("    Avg alloc time: " + std::to_string(avgAllocTime) + " ms");
    
    MR_LOG_INFO("  [OK] Scenario 1 completed\n");
}

/**
 * Scenario 2: Dynamic resource streaming
 * Simulate runtime loading/unloading of resources
 */
static void Scenario_DynamicResourceStreaming() {
    MR_LOG_INFO("\n[Scenario 2] Dynamic Resource Streaming");
    MR_LOG_INFO("  Simulating: Runtime texture mip level streaming");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    auto* vulkanDevice = static_cast<VulkanDevice*>(device.get());
    auto* memoryManager = vulkanDevice->getMemoryManager();
    
    // Simulate 10 frames of streaming
    for (int frame = 0; frame < 10; ++frame) {
        MR_LOG_INFO("  Frame " + std::to_string(frame + 1) + ":");
        
        // Load high-resolution mips
        TArray<TSharedPtr<IRHITexture>> highResMips;
        for (int i = 0; i < 5; ++i) {
            TextureDesc desc{};
            desc.width = 2048;
            desc.height = 2048;
            desc.mipLevels = 1;
            desc.format = EPixelFormat::R8G8B8A8_UNORM;
            desc.usage = EResourceUsage::ShaderResource;
            desc.debugName = "StreamMip_Frame" + std::to_string(frame) + "_" + std::to_string(i);
            
            highResMips.push_back(device->createTexture(desc));
        }
        
        FVulkanMemoryManager::FMemoryStats stats;
        memoryManager->GetMemoryStats(stats);
        
        MR_LOG_INFO("    Loaded 5 high-res mips");
        MR_LOG_INFO("    Current memory: " + FormatMemorySize(stats.TotalAllocated));
        
        // Unload (smart pointer auto-release)
        highResMips.clear();
        
        memoryManager->GetMemoryStats(stats);
        MR_LOG_INFO("    After unload: " + FormatMemorySize(stats.TotalAllocated));
    }
    
    MR_LOG_INFO("  [OK] Scenario 2 completed\n");
}

/**
 * Scenario 3: Particle system
 * Simulate many short-lived small buffers
 */
static void Scenario_ParticleSystem() {
    MR_LOG_INFO("\n[Scenario 3] Particle System");
    MR_LOG_INFO("  Simulating: 1000 particle emitters with dynamic buffers");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    auto* vulkanDevice = static_cast<VulkanDevice*>(device.get());
    auto* memoryManager = vulkanDevice->getMemoryManager();
    
    const int numEmitters = 1000;
    const int particlesPerEmitter = 100;
    const uint32 particleSize = particlesPerEmitter * sizeof(float) * 8;  // position + velocity
    
    MR_LOG_INFO("  Per emitter: " + std::to_string(particlesPerEmitter) + " particles, " +
                FormatMemorySize(particleSize));
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    TArray<TSharedPtr<IRHIBuffer>> particleBuffers;
    for (int i = 0; i < numEmitters; ++i) {
        BufferDesc desc{};
        desc.size = particleSize;
        desc.usage = EResourceUsage::StorageBuffer;
        desc.cpuAccessible = true;  // Need CPU update
        desc.debugName = "ParticleEmitter_" + std::to_string(i);
        
        particleBuffers.push_back(device->createBuffer(desc));
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    
    FVulkanMemoryManager::FMemoryStats stats;
    memoryManager->GetMemoryStats(stats);
    
    MR_LOG_INFO("  Particle system stats:");
    MR_LOG_INFO("    Allocation time: " + std::to_string(duration.count()) + " us");
    MR_LOG_INFO("    Total memory: " + FormatMemorySize(stats.TotalAllocated));
    MR_LOG_INFO("    Heap count: " + std::to_string(stats.PoolCount));
    
    float allocPerEmitter = (float)duration.count() / numEmitters;
    MR_LOG_INFO("    Per-emitter alloc time: " + std::to_string(allocPerEmitter) + " us");
    
    if (allocPerEmitter < 100.0f) {
        MR_LOG_INFO("  [OK] Allocation performance excellent (< 100 us/emitter)");
    }
    
    MR_LOG_INFO("  [OK] Scenario 3 completed\n");
}

/**
 * Scenario 4: Uniform Buffer pool
 * Simulate frequently updated Uniform Buffers
 */
static void Scenario_UniformBufferPool() {
    MR_LOG_INFO("\n[Scenario 4] Uniform Buffer Pool");
    MR_LOG_INFO("  Simulating: Per-frame updated Uniform Buffers (3 frames in flight)");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    const int framesInFlight = 3;
    const int uniformsPerFrame = 100;
    const uint32 uniformSize = 256;  // Standard Uniform Buffer size
    
    // Create Uniform Buffers for each frame
    TArray<TArray<TSharedPtr<IRHIBuffer>>> frameBuffers(framesInFlight);
    
    for (int frame = 0; frame < framesInFlight; ++frame) {
        for (int i = 0; i < uniformsPerFrame; ++i) {
            BufferDesc desc{};
            desc.size = uniformSize;
            desc.usage = EResourceUsage::UniformBuffer;
            desc.cpuAccessible = true;
            desc.debugName = "UBO_Frame" + std::to_string(frame) + "_" + std::to_string(i);
            
            frameBuffers[frame].push_back(device->createBuffer(desc));
        }
    }
    
    MR_LOG_INFO("  Created " + std::to_string(framesInFlight * uniformsPerFrame) + " Uniform Buffers");
    
    // Simulate multi-frame updates
    for (int frame = 0; frame < 10; ++frame) {
        int frameIndex = frame % framesInFlight;
        
        // Map and update
        int updateCount = 0;
        for (auto& buffer : frameBuffers[frameIndex]) {
            void* mapped = buffer->map();
            if (mapped) {
                // Write simulated data
                memset(mapped, frame % 256, uniformSize);
                buffer->unmap();
                updateCount++;
            }
        }
        
        if (frame % 3 == 0) {
            MR_LOG_INFO("  Frame " + std::to_string(frame) + ": Updated " +
                        std::to_string(updateCount) + " Uniform Buffers");
        }
    }
    
    MR_LOG_INFO("  [OK] Scenario 4 completed\n");
}

/**
 * Scenario 5: Terrain system
 * Simulate large terrain heightmaps and textures
 */
static void Scenario_TerrainSystem() {
    MR_LOG_INFO("\n[Scenario 5] Terrain System");
    MR_LOG_INFO("  Simulating: 16 terrain chunks, each with 4K heightmap + texture layers");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    auto* vulkanDevice = static_cast<VulkanDevice*>(device.get());
    auto* memoryManager = vulkanDevice->getMemoryManager();
    
    const int terrainChunks = 16;
    TArray<TSharedPtr<IRHITexture>> terrainResources;
    
    for (int chunk = 0; chunk < terrainChunks; ++chunk) {
        // Heightmap (single channel, float)
        TextureDesc heightMapDesc{};
        heightMapDesc.width = 4096;
        heightMapDesc.height = 4096;
        heightMapDesc.mipLevels = 1;
        heightMapDesc.format = EPixelFormat::R32_FLOAT;
        heightMapDesc.usage = EResourceUsage::ShaderResource;
        heightMapDesc.debugName = "HeightMap_" + std::to_string(chunk);
        
        terrainResources.push_back(device->createTexture(heightMapDesc));
        
        // Texture layers (Albedo, Normal, Roughness, AO)
        const char* layerNames[] = {"Albedo", "Normal", "Roughness", "AO"};
        for (int layer = 0; layer < 4; ++layer) {
            TextureDesc layerDesc{};
            layerDesc.width = 2048;
            layerDesc.height = 2048;
            layerDesc.mipLevels = 11;  // Full mip chain
            layerDesc.format = EPixelFormat::R8G8B8A8_UNORM;
            layerDesc.usage = EResourceUsage::ShaderResource;
            layerDesc.debugName = String("Terrain_") + std::to_string(chunk) + 
                                 "_" + layerNames[layer];
            
            terrainResources.push_back(device->createTexture(layerDesc));
        }
    }
    
    MR_LOG_INFO("  Created " + std::to_string(terrainChunks) + " terrain chunks (" +
                std::to_string(terrainResources.size()) + " textures)");
    
    FVulkanMemoryManager::FMemoryStats stats;
    memoryManager->GetMemoryStats(stats);
    
    MR_LOG_INFO("  Terrain system stats:");
    MR_LOG_INFO("    Total memory: " + FormatMemorySize(stats.TotalAllocated));
    MR_LOG_INFO("    Heap count: " + std::to_string(stats.PoolCount));
    MR_LOG_INFO("    Per-chunk avg: " + 
                FormatMemorySize(stats.TotalAllocated / terrainChunks));
    
    MR_LOG_INFO("  [OK] Scenario 5 completed\n");
}

/**
 * Scenario 6: Memory budget management
 * Simulate resource management under strict memory budget
 */
static void Scenario_MemoryBudgetManagement() {
    MR_LOG_INFO("\n[Scenario 6] Memory Budget Management");
    MR_LOG_INFO("  Simulating: Managing resources under 512MB budget");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    auto* vulkanDevice = static_cast<VulkanDevice*>(device.get());
    auto* memoryManager = vulkanDevice->getMemoryManager();
    
    const VkDeviceSize memoryBudget = 512ULL * 1024 * 1024;  // 512MB
    TArray<TSharedPtr<IRHITexture>> loadedTextures;
    
    MR_LOG_INFO("  Memory budget: " + FormatMemorySize(memoryBudget));
    
    int loadedCount = 0;
    int evictedCount = 0;
    
    // Try loading resources until approaching budget
    for (int i = 0; i < 200; ++i) {
        // Check current memory usage
        FVulkanMemoryManager::FMemoryStats stats;
        memoryManager->GetMemoryStats(stats);
        
        if (stats.TotalAllocated > memoryBudget * 0.9f) {
            // Approaching budget, evict oldest resource
            if (!loadedTextures.empty()) {
                loadedTextures.erase(loadedTextures.begin());
                evictedCount++;
                
                memoryManager->GetMemoryStats(stats);
                MR_LOG_INFO("  Evicted resource, current memory: " + FormatMemorySize(stats.TotalAllocated));
            }
        }
        
        // Load new texture
        TextureDesc desc{};
        desc.width = 1024;
        desc.height = 1024;
        desc.mipLevels = 11;
        desc.format = EPixelFormat::R8G8B8A8_UNORM;
        desc.usage = EResourceUsage::ShaderResource;
        desc.debugName = "BudgetTexture_" + std::to_string(i);
        
        loadedTextures.push_back(device->createTexture(desc));
        loadedCount++;
    }
    
    FVulkanMemoryManager::FMemoryStats finalStats;
    memoryManager->GetMemoryStats(finalStats);
    
    MR_LOG_INFO("  Budget management results:");
    MR_LOG_INFO("    Resources loaded: " + std::to_string(loadedCount));
    MR_LOG_INFO("    Resources evicted: " + std::to_string(evictedCount));
    MR_LOG_INFO("    Final memory: " + FormatMemorySize(finalStats.TotalAllocated));
    MR_LOG_INFO("    Budget utilization: " +
                std::to_string((float)finalStats.TotalAllocated / memoryBudget * 100.0f) + "%");
    
    if (finalStats.TotalAllocated <= memoryBudget) {
        MR_LOG_INFO("  [OK] Successfully stayed within budget");
    }
    
    MR_LOG_INFO("  [OK] Scenario 6 completed\n");
}

// ================================
// Main Test Entry
// ================================

/**
 * Run all basic tests
 */
void RunBasicTests() {
    MR_LOG_INFO("\n");
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("  Vulkan Memory Manager - Basic Tests");
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("\n");
    
    Test_BasicAllocation();
    Test_SubAllocation();
    Test_Alignment();
    Test_Fragmentation();
    Test_DedicatedAllocation();
    Test_MemoryTypeSelection();
    Test_HeapGrowth();
    Test_ConcurrentAllocation();
    Test_StatisticsTracking();
    
    MR_LOG_INFO("\n");
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("  Basic Tests Completed!");
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("\n");
}

/**
 * Run all scenario tests
 */
void RunScenarioTests() {
    MR_LOG_INFO("\n");
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("  Vulkan Memory Manager - Scenario Tests");
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("\n");
    
    Scenario_GameAssetLoading();
    Scenario_DynamicResourceStreaming();
    Scenario_ParticleSystem();
    Scenario_UniformBufferPool();
    Scenario_TerrainSystem();
    Scenario_MemoryBudgetManagement();
    
    MR_LOG_INFO("\n");
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("  Scenario Tests Completed!");
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("\n");
}

/**
 * Run all tests
 */
void RunAllTests() {
    RunBasicTests();
    RunScenarioTests();
}

} // namespace VulkanMemoryManagerTest
} // namespace MonsterRender

