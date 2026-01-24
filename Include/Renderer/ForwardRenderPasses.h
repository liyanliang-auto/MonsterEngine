// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file ForwardRenderPasses.h
 * @brief Forward rendering pipeline passes
 * 
 * Implements the core render passes for forward rendering:
 * - Depth Prepass (Early-Z optimization)
 * - Opaque Pass (Forward lighting)
 * - Skybox Pass (Environment rendering)
 * - Transparent Pass (Alpha blended objects)
 * - Shadow Depth Pass (Shadow map generation)
 * 
 * Reference UE5 files:
 * - Engine/Source/Runtime/Renderer/Private/MobileShadingRenderer.cpp
 * - Engine/Source/Runtime/Renderer/Private/DepthRendering.h
 * - Engine/Source/Runtime/Renderer/Private/BasePassRendering.h
 * - Engine/Source/Runtime/Renderer/Private/TranslucentRendering.h
 */

#include "Renderer/RenderPass.h"
#include "Engine/SceneRenderer.h"
#include "Math/Vector.h"
#include "Math/Matrix.h"
#include "Core/Templates/SharedPointer.h"

// Forward declarations for RHI types
namespace MonsterRender { namespace RHI {
    class IRHITexture;
}}

namespace MonsterEngine
{

// Forward declarations
class FPrimitiveSceneInfo;
class FLightSceneInfo;
class FLightSceneProxy;

// ============================================================================
// Shadow Map Configuration
// ============================================================================

/**
 * Shadow map type enumeration
 */
enum class EShadowMapType : uint8
{
    /** Standard 2D shadow map for directional/spot lights */
    Standard2D,
    
    /** Cube shadow map for point lights */
    CubeMap,
    
    /** Cascaded shadow maps for directional lights */
    Cascaded
};

/**
 * Shadow map configuration
 */
struct FShadowMapConfig
{
    /** Shadow map resolution */
    int32 Resolution = 1024;
    
    /** Shadow map type */
    EShadowMapType Type = EShadowMapType::Standard2D;
    
    /** Number of cascades (for CSM) */
    int32 NumCascades = 4;
    
    /** Cascade split lambda (0 = linear, 1 = logarithmic) */
    float CascadeSplitLambda = 0.5f;
    
    /** Shadow bias to prevent shadow acne */
    float DepthBias = 0.005f;
    
    /** Slope-scaled depth bias */
    float SlopeScaledDepthBias = 1.0f;
    
    /** Normal offset bias */
    float NormalOffsetBias = 0.0f;
    
    /** Shadow distance (far plane) */
    float ShadowDistance = 100.0f;
    
    /** Scene bounds radius for shadow map coverage */
    float SceneBoundsRadius = 30.0f;
    
    /** Enable PCF filtering */
    bool bEnablePCF = true;
    
    /** PCF filter size (1 = 3x3, 2 = 5x5, etc.) */
    int32 PCFFilterSize = 1;
};

/**
 * Per-light shadow data
 */
struct FShadowData
{
    /** Light that casts this shadow */
    FLightSceneInfo* Light = nullptr;
    
    /** Shadow map texture handle */
    TSharedPtr<MonsterRender::RHI::IRHITexture> ShadowMapTexture;
    
    /** Light view-projection matrix */
    FMatrix LightViewProjection;
    
    /** Cascade view-projection matrices (for CSM) */
    TArray<FMatrix> CascadeViewProjections;
    
    /** Cascade split distances */
    TArray<float> CascadeSplits;
    
    /** Shadow map configuration */
    FShadowMapConfig Config;
    
    /** Whether shadow data is valid */
    bool bValid = false;
};

// ============================================================================
// Depth Prepass
// ============================================================================

/**
 * Depth prepass for early-z optimization
 * Renders depth-only for all opaque geometry to populate the depth buffer
 * This enables early-z rejection in subsequent passes
 * 
 * Following UE5's DepthRendering pattern
 */
class FDepthPrepass : public FRenderPassBase
{
public:
    /** Constructor */
    FDepthPrepass();
    
    /** Destructor */
    virtual ~FDepthPrepass() = default;
    
    // IRenderPass interface
    virtual bool ShouldExecute(const FRenderPassContext& Context) const override;
    virtual void Execute(FRenderPassContext& Context) override;
    
    /**
     * Set whether to render masked materials
     * @param bEnable - True to render masked materials with alpha test
     */
    void SetRenderMasked(bool bEnable) { bRenderMasked = bEnable; }
    
    /**
     * Check if masked materials are rendered
     * @return True if masked materials are rendered
     */
    bool GetRenderMasked() const { return bRenderMasked; }
    
protected:
    /**
     * Render a single primitive's depth
     * @param Context - Render context
     * @param Primitive - Primitive to render
     */
    virtual void RenderPrimitiveDepth(FRenderPassContext& Context, FPrimitiveSceneInfo* Primitive);
    
private:
    /** Whether to render masked materials in depth prepass */
    bool bRenderMasked = false;
};

// ============================================================================
// Opaque Pass
// ============================================================================

/**
 * Opaque geometry pass with forward lighting
 * Renders all opaque geometry with full lighting calculations
 * 
 * Following UE5's BasePassRendering and MobileBasePassRendering patterns
 */
class FOpaquePass : public FRenderPassBase
{
public:
    /** Constructor */
    FOpaquePass();
    
    /** Destructor */
    virtual ~FOpaquePass() = default;
    
    // IRenderPass interface
    virtual bool ShouldExecute(const FRenderPassContext& Context) const override;
    virtual void Execute(FRenderPassContext& Context) override;
    
    /**
     * Set maximum number of lights per object
     * @param MaxLights - Maximum light count
     */
    void SetMaxLightsPerObject(int32 MaxLights) { MaxLightsPerObject = MaxLights; }
    
    /**
     * Get maximum lights per object
     * @return Maximum light count
     */
    int32 GetMaxLightsPerObject() const { return MaxLightsPerObject; }
    
    /**
     * Enable/disable lighting
     * @param bEnable - True to enable lighting
     */
    void SetLightingEnabled(bool bEnable) { bLightingEnabled = bEnable; }
    
    /**
     * Check if lighting is enabled
     * @return True if lighting is enabled
     */
    bool IsLightingEnabled() const { return bLightingEnabled; }
    
    /**
     * Set shadow data for rendering
     * @param InShadowData - Array of shadow data for visible lights
     */
    void SetShadowData(const TArray<FShadowData>& InShadowData) { ShadowData = InShadowData; }
    
protected:
    /**
     * Render a mesh batch with RHI commands (UE5 style)
     * @param Context - Render context
     * @param MeshBatch - Mesh batch to render
     * @param Proxy - Primitive scene proxy that owns this mesh
     */
    virtual void RenderMeshBatch(
        FRenderPassContext& Context,
        const struct FMeshBatch* MeshBatch,
        const FPrimitiveSceneProxy* Proxy);
    
    /**
     * Gather lights affecting a primitive
     * @param Context - Render context
     * @param Primitive - Primitive to check
     * @param OutLights - Array to receive affecting lights
     */
    virtual void GatherAffectingLights(
        FRenderPassContext& Context,
        FPrimitiveSceneInfo* Primitive,
        TArray<FLightSceneInfo*>& OutLights);
    
    /**
     * Setup light uniform buffer for a primitive
     * @param Context - Render context
     * @param Lights - Lights to setup
     */
    virtual void SetupLightBuffer(
        FRenderPassContext& Context,
        const TArray<FLightSceneInfo*>& Lights);
    
private:
    /** Maximum number of lights per object */
    int32 MaxLightsPerObject = 8;
    
    /** Whether lighting is enabled */
    bool bLightingEnabled = true;
    
    /** Shadow data for visible lights */
    TArray<FShadowData> ShadowData;
    
    /** Temporary array for light gathering */
    TArray<FLightSceneInfo*> TempAffectingLights;
};

// ============================================================================
// Skybox Pass
// ============================================================================

/**
 * Skybox/environment rendering pass
 * Renders the skybox after opaque geometry but before transparent
 * Uses depth test but no depth write
 * 
 * Following UE5's SkyPass pattern
 */
class FSkyboxPass : public FRenderPassBase
{
public:
    /** Constructor */
    FSkyboxPass();
    
    /** Destructor */
    virtual ~FSkyboxPass() = default;
    
    // IRenderPass interface
    virtual bool ShouldExecute(const FRenderPassContext& Context) const override;
    virtual void Execute(FRenderPassContext& Context) override;
    
    /**
     * Set the skybox texture (cubemap)
     * @param TextureHandle - Handle to the cubemap texture
     */
    void SetSkyboxTexture(void* TextureHandle) { SkyboxTexture = TextureHandle; }
    
    /**
     * Get the skybox texture
     * @return Skybox texture handle
     */
    void* GetSkyboxTexture() const { return SkyboxTexture; }
    
    /**
     * Set skybox color tint
     * @param Color - Tint color
     */
    void SetSkyboxTint(const FVector3f& Color) { SkyboxTint = Color; }
    
    /**
     * Get skybox tint
     * @return Tint color
     */
    const FVector3f& GetSkyboxTint() const { return SkyboxTint; }
    
    /**
     * Set skybox intensity
     * @param Intensity - Intensity multiplier
     */
    void SetSkyboxIntensity(float Intensity) { SkyboxIntensity = Intensity; }
    
    /**
     * Get skybox intensity
     * @return Intensity multiplier
     */
    float GetSkyboxIntensity() const { return SkyboxIntensity; }
    
    /**
     * Enable/disable atmospheric scattering
     * @param bEnable - True to enable
     */
    void SetAtmosphericScattering(bool bEnable) { bAtmosphericScattering = bEnable; }
    
    /**
     * Check if atmospheric scattering is enabled
     * @return True if enabled
     */
    bool IsAtmosphericScatteringEnabled() const { return bAtmosphericScattering; }
    
protected:
    /**
     * Render the skybox geometry
     * @param Context - Render context
     */
    virtual void RenderSkybox(FRenderPassContext& Context);
    
    /**
     * Render procedural sky (if no cubemap)
     * @param Context - Render context
     */
    virtual void RenderProceduralSky(FRenderPassContext& Context);
    
private:
    /** Skybox cubemap texture handle */
    void* SkyboxTexture = nullptr;
    
    /** Skybox color tint */
    FVector3f SkyboxTint = FVector3f(1.0f, 1.0f, 1.0f);
    
    /** Skybox intensity */
    float SkyboxIntensity = 1.0f;
    
    /** Enable atmospheric scattering */
    bool bAtmosphericScattering = false;
};

// ============================================================================
// Transparent Pass
// ============================================================================

/**
 * Transparent geometry pass
 * Renders transparent objects sorted back-to-front
 * Uses alpha blending with depth test but no depth write
 * 
 * Following UE5's TranslucentRendering pattern
 */
class FTransparentPass : public FRenderPassBase
{
public:
    /** Sorting mode for transparent objects */
    enum class ESortMode : uint8
    {
        /** Sort by distance from camera (back to front) */
        BackToFront,
        
        /** Sort by distance from camera (front to back) for certain effects */
        FrontToBack,
        
        /** No sorting (render in submission order) */
        None
    };
    
    /** Constructor */
    FTransparentPass();
    
    /** Destructor */
    virtual ~FTransparentPass() = default;
    
    // IRenderPass interface
    virtual bool ShouldExecute(const FRenderPassContext& Context) const override;
    virtual void Setup(FRenderPassContext& Context) override;
    virtual void Execute(FRenderPassContext& Context) override;
    
    /**
     * Set the sorting mode
     * @param Mode - Sorting mode
     */
    void SetSortMode(ESortMode Mode) { SortMode = Mode; }
    
    /**
     * Get the sorting mode
     * @return Current sorting mode
     */
    ESortMode GetSortMode() const { return SortMode; }
    
    /**
     * Enable/disable lighting for transparent objects
     * @param bEnable - True to enable lighting
     */
    void SetLightingEnabled(bool bEnable) { bLightingEnabled = bEnable; }
    
    /**
     * Check if lighting is enabled
     * @return True if lighting is enabled
     */
    bool IsLightingEnabled() const { return bLightingEnabled; }
    
protected:
    /**
     * Sort transparent primitives
     * @param Context - Render context
     * @param Primitives - Primitives to sort
     */
    virtual void SortPrimitives(
        FRenderPassContext& Context,
        TArray<FPrimitiveSceneInfo*>& Primitives);
    
    /**
     * Render a single transparent primitive
     * @param Context - Render context
     * @param Primitive - Primitive to render
     */
    virtual void RenderTransparentPrimitive(
        FRenderPassContext& Context,
        FPrimitiveSceneInfo* Primitive);
    
    /**
     * Calculate sort key for a primitive
     * @param Context - Render context
     * @param Primitive - Primitive to calculate key for
     * @return Sort key (distance squared from camera)
     */
    virtual float CalculateSortKey(
        FRenderPassContext& Context,
        FPrimitiveSceneInfo* Primitive);
    
private:
    /** Sorting mode */
    ESortMode SortMode = ESortMode::BackToFront;
    
    /** Whether lighting is enabled for transparent objects */
    bool bLightingEnabled = true;
    
    /** Sorted primitives (temporary storage) */
    TArray<TPair<float, FPrimitiveSceneInfo*>> SortedPrimitives;
};

// ============================================================================
// Shadow Depth Pass
// ============================================================================

/**
 * Shadow depth pass for generating shadow maps
 * Renders scene depth from light's perspective
 * 
 * Following UE5's ShadowDepthRendering pattern
 */
class FShadowDepthPass : public FRenderPassBase
{
public:
    /** Constructor */
    FShadowDepthPass();
    
    /** Destructor */
    virtual ~FShadowDepthPass() = default;
    
    // IRenderPass interface
    virtual bool ShouldExecute(const FRenderPassContext& Context) const override;
    virtual void Execute(FRenderPassContext& Context) override;
    
    /**
     * Set the light to render shadows for
     * @param Light - Light scene info
     */
    void SetLight(FLightSceneInfo* Light) { CurrentLight = Light; }
    
    /**
     * Get the current light
     * @return Current light
     */
    FLightSceneInfo* GetLight() const { return CurrentLight; }
    
    /**
     * Set shadow map configuration
     * @param InConfig - Shadow map config
     */
    void SetShadowConfig(const FShadowMapConfig& InConfig) { ShadowConfig = InConfig; }
    
    /**
     * Get shadow map configuration
     * @return Shadow map config
     */
    const FShadowMapConfig& GetShadowConfig() const { return ShadowConfig; }
    
    /**
     * Get the generated shadow data
     * @return Shadow data
     */
    const FShadowData& GetShadowData() const { return GeneratedShadowData; }
    
    /**
     * Set cascade index for CSM rendering
     * @param Index - Cascade index
     */
    void SetCascadeIndex(int32 Index) { CurrentCascadeIndex = Index; }
    
protected:
    /**
     * Setup shadow map render target
     * @param Context - Render context
     */
    virtual void SetupShadowMapTarget(FRenderPassContext& Context);
    
    /**
     * Calculate light view-projection matrix
     * @param Context - Render context
     * @return Light view-projection matrix
     */
    virtual FMatrix CalculateLightViewProjection(FRenderPassContext& Context);
    
    /**
     * Calculate cascade view-projection for CSM
     * @param Context - Render context
     * @param CascadeIndex - Cascade index
     * @return Cascade view-projection matrix
     */
    virtual FMatrix CalculateCascadeViewProjection(FRenderPassContext& Context, int32 CascadeIndex);
    
    /**
     * Render shadow casters
     * @param Context - Render context
     * @param LightViewProjection - Light's view-projection matrix
     */
    virtual void RenderShadowCasters(FRenderPassContext& Context, const FMatrix& LightViewProjection);
    
private:
    /** Current light being rendered */
    FLightSceneInfo* CurrentLight = nullptr;
    
    /** Shadow map configuration */
    FShadowMapConfig ShadowConfig;
    
    /** Generated shadow data */
    FShadowData GeneratedShadowData;
    
    /** Current cascade index for CSM */
    int32 CurrentCascadeIndex = 0;
};

} // namespace MonsterEngine
