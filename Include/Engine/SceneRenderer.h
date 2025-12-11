// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file SceneRenderer.h
 * @brief Scene renderer for integrating scene system with RHI, inspired by UE5's FSceneRenderer
 * 
 * This file defines the scene renderer which is responsible for:
 * - Managing the rendering pipeline for a scene
 * - Coordinating visibility determination
 * - Dispatching draw calls through RHI
 * - Managing render passes (depth, base, lighting, etc.)
 */

#include "Core/CoreMinimal.h"
#include "Math/MonsterMath.h"
#include "Engine/SceneTypes.h"
#include "Engine/SceneView.h"
#include "Engine/SceneVisibility.h"
#include "Containers/Array.h"

namespace MonsterEngine
{

// Forward declarations
class FScene;
class FSceneView;
class FSceneViewFamily;
class FPrimitiveSceneInfo;
class FPrimitiveSceneProxy;
class FLightSceneInfo;

} // namespace MonsterEngine

// Forward declare RHI types in their proper namespace
namespace MonsterRender { namespace RHI {
    class IRHICommandList;
    class IRHIDevice;
}}

namespace MonsterEngine
{
// Bring RHI types into MonsterEngine namespace for convenience
using IRHICommandList = ::MonsterRender::RHI::IRHICommandList;
using IRHIDevice = ::MonsterRender::RHI::IRHIDevice;

// ============================================================================
// Render Pass Types
// ============================================================================

/**
 * Types of render passes
 */
enum class ERenderPass : uint8
{
    /** Depth prepass for early-z */
    DepthPrepass,
    
    /** Base pass (GBuffer for deferred, or forward shading) */
    BasePass,
    
    /** Shadow depth pass */
    ShadowDepth,
    
    /** Lighting pass (deferred) */
    Lighting,
    
    /** Translucency pass */
    Translucency,
    
    /** Post-processing pass */
    PostProcess,
    
    /** Debug visualization pass */
    Debug,
    
    /** Number of pass types */
    Num
};

/**
 * Flags for render pass configuration
 */
enum class ERenderPassFlags : uint32
{
    None = 0,
    
    /** Enable depth testing */
    DepthTest = 1 << 0,
    
    /** Enable depth writing */
    DepthWrite = 1 << 1,
    
    /** Enable stencil testing */
    StencilTest = 1 << 2,
    
    /** Enable alpha blending */
    AlphaBlend = 1 << 3,
    
    /** Render back faces */
    RenderBackFaces = 1 << 4,
    
    /** Use instancing */
    UseInstancing = 1 << 5,
    
    /** Enable wireframe mode */
    Wireframe = 1 << 6,
};

// Enable bitwise operations for ERenderPassFlags
inline ERenderPassFlags operator|(ERenderPassFlags A, ERenderPassFlags B) { return static_cast<ERenderPassFlags>(static_cast<uint32>(A) | static_cast<uint32>(B)); }
inline ERenderPassFlags operator&(ERenderPassFlags A, ERenderPassFlags B) { return static_cast<ERenderPassFlags>(static_cast<uint32>(A) & static_cast<uint32>(B)); }
inline ERenderPassFlags operator~(ERenderPassFlags A) { return static_cast<ERenderPassFlags>(~static_cast<uint32>(A)); }
inline ERenderPassFlags& operator|=(ERenderPassFlags& A, ERenderPassFlags B) { return A = A | B; }
inline ERenderPassFlags& operator&=(ERenderPassFlags& A, ERenderPassFlags B) { return A = A & B; }

// ============================================================================
// View Info (Extended View for Rendering)
// ============================================================================

/**
 * Extended view information for rendering
 * Contains additional data needed during rendering that isn't in FSceneView
 */
struct FViewInfo : public FSceneView
{
    /** Visibility results for this view */
    FViewVisibilityResult VisibilityResult;

    /** Visible primitives sorted by material/state */
    TArray<FPrimitiveSceneInfo*> VisibleStaticPrimitives;
    
    /** Visible dynamic primitives */
    TArray<FPrimitiveSceneInfo*> VisibleDynamicPrimitives;

    /** Visible translucent primitives */
    TArray<FPrimitiveSceneInfo*> VisibleTranslucentPrimitives;

    /** Visible lights affecting this view */
    TArray<FLightSceneInfo*> VisibleLights;

    /** Whether visibility has been computed */
    bool bVisibilityComputed = false;

    /** Constructor from scene view */
    FViewInfo(const FSceneView& InView)
        : FSceneView(InView)
    {
    }

    /** Destructor */
    virtual ~FViewInfo() = default;
};

// ============================================================================
// Mesh Draw Command
// ============================================================================

/**
 * Represents a single draw command for a mesh element
 */
struct FMeshDrawCommand
{
    /** Primitive scene info */
    FPrimitiveSceneInfo* PrimitiveSceneInfo = nullptr;

    /** Primitive scene proxy */
    FPrimitiveSceneProxy* PrimitiveSceneProxy = nullptr;

    /** Material index */
    int32 MaterialIndex = 0;

    /** Mesh element index */
    int32 MeshElementIndex = 0;

    /** Sort key for batching */
    uint64 SortKey = 0;

    /** Number of instances */
    int32 NumInstances = 1;

    /** First instance index */
    int32 FirstInstance = 0;

    /** Computes the sort key for this command */
    void ComputeSortKey();

    /** Comparison for sorting */
    bool operator<(const FMeshDrawCommand& Other) const
    {
        return SortKey < Other.SortKey;
    }
};

// ============================================================================
// Scene Renderer
// ============================================================================

/**
 * Base class for scene renderers
 * Manages the rendering pipeline for a scene view family
 */
class FSceneRenderer
{
public:
    /**
     * Constructor
     * @param InScene - The scene to render
     * @param InViewFamily - The view family to render
     */
    FSceneRenderer(FScene* InScene, const FSceneViewFamily& InViewFamily);

    /** Destructor */
    virtual ~FSceneRenderer();

    /**
     * Renders the scene
     * @param RHICmdList - RHI command list for rendering
     */
    virtual void Render(IRHICommandList& RHICmdList);

    /**
     * Gets the scene being rendered
     * @return Pointer to the scene
     */
    FScene* GetScene() const { return Scene; }

    /**
     * Gets the view family being rendered
     * @return Reference to the view family
     */
    const FSceneViewFamily& GetViewFamily() const { return ViewFamily; }

    /**
     * Gets the views being rendered
     * @return Array of view info
     */
    const TArray<FViewInfo>& GetViews() const { return Views; }

protected:
    // ============================================================================
    // Initialization
    // ============================================================================

    /**
     * Initializes views from the view family
     */
    virtual void InitViews();

    /**
     * Computes visibility for all views
     */
    virtual void ComputeVisibility();

    /**
     * Gathers visible primitives for rendering
     */
    virtual void GatherVisiblePrimitives();

    /**
     * Sorts primitives for optimal rendering
     */
    virtual void SortPrimitives();

    // ============================================================================
    // Render Passes
    // ============================================================================

    /**
     * Renders the depth prepass
     * @param RHICmdList - RHI command list
     */
    virtual void RenderDepthPrepass(IRHICommandList& RHICmdList);

    /**
     * Renders the base pass
     * @param RHICmdList - RHI command list
     */
    virtual void RenderBasePass(IRHICommandList& RHICmdList);

    /**
     * Renders shadow depths
     * @param RHICmdList - RHI command list
     */
    virtual void RenderShadowDepths(IRHICommandList& RHICmdList);

    /**
     * Renders the lighting pass
     * @param RHICmdList - RHI command list
     */
    virtual void RenderLighting(IRHICommandList& RHICmdList);

    /**
     * Renders translucent objects
     * @param RHICmdList - RHI command list
     */
    virtual void RenderTranslucency(IRHICommandList& RHICmdList);

    /**
     * Renders post-processing effects
     * @param RHICmdList - RHI command list
     */
    virtual void RenderPostProcess(IRHICommandList& RHICmdList);

    // ============================================================================
    // Draw Command Generation
    // ============================================================================

    /**
     * Generates draw commands for a view
     * @param ViewIndex - Index of the view
     * @param Pass - Render pass type
     * @param OutCommands - Array to receive draw commands
     */
    virtual void GenerateDrawCommands(int32 ViewIndex, ERenderPass Pass, TArray<FMeshDrawCommand>& OutCommands);

    /**
     * Submits draw commands to the RHI
     * @param RHICmdList - RHI command list
     * @param Commands - Draw commands to submit
     */
    virtual void SubmitDrawCommands(IRHICommandList& RHICmdList, const TArray<FMeshDrawCommand>& Commands);

protected:
    /** The scene being rendered */
    FScene* Scene;

    /** The view family being rendered */
    const FSceneViewFamily& ViewFamily;

    /** Extended view information for each view */
    TArray<FViewInfo> Views;

    /** Visibility manager */
    FSceneVisibilityManager VisibilityManager;

    /** Frame number */
    uint32 FrameNumber;

    /** Whether the renderer has been initialized */
    bool bInitialized;
};

// ============================================================================
// Deferred Shading Renderer
// ============================================================================

/**
 * Deferred shading renderer
 * Implements a deferred rendering pipeline with GBuffer
 */
class FDeferredShadingRenderer : public FSceneRenderer
{
public:
    /**
     * Constructor
     * @param InScene - The scene to render
     * @param InViewFamily - The view family to render
     */
    FDeferredShadingRenderer(FScene* InScene, const FSceneViewFamily& InViewFamily);

    /** Destructor */
    virtual ~FDeferredShadingRenderer();

    /**
     * Renders the scene using deferred shading
     * @param RHICmdList - RHI command list
     */
    virtual void Render(IRHICommandList& RHICmdList) override;

protected:
    /**
     * Renders the GBuffer pass
     * @param RHICmdList - RHI command list
     */
    virtual void RenderGBuffer(IRHICommandList& RHICmdList);

    /**
     * Renders deferred lighting
     * @param RHICmdList - RHI command list
     */
    virtual void RenderDeferredLighting(IRHICommandList& RHICmdList);

    /**
     * Renders screen-space ambient occlusion
     * @param RHICmdList - RHI command list
     */
    virtual void RenderSSAO(IRHICommandList& RHICmdList);

    /**
     * Renders screen-space reflections
     * @param RHICmdList - RHI command list
     */
    virtual void RenderSSR(IRHICommandList& RHICmdList);
};

// ============================================================================
// Forward Shading Renderer
// ============================================================================

// Forward declaration
class FForwardRenderer;

/**
 * Forward shading renderer
 * Implements a forward rendering pipeline using FForwardRenderer
 * 
 * Reference UE5: FMobileSceneRenderer
 */
class FForwardShadingRenderer : public FSceneRenderer
{
public:
    /**
     * Constructor
     * @param InScene - The scene to render
     * @param InViewFamily - The view family to render
     */
    FForwardShadingRenderer(FScene* InScene, const FSceneViewFamily& InViewFamily);

    /** Destructor */
    virtual ~FForwardShadingRenderer();

    /**
     * Renders the scene using forward shading
     * @param RHICmdList - RHI command list
     */
    virtual void Render(IRHICommandList& RHICmdList) override;
    
    /**
     * Get the forward renderer
     * @return Pointer to the forward renderer
     */
    FForwardRenderer* GetForwardRenderer() const { return ForwardRenderer; }

protected:
    /**
     * Renders opaque objects with forward shading (legacy path)
     * @param RHICmdList - RHI command list
     */
    virtual void RenderForwardOpaque(IRHICommandList& RHICmdList);
    
    /** Forward renderer instance */
    FForwardRenderer* ForwardRenderer;
};

// ============================================================================
// Renderer Factory
// ============================================================================

/**
 * Creates the appropriate scene renderer
 * @param Scene - The scene to render
 * @param ViewFamily - The view family to render
 * @param bUseDeferred - Whether to use deferred shading
 * @return Pointer to the created renderer
 */
FSceneRenderer* CreateSceneRenderer(FScene* Scene, const FSceneViewFamily& ViewFamily, bool bUseDeferred = true);

// ============================================================================
// Render Target
// ============================================================================

/**
 * Render target interface
 */
class FRenderTarget
{
public:
    virtual ~FRenderTarget() = default;

    /** Gets the width of the render target */
    virtual int32 GetWidth() const = 0;

    /** Gets the height of the render target */
    virtual int32 GetHeight() const = 0;

    /** Gets the size of the render target */
    virtual FIntPoint GetSize() const
    {
        return FIntPoint(GetWidth(), GetHeight());
    }

    /** Gets the gamma value */
    virtual float GetGamma() const { return 2.2f; }
};

} // namespace MonsterEngine
