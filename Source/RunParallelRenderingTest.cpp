// Copyright Monster Engine. All Rights Reserved.
// RunParallelRenderingTest - Runnable test program for parallel rendering system
//
// This is a complete, runnable test that demonstrates the entire parallel rendering
// pipeline from command recording to execution.
//
// Reference: UE5 rendering tests
// Engine/Source/Runtime/RenderCore/Private/Tests/RenderingTests.cpp

#include "Core/CoreMinimal.h"
#include "Core/Log.h"
#include "Core/FTaskGraph.h"
#include "RHI/FRHICommandListParallelTranslator.h"
#include "RHI/FTranslatedCommandBufferCollection.h"
#include "Platform/Vulkan/VulkanRHICommandListRecorder.h"
#include "Platform/Vulkan/VulkanContextManager.h"
#include "Platform/Vulkan/VulkanCommandListContext.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanRHI.h"
#include "Tests/TestParallelRendering.h"
#include <chrono>

using namespace MonsterEngine;
using namespace MonsterRender::RHI;
using namespace MonsterRender::RHI::Vulkan;

/**
 * FParallelRenderingTestStats
 * 
 * Statistics collected during parallel rendering test
 */
struct FParallelRenderingTestStats {
    // Timing
    double totalTimeMs = 0.0;
    double recordingTimeMs = 0.0;
    double translationTimeMs = 0.0;
    double executionTimeMs = 0.0;
    
    // Counts
    uint32 totalCommandLists = 0;
    uint32 totalCommands = 0;
    uint32 totalDrawCalls = 0;
    uint32 secondaryBuffersExecuted = 0;
    
    // Performance
    double commandsPerSecond = 0.0;
    double drawCallsPerSecond = 0.0;
    
    void CalculatePerformanceMetrics() {
        if (totalTimeMs > 0.0) {
            commandsPerSecond = (totalCommands * 1000.0) / totalTimeMs;
            drawCallsPerSecond = (totalDrawCalls * 1000.0) / totalTimeMs;
        }
    }
    
    void Print() const {
        MR_LOG_INFO("=== Parallel Rendering Test Statistics ===");
        MR_LOG_INFO("Timing:");
        MR_LOG_INFO("  Total Time:       " + std::to_string(totalTimeMs) + " ms");
        MR_LOG_INFO("  Recording Time:   " + std::to_string(recordingTimeMs) + " ms");
        MR_LOG_INFO("  Translation Time: " + std::to_string(translationTimeMs) + " ms");
        MR_LOG_INFO("  Execution Time:   " + std::to_string(executionTimeMs) + " ms");
        MR_LOG_INFO("");
        MR_LOG_INFO("Counts:");
        MR_LOG_INFO("  Command Lists:    " + std::to_string(totalCommandLists));
        MR_LOG_INFO("  Total Commands:   " + std::to_string(totalCommands));
        MR_LOG_INFO("  Total Draw Calls: " + std::to_string(totalDrawCalls));
        MR_LOG_INFO("  Secondary Buffers: " + std::to_string(secondaryBuffersExecuted));
        MR_LOG_INFO("");
        MR_LOG_INFO("Performance:");
        MR_LOG_INFO("  Commands/sec:     " + std::to_string(static_cast<uint32>(commandsPerSecond)));
        MR_LOG_INFO("  Draw Calls/sec:   " + std::to_string(static_cast<uint32>(drawCallsPerSecond)));
        MR_LOG_INFO("==========================================");
    }
};

/**
 * FParallelRenderingTest
 * 
 * Main test class for parallel rendering
 * Manages the complete test lifecycle and collects statistics
 */
class FParallelRenderingTest {
public:
    FParallelRenderingTest(VulkanDevice* device, const FParallelRenderingTestConfig& config)
        : m_device(device)
        , m_config(config)
        , m_translator(nullptr)
        , m_primaryContext(nullptr) {
    }
    
    ~FParallelRenderingTest() {
        Cleanup();
    }
    
    /**
     * Initialize the test
     */
    bool Initialize() {
        MR_LOG_INFO("=== Initializing Parallel Rendering Test ===");
        
        // Validate device
        if (!m_device) {
            MR_LOG_ERROR("Invalid Vulkan device");
            return false;
        }
        
        // Create parallel translator
        m_translator = MakeUnique<FRHICommandListParallelTranslator>();
        if (!m_translator) {
            MR_LOG_ERROR("Failed to create parallel translator");
            return false;
        }
        
        // Get primary context
        m_primaryContext = FVulkanContextManager::GetOrCreateForCurrentThread();
        if (!m_primaryContext) {
            MR_LOG_ERROR("Failed to get primary command list context");
            return false;
        }
        
        MR_LOG_INFO("Test initialized successfully");
        MR_LOG_INFO("Configuration:");
        MR_LOG_INFO("  Parallel Lists:   " + std::to_string(m_config.numParallelLists));
        MR_LOG_INFO("  Draws per List:   " + std::to_string(m_config.drawCallsPerList));
        MR_LOG_INFO("  Frames:           " + std::to_string(m_config.numFrames));
        MR_LOG_INFO("  Timing Enabled:   " + std::string(m_config.enableTiming ? "Yes" : "No"));
        MR_LOG_INFO("  Validation:       " + std::string(m_config.enableValidation ? "Yes" : "No"));
        
        return true;
    }
    
    /**
     * Run the complete test
     */
    bool Run() {
        MR_LOG_INFO("");
        MR_LOG_INFO("========================================");
        MR_LOG_INFO("Starting Parallel Rendering Test");
        MR_LOG_INFO("========================================");
        
        auto startTime = std::chrono::high_resolution_clock::now();
        
        // Run multiple frames
        for (uint32 frameIndex = 0; frameIndex < m_config.numFrames; ++frameIndex) {
            if (m_config.numFrames > 1) {
                MR_LOG_INFO("");
                MR_LOG_INFO(">>> Frame " + std::to_string(frameIndex + 1) + " / " + 
                           std::to_string(m_config.numFrames) + " <<<");
            }
            
            if (!RunFrame(frameIndex)) {
                MR_LOG_ERROR("Frame " + std::to_string(frameIndex) + " failed");
                return false;
            }
        }
        
        auto endTime = std::chrono::high_resolution_clock::now();
        m_stats.totalTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
        
        // Calculate performance metrics
        m_stats.CalculatePerformanceMetrics();
        
        // Print statistics
        MR_LOG_INFO("");
        m_stats.Print();
        
        MR_LOG_INFO("");
        MR_LOG_INFO("========================================");
        MR_LOG_INFO("Parallel Rendering Test PASSED");
        MR_LOG_INFO("========================================");
        
        return true;
    }
    
    /**
     * Get test statistics
     */
    const FParallelRenderingTestStats& GetStats() const {
        return m_stats;
    }
    
private:
    /**
     * Run a single frame
     */
    bool RunFrame(uint32 frameIndex) {
        // Step 1: Begin frame
        if (!BeginFrame(frameIndex)) {
            return false;
        }
        
        // Step 2: Record command lists
        auto recordStartTime = std::chrono::high_resolution_clock::now();
        
        TArray<TSharedPtr<VulkanRHICommandListRecorder>> commandLists;
        if (!RecordCommandLists(commandLists)) {
            return false;
        }
        
        auto recordEndTime = std::chrono::high_resolution_clock::now();
        double recordTimeMs = std::chrono::duration<double, std::milli>(recordEndTime - recordStartTime).count();
        m_stats.recordingTimeMs += recordTimeMs;
        
        if (m_config.verbose) {
            MR_LOG_INFO("Command recording took " + std::to_string(recordTimeMs) + " ms");
        }
        
        // Step 3: Submit for parallel translation
        auto translateStartTime = std::chrono::high_resolution_clock::now();
        
        TArray<FGraphEventRef> translationEvents;
        if (!SubmitForTranslation(commandLists, translationEvents)) {
            return false;
        }
        
        // Step 4: Wait for translation completion
        WaitForTranslation(translationEvents);
        
        auto translateEndTime = std::chrono::high_resolution_clock::now();
        double translateTimeMs = std::chrono::duration<double, std::milli>(translateEndTime - translateStartTime).count();
        m_stats.translationTimeMs += translateTimeMs;
        
        if (m_config.verbose) {
            MR_LOG_INFO("Parallel translation took " + std::to_string(translateTimeMs) + " ms");
        }
        
        // Step 5: Execute secondary command buffers
        auto executeStartTime = std::chrono::high_resolution_clock::now();
        
        if (!ExecuteSecondaryBuffers()) {
            return false;
        }
        
        auto executeEndTime = std::chrono::high_resolution_clock::now();
        double executeTimeMs = std::chrono::duration<double, std::milli>(executeEndTime - executeStartTime).count();
        m_stats.executionTimeMs += executeTimeMs;
        
        if (m_config.verbose) {
            MR_LOG_INFO("Command execution took " + std::to_string(executeTimeMs) + " ms");
        }
        
        // Step 6: Validate results
        if (m_config.enableValidation) {
            if (!ValidateFrame()) {
                return false;
            }
        }
        
        // Step 7: End frame
        EndFrame();
        
        return true;
    }
    
    /**
     * Begin a new frame
     */
    bool BeginFrame(uint32 frameIndex) {
        if (m_config.verbose) {
            MR_LOG_INFO("BeginFrame " + std::to_string(frameIndex));
        }
        
        // Create command buffer collection for this frame
        auto collection = MakeUnique<FTranslatedCommandBufferCollection>();
        if (!collection) {
            MR_LOG_ERROR("Failed to create command buffer collection");
            return false;
        }
        
        // Set collection in translator
        m_translator->SetCommandBufferCollection(std::move(collection));
        
        return true;
    }
    
    /**
     * Record command lists
     */
    bool RecordCommandLists(TArray<TSharedPtr<VulkanRHICommandListRecorder>>& outCommandLists) {
        if (m_config.verbose) {
            MR_LOG_INFO("Recording " + std::to_string(m_config.numParallelLists) + " command lists");
        }
        
        outCommandLists.reserve(m_config.numParallelLists);
        
        for (uint32 i = 0; i < m_config.numParallelLists; ++i) {
            auto cmdList = CreateCommandList(i);
            if (!cmdList) {
                MR_LOG_ERROR("Failed to create command list " + std::to_string(i));
                return false;
            }
            
            // Finish recording
            cmdList->FinishRecording();
            
            // Update statistics
            m_stats.totalCommandLists++;
            m_stats.totalCommands += cmdList->GetCommandCount();
            m_stats.totalDrawCalls += cmdList->GetDrawCallCount();
            
            outCommandLists.push_back(cmdList);
        }
        
        return true;
    }
    
    /**
     * Create a single command list with test commands
     */
    TSharedPtr<VulkanRHICommandListRecorder> CreateCommandList(uint32 index) {
        auto cmdList = MakeShared<VulkanRHICommandListRecorder>(m_device);
        
        // Begin recording
        cmdList->begin();
        
        // Add marker for debugging
        cmdList->setMarker("ParallelCommandList_" + std::to_string(index));
        
        // Record draw calls
        for (uint32 i = 0; i < m_config.drawCallsPerList; ++i) {
            // Simulate rendering work
            cmdList->draw(100, 0);
        }
        
        if (m_config.verbose) {
            MR_LOG_DEBUG("Created command list " + std::to_string(index) + 
                        " with " + std::to_string(m_config.drawCallsPerList) + " draw calls");
        }
        
        return cmdList;
    }
    
    /**
     * Submit command lists for parallel translation
     */
    bool SubmitForTranslation(
        const TArray<TSharedPtr<VulkanRHICommandListRecorder>>& commandLists,
        TArray<FGraphEventRef>& outEvents) {
        
        if (m_config.verbose) {
            MR_LOG_INFO("Submitting " + std::to_string(commandLists.size()) + 
                       " command lists for parallel translation");
        }
        
        outEvents.reserve(commandLists.size());
        
        for (uint32 i = 0; i < commandLists.size(); ++i) {
            auto completionEvent = m_translator->TranslateCommandListAsync(
                commandLists[i].get(),
                i
            );
            
            if (completionEvent) {
                outEvents.push_back(completionEvent);
            }
        }
        
        return !outEvents.empty();
    }
    
    /**
     * Wait for all translation tasks to complete
     */
    void WaitForTranslation(const TArray<FGraphEventRef>& events) {
        if (m_config.verbose) {
            MR_LOG_INFO("Waiting for " + std::to_string(events.size()) + " translation tasks");
        }
        
        for (auto& event : events) {
            if (event) {
                event->Wait();
            }
        }
        
        if (m_config.verbose) {
            MR_LOG_INFO("All translation tasks completed");
        }
    }
    
    /**
     * Execute secondary command buffers
     */
    bool ExecuteSecondaryBuffers() {
        auto* collection = m_translator->GetCommandBufferCollection();
        if (!collection) {
            MR_LOG_ERROR("No command buffer collection available");
            return false;
        }
        
        if (collection->IsEmpty()) {
            MR_LOG_WARNING("Command buffer collection is empty");
            return false;
        }
        
        uint32 bufferCount = collection->GetCount();
        m_stats.secondaryBuffersExecuted += bufferCount;
        
        if (m_config.verbose) {
            MR_LOG_INFO("Executing " + std::to_string(bufferCount) + " secondary command buffers");
            MR_LOG_INFO("  Total commands: " + std::to_string(collection->GetTotalCommandCount()));
            MR_LOG_INFO("  Total draws:    " + std::to_string(collection->GetTotalDrawCallCount()));
        }
        
        // Get primary command buffer
        auto* primaryCmdBuffer = m_primaryContext->getCmdBuffer();
        if (!primaryCmdBuffer) {
            MR_LOG_ERROR("Failed to get primary command buffer");
            return false;
        }
        
        // Execute all secondary buffers
        bool success = collection->ExecuteInPrimaryCommandBuffer(primaryCmdBuffer);
        
        if (!success) {
            MR_LOG_ERROR("Failed to execute secondary command buffers");
            return false;
        }
        
        if (m_config.verbose) {
            MR_LOG_INFO("Successfully executed all secondary command buffers");
        }
        
        return true;
    }
    
    /**
     * Validate frame results
     */
    bool ValidateFrame() {
        auto* collection = m_translator->GetCommandBufferCollection();
        if (!collection) {
            MR_LOG_ERROR("Validation failed: No collection");
            return false;
        }
        
        // Verify collection has correct number of buffers
        if (collection->GetCount() != m_config.numParallelLists) {
            MR_LOG_ERROR("Validation failed: Expected " + 
                        std::to_string(m_config.numParallelLists) + 
                        " buffers, got " + std::to_string(collection->GetCount()));
            return false;
        }
        
        if (m_config.verbose) {
            MR_LOG_INFO("Frame validation passed");
        }
        
        return true;
    }
    
    /**
     * End the current frame
     */
    void EndFrame() {
        // Reset command buffer collection
        m_translator->ResetCommandBufferCollection();
        
        if (m_config.verbose) {
            MR_LOG_INFO("EndFrame - resources cleaned up");
        }
    }
    
    /**
     * Cleanup test resources
     */
    void Cleanup() {
        if (m_translator) {
            m_translator->ResetCommandBufferCollection();
        }
    }
    
private:
    VulkanDevice* m_device;
    FParallelRenderingTestConfig m_config;
    TUniquePtr<FRHICommandListParallelTranslator> m_translator;
    FVulkanCommandListContext* m_primaryContext;
    FParallelRenderingTestStats m_stats;
};

/**
 * Entry point for parallel rendering test
 * 
 * @param device Vulkan device
 * @param config Test configuration
 * @return True if test passed
 */
bool RunParallelRenderingTest(VulkanDevice* device, const FParallelRenderingTestConfig& config) {
    if (!device) {
        MR_LOG_ERROR("RunParallelRenderingTest - Invalid device");
        return false;
    }
    
    // Create and run test
    FParallelRenderingTest test(device, config);
    
    if (!test.Initialize()) {
        MR_LOG_ERROR("Test initialization failed");
        return false;
    }
    
    return test.Run();
}

/**
 * Entry point with default configuration
 */
bool RunParallelRenderingTest(VulkanDevice* device) {
    FParallelRenderingTestConfig config;
    config.numParallelLists = 4;
    config.drawCallsPerList = 50;
    config.numFrames = 1;
    config.enableTiming = true;
    config.enableValidation = true;
    config.verbose = true;
    
    return RunParallelRenderingTest(device, config);
}
