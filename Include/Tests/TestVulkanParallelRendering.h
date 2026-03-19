// Copyright Monster Engine. All Rights Reserved.

#pragma once

namespace MonsterEngine {
namespace Tests {

/**
 * Run Vulkan parallel rendering tests
 * 
 * Tests the integration of:
 * - FRHICommandListParallelTranslator
 * - FVulkanContextManager
 * - FVulkanCommandBufferPool
 * - Multi-threaded command recording
 */
void RunVulkanParallelRenderingTests();

} // namespace Tests
} // namespace MonsterEngine
