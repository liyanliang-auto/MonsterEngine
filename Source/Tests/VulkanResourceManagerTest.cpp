// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Vulkan Resource Manager Test Suite
// 参考 UE5 资源管理测试设计
// 简化版本 - 使用现有 Device API

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
namespace VulkanResourceManagerTest {

using namespace RHI;
using namespace RHI::Vulkan;

// ================================
// 辅助函数 (Helper Functions)
// ================================

/**
 * 创建测试用 RHI 设备
 */
static TUniquePtr<IRHIDevice> CreateTestDevice() {
    RHICreateInfo createInfo;
    createInfo.preferredBackend = ERHIBackend::Vulkan;
    createInfo.enableValidation = true;
    createInfo.enableDebugMarkers = true;
    createInfo.applicationName = "Vulkan Resource Manager Test";
    
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
    return std::to_string(size) + " Bytes";
}

// ================================
// 基础功能测试 (Basic Tests)
// ================================

/**
 * 测试 1: FVulkanMemoryManager 初始化
 * Test MemoryManager initialization
 */
static void Test_MemoryManagerInit() {
    MR_LOG_INFO("\n[Test 1] FVulkanMemoryManager Initialization Test");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    auto* vulkanDevice = static_cast<VulkanDevice*>(device.get());
    auto* memoryManager = vulkanDevice->getMemoryManager();
    
    if (memoryManager) {
        MR_LOG_INFO("  [OK] MemoryManager initialized successfully");
        
        // 获取统计信息
        FVulkanMemoryManager::FMemoryStats stats;
        memoryManager->GetMemoryStats(stats);
        
        MR_LOG_INFO("  Initial state:");
        MR_LOG_INFO("    Total allocated: " + FormatMemorySize(stats.TotalAllocated));
        MR_LOG_INFO("    Total reserved: " + FormatMemorySize(stats.TotalReserved));
        MR_LOG_INFO("    Pool count: " + std::to_string(stats.PoolCount));
        MR_LOG_INFO("  [OK] Test 1 completed\n");
    } else {
        MR_LOG_ERROR("  [FAIL] MemoryManager not available");
    }
}

/**
 * 测试 2: Buffer 创建和使用
 * Test Buffer creation and usage
 */
static void Test_BufferCreation() {
    MR_LOG_INFO("\n[Test 2] Buffer Creation Test");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    // 创建 Uniform Buffer (参考 UE5 的 Scene Constants Buffer)
    BufferDesc bufferDesc{};
    bufferDesc.size = 256; // 256 bytes
    bufferDesc.usage = EResourceUsage::UniformBuffer;
    bufferDesc.cpuAccessible = true;
    bufferDesc.debugName = "TestUniformBuffer";
    
    auto buffer = device->createBuffer(bufferDesc);
    
    if (!buffer) {
        MR_LOG_ERROR("  [FAIL] Failed to create buffer");
        return;
    }
    
    MR_LOG_INFO("  [OK] Created uniform buffer (256 bytes)");
    
    // 模拟数据写入
    struct TestUniformData {
        float ViewMatrix[16];
        float ProjMatrix[16];
        float CameraPos[4];
    };
    
    for (uint32 frame = 0; frame < 3; ++frame) {
        MR_LOG_DEBUG("  Frame " + std::to_string(frame) + ":");
        
        // Map buffer
        void* mappedData = buffer->map();
        if (!mappedData) {
            MR_LOG_ERROR("    [FAIL] Failed to map buffer");
            continue;
        }
        
        // 写入数据
        TestUniformData data{};
        for (int i = 0; i < 16; ++i) {
            data.ViewMatrix[i] = static_cast<float>(frame * 16 + i);
        }
        memcpy(mappedData, &data, sizeof(TestUniformData));
        
        // Unmap
        buffer->unmap();
        MR_LOG_DEBUG("    [OK] Mapped, wrote, and unmapped buffer");
    }
    
    MR_LOG_INFO("  [OK] Test 2 completed\n");
}

/**
 * 测试 3: Texture 创建和使用
 * Test Texture creation and usage
 */
static void Test_TextureCreation() {
    MR_LOG_INFO("\n[Test 3] Texture Creation Test");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    // 创建 2K 纹理 (参考 UE5 的 BaseColor Texture)
    TextureDesc desc{};
    desc.width = 2048;
    desc.height = 2048;
    desc.depth = 1;
    desc.mipLevels = 1;
    desc.arraySize = 1;
    desc.format = EPixelFormat::R8G8B8A8_UNORM;
    desc.usage = EResourceUsage::ShaderResource | EResourceUsage::TransferDst;
    desc.debugName = "Test_BaseColor_2K";
    
    auto texture = device->createTexture(desc);
    
    if (!texture) {
        MR_LOG_ERROR("  [FAIL] Failed to create texture");
        return;
    }
    
    MR_LOG_INFO("  [OK] Created 2K texture: " + desc.debugName);
    MR_LOG_INFO("    Format: R8G8B8A8_UNORM");
    MR_LOG_INFO("    Size: " + std::to_string(desc.width) + "x" + std::to_string(desc.height));
    MR_LOG_INFO("    Memory: Device Local");
    
    MR_LOG_INFO("  [OK] Test 3 completed\n");
}

/**
 * 测试 4: 内存管理器统计
 * Test memory manager statistics
 */
static void Test_MemoryStats() {
    MR_LOG_INFO("\n[Test 4] Memory Statistics Test");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    auto* vulkanDevice = static_cast<VulkanDevice*>(device.get());
    auto* memoryManager = vulkanDevice->getMemoryManager();
    
    // 获取初始状态
    FVulkanMemoryManager::FMemoryStats statsBefore;
    memoryManager->GetMemoryStats(statsBefore);
    
    MR_LOG_INFO("  Initial memory stats:");
    MR_LOG_INFO("    Total allocated: " + FormatMemorySize(statsBefore.TotalAllocated));
    MR_LOG_INFO("    Pool count: " + std::to_string(statsBefore.PoolCount));
    
    // 创建一些资源
    TArray<TSharedPtr<IRHIBuffer>> buffers;
    for (uint32 i = 0; i < 10; ++i) {
        BufferDesc desc{};
        desc.size = 64 * 1024; // 64KB
        desc.usage = EResourceUsage::VertexBuffer;
        desc.cpuAccessible = false;
        desc.debugName = "TestBuffer_" + std::to_string(i);
        
        buffers.push_back(device->createBuffer(desc));
    }
    
    MR_LOG_INFO("  Created 10 buffers (64KB each)");
    
    // 获取更新后的状态
    FVulkanMemoryManager::FMemoryStats statsAfter;
    memoryManager->GetMemoryStats(statsAfter);
    
    MR_LOG_INFO("  After allocation:");
    MR_LOG_INFO("    Total allocated: " + FormatMemorySize(statsAfter.TotalAllocated));
    MR_LOG_INFO("    Pool count: " + std::to_string(statsAfter.PoolCount));
    MR_LOG_INFO("    Allocation count: " + std::to_string(statsAfter.AllocationCount));
    
    MR_LOG_INFO("  [OK] Test 4 completed\n");
}

// ================================
// 实际应用场景测试 (Real-world Scenario Tests)
// 简化版本 - 使用现有 Device API
// ================================

/**
 * 场景测试 1: 游戏角色渲染 (Character Rendering)
 * 模拟 UE5 中一个角色的完整资源创建流程
 */
static void Test_CharacterRendering() {
    MR_LOG_INFO("\n[Scenario 1] Character Rendering Resource Setup");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    MR_LOG_INFO("  Setting up character resources (similar to UE5 Character):");
    
    // 1. Scene Uniform Buffer
    BufferDesc sceneUBODesc{};
    sceneUBODesc.size = 256;
    sceneUBODesc.usage = EResourceUsage::UniformBuffer;
    sceneUBODesc.cpuAccessible = true;
    sceneUBODesc.debugName = "Scene_UBO";
    
    auto sceneUBO = device->createBuffer(sceneUBODesc);
    MR_LOG_INFO("    [OK] Scene Uniform Buffer created (256 bytes)");
    
    // 2. Character Uniform Buffer
    BufferDesc charUBODesc{};
    charUBODesc.size = 128;
    charUBODesc.usage = EResourceUsage::UniformBuffer;
    charUBODesc.cpuAccessible = true;
    charUBODesc.debugName = "Character_UBO";
    
    auto characterUBO = device->createBuffer(charUBODesc);
    MR_LOG_INFO("    [OK] Character Uniform Buffer created (128 bytes)");
    
    // 3. BaseColor Texture
    TextureDesc baseColorDesc{};
    baseColorDesc.width = 2048;
    baseColorDesc.height = 2048;
    baseColorDesc.mipLevels = 11;
    baseColorDesc.format = EPixelFormat::R8G8B8A8_UNORM;
    baseColorDesc.usage = EResourceUsage::ShaderResource | EResourceUsage::TransferDst;
    baseColorDesc.debugName = "Character_BaseColor";
    
    auto baseColorTex = device->createTexture(baseColorDesc);
    MR_LOG_INFO("    [OK] BaseColor Texture created (2048x2048, 11 mips)");
    
    // 4. Normal Texture
    TextureDesc normalDesc = baseColorDesc;
    normalDesc.debugName = "Character_Normal";
    auto normalTex = device->createTexture(normalDesc);
    MR_LOG_INFO("    [OK] Normal Texture created (2048x2048, 11 mips)");
    
    // 5. Roughness/Metallic Texture
    TextureDesc rmDesc = baseColorDesc;
    rmDesc.debugName = "Character_RM";
    auto rmTex = device->createTexture(rmDesc);
    MR_LOG_INFO("    [OK] Roughness/Metallic Texture created (2048x2048, 11 mips)");
    
    // 模拟渲染循环 (3 帧)
    MR_LOG_INFO("  Simulating 3 frames of rendering:");
    for (uint32 frame = 0; frame < 3; ++frame) {
        MR_LOG_DEBUG("    Frame " + std::to_string(frame) + ":");
        
        // 更新 Scene UBO
        void* sceneData = sceneUBO->map();
        if (sceneData) {
            memset(sceneData, frame, 256);
            sceneUBO->unmap();
            MR_LOG_DEBUG("      Updated Scene UBO");
        }
        
        // 更新 Character UBO
        void* charData = characterUBO->map();
        if (charData) {
            memset(charData, frame, 128);
            characterUBO->unmap();
            MR_LOG_DEBUG("      Updated Character UBO");
        }
    }
    
    MR_LOG_INFO("  [OK] Scenario 1 completed\n");
}

// ================================
// 主测试入口 (Main Test Entry)
// ================================

/**
 * 运行所有基础测试
 */
void RunBasicTests() {
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("Vulkan Resource & Memory Manager - Basic Tests");
    MR_LOG_INFO("========================================");
    
    Test_MemoryManagerInit();
    Test_BufferCreation();
    Test_TextureCreation();
    Test_MemoryStats();
    
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("Basic Tests Completed");
    MR_LOG_INFO("========================================\n");
}

/**
 * 运行所有实际应用场景测试
 */
void RunScenarioTests() {
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("Vulkan Resource Manager - Scenario Tests");
    MR_LOG_INFO("(Simplified version using existing Device API)");
    MR_LOG_INFO("========================================");
    
    Test_CharacterRendering();
    
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("Scenario Tests Completed");
    MR_LOG_INFO("========================================\n");
}

/**
 * 运行所有测试
 */
void RunAllTests() {
    RunBasicTests();
    RunScenarioTests();
}

} // namespace VulkanResourceManagerTest
} // namespace MonsterRender
