// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file ShadowDepthPass.h
 * @brief Shadow depth rendering pass implementation
 * 
 * This file defines the shadow depth pass for rendering shadow maps from
 * light perspectives. Supports directional, point, and spot lights.
 * 
 * Reference: UE5 Engine/Source/Runtime/Renderer/Private/ShadowDepthRendering.cpp
 */

#include "Core/CoreMinimal.h"
#include "Core/CoreTypes.h"
#include "Containers/Array.h"
#include "Core/Templates/SharedPointer.h"
#include "Renderer/RenderPass.h"
#include "Math/Matrix.h"
#include "Math/Vector4.h"
#include "Renderer/SceneTypes.h"
#include <mutex>

// Forward declarations for RHI types
namespace MonsterRender { 
namespace RHI {
    class IRHIDevice;
    class IRHICommandList;
    class IRHITexture;
    class IRHIBuffer;
    class IRHIPipelineState;
    class IRHIShader;
    class IRHIVertexShader;
    class IRHIPixelShader;
}}

namespace MonsterEngine
{

// Forward declarations
class FScene;
struct FViewInfo;
class FPrimitiveSceneInfo;
class FLightSceneInfo;
struct FMeshBatch;

namespace Renderer
{

// Forward declarations
class FProjectedShadowInfo;
class FShadowMap;

// ============================================================================
// Shadow Depth Pass Uniform Parameters
// ============================================================================

/**
 * @struct FShadowDepthPassUniformParameters
 * @brief Uniform buffer data for shadow depth pass
 * 
 * Contains all parameters needed by the shadow depth shaders.
 * Reference: UE5 FShadowDepthPassUniformParameters
 */
struct FShadowDepthPassUniformParameters
{
    /** Light view matrix (world to light view space) */
    FMatrix LightViewMatrix;
    
    /** Light projection matrix (light view to clip space) */
    FMatrix LightProjectionMatrix;
    
    /** Combined light view-projection matrix */
    FMatrix LightViewProjectionMatrix;
    
    /** Light position (w = 1 for point/spot, w = 0 for directional) */
    FVector4f LightPosition;
    
    /** Light direction (normalized) */
    FVector4f LightDirection;
    
    /** Depth bias constant */
    float DepthBias;
    
    /** Slope-scaled depth bias */
    float SlopeScaledBias;
    
    /** Normal offset bias */
    float NormalOffsetBias;
    
    /** Maximum shadow distance */
    float ShadowDistance;
    
    /** Inverse of maximum subject depth (for depth normalization) */
    float InvMaxSubjectDepth;
    
    /** Whether to clamp to near plane */
    float bClampToNearPlane;
    
    /** Padding for alignment */
    float Padding[2];
    
    /** Default constructor */
    FShadowDepthPassUniformParameters()
        : LightViewMatrix(FMatrix::Identity)
        , LightProjectionMatrix(FMatrix::Identity)
        , LightViewProjectionMatrix(FMatrix::Identity)
        , LightPosition(0.0f, 0.0f, 0.0f, 0.0f)
        , LightDirection(0.0f, -1.0f, 0.0f, 0.0f)
        , DepthBias(0.0f)
        , SlopeScaledBias(0.0f)
        , NormalOffsetBias(0.0f)
        , ShadowDistance(10000.0f)
        , InvMaxSubjectDepth(1.0f)
        , bClampToNearPlane(0.0f)
    {
        Padding[0] = 0.0f;
        Padding[1] = 0.0f;
    }
};

/**
 * @struct FShadowDepthPassPushConstants
 * @brief Push constants for shadow depth pass
 * 
 * Small data that changes per-draw call.
 */
struct FShadowDepthPassPushConstants
{
    /** Whether alpha testing is enabled (0 = no, 1 = yes) */
    int32 bAlphaTest;
    
    /** Whether this is a point light (0 = directional/spot, 1 = point) */
    int32 bPointLight;
    
    /** Default constructor */
    FShadowDepthPassPushConstants()
        : bAlphaTest(0)
        , bPointLight(0)
    {
    }
};

// ============================================================================
// Shadow Depth Pass Configuration
// ============================================================================

/**
 * @struct FShadowDepthPassConfig
 * @brief Configuration for shadow depth pass
 */
struct FShadowDepthPassConfig
{
    /** Shadow map resolution */
    uint32 ShadowMapResolution;
    
    /** Whether to use hardware depth bias */
    bool bUseHardwareDepthBias;
    
    /** Whether to render two-sided geometry */
    bool bTwoSidedShadows;
    
    /** Whether to use reverse depth (1 = near, 0 = far) */
    bool bReverseDepth;
    
    /** Depth bias constant */
    float DepthBiasConstant;
    
    /** Slope-scaled depth bias */
    float DepthBiasSlopeScale;
    
    /** Default constructor */
    FShadowDepthPassConfig()
        : ShadowMapResolution(1024)
        , bUseHardwareDepthBias(true)
        , bTwoSidedShadows(false)
        , bReverseDepth(false)
        , DepthBiasConstant(1.0f)
        , DepthBiasSlopeScale(1.0f)
    {
    }
};

// ============================================================================
// Shadow Depth Pass
// ============================================================================

/**
 * @class FShadowDepthPass
 * @brief Render pass for shadow depth map generation
 * 
 * Renders scene geometry from light's perspective to generate shadow maps.
 * Supports:
 * - Directional light cascaded shadow maps
 * - Point light cube shadow maps
 * - Spot light shadow maps
 * - Alpha-tested shadows for masked materials
 * 
 * Reference: UE5 FShadowDepthPassMeshProcessor, ShadowDepthRendering.cpp
 */
class FShadowDepthPass : public FRenderPassBase
{
public:
    // ========================================================================
    // Construction and Destruction
    // ========================================================================
    
    /**
     * Constructor
     * @param InDevice RHI device for resource creation
     */
    explicit FShadowDepthPass(MonsterRender::RHI::IRHIDevice* InDevice);
    
    /** Destructor */
    virtual ~FShadowDepthPass();
    
    // ========================================================================
    // Initialization
    // ========================================================================
    
    /**
     * Initialize the shadow depth pass
     * Creates shaders, pipeline states, and uniform buffers
     * @return True if initialization succeeded
     */
    bool initialize();
    
    /**
     * Release all resources
     */
    void release();
    
    /**
     * Check if the pass is initialized
     * @return True if initialized
     */
    bool isInitialized() const { return m_bInitialized; }
    
    // ========================================================================
    // IRenderPass Interface
    // ========================================================================
    
    virtual ERenderPassType GetPassType() const override { return ERenderPassType::ShadowDepth; }
    virtual const TCHAR* GetPassName() const override { return TEXT("ShadowDepth"); }
    virtual bool ShouldExecute(const FRenderPassContext& Context) const override;
    virtual void Setup(FRenderPassContext& Context) override;
    virtual void Execute(FRenderPassContext& Context) override;
    virtual void Cleanup(FRenderPassContext& Context) override;
    
    // ========================================================================
    // Shadow Rendering
    // ========================================================================
    
    /**
     * Render shadow depth for a projected shadow
     * @param RHICmdList Command list for GPU commands
     * @param ShadowInfo Shadow projection information
     * @param ShadowMap Target shadow map
     */
    void renderShadowDepth(
        MonsterRender::RHI::IRHICommandList& RHICmdList,
        const FProjectedShadowInfo* ShadowInfo,
        FShadowMap* ShadowMap);
    
    /**
     * Render shadow depth for multiple shadows (batch)
     * @param RHICmdList Command list for GPU commands
     * @param Shadows Array of shadow infos to render
     */
    void renderShadowDepthBatch(
        MonsterRender::RHI::IRHICommandList& RHICmdList,
        const TArray<FProjectedShadowInfo*>& Shadows);
    
    /**
     * Clear shadow depth target
     * @param RHICmdList Command list for GPU commands
     * @param ShadowMap Shadow map to clear
     */
    void clearShadowDepth(
        MonsterRender::RHI::IRHICommandList& RHICmdList,
        FShadowMap* ShadowMap);
    
    // ========================================================================
    // Configuration
    // ========================================================================
    
    /**
     * Get the shadow depth pass configuration
     * @return Reference to configuration
     */
    const FShadowDepthPassConfig& getConfig() const { return m_shadowConfig; }
    
    /**
     * Set the shadow depth pass configuration
     * @param InConfig New configuration
     */
    void setConfig(const FShadowDepthPassConfig& InConfig);
    
    // ========================================================================
    // Uniform Buffer Management
    // ========================================================================
    
    /**
     * Update uniform buffer with shadow parameters
     * @param ShadowInfo Shadow projection information
     */
    void updateUniformBuffer(const FProjectedShadowInfo* ShadowInfo);
    
    /**
     * Get the current uniform parameters
     * @return Reference to uniform parameters
     */
    const FShadowDepthPassUniformParameters& getUniformParameters() const { return m_uniformParams; }
    
protected:
    // ========================================================================
    // Internal Methods
    // ========================================================================
    
    /**
     * Load and compile shadow depth shaders
     * @return True if shaders loaded successfully
     */
    bool _loadShaders();
    
    /**
     * Create pipeline state for shadow depth rendering
     * @return True if pipeline state created successfully
     */
    bool _createPipelineState();
    
    /**
     * Create uniform buffer for shadow pass data
     * @return True if uniform buffer created successfully
     */
    bool _createUniformBuffer();
    
    /**
     * Bind pipeline state and resources
     * @param RHICmdList Command list for GPU commands
     */
    void _bindPipelineState(MonsterRender::RHI::IRHICommandList& RHICmdList);
    
    /**
     * Render primitives for shadow depth
     * @param RHICmdList Command list for GPU commands
     * @param ShadowInfo Shadow projection information
     */
    void _renderShadowPrimitives(
        MonsterRender::RHI::IRHICommandList& RHICmdList,
        const FProjectedShadowInfo* ShadowInfo);
    
    /**
     * Set render target for shadow depth
     * @param RHICmdList Command list for GPU commands
     * @param ShadowMap Target shadow map
     */
    void _setRenderTarget(
        MonsterRender::RHI::IRHICommandList& RHICmdList,
        FShadowMap* ShadowMap);
    
    /**
     * Set viewport for shadow rendering
     * @param RHICmdList Command list for GPU commands
     * @param ShadowInfo Shadow projection information
     */
    void _setViewport(
        MonsterRender::RHI::IRHICommandList& RHICmdList,
        const FProjectedShadowInfo* ShadowInfo);
    
private:
    // ========================================================================
    // Member Variables
    // ========================================================================
    
    /** RHI device reference */
    MonsterRender::RHI::IRHIDevice* m_device;
    
    /** Shadow depth vertex shader */
    TSharedPtr<MonsterRender::RHI::IRHIVertexShader> m_vertexShader;
    
    /** Shadow depth pixel shader */
    TSharedPtr<MonsterRender::RHI::IRHIPixelShader> m_pixelShader;
    
    /** Pipeline state for shadow depth rendering */
    TSharedPtr<MonsterRender::RHI::IRHIPipelineState> m_pipelineState;
    
    /** Pipeline state for two-sided shadow rendering */
    TSharedPtr<MonsterRender::RHI::IRHIPipelineState> m_pipelineStateTwoSided;
    
    /** Uniform buffer for shadow pass data */
    TSharedPtr<MonsterRender::RHI::IRHIBuffer> m_uniformBuffer;
    
    /** Current uniform parameters */
    FShadowDepthPassUniformParameters m_uniformParams;
    
    /** Push constants */
    FShadowDepthPassPushConstants m_pushConstants;
    
    /** Shadow depth pass configuration */
    FShadowDepthPassConfig m_shadowConfig;
    
    /** Whether the pass is initialized */
    bool m_bInitialized;
    
    /** Mutex for thread safety */
    mutable std::mutex m_mutex;
    
    /** Shader file paths */
    static constexpr const char* SHADOW_DEPTH_VERT_PATH = "Shaders/Forward/ShadowDepth.vert";
    static constexpr const char* SHADOW_DEPTH_FRAG_PATH = "Shaders/Forward/ShadowDepth.frag";
};

// ============================================================================
// Shadow Depth Pass Processor
// ============================================================================

/**
 * @class FShadowDepthPassProcessor
 * @brief Processes mesh elements for shadow depth rendering
 * 
 * Collects and processes mesh draw commands for shadow depth pass.
 * Reference: UE5 FShadowDepthPassMeshProcessor
 */
class FShadowDepthPassProcessor
{
public:
    /**
     * Constructor
     * @param InScene Scene being rendered
     * @param InShadowInfo Shadow projection information
     */
    FShadowDepthPassProcessor(FScene* InScene, const FProjectedShadowInfo* InShadowInfo);
    
    /** Destructor */
    ~FShadowDepthPassProcessor();
    
    /**
     * Add mesh batch for shadow depth rendering
     * @param MeshBatch Mesh batch to add
     * @param bCastShadow Whether the mesh casts shadows
     * @return True if mesh was added
     */
    bool addMeshBatch(const FMeshBatch& MeshBatch, bool bCastShadow);
    
    /**
     * Process all collected mesh batches
     * @param RHICmdList Command list for GPU commands
     * @param ShadowDepthPass Shadow depth pass for rendering
     */
    void process(
        MonsterRender::RHI::IRHICommandList& RHICmdList,
        FShadowDepthPass* ShadowDepthPass);
    
    /**
     * Get the number of collected mesh batches
     * @return Number of mesh batches
     */
    int32 getNumMeshBatches() const { return m_meshBatches.Num(); }
    
    /**
     * Clear all collected mesh batches
     */
    void clear();
    
private:
    /** Scene being rendered */
    FScene* m_scene;
    
    /** Shadow projection information */
    const FProjectedShadowInfo* m_shadowInfo;
    
    /** Collected mesh batches */
    TArray<FMeshBatch> m_meshBatches;
    
    /** Whether each mesh requires alpha testing */
    TArray<bool> m_meshRequiresAlphaTest;
};

} // namespace Renderer
} // namespace MonsterEngine
