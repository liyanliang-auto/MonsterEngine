// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Vulkan GPU Memory System Comprehensive Test
//
// 测试四层架构：RHI Layer -> ResourceManager Layer -> PoolManager Layer -> Vulkan API Layer

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

// 测试辅助函数
void PrintSeparator(const String& title) {
    MR_LOG_INFO("========================================");
    MR_LOG_INFO(title);
    MR_LOG_INFO("========================================");
}

/**
 * 测试 1: RHI 层 - 资源引用计数
 */
void TestRHIRefCounting() {
    PrintSeparator("测试 1: RHI 层引用计数");
    
    // 创建一个简单的 Buffer（手动实现用于测试）
    class TestBuffer : public FRHIBuffer {
    public:
        TestBuffer() : FRHIBuffer(1024, EResourceUsage::VertexBuffer, 4) {}
        virtual ~TestBuffer() {
            MR_LOG_INFO("TestBuffer 被销毁");
        }
        virtual void* Lock(uint32, uint32) override { return nullptr; }
        virtual void Unlock() override {}
    };
    
    {
        // 测试引用计数智能指针
        FRHIBufferRef bufferRef1 = new TestBuffer();
        MR_LOG_INFO("初始引用计数: " + std::to_string(bufferRef1->GetRefCount()));
        
        {
            FRHIBufferRef bufferRef2 = bufferRef1;
            MR_LOG_INFO("增加引用后: " + std::to_string(bufferRef1->GetRefCount()));
        }
        
        MR_LOG_INFO("减少引用后: " + std::to_string(bufferRef1->GetRefCount()));
    }
    
    MR_LOG_INFO("[OK] RHI 层引用计数测试通过");
}

/**
 * 测试 2: ResourceManager 层 - 缓冲区创建和销毁
 */
void TestResourceManagerBuffers(VulkanDevice* device) {
    PrintSeparator("测试 2: ResourceManager 层 - 缓冲区管理");
    
    FVulkanMemoryManager* memMgr = device->GetMemoryManager();
    FVulkanResourceManager resourceMgr(device, memMgr);
    
    // 创建多个不同类型的缓冲区
    std::vector<FRHIBufferRef> buffers;
    
    // Vertex Buffer (Device Local)
    auto vb = resourceMgr.CreateBuffer(
        64 * 1024,  // 64KB
        EResourceUsage::VertexBuffer,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        sizeof(float) * 3  // stride = 3 floats (position)
    );
    if (vb) {
        MR_LOG_INFO("[OK] Vertex Buffer 创建成功");
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
        MR_LOG_INFO("[OK] Index Buffer 创建成功");
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
        MR_LOG_INFO("[OK] Uniform Buffer 创建成功");
        buffers.push_back(ub);
    }
    
    // 获取统计信息
    FVulkanResourceManager::FResourceStats stats;
    resourceMgr.GetResourceStats(stats);
    
    MR_LOG_INFO("ResourceManager 统计:");
    MR_LOG_INFO("  缓冲区数量: " + std::to_string(stats.NumBuffers));
    MR_LOG_INFO("  缓冲区内存: " + std::to_string(stats.BufferMemory / 1024) + " KB");
    
    // 清理（通过引用计数自动释放）
    buffers.clear();
    
    MR_LOG_INFO("[OK] ResourceManager 缓冲区测试通过");
}

/**
 * 测试 3: ResourceManager 层 - 纹理创建和销毁
 */
void TestResourceManagerTextures(VulkanDevice* device) {
    PrintSeparator("测试 3: ResourceManager 层 - 纹理管理");
    
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
        MR_LOG_INFO("[OK] 2D Texture 创建成功 (1024x1024, 10 mips)");
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
        MR_LOG_INFO("[OK] Cube Texture 创建成功 (512x512x6, 9 mips)");
        textures.push_back(texCube);
    }
    
    // 获取统计信息
    FVulkanResourceManager::FResourceStats stats;
    resourceMgr.GetResourceStats(stats);
    
    MR_LOG_INFO("ResourceManager 统计:");
    MR_LOG_INFO("  纹理数量: " + std::to_string(stats.NumTextures));
    MR_LOG_INFO("  纹理内存: " + std::to_string(stats.TextureMemory / (1024 * 1024)) + " MB");
    
    textures.clear();
    
    MR_LOG_INFO("[OK] ResourceManager 纹理测试通过");
}

/**
 * 测试 4: PoolManager 层 - 内存池分配和释放
 */
void TestPoolManager(VulkanDevice* device) {
    PrintSeparator("测试 4: PoolManager 层 - 内存池管理");
    
    FVulkanPoolManager poolMgr(device);
    
    // 创建多个不同大小的分配请求
    std::vector<FVulkanAllocation> allocations;
    
    for (int i = 0; i < 10; ++i) {
        FVulkanMemoryManager::FAllocationRequest request{};
        request.Size = (i + 1) * 1024 * 1024;  // 1MB, 2MB, ..., 10MB
        request.Alignment = 256;
        request.MemoryTypeBits = 0xFFFFFFFF;  // 任意类型
        request.RequiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        
        FVulkanAllocation allocation;
        if (poolMgr.Allocate(request, allocation)) {
            MR_LOG_INFO("[OK] 分配 " + std::to_string(request.Size / (1024 * 1024)) + "MB 成功");
            allocations.push_back(allocation);
        }
    }
    
    // 获取统计信息
    FVulkanPoolManager::FManagerStats stats;
    poolMgr.GetStats(stats);
    
    // 释放一半的分配
    for (size_t i = 0; i < allocations.size() / 2; ++i) {
        poolMgr.Free(allocations[i]);
    }
    allocations.erase(allocations.begin(), allocations.begin() + allocations.size() / 2);
    
    MR_LOG_INFO("释放一半分配后:");
    poolMgr.GetStats(stats);
    
    // 清理空闲页
    uint32 freedPages = poolMgr.TrimAllPools();
    MR_LOG_INFO("清理了 " + std::to_string(freedPages) + " 个空闲页");
    
    // 释放剩余分配
    for (auto& alloc : allocations) {
        poolMgr.Free(alloc);
    }
    allocations.clear();
    
    MR_LOG_INFO("[OK] PoolManager 测试通过");
}

/**
 * 测试 5: 并发分配测试（多线程）
 */
void TestConcurrentAllocations(VulkanDevice* device) {
    PrintSeparator("测试 5: 并发分配测试");
    
    FVulkanMemoryManager* memMgr = device->GetMemoryManager();
    FVulkanResourceManager resourceMgr(device, memMgr);
    
    const int NumThreads = 4;
    const int AllocationsPerThread = 50;
    
    std::vector<std::thread> threads;
    std::atomic<int> successCount{0};
    
    auto workerFunc = [&](int threadId) {
        for (int i = 0; i < AllocationsPerThread; ++i) {
            // 交替创建 Buffer 和 Texture
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
    
    MR_LOG_INFO("并发分配完成:");
    MR_LOG_INFO("  线程数: " + std::to_string(NumThreads));
    MR_LOG_INFO("  每线程分配数: " + std::to_string(AllocationsPerThread));
    MR_LOG_INFO("  成功分配数: " + std::to_string(successCount.load()));
    MR_LOG_INFO("  耗时: " + std::to_string(duration.count()) + " ms");
    
    FVulkanResourceManager::FResourceStats stats;
    resourceMgr.GetResourceStats(stats);
    
    MR_LOG_INFO("[OK] 并发分配测试通过");
}

/**
 * 测试 6: 延迟释放机制
 */
void TestDeferredRelease(VulkanDevice* device) {
    PrintSeparator("测试 6: 延迟释放机制");
    
    FVulkanMemoryManager* memMgr = device->GetMemoryManager();
    FVulkanResourceManager resourceMgr(device, memMgr);
    
    // 创建资源
    auto buffer = resourceMgr.CreateBuffer(
        1024 * 1024,  // 1MB
        EResourceUsage::VertexBuffer,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        16
    );
    
    if (buffer) {
        MR_LOG_INFO("[OK] 创建 Buffer 用于延迟释放测试");
        
        // 模拟 GPU 使用资源（帧号 0）
        uint64 currentFrame = 0;
        
        // 请求延迟释放
        resourceMgr.DeferredRelease(buffer.Get(), currentFrame);
        buffer.SafeRelease();  // 清空智能指针
        
        MR_LOG_INFO("请求延迟释放（帧 " + std::to_string(currentFrame) + "）");
        
        // 模拟多帧推进
        for (uint64 frame = 1; frame <= 5; ++frame) {
            resourceMgr.ProcessDeferredReleases(frame);
            MR_LOG_INFO("处理延迟释放（帧 " + std::to_string(frame) + "）");
            
            FVulkanResourceManager::FResourceStats stats;
            resourceMgr.GetResourceStats(stats);
            
            if (stats.PendingReleases > 0) {
                MR_LOG_INFO("  仍有 " + std::to_string(stats.PendingReleases) + " 个待释放资源");
            } else {
                MR_LOG_INFO("  所有资源已释放");
                break;
            }
        }
    }
    
    MR_LOG_INFO("[OK] 延迟释放测试通过");
}

/**
 * 测试 7: 实际场景 - 游戏资产加载
 */
void TestRealWorldScenario_AssetLoading(VulkanDevice* device) {
    PrintSeparator("测试 7: 实际场景 - 游戏资产加载");
    
    FVulkanMemoryManager* memMgr = device->GetMemoryManager();
    FVulkanResourceManager resourceMgr(device, memMgr);
    
    MR_LOG_INFO("模拟加载一个完整的游戏场景...");
    
    std::vector<FRHIBufferRef> buffers;
    std::vector<FRHITextureRef> textures;
    
    // 1. 加载几何体数据（Vertex + Index Buffers）
    MR_LOG_INFO("[1/4] 加载几何体数据...");
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
    
    // 2. 加载纹理（Albedo, Normal, Roughness, etc.）
    MR_LOG_INFO("[2/4] 加载纹理数据...");
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
    
    // 3. 加载 Uniform Buffers（材质参数、变换矩阵等）
    MR_LOG_INFO("[3/4] 加载 Uniform Buffers...");
    for (int i = 0; i < 100; ++i) {
        auto ub = resourceMgr.CreateBuffer(
            256,  // typical UBO size
            EResourceUsage::UniformBuffer,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            0
        );
        if (ub) buffers.push_back(ub);
    }
    
    // 4. 加载环境贴图（Cube Map）
    MR_LOG_INFO("[4/4] 加载环境贴图...");
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
    
    // 获取最终统计
    FVulkanResourceManager::FResourceStats stats;
    resourceMgr.GetResourceStats(stats);
    
    MR_LOG_INFO("场景加载完成:");
    MR_LOG_INFO("  总缓冲区: " + std::to_string(stats.NumBuffers));
    MR_LOG_INFO("  总纹理: " + std::to_string(stats.NumTextures));
    MR_LOG_INFO("  缓冲区内存: " + std::to_string(stats.BufferMemory / (1024 * 1024)) + " MB");
    MR_LOG_INFO("  纹理内存: " + std::to_string(stats.TextureMemory / (1024 * 1024)) + " MB");
    MR_LOG_INFO("  总内存: " + std::to_string((stats.BufferMemory + stats.TextureMemory) / (1024 * 1024)) + " MB");
    
    // 清理
    buffers.clear();
    textures.clear();
    
    MR_LOG_INFO("[OK] 游戏资产加载场景测试通过");
}

/**
 * 运行所有测试
 */
void RunAllTests() {
    PrintSeparator("Vulkan GPU 内存系统综合测试（四层架构）");
    
    // 初始化 Vulkan 设备
    MR_LOG_INFO("初始化 Vulkan 设备...");
    
    auto window = std::make_unique<GLFWWindow>();
    if (!window->initialize("VulkanGPUMemoryTest", 800, 600)) {
        MR_LOG_ERROR("窗口初始化失败");
        return;
    }
    
    auto device = std::make_unique<VulkanDevice>();
    if (!device->initialize(window.get())) {
        MR_LOG_ERROR("Vulkan 设备初始化失败");
        return;
    }
    
    MR_LOG_INFO("[OK] Vulkan 设备初始化成功");
    MR_LOG_INFO("");
    
    try {
        // 运行各项测试
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
        
        PrintSeparator("所有测试通过！");
        
    } catch (const std::exception& e) {
        MR_LOG_ERROR("测试异常: " + String(e.what()));
    }
    
    // 清理
    device->shutdown();
    window->shutdown();
}

} // namespace VulkanGPUMemorySystemTest
} // namespace MonsterRender

