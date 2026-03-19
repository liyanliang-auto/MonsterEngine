// Copyright Monster Engine. All Rights Reserved.

#pragma once

namespace MonsterEngine {
namespace Tests {

/**
 * Run parallel rendering performance benchmarks
 * 
 * Compares single-threaded vs multi-threaded performance:
 * - Command recording time
 * - Translation time
 * - Scalability with different thread counts
 * - Memory usage
 */
void RunParallelRenderingBenchmarks();

} // namespace Tests
} // namespace MonsterEngine
