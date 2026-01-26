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
#include "Renderer/ShadowRendering.h"
#include "Renderer/ShadowDepthPass.h"
#include "Renderer/ShadowProjectionPass.h"
#include "Renderer/ForwardRenderPasses.h"
#include "Core/Logging/Logging.h"
#include "Math/MathFunctions.h"
#include "RHI/IRHICommandList.h"
#include "RHI/IRHIDevice.h"
#include "RHI/RHIDefinitions.h"
#include <cstdlib>

using namespace MonsterRender;

namespace MonsterEngine
{
namespace Renderer
{

// ============================================================================
// FSceneRenderer Implementation
// ============================================================================

FSceneRenderer::FSceneRenderer(const FSceneViewFamily* InViewFamily)
    : Scene(nullptr)
    , ShadowSceneRenderer(nullptr)
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
    
    MR_LOG(LogRenderer, Verbose, "FSceneRenderer created");
}

FSceneRenderer::~FSceneRenderer()
{
    MR_LOG(LogRenderer, Verbose, "FSceneRenderer destroyed");
}

FSceneRenderer* FSceneRenderer::CreateSceneRenderer(const FSceneViewFamily* InViewFamily)
{
    if (!InViewFamily)
    {
        MR_LOG(LogRenderer, Error, "Cannot create scene renderer with null view family");
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
    
    MR_LOG(LogRenderer, Verbose, "RenderThreadBegin: %d views, family size: %dx%d",
                 Views.Num(), FamilySize.X, FamilySize.Y);
}

void FSceneRenderer::RenderThreadEnd(RHI::IRHICommandList& RHICmdList)
{
    // Cleanup after rendering
    MeshCollector.ClearMeshes();
    
    MR_LOG(LogRenderer, Verbose, "RenderThreadEnd");
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
        
        FamilySize.X = Math::FMath::Max(FamilySize.X, ViewRight);
        FamilySize.Y = Math::FMath::Max(FamilySize.Y, ViewBottom);
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
    
    MR_LOG(LogRenderer, Verbose, "PreVisibilityFrameSetup complete");
}

void FSceneRenderer::ComputeViewVisibility(RHI::IRHICommandList& RHICmdList)
{
    if (!Scene)
    {
        MR_LOG(LogRenderer, Warning, "ComputeViewVisibility: No scene");
        return;
    }
    
    int32 NumPrimitives = Scene->GetNumPrimitives();
    if (NumPrimitives == 0)
    {
        MR_LOG(LogRenderer, Verbose, "ComputeViewVisibility: No primitives in scene");
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
        
        MR_LOG(LogRenderer, Verbose, "View %d visibility: %d primitives, %d frustum culled, %d distance culled",
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
    
    MR_LOG(LogRenderer, Verbose, "ComputeLightVisibility: %d visible lights out of %d",
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
    
    MR_LOG(LogRenderer, Verbose, "PostVisibilityFrameSetup complete");
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
    
    MR_LOG(LogRenderer, Verbose, "GatherDynamicMeshElements: collected %d mesh batches",
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
    MR_LOG(LogRenderer, Verbose, "InitDynamicShadows begin");
    
    // Clear previous frame's shadow data
    VisibleProjectedShadows.Empty();
    
    // Skip if no scene or no visible lights
    if (!Scene || VisibleLightInfos.Num() == 0)
    {
        MR_LOG(LogRenderer, Verbose, "InitDynamicShadows - No scene or no visible lights");
        return;
    }
    
    // Shadow map configuration
    const uint32 DefaultShadowResolution = 1024;
    const uint32 ShadowBorder = 4;
    const float DefaultShadowDistance = 5000.0f;
    
    // Iterate through visible lights and setup shadows
    for (int32 LightIndex = 0; LightIndex < VisibleLightInfos.Num(); ++LightIndex)
    {
        FVisibleLightInfo& VisibleLightInfo = VisibleLightInfos[LightIndex];
        FLightSceneInfo* LightSceneInfo = VisibleLightInfo.LightSceneInfo;
        
        if (!LightSceneInfo)
        {
            continue;
        }
        
        // Get light proxy for light type and shadow settings
        FLightSceneProxy* LightProxy = LightSceneInfo->GetProxy();
        if (!LightProxy)
        {
            continue;
        }
        
        // Check if light casts dynamic shadows
        if (!LightProxy->bCastShadows)
        {
            continue;
        }
        
        // Get light type (using Renderer::FLightSceneProxy::ELightType)
        FLightSceneProxy::ELightType LightType = LightProxy->GetLightType();
        
        // Create shadow based on light type
        switch (LightType)
        {
            case FLightSceneProxy::ELightType::Directional:
            {
                // Create directional light shadow
                _createDirectionalLightShadow(LightSceneInfo, LightProxy, DefaultShadowResolution, ShadowBorder, DefaultShadowDistance);
                break;
            }
            case FLightSceneProxy::ELightType::Point:
            {
                // TODO: Create point light shadow (cube map) - reserved for future implementation
                MR_LOG(LogRenderer, Verbose, "InitDynamicShadows - Point light shadow not yet implemented");
                break;
            }
            case FLightSceneProxy::ELightType::Spot:
            {
                // TODO: Create spot light shadow - reserved for future implementation
                MR_LOG(LogRenderer, Verbose, "InitDynamicShadows - Spot light shadow not yet implemented");
                break;
            }
            default:
            {
                MR_LOG(LogRenderer, Warning, "InitDynamicShadows - Unknown light type: %d", static_cast<int32>(LightType));
                break;
            }
        }
        
        MR_LOG(LogRenderer, Verbose, "InitDynamicShadows - Processed light %d, type: %d", 
               LightIndex, static_cast<int32>(LightType));
    }
    
    // Gather shadow primitives
    GatherShadowPrimitives();
    
    MR_LOG(LogRenderer, Verbose, "InitDynamicShadows end - %d shadows setup", VisibleProjectedShadows.Num());
}

void FSceneRenderer::GatherShadowPrimitives()
{
    MR_LOG(LogRenderer, Verbose, "GatherShadowPrimitives begin");
    
    // Skip if no shadows to render
    if (VisibleProjectedShadows.Num() == 0)
    {
        return;
    }
    
    // Iterate through each shadow and gather primitives
    for (FProjectedShadowInfo* ShadowInfo : VisibleProjectedShadows)
    {
        if (!ShadowInfo)
        {
            continue;
        }
        
        // TODO: Gather primitives that are within the shadow frustum
        // and add them to ShadowInfo->DynamicSubjectPrimitives
        
        // For each view, check which primitives cast shadows into this shadow map
        for (const FViewInfo& View : Views)
        {
            // TODO: Frustum cull primitives against shadow frustum
            // Add shadow-casting primitives to the shadow info
        }
    }
    
    MR_LOG(LogRenderer, Verbose, "GatherShadowPrimitives end");
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
    MR_LOG(LogRenderer, Verbose, "RenderFinish");
}

void FSceneRenderer::_createDirectionalLightShadow(
    FLightSceneInfo* LightSceneInfo,
    FLightSceneProxy* LightProxy,
    uint32 ShadowResolution,
    uint32 ShadowBorder,
    float ShadowDistance)
{
    if (!LightSceneInfo || !LightProxy)
    {
        MR_LOG(LogRenderer, Warning, "_createDirectionalLightShadow - Invalid light info or proxy");
        return;
    }
    
    // Get light direction from proxy
    FVector LightDirection = LightProxy->GetDirection();
    
    // For each view, create a shadow
    // Note: For CSM, we would create multiple shadows per view (one per cascade)
    // Currently we create a single shadow per directional light
    for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
    {
        FViewInfo& View = Views[ViewIndex];
        
        // Compute shadow bounds based on view frustum and shadow distance
        FSphere ShadowBounds = _computeDirectionalLightShadowBounds(View, LightDirection, ShadowDistance);
        
        // Create projected shadow info
        // Allocate memory for shadow info
        void* ShadowMemory = std::malloc(sizeof(FProjectedShadowInfo));
        if (!ShadowMemory)
        {
            MR_LOG(LogRenderer, Error, "_createDirectionalLightShadow - Failed to allocate memory");
            continue;
        }
        FProjectedShadowInfo* ShadowInfo = static_cast<FProjectedShadowInfo*>(ShadowMemory);
        
        if (!ShadowInfo)
        {
            MR_LOG(LogRenderer, Error, "_createDirectionalLightShadow - Failed to allocate FProjectedShadowInfo");
            continue;
        }
        
        // Placement new to construct the object
        new (ShadowInfo) FProjectedShadowInfo();
        
        // Setup directional light shadow with computed bounds
        bool bSuccess = ShadowInfo->setupDirectionalLightShadow(
            LightSceneInfo,
            &View,
            LightDirection,
            ShadowBounds,
            ShadowResolution,
            ShadowResolution,
            ShadowBorder,
            -1  // Single shadow, no cascade index
        );
        
        if (bSuccess)
        {
            // Add to visible projected shadows
            VisibleProjectedShadows.Add(ShadowInfo);
            
            MR_LOG(LogRenderer, Log, 
                   "_createDirectionalLightShadow - Created shadow for view %d, resolution: %ux%u",
                   ViewIndex, ShadowResolution, ShadowResolution);
        }
        else
        {
            // Cleanup on failure
            ShadowInfo->~FProjectedShadowInfo();
            std::free(ShadowInfo);
            
            MR_LOG(LogRenderer, Warning, 
                   "_createDirectionalLightShadow - Failed to setup shadow for view %d", ViewIndex);
        }
    }
}

FSphere FSceneRenderer::_computeDirectionalLightShadowBounds(
    const FViewInfo& View,
    const FVector& LightDirection,
    float ShadowDistance) const
{
    // Compute shadow bounds based on view frustum
    // The shadow bounds should encompass the visible area that needs shadows
    
    // Get view origin and direction
    FVector ViewOrigin = View.ViewMatrices.ViewOrigin;
    FVector ViewDirection = View.ViewMatrices.ViewForward;
    
    // Compute shadow center - offset from view origin along view direction
    // Place the shadow center at half the shadow distance in front of the camera
    float HalfShadowDistance = ShadowDistance * 0.5f;
    FVector ShadowCenter = ViewOrigin + ViewDirection * HalfShadowDistance;
    
    // Shadow radius should cover the shadow distance
    // Use a slightly larger radius to ensure full coverage
    float ShadowRadius = ShadowDistance * 0.6f;
    
    // Adjust shadow center along light direction to ensure proper depth coverage
    // Move the center back along the light direction by the radius
    // This ensures objects behind the camera can still cast shadows
    FVector NormalizedLightDir = LightDirection.GetSafeNormal();
    ShadowCenter = ShadowCenter - NormalizedLightDir * ShadowRadius * 0.5f;
    
    MR_LOG(LogRenderer, Verbose, 
           "_computeDirectionalLightShadowBounds - Center: (%.1f, %.1f, %.1f), Radius: %.1f",
           ShadowCenter.X, ShadowCenter.Y, ShadowCenter.Z, ShadowRadius);
    
    return FSphere(ShadowCenter, ShadowRadius);
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
    MR_LOG(LogRenderer, Verbose, "FDeferredShadingSceneRenderer created");
}

FDeferredShadingSceneRenderer::~FDeferredShadingSceneRenderer()
{
    MR_LOG(LogRenderer, Verbose, "FDeferredShadingSceneRenderer destroyed");
}

void FDeferredShadingSceneRenderer::Render(RHI::IRHICommandList& RHICmdList)
{
    MR_LOG(LogRenderer, Verbose, "FDeferredShadingSceneRenderer::Render begin");
    
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
    
    MR_LOG(LogRenderer, Verbose, "FDeferredShadingSceneRenderer::Render end");
}

void FDeferredShadingSceneRenderer::RenderHitProxies(RHI::IRHICommandList& RHICmdList)
{
    MR_LOG(LogRenderer, Verbose, "RenderHitProxies");
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
    MR_LOG(LogRenderer, Verbose, "RenderPrePass");
    // Render depth-only pass for early-Z optimization
}

void FDeferredShadingSceneRenderer::RenderBasePass(RHI::IRHICommandList& RHICmdList)
{
    MR_LOG(LogRenderer, Verbose, "RenderBasePass");
    // Render GBuffer fill pass
}

void FDeferredShadingSceneRenderer::RenderLights(RHI::IRHICommandList& RHICmdList)
{
    MR_LOG(LogRenderer, Verbose, "RenderLights: %d visible lights", VisibleLightInfos.Num());
    // Render deferred lighting
}

void FDeferredShadingSceneRenderer::RenderTranslucency(RHI::IRHICommandList& RHICmdList)
{
    MR_LOG(LogRenderer, Verbose, "RenderTranslucency");
    // Render translucent objects
}

void FDeferredShadingSceneRenderer::RenderAmbientOcclusion(RHI::IRHICommandList& RHICmdList)
{
    MR_LOG(LogRenderer, Verbose, "RenderAmbientOcclusion");
    // Render SSAO
}

void FDeferredShadingSceneRenderer::RenderSkyAtmosphere(RHI::IRHICommandList& RHICmdList)
{
    MR_LOG(LogRenderer, Verbose, "RenderSkyAtmosphere");
    // Render sky and atmosphere
}

void FDeferredShadingSceneRenderer::RenderFog(RHI::IRHICommandList& RHICmdList)
{
    MR_LOG(LogRenderer, Verbose, "RenderFog");
    // Render volumetric fog
}

void FDeferredShadingSceneRenderer::RenderPostProcessing(RHI::IRHICommandList& RHICmdList)
{
    MR_LOG(LogRenderer, Verbose, "RenderPostProcessing");
    // Render post processing effects
}

void FDeferredShadingSceneRenderer::RenderShadowDepthMaps(RHI::IRHICommandList& RHICmdList)
{
    MR_LOG(LogRenderer, Verbose, "RenderShadowDepthMaps begin");
    
    // Skip if no shadows to render
    if (VisibleProjectedShadows.Num() == 0)
    {
        MR_LOG(LogRenderer, Verbose, "RenderShadowDepthMaps - No shadows to render");
        return;
    }
    
    // Begin debug event for shadow depth rendering
    RHICmdList.beginEvent("ShadowDepthMaps");
    
    // Get RHI device for render target allocation
    RHI::IRHIDevice* RHIDevice = nullptr;
    if (Scene)
    {
        RHIDevice = Scene->getRHIDevice();
    }
    
    // Track number of shadows rendered
    int32 ShadowsRendered = 0;
    
    // Render each shadow depth map
    for (FProjectedShadowInfo* ShadowInfo : VisibleProjectedShadows)
    {
        if (!ShadowInfo)
        {
            continue;
        }
        
        // Allocate render targets if not already allocated
        if (!ShadowInfo->hasRenderTargets() && RHIDevice)
        {
            if (!ShadowInfo->allocateRenderTargets(RHIDevice))
            {
                MR_LOG(LogRenderer, Warning, 
                       "RenderShadowDepthMaps - Failed to allocate render targets for shadow %d",
                       ShadowInfo->ShadowId);
                continue;
            }
        }
        
        // Skip if still no render targets
        if (!ShadowInfo->hasRenderTargets())
        {
            MR_LOG(LogRenderer, Warning, 
                   "RenderShadowDepthMaps - No render targets for shadow %d",
                   ShadowInfo->ShadowId);
            continue;
        }
        
        MR_LOG(LogRenderer, Verbose, "Rendering shadow depth map %d: resolution=%ux%u",
               ShadowInfo->ShadowId, ShadowInfo->ResolutionX, ShadowInfo->ResolutionY);
        
        // Render shadow depth directly using FProjectedShadowInfo::renderDepth
        ShadowInfo->renderDepth(RHICmdList, this);
        
        ++ShadowsRendered;
    }
    
    // End debug event
    RHICmdList.endEvent();
    
    MR_LOG(LogRenderer, Verbose, "RenderShadowDepthMaps end - %d/%d shadows rendered", 
           ShadowsRendered, VisibleProjectedShadows.Num());
}

void FDeferredShadingSceneRenderer::RenderShadowProjections(RHI::IRHICommandList& RHICmdList)
{
    MR_LOG(LogRenderer, Verbose, "RenderShadowProjections begin");
    
    // Skip if no shadows to project
    if (VisibleProjectedShadows.Num() == 0)
    {
        MR_LOG(LogRenderer, Verbose, "RenderShadowProjections - No shadows to project");
        return;
    }
    
    // Begin debug event
    RHICmdList.beginEvent("ShadowProjections");
    
    // Track number of shadows projected
    int32 ShadowsProjected = 0;
    
    // Project shadows for each view
    for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
    {
        FViewInfo& View = Views[ViewIndex];
        
        MR_LOG(LogRenderer, Verbose, "Projecting shadows for view %d", ViewIndex);
        
        // Project each shadow onto the view
        for (FProjectedShadowInfo* ShadowInfo : VisibleProjectedShadows)
        {
            if (!ShadowInfo || !ShadowInfo->bAllocated || !ShadowInfo->bRendered)
            {
                continue;
            }
            
            // Check if shadow has valid render targets
            if (!ShadowInfo->hasRenderTargets())
            {
                continue;
            }
            
            MR_LOG(LogRenderer, Verbose, 
                   "Projecting shadow %d: resolution=%ux%u, directional=%s",
                   ShadowInfo->ShadowId, 
                   ShadowInfo->ResolutionX, ShadowInfo->ResolutionY,
                   ShadowInfo->bDirectionalLight ? "true" : "false");
            
            // Project the shadow onto the scene
            // This samples the shadow depth map and computes shadow factors
            _projectShadowToView(RHICmdList, ShadowInfo, &View);
            
            ++ShadowsProjected;
        }
    }
    
    // End debug event
    RHICmdList.endEvent();
    
    MR_LOG(LogRenderer, Verbose, "RenderShadowProjections end - %d shadows projected", 
           ShadowsProjected);
}

void FDeferredShadingSceneRenderer::_projectShadowToView(
    RHI::IRHICommandList& RHICmdList,
    FProjectedShadowInfo* ShadowInfo,
    FViewInfo* View)
{
    if (!ShadowInfo || !View)
    {
        return;
    }
    
    // Begin debug event for this shadow
    RHICmdList.beginEvent("ProjectShadow");
    
    // Get shadow depth texture
    MonsterRender::RHI::IRHITexture* ShadowDepthTexture = ShadowInfo->RenderTargets.DepthTarget;
    if (!ShadowDepthTexture)
    {
        MR_LOG(LogRenderer, Warning, 
               "_projectShadowToView - Shadow %d has no depth texture",
               ShadowInfo->ShadowId);
        RHICmdList.endEvent();
        return;
    }
    
    // Compute screen to shadow matrix
    // This transforms screen space positions to shadow UV + depth
    FMatrix InvViewProj = View->ViewMatrices.InvViewProjectionMatrix;
    FMatrix ShadowWorldToClip = FMatrix(ShadowInfo->TranslatedWorldToClipOuterMatrix);
    FMatrix ScreenToShadow = InvViewProj * ShadowWorldToClip;
    
    // Set viewport to view rect
    using namespace MonsterRender::RHI;
    Viewport VP;
    VP.x = View->ViewRect.x;
    VP.y = View->ViewRect.y;
    VP.width = View->ViewRect.width;
    VP.height = View->ViewRect.height;
    VP.minDepth = 0.0f;
    VP.maxDepth = 1.0f;
    RHICmdList.setViewport(VP);
    
    // Set scissor rect
    ScissorRect Scissor;
    Scissor.left = static_cast<int32>(View->ViewRect.x);
    Scissor.top = static_cast<int32>(View->ViewRect.y);
    Scissor.right = static_cast<int32>(View->ViewRect.x + View->ViewRect.width);
    Scissor.bottom = static_cast<int32>(View->ViewRect.y + View->ViewRect.height);
    RHICmdList.setScissorRect(Scissor);
    
    // Set depth-stencil state (read depth, no write)
    RHICmdList.setDepthStencilState(false, false, 7);  // 7 = Always
    
    // Set blend state for shadow mask multiplication
    // Output = Dst * ShadowFactor (multiply blend)
    RHICmdList.setBlendState(
        true,   // Enable blend
        0,      // Zero (src color factor)
        3,      // SrcColor (dst color factor)
        0,      // Add
        0,      // Zero (src alpha)
        1,      // One (dst alpha)
        0,      // Add
        0x0F);  // RGBA write mask
    
    // Set rasterizer state (no culling for full screen)
    RHICmdList.setRasterizerState(0, 0, false, 0.0f, 0.0f);
    
    // Bind shadow depth texture
    TSharedPtr<IRHITexture> DepthTexture(ShadowDepthTexture, [](IRHITexture*){});
    RHICmdList.setShaderResource(0, DepthTexture);
    
    // TODO: Bind shadow projection shader and draw full screen quad
    // For now, we just log that we would project the shadow
    MR_LOG(LogRenderer, Verbose, 
           "_projectShadowToView - Would project shadow %d with matrix",
           ShadowInfo->ShadowId);
    
    // Draw full screen triangle (3 vertices)
    // The vertex shader generates positions from vertex ID
    // RHICmdList.draw(3, 0);
    
    // End debug event
    RHICmdList.endEvent();
}

// ============================================================================
// FForwardShadingSceneRenderer Implementation
// ============================================================================

FForwardShadingSceneRenderer::FForwardShadingSceneRenderer(const FSceneViewFamily* InViewFamily)
    : FSceneRenderer(InViewFamily)
    , bDepthPrepassEnabled(false)
    , bSkyboxEnabled(false)
    , bShadowsEnabled(true)
{
    MR_LOG(LogRenderer, Log, "FForwardShadingSceneRenderer created");
    
    // Create render pass instances using MakeUnique
    DepthPrepass = MakeUnique<FDepthPrepass>();
    OpaquePass = MakeUnique<FOpaquePass>();
    SkyboxPass = MakeUnique<FSkyboxPass>();
    TransparentPass = MakeUnique<FTransparentPass>();
    ShadowDepthPass = MakeUnique<FShadowDepthPass>();
    
    // Configure shadow settings
    ShadowConfig.Resolution = 2048;
    ShadowConfig.Type = EShadowMapType::Standard2D;
    ShadowConfig.DepthBias = 0.005f;
    ShadowConfig.SlopeScaledDepthBias = 1.0f;
    
    // Reserve space for shadow data
    ShadowDataArray.Reserve(8);
}

FForwardShadingSceneRenderer::~FForwardShadingSceneRenderer()
{
    MR_LOG(LogRenderer, Log, "FForwardShadingSceneRenderer destroyed");
    
    // TUniquePtr will automatically clean up pass instances
}

void FForwardShadingSceneRenderer::Render(IRHICommandList& RHICmdList)
{
    MR_LOG(LogRenderer, Log, "FForwardShadingSceneRenderer::Render begin - Frame %u", ViewFamily.FrameNumber);
    
    // Reference: UE5 FMobileSceneRenderer::Render()
    // @E:\UnrealEngine\Engine\Source\Runtime\Renderer\Private\MobileShadingRenderer.cpp:821
    
    // Step 1: Pre-visibility setup
    PreVisibilityFrameSetup();
    
    // Step 2: Compute visibility (frustum culling, occlusion culling)
    ComputeViewVisibility(RHICmdList);
    ComputeLightVisibility();
    
    // Step 3: Post-visibility setup
    PostVisibilityFrameSetup();
    
    // Step 4: Gather dynamic mesh elements from visible primitives
    GatherDynamicMeshElements();
    
    // Step 5: Render shadow depth maps (if shadows enabled)
    if (bShadowsEnabled)
    {
        RenderShadowDepthMaps(RHICmdList);
    }
    
    // Step 6: Render depth prepass (if enabled)
    if (bDepthPrepassEnabled)
    {
        RenderPrePass(RHICmdList);
    }
    
    // Step 7: Render main forward pass (opaque geometry with lighting)
    RenderForwardPass(RHICmdList);
    
    // Step 8: Render skybox (if enabled)
    if (bSkyboxEnabled)
    {
        RenderSkybox(RHICmdList);
    }
    
    // Step 9: Render translucency
    if (ShouldRenderTranslucency())
    {
        RenderTranslucency(RHICmdList);
    }
    
    // Step 10: Render post-processing
    RenderPostProcessing(RHICmdList);
    
    // Step 11: Finish rendering
    RenderFinish(RHICmdList);
    
    MR_LOG(LogRenderer, Log, "FForwardShadingSceneRenderer::Render end - Frame %u", ViewFamily.FrameNumber);
}

bool FForwardShadingSceneRenderer::ShouldRenderVelocities() const
{
    // Forward shading doesn't support velocity rendering
    return false;
}

bool FForwardShadingSceneRenderer::ShouldRenderPrePass() const
{
    // Depth prepass is optional in forward rendering
    return bDepthPrepassEnabled;
}

// ============================================================================
// Rendering Pass Implementations
// ============================================================================

void FForwardShadingSceneRenderer::RenderShadowDepthMaps(IRHICommandList& RHICmdList)
{
    MR_LOG(LogRenderer, Verbose, "RenderShadowDepthMaps");
    
    if (!bShadowsEnabled || !ShadowDepthPass)
    {
        return;
    }
    
    // Gather shadow-casting lights
    TArray<FLightSceneInfo*> ShadowCastingLights;
    _gatherShadowCastingLights(ShadowCastingLights);
    
    if (ShadowCastingLights.Num() == 0)
    {
        MR_LOG(LogRenderer, Verbose, "No shadow-casting lights found");
        return;
    }
    
    MR_LOG(LogRenderer, Log, "Rendering shadow maps for %d lights", ShadowCastingLights.Num());
    
    // Clear shadow data array
    ShadowDataArray.Empty();
    ShadowDataArray.Reserve(ShadowCastingLights.Num());
    
    // Render shadow map for each light
    for (FLightSceneInfo* LightInfo : ShadowCastingLights)
    {
        if (!LightInfo || !LightInfo->GetProxy())
        {
            continue;
        }
        
        // Setup shadow pass for this light
        ShadowDepthPass->SetLight(LightInfo);
        ShadowDepthPass->SetShadowConfig(ShadowConfig);
        
        // Setup render pass context for each view
        for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
        {
            FViewInfo& ViewInfo = Views[ViewIndex];
            
            FRenderPassContext Context;
            Context.Scene = Scene;
            Context.View = &ViewInfo;
            Context.ViewIndex = ViewIndex;
            Context.RHICmdList = &RHICmdList;
            Context.FrameNumber = ViewFamily.FrameNumber;
            
            // Execute shadow depth pass
            if (ShadowDepthPass->ShouldExecute(Context))
            {
                ShadowDepthPass->Setup(Context);
                ShadowDepthPass->Execute(Context);
                ShadowDepthPass->Cleanup(Context);
                
                // Store generated shadow data
                const FShadowData& ShadowData = ShadowDepthPass->GetShadowData();
                if (ShadowData.bValid)
                {
                    ShadowDataArray.Add(ShadowData);
                }
            }
        }
    }
    
    MR_LOG(LogRenderer, Verbose, "Generated %d shadow maps", ShadowDataArray.Num());
}

void FForwardShadingSceneRenderer::RenderPrePass(IRHICommandList& RHICmdList)
{
    MR_LOG(LogRenderer, Verbose, "RenderPrePass (Depth Prepass)");
    
    if (!DepthPrepass)
    {
        return;
    }
    
    // Render depth prepass for each view
    for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
    {
        FViewInfo& ViewInfo = Views[ViewIndex];
        
        FRenderPassContext Context;
        Context.Scene = Scene;
        Context.View = &ViewInfo;
        Context.ViewIndex = ViewIndex;
        Context.RHICmdList = &RHICmdList;
        Context.FrameNumber = ViewFamily.FrameNumber;
        
        if (DepthPrepass->ShouldExecute(Context))
        {
            DepthPrepass->Setup(Context);
            DepthPrepass->Execute(Context);
            DepthPrepass->Cleanup(Context);
        }
    }
}

void FForwardShadingSceneRenderer::RenderForwardPass(IRHICommandList& RHICmdList)
{
    MR_LOG(LogRenderer, Verbose, "RenderForwardPass");
    
    // Render opaque geometry with forward lighting
    RenderOpaqueGeometry(RHICmdList);
}

void FForwardShadingSceneRenderer::RenderOpaqueGeometry(IRHICommandList& RHICmdList)
{
    MR_LOG(LogRenderer, Verbose, "RenderOpaqueGeometry");
    
    if (!OpaquePass)
    {
        return;
    }
    
    // Pass shadow data to opaque pass
    if (bShadowsEnabled && ShadowDataArray.Num() > 0)
    {
        OpaquePass->SetShadowData(ShadowDataArray);
    }
    
    // Render opaque geometry for each view
    for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
    {
        FViewInfo& ViewInfo = Views[ViewIndex];
        
        FRenderPassContext Context;
        Context.Scene = Scene;
        Context.View = &ViewInfo;
        Context.ViewIndex = ViewIndex;
        Context.RHICmdList = &RHICmdList;
        Context.FrameNumber = ViewFamily.FrameNumber;
        
        if (OpaquePass->ShouldExecute(Context))
        {
            OpaquePass->Setup(Context);
            OpaquePass->Execute(Context);
            OpaquePass->Cleanup(Context);
        }
    }
}

void FForwardShadingSceneRenderer::RenderSkybox(IRHICommandList& RHICmdList)
{
    MR_LOG(LogRenderer, Verbose, "RenderSkybox");
    
    if (!SkyboxPass)
    {
        return;
    }
    
    // Render skybox for each view
    for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
    {
        FViewInfo& ViewInfo = Views[ViewIndex];
        
        FRenderPassContext Context;
        Context.Scene = Scene;
        Context.View = &ViewInfo;
        Context.ViewIndex = ViewIndex;
        Context.RHICmdList = &RHICmdList;
        Context.FrameNumber = ViewFamily.FrameNumber;
        
        if (SkyboxPass->ShouldExecute(Context))
        {
            SkyboxPass->Setup(Context);
            SkyboxPass->Execute(Context);
            SkyboxPass->Cleanup(Context);
        }
    }
}

void FForwardShadingSceneRenderer::RenderTranslucency(IRHICommandList& RHICmdList)
{
    MR_LOG(LogRenderer, Verbose, "RenderTranslucency");
    
    if (!TransparentPass)
    {
        return;
    }
    
    // Render translucent geometry for each view
    for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
    {
        FViewInfo& ViewInfo = Views[ViewIndex];
        
        FRenderPassContext Context;
        Context.Scene = Scene;
        Context.View = &ViewInfo;
        Context.ViewIndex = ViewIndex;
        Context.RHICmdList = &RHICmdList;
        Context.FrameNumber = ViewFamily.FrameNumber;
        
        if (TransparentPass->ShouldExecute(Context))
        {
            TransparentPass->Setup(Context);
            TransparentPass->Execute(Context);
            TransparentPass->Cleanup(Context);
        }
    }
}

void FForwardShadingSceneRenderer::RenderPostProcessing(IRHICommandList& RHICmdList)
{
    MR_LOG(LogRenderer, Verbose, "RenderPostProcessing");
    
    // TODO: Implement post-processing pipeline
    // - Tone mapping
    // - Bloom
    // - Color grading
    // - Anti-aliasing (FXAA/TAA)
}

// ============================================================================
// Helper Methods
// ============================================================================

void FForwardShadingSceneRenderer::_gatherShadowCastingLights(TArray<FLightSceneInfo*>& OutLights)
{
    OutLights.Empty();
    
    // Gather lights that cast dynamic shadows
    for (FVisibleLightInfo& LightInfo : VisibleLightInfos)
    {
        // TODO: Check if light casts shadows
        // For now, assume all visible lights can cast shadows
        // In UE5, this checks LightSceneInfo->Proxy->CastsDynamicShadow()
    }
    
    MR_LOG(LogRenderer, Verbose, "Gathered %d shadow-casting lights", OutLights.Num());
}

void FForwardShadingSceneRenderer::_setupLightBuffer(IRHICommandList& RHICmdList, const TArray<FLightSceneInfo*>& Lights)
{
    // TODO: Setup light uniform buffer for forward shading
    // This will pack light data into GPU-friendly format
    // Reference: UE5 FMobileSceneRenderer::UpdateDirectionalLightUniformBuffers
    
    MR_LOG(LogRenderer, Verbose, "Setup light buffer for %d lights", Lights.Num());
}

} // namespace Renderer
} // namespace MonsterEngine
