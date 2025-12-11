// Copyright Monster Engine. All Rights Reserved.

/**
 * @file SceneRenderer.cpp
 * @brief Scene renderer implementation
 * 
 * Implements FSceneRenderer and FDeferredShadingSceneRenderer classes.
 * Reference: UE5 SceneRendering.cpp, DeferredShadingRenderer.cpp
 */

#include "Renderer/SceneRenderer.h"
#include "Renderer/Scene.h"
#include "Renderer/SceneVisibility.h"
#include "Core/Logging/Logging.h"
#include "RHI/IRHICommandList.h"
#include "RHI/IRHIDevice.h"

using namespace MonsterRender;

namespace MonsterEngine
{

// ============================================================================
// FSceneRenderer Implementation
// ============================================================================

FSceneRenderer::FSceneRenderer(const FSceneViewFamily* InViewFamily)
    : Scene(nullptr)
    , FeatureLevel(0)
    , ShaderPlatform(0)
    , bUsedPrecomputedVisibility(false)
    , bIsFirstSceneRenderer(true)
    , bIsLastSceneRenderer(true)
    , FamilySize(0, 0)
{
    if (InViewFamily)
    {
        ViewFamily = *InViewFamily;
        Scene = ViewFamily.Scene;
    }
    
    MR_LOG_DEBUG(LogRenderer, "FSceneRenderer created");
}

FSceneRenderer::~FSceneRenderer()
{
    MR_LOG_DEBUG(LogRenderer, "FSceneRenderer destroyed");
}

FSceneRenderer* FSceneRenderer::CreateSceneRenderer(const FSceneViewFamily* InViewFamily)
{
    if (!InViewFamily)
    {
        MR_LOG_ERROR(LogRenderer, "Cannot create scene renderer with null view family");
        return nullptr;
    }
    
    // Create deferred shading renderer by default
    if (InViewFamily->bDeferredShading)
    {
        return new FDeferredShadingSceneRenderer(InViewFamily);
    }
    else
    {
        return new FForwardShadingSceneRenderer(InViewFamily);
    }
}

void FSceneRenderer::RenderThreadBegin(RHI::IRHICommandList& RHICmdList)
{
    // Initialize views from the view family
    InitViews();
    
    // Compute family size
    ComputeFamilySize();
    
    MR_LOG_DEBUG(LogRenderer, "RenderThreadBegin: %d views, family size: %dx%d",
                 Views.Num(), FamilySize.X, FamilySize.Y);
}

void FSceneRenderer::RenderThreadEnd(RHI::IRHICommandList& RHICmdList)
{
    // Cleanup after rendering
    MeshCollector.ClearMeshes();
    
    MR_LOG_DEBUG(LogRenderer, "RenderThreadEnd");
}

void FSceneRenderer::PrepareViewRectsForRendering(RHI::IRHICommandList& RHICmdList)
{
    // Setup view rectangles based on screen percentage and other settings
    for (FViewInfo& View : Views)
    {
        // For now, use the unscaled view rect
        View.ViewRect = View.UnscaledViewRect;
    }
}

void FSceneRenderer::InitViews()
{
    // Create view infos from the view family
    // In a full implementation, this would copy view data from the view family
    
    if (Views.Num() == 0)
    {
        // Create at least one default view
        int32 ViewIdx = Views.Add(FViewInfo());
        FViewInfo& View = Views[ViewIdx];
        View.Family = &ViewFamily;
        View.ViewIndex = 0;
        View.bIsPrimaryView = true;
        
        // Initialize visibility arrays if we have a scene
        if (Scene)
        {
            View.InitVisibilityArrays(Scene->GetNumPrimitives());
        }
    }
}

void FSceneRenderer::ComputeFamilySize()
{
    FamilySize = Math::FIntPoint(0, 0);
    
    for (const FViewInfo& View : Views)
    {
        int32 ViewRight = static_cast<int32>(View.ViewRect.x + View.ViewRect.width);
        int32 ViewBottom = static_cast<int32>(View.ViewRect.y + View.ViewRect.height);
        
        FamilySize.X = Math::Max(FamilySize.X, ViewRight);
        FamilySize.Y = Math::Max(FamilySize.Y, ViewBottom);
    }
}

// ============================================================================
// Visibility Computation
// ============================================================================

void FSceneRenderer::PreVisibilityFrameSetup()
{
    // Setup before visibility computation
    for (FViewInfo& View : Views)
    {
        // Reset visibility data
        View.ResetVisibility();
        
        // Initialize view frustum
        View.InitViewFrustum();
        
        // Update view state
        if (View.State)
        {
            View.State->OnStartFrame(ViewFamily.FrameNumber);
        }
    }
    
    MR_LOG_DEBUG(LogRenderer, "PreVisibilityFrameSetup complete");
}

void FSceneRenderer::ComputeViewVisibility(RHI::IRHICommandList& RHICmdList)
{
    if (!Scene)
    {
        MR_LOG_WARNING(LogRenderer, "ComputeViewVisibility: No scene");
        return;
    }
    
    int32 NumPrimitives = Scene->GetNumPrimitives();
    if (NumPrimitives == 0)
    {
        MR_LOG_DEBUG(LogRenderer, "ComputeViewVisibility: No primitives in scene");
        return;
    }
    
    // Process each view
    for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
    {
        FViewInfo& View = Views[ViewIndex];
        
        // Ensure visibility arrays are properly sized
        if (View.PrimitiveVisibilityMap.Num() != NumPrimitives)
        {
            View.InitVisibilityArrays(NumPrimitives);
        }
        
        // Perform frustum culling
        int32 NumFrustumCulled = FrustumCull(View);
        
        // Perform distance culling
        int32 NumDistanceCulled = DistanceCull(View);
        
        // Perform occlusion culling (if enabled)
        OcclusionCull(View, RHICmdList);
        
        // Compute view relevance for visible primitives
        ComputeViewRelevance(View);
        
        // Mark visibility as computed
        View.bVisibilityComputed = true;
        
        MR_LOG_DEBUG(LogRenderer, "View %d visibility: %d primitives, %d frustum culled, %d distance culled",
                     ViewIndex, NumPrimitives, NumFrustumCulled, NumDistanceCulled);
    }
}

void FSceneRenderer::ComputeLightVisibility()
{
    if (!Scene)
    {
        return;
    }
    
    VisibleLightInfos.Empty();
    
    int32 NumLights = Scene->GetNumLights();
    for (int32 LightIndex = 0; LightIndex < NumLights; ++LightIndex)
    {
        FLightSceneInfo* LightSceneInfo = Scene->GetLight(LightIndex);
        if (!LightSceneInfo || !LightSceneInfo->bVisible)
        {
            continue;
        }
        
        FLightSceneProxy* LightProxy = LightSceneInfo->GetProxy();
        if (!LightProxy || !LightProxy->bAffectsWorld)
        {
            continue;
        }
        
        // Check if light affects any view
        bool bAffectsAnyView = false;
        
        if (LightProxy->IsDirectionalLight())
        {
            // Directional lights always affect all views
            bAffectsAnyView = true;
        }
        else
        {
            // Check light bounds against view frustums
            Math::FSphere LightBounds = LightProxy->GetBoundingSphere();
            
            for (const FViewInfo& View : Views)
            {
                if (View.ViewFrustum.IntersectSphere(LightBounds.Center, LightBounds.W))
                {
                    bAffectsAnyView = true;
                    break;
                }
            }
        }
        
        if (bAffectsAnyView)
        {
            int32 LightIdx = VisibleLightInfos.Add(FVisibleLightInfo());
            FVisibleLightInfo& VisibleLight = VisibleLightInfos[LightIdx];
            VisibleLight.LightIndex = LightIndex;
            VisibleLight.LightSceneInfo = LightSceneInfo;
            VisibleLight.bAffectsView = true;
        }
    }
    
    MR_LOG_DEBUG(LogRenderer, "ComputeLightVisibility: %d visible lights out of %d",
                 VisibleLightInfos.Num(), NumLights);
}

void FSceneRenderer::PostVisibilityFrameSetup()
{
    // Count visible primitives
    for (FViewInfo& View : Views)
    {
        View.NumVisibleDynamicPrimitives = 0;
        View.NumVisibleStaticMeshElements = 0;
        
        for (int32 i = 0; i < View.PrimitiveVisibilityMap.Num(); ++i)
        {
            if (View.PrimitiveVisibilityMap[i])
            {
                View.NumVisibleDynamicPrimitives++;
            }
        }
    }
    
    MR_LOG_DEBUG(LogRenderer, "PostVisibilityFrameSetup complete");
}

// ============================================================================
// Culling Methods
// ============================================================================

int32 FSceneRenderer::FrustumCull(FViewInfo& View)
{
    if (!Scene)
    {
        return 0;
    }
    
    int32 NumCulled = 0;
    const TArray<FPrimitiveBounds>& PrimitiveBounds = Scene->GetPrimitiveBounds();
    
    for (int32 PrimitiveIndex = 0; PrimitiveIndex < PrimitiveBounds.Num(); ++PrimitiveIndex)
    {
        const FPrimitiveBounds& Bounds = PrimitiveBounds[PrimitiveIndex];
        
        // Test against view frustum
        bool bVisible = View.ViewFrustum.IntersectBounds(Bounds.BoxSphereBounds);
        
        if (bVisible)
        {
            View.SetPrimitiveVisibility(PrimitiveIndex, true);
        }
        else
        {
            View.SetPrimitiveVisibility(PrimitiveIndex, false);
            NumCulled++;
        }
    }
    
    return NumCulled;
}

void FSceneRenderer::OcclusionCull(FViewInfo& View, RHI::IRHICommandList& RHICmdList)
{
    // Occlusion culling is optional and requires additional setup
    // For now, this is a placeholder
}

int32 FSceneRenderer::DistanceCull(FViewInfo& View)
{
    if (!Scene)
    {
        return 0;
    }
    
    int32 NumCulled = 0;
    const TArray<FPrimitiveBounds>& PrimitiveBounds = Scene->GetPrimitiveBounds();
    const Math::FVector& ViewOrigin = View.GetViewOrigin();
    
    for (int32 PrimitiveIndex = 0; PrimitiveIndex < PrimitiveBounds.Num(); ++PrimitiveIndex)
    {
        // Skip already culled primitives
        if (!View.IsPrimitiveVisible(PrimitiveIndex))
        {
            continue;
        }
        
        const FPrimitiveBounds& Bounds = PrimitiveBounds[PrimitiveIndex];
        
        // Calculate distance squared
        float DistanceSquared = (Bounds.BoxSphereBounds.Origin - ViewOrigin).SizeSquared();
        
        // Check distance culling
        if (View.IsDistanceCulled(DistanceSquared, Bounds.MinDrawDistance, Bounds.MaxCullDistance))
        {
            View.SetPrimitiveVisibility(PrimitiveIndex, false);
            NumCulled++;
        }
    }
    
    return NumCulled;
}

void FSceneRenderer::ComputeViewRelevance(FViewInfo& View)
{
    if (!Scene)
    {
        return;
    }
    
    for (int32 PrimitiveIndex = 0; PrimitiveIndex < Scene->GetNumPrimitives(); ++PrimitiveIndex)
    {
        if (!View.IsPrimitiveVisible(PrimitiveIndex))
        {
            continue;
        }
        
        FPrimitiveSceneInfo* PrimitiveSceneInfo = Scene->GetPrimitive(PrimitiveIndex);
        if (!PrimitiveSceneInfo || !PrimitiveSceneInfo->Proxy)
        {
            continue;
        }
        
        // Get view relevance from the proxy
        FPrimitiveViewRelevance ViewRelevance = PrimitiveSceneInfo->Proxy->GetViewRelevance(&View);
        
        // Store in the view's relevance map
        if (PrimitiveIndex < View.PrimitiveViewRelevanceMap.Num())
        {
            View.PrimitiveViewRelevanceMap[PrimitiveIndex] = ViewRelevance;
        }
        
        // Update view flags based on relevance
        if (ViewRelevance.HasTranslucency())
        {
            View.bHasTranslucentPrimitives = true;
        }
        if (ViewRelevance.bDistortionRelevance)
        {
            View.bHasDistortionPrimitives = true;
        }
        if (ViewRelevance.bRenderCustomDepth)
        {
            View.bHasCustomDepthPrimitives = true;
        }
    }
}

// ============================================================================
// Mesh Gathering
// ============================================================================

void FSceneRenderer::GatherDynamicMeshElements()
{
    if (!Scene)
    {
        return;
    }
    
    MeshCollector.ClearMeshes();
    
    for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
    {
        FViewInfo& View = Views[ViewIndex];
        
        // Gather mesh elements from visible primitives
        for (int32 PrimitiveIndex = 0; PrimitiveIndex < Scene->GetNumPrimitives(); ++PrimitiveIndex)
        {
            if (!View.IsPrimitiveVisible(PrimitiveIndex))
            {
                continue;
            }
            
            FPrimitiveSceneInfo* PrimitiveSceneInfo = Scene->GetPrimitive(PrimitiveIndex);
            if (!PrimitiveSceneInfo || !PrimitiveSceneInfo->Proxy)
            {
                continue;
            }
            
            // Get view relevance
            FPrimitiveViewRelevance ViewRelevance;
            if (PrimitiveIndex < View.PrimitiveViewRelevanceMap.Num())
            {
                ViewRelevance = View.PrimitiveViewRelevanceMap[PrimitiveIndex];
            }
            
            // Request dynamic mesh elements from the proxy
            TArray<const FViewInfo*> ViewArray;
            ViewArray.Add(&View);
            
            uint32 VisibilityMap = 1 << ViewIndex;
            PrimitiveSceneInfo->Proxy->GetDynamicMeshElements(ViewArray, ViewFamily, VisibilityMap, MeshCollector);
        }
    }
    
    MR_LOG_DEBUG(LogRenderer, "GatherDynamicMeshElements: collected %d mesh batches",
                 MeshCollector.GetNumMeshBatches());
}

void FSceneRenderer::SetupMeshPass(FViewInfo& View, FViewCommands& ViewCommands)
{
    // Setup mesh commands for each pass
    ViewCommands.Reset();
    
    // Process collected mesh batches
    const TArray<FMeshBatch>& MeshBatches = MeshCollector.GetMeshBatches();
    
    for (const FMeshBatch& MeshBatch : MeshBatches)
    {
        if (!MeshBatch.IsValid())
        {
            continue;
        }
        
        // Add to appropriate passes based on material properties
        // For now, add all to base pass
        ViewCommands.AddMeshCommand(EMeshPass::BasePass, MeshBatch);
        
        // Add to depth pass if needed
        if (MeshBatch.bCastShadow)
        {
            ViewCommands.AddMeshCommand(EMeshPass::DepthPass, MeshBatch);
        }
    }
}

// ============================================================================
// Shadow Setup
// ============================================================================

void FSceneRenderer::InitDynamicShadows()
{
    // Initialize shadow data structures
    MR_LOG_DEBUG(LogRenderer, "InitDynamicShadows");
}

void FSceneRenderer::GatherShadowPrimitives()
{
    // Gather primitives that cast shadows
    MR_LOG_DEBUG(LogRenderer, "GatherShadowPrimitives");
}

// ============================================================================
// Helper Methods
// ============================================================================

void FSceneRenderer::GatherSimpleLights()
{
    // Gather simple lights from visible primitives
}

void FSceneRenderer::InitFogConstants()
{
    // Initialize fog constants for each view
}

bool FSceneRenderer::ShouldRenderTranslucency() const
{
    for (const FViewInfo& View : Views)
    {
        if (View.bHasTranslucentPrimitives)
        {
            return true;
        }
    }
    return false;
}

void FSceneRenderer::RenderFinish(RHI::IRHICommandList& RHICmdList)
{
    // Cleanup after rendering
    MR_LOG_DEBUG(LogRenderer, "RenderFinish");
}

// ============================================================================
// FDeferredShadingSceneRenderer Implementation
// ============================================================================

FDeferredShadingSceneRenderer::FDeferredShadingSceneRenderer(const FSceneViewFamily* InViewFamily)
    : FSceneRenderer(InViewFamily)
    , bUseEarlyZPass(true)
    , bUseDeferredLighting(true)
    , bUseSSAO(true)
    , bUseSSR(false)
    , bUseMotionBlur(false)
    , bUseBloom(true)
    , bUseToneMapping(true)
{
    MR_LOG_DEBUG(LogRenderer, "FDeferredShadingSceneRenderer created");
}

FDeferredShadingSceneRenderer::~FDeferredShadingSceneRenderer()
{
    MR_LOG_DEBUG(LogRenderer, "FDeferredShadingSceneRenderer destroyed");
}

void FDeferredShadingSceneRenderer::Render(RHI::IRHICommandList& RHICmdList)
{
    MR_LOG_DEBUG(LogRenderer, "FDeferredShadingSceneRenderer::Render begin");
    
    // Pre-visibility setup
    PreVisibilityFrameSetup();
    
    // Compute visibility
    ComputeViewVisibility(RHICmdList);
    ComputeLightVisibility();
    
    // Post-visibility setup
    PostVisibilityFrameSetup();
    
    // Gather dynamic mesh elements
    PreGatherDynamicMeshElements();
    GatherDynamicMeshElements();
    
    // Initialize shadows
    InitDynamicShadows();
    
    // Render shadow depth maps
    RenderShadowDepthMaps(RHICmdList);
    
    // Render depth prepass (if enabled)
    if (bUseEarlyZPass)
    {
        RenderPrePass(RHICmdList);
    }
    
    // Render base pass (GBuffer fill)
    RenderBasePass(RHICmdList);
    
    // Render ambient occlusion
    if (bUseSSAO)
    {
        RenderAmbientOcclusion(RHICmdList);
    }
    
    // Render lighting
    RenderLights(RHICmdList);
    
    // Render sky atmosphere
    RenderSkyAtmosphere(RHICmdList);
    
    // Render fog
    if (ViewFamily.bRenderFog)
    {
        RenderFog(RHICmdList);
    }
    
    // Render translucency
    if (ShouldRenderTranslucency())
    {
        RenderTranslucency(RHICmdList);
    }
    
    // Render post processing
    if (ViewFamily.bRenderPostProcessing)
    {
        RenderPostProcessing(RHICmdList);
    }
    
    // Finish rendering
    RenderFinish(RHICmdList);
    
    MR_LOG_DEBUG(LogRenderer, "FDeferredShadingSceneRenderer::Render end");
}

void FDeferredShadingSceneRenderer::RenderHitProxies(RHI::IRHICommandList& RHICmdList)
{
    MR_LOG_DEBUG(LogRenderer, "RenderHitProxies");
}

bool FDeferredShadingSceneRenderer::ShouldRenderVelocities() const
{
    return bUseMotionBlur && ViewFamily.bRenderMotionBlur;
}

bool FDeferredShadingSceneRenderer::ShouldRenderPrePass() const
{
    return bUseEarlyZPass;
}

void FDeferredShadingSceneRenderer::RenderPrePass(RHI::IRHICommandList& RHICmdList)
{
    MR_LOG_DEBUG(LogRenderer, "RenderPrePass");
    // Render depth-only pass for early-Z optimization
}

void FDeferredShadingSceneRenderer::RenderBasePass(RHI::IRHICommandList& RHICmdList)
{
    MR_LOG_DEBUG(LogRenderer, "RenderBasePass");
    // Render GBuffer fill pass
}

void FDeferredShadingSceneRenderer::RenderLights(RHI::IRHICommandList& RHICmdList)
{
    MR_LOG_DEBUG(LogRenderer, "RenderLights: %d visible lights", VisibleLightInfos.Num());
    // Render deferred lighting
}

void FDeferredShadingSceneRenderer::RenderTranslucency(RHI::IRHICommandList& RHICmdList)
{
    MR_LOG_DEBUG(LogRenderer, "RenderTranslucency");
    // Render translucent objects
}

void FDeferredShadingSceneRenderer::RenderAmbientOcclusion(RHI::IRHICommandList& RHICmdList)
{
    MR_LOG_DEBUG(LogRenderer, "RenderAmbientOcclusion");
    // Render SSAO
}

void FDeferredShadingSceneRenderer::RenderSkyAtmosphere(RHI::IRHICommandList& RHICmdList)
{
    MR_LOG_DEBUG(LogRenderer, "RenderSkyAtmosphere");
    // Render sky and atmosphere
}

void FDeferredShadingSceneRenderer::RenderFog(RHI::IRHICommandList& RHICmdList)
{
    MR_LOG_DEBUG(LogRenderer, "RenderFog");
    // Render volumetric fog
}

void FDeferredShadingSceneRenderer::RenderPostProcessing(RHI::IRHICommandList& RHICmdList)
{
    MR_LOG_DEBUG(LogRenderer, "RenderPostProcessing");
    // Render post processing effects
}

void FDeferredShadingSceneRenderer::RenderShadowDepthMaps(RHI::IRHICommandList& RHICmdList)
{
    MR_LOG_DEBUG(LogRenderer, "RenderShadowDepthMaps");
    // Render shadow depth maps
}

void FDeferredShadingSceneRenderer::RenderShadowProjections(RHI::IRHICommandList& RHICmdList)
{
    MR_LOG_DEBUG(LogRenderer, "RenderShadowProjections");
    // Render shadow projections
}

// ============================================================================
// FForwardShadingSceneRenderer Implementation
// ============================================================================

FForwardShadingSceneRenderer::FForwardShadingSceneRenderer(const FSceneViewFamily* InViewFamily)
    : FSceneRenderer(InViewFamily)
{
    MR_LOG_DEBUG(LogRenderer, "FForwardShadingSceneRenderer created");
}

FForwardShadingSceneRenderer::~FForwardShadingSceneRenderer()
{
    MR_LOG_DEBUG(LogRenderer, "FForwardShadingSceneRenderer destroyed");
}

void FForwardShadingSceneRenderer::Render(RHI::IRHICommandList& RHICmdList)
{
    MR_LOG_DEBUG(LogRenderer, "FForwardShadingSceneRenderer::Render begin");
    
    // Pre-visibility setup
    PreVisibilityFrameSetup();
    
    // Compute visibility
    ComputeViewVisibility(RHICmdList);
    ComputeLightVisibility();
    
    // Post-visibility setup
    PostVisibilityFrameSetup();
    
    // Gather dynamic mesh elements
    GatherDynamicMeshElements();
    
    // Render forward pass
    RenderForwardPass(RHICmdList);
    
    // Render translucency
    if (ShouldRenderTranslucency())
    {
        RenderTranslucency(RHICmdList);
    }
    
    // Finish rendering
    RenderFinish(RHICmdList);
    
    MR_LOG_DEBUG(LogRenderer, "FForwardShadingSceneRenderer::Render end");
}

bool FForwardShadingSceneRenderer::ShouldRenderVelocities() const
{
    return false;
}

bool FForwardShadingSceneRenderer::ShouldRenderPrePass() const
{
    return false;
}

void FForwardShadingSceneRenderer::RenderForwardPass(RHI::IRHICommandList& RHICmdList)
{
    MR_LOG_DEBUG(LogRenderer, "RenderForwardPass");
    // Render forward shading pass
}

void FForwardShadingSceneRenderer::RenderTranslucency(RHI::IRHICommandList& RHICmdList)
{
    MR_LOG_DEBUG(LogRenderer, "RenderTranslucency (Forward)");
    // Render translucent objects
}

} // namespace MonsterEngine
