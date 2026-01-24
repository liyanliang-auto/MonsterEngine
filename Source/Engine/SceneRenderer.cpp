// Copyright Monster Engine. All Rights Reserved.

/**
 * @file SceneRenderer.cpp
 * @brief Implementation of scene renderer classes
 */

#include "Engine/SceneRenderer.h"
#include "Engine/Scene.h"
#include "Engine/PrimitiveSceneInfo.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Engine/LightSceneInfo.h"
#include "Engine/LightSceneProxy.h"
#include "Renderer/ForwardRenderPasses.h"
#include "Renderer/RenderPass.h"
#include "RHI/IRHICommandList.h"
#include "Containers/SparseArray.h"
#include "Core/Logging/LogMacros.h"

// Use RHI namespace
using namespace MonsterRender::RHI;

namespace MonsterEngine
{

// Use global log category (defined in LogCategories.cpp)
using MonsterRender::LogSceneRenderer;

// ============================================================================
// FMeshDrawCommand Implementation
// ============================================================================

void FMeshDrawCommand::ComputeSortKey()
{
    // Sort key composition:
    // Bits 63-48: Material ID (for batching)
    // Bits 47-32: Mesh element index
    // Bits 31-0:  Distance (for front-to-back sorting)
    
    uint64 MaterialKey = 0;
    if (PrimitiveSceneProxy)
    {
        // Use a hash of the proxy pointer as a simple material key
        MaterialKey = reinterpret_cast<uint64>(PrimitiveSceneProxy) & 0xFFFF;
    }
    
    uint64 MeshKey = static_cast<uint64>(MeshElementIndex) & 0xFFFF;
    
    SortKey = (MaterialKey << 48) | (MeshKey << 32);
}

// ============================================================================
// FSceneRenderer Implementation
// ============================================================================

FSceneRenderer::FSceneRenderer(FScene* InScene, const FSceneViewFamily& InViewFamily)
    : Scene(InScene)
    , ViewFamily(InViewFamily)
    , FrameNumber(InViewFamily.FrameNumber)
    , bInitialized(false)
{
    MR_LOG(LogSceneRenderer, Verbose, "SceneRenderer created for frame %u", FrameNumber);
}

FSceneRenderer::~FSceneRenderer()
{
    MR_LOG(LogSceneRenderer, Verbose, "SceneRenderer destroyed");
}

void FSceneRenderer::Render(IRHICommandList& RHICmdList)
{
    if (!Scene)
    {
        MR_LOG(LogSceneRenderer, Warning, "Cannot render: no scene");
        return;
    }

    // Initialize views
    InitViews();

    // Compute visibility for all views
    ComputeVisibility();

    // Gather visible primitives
    GatherVisiblePrimitives();

    // Sort primitives for optimal rendering
    SortPrimitives();

    // Render passes
    RenderDepthPrepass(RHICmdList);
    RenderShadowDepths(RHICmdList);
    RenderBasePass(RHICmdList);
    RenderLighting(RHICmdList);
    RenderTranslucency(RHICmdList);
    RenderPostProcess(RHICmdList);

    MR_LOG(LogSceneRenderer, Verbose, "Frame %u rendered with %d views", FrameNumber, Views.Num());
}

void FSceneRenderer::InitViews()
{
    Views.Empty();
    
    // Create FViewInfo for each view in the family
    for (int32 ViewIndex = 0; ViewIndex < ViewFamily.GetNumViews(); ++ViewIndex)
    {
        const FSceneView* View = ViewFamily.GetView(ViewIndex);
        if (View)
        {
            Views.Add(FViewInfo(*View));
        }
    }

    bInitialized = true;
    
    MR_LOG(LogSceneRenderer, Verbose, "Initialized %d views", Views.Num());
}

void FSceneRenderer::ComputeVisibility()
{
    for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
    {
        FViewInfo& ViewInfo = Views[ViewIndex];
        
        // Compute visibility for this view
        VisibilityManager.ComputeVisibility(*Scene, ViewInfo, ViewInfo.VisibilityResult);
        
        ViewInfo.bVisibilityComputed = true;
        
        MR_LOG(LogSceneRenderer, Verbose, "View %d: %d visible primitives", 
               ViewIndex, ViewInfo.VisibilityResult.NumVisiblePrimitives);
    }
}

void FSceneRenderer::GatherVisiblePrimitives()
{
    const TArray<FPrimitiveSceneInfo*>& AllPrimitives = Scene->GetPrimitives();
    
    for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
    {
        FViewInfo& ViewInfo = Views[ViewIndex];
        
        ViewInfo.VisibleStaticPrimitives.Empty();
        ViewInfo.VisibleDynamicPrimitives.Empty();
        ViewInfo.VisibleTranslucentPrimitives.Empty();
        
        for (int32 PrimitiveIndex = 0; PrimitiveIndex < AllPrimitives.Num(); ++PrimitiveIndex)
        {
            if (!ViewInfo.VisibilityResult.IsVisible(PrimitiveIndex))
            {
                continue;
            }
            
            FPrimitiveSceneInfo* PrimitiveInfo = AllPrimitives[PrimitiveIndex];
            if (!PrimitiveInfo)
            {
                continue;
            }
            
            FPrimitiveSceneProxy* Proxy = PrimitiveInfo->GetProxy();
            if (!Proxy)
            {
                continue;
            }
            
            // Get view relevance
            FPrimitiveViewRelevance ViewRelevance = Proxy->GetViewRelevance(&ViewInfo);
            
            // Categorize primitive
            if (ViewRelevance.bHasTranslucency)
            {
                ViewInfo.VisibleTranslucentPrimitives.Add(PrimitiveInfo);
            }
            else if (ViewRelevance.bStaticRelevance)
            {
                ViewInfo.VisibleStaticPrimitives.Add(PrimitiveInfo);
            }
            else if (ViewRelevance.bDynamicRelevance)
            {
                ViewInfo.VisibleDynamicPrimitives.Add(PrimitiveInfo);
            }
        }
        
        MR_LOG(LogSceneRenderer, Verbose, 
               "View %d gathered: %d static, %d dynamic, %d translucent",
               ViewIndex, 
               ViewInfo.VisibleStaticPrimitives.Num(),
               ViewInfo.VisibleDynamicPrimitives.Num(),
               ViewInfo.VisibleTranslucentPrimitives.Num());
    }
    
    // Gather visible lights from directional lights array
    const TArray<FLightSceneInfo*>& DirectionalLights = Scene->GetDirectionalLights();
    
    for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
    {
        FViewInfo& ViewInfo = Views[ViewIndex];
        ViewInfo.VisibleLights.Empty();
        
        // Add directional lights
        for (FLightSceneInfo* LightInfo : DirectionalLights)
        {
            if (!LightInfo)
            {
                continue;
            }
            
            FLightSceneProxy* LightProxy = LightInfo->GetProxy();
            if (!LightProxy)
            {
                continue;
            }
            
            // Check if light affects this view
            // For now, include all lights (proper culling would check light bounds)
            ViewInfo.VisibleLights.Add(LightInfo);
        }
        
        // Also iterate through the sparse array of all lights
        const TSparseArray<FLightSceneInfoCompact>& AllLights = Scene->GetLights();
        for (auto It = AllLights.CreateConstIterator(); It; ++It)
        {
            FLightSceneInfo* LightInfo = It->LightSceneInfo;
            if (!LightInfo)
            {
                continue;
            }
            
            // Skip if already added (directional lights)
            if (ViewInfo.VisibleLights.Contains(LightInfo))
            {
                continue;
            }
            
            FLightSceneProxy* LightProxy = LightInfo->GetProxy();
            if (!LightProxy)
            {
                continue;
            }
            
            ViewInfo.VisibleLights.Add(LightInfo);
        }
        
        MR_LOG(LogSceneRenderer, Verbose, "View %d: %d visible lights", 
               ViewIndex, ViewInfo.VisibleLights.Num());
    }
}

void FSceneRenderer::SortPrimitives()
{
    for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
    {
        FViewInfo& ViewInfo = Views[ViewIndex];
        
        // Sort static primitives by material for batching
        // In a real implementation, this would use a more sophisticated sort key
        
        // Sort translucent primitives back-to-front
        if (ViewInfo.VisibleTranslucentPrimitives.Num() > 1)
        {
            const FVector ViewOrigin = ViewInfo.ViewLocation;
            
            ViewInfo.VisibleTranslucentPrimitives.Sort(
                [&ViewOrigin](const FPrimitiveSceneInfo* A, const FPrimitiveSceneInfo* B)
                {
                    if (!A || !B) return false;
                    
                    FPrimitiveSceneProxy* ProxyA = A->GetProxy();
                    FPrimitiveSceneProxy* ProxyB = B->GetProxy();
                    
                    if (!ProxyA || !ProxyB) return false;
                    
                    float DistA = (ProxyA->GetBounds().Origin - ViewOrigin).SizeSquared();
                    float DistB = (ProxyB->GetBounds().Origin - ViewOrigin).SizeSquared();
                    
                    return DistA > DistB; // Back-to-front
                });
        }
    }
}

void FSceneRenderer::RenderDepthPrepass(IRHICommandList& RHICmdList)
{
    MR_LOG(LogSceneRenderer, Verbose, "Rendering depth prepass");
    
    for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
    {
        TArray<FMeshDrawCommand> DrawCommands;
        GenerateDrawCommands(ViewIndex, ERenderPass::DepthPrepass, DrawCommands);
        SubmitDrawCommands(RHICmdList, DrawCommands);
    }
}

void FSceneRenderer::RenderBasePass(IRHICommandList& RHICmdList)
{
    MR_LOG(LogSceneRenderer, Verbose, "Rendering base pass");
    
    for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
    {
        TArray<FMeshDrawCommand> DrawCommands;
        GenerateDrawCommands(ViewIndex, ERenderPass::BasePass, DrawCommands);
        SubmitDrawCommands(RHICmdList, DrawCommands);
    }
}

void FSceneRenderer::RenderShadowDepths(IRHICommandList& RHICmdList)
{
    MR_LOG(LogSceneRenderer, Verbose, "Rendering shadow depths");
    
    // For each shadow-casting light, render shadow maps
    for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
    {
        FViewInfo& ViewInfo = Views[ViewIndex];
        
        for (FLightSceneInfo* LightInfo : ViewInfo.VisibleLights)
        {
            if (!LightInfo)
            {
                continue;
            }
            
            FLightSceneProxy* LightProxy = LightInfo->GetProxy();
            if (!LightProxy || !LightProxy->CastsShadow())
            {
                continue;
            }
            
            // Generate shadow draw commands
            TArray<FMeshDrawCommand> ShadowCommands;
            GenerateDrawCommands(ViewIndex, ERenderPass::ShadowDepth, ShadowCommands);
            SubmitDrawCommands(RHICmdList, ShadowCommands);
        }
    }
}

void FSceneRenderer::RenderLighting(IRHICommandList& RHICmdList)
{
    MR_LOG(LogSceneRenderer, Verbose, "Rendering lighting");
    
    // Base implementation does nothing - derived classes implement specific lighting
}

void FSceneRenderer::RenderTranslucency(IRHICommandList& RHICmdList)
{
    MR_LOG(LogSceneRenderer, Verbose, "Rendering translucency");
    
    for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
    {
        TArray<FMeshDrawCommand> DrawCommands;
        GenerateDrawCommands(ViewIndex, ERenderPass::Translucency, DrawCommands);
        SubmitDrawCommands(RHICmdList, DrawCommands);
    }
}

void FSceneRenderer::RenderPostProcess(IRHICommandList& RHICmdList)
{
    MR_LOG(LogSceneRenderer, Verbose, "Rendering post-process");
    
    // Base implementation does nothing - derived classes implement post-processing
}

void FSceneRenderer::GenerateDrawCommands(int32 ViewIndex, ERenderPass Pass, TArray<FMeshDrawCommand>& OutCommands)
{
    if (ViewIndex < 0 || ViewIndex >= Views.Num())
    {
        return;
    }
    
    FViewInfo& ViewInfo = Views[ViewIndex];
    
    // Select primitive list based on pass
    TArray<FPrimitiveSceneInfo*>* PrimitiveList = nullptr;
    
    switch (Pass)
    {
    case ERenderPass::DepthPrepass:
    case ERenderPass::BasePass:
    case ERenderPass::ShadowDepth:
        // Use static and dynamic primitives
        PrimitiveList = &ViewInfo.VisibleStaticPrimitives;
        break;
        
    case ERenderPass::Translucency:
        PrimitiveList = &ViewInfo.VisibleTranslucentPrimitives;
        break;
        
    default:
        return;
    }
    
    if (!PrimitiveList)
    {
        return;
    }
    
    // Generate draw commands for each primitive
    for (FPrimitiveSceneInfo* PrimitiveInfo : *PrimitiveList)
    {
        if (!PrimitiveInfo)
        {
            continue;
        }
        
        FPrimitiveSceneProxy* Proxy = PrimitiveInfo->GetProxy();
        if (!Proxy)
        {
            continue;
        }
        
        // Create draw command
        FMeshDrawCommand Command;
        Command.PrimitiveSceneInfo = PrimitiveInfo;
        Command.PrimitiveSceneProxy = Proxy;
        Command.MaterialIndex = 0;
        Command.MeshElementIndex = 0;
        Command.NumInstances = 1;
        Command.FirstInstance = 0;
        Command.ComputeSortKey();
        
        OutCommands.Add(Command);
    }
    
    // Also add dynamic primitives for opaque passes
    if (Pass == ERenderPass::BasePass || Pass == ERenderPass::DepthPrepass)
    {
        for (FPrimitiveSceneInfo* PrimitiveInfo : ViewInfo.VisibleDynamicPrimitives)
        {
            if (!PrimitiveInfo)
            {
                continue;
            }
            
            FPrimitiveSceneProxy* Proxy = PrimitiveInfo->GetProxy();
            if (!Proxy)
            {
                continue;
            }
            
            FMeshDrawCommand Command;
            Command.PrimitiveSceneInfo = PrimitiveInfo;
            Command.PrimitiveSceneProxy = Proxy;
            Command.MaterialIndex = 0;
            Command.MeshElementIndex = 0;
            Command.NumInstances = 1;
            Command.FirstInstance = 0;
            Command.ComputeSortKey();
            
            OutCommands.Add(Command);
        }
    }
    
    // Sort commands by sort key
    OutCommands.Sort();
}

void FSceneRenderer::SubmitDrawCommands(IRHICommandList& RHICmdList, const TArray<FMeshDrawCommand>& Commands)
{
    // In a real implementation, this would:
    // 1. Set render state for each command
    // 2. Bind vertex/index buffers
    // 3. Bind shaders and resources
    // 4. Issue draw calls
    
    // For now, just log the number of commands
    MR_LOG(LogSceneRenderer, Verbose, "Submitting %d draw commands", Commands.Num());
}

// ============================================================================
// FDeferredShadingRenderer Implementation
// ============================================================================

FDeferredShadingRenderer::FDeferredShadingRenderer(FScene* InScene, const FSceneViewFamily& InViewFamily)
    : FSceneRenderer(InScene, InViewFamily)
{
    MR_LOG(LogSceneRenderer, Verbose, "DeferredShadingRenderer created");
}

FDeferredShadingRenderer::~FDeferredShadingRenderer()
{
    MR_LOG(LogSceneRenderer, Verbose, "DeferredShadingRenderer destroyed");
}

void FDeferredShadingRenderer::Render(IRHICommandList& RHICmdList)
{
    if (!Scene)
    {
        MR_LOG(LogSceneRenderer, Warning, "Cannot render: no scene");
        return;
    }

    // Initialize views
    InitViews();

    // Compute visibility
    ComputeVisibility();

    // Gather visible primitives
    GatherVisiblePrimitives();

    // Sort primitives
    SortPrimitives();

    // Deferred rendering pipeline
    RenderDepthPrepass(RHICmdList);
    RenderShadowDepths(RHICmdList);
    RenderGBuffer(RHICmdList);
    RenderSSAO(RHICmdList);
    RenderDeferredLighting(RHICmdList);
    RenderSSR(RHICmdList);
    RenderTranslucency(RHICmdList);
    RenderPostProcess(RHICmdList);

    MR_LOG(LogSceneRenderer, Verbose, "Deferred frame %u rendered", FrameNumber);
}

void FDeferredShadingRenderer::RenderGBuffer(IRHICommandList& RHICmdList)
{
    MR_LOG(LogSceneRenderer, Verbose, "Rendering GBuffer");
    
    // GBuffer pass renders to multiple render targets:
    // - GBufferA: World Normal (RGB), Per-object data (A)
    // - GBufferB: Metallic (R), Specular (G), Roughness (B), Shading Model (A)
    // - GBufferC: Base Color (RGB), AO (A)
    // - GBufferD: Custom data
    // - Scene Depth
    
    for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
    {
        TArray<FMeshDrawCommand> DrawCommands;
        GenerateDrawCommands(ViewIndex, ERenderPass::BasePass, DrawCommands);
        SubmitDrawCommands(RHICmdList, DrawCommands);
    }
}

void FDeferredShadingRenderer::RenderDeferredLighting(IRHICommandList& RHICmdList)
{
    MR_LOG(LogSceneRenderer, Verbose, "Rendering deferred lighting");
    
    for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
    {
        FViewInfo& ViewInfo = Views[ViewIndex];
        
        // Render each light
        for (FLightSceneInfo* LightInfo : ViewInfo.VisibleLights)
        {
            if (!LightInfo)
            {
                continue;
            }
            
            FLightSceneProxy* LightProxy = LightInfo->GetProxy();
            if (!LightProxy)
            {
                continue;
            }
            
            // In a real implementation:
            // 1. Set light shader parameters
            // 2. Render light volume or full-screen quad
            // 3. Accumulate lighting contribution
            
            MR_LOG(LogSceneRenderer, Verbose, "Processing light type %d", 
                   static_cast<int32>(LightProxy->GetLightType()));
        }
    }
}

void FDeferredShadingRenderer::RenderSSAO(IRHICommandList& RHICmdList)
{
    MR_LOG(LogSceneRenderer, Verbose, "Rendering SSAO");
    
    // Screen-space ambient occlusion
    // In a real implementation:
    // 1. Sample depth buffer
    // 2. Reconstruct view-space positions
    // 3. Sample hemisphere and compute occlusion
    // 4. Blur and apply to lighting
}

void FDeferredShadingRenderer::RenderSSR(IRHICommandList& RHICmdList)
{
    MR_LOG(LogSceneRenderer, Verbose, "Rendering SSR");
    
    // Screen-space reflections
    // In a real implementation:
    // 1. Ray march in screen space
    // 2. Sample scene color at hit points
    // 3. Blend with environment reflections
}

} // namespace MonsterEngine
