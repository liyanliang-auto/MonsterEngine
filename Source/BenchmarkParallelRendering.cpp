// Copyright Monster Engine. All Rights Reserved.

#include "Tests/BenchmarkParallelRendering.h"
#include "Core/Log.h"
#include "Core/FTaskGraph.h"
#include "Core/FGraphEvent.h"
#include "RHI/FRHICommandListPool.h"
#include "RHI/FRHICommandListParallelTranslator.h"
#include "RHI/MockCommandList.h"
#include <chrono>
#include <thread>
#include <iomanip>
#include <sstream>

using namespace MonsterEngine::RHI;
using namespace MonsterRender::RHI;

namespace MonsterEngine {
namespace Tests {

/**
 * High-resolution timer for performance measurement
 */
class FPerfTimer {
public:
    FPerfTimer() : m_start(std::chrono::high_resolution_clock::now()) {}
    
    /**
     * Get elapsed time in milliseconds
     */
    double GetElapsedMs() const {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - m_start);
        return duration.count() / 1000.0;
    }
    
    /**
     * Reset the timer
     */
    void Reset() {
        m_start = std::chrono::high_resolution_clock::now();
    }
    
private:
    std::chrono::high_resolution_clock::time_point m_start;
};

/**
 * Benchmark configuration
 */
struct FBenchmarkConfig {
    uint32 numCommandLists = 4;
    uint32 drawCallsPerList = 100;
    uint32 numIterations = 10;
    uint32 threadCount = 4;
    String sceneName = "Default";
};

/**
 * Benchmark results
 */
struct FBenchmarkResults {
    double avgRecordingTimeMs = 0.0;
    double avgTranslationTimeMs = 0.0;
    double avgTotalTimeMs = 0.0;
    double minTimeMs = 0.0;
    double maxTimeMs = 0.0;
    uint32 totalDrawCalls = 0;
    
    /**
     * Calculate speedup compared to baseline
     */
    double GetSpeedup(const FBenchmarkResults& baseline) const {
        if (avgTotalTimeMs <= 0.0) return 0.0;
        return baseline.avgTotalTimeMs / avgTotalTimeMs;
    }
};

/**
 * Format time with appropriate unit
 */
String FormatTime(double ms) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << ms << " ms";
    return oss.str();
}

/**
 * Format speedup
 */
String FormatSpeedup(double speedup) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << speedup << "x";
    return oss.str();
}

/**
 * Simulate command recording work
 * 
 * @param cmdList Command list to record into
 * @param drawCalls Number of draw calls to simulate
 */
void SimulateCommandRecording(IRHICommandList* cmdList, uint32 drawCalls) {
    if (!cmdList) return;
    
    cmdList->begin();
    
    // Simulate draw calls
    for (uint32 i = 0; i < drawCalls; i++) {
        // Simulate some work (minimal to focus on system overhead)
        volatile int dummy = 0;
        for (int j = 0; j < 100; j++) {
            dummy += j;
        }
    }
    
    cmdList->end();
}

/**
 * Run single-threaded benchmark
 * 
 * @param config Benchmark configuration
 * @return Benchmark results
 */
FBenchmarkResults RunSingleThreadedBenchmark(const FBenchmarkConfig& config) {
    MR_LOG_INFO("Running single-threaded benchmark: " + config.sceneName);
    
    FBenchmarkResults results;
    results.totalDrawCalls = config.numCommandLists * config.drawCallsPerList;
    
    double totalTime = 0.0;
    double minTime = 1e9;
    double maxTime = 0.0;
    
    for (uint32 iter = 0; iter < config.numIterations; iter++) {
        FPerfTimer timer;
        
        FParallelCommandListSet parallelSet;
        
        // Allocate command lists
        TArray<IRHICommandList*> cmdLists;
        for (uint32 i = 0; i < config.numCommandLists; i++) {
            auto* cmdList = parallelSet.AllocateCommandList();
            if (cmdList) {
                cmdLists.push_back(cmdList);
            }
        }
        
        // Record commands sequentially
        for (size_t i = 0; i < cmdLists.size(); i++) {
            SimulateCommandRecording(cmdLists[i], config.drawCallsPerList);
        }
        
        // Submit (will use serial translation due to disabled parallel)
        parallelSet.Submit(ETranslatePriority::Disabled);
        parallelSet.Wait();
        
        double elapsed = timer.GetElapsedMs();
        totalTime += elapsed;
        minTime = std::min(minTime, elapsed);
        maxTime = std::max(maxTime, elapsed);
    }
    
    results.avgTotalTimeMs = totalTime / config.numIterations;
    results.minTimeMs = minTime;
    results.maxTimeMs = maxTime;
    
    MR_LOG_INFO("  Avg time: " + FormatTime(results.avgTotalTimeMs));
    MR_LOG_INFO("  Min time: " + FormatTime(results.minTimeMs));
    MR_LOG_INFO("  Max time: " + FormatTime(results.maxTimeMs));
    
    return results;
}

/**
 * Run multi-threaded benchmark
 * 
 * @param config Benchmark configuration
 * @return Benchmark results
 */
FBenchmarkResults RunMultiThreadedBenchmark(const FBenchmarkConfig& config) {
    MR_LOG_INFO("Running multi-threaded benchmark: " + config.sceneName + 
               " (" + std::to_string(config.threadCount) + " threads)");
    
    FBenchmarkResults results;
    results.totalDrawCalls = config.numCommandLists * config.drawCallsPerList;
    
    double totalTime = 0.0;
    double minTime = 1e9;
    double maxTime = 0.0;
    
    for (uint32 iter = 0; iter < config.numIterations; iter++) {
        FPerfTimer timer;
        
        FParallelCommandListSet parallelSet;
        
        // Allocate command lists
        TArray<IRHICommandList*> cmdLists;
        for (uint32 i = 0; i < config.numCommandLists; i++) {
            auto* cmdList = parallelSet.AllocateCommandList();
            if (cmdList) {
                cmdLists.push_back(cmdList);
            }
        }
        
        // Record commands in parallel
        TArray<FGraphEventRef> recordingEvents;
        for (size_t i = 0; i < cmdLists.size(); i++) {
            auto* cmdList = cmdLists[i];
            uint32 drawCalls = config.drawCallsPerList;
            
            FGraphEventRef event = FTaskGraph::QueueNamedTask(
                "RecordCommands_" + std::to_string(i),
                [cmdList, drawCalls]() {
                    SimulateCommandRecording(cmdList, drawCalls);
                },
                {}
            );
            
            recordingEvents.push_back(event);
        }
        
        // Wait for recording to complete
        for (auto& event : recordingEvents) {
            if (event) {
                event->Wait();
            }
        }
        
        // Submit for parallel translation
        parallelSet.Submit(ETranslatePriority::Normal);
        parallelSet.Wait();
        
        double elapsed = timer.GetElapsedMs();
        totalTime += elapsed;
        minTime = std::min(minTime, elapsed);
        maxTime = std::max(maxTime, elapsed);
    }
    
    results.avgTotalTimeMs = totalTime / config.numIterations;
    results.minTimeMs = minTime;
    results.maxTimeMs = maxTime;
    
    MR_LOG_INFO("  Avg time: " + FormatTime(results.avgTotalTimeMs));
    MR_LOG_INFO("  Min time: " + FormatTime(results.minTimeMs));
    MR_LOG_INFO("  Max time: " + FormatTime(results.maxTimeMs));
    
    return results;
}

/**
 * Print comparison table
 */
void PrintComparisonTable(const FBenchmarkConfig& config,
                         const FBenchmarkResults& singleThreaded,
                         const FBenchmarkResults& multiThreaded) {
    MR_LOG_INFO("");
    MR_LOG_INFO("=== Performance Comparison ===");
    MR_LOG_INFO("Scene: " + config.sceneName);
    MR_LOG_INFO("Command Lists: " + std::to_string(config.numCommandLists));
    MR_LOG_INFO("Draw Calls/List: " + std::to_string(config.drawCallsPerList));
    MR_LOG_INFO("Total Draw Calls: " + std::to_string(singleThreaded.totalDrawCalls));
    MR_LOG_INFO("");
    
    MR_LOG_INFO("Single-threaded:");
    MR_LOG_INFO("  Average: " + FormatTime(singleThreaded.avgTotalTimeMs));
    MR_LOG_INFO("  Min:     " + FormatTime(singleThreaded.minTimeMs));
    MR_LOG_INFO("  Max:     " + FormatTime(singleThreaded.maxTimeMs));
    MR_LOG_INFO("");
    
    MR_LOG_INFO("Multi-threaded (" + std::to_string(config.threadCount) + " threads):");
    MR_LOG_INFO("  Average: " + FormatTime(multiThreaded.avgTotalTimeMs));
    MR_LOG_INFO("  Min:     " + FormatTime(multiThreaded.minTimeMs));
    MR_LOG_INFO("  Max:     " + FormatTime(multiThreaded.maxTimeMs));
    MR_LOG_INFO("");
    
    double speedup = multiThreaded.GetSpeedup(singleThreaded);
    MR_LOG_INFO("Speedup: " + FormatSpeedup(speedup));
    
    if (speedup > 1.0) {
        double improvement = (speedup - 1.0) * 100.0;
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << improvement << "%";
        MR_LOG_INFO("Performance improvement: " + oss.str());
    }
    
    MR_LOG_INFO("");
}

/**
 * Benchmark 1: Simple scene (low draw call count)
 */
void BenchmarkSimpleScene() {
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("Benchmark 1: Simple Scene");
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("");
    
    FBenchmarkConfig config;
    config.sceneName = "Simple Scene";
    config.numCommandLists = 4;
    config.drawCallsPerList = 50;
    config.numIterations = 10;
    config.threadCount = 4;
    
    auto singleThreaded = RunSingleThreadedBenchmark(config);
    auto multiThreaded = RunMultiThreadedBenchmark(config);
    
    PrintComparisonTable(config, singleThreaded, multiThreaded);
}

/**
 * Benchmark 2: Medium scene (moderate draw call count)
 */
void BenchmarkMediumScene() {
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("Benchmark 2: Medium Scene");
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("");
    
    FBenchmarkConfig config;
    config.sceneName = "Medium Scene";
    config.numCommandLists = 8;
    config.drawCallsPerList = 200;
    config.numIterations = 10;
    config.threadCount = 4;
    
    auto singleThreaded = RunSingleThreadedBenchmark(config);
    auto multiThreaded = RunMultiThreadedBenchmark(config);
    
    PrintComparisonTable(config, singleThreaded, multiThreaded);
}

/**
 * Benchmark 3: Complex scene (high draw call count)
 */
void BenchmarkComplexScene() {
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("Benchmark 3: Complex Scene");
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("");
    
    FBenchmarkConfig config;
    config.sceneName = "Complex Scene";
    config.numCommandLists = 16;
    config.drawCallsPerList = 500;
    config.numIterations = 5;
    config.threadCount = 4;
    
    auto singleThreaded = RunSingleThreadedBenchmark(config);
    auto multiThreaded = RunMultiThreadedBenchmark(config);
    
    PrintComparisonTable(config, singleThreaded, multiThreaded);
}

/**
 * Benchmark 4: Thread scalability test
 */
void BenchmarkThreadScalability() {
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("Benchmark 4: Thread Scalability");
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("");
    
    FBenchmarkConfig baseConfig;
    baseConfig.sceneName = "Scalability Test";
    baseConfig.numCommandLists = 8;
    baseConfig.drawCallsPerList = 200;
    baseConfig.numIterations = 10;
    
    // Run single-threaded baseline
    baseConfig.threadCount = 1;
    auto baseline = RunSingleThreadedBenchmark(baseConfig);
    
    MR_LOG_INFO("");
    MR_LOG_INFO("Thread Scalability Results:");
    MR_LOG_INFO("Threads | Avg Time | Speedup");
    MR_LOG_INFO("--------|----------|--------");
    
    std::ostringstream oss;
    oss << std::setw(7) << "1" << " | "
        << std::setw(8) << FormatTime(baseline.avgTotalTimeMs) << " | "
        << std::setw(6) << "1.00x";
    MR_LOG_INFO(oss.str());
    
    // Test with different thread counts
    uint32 threadCounts[] = {2, 4, 8};
    for (uint32 threadCount : threadCounts) {
        // Reinitialize task graph with new thread count
        if (FTaskGraph::IsInitialized()) {
            FTaskGraph::Shutdown();
        }
        FTaskGraph::Initialize(threadCount);
        
        baseConfig.threadCount = threadCount;
        auto results = RunMultiThreadedBenchmark(baseConfig);
        
        double speedup = results.GetSpeedup(baseline);
        
        std::ostringstream oss2;
        oss2 << std::setw(7) << threadCount << " | "
             << std::setw(8) << FormatTime(results.avgTotalTimeMs) << " | "
             << std::setw(6) << FormatSpeedup(speedup);
        MR_LOG_INFO(oss2.str());
    }
    
    // Restore default thread count
    if (FTaskGraph::IsInitialized()) {
        FTaskGraph::Shutdown();
    }
    FTaskGraph::Initialize(4);
    
    MR_LOG_INFO("");
}

/**
 * Main benchmark entry point
 */
void RunParallelRenderingBenchmarks() {
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("Parallel Rendering Performance Benchmarks");
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("");
    
    // Initialize systems
    if (!FTaskGraph::IsInitialized()) {
        FTaskGraph::Initialize(4);
    }
    
    if (!FRHICommandListPool::IsInitialized()) {
        FRHICommandListPool::Initialize(20);
    }
    
    if (!FRHICommandListParallelTranslator::IsInitialized()) {
        FRHICommandListParallelTranslator::Initialize(true, 100);
    }
    
    try {
        // Run benchmarks
        BenchmarkSimpleScene();
        BenchmarkMediumScene();
        BenchmarkComplexScene();
        BenchmarkThreadScalability();
        
        MR_LOG_INFO("========================================");
        MR_LOG_INFO("All benchmarks completed successfully!");
        MR_LOG_INFO("========================================");
        
    } catch (const std::exception& e) {
        MR_LOG_ERROR("Benchmark failed with exception: " + String(e.what()));
    }
    
    // Cleanup
    if (FRHICommandListParallelTranslator::IsInitialized()) {
        FRHICommandListParallelTranslator::Shutdown();
    }
    
    if (FRHICommandListPool::IsInitialized()) {
        FRHICommandListPool::Shutdown();
    }
    
    if (FTaskGraph::IsInitialized()) {
        FTaskGraph::Shutdown();
    }
}

} // namespace Tests
} // namespace MonsterEngine
