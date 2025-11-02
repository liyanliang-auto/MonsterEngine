// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Vulkan Memory Manager Test Suite
// 参考 UE5 的内存管理测试设计

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
// 辅助函数
// ================================

/**
 * 创建测试用的 RHI 设备
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
 * 格式化内存大小显示
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
// 基础功能测试
// ================================

/**
 * 测试 1: 基础分配和释放
 */
static void Test_BasicAllocation() {
    MR_LOG_INFO("\n[Test 1] 基础分配和释放测试");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    auto* vulkanDevice = static_cast<VulkanDevice*>(device.get());
    auto* memoryManager = vulkanDevice->getMemoryManager();
    
    // 创建小缓冲区（64KB）
    BufferDesc bufferDesc{};
    bufferDesc.size = 64 * 1024;
    bufferDesc.usage = EResourceUsage::VertexBuffer;
    bufferDesc.cpuAccessible = false;
    bufferDesc.debugName = "TestBuffer_64KB";
    
    auto buffer = device->createBuffer(bufferDesc);
    if (buffer) {
        MR_LOG_INFO("  [OK] 成功分配 64KB 缓冲区");
        
        // 验证缓冲区有效
        auto* vulkanBuffer = static_cast<VulkanBuffer*>(buffer.get());
        if (vulkanBuffer->isValid()) {
            MR_LOG_INFO("  [OK] 缓冲区验证通过");
        }
    } else {
        MR_LOG_ERROR("  [FAIL] 缓冲区分配失败");
    }
    
    // 释放缓冲区（智能指针自动释放）
    buffer.reset();
    MR_LOG_INFO("  [OK] 缓冲区已释放");
    
    // 获取内存统计
    FVulkanMemoryManager::FMemoryStats stats;
    memoryManager->GetMemoryStats(stats);
    
    MR_LOG_INFO("  内存统计:");
    MR_LOG_INFO("    总分配: " + FormatMemorySize(stats.TotalAllocated));
    MR_LOG_INFO("    堆数量: " + std::to_string(stats.HeapCount));
    
    MR_LOG_INFO("  [OK] Test 1 完成\n");
}

/**
 * 测试 2: 子分配（Sub-Allocation）
 * 验证多个小分配共享一个大的 VkDeviceMemory
 */
static void Test_SubAllocation() {
    MR_LOG_INFO("\n[Test 2] 子分配测试");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    auto* vulkanDevice = static_cast<VulkanDevice*>(device.get());
    auto* memoryManager = vulkanDevice->getMemoryManager();
    
    // 创建多个小缓冲区（应该使用子分配）
    TArray<TSharedPtr<IRHIBuffer>> buffers;
    const int numBuffers = 10;
    const uint32 bufferSize = 256 * 1024;  // 256KB
    
    MR_LOG_INFO("  分配 " + std::to_string(numBuffers) + " 个 " + 
                FormatMemorySize(bufferSize) + " 缓冲区...");
    
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
            MR_LOG_ERROR("  [FAIL] Buffer " + std::to_string(i) + " 分配失败");
        }
    }
    
    MR_LOG_INFO("  [OK] 成功分配 " + std::to_string(buffers.size()) + " 个缓冲区");
    
    // 获取统计信息
    FVulkanMemoryManager::FMemoryStats stats;
    memoryManager->GetMemoryStats(stats);
    
    MR_LOG_INFO("  子分配统计:");
    MR_LOG_INFO("    总分配: " + FormatMemorySize(stats.TotalAllocated));
    MR_LOG_INFO("    总预留: " + FormatMemorySize(stats.TotalReserved));
    MR_LOG_INFO("    堆数量: " + std::to_string(stats.HeapCount));
    MR_LOG_INFO("    分配次数: " + std::to_string(stats.AllocationCount));
    
    float utilization = (float)stats.TotalAllocated / stats.TotalReserved * 100.0f;
    MR_LOG_INFO("    内存利用率: " + std::to_string(utilization) + "%");
    
    if (stats.HeapCount <= 2) {
        MR_LOG_INFO("  [OK] 子分配工作正常（多个缓冲区共享少量堆）");
    }
    
    // 释放部分缓冲区
    buffers.erase(buffers.begin(), buffers.begin() + 5);
    MR_LOG_INFO("  [OK] 释放了 5 个缓冲区");
    
    memoryManager->GetMemoryStats(stats);
    MR_LOG_INFO("    释放后总分配: " + FormatMemorySize(stats.TotalAllocated));
    
    MR_LOG_INFO("  [OK] Test 2 完成\n");
}

/**
 * 测试 3: 对齐要求
 * 验证不同对齐要求的分配
 */
static void Test_Alignment() {
    MR_LOG_INFO("\n[Test 3] 对齐要求测试");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    // 测试不同对齐的缓冲区
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
            
            // 验证对齐
            if (offset % alignment == 0) {
                MR_LOG_INFO("  [OK] " + std::to_string(alignment) + " 字节对齐: offset = " +
                            std::to_string(offset));
            } else {
                MR_LOG_WARNING("  [WARN] " + std::to_string(alignment) + " 字节对齐失败");
            }
        }
    }
    
    MR_LOG_INFO("  [OK] Test 3 完成\n");
}

/**
 * 测试 4: 碎片化和合并
 * 测试内存块的分配、释放和合并
 */
static void Test_Fragmentation() {
    MR_LOG_INFO("\n[Test 4] 碎片化和合并测试");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    auto* vulkanDevice = static_cast<VulkanDevice*>(device.get());
    auto* memoryManager = vulkanDevice->getMemoryManager();
    
    // 分配多个缓冲区
    TArray<TSharedPtr<IRHIBuffer>> buffers;
    for (int i = 0; i < 20; ++i) {
        BufferDesc desc{};
        desc.size = 128 * 1024;  // 128KB
        desc.usage = EResourceUsage::VertexBuffer;
        desc.cpuAccessible = false;
        desc.debugName = "FragBuffer_" + std::to_string(i);
        
        buffers.push_back(device->createBuffer(desc));
    }
    
    MR_LOG_INFO("  分配了 20 个缓冲区");
    
    FVulkanMemoryManager::FMemoryStats stats1;
    memoryManager->GetMemoryStats(stats1);
    MR_LOG_INFO("    分配后: " + FormatMemorySize(stats1.TotalAllocated));
    
    // 释放奇数索引的缓冲区（制造碎片）
    for (int i = 1; i < 20; i += 2) {
        buffers[i].reset();
    }
    
    MR_LOG_INFO("  释放了 10 个缓冲区（制造碎片）");
    
    FVulkanMemoryManager::FMemoryStats stats2;
    memoryManager->GetMemoryStats(stats2);
    MR_LOG_INFO("    释放后: " + FormatMemorySize(stats2.TotalAllocated));
    MR_LOG_INFO("    最大空闲块: " + FormatMemorySize(stats2.LargestFreeBlock));
    
    // 尝试压缩
    memoryManager->Compact();
    MR_LOG_INFO("  [OK] 执行内存压缩");
    
    MR_LOG_INFO("  [OK] Test 4 完成\n");
}

/**
 * 测试 5: 专用分配（Dedicated Allocation）
 * 测试大资源使用专用内存
 */
static void Test_DedicatedAllocation() {
    MR_LOG_INFO("\n[Test 5] 专用分配测试");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    auto* vulkanDevice = static_cast<VulkanDevice*>(device.get());
    auto* memoryManager = vulkanDevice->getMemoryManager();
    
    // 创建大纹理（应该触发专用分配）
    TextureDesc texDesc{};
    texDesc.width = 4096;
    texDesc.height = 4096;
    texDesc.mipLevels = 1;
    texDesc.format = EPixelFormat::R8G8B8A8_UNORM;
    texDesc.usage = EResourceUsage::ShaderResource;
    texDesc.debugName = "LargeTexture_4K";
    
    MR_LOG_INFO("  创建 4K 纹理（应使用专用分配）...");
    
    FVulkanMemoryManager::FMemoryStats statsBefore;
    memoryManager->GetMemoryStats(statsBefore);
    
    auto texture = device->createTexture(texDesc);
    if (texture) {
        MR_LOG_INFO("  [OK] 纹理创建成功");
        
        auto* vulkanTexture = static_cast<VulkanTexture*>(texture.get());
        if (vulkanTexture->isValid()) {
            MR_LOG_INFO("  [OK] 纹理验证通过");
        }
    }
    
    FVulkanMemoryManager::FMemoryStats statsAfter;
    memoryManager->GetMemoryStats(statsAfter);
    
    VkDeviceSize allocated = statsAfter.TotalAllocated - statsBefore.TotalAllocated;
    MR_LOG_INFO("  纹理内存: " + FormatMemorySize(allocated));
    
    // 预期大小: 4096 * 4096 * 4 = 64MB
    VkDeviceSize expectedSize = 4096ULL * 4096ULL * 4ULL;
    if (allocated >= expectedSize * 0.9f) {  // 允许一些开销
        MR_LOG_INFO("  [OK] 专用分配大小符合预期");
    }
    
    MR_LOG_INFO("  [OK] Test 5 完成\n");
}

/**
 * 测试 6: 内存类型选择
 * 测试不同内存属性的分配
 */
static void Test_MemoryTypeSelection() {
    MR_LOG_INFO("\n[Test 6] 内存类型选择测试");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    // CPU 可见内存（Staging Buffer）
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
                MR_LOG_INFO("  [OK] CPU 可见内存分配成功（可映射）");
                buffer->unmap();
            }
        }
    }
    
    // GPU 专用内存（Device Local）
    {
        BufferDesc desc{};
        desc.size = 1024 * 1024;  // 1MB
        desc.usage = EResourceUsage::VertexBuffer;
        desc.cpuAccessible = false;
        desc.debugName = "GPUOnlyBuffer";
        
        auto buffer = device->createBuffer(desc);
        if (buffer) {
            MR_LOG_INFO("  [OK] GPU 专用内存分配成功");
        }
    }
    
    // CPU 缓存一致性内存（Uniform Buffer）
    {
        BufferDesc desc{};
        desc.size = 256;
        desc.usage = EResourceUsage::UniformBuffer;
        desc.cpuAccessible = true;
        desc.debugName = "UniformBuffer";
        
        auto buffer = device->createBuffer(desc);
        if (buffer) {
            MR_LOG_INFO("  [OK] Uniform Buffer 内存分配成功");
        }
    }
    
    MR_LOG_INFO("  [OK] Test 6 完成\n");
}

/**
 * 测试 7: 堆增长
 * 测试当现有堆空间不足时，自动创建新堆
 */
static void Test_HeapGrowth() {
    MR_LOG_INFO("\n[Test 7] 堆增长测试");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    auto* vulkanDevice = static_cast<VulkanDevice*>(device.get());
    auto* memoryManager = vulkanDevice->getMemoryManager();
    
    // 分配大量小缓冲区，强制创建多个堆
    TArray<TSharedPtr<IRHIBuffer>> buffers;
    const int numBuffers = 100;
    
    MR_LOG_INFO("  分配 " + std::to_string(numBuffers) + " 个缓冲区...");
    
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
            MR_LOG_INFO("    " + std::to_string(i + 1) + " 个缓冲区: " +
                        std::to_string(stats.HeapCount) + " 个堆, " +
                        FormatMemorySize(stats.TotalReserved) + " 总预留");
        }
    }
    
    FVulkanMemoryManager::FMemoryStats finalStats;
    memoryManager->GetMemoryStats(finalStats);
    
    MR_LOG_INFO("  最终统计:");
    MR_LOG_INFO("    堆数量: " + std::to_string(finalStats.HeapCount));
    MR_LOG_INFO("    总分配: " + FormatMemorySize(finalStats.TotalAllocated));
    MR_LOG_INFO("    总预留: " + FormatMemorySize(finalStats.TotalReserved));
    
    if (finalStats.HeapCount > 1) {
        MR_LOG_INFO("  [OK] 堆自动增长工作正常");
    }
    
    MR_LOG_INFO("  [OK] Test 7 完成\n");
}

/**
 * 测试 8: 并发分配
 * 测试多线程环境下的内存分配
 */
static void Test_ConcurrentAllocation() {
    MR_LOG_INFO("\n[Test 8] 并发分配测试");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    const int numThreads = 4;
    const int allocsPerThread = 10;
    
    MR_LOG_INFO("  启动 " + std::to_string(numThreads) + " 个线程，每个分配 " +
                std::to_string(allocsPerThread) + " 个缓冲区...");
    
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
            
            // 随机延迟
            std::this_thread::sleep_for(std::chrono::milliseconds(rand() % 10));
        }
    };
    
    // 启动线程
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back(allocFunc, i);
    }
    
    // 等待完成
    for (auto& thread : threads) {
        thread.join();
    }
    
    MR_LOG_INFO("  并发分配完成:");
    MR_LOG_INFO("    成功: " + std::to_string(successCount.load()));
    MR_LOG_INFO("    失败: " + std::to_string(failCount.load()));
    
    if (failCount == 0) {
        MR_LOG_INFO("  [OK] 并发分配安全性验证通过");
    } else {
        MR_LOG_WARNING("  [WARN] 存在分配失败");
    }
    
    MR_LOG_INFO("  [OK] Test 8 完成\n");
}

/**
 * 测试 9: 统计追踪
 * 验证内存统计的准确性
 */
static void Test_StatisticsTracking() {
    MR_LOG_INFO("\n[Test 9] 统计追踪测试");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    auto* vulkanDevice = static_cast<VulkanDevice*>(device.get());
    auto* memoryManager = vulkanDevice->getMemoryManager();
    
    // 初始统计
    FVulkanMemoryManager::FMemoryStats stats0;
    memoryManager->GetMemoryStats(stats0);
    MR_LOG_INFO("  初始状态: " + FormatMemorySize(stats0.TotalAllocated));
    
    // 分配一些资源
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
    
    MR_LOG_INFO("  分配统计:");
    MR_LOG_INFO("    预期分配: " + FormatMemorySize(totalExpected));
    MR_LOG_INFO("    实际分配: " + FormatMemorySize(actualAllocated));
    MR_LOG_INFO("    分配次数: " + std::to_string(stats1.AllocationCount - stats0.AllocationCount));
    
    // 验证准确性（允许一些对齐和开销）
    float accuracy = (float)totalExpected / actualAllocated * 100.0f;
    MR_LOG_INFO("    准确性: " + std::to_string(accuracy) + "%");
    
    if (accuracy > 80.0f) {
        MR_LOG_INFO("  [OK] 统计追踪准确");
    }
    
    MR_LOG_INFO("  [OK] Test 9 完成\n");
}

// ================================
// 实际应用场景测试
// ================================

/**
 * 场景 1: 游戏资产加载
 * 模拟游戏启动时加载大量网格和纹理
 */
static void Scenario_GameAssetLoading() {
    MR_LOG_INFO("\n[场景 1] 游戏资产加载");
    MR_LOG_INFO("  模拟: 加载 50 个网格 + 100 个纹理");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    auto* vulkanDevice = static_cast<VulkanDevice*>(device.get());
    auto* memoryManager = vulkanDevice->getMemoryManager();
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // 加载网格缓冲区
    TArray<TSharedPtr<IRHIBuffer>> meshBuffers;
    for (int i = 0; i < 50; ++i) {
        // 顶点缓冲区（随机大小：100KB - 5MB）
        uint32 vertexSize = (100 + rand() % 4900) * 1024;
        BufferDesc vertexDesc{};
        vertexDesc.size = vertexSize;
        vertexDesc.usage = EResourceUsage::VertexBuffer;
        vertexDesc.cpuAccessible = false;
        vertexDesc.debugName = "Mesh_" + std::to_string(i) + "_Vertex";
        
        meshBuffers.push_back(device->createBuffer(vertexDesc));
        
        // 索引缓冲区
        uint32 indexSize = vertexSize / 4;
        BufferDesc indexDesc{};
        indexDesc.size = indexSize;
        indexDesc.usage = EResourceUsage::IndexBuffer;
        indexDesc.cpuAccessible = false;
        indexDesc.debugName = "Mesh_" + std::to_string(i) + "_Index";
        
        meshBuffers.push_back(device->createBuffer(indexDesc));
    }
    
    MR_LOG_INFO("  [OK] 加载了 50 个网格（100 个缓冲区）");
    
    // 加载纹理
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
    
    MR_LOG_INFO("  [OK] 加载了 100 个纹理");
    
    // 统计
    FVulkanMemoryManager::FMemoryStats stats;
    memoryManager->GetMemoryStats(stats);
    
    MR_LOG_INFO("  加载统计:");
    MR_LOG_INFO("    加载时间: " + std::to_string(duration.count()) + " ms");
    MR_LOG_INFO("    总内存: " + FormatMemorySize(stats.TotalAllocated));
    MR_LOG_INFO("    堆数量: " + std::to_string(stats.HeapCount));
    MR_LOG_INFO("    分配次数: " + std::to_string(stats.AllocationCount));
    
    float avgAllocTime = (float)duration.count() / stats.AllocationCount;
    MR_LOG_INFO("    平均分配时间: " + std::to_string(avgAllocTime) + " ms");
    
    MR_LOG_INFO("  [OK] 场景 1 完成\n");
}

/**
 * 场景 2: 动态资源流送
 * 模拟运行时动态加载/卸载资源
 */
static void Scenario_DynamicResourceStreaming() {
    MR_LOG_INFO("\n[场景 2] 动态资源流送");
    MR_LOG_INFO("  模拟: 运行时流送纹理 Mip 级别");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    auto* vulkanDevice = static_cast<VulkanDevice*>(device.get());
    auto* memoryManager = vulkanDevice->getMemoryManager();
    
    // 模拟 10 帧的流送
    for (int frame = 0; frame < 10; ++frame) {
        MR_LOG_INFO("  帧 " + std::to_string(frame + 1) + ":");
        
        // 加载高分辨率 Mip
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
        
        MR_LOG_INFO("    加载 5 个高分辨率 Mip");
        MR_LOG_INFO("    当前内存: " + FormatMemorySize(stats.TotalAllocated));
        
        // 卸载（智能指针自动释放）
        highResMips.clear();
        
        memoryManager->GetMemoryStats(stats);
        MR_LOG_INFO("    卸载后: " + FormatMemorySize(stats.TotalAllocated));
    }
    
    MR_LOG_INFO("  [OK] 场景 2 完成\n");
}

/**
 * 场景 3: 粒子系统
 * 模拟大量短生命周期的小缓冲区
 */
static void Scenario_ParticleSystem() {
    MR_LOG_INFO("\n[场景 3] 粒子系统");
    MR_LOG_INFO("  模拟: 1000 个粒子发射器的动态缓冲区");
    
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
    
    MR_LOG_INFO("  每个发射器: " + std::to_string(particlesPerEmitter) + " 粒子, " +
                FormatMemorySize(particleSize));
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    TArray<TSharedPtr<IRHIBuffer>> particleBuffers;
    for (int i = 0; i < numEmitters; ++i) {
        BufferDesc desc{};
        desc.size = particleSize;
        desc.usage = EResourceUsage::StorageBuffer;
        desc.cpuAccessible = true;  // 需要 CPU 更新
        desc.debugName = "ParticleEmitter_" + std::to_string(i);
        
        particleBuffers.push_back(device->createBuffer(desc));
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    
    FVulkanMemoryManager::FMemoryStats stats;
    memoryManager->GetMemoryStats(stats);
    
    MR_LOG_INFO("  粒子系统统计:");
    MR_LOG_INFO("    分配时间: " + std::to_string(duration.count()) + " us");
    MR_LOG_INFO("    总内存: " + FormatMemorySize(stats.TotalAllocated));
    MR_LOG_INFO("    堆数量: " + std::to_string(stats.HeapCount));
    
    float allocPerEmitter = (float)duration.count() / numEmitters;
    MR_LOG_INFO("    每发射器分配时间: " + std::to_string(allocPerEmitter) + " us");
    
    if (allocPerEmitter < 100.0f) {
        MR_LOG_INFO("  [OK] 分配性能优异（< 100 us/发射器）");
    }
    
    MR_LOG_INFO("  [OK] 场景 3 完成\n");
}

/**
 * 场景 4: Uniform Buffer 池
 * 模拟频繁更新的 Uniform Buffer
 */
static void Scenario_UniformBufferPool() {
    MR_LOG_INFO("\n[场景 4] Uniform Buffer 池");
    MR_LOG_INFO("  模拟: 每帧更新的 Uniform Buffer（3 帧缓冲）");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    const int framesInFlight = 3;
    const int uniformsPerFrame = 100;
    const uint32 uniformSize = 256;  // 标准 Uniform Buffer 大小
    
    // 为每一帧创建 Uniform Buffer
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
    
    MR_LOG_INFO("  创建了 " + std::to_string(framesInFlight * uniformsPerFrame) + " 个 Uniform Buffer");
    
    // 模拟多帧更新
    for (int frame = 0; frame < 10; ++frame) {
        int frameIndex = frame % framesInFlight;
        
        // 映射并更新
        int updateCount = 0;
        for (auto& buffer : frameBuffers[frameIndex]) {
            void* mapped = buffer->map();
            if (mapped) {
                // 写入模拟数据
                memset(mapped, frame % 256, uniformSize);
                buffer->unmap();
                updateCount++;
            }
        }
        
        if (frame % 3 == 0) {
            MR_LOG_INFO("  帧 " + std::to_string(frame) + ": 更新了 " +
                        std::to_string(updateCount) + " 个 Uniform Buffer");
        }
    }
    
    MR_LOG_INFO("  [OK] 场景 4 完成\n");
}

/**
 * 场景 5: 地形系统
 * 模拟大型地形的高度图和纹理
 */
static void Scenario_TerrainSystem() {
    MR_LOG_INFO("\n[场景 5] 地形系统");
    MR_LOG_INFO("  模拟: 16 个地形块，每块 4K 高度图 + 多层纹理");
    
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
        // 高度图（单通道，浮点）
        TextureDesc heightMapDesc{};
        heightMapDesc.width = 4096;
        heightMapDesc.height = 4096;
        heightMapDesc.mipLevels = 1;
        heightMapDesc.format = EPixelFormat::R32_FLOAT;
        heightMapDesc.usage = EResourceUsage::ShaderResource;
        heightMapDesc.debugName = "HeightMap_" + std::to_string(chunk);
        
        terrainResources.push_back(device->createTexture(heightMapDesc));
        
        // 纹理层（Albedo, Normal, Roughness, AO）
        const char* layerNames[] = {"Albedo", "Normal", "Roughness", "AO"};
        for (int layer = 0; layer < 4; ++layer) {
            TextureDesc layerDesc{};
            layerDesc.width = 2048;
            layerDesc.height = 2048;
            layerDesc.mipLevels = 11;  // 完整 Mip 链
            layerDesc.format = EPixelFormat::R8G8B8A8_UNORM;
            layerDesc.usage = EResourceUsage::ShaderResource;
            layerDesc.debugName = String("Terrain_") + std::to_string(chunk) + 
                                 "_" + layerNames[layer];
            
            terrainResources.push_back(device->createTexture(layerDesc));
        }
    }
    
    MR_LOG_INFO("  创建了 " + std::to_string(terrainChunks) + " 个地形块（" +
                std::to_string(terrainResources.size()) + " 个纹理）");
    
    FVulkanMemoryManager::FMemoryStats stats;
    memoryManager->GetMemoryStats(stats);
    
    MR_LOG_INFO("  地形系统统计:");
    MR_LOG_INFO("    总内存: " + FormatMemorySize(stats.TotalAllocated));
    MR_LOG_INFO("    堆数量: " + std::to_string(stats.HeapCount));
    MR_LOG_INFO("    每地形块平均: " + 
                FormatMemorySize(stats.TotalAllocated / terrainChunks));
    
    MR_LOG_INFO("  [OK] 场景 5 完成\n");
}

/**
 * 场景 6: 内存预算管理
 * 模拟在严格内存预算下的资源管理
 */
static void Scenario_MemoryBudgetManagement() {
    MR_LOG_INFO("\n[场景 6] 内存预算管理");
    MR_LOG_INFO("  模拟: 在 512MB 预算下管理资源");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    auto* vulkanDevice = static_cast<VulkanDevice*>(device.get());
    auto* memoryManager = vulkanDevice->getMemoryManager();
    
    const VkDeviceSize memoryBudget = 512ULL * 1024 * 1024;  // 512MB
    TArray<TSharedPtr<IRHITexture>> loadedTextures;
    
    MR_LOG_INFO("  内存预算: " + FormatMemorySize(memoryBudget));
    
    int loadedCount = 0;
    int evictedCount = 0;
    
    // 尝试加载资源直到接近预算
    for (int i = 0; i < 200; ++i) {
        // 检查当前内存使用
        FVulkanMemoryManager::FMemoryStats stats;
        memoryManager->GetMemoryStats(stats);
        
        if (stats.TotalAllocated > memoryBudget * 0.9f) {
            // 接近预算，驱逐最旧的资源
            if (!loadedTextures.empty()) {
                loadedTextures.erase(loadedTextures.begin());
                evictedCount++;
                
                memoryManager->GetMemoryStats(stats);
                MR_LOG_INFO("  驱逐资源，当前内存: " + FormatMemorySize(stats.TotalAllocated));
            }
        }
        
        // 加载新纹理
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
    
    MR_LOG_INFO("  预算管理结果:");
    MR_LOG_INFO("    加载资源: " + std::to_string(loadedCount));
    MR_LOG_INFO("    驱逐资源: " + std::to_string(evictedCount));
    MR_LOG_INFO("    最终内存: " + FormatMemorySize(finalStats.TotalAllocated));
    MR_LOG_INFO("    预算利用率: " +
                std::to_string((float)finalStats.TotalAllocated / memoryBudget * 100.0f) + "%");
    
    if (finalStats.TotalAllocated <= memoryBudget) {
        MR_LOG_INFO("  [OK] 成功保持在预算内");
    }
    
    MR_LOG_INFO("  [OK] 场景 6 完成\n");
}

// ================================
// 主测试入口
// ================================

/**
 * 运行所有基础测试
 */
void RunBasicTests() {
    MR_LOG_INFO("\n");
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("  Vulkan Memory Manager - 基础测试");
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
    MR_LOG_INFO("  基础测试完成！");
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("\n");
}

/**
 * 运行所有场景测试
 */
void RunScenarioTests() {
    MR_LOG_INFO("\n");
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("  Vulkan Memory Manager - 场景测试");
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
    MR_LOG_INFO("  场景测试完成！");
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("\n");
}

/**
 * 运行所有测试
 */
void RunAllTests() {
    RunBasicTests();
    RunScenarioTests();
}

} // namespace VulkanMemoryManagerTest
} // namespace MonsterRender

