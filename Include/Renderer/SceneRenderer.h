// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file SceneRenderer.h
 * @brief Base scene renderer class
 * 
 * This file defines FSceneRenderer, the base class for all scene renderers.
 * Provides the framework for visibility computation, shadow setup, and rendering.
 * Reference: UE5 SceneRendering.h, FSceneRenderer
 */

#include "Core/CoreMinimal.h"
#include "Core/CoreTypes.h"
#include "Containers/Array.h"
#include "Renderer/SceneTypes.h"
#include "Renderer/SceneView.h"
#include "Renderer/Scene.h"

namespace MonsterEngine
{

// Forward declarations
class FScene;
class FViewInfo;
class FSceneViewState;
class FFrustumCuller;
class FOcclusionCuller;
class FMeshElementCollector;

namespace RHI
{
    class IRHIDevice;
    class IRHICommandList;
    class IRHITexture;
    class IRHIBuffer;
}

// ============================================================================
// FMeshElementCollector - Mesh Element Collection Helper
// ============================================================================

/**
 * @class FMeshElementCollector
 * @brief Collects mesh elements from primitives for rendering
 * 
 * Used during the mesh gathering phase to collect dynamic mesh elements
 * from visible primitives.
 * Reference: UE5 FMeshElementCollector
 */
class FMeshElementCollector
{
public:
    /** Constructor */
    FMeshElementCollector()
        : NumMeshBatches(0)
    {
    }
    
    /**
     * Allocate a mesh batch
     * @return Reference to the allocated mesh batch
     */
    FMeshBatch& AllocateMesh()
    {
        FMeshBatch& NewBatch = MeshBatches.AddDefaulted_GetRef();
        NumMeshBatches++;
        return NewBatch;
    }
    
    /**
     * Get the number of collected mesh batches
     */
    int32 GetNumMeshBatches() const { return NumMeshBatches; }
    
    /**
     * Get all collected mesh batches
     */
    const TArray<FMeshBatch>& GetMeshBatches() const { return MeshBatches; }
    
    /**
     * Clear all collected meshes
     */
    void ClearMeshes()
    {
        MeshBatches.Empty();
        NumMeshBatches = 0;
    }
    
private:
    /** Collected mesh batches */
    TArray<FMeshBatch> MeshBatches;
    
    /** Number of mesh batches */
    int32 NumMeshBatches;
};

// ============================================================================
// FSceneRenderer - Base Scene Renderer
// ============================================================================

/**
 * @class FSceneRenderer
 * @brief Base class for scene renderers
 * 
 * Provides the common framework for rendering a scene, including:
 * - View setup and management
 * - Visibility computation (frustum culling, occlusion culling)
 * - Shadow setup
 * - Light visibility
 * - Mesh element gathering
 * 
 * Derived classes implement specific rendering pipelines (deferred, forward, mobile).
 * Reference: UE5 FSceneRenderer
 */
class FSceneRenderer
{
public:
    // ========================================================================
    // Construction and Destruction
    // ========================================================================
    
    /**
     * Constructor
     * @param InViewFamily The view family to render
     */
    FSceneRenderer(const FSceneViewFamily* InViewFamily);
    
    /** Virtual destructor */
    virtual ~FSceneRenderer();
    
    /**
     * Factory method to create the appropriate scene renderer
     * @param InViewFamily The view family to render
     * @return The created scene renderer
     */
    static FSceneRenderer* CreateSceneRenderer(const FSceneViewFamily* InViewFamily);
    
    // ========================================================================
    // Main Rendering Interface
    // ========================================================================
    
    /**
     * Main render function - must be implemented by derived classes
     * @param RHICmdList The command list to record rendering commands
     */
    virtual void Render(RHI::IRHICommandList& RHICmdList) = 0;
    
    /**
     * Render hit proxies for editor picking
     * @param RHICmdList The command list
     */
    virtual void RenderHitProxies(RHI::IRHICommandList& RHICmdList) {}
    
    /**
     * Whether velocities should be rendered
     */
    virtual bool ShouldRenderVelocities() const { return false; }
    
    /**
     * Whether a depth prepass should be rendered
     */
    virtual bool ShouldRenderPrePass() const { return false; }
    
    /**
     * Whether simple lights are allowed
     */
    virtual bool AllowSimpleLights() const { return true; }
    
    // ========================================================================
    // Initialization
    // ========================================================================
    
    /**
     * Called at the beginning of rendering on the render thread
     * @param RHICmdList The command list
     */
    void RenderThreadBegin(RHI::IRHICommandList& RHICmdList);
    
    /**
     * Called at the end of rendering on the render thread
     * @param RHICmdList The command list
     */
    void RenderThreadEnd(RHI::IRHICommandList& RHICmdList);
    
    /**
     * Prepare view rectangles for rendering
     * @param RHICmdList The command list
     */
    void PrepareViewRectsForRendering(RHI::IRHICommandList& RHICmdList);
    
    // ========================================================================
    // Visibility Computation
    // ========================================================================
    
    /**
     * Performs setup prior to visibility determination
     */
    void PreVisibilityFrameSetup();
    
    /**
     * Compute visibility for all views
     * @param RHICmdList The command list
     */
    void ComputeViewVisibility(RHI::IRHICommandList& RHICmdList);
    
    /**
     * Compute light visibility
     */
    virtual void ComputeLightVisibility();
    
    /**
     * Performs setup after visibility determination
     */
    void PostVisibilityFrameSetup();
    
    // ========================================================================
    // Mesh Gathering
    // ========================================================================
    
    /**
     * Called before gathering dynamic mesh elements
     */
    virtual void PreGatherDynamicMeshElements() {}
    
    /**
     * Gather dynamic mesh elements from visible primitives
     */
    void GatherDynamicMeshElements();
    
    // ========================================================================
    // Shadow Setup
    // ========================================================================
    
    /**
     * Initialize dynamic shadows
     */
    void InitDynamicShadows();
    
    /**
     * Gather primitives for shadow rendering
     */
    void GatherShadowPrimitives();
    
    // ========================================================================
    // Accessors
    // ========================================================================
    
    /**
     * Get the scene being rendered
     */
    FScene* GetScene() const { return Scene; }
    
    /**
     * Get the view family
     */
    const FSceneViewFamily& GetViewFamily() const { return ViewFamily; }
    
    /**
     * Get the views array
     */
    TArray<FViewInfo>& GetViews() { return Views; }
    const TArray<FViewInfo>& GetViews() const { return Views; }
    
    /**
     * Get a specific view
     */
    FViewInfo& GetView(int32 Index) { return Views[Index]; }
    const FViewInfo& GetView(int32 Index) const { return Views[Index]; }
    
    /**
     * Get the number of views
     */
    int32 GetNumViews() const { return Views.Num(); }
    
    /**
     * Get visible light infos
     */
    TArray<FVisibleLightInfo>& GetVisibleLightInfos() { return VisibleLightInfos; }
    const TArray<FVisibleLightInfo>& GetVisibleLightInfos() const { return VisibleLightInfos; }
    
    /**
     * Get the feature level
     */
    uint32 GetFeatureLevel() const { return FeatureLevel; }
    
    /**
     * Check if this is the first scene renderer in a group
     */
    bool IsFirstSceneRenderer() const { return bIsFirstSceneRenderer; }
    
    /**
     * Check if this is the last scene renderer in a group
     */
    bool IsLastSceneRenderer() const { return bIsLastSceneRenderer; }
    
protected:
    // ========================================================================
    // Internal Helper Methods
    // ========================================================================
    
    /**
     * Initialize views from the view family
     */
    void InitViews();
    
    /**
     * Perform frustum culling for a view
     * @param View The view to cull for
     * @return Number of primitives culled
     */
    int32 FrustumCull(FViewInfo& View);
    
    /**
     * Perform occlusion culling for a view
     * @param View The view to cull for
     * @param RHICmdList The command list
     */
    void OcclusionCull(FViewInfo& View, RHI::IRHICommandList& RHICmdList);
    
    /**
     * Perform distance culling for a view
     * @param View The view to cull for
     * @return Number of primitives culled
     */
    int32 DistanceCull(FViewInfo& View);
    
    /**
     * Compute view relevance for visible primitives
     * @param View The view
     */
    void ComputeViewRelevance(FViewInfo& View);
    
    /**
     * Setup mesh passes for a view
     * @param View The view
     * @param ViewCommands The view commands to populate
     */
    void SetupMeshPass(FViewInfo& View, FViewCommands& ViewCommands);
    
    /**
     * Gather simple lights from visible primitives
     */
    void GatherSimpleLights();
    
    /**
     * Initialize fog constants for views
     */
    void InitFogConstants();
    
    /**
     * Check if translucency should be rendered
     */
    bool ShouldRenderTranslucency() const;
    
    /**
     * Finish rendering and cleanup
     * @param RHICmdList The command list
     */
    void RenderFinish(RHI::IRHICommandList& RHICmdList);
    
protected:
    // ========================================================================
    // Scene Data
    // ========================================================================
    
    /** The scene being rendered */
    FScene* Scene;
    
    /** View family information */
    FSceneViewFamily ViewFamily;
    
    /** Array of views to render */
    TArray<FViewInfo> Views;
    
    /** Per-view commands */
    TArray<FViewCommands> ViewCommands;
    
    /** Mesh element collector */
    FMeshElementCollector MeshCollector;
    
    /** Visible light information */
    TArray<FVisibleLightInfo> VisibleLightInfos;
    
    // ========================================================================
    // Rendering State
    // ========================================================================
    
    /** Feature level being rendered */
    uint32 FeatureLevel;
    
    /** Shader platform */
    uint32 ShaderPlatform;
    
    /** Whether precomputed visibility was used */
    bool bUsedPrecomputedVisibility;
    
    /** Whether this is the first scene renderer in a group */
    bool bIsFirstSceneRenderer;
    
    /** Whether this is the last scene renderer in a group */
    bool bIsLastSceneRenderer;
    
    /** Size of the view family */
    Math::FIntPoint FamilySize;
    
private:
    /**
     * Compute the family size from views
     */
    void ComputeFamilySize();
};

// ============================================================================
// FDeferredShadingSceneRenderer - Deferred Shading Renderer
// ============================================================================

/**
 * @class FDeferredShadingSceneRenderer
 * @brief Deferred shading scene renderer
 * 
 * Implements the deferred shading rendering pipeline:
 * 1. Depth PrePass (optional)
 * 2. Base Pass (GBuffer fill)
 * 3. Lighting Pass
 * 4. Translucency
 * 5. Post Processing
 * 
 * Reference: UE5 FDeferredShadingSceneRenderer
 */
class FDeferredShadingSceneRenderer : public FSceneRenderer
{
public:
    /** Constructor */
    FDeferredShadingSceneRenderer(const FSceneViewFamily* InViewFamily);
    
    /** Destructor */
    virtual ~FDeferredShadingSceneRenderer();
    
    // ========================================================================
    // FSceneRenderer Interface
    // ========================================================================
    
    virtual void Render(RHI::IRHICommandList& RHICmdList) override;
    virtual void RenderHitProxies(RHI::IRHICommandList& RHICmdList) override;
    virtual bool ShouldRenderVelocities() const override;
    virtual bool ShouldRenderPrePass() const override;
    
protected:
    // ========================================================================
    // Rendering Passes
    // ========================================================================
    
    /**
     * Render the depth prepass
     * @param RHICmdList The command list
     */
    void RenderPrePass(RHI::IRHICommandList& RHICmdList);
    
    /**
     * Render the base pass (GBuffer fill)
     * @param RHICmdList The command list
     */
    void RenderBasePass(RHI::IRHICommandList& RHICmdList);
    
    /**
     * Render lighting
     * @param RHICmdList The command list
     */
    void RenderLights(RHI::IRHICommandList& RHICmdList);
    
    /**
     * Render translucent objects
     * @param RHICmdList The command list
     */
    void RenderTranslucency(RHI::IRHICommandList& RHICmdList);
    
    /**
     * Render ambient occlusion
     * @param RHICmdList The command list
     */
    void RenderAmbientOcclusion(RHI::IRHICommandList& RHICmdList);
    
    /**
     * Render sky atmosphere
     * @param RHICmdList The command list
     */
    void RenderSkyAtmosphere(RHI::IRHICommandList& RHICmdList);
    
    /**
     * Render fog
     * @param RHICmdList The command list
     */
    void RenderFog(RHI::IRHICommandList& RHICmdList);
    
    /**
     * Render post processing
     * @param RHICmdList The command list
     */
    void RenderPostProcessing(RHI::IRHICommandList& RHICmdList);
    
    // ========================================================================
    // Shadow Rendering
    // ========================================================================
    
    /**
     * Render shadow depth maps
     * @param RHICmdList The command list
     */
    void RenderShadowDepthMaps(RHI::IRHICommandList& RHICmdList);
    
    /**
     * Render shadow projections
     * @param RHICmdList The command list
     */
    void RenderShadowProjections(RHI::IRHICommandList& RHICmdList);
    
protected:
    // ========================================================================
    // Pipeline State
    // ========================================================================
    
    /** Whether to use early Z pass */
    bool bUseEarlyZPass;
    
    /** Whether to use deferred lighting */
    bool bUseDeferredLighting;
    
    /** Whether screen space ambient occlusion is enabled */
    bool bUseSSAO;
    
    /** Whether screen space reflections are enabled */
    bool bUseSSR;
    
    /** Whether motion blur is enabled */
    bool bUseMotionBlur;
    
    /** Whether bloom is enabled */
    bool bUseBloom;
    
    /** Whether tone mapping is enabled */
    bool bUseToneMapping;
};

// ============================================================================
// FForwardShadingSceneRenderer - Forward Shading Renderer
// ============================================================================

/**
 * @class FForwardShadingSceneRenderer
 * @brief Forward shading scene renderer (for mobile/simple rendering)
 * 
 * Implements a forward shading pipeline suitable for mobile devices
 * or simpler rendering scenarios.
 * Reference: UE5 FMobileSceneRenderer
 */
class FForwardShadingSceneRenderer : public FSceneRenderer
{
public:
    /** Constructor */
    FForwardShadingSceneRenderer(const FSceneViewFamily* InViewFamily);
    
    /** Destructor */
    virtual ~FForwardShadingSceneRenderer();
    
    // ========================================================================
    // FSceneRenderer Interface
    // ========================================================================
    
    virtual void Render(RHI::IRHICommandList& RHICmdList) override;
    virtual bool ShouldRenderVelocities() const override;
    virtual bool ShouldRenderPrePass() const override;
    
protected:
    /**
     * Render the main forward pass
     * @param RHICmdList The command list
     */
    void RenderForwardPass(RHI::IRHICommandList& RHICmdList);
    
    /**
     * Render translucent objects
     * @param RHICmdList The command list
     */
    void RenderTranslucency(RHI::IRHICommandList& RHICmdList);
};

} // namespace MonsterEngine
