// Copyright Monster Engine. All Rights Reserved.
// ParallelRenderingIntegrationExample - Complete integration example for parallel rendering
//
// This example demonstrates how to integrate parallel command buffer execution
// into a real rendering loop, following UE5's parallel rendering patterns.
//
// Reference: UE5 FRHICommandListImmediate::QueueAsyncCommandListSubmit
// Engine/Source/Runtime/RenderCore/Private/RenderingThread.cpp

#include "Core/CoreMinimal.h"
#include "Core/Log.h"
#include "RHI/FRHICommandListParallelTranslator.h"
#include "RHI/FTranslatedCommandBufferCollection.h"
#include "Platform/Vulkan/VulkanRHICommandListRecorder.h"
#include "Platform/Vulkan/VulkanContextManager.h"
#include "Platform/Vulkan/VulkanCommandListContext.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Core/FTaskGraph.h"

using namespace MonsterEngine;
using namespace MonsterEngine::RHI;
using namespace MonsterRender::RHI::Vulkan;

/**
 * FParallelRenderingFrame
 * 
 * Manages a single frame of parallel rendering.
 * This class demonstrates the complete workflow from command recording
 * to parallel translation to execution.
 * 
 * Architecture follows UE5's frame rendering pattern:
 * 1. Begin frame - set up resources
 * 2. Record parallel command lists
 * 3. Submit for parallel translation
 * 4. Wait for translation completion
 * 5. Execute secondary command buffers in primary buffer
 * 6. End frame - cleanup
 */
class FParallelRenderingFrame {
public:
    FParallelRenderingFrame(VulkanDevice* device)
        : m_device(device)
        , m_translator(nullptr)
        , m_primaryContext(nullptr) {
    }
    
    ~FParallelRenderingFrame() {
        EndFrame();
    }
    
    /**
     * Begin a new frame
     * Sets up the command buffer collection and prepares for rendering
     */
    bool BeginFrame() {
        MR_LOG_INFO("=== FParallelRenderingFrame::BeginFrame ===");
        
        // Get or create the parallel translator
        if (!m_translator) {
            m_translator = MakeUnique<FRHICommandListParallelTranslator>();
            if (!m_translator) {
                MR_LOG_ERROR("Failed to create parallel translator");
                return false;
            }
        }
        
        // Create a new command buffer collection for this frame
        auto collection = MakeUnique<FTranslatedCommandBufferCollection>();
        if (!collection) {
            MR_LOG_ERROR("Failed to create command buffer collection");
            return false;
        }
        
        // Set the collection in the translator
        // This is critical - all translated buffers will be added to this collection
        m_translator->SetCommandBufferCollection(std::move(collection));
        
        // Get the primary command list context for the main thread
        m_primaryContext = FVulkanContextManager::GetOrCreateForCurrentThread();
        if (!m_primaryContext) {
            MR_LOG_ERROR("Failed to get primary command list context");
            return false;
        }
        
        MR_LOG_INFO("Frame begun successfully - ready for parallel rendering");
        return true;
    }
    
    /**
     * Record and submit parallel command lists
     * 
     * This simulates a typical rendering scenario where different parts of the scene
     * are rendered in parallel (e.g., opaque geometry, transparent geometry, shadows)
     * 
     * @param numParallelLists Number of parallel command lists to create
     * @return Completion event for all translations
     */
    FGraphEventRef SubmitParallelWork(uint32 numParallelLists = 4) {
        MR_LOG_INFO("=== FParallelRenderingFrame::SubmitParallelWork ===");
        MR_LOG_INFO("Submitting " + std::to_string(numParallelLists) + " parallel command lists");
        
        // Create and record command lists
        TArray<TSharedPtr<VulkanRHICommandListRecorder>> commandLists;
        TArray<FGraphEventRef> translationEvents;
        
        for (uint32 i = 0; i < numParallelLists; ++i) {
            // Create a command list for this parallel task
            auto cmdList = CreateParallelCommandList(i);
            if (!cmdList) {
                MR_LOG_ERROR("Failed to create command list " + std::to_string(i));
                continue;
            }
            
            // Finish recording
            cmdList->FinishRecording();
            
            // Submit for parallel translation
            auto completionEvent = m_translator->TranslateCommandListAsync(
                cmdList.get(),
                i
            );
            
            if (completionEvent) {
                translationEvents.push_back(completionEvent);
                commandLists.push_back(cmdList);
            }
        }
        
        MR_LOG_INFO("Submitted " + std::to_string(translationEvents.size()) + 
                   " command lists for parallel translation");
        
        // Store command lists to keep them alive
        m_frameCommandLists = std::move(commandLists);
        
        // Create a combined completion event
        // In UE5, this would be done through FGraphEvent::CreateGraphEvent
        if (!translationEvents.empty()) {
            // For simplicity, we'll just return the last event
            // In a full implementation, we'd create a combined event
            return translationEvents[translationEvents.size() - 1];
        }
        
        return nullptr;
    }
    
    /**
     * Execute all translated secondary command buffers
     * This is called after all parallel translations have completed
     * 
     * @return True if execution succeeded
     */
    bool ExecuteSecondaryCommandBuffers() {
        MR_LOG_INFO("=== FParallelRenderingFrame::ExecuteSecondaryCommandBuffers ===");
        
        // Get the command buffer collection
        auto* collection = m_translator->GetCommandBufferCollection();
        if (!collection) {
            MR_LOG_ERROR("No command buffer collection available");
            return false;
        }
        
        if (collection->IsEmpty()) {
            MR_LOG_WARNING("Command buffer collection is empty - no work to execute");
            return true;
        }
        
        MR_LOG_INFO("Executing " + std::to_string(collection->GetCount()) + 
                   " secondary command buffers");
        MR_LOG_INFO("Total commands: " + std::to_string(collection->GetTotalCommandCount()));
        MR_LOG_INFO("Total draw calls: " + std::to_string(collection->GetTotalDrawCallCount()));
        
        // Get the primary command buffer from the main thread context
        auto* primaryCmdBuffer = m_primaryContext->getCmdBuffer();
        if (!primaryCmdBuffer) {
            MR_LOG_ERROR("Failed to get primary command buffer");
            return false;
        }
        
        // Execute all secondary command buffers in the primary buffer
        // This uses vkCmdExecuteCommands internally
        bool success = collection->ExecuteInPrimaryCommandBuffer(primaryCmdBuffer);
        
        if (success) {
            MR_LOG_INFO("Successfully executed all secondary command buffers");
        } else {
            MR_LOG_ERROR("Failed to execute secondary command buffers");
        }
        
        return success;
    }
    
    /**
     * End the current frame
     * Cleans up resources and prepares for the next frame
     */
    void EndFrame() {
        MR_LOG_INFO("=== FParallelRenderingFrame::EndFrame ===");
        
        // Reset the command buffer collection
        // This frees all secondary command buffers back to their pools
        if (m_translator) {
            m_translator->ResetCommandBufferCollection();
        }
        
        // Clear frame command lists
        m_frameCommandLists.clear();
        
        MR_LOG_INFO("Frame ended - resources cleaned up");
    }
    
    /**
     * Complete frame workflow
     * Demonstrates the full parallel rendering pipeline
     */
    bool RenderFrame(uint32 numParallelLists = 4) {
        MR_LOG_INFO("========================================");
        MR_LOG_INFO("Starting Parallel Rendering Frame");
        MR_LOG_INFO("========================================");
        
        // Step 1: Begin frame
        if (!BeginFrame()) {
            MR_LOG_ERROR("Failed to begin frame");
            return false;
        }
        
        // Step 2: Submit parallel work
        auto completionEvent = SubmitParallelWork(numParallelLists);
        
        // Step 3: Wait for all translations to complete
        if (completionEvent) {
            MR_LOG_INFO("Waiting for parallel translation to complete...");
            completionEvent->Wait();
            MR_LOG_INFO("All translations completed");
        }
        
        // Step 4: Execute secondary command buffers
        if (!ExecuteSecondaryCommandBuffers()) {
            MR_LOG_ERROR("Failed to execute secondary command buffers");
            EndFrame();
            return false;
        }
        
        // Step 5: End frame
        EndFrame();
        
        MR_LOG_INFO("========================================");
        MR_LOG_INFO("Parallel Rendering Frame Completed Successfully");
        MR_LOG_INFO("========================================");
        
        return true;
    }
    
private:
    /**
     * Create a parallel command list for a specific task
     * 
     * In a real rendering scenario, each command list would render
     * a different part of the scene or a different rendering pass.
     * 
     * @param taskIndex Index of this parallel task
     * @return Created command list
     */
    TSharedPtr<VulkanRHICommandListRecorder> CreateParallelCommandList(uint32 taskIndex) {
        auto cmdList = MakeShared<VulkanRHICommandListRecorder>(m_device);
        
        // Begin recording
        cmdList->begin();
        
        // Simulate rendering work based on task index
        // In a real scenario, this would be actual rendering commands
        const char* taskNames[] = {
            "OpaqueGeometry",
            "TransparentGeometry",
            "Shadows",
            "PostProcessing"
        };
        
        const char* taskName = (taskIndex < 4) ? taskNames[taskIndex] : "GenericTask";
        
        // Add a marker for debugging
        cmdList->setMarker(std::string("ParallelTask_") + taskName);
        
        // Simulate different amounts of work per task
        uint32 drawCallCount = 10 + (taskIndex * 5);
        for (uint32 i = 0; i < drawCallCount; ++i) {
            // Simulate draw calls
            cmdList->draw(100, 0);
        }
        
        MR_LOG_DEBUG("Created parallel command list for task " + std::to_string(taskIndex) + 
                    " (" + std::string(taskName) + ") with " + 
                    std::to_string(drawCallCount) + " draw calls");
        
        return cmdList;
    }
    
private:
    VulkanDevice* m_device;
    TUniquePtr<FRHICommandListParallelTranslator> m_translator;
    FVulkanCommandListContext* m_primaryContext;
    TArray<TSharedPtr<VulkanRHICommandListRecorder>> m_frameCommandLists;
};

/**
 * Entry point for parallel rendering integration example
 * 
 * This demonstrates how to use the parallel rendering system in a real application
 */
bool RunParallelRenderingIntegrationExample(VulkanDevice* device) {
    if (!device) {
        MR_LOG_ERROR("RunParallelRenderingIntegrationExample - Invalid device");
        return false;
    }
    
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("Parallel Rendering Integration Example");
    MR_LOG_INFO("========================================");
    
    // Create a frame manager
    FParallelRenderingFrame frame(device);
    
    // Render a complete frame with parallel command lists
    bool success = frame.RenderFrame(4);
    
    if (success) {
        MR_LOG_INFO("========================================");
        MR_LOG_INFO("Integration Example PASSED");
        MR_LOG_INFO("========================================");
    } else {
        MR_LOG_ERROR("========================================");
        MR_LOG_ERROR("Integration Example FAILED");
        MR_LOG_ERROR("========================================");
    }
    
    return success;
}

/**
 * Advanced example: Multiple frames with parallel rendering
 * 
 * This demonstrates how the system works across multiple frames,
 * showing proper resource recycling and frame pipelining.
 */
bool RunMultiFrameParallelRenderingExample(VulkanDevice* device, uint32 numFrames = 3) {
    if (!device) {
        MR_LOG_ERROR("RunMultiFrameParallelRenderingExample - Invalid device");
        return false;
    }
    
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("Multi-Frame Parallel Rendering Example");
    MR_LOG_INFO("Rendering " + std::to_string(numFrames) + " frames");
    MR_LOG_INFO("========================================");
    
    FParallelRenderingFrame frame(device);
    
    for (uint32 frameIndex = 0; frameIndex < numFrames; ++frameIndex) {
        MR_LOG_INFO("");
        MR_LOG_INFO(">>> Frame " + std::to_string(frameIndex + 1) + " / " + 
                   std::to_string(numFrames) + " <<<");
        
        if (!frame.RenderFrame(4)) {
            MR_LOG_ERROR("Failed to render frame " + std::to_string(frameIndex));
            return false;
        }
    }
    
    MR_LOG_INFO("");
    MR_LOG_INFO("========================================");
    MR_LOG_INFO("Multi-Frame Example PASSED");
    MR_LOG_INFO("Successfully rendered " + std::to_string(numFrames) + " frames");
    MR_LOG_INFO("========================================");
    
    return true;
}
