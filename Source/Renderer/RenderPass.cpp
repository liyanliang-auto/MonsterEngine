// Copyright Monster Engine. All Rights Reserved.

/**
 * @file RenderPass.cpp
 * @brief Implementation of render pass system
 * 
 * This file implements the base render pass functionality and the pass manager
 * for orchestrating render pass execution.
 * 
 * Reference UE5: Engine/Source/Runtime/Renderer/Private/SceneRendering.cpp
 */

#include "Renderer/RenderPass.h"
#include "RHI/IRHICommandList.h"
#include "Core/Logging/LogMacros.h"

// Use RHI namespace
using namespace MonsterRender::RHI;

namespace MonsterEngine
{

// Define log category for render pass system
DECLARE_LOG_CATEGORY_EXTERN(LogRenderPass, Log, All);
DEFINE_LOG_CATEGORY(LogRenderPass);

// ============================================================================
// FRenderPassBase Implementation
// ============================================================================

void FRenderPassBase::ApplyRenderStates(FRenderPassContext& Context)
{
    // Apply depth-stencil, blend, and rasterizer states through RHI
    // Reference UE5: FMeshPassProcessor::SetDepthStencilState, SetBlendState
    
    if (!Context.RHICmdList)
    {
        return;
    }
    
    IRHICommandList* CmdList = Context.RHICmdList;
    
    // Apply depth-stencil state
    // Convert ECompareFunc to uint8 for RHI interface
    uint8 DepthCompareFunc = static_cast<uint8>(Config.DepthStencilState.DepthCompareFunc);
    CmdList->setDepthStencilState(
        Config.DepthStencilState.bDepthTestEnable,
        Config.DepthStencilState.bDepthWriteEnable,
        DepthCompareFunc
    );
    
    // Apply blend state
    // Convert blend factors and operations to uint8 for RHI interface
    uint8 SrcColorBlend = static_cast<uint8>(Config.BlendState.SrcColorBlend);
    uint8 DstColorBlend = static_cast<uint8>(Config.BlendState.DstColorBlend);
    uint8 ColorBlendOp = static_cast<uint8>(Config.BlendState.ColorBlendOp);
    uint8 SrcAlphaBlend = static_cast<uint8>(Config.BlendState.SrcAlphaBlend);
    uint8 DstAlphaBlend = static_cast<uint8>(Config.BlendState.DstAlphaBlend);
    uint8 AlphaBlendOp = static_cast<uint8>(Config.BlendState.AlphaBlendOp);
    
    CmdList->setBlendState(
        Config.BlendState.bBlendEnable,
        SrcColorBlend, DstColorBlend, ColorBlendOp,
        SrcAlphaBlend, DstAlphaBlend, AlphaBlendOp,
        Config.BlendState.ColorWriteMask
    );
    
    // Apply rasterizer state
    uint8 FillMode = static_cast<uint8>(Config.RasterizerState.FillMode);
    uint8 CullMode = static_cast<uint8>(Config.RasterizerState.CullMode);
    
    CmdList->setRasterizerState(
        FillMode,
        CullMode,
        Config.RasterizerState.bFrontCounterClockwise,
        Config.RasterizerState.DepthBias,
        Config.RasterizerState.SlopeScaledDepthBias
    );
    
    MR_LOG(LogRenderPass, Verbose, "Applied render states for pass: %s", GetPassName());
}

void FRenderPassBase::SetViewport(FRenderPassContext& Context)
{
    // Set viewport through RHI command list
    // Reference UE5: FSceneRenderer::SetupViewRectUniformBufferParameters
    
    if (!Context.RHICmdList)
    {
        return;
    }
    
    IRHICommandList* CmdList = Context.RHICmdList;
    
    // Create viewport structure
    Viewport ViewportData;
    ViewportData.x = static_cast<float>(Context.ViewportX);
    ViewportData.y = static_cast<float>(Context.ViewportY);
    ViewportData.width = static_cast<float>(Context.ViewportWidth);
    ViewportData.height = static_cast<float>(Context.ViewportHeight);
    ViewportData.minDepth = 0.0f;
    ViewportData.maxDepth = 1.0f;
    
    CmdList->setViewport(ViewportData);
    
    // Also set scissor rect to match viewport
    ScissorRect ScissorData;
    ScissorData.left = Context.ViewportX;
    ScissorData.top = Context.ViewportY;
    ScissorData.right = Context.ViewportX + Context.ViewportWidth;
    ScissorData.bottom = Context.ViewportY + Context.ViewportHeight;
    
    CmdList->setScissorRect(ScissorData);
}

void FRenderPassBase::ClearTargets(FRenderPassContext& Context)
{
    // Clear render targets if configured
    // Reference UE5: FSceneRenderer::ClearView
    
    if (!Context.RHICmdList)
    {
        return;
    }
    
    IRHICommandList* CmdList = Context.RHICmdList;
    
    // Clear color target if configured
    if (Config.bClearColor && Context.ColorTarget)
    {
        float ClearColor[4] = {
            Config.ClearColor.X,
            Config.ClearColor.Y,
            Config.ClearColor.Z,
            Config.ClearColor.W
        };
        CmdList->clearRenderTarget(Context.ColorTarget, ClearColor);
    }
    
    // Clear depth/stencil target if configured
    if ((Config.bClearDepth || Config.bClearStencil) && Context.DepthTarget)
    {
        CmdList->clearDepthStencil(
            Context.DepthTarget,
            Config.bClearDepth,
            Config.bClearStencil,
            Config.ClearDepth,
            Config.ClearStencil
        );
    }
}

// ============================================================================
// FRenderPassManager Implementation
// ============================================================================

FRenderPassManager::FRenderPassManager()
{
    // Reserve space for common passes
    Passes.Reserve(static_cast<int32>(ERenderPassType::Num));
}

FRenderPassManager::~FRenderPassManager()
{
    ClearPasses();
}

void FRenderPassManager::RegisterPass(IRenderPass* Pass)
{
    if (!Pass)
    {
        MR_LOG(LogRenderPass, Warning, "Attempted to register null render pass");
        return;
    }
    
    // Check for duplicate pass type and replace if found
    for (int32 i = 0; i < Passes.Num(); ++i)
    {
        if (Passes[i]->GetPassType() == Pass->GetPassType())
        {
            MR_LOG(LogRenderPass, Warning, "Replacing existing render pass: %s", 
                   GetRenderPassName(Pass->GetPassType()));
            delete Passes[i];
            Passes[i] = Pass;
            bNeedsSorting = true;
            return;
        }
    }
    
    // Add new pass
    Passes.Add(Pass);
    bNeedsSorting = true;
    
    MR_LOG(LogRenderPass, Log, "Registered render pass: %s (Priority: %d)", 
           Pass->GetPassName(), Pass->GetConfig().Priority);
}

void FRenderPassManager::UnregisterPass(ERenderPassType PassType)
{
    for (int32 i = Passes.Num() - 1; i >= 0; --i)
    {
        if (Passes[i]->GetPassType() == PassType)
        {
            MR_LOG(LogRenderPass, Log, "Unregistered render pass: %s", 
                   Passes[i]->GetPassName());
            delete Passes[i];
            Passes.RemoveAt(i);
            return;
        }
    }
    
    MR_LOG(LogRenderPass, Warning, "Failed to unregister pass: %s (not found)",
           GetRenderPassName(PassType));
}

IRenderPass* FRenderPassManager::GetPass(ERenderPassType PassType) const
{
    for (IRenderPass* Pass : Passes)
    {
        if (Pass && Pass->GetPassType() == PassType)
        {
            return Pass;
        }
    }
    return nullptr;
}

void FRenderPassManager::ExecuteAllPasses(FRenderPassContext& Context)
{
    // Sort passes if needed
    if (bNeedsSorting)
    {
        SortPasses();
    }
    
    // Execute each pass in order
    for (IRenderPass* Pass : Passes)
    {
        if (Pass && Pass->ShouldExecute(Context))
        {
            // Setup pass
            Pass->Setup(Context);
            
            // Execute pass
            Pass->Execute(Context);
            
            // Cleanup pass
            Pass->Cleanup(Context);
        }
    }
}

void FRenderPassManager::ExecutePass(ERenderPassType PassType, FRenderPassContext& Context)
{
    IRenderPass* Pass = GetPass(PassType);
    if (Pass && Pass->ShouldExecute(Context))
    {
        Pass->Setup(Context);
        Pass->Execute(Context);
        Pass->Cleanup(Context);
    }
    else if (!Pass)
    {
        MR_LOG(LogRenderPass, Warning, "Cannot execute pass %s: not registered",
               GetRenderPassName(PassType));
    }
}

void FRenderPassManager::SortPasses()
{
    // Sort by priority (lower priority = earlier execution)
    Passes.Sort([](const IRenderPass* A, const IRenderPass* B)
    {
        if (!A || !B) return false;
        return A->GetConfig().Priority < B->GetConfig().Priority;
    });
    
    bNeedsSorting = false;
    
    MR_LOG(LogRenderPass, Verbose, "Sorted %d render passes by priority", Passes.Num());
}

void FRenderPassManager::ClearPasses()
{
    for (IRenderPass* Pass : Passes)
    {
        if (Pass)
        {
            delete Pass;
        }
    }
    Passes.Empty();
    bNeedsSorting = false;
    
    MR_LOG(LogRenderPass, Log, "Cleared all render passes");
}

} // namespace MonsterEngine
