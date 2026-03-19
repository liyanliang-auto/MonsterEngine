// Copyright Monster Engine. All Rights Reserved.

#pragma once

namespace MonsterEngine {
namespace Tests {

/**
 * Run all parallel rendering tests
 * 
 * Executes the complete test suite:
 * - Command List Pool Tests
 * - Parallel Translator Tests
 * - Vulkan Parallel Rendering Tests
 * 
 * @return 0 if all tests passed, 1 otherwise
 */
int RunAllParallelRenderingTests();

} // namespace Tests
} // namespace MonsterEngine
