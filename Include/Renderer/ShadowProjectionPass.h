// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file ShadowProjectionPass.h
 * @brief Shadow projection pass for applying shadows to lit surfaces
 * 
 * This pass projects shadow depth maps onto the scene during lighting.
 * It samples the shadow map and computes shadow factors for each pixel.
 * Reference: UE5 ShadowRendering.cpp, FShadowProjectionPassParameters
 */

#include "Core/CoreMinimal.h"
#include "Core/CoreTypes.h"
#include "Containers/Array.h"
#include "Core/Templates/SharedPointer.h"
#include "Math/Matrix.h"
#include "Math/Vector4.h"
#include "Renderer/RenderPass.h"

// Forward declarations
namespace MonsterRender { namespace RHI {
    class IRHIDevice;
    class IRHICommandList;
    class IRHITexture;
    class IRHIBuffer;
    class IRHIPipelineState;
    class IRHISampler;
}}

namespace MonsterEngine
{
namespace Renderer
{

// Forward declarations
class FProjectedShadowInfo;
class FViewInfo;
class FLightSceneInfo;
class FSceneRenderer;

// ============================================================================
// FShadowProjectionUniformParameters - Uniform buffer for shadow projection
// ============================================================================

/**
 * @struct FShadowProjectionUniformParameters
 * @brief Uniform parameters for shadow projection shader
 * 
 * Contains matrices and parameters needed to project shadows onto surfaces.
 * Reference: UE5 ShadowProjectionShaderParameters
 */
struct FShadowProjectionUniformParameters
{
    /** World to shadow matrix (transforms world position to shadow UV + depth) */
    FMatrix44f ScreenToShadowMatrix;
    
    /** Shadow UV min/max bounds for clamping */
    FVector4f ShadowUVMinMax;
    
    /** Shadow buffer size and inverse size */
    FVector4f ShadowBufferSize;
    
    /** Shadow depth bias parameters */
    FVector4f ShadowParams;
    
    /** Light position (xyz) and type (w: 0=directional, 1=point, 2=spot) */
    FVector4f LightPositionAndType;
    
    /** Light direction (xyz) and attenuation radius (w) */
    FVector4f LightDirectionAndRadius;
    
    /** Shadow fade parameters */
    FVector4f ShadowFadeParams;
    
    /** Shadow transition scale */
    float TransitionScale;
    
    /** Shadow soft transition scale */
    float SoftTransitionScale;
    
    /** Shadow projection depth bias */
    float ProjectionDepthBias;
    
    /** Padding for alignment */
    float Padding0;
    
    /** Constructor with default values */
    FShadowProjectionUniformParameters()
        : ScreenToShadowMatrix(FMatrix44f::Identity)
        , ShadowUVMinMax(0.0f, 0.0f, 1.0f, 1.0f)
        , ShadowBufferSize(1024.0f, 1024.0f, 1.0f / 1024.0f, 1.0f / 1024.0f)
        , ShadowParams(0.0f, 0.0f, 0.0f, 1.0f)
        , LightPositionAndType(0.0f, 0.0f, 0.0f, 0.0f)
        , LightDirectionAndRadius(0.0f, -1.0f, 0.0f, 10000.0f)
        , ShadowFadeParams(0.0f, 0.0f, 1.0f, 1.0f)
        , TransitionScale(60.0f)
        , SoftTransitionScale(1.0f)
        , ProjectionDepthBias(0.0f)
        , Padding0(0.0f)
    {}
};

// ============================================================================
// FShadowProjectionPassConfig - Configuration for shadow projection pass
// ============================================================================

/**
 * @struct FShadowProjectionPassConfig
 * @brief Configuration for shadow projection pass
 */
struct FShadowProjectionPassConfig
{
    /** Shadow quality level (1-5) */
    uint32 ShadowQuality;
    
    /** Whether to use PCF filtering */
    bool bUsePCF;
    
    /** PCF kernel size (1, 3, 5, 7) */
    uint32 PCFKernelSize;
    
    /** Whether to use contact hardening shadows */
    bool bUseContactHardeningShadows;
    
    /** Whether to use screen space shadows */
    bool bUseScreenSpaceShadows;
    
    /** Constructor with default values */
    FShadowProjectionPassConfig()
        : ShadowQuality(3)
        , bUsePCF(true)
        , PCFKernelSize(3)
        , bUseContactHardeningShadows(false)
        , bUseScreenSpaceShadows(false)
    {}
};

// ============================================================================
// FShadowProjectionPass - Shadow projection rendering pass
// ============================================================================

/**
 * @class FShadowProjectionPass
 * @brief Renders shadow projections onto lit surfaces
 * 
 * This pass takes rendered shadow depth maps and projects them onto
 * the scene during the lighting pass. It outputs a shadow mask texture
 * that can be used to modulate light contribution.
 * 
 * Reference: UE5 FSceneRenderer::RenderShadowProjections
 */
class FShadowProjectionPass : public FRenderPassBase
{
public:
    /**
     * Constructor
     * @param InDevice RHI device for resource creation
     */
    explicit FShadowProjectionPass(MonsterRender::RHI::IRHIDevice* InDevice);
    
    /** Destructor */
    virtual ~FShadowProjectionPass();
    
    // Non-copyable
    FShadowProjectionPass(const FShadowProjectionPass&) = delete;
    FShadowProjectionPass& operator=(const FShadowProjectionPass&) = delete;
    
    // ========================================================================
    // Initialization
    // ========================================================================
    
    /**
     * Initialize the shadow projection pass
     * @return true if initialization succeeded
     */
    bool initialize();
    
    /**
     * Release all resources
     */
    void release();
    
    /**
     * Check if the pass is initialized
     */
    bool isInitialized() const { return m_bInitialized; }
    
    // ========================================================================
    // FRenderPassBase Interface
    // ========================================================================
    
    virtual bool ShouldExecute(const FRenderPassContext& Context) const override;
    virtual void Setup(FRenderPassContext& Context) override;
    virtual void Execute(FRenderPassContext& Context) override;
    virtual void Cleanup(FRenderPassContext& Context) override;
    
    // ========================================================================
    // Shadow Projection Methods
    // ========================================================================
    
    /**
     * Project a single shadow onto the scene
     * @param RHICmdList Command list for GPU commands
     * @param ShadowInfo Shadow information
     * @param View View to project shadow for
     * @param LightInfo Light that casts this shadow
     * @param ShadowMaskTexture Output shadow mask texture
     */
    void projectShadow(
        MonsterRender::RHI::IRHICommandList& RHICmdList,
        const FProjectedShadowInfo* ShadowInfo,
        const FViewInfo* View,
        const FLightSceneInfo* LightInfo,
        MonsterRender::RHI::IRHITexture* ShadowMaskTexture);
    
    /**
     * Project all shadows for a light
     * @param RHICmdList Command list for GPU commands
     * @param Shadows Array of shadows to project
     * @param View View to project shadows for
     * @param LightInfo Light that casts these shadows
     * @param ShadowMaskTexture Output shadow mask texture
     */
    void projectShadowsForLight(
        MonsterRender::RHI::IRHICommandList& RHICmdList,
        const TArray<FProjectedShadowInfo*>& Shadows,
        const FViewInfo* View,
        const FLightSceneInfo* LightInfo,
        MonsterRender::RHI::IRHITexture* ShadowMaskTexture);
    
    /**
     * Create shadow mask texture for a view
     * @param Width Texture width
     * @param Height Texture height
     * @return Created shadow mask texture
     */
    TSharedPtr<MonsterRender::RHI::IRHITexture> createShadowMaskTexture(
        uint32 Width, uint32 Height);
    
    // ========================================================================
    // Configuration
    // ========================================================================
    
    /**
     * Set shadow projection configuration
     * @param InConfig New configuration
     */
    void setConfig(const FShadowProjectionPassConfig& InConfig);
    
    /**
     * Get current configuration
     */
    const FShadowProjectionPassConfig& getConfig() const { return m_config; }
    
    /**
     * Update uniform buffer with shadow parameters
     * @param ShadowInfo Shadow information
     * @param View View information
     */
    void updateUniformBuffer(
        const FProjectedShadowInfo* ShadowInfo,
        const FViewInfo* View);

private:
    // ========================================================================
    // Internal Methods
    // ========================================================================
    
    /**
     * Load shadow projection shaders
     * @return true if successful
     */
    bool _loadShaders();
    
    /**
     * Create pipeline state for shadow projection
     * @return true if successful
     */
    bool _createPipelineState();
    
    /**
     * Create uniform buffer
     * @return true if successful
     */
    bool _createUniformBuffer();
    
    /**
     * Create shadow sampler
     * @return true if successful
     */
    bool _createShadowSampler();
    
    /**
     * Bind pipeline state and resources
     * @param RHICmdList Command list
     * @param ShadowInfo Shadow information
     */
    void _bindPipelineState(
        MonsterRender::RHI::IRHICommandList& RHICmdList,
        const FProjectedShadowInfo* ShadowInfo);
    
    /**
     * Draw full screen quad for shadow projection
     * @param RHICmdList Command list
     * @param View View information
     */
    void _drawFullScreenQuad(
        MonsterRender::RHI::IRHICommandList& RHICmdList,
        const FViewInfo* View);
    
    /**
     * Compute screen to shadow matrix
     * @param ShadowInfo Shadow information
     * @param View View information
     * @return Screen to shadow transformation matrix
     */
    FMatrix44f _computeScreenToShadowMatrix(
        const FProjectedShadowInfo* ShadowInfo,
        const FViewInfo* View) const;

private:
    // ========================================================================
    // Member Variables
    // ========================================================================
    
    /** RHI device */
    MonsterRender::RHI::IRHIDevice* m_device;
    
    /** Whether the pass is initialized */
    bool m_bInitialized;
    
    /** Configuration */
    FShadowProjectionPassConfig m_config;
    
    /** Uniform parameters */
    FShadowProjectionUniformParameters m_uniformParams;
    
    /** Uniform buffer */
    TSharedPtr<MonsterRender::RHI::IRHIBuffer> m_uniformBuffer;
    
    /** Vertex shader */
    TSharedPtr<MonsterRender::RHI::IRHIResource> m_vertexShader;
    
    /** Pixel shader for standard shadows */
    TSharedPtr<MonsterRender::RHI::IRHIResource> m_pixelShader;
    
    /** Pixel shader for PCF shadows */
    TSharedPtr<MonsterRender::RHI::IRHIResource> m_pixelShaderPCF;
    
    /** Pipeline state for standard shadows */
    TSharedPtr<MonsterRender::RHI::IRHIPipelineState> m_pipelineState;
    
    /** Pipeline state for PCF shadows */
    TSharedPtr<MonsterRender::RHI::IRHIPipelineState> m_pipelineStatePCF;
    
    /** Shadow comparison sampler */
    TSharedPtr<MonsterRender::RHI::IRHISampler> m_shadowSampler;
    
    /** Full screen quad vertex buffer */
    TSharedPtr<MonsterRender::RHI::IRHIBuffer> m_fullScreenQuadVB;
    
    /** Thread safety mutex */
    mutable std::mutex m_mutex;
};

// ============================================================================
// Shader Paths
// ============================================================================

/** Shadow projection vertex shader path */
constexpr const char* SHADOW_PROJECTION_VERT_PATH = "Shaders/Forward/ShadowProjection.vert";

/** Shadow projection pixel shader path */
constexpr const char* SHADOW_PROJECTION_FRAG_PATH = "Shaders/Forward/ShadowProjection.frag";

/** Shadow projection PCF pixel shader path */
constexpr const char* SHADOW_PROJECTION_PCF_FRAG_PATH = "Shaders/Forward/ShadowProjectionPCF.frag";

} // namespace Renderer
} // namespace MonsterEngine
