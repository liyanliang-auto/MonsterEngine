// Copyright Monster Engine. All Rights Reserved.

#include "Renderer/ParallelSceneRenderer.h"
#include "Core/Log.h"
#include "RHI/FRHICommandListParallelTranslator.h"
#include "RHI/FTranslatedCommandBufferCollection.h"
#include "Renderer/Scene.h"
#include "Renderer/SceneView.h"
#include "Core/FTaskGraph.h"

namespace MonsterEngine {
namespace Renderer {

FParallelSceneRenderer::FParallelSceneRenderer(FScene* Scene, FSceneViewFamily* ViewFamily)
    : m_scene(Scene)
    , m_viewFamily(ViewFamily)
    , m_bEnableParallelRendering(true)
    , m_numParallelThreads(4)
    , m_bFrameBegun(false)
{
    MR_LOG_INFO("FParallelSceneRenderer created");
    
    // Create parallel translator
    m_parallelTranslator = MakeUnique<MonsterRender::RHI::FRHICommandListParallelTranslator>();
    
    // Create render pass manager
    m_passManager = MakeUnique<FRenderPassManager>();
}

FParallelSceneRenderer::~FParallelSceneRenderer() {
    if (m_bFrameBegun) {
        EndFrame();
    }
    
    MR_LOG_INFO("FParallelSceneRenderer destroyed");
}

void FParallelSceneRenderer::Render(MonsterRender::RHI::IRHICommandList* RHICmdList) {
    if (!RHICmdList) {
        MR_LOG_ERROR("FParallelSceneRenderer::Render - Null command list");
        return;
    }
    
    MR_LOG_INFO("=== FParallelSceneRenderer::Render ===");
    
    // Begin frame
    if (!BeginFrame()) {
        MR_LOG_ERROR("FParallelSceneRenderer::Render - Failed to begin frame");
        return;
    }
    
    // Initialize views (visibility culling)
    InitViews();
    
    // Decide whether to use parallel rendering
    if (ShouldUseParallelRendering()) {
        MR_LOG_INFO("Using parallel rendering path");
        
        // Dispatch parallel render passes
        DispatchParallelRenderPasses();
        
        // Wait for all tasks to complete
        WaitForParallelTasks();
        
        // Execute all secondary command buffers
        ExecuteSecondaryCommandBuffers(RHICmdList);
    } else {
        MR_LOG_INFO("Using single-threaded rendering path");
        
        // Fallback to single-threaded rendering
        // This will be implemented when integrating with CubeSceneApplication
    }
    
    // End frame
    EndFrame();
}

bool FParallelSceneRenderer::BeginFrame() {
    if (m_bFrameBegun) {
        MR_LOG_WARNING("FParallelSceneRenderer::BeginFrame - Frame already begun");
        return false;
    }
    
    MR_LOG_INFO("FParallelSceneRenderer::BeginFrame");
    
    // Create a new command buffer collection for this frame
    m_commandBufferCollection = MakeUnique<MonsterRender::RHI::FTranslatedCommandBufferCollection>();
    if (!m_commandBufferCollection) {
        MR_LOG_ERROR("Failed to create command buffer collection");
        return false;
    }
    
    // Set the collection in the translator
    m_parallelTranslator->SetCommandBufferCollection(std::move(m_commandBufferCollection));
    
    // Clear parallel task events
    m_parallelTaskEvents.clear();
    
    m_bFrameBegun = true;
    return true;
}

void FParallelSceneRenderer::EndFrame() {
    if (!m_bFrameBegun) {
        return;
    }
    
    MR_LOG_INFO("FParallelSceneRenderer::EndFrame");
    
    // Clear task events
    m_parallelTaskEvents.clear();
    
    // Reset command buffer collection
    m_commandBufferCollection.reset();
    
    m_bFrameBegun = false;
}

void FParallelSceneRenderer::ExecuteSecondaryCommandBuffers(MonsterRender::RHI::IRHICommandList* RHICmdList) {
    if (!RHICmdList) {
        MR_LOG_ERROR("FParallelSceneRenderer::ExecuteSecondaryCommandBuffers - Null command list");
        return;
    }
    
    MR_LOG_INFO("FParallelSceneRenderer::ExecuteSecondaryCommandBuffers");
    
    // Get the command buffer collection from translator
    auto* collection = m_parallelTranslator->GetCommandBufferCollection();
    if (!collection) {
        MR_LOG_WARNING("No command buffer collection available");
        return;
    }
    
    // Execute all secondary command buffers
    // This will be implemented when we have the actual command buffer execution
    MR_LOG_INFO("Executing secondary command buffers (implementation pending)");
}

void FParallelSceneRenderer::SetNumParallelThreads(uint32 NumThreads) {
    if (NumThreads < 1 || NumThreads > 8) {
        MR_LOG_WARNING("FParallelSceneRenderer::SetNumParallelThreads - Invalid thread count: " + 
                      std::to_string(NumThreads) + ", clamping to [1, 8]");
        NumThreads = (NumThreads < 1) ? 1 : 8;
    }
    
    m_numParallelThreads = NumThreads;
    MR_LOG_INFO("Parallel threads set to: " + std::to_string(m_numParallelThreads));
}

void FParallelSceneRenderer::InitViews() {
    MR_LOG_DEBUG("FParallelSceneRenderer::InitViews");
    
    // View initialization will be implemented when integrating with scene system
    // For now, this is a placeholder
}

void FParallelSceneRenderer::RenderBasePassParallel(MonsterRender::RHI::IRHICommandList* RHICmdList) {
    MR_LOG_DEBUG("FParallelSceneRenderer::RenderBasePassParallel");
    
    // Base pass rendering will be implemented in Phase 3
}

void FParallelSceneRenderer::RenderPBRPassParallel(MonsterRender::RHI::IRHICommandList* RHICmdList) {
    MR_LOG_DEBUG("FParallelSceneRenderer::RenderPBRPassParallel");
    
    // PBR pass rendering will be implemented in Phase 3
}

void FParallelSceneRenderer::RenderShadowDepthPassParallel(MonsterRender::RHI::IRHICommandList* RHICmdList) {
    MR_LOG_DEBUG("FParallelSceneRenderer::RenderShadowDepthPassParallel");
    
    // Shadow depth pass rendering will be implemented in Phase 3
}

void FParallelSceneRenderer::SetRenderTaskDelegates(
    FRenderTaskDelegate ShadowDepthTask,
    FRenderTaskDelegate BasePassTask,
    FRenderTaskDelegate PBRPassTask
) {
    m_shadowDepthTaskDelegate = std::move(ShadowDepthTask);
    m_basePassTaskDelegate = std::move(BasePassTask);
    m_pbrPassTaskDelegate = std::move(PBRPassTask);
    
    MR_LOG_INFO("FParallelSceneRenderer::SetRenderTaskDelegates - Delegates configured");
}

void FParallelSceneRenderer::DispatchParallelRenderPasses() {
    MR_LOG_INFO("FParallelSceneRenderer::DispatchParallelRenderPasses");
    
    // Clear previous task events
    m_parallelTaskEvents.clear();
    
    // Dispatch shadow depth pass
    if (m_shadowDepthTaskDelegate) {
        MR_LOG_DEBUG("Dispatching shadow depth pass");
        auto shadowTask = m_shadowDepthTaskDelegate();
        if (shadowTask) {
            m_parallelTaskEvents.push_back(shadowTask);
        }
    }
    
    // Dispatch base pass
    if (m_basePassTaskDelegate) {
        MR_LOG_DEBUG("Dispatching base pass");
        auto baseTask = m_basePassTaskDelegate();
        if (baseTask) {
            m_parallelTaskEvents.push_back(baseTask);
        }
    }
    
    // Dispatch PBR pass
    if (m_pbrPassTaskDelegate) {
        MR_LOG_DEBUG("Dispatching PBR pass");
        auto pbrTask = m_pbrPassTaskDelegate();
        if (pbrTask) {
            m_parallelTaskEvents.push_back(pbrTask);
        }
    }
    
    MR_LOG_INFO("Dispatched " + std::to_string(m_parallelTaskEvents.size()) + " parallel render tasks");
}

void FParallelSceneRenderer::WaitForParallelTasks() {
    if (m_parallelTaskEvents.empty()) {
        return;
    }
    
    MR_LOG_INFO("FParallelSceneRenderer::WaitForParallelTasks - Waiting for " + 
               std::to_string(m_parallelTaskEvents.size()) + " tasks");
    
    // Wait for all tasks
    MonsterEngine::FTaskGraph::WaitForTasks(m_parallelTaskEvents);
    
    MR_LOG_INFO("All parallel tasks completed");
}

bool FParallelSceneRenderer::ShouldUseParallelRendering() const {
    // Check if parallel rendering is enabled
    if (!m_bEnableParallelRendering) {
        return false;
    }
    
    // Check if we have enough threads
    if (m_numParallelThreads < 2) {
        return false;
    }
    
    // For now, always use parallel rendering if enabled
    // In the future, we can add heuristics based on scene complexity
    return true;
}

} // namespace Renderer
} // namespace MonsterEngine
