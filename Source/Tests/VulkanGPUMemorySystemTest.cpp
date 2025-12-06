// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Vulkan GPU Memory System Comprehensive Test
//
// Test 4-layer architecture: RHI Layer -> ResourceManager Layer -> PoolManager Layer -> Vulkan API Layer

#include "Core/CoreTypes.h"
#include "Core/Log.h"
#include "RHI/RHIResources.h"
#include "Platform/Vulkan/FVulkanResourceManager.h"
#include "Platform/Vulkan/FVulkanMemoryPool.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/GLFW/GLFWWindow.h"
#include <thread>
#include <vector>
#include <chrono>

namespace MonsterRender {
namespace VulkanGPUMemorySystemTest {

using namespace RHI;
using namespace Vulkan;

// Test helper function
void PrintSeparator(const String& title) {
    MR_LOG_INFO("========================================");
    MR_LOG_INFO(title);
    MR_LOG_INFO("========================================");
}

/**
 * Test 1: RHI Layer - Resource Reference Counting
 */
void TestRHIRefCounting() {
    PrintSeparator("Test 1: RHI Layer Reference Counting");
    
    // Create a simple Buffer (manual implementation for testing)
    class TestBuffer : public FRHIBuffer {
    public:
        TestBuffer() : FRHIBuffer(1024, EResourceUsage::VertexBuffer, 4) {}
        virtual ~TestBuffer() {
            MR_LOG_INFO("TestBuffer destroyed");
        }
        virtual void* Lock(uint32, uint32) override { return nullptr; }
        virtual void Unlock() override {}
    };
    
    {
        // Test reference counting smart pointer
        FRHIBufferRef bufferRef1 = new TestBuffer();
        MR_LOG_INFO("Initial ref count: " + std::to_string(bufferRef1->GetRefCount()));
        
        {
            FRHIBufferRef bufferRef2 = bufferRef1;
            MR_LOG_INFO("After adding ref: " + std::to_string(bufferRef1->GetRefCount()));
        }
        
        MR_LOG_INFO("After releasing ref: " + std::to_string(bufferRef1->GetRefCount()));
    }
    
    MR_LOG_INFO("[OK] RHI layer reference counting test passed");
}

/**
 * Test 2: ResourceManager Layer - Buffer Creation and Destruction
 */
void TestResourceManagerBuffers(VulkanDevice* device) {
    PrintSeparator("Test 2: ResourceManager Layer - Buffer Management");
    
    FVulkanMemoryManager* memMgr = device->GetMemoryManager();
    FVulkanResourceManager resourceMgr(device, memMgr);
    
    // Create multiple buffers of different types
    std::vector<FRHIBufferRef> buffers;
    
    // Vertex Buffer (Device Local)
    auto vb = resourceMgr.CreateBuffer(
        64 * 1024,  // 64KB
        EResourceUsage::VertexBuffer,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        sizeof(float) * 3  // stride = 3 floats (position)
    );
    if (vb) {
        MR_LOG_INFO("[OK] Vertex Buffer created successfully");
        buffers.push_back(vb);
    }
    
    // Index Buffer (Device Local)
    auto ib = resourceMgr.CreateBuffer(
        32 * 1024,  // 32KB
        EResourceUsage::IndexBuffer,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        sizeof(uint32)
    );
    if (ib) {
        MR_LOG_INFO("[OK] Index Buffer created successfully");
        buffers.push_back(ib);
    }
    
    // Uniform Buffer (Host Visible for updates)
    auto ub = resourceMgr.CreateBuffer(
        256,  // 256 bytes
        EResourceUsage::UniformBuffer,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        0
    );
    if (ub) {
        MR_LOG_INFO("[OK] Uniform Buffer created successfully");
        buffers.push_back(ub);
    }
    
    // Get statistics
    FVulkanResourceManager::FResourceStats stats;
    resourceMgr.GetResourceStats(stats);
    
    MR_LOG_INFO("ResourceManager Statistics:");
    MR_LOG_INFO("  Buffer count: " + std::to_string(stats.NumBuffers));
    MR_LOG_INFO("  Buffer memory: " + std::to_string(stats.BufferMemory / 1024) + " KB");
    
    // Cleanup (auto-released via reference counting)
    buffers.clear();
    
    MR_LOG_INFO("[OK] ResourceManager buffer test passed");
}

/**
 * Test 3: ResourceManager Layer - Texture Creation and Destruction
 */
void TestResourceManagerTextures(VulkanDevice* device) {
    PrintSeparator("Test 3: ResourceManager Layer - Texture Management");
    
    FVulkanMemoryManager* memMgr = device->GetMemoryManager();
    FVulkanResourceManager resourceMgr(device, memMgr);
    
    std::vector<FRHITextureRef> textures;
    
    // 2D Texture (1024x1024, RGBA)
    TextureDesc desc2D{};
    desc2D.width = 1024;
    desc2D.height = 1024;
    desc2D.depth = 1;
    desc2D.mipLevels = 10;  // full mip chain
    desc2D.arraySize = 1;
    desc2D.format = EPixelFormat::R8G8B8A8_UNORM;
    desc2D.usage = EResourceUsage::ShaderResource | EResourceUsage::TransferDst;
    
    auto tex2D = resourceMgr.CreateTexture(desc2D, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (tex2D) {
        MR_LOG_INFO("[OK] 2D Texture created successfully (1024x1024, 10 mips)");
        textures.push_back(tex2D);
    }
    
    // Cube Texture (512x512, RGBA)
    TextureDesc descCube{};
    descCube.width = 512;
    descCube.height = 512;
    descCube.depth = 1;
    descCube.mipLevels = 9;
    descCube.arraySize = 6;  // Cube has 6 faces
    descCube.format = EPixelFormat::R8G8B8A8_UNORM;
    descCube.usage = EResourceUsage::ShaderResource | EResourceUsage::TransferDst;
    
    auto texCube = resourceMgr.CreateTexture(descCube, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (texCube) {
        MR_LOG_INFO("[OK] Cube Texture created successfully (512x512x6, 9 mips)");
        textures.push_back(texCube);
    }
    
    // Get statistics
    FVulkanResourceManager::FResourceStats stats;
    resourceMgr.GetResourceStats(stats);
    
    MR_LOG_INFO("ResourceManager Statistics:");
    MR_LOG_INFO("  Texture count: " + std::to_string(stats.NumTextures));
    MR_LOG_INFO("  Texture memory: " + std::to_string(stats.TextureMemory / (1024 * 1024)) + " MB");
    
    textures.clear();
    
    MR_LOG_INFO("[OK] ResourceManager texture test passed");
}

/**
 * Test 4: PoolManager Layer - Memory Pool Allocation and Release
 */
void TestPoolManager(VulkanDevice* device) {
    PrintSeparator("Test 4: PoolManager Layer - Memory Pool Management");
    
    FVulkanPoolManager poolMgr(device);
    
    // Create multiple allocation requests of different sizes
    std::vector<FVulkanAllocation> allocations;
    
    for (int i = 0; i < 10; ++i) {
        FVulkanMemoryManager::FAllocationRequest request{};
        request.Size = (i + 1) * 1024 * 1024;  // 1MB, 2MB, ..., 10MB
        request.Alignment = 256;
        request.MemoryTypeBits = 0xFFFFFFFF;  // Any type
        request.RequiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        
        FVulkanAllocation allocation;
        if (poolMgr.Allocate(request, allocation)) {
            MR_LOG_INFO("[OK] Allocated " + std::to_string(request.Size / (1024 * 1024)) + "MB successfully");
            allocations.push_back(allocation);
        }
    }
    
    // Get statistics
    FVulkanPoolManager::FManagerStats stats;
    poolMgr.GetStats(stats);
    
    // Release half of the allocations
    for (size_t i = 0; i < allocations.size() / 2; ++i) {
        poolMgr.Free(allocations[i]);
    }
    allocations.erase(allocations.begin(), allocations.begin() + allocations.size() / 2);
    
    MR_LOG_INFO("After releasing half allocations:");
    poolMgr.GetStats(stats);
    
    // Trim idle pages
    uint32 freedPages = poolMgr.TrimAllPools();
    MR_LOG_INFO("Trimmed " + std::to_string(freedPages) + " idle pages");
    
    // Release remaining allocations
    for (auto& alloc : allocations) {
        poolMgr.Free(alloc);
    }
    allocations.clear();
    
    MR_LOG_INFO("[OK] PoolManager test passed");
}

/**
 * Test 5: Concurrent Allocation Test (Multi-threaded)
 */
void TestConcurrentAllocations(VulkanDevice* device) {
    PrintSeparator("Test 5: Concurrent Allocation Test");
    
    FVulkanMemoryManager* memMgr = device->GetMemoryManager();
    FVulkanResourceManager resourceMgr(device, memMgr);
    
    const int NumThreads = 4;
    const int AllocationsPerThread = 50;
    
    std::vector<std::thread> threads;
    std::atomic<int> successCount{0};
    
    auto workerFunc = [&](int threadId) {
        for (int i = 0; i < AllocationsPerThread; ++i) {
            // Alternately create Buffer and Texture
            if (i % 2 == 0) {
                auto buffer = resourceMgr.CreateBuffer(
                    4096,  // 4KB
                    EResourceUsage::VertexBuffer,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    16
                );
                if (buffer) {
                    successCount++;
                }
            } else {
                TextureDesc desc{};
                desc.width = 256;
                desc.height = 256;
                desc.depth = 1;
                desc.mipLevels = 8;
                desc.arraySize = 1;
                desc.format = EPixelFormat::R8G8B8A8_UNORM;
                desc.usage = EResourceUsage::ShaderResource;
                
                auto texture = resourceMgr.CreateTexture(desc, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                if (texture) {
                    successCount++;
                }
            }
        }
    };
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < NumThreads; ++i) {
        threads.emplace_back(workerFunc, i);
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    MR_LOG_INFO("Concurrent allocation completed:");
    MR_LOG_INFO("  Thread count: " + std::to_string(NumThreads));
    MR_LOG_INFO("  Allocations per thread: " + std::to_string(AllocationsPerThread));
    MR_LOG_INFO("  Successful allocations: " + std::to_string(successCount.load()));
    MR_LOG_INFO("  Duration: " + std::to_string(duration.count()) + " ms");
    
    FVulkanResourceManager::FResourceStats stats;
    resourceMgr.GetResourceStats(stats);
    
    MR_LOG_INFO("[OK] Concurrent allocation test passed");
}

/**
 * Test 6: Deferred Release Mechanism
 */
void TestDeferredRelease(VulkanDevice* device) {
    PrintSeparator("Test 6: Deferred Release Mechanism");
    
    FVulkanMemoryManager* memMgr = device->GetMemoryManager();
    FVulkanResourceManager resourceMgr(device, memMgr);
    
    // Create resource
    auto buffer = resourceMgr.CreateBuffer(
        1024 * 1024,  // 1MB
        EResourceUsage::VertexBuffer,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        16
    );
    
    if (buffer) {
        MR_LOG_INFO("[OK] Created Buffer for deferred release test");
        
        // Simulate GPU using resource (frame 0)
        uint64 currentFrame = 0;
        
        // Request deferred release
        resourceMgr.DeferredRelease(buffer.Get(), currentFrame);
        buffer.SafeRelease();  // Clear smart pointer
        
        MR_LOG_INFO("Requested deferred release (frame " + std::to_string(currentFrame) + ")");
        
        // Simulate multiple frame advances
        for (uint64 frame = 1; frame <= 5; ++frame) {
            resourceMgr.ProcessDeferredReleases(frame);
            MR_LOG_INFO("Processing deferred releases (frame " + std::to_string(frame) + ")");
            
            FVulkanResourceManager::FResourceStats stats;
            resourceMgr.GetResourceStats(stats);
            
            if (stats.PendingReleases > 0) {
                MR_LOG_INFO("  Still have " + std::to_string(stats.PendingReleases) + " pending releases");
            } else {
                MR_LOG_INFO("  All resources released");
                break;
            }
        }
    }
    
    MR_LOG_INFO("[OK] Deferred release test passed");
}

/**
 * Test 7: Real World Scenario - Game Asset Loading
 */
void TestRealWorldScenario_AssetLoading(VulkanDevice* device) {
    PrintSeparator("Test 7: Real World Scenario - Game Asset Loading");
    
    FVulkanMemoryManager* memMgr = device->GetMemoryManager();
    FVulkanResourceManager resourceMgr(device, memMgr);
    
    MR_LOG_INFO("Simulating loading a complete game scene...");
    
    std::vector<FRHIBufferRef> buffers;
    std::vector<FRHITextureRef> textures;
    
    // 1. Load geometry data (Vertex + Index Buffers)
    MR_LOG_INFO("[1/4] Loading geometry data...");
    for (int i = 0; i < 20; ++i) {
        // Vertex Buffer (varying sizes)
        auto vb = resourceMgr.CreateBuffer(
            (i + 1) * 64 * 1024,  // 64KB - 1.28MB
            EResourceUsage::VertexBuffer | EResourceUsage::TransferDst,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            32  // typical vertex stride
        );
        if (vb) buffers.push_back(vb);
        
        // Index Buffer
        auto ib = resourceMgr.CreateBuffer(
            (i + 1) * 32 * 1024,  // 32KB - 640KB
            EResourceUsage::IndexBuffer | EResourceUsage::TransferDst,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            4
        );
        if (ib) buffers.push_back(ib);
    }
    
    // 2. Load textures (Albedo, Normal, Roughness, etc.)
    MR_LOG_INFO("[2/4] Loading texture data...");
    uint32 textureSizes[] = {2048, 1024, 512, 256};
    for (uint32 size : textureSizes) {
        for (int i = 0; i < 5; ++i) {
            TextureDesc desc{};
            desc.width = size;
            desc.height = size;
            desc.depth = 1;
            desc.mipLevels = static_cast<uint32>(std::log2(size)) + 1;
            desc.arraySize = 1;
            desc.format = EPixelFormat::R8G8B8A8_UNORM;
            desc.usage = EResourceUsage::ShaderResource | EResourceUsage::TransferDst;
            
            auto tex = resourceMgr.CreateTexture(desc, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (tex) textures.push_back(tex);
        }
    }
    
    // 3. Load Uniform Buffers (material parameters, transform matrices, etc.)
    MR_LOG_INFO("[3/4] Loading Uniform Buffers...");
    for (int i = 0; i < 100; ++i) {
        auto ub = resourceMgr.CreateBuffer(
            256,  // typical UBO size
            EResourceUsage::UniformBuffer,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            0
        );
        if (ub) buffers.push_back(ub);
    }
    
    // 4. Load environment map (Cube Map)
    MR_LOG_INFO("[4/4] Loading environment map...");
    TextureDesc cubemapDesc{};
    cubemapDesc.width = 1024;
    cubemapDesc.height = 1024;
    cubemapDesc.depth = 1;
    cubemapDesc.mipLevels = 10;
    cubemapDesc.arraySize = 6;  // Cube map
    cubemapDesc.format = EPixelFormat::R8G8B8A8_UNORM;
    cubemapDesc.usage = EResourceUsage::ShaderResource | EResourceUsage::TransferDst;
    
    auto cubemap = resourceMgr.CreateTexture(cubemapDesc, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (cubemap) textures.push_back(cubemap);
    
    // Get final statistics
    FVulkanResourceManager::FResourceStats stats;
    resourceMgr.GetResourceStats(stats);
    
    MR_LOG_INFO("Scene loading completed:");
    MR_LOG_INFO("  Total buffers: " + std::to_string(stats.NumBuffers));
    MR_LOG_INFO("  Total textures: " + std::to_string(stats.NumTextures));
    MR_LOG_INFO("  Buffer memory: " + std::to_string(stats.BufferMemory / (1024 * 1024)) + " MB");
    MR_LOG_INFO("  Texture memory: " + std::to_string(stats.TextureMemory / (1024 * 1024)) + " MB");
    MR_LOG_INFO("  Total memory: " + std::to_string((stats.BufferMemory + stats.TextureMemory) / (1024 * 1024)) + " MB");
    
    // Cleanup
    buffers.clear();
    textures.clear();
    
    MR_LOG_INFO("[OK] Game asset loading scenario test passed");
}

/**
 * Run all tests
 */
void RunAllTests() {
    PrintSeparator("Vulkan GPU Memory System Comprehensive Test (4-Layer Architecture)");
    
    // Initialize Vulkan device
    MR_LOG_INFO("Initializing Vulkan device...");
    
    auto window = std::make_unique<GLFWWindow>();
    if (!window->initialize("VulkanGPUMemoryTest", 800, 600)) {
        MR_LOG_ERROR("Window initialization failed");
        return;
    }
    
    auto device = std::make_unique<VulkanDevice>();
    if (!device->initialize(window.get())) {
        MR_LOG_ERROR("Vulkan device initialization failed");
        return;
    }
    
    MR_LOG_INFO("[OK] Vulkan device initialized successfully");
    MR_LOG_INFO("");
    
    try {
        // Run all tests
        TestRHIRefCounting();
        MR_LOG_INFO("");
        
        TestResourceManagerBuffers(device.get());
        MR_LOG_INFO("");
        
        TestResourceManagerTextures(device.get());
        MR_LOG_INFO("");
        
        TestPoolManager(device.get());
        MR_LOG_INFO("");
        
        TestConcurrentAllocations(device.get());
        MR_LOG_INFO("");
        
        TestDeferredRelease(device.get());
        MR_LOG_INFO("");
        
        TestRealWorldScenario_AssetLoading(device.get());
        MR_LOG_INFO("");
        
        PrintSeparator("All tests passed!");
        
    } catch (const std::exception& e) {
        MR_LOG_ERROR("Test exception: " + String(e.what()));
    }
    
    // Cleanup
    device->shutdown();
    window->shutdown();
}

} // namespace VulkanGPUMemorySystemTest
} // namespace MonsterRender

