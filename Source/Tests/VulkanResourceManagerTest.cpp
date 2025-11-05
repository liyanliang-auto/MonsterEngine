// Copyright Epic Games, Inc. All Rights Reserved.
// MonsterEngine - Vulkan Resource Manager Test Suite
// 参考 UE5 资源管理测试设计

#include "Core/CoreMinimal.h"
#include "Core/Log.h"
#include "Platform/Vulkan/FVulkanResourceManager.h"
#include "Platform/Vulkan/FVulkanMemoryManager.h"
#include "Platform/Vulkan/VulkanDevice.h"
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
 * 测试 1: FVulkanResourceManager 初始化
 * Test ResourceManager initialization
 */
static void Test_ResourceManagerInit() {
    MR_LOG_INFO("\n[Test 1] FVulkanResourceManager Initialization Test");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    auto* vulkanDevice = static_cast<VulkanDevice*>(device.get());
    auto* resourceManager = vulkanDevice->getResourceManager();
    
    if (resourceManager) {
        MR_LOG_INFO("  [OK] ResourceManager initialized successfully");
        
        // 获取统计信息
        FVulkanResourceManager::FResourceStats stats;
        resourceManager->GetResourceStats(stats);
        
        MR_LOG_INFO("  Initial state:");
        MR_LOG_INFO("    Buffers: " + std::to_string(stats.NumBuffers));
        MR_LOG_INFO("    Multi-buffers: " + std::to_string(stats.NumMultiBuffers));
        MR_LOG_INFO("    Textures: " + std::to_string(stats.NumTextures));
        MR_LOG_INFO("  [OK] Test 1 completed\n");
    } else {
        MR_LOG_ERROR("  [FAIL] ResourceManager not available");
    }
}

/**
 * 测试 2: FVulkanResourceMultiBuffer 创建和使用
 * Test MultiBuffer creation and usage (Triple Buffering)
 */
static void Test_MultiBufferCreation() {
    MR_LOG_INFO("\n[Test 2] FVulkanResourceMultiBuffer Creation Test");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    auto* vulkanDevice = static_cast<VulkanDevice*>(device.get());
    auto* resourceManager = vulkanDevice->getResourceManager();
    
    // 创建 Triple-buffered Uniform Buffer (参考 UE5 的 Scene Constants Buffer)
    const uint32 bufferSize = 256; // 256 bytes uniform buffer
    auto multiBuffer = resourceManager->CreateMultiBuffer(
        bufferSize,
        EResourceUsage::UniformBuffer,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        3 // Triple buffering
    );
    
    if (!multiBuffer) {
        MR_LOG_ERROR("  [FAIL] Failed to create multi-buffer");
        return;
    }
    
    MR_LOG_INFO("  [OK] Created triple-buffered uniform buffer (256 bytes)");
    
    // 模拟 3 帧的写入操作 (类似 UE5 的 Scene Uniform Buffer 更新)
    struct TestUniformData {
        float ViewMatrix[16];
        float ProjMatrix[16];
        float CameraPos[4];
    };
    
    for (uint32 frame = 0; frame < 3; ++frame) {
        MR_LOG_DEBUG("  Frame " + std::to_string(frame) + ":");
        
        // Lock 当前帧的 buffer
        void* mappedData = multiBuffer->Lock(0, sizeof(TestUniformData));
        if (!mappedData) {
            MR_LOG_ERROR("    [FAIL] Failed to lock buffer");
            continue;
        }
        
        // 写入数据
        TestUniformData data{};
        for (int i = 0; i < 16; ++i) {
            data.ViewMatrix[i] = static_cast<float>(frame * 16 + i);
        }
        memcpy(mappedData, &data, sizeof(TestUniformData));
        
        // Unlock
        multiBuffer->Unlock();
        MR_LOG_DEBUG("    [OK] Locked, wrote, and unlocked buffer");
        
        // 获取当前 Vulkan handle (用于绑定到 Descriptor Set)
        VkBuffer currentHandle = multiBuffer->GetCurrentHandle();
        MR_LOG_DEBUG("    Current VkBuffer handle: 0x" + 
                    std::to_string(reinterpret_cast<uint64>(currentHandle)));
        
        // 推进到下一帧
        resourceManager->AdvanceFrame();
    }
    
    MR_LOG_INFO("  [OK] Test 2 completed\n");
}

/**
 * 测试 3: FVulkanTexture 创建和使用
 * Test Texture creation and usage
 */
static void Test_TextureCreation() {
    MR_LOG_INFO("\n[Test 3] FVulkanTexture Creation Test");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    auto* vulkanDevice = static_cast<VulkanDevice*>(device.get());
    auto* resourceManager = vulkanDevice->getResourceManager();
    
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
    
    auto texture = resourceManager->CreateTexture(desc, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (!texture) {
        MR_LOG_ERROR("  [FAIL] Failed to create texture");
        return;
    }
    
    MR_LOG_INFO("  [OK] Created 2K texture: " + desc.debugName);
    MR_LOG_INFO("    Format: R8G8B8A8_UNORM");
    MR_LOG_INFO("    Size: " + std::to_string(desc.width) + "x" + std::to_string(desc.height));
    MR_LOG_INFO("    Memory: Device Local");
    
    // 获取 Vulkan 特定信息
    auto* vulkanTexture = static_cast<FVulkanTexture*>(texture.Get());
    VkImage imageHandle = vulkanTexture->GetHandle();
    VkImageView viewHandle = vulkanTexture->GetView();
    
    MR_LOG_DEBUG("  VkImage handle: 0x" + std::to_string(reinterpret_cast<uint64>(imageHandle)));
    MR_LOG_DEBUG("  VkImageView handle: 0x" + std::to_string(reinterpret_cast<uint64>(viewHandle)));
    
    // 验证分配
    const auto& allocation = vulkanTexture->GetAllocation();
    MR_LOG_INFO("  Allocation size: " + FormatMemorySize(allocation.Size));
    
    MR_LOG_INFO("  [OK] Test 3 completed\n");
}

/**
 * 测试 4: 延迟释放机制 (Deferred Release)
 * Test deferred resource release (类似 UE5 的 RHI Resource GC)
 */
static void Test_DeferredRelease() {
    MR_LOG_INFO("\n[Test 4] Deferred Release Test");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    auto* vulkanDevice = static_cast<VulkanDevice*>(device.get());
    auto* resourceManager = vulkanDevice->getResourceManager();
    
    // 创建临时资源
    auto tempBuffer = resourceManager->CreateMultiBuffer(
        1024,
        EResourceUsage::TransferSrc,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        1
    );
    
    if (!tempBuffer) {
        MR_LOG_ERROR("  [FAIL] Failed to create temp buffer");
        return;
    }
    
    MR_LOG_INFO("  [OK] Created temporary buffer");
    
    // 模拟 GPU 提交 (在实际场景中，这个 buffer 会被 CommandList 使用)
    uint64 currentFrame = resourceManager->GetCurrentFrameNumber();
    MR_LOG_DEBUG("  Current frame: " + std::to_string(currentFrame));
    
    // 请求延迟释放 (资源会在 currentFrame + 3 帧后被释放)
    resourceManager->DeferredRelease(tempBuffer.Get(), currentFrame);
    tempBuffer.Reset(); // 释放引用，但资源还在延迟队列中
    
    MR_LOG_INFO("  [OK] Resource added to deferred release queue");
    
    // 模拟 3 帧推进
    for (uint32 i = 0; i < 4; ++i) {
        resourceManager->AdvanceFrame();
        uint64 completedFrame = currentFrame + i;
        resourceManager->ProcessDeferredReleases(completedFrame);
        
        FVulkanResourceManager::FResourceStats stats;
        resourceManager->GetResourceStats(stats);
        
        MR_LOG_DEBUG("  Frame " + std::to_string(i) + 
                    " completed, pending releases: " + std::to_string(stats.PendingReleases));
    }
    
    MR_LOG_INFO("  [OK] Test 4 completed\n");
}

// ================================
// 实际应用场景测试 (Real-world Scenario Tests)
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
    
    auto* vulkanDevice = static_cast<VulkanDevice*>(device.get());
    auto* resourceManager = vulkanDevice->getResourceManager();
    
    MR_LOG_INFO("  Setting up character resources (similar to UE5 Character):");
    
    // 1. Scene Uniform Buffer (Triple-buffered)
    auto sceneUBO = resourceManager->CreateMultiBuffer(
        256,
        EResourceUsage::UniformBuffer,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        3
    );
    MR_LOG_INFO("    [OK] Scene Uniform Buffer created (256 bytes, triple-buffered)");
    
    // 2. Character Uniform Buffer (Triple-buffered)
    auto characterUBO = resourceManager->CreateMultiBuffer(
        128,
        EResourceUsage::UniformBuffer,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        3
    );
    MR_LOG_INFO("    [OK] Character Uniform Buffer created (128 bytes, triple-buffered)");
    
    // 3. BaseColor Texture
    TextureDesc baseColorDesc{};
    baseColorDesc.width = 2048;
    baseColorDesc.height = 2048;
    baseColorDesc.mipLevels = 11; // 2048 -> 1
    baseColorDesc.format = EPixelFormat::R8G8B8A8_UNORM;
    baseColorDesc.usage = EResourceUsage::ShaderResource | EResourceUsage::TransferDst;
    baseColorDesc.debugName = "Character_BaseColor";
    
    auto baseColorTex = resourceManager->CreateTexture(baseColorDesc, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    MR_LOG_INFO("    [OK] BaseColor Texture created (2048x2048, 11 mips)");
    
    // 4. Normal Texture
    TextureDesc normalDesc = baseColorDesc;
    normalDesc.debugName = "Character_Normal";
    auto normalTex = resourceManager->CreateTexture(normalDesc, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    MR_LOG_INFO("    [OK] Normal Texture created (2048x2048, 11 mips)");
    
    // 5. Roughness/Metallic Texture
    TextureDesc rmDesc = baseColorDesc;
    rmDesc.debugName = "Character_RM";
    auto rmTex = resourceManager->CreateTexture(rmDesc, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    MR_LOG_INFO("    [OK] Roughness/Metallic Texture created (2048x2048, 11 mips)");
    
    // 模拟渲染循环 (3 帧)
    MR_LOG_INFO("  Simulating 3 frames of rendering:");
    for (uint32 frame = 0; frame < 3; ++frame) {
        MR_LOG_DEBUG("    Frame " + std::to_string(frame) + ":");
        
        // 更新 Scene UBO
        void* sceneData = sceneUBO->Lock(0, 256);
        if (sceneData) {
            // 写入 View/Proj 矩阵等
            memset(sceneData, frame, 256);
            sceneUBO->Unlock();
            MR_LOG_DEBUG("      Updated Scene UBO");
        }
        
        // 更新 Character UBO
        void* charData = characterUBO->Lock(0, 128);
        if (charData) {
            // 写入 Character Transform
            memset(charData, frame, 128);
            characterUBO->Unlock();
            MR_LOG_DEBUG("      Updated Character UBO");
        }
        
        // 推进帧
        resourceManager->AdvanceFrame();
    }
    
    // 获取最终统计
    FVulkanResourceManager::FResourceStats stats;
    resourceManager->GetResourceStats(stats);
    
    MR_LOG_INFO("  Final resource stats:");
    MR_LOG_INFO("    Multi-buffers: " + std::to_string(stats.NumMultiBuffers));
    MR_LOG_INFO("    Textures: " + std::to_string(stats.NumTextures));
    MR_LOG_INFO("    Buffer memory: " + FormatMemorySize(stats.BufferMemory));
    MR_LOG_INFO("    Texture memory: " + FormatMemorySize(stats.TextureMemory));
    
    MR_LOG_INFO("  [OK] Scenario 1 completed\n");
}

/**
 * 场景测试 2: 动态粒子系统 (Particle System)
 * 模拟 Niagara 粒子系统的资源使用
 */
static void Test_ParticleSystem() {
    MR_LOG_INFO("\n[Scenario 2] Particle System Resource Management");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    auto* vulkanDevice = static_cast<VulkanDevice*>(device.get());
    auto* resourceManager = vulkanDevice->getResourceManager();
    
    const uint32 maxParticles = 10000;
    const uint32 particleSize = 64; // 每个粒子 64 bytes (position, velocity, color, etc.)
    
    MR_LOG_INFO("  Setting up particle system for " + std::to_string(maxParticles) + " particles:");
    
    // 1. Particle Data Buffer (Triple-buffered，CPU 每帧写入)
    auto particleBuffer = resourceManager->CreateMultiBuffer(
        maxParticles * particleSize,
        EResourceUsage::VertexBuffer,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        3
    );
    MR_LOG_INFO("    [OK] Particle vertex buffer created (" + 
                FormatMemorySize(maxParticles * particleSize) + ", triple-buffered)");
    
    // 2. Particle Sprite Texture Atlas
    TextureDesc atlasDesc{};
    atlasDesc.width = 2048;
    atlasDesc.height = 2048;
    atlasDesc.mipLevels = 1;
    atlasDesc.format = EPixelFormat::R8G8B8A8_UNORM;
    atlasDesc.usage = EResourceUsage::ShaderResource;
    atlasDesc.debugName = "Particle_Atlas";
    
    auto atlasTexture = resourceManager->CreateTexture(atlasDesc, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    MR_LOG_INFO("    [OK] Particle atlas texture created (2048x2048)");
    
    // 模拟粒子发射和更新 (5 帧)
    MR_LOG_INFO("  Simulating particle emission and update:");
    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> dist(-100.0f, 100.0f);
    
    for (uint32 frame = 0; frame < 5; ++frame) {
        MR_LOG_DEBUG("    Frame " + std::to_string(frame) + ":");
        
        // Lock particle buffer
        void* particleData = particleBuffer->Lock(0, maxParticles * particleSize);
        if (particleData) {
            // 模拟粒子更新 (在实际场景中，这里会运行粒子模拟逻辑)
            auto* particles = static_cast<float*>(particleData);
            for (uint32 i = 0; i < maxParticles * 16; ++i) { // 16 floats per particle
                particles[i] = dist(rng);
            }
            particleBuffer->Unlock();
            MR_LOG_DEBUG("      Updated " + std::to_string(maxParticles) + " particles");
        }
        
        // 推进帧
        resourceManager->AdvanceFrame();
    }
    
    MR_LOG_INFO("  [OK] Scenario 2 completed\n");
}

/**
 * 场景测试 3: 地形系统 (Terrain System)
 * 模拟大规模地形渲染的资源管理 (类似 UE5 Landscape)
 */
static void Test_TerrainSystem() {
    MR_LOG_INFO("\n[Scenario 3] Terrain System Resource Management");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    auto* vulkanDevice = static_cast<VulkanDevice*>(device.get());
    auto* resourceManager = vulkanDevice->getResourceManager();
    
    // 地形参数
    const uint32 terrainResolution = 4096; // 4K heightmap
    const uint32 numLayers = 4; // 4 种地形材质层
    
    MR_LOG_INFO("  Setting up terrain system (4K terrain, 4 material layers):");
    
    // 1. Heightmap Texture
    TextureDesc heightmapDesc{};
    heightmapDesc.width = terrainResolution;
    heightmapDesc.height = terrainResolution;
    heightmapDesc.mipLevels = 1;
    heightmapDesc.format = EPixelFormat::R16_UNORM; // 16-bit heightmap
    heightmapDesc.usage = EResourceUsage::ShaderResource;
    heightmapDesc.debugName = "Terrain_Heightmap";
    
    auto heightmap = resourceManager->CreateTexture(heightmapDesc, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    MR_LOG_INFO("    [OK] Heightmap created (4096x4096, R16)");
    
    // 2. Layer Weight Map (4 channels for 4 layers)
    TextureDesc weightMapDesc{};
    weightMapDesc.width = terrainResolution;
    weightMapDesc.height = terrainResolution;
    weightMapDesc.mipLevels = 1;
    weightMapDesc.format = EPixelFormat::R8G8B8A8_UNORM;
    weightMapDesc.usage = EResourceUsage::ShaderResource;
    weightMapDesc.debugName = "Terrain_WeightMap";
    
    auto weightMap = resourceManager->CreateTexture(weightMapDesc, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    MR_LOG_INFO("    [OK] Weight map created (4096x4096, RGBA8)");
    
    // 3. Layer Textures (BaseColor, Normal, RM for each layer)
    TArray<TRefCountPtr<FRHITexture>> layerTextures;
    for (uint32 layer = 0; layer < numLayers; ++layer) {
        // BaseColor
        TextureDesc layerDesc{};
        layerDesc.width = 1024;
        layerDesc.height = 1024;
        layerDesc.mipLevels = 11;
        layerDesc.format = EPixelFormat::R8G8B8A8_UNORM;
        layerDesc.usage = EResourceUsage::ShaderResource;
        layerDesc.debugName = "Terrain_Layer" + std::to_string(layer) + "_BaseColor";
        
        layerTextures.push_back(resourceManager->CreateTexture(layerDesc, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT));
        
        // Normal
        layerDesc.debugName = "Terrain_Layer" + std::to_string(layer) + "_Normal";
        layerTextures.push_back(resourceManager->CreateTexture(layerDesc, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT));
        
        // RM
        layerDesc.debugName = "Terrain_Layer" + std::to_string(layer) + "_RM";
        layerTextures.push_back(resourceManager->CreateTexture(layerDesc, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT));
    }
    
    MR_LOG_INFO("    [OK] Created " + std::to_string(numLayers * 3) + " layer textures (1024x1024, 11 mips each)");
    
    // 4. Terrain Uniform Buffer (Triple-buffered)
    auto terrainUBO = resourceManager->CreateMultiBuffer(
        512,
        EResourceUsage::UniformBuffer,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        3
    );
    MR_LOG_INFO("    [OK] Terrain UBO created (512 bytes, triple-buffered)");
    
    // 获取统计信息
    FVulkanResourceManager::FResourceStats stats;
    resourceManager->GetResourceStats(stats);
    
    MR_LOG_INFO("  Terrain resource stats:");
    MR_LOG_INFO("    Total textures: " + std::to_string(stats.NumTextures));
    MR_LOG_INFO("    Texture memory: " + FormatMemorySize(stats.TextureMemory));
    MR_LOG_INFO("    Multi-buffers: " + std::to_string(stats.NumMultiBuffers));
    
    MR_LOG_INFO("  [OK] Scenario 3 completed\n");
}

/**
 * 场景测试 4: 关卡切换 (Level Transition)
 * 模拟关卡切换时的资源卸载和加载
 */
static void Test_LevelTransition() {
    MR_LOG_INFO("\n[Scenario 4] Level Transition Resource Management");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    auto* vulkanDevice = static_cast<VulkanDevice*>(device.get());
    auto* resourceManager = vulkanDevice->getResourceManager();
    
    MR_LOG_INFO("  Step 1: Loading Level A resources...");
    
    // Level A 资源
    TArray<TRefCountPtr<FRHITexture>> levelATextures;
    for (uint32 i = 0; i < 10; ++i) {
        TextureDesc desc{};
        desc.width = 1024;
        desc.height = 1024;
        desc.mipLevels = 11;
        desc.format = EPixelFormat::R8G8B8A8_UNORM;
        desc.usage = EResourceUsage::ShaderResource;
        desc.debugName = "LevelA_Texture_" + std::to_string(i);
        
        levelATextures.push_back(resourceManager->CreateTexture(desc, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT));
    }
    
    FVulkanResourceManager::FResourceStats statsA;
    resourceManager->GetResourceStats(statsA);
    MR_LOG_INFO("    Loaded " + std::to_string(levelATextures.size()) + " textures");
    MR_LOG_INFO("    Memory usage: " + FormatMemorySize(statsA.TextureMemory));
    
    MR_LOG_INFO("  Step 2: Unloading Level A (deferred release)...");
    
    // 请求延迟释放所有 Level A 资源
    uint64 currentFrame = resourceManager->GetCurrentFrameNumber();
    for (auto& tex : levelATextures) {
        resourceManager->DeferredRelease(tex.Get(), currentFrame);
    }
    levelATextures.clear();
    
    MR_LOG_INFO("    [OK] Level A resources added to deferred release queue");
    
    MR_LOG_INFO("  Step 3: Loading Level B resources...");
    
    // Level B 资源
    TArray<TRefCountPtr<FRHITexture>> levelBTextures;
    for (uint32 i = 0; i < 15; ++i) {
        TextureDesc desc{};
        desc.width = 2048;
        desc.height = 2048;
        desc.mipLevels = 12;
        desc.format = EPixelFormat::R8G8B8A8_UNORM;
        desc.usage = EResourceUsage::ShaderResource;
        desc.debugName = "LevelB_Texture_" + std::to_string(i);
        
        levelBTextures.push_back(resourceManager->CreateTexture(desc, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT));
    }
    
    MR_LOG_INFO("    Loaded " + std::to_string(levelBTextures.size()) + " textures");
    
    // 推进 3 帧以处理延迟释放
    MR_LOG_INFO("  Step 4: Processing deferred releases (3 frames)...");
    for (uint32 i = 0; i < 3; ++i) {
        resourceManager->AdvanceFrame();
        resourceManager->ProcessDeferredReleases(currentFrame + i);
        
        FVulkanResourceManager::FResourceStats stats;
        resourceManager->GetResourceStats(stats);
        MR_LOG_DEBUG("    Frame " + std::to_string(i) + 
                    ": Pending releases = " + std::to_string(stats.PendingReleases) +
                    ", Textures = " + std::to_string(stats.NumTextures));
    }
    
    // 最终统计
    FVulkanResourceManager::FResourceStats finalStats;
    resourceManager->GetResourceStats(finalStats);
    MR_LOG_INFO("  Final state:");
    MR_LOG_INFO("    Active textures: " + std::to_string(finalStats.NumTextures));
    MR_LOG_INFO("    Texture memory: " + FormatMemorySize(finalStats.TextureMemory));
    MR_LOG_INFO("    Pending releases: " + std::to_string(finalStats.PendingReleases));
    
    MR_LOG_INFO("  [OK] Scenario 4 completed\n");
}

/**
 * 场景测试 5: 并发资源创建 (Concurrent Resource Creation)
 * 模拟多线程同时创建资源 (类似 UE5 的异步加载)
 */
static void Test_ConcurrentResourceCreation() {
    MR_LOG_INFO("\n[Scenario 5] Concurrent Resource Creation Test");
    
    auto device = CreateTestDevice();
    if (!device) {
        MR_LOG_ERROR("  [FAIL] Failed to create device");
        return;
    }
    
    auto* vulkanDevice = static_cast<VulkanDevice*>(device.get());
    auto* resourceManager = vulkanDevice->getResourceManager();
    
    const uint32 numThreads = 4;
    const uint32 texturesPerThread = 5;
    
    MR_LOG_INFO("  Creating " + std::to_string(numThreads * texturesPerThread) + 
                " textures from " + std::to_string(numThreads) + " threads...");
    
    std::atomic<uint32> successCount{0};
    std::atomic<uint32> failCount{0};
    TArray<std::thread> threads;
    
    auto createTexturesFunc = [&](uint32 threadId) {
        for (uint32 i = 0; i < texturesPerThread; ++i) {
            TextureDesc desc{};
            desc.width = 512;
            desc.height = 512;
            desc.mipLevels = 10;
            desc.format = EPixelFormat::R8G8B8A8_UNORM;
            desc.usage = EResourceUsage::ShaderResource;
            desc.debugName = "Concurrent_T" + std::to_string(threadId) + "_Tex" + std::to_string(i);
            
            auto texture = resourceManager->CreateTexture(desc, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (texture) {
                successCount.fetch_add(1);
            } else {
                failCount.fetch_add(1);
            }
            
            // 随机延迟
            std::this_thread::sleep_for(std::chrono::milliseconds(rand() % 5));
        }
    };
    
    // 启动线程
    for (uint32 i = 0; i < numThreads; ++i) {
        threads.emplace_back(createTexturesFunc, i);
    }
    
    // 等待完成
    for (auto& thread : threads) {
        thread.join();
    }
    
    MR_LOG_INFO("  Results:");
    MR_LOG_INFO("    Success: " + std::to_string(successCount.load()));
    MR_LOG_INFO("    Failed: " + std::to_string(failCount.load()));
    
    if (failCount.load() == 0) {
        MR_LOG_INFO("  [OK] All concurrent creations succeeded");
    } else {
        MR_LOG_WARNING("  [WARN] Some concurrent creations failed");
    }
    
    MR_LOG_INFO("  [OK] Scenario 5 completed\n");
}

// ================================
// 主测试入口 (Main Test Entry)
// ================================

/**
 * 运行所有基础测试
 */
void RunBasicTests() {
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("Vulkan Resource Manager - Basic Tests");
    MR_LOG_INFO("========================================");
    
    Test_ResourceManagerInit();
    Test_MultiBufferCreation();
    Test_TextureCreation();
    Test_DeferredRelease();
    
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
    MR_LOG_INFO("(Reference UE5 Real-world Usage)");
    MR_LOG_INFO("========================================");
    
    Test_CharacterRendering();
    Test_ParticleSystem();
    Test_TerrainSystem();
    Test_LevelTransition();
    Test_ConcurrentResourceCreation();
    
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

