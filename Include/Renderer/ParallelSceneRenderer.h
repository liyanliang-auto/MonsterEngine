// Copyright Monster Engine. All Rights Reserved.
#pragma once

/**
 * @file ParallelSceneRenderer.h
 * @brief Parallel scene renderer - UE5 FSceneRenderer pattern
 * 
 * This class manages parallel rendering of multiple passes, following UE5's
 * parallel rendering architecture.
 * 
 * Reference: UE5 FSceneRenderer
 * Engine/Source/Runtime/Renderer/Private/SceneRendering.h
 */

#include "Core/CoreMinimal.h"
#include "Core/Templates/SharedPointer.h"
#include "Containers/Array.h"
#include "RHI/RHI.h"
#include "Renderer/RenderPasses.h"
#include "Core/FTaskGraph.h"

// Forward declarations
namespace MonsterRender::RHI {
class FRHICommandListParallelTranslator;
class FTranslatedCommandBufferCollection;
}

namespace MonsterEngine {
namespace Renderer {

// Forward declarations
class FScene;
class FSceneViewFamily;
class FSceneView;

/**
 * Parallel scene renderer
 * Manages parallel rendering of scene with multiple render passes
 * 
 * Architecture follows UE5's FSceneRenderer pattern:
 * 1. BeginFrame - set up resources
 * 2. InitViews - visibility culling
 * 3. RenderParallel - dispatch parallel passes
 * 4. WaitForTasks - synchronize
 * 5. ExecuteCommandBuffers - merge and execute
 * 6. EndFrame - cleanup
 */
class FParallelSceneRenderer {
public:
    /**
     * Constructor
     * @param Scene Scene to render
     * @param ViewFamily View family containing views
     */
    FParallelSceneRenderer(FScene* Scene, FSceneViewFamily* ViewFamily);
    
    /**
     * Destructor
     */
    ~FParallelSceneRenderer();
    
    /**
     * Main rendering entry point
     * @param RHICmdList RHI command list for rendering
     */
    void Render(MonsterRender::RHI::IRHICommandList* RHICmdList);
    
    /**
     * Begin a new frame
     * Sets up command buffer collection and prepares for rendering
     * @return True if successful
     */
    bool BeginFrame();
    
    /**
     * End the current frame
     * Cleans up resources
     */
    void EndFrame();
    
    /**
     * Execute all secondary command buffers in primary command buffer
     * @param RHICmdList Primary command list
     */
    void ExecuteSecondaryCommandBuffers(MonsterRender::RHI::IRHICommandList* RHICmdList);
    
    /**
     * Enable or disable parallel rendering
     * @param bEnable True to enable parallel rendering
     */
    void SetEnableParallelRendering(bool bEnable) { m_bEnableParallelRendering = bEnable; }
    
    /**
     * Check if parallel rendering is enabled
     */
    bool IsParallelRenderingEnabled() const { return m_bEnableParallelRendering; }
    
    /**
     * Set number of parallel worker threads
     * @param NumThreads Number of threads (1-8)
     */
    void SetNumParallelThreads(uint32 NumThreads);
    
    /**
     * Get current number of parallel threads
     */
    uint32 GetNumParallelThreads() const { return m_numParallelThreads; }
    
private:
    /**
     * Initialize views for rendering
     * Performs visibility culling and view setup
     */
    void InitViews();
    
    /**
     * Render base pass in parallel
     * Renders opaque geometry
     * @param RHICmdList Command list
     */
    void RenderBasePassParallel(MonsterRender::RHI::IRHICommandList* RHICmdList);
    
    /**
     * Render PBR pass in parallel
     * Renders PBR materials
     * @param RHICmdList Command list
     */
    void RenderPBRPassParallel(MonsterRender::RHI::IRHICommandList* RHICmdList);
    
    /**
     * Render shadow depth pass in parallel
     * Generates shadow maps
     * @param RHICmdList Command list
     */
    void RenderShadowDepthPassParallel(MonsterRender::RHI::IRHICommandList* RHICmdList);
    
    /**
     * Dispatch all parallel render passes
     * Creates tasks for parallel execution
     */
    void DispatchParallelRenderPasses();
    
    /**
     * Wait for all parallel tasks to complete
     */
    void WaitForParallelTasks();
    
    /**
     * Determine if parallel rendering should be used
     * Based on scene complexity and configuration
     * @return True if parallel rendering should be used
     */
    bool ShouldUseParallelRendering() const;
    
private:
    // Scene data
    FScene* m_scene;
    FSceneViewFamily* m_viewFamily;
    
    // Parallel rendering system
    MonsterEngine::TUniquePtr<MonsterRender::RHI::FRHICommandListParallelTranslator> m_parallelTranslator;
    MonsterEngine::TUniquePtr<MonsterRender::RHI::FTranslatedCommandBufferCollection> m_commandBufferCollection;
    
    // Render pass management
    MonsterEngine::TUniquePtr<FRenderPassManager> m_passManager;
    
    // Parallel task events
    TArray<FGraphEventRef> m_parallelTaskEvents;
    
    // Configuration
    bool m_bEnableParallelRendering;
    uint32 m_numParallelThreads;
    
    // Frame state
    bool m_bFrameBegun;
};

} // namespace Renderer
} // namespace MonsterEngine
