// Copyright Monster Engine. All Rights Reserved.
// TestParallelRendering - Header for parallel rendering tests
//
// This header declares the parallel rendering test functions that can be
// called from the main application or test harness.

#pragma once

#include "Core/CoreMinimal.h"

// Forward declarations
namespace MonsterRender::RHI::Vulkan {
    class VulkanDevice;
}

/**
 * Configuration for parallel rendering tests
 */
struct FParallelRenderingTestConfig {
    unsigned int numParallelLists = 4;        // Number of parallel command lists
    unsigned int drawCallsPerList = 50;       // Draw calls per command list
    unsigned int numFrames = 1;               // Number of frames to render
    bool enableTiming = true;                 // Enable performance timing
    bool enableValidation = true;             // Enable validation checks
    bool verbose = false;                     // Enable verbose logging
    
    FParallelRenderingTestConfig() = default;
};

/**
 * Run parallel rendering test with custom configuration
 * 
 * @param device Vulkan device to use for rendering
 * @param config Test configuration
 * @return True if test passed, false otherwise
 */
bool RunParallelRenderingTest(
    MonsterRender::RHI::Vulkan::VulkanDevice* device,
    const FParallelRenderingTestConfig& config
);

/**
 * Run parallel rendering test with default configuration
 * 
 * @param device Vulkan device to use for rendering
 * @return True if test passed, false otherwise
 */
bool RunParallelRenderingTest(MonsterRender::RHI::Vulkan::VulkanDevice* device);

/**
 * Run parallel command buffer execution test
 * Tests the complete parallel translation and execution pipeline
 * 
 * @param device Vulkan device to use for rendering
 * @return True if test passed, false otherwise
 */
bool TestParallelCommandBufferExecution(MonsterRender::RHI::Vulkan::VulkanDevice* device);

/**
 * Run parallel rendering integration example
 * Demonstrates complete frame rendering with parallel translation
 * 
 * @param device Vulkan device to use for rendering
 * @return True if test passed, false otherwise
 */
bool RunParallelRenderingIntegrationExample(MonsterRender::RHI::Vulkan::VulkanDevice* device);

/**
 * Run multi-frame parallel rendering example
 * Tests resource recycling and frame pipelining
 * 
 * @param device Vulkan device to use for rendering
 * @param numFrames Number of frames to render
 * @return True if test passed, false otherwise
 */
bool RunMultiFrameParallelRenderingExample(
    MonsterRender::RHI::Vulkan::VulkanDevice* device,
    unsigned int numFrames = 3
);
