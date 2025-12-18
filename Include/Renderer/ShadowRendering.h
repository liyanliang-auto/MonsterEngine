// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file ShadowRendering.h
 * @brief Shadow rendering classes and definitions
 * 
 * This file defines the core shadow rendering infrastructure including:
 * - FShadowMapRenderTargets: Shadow map render target management
 * - FShadowMap: Shadow map texture wrapper
 * - FProjectedShadowInfo: Complete shadow projection information
 * 
 * Reference: UE5 Engine/Source/Runtime/Renderer/Private/ShadowRendering.h
 */

#include "Core/CoreMinimal.h"
#include "Core/CoreTypes.h"
#include "Containers/Array.h"
#include "Math/Vector.h"
#include "Math/Vector4.h"
#include "Math/Matrix.h"
#include "Math/Sphere.h"
#include "Math/Box.h"
#include "Core/Templates/SharedPointer.h"
#include "Engine/SceneView.h"
#include <mutex>

// Forward declarations for RHI types
namespace MonsterRender { 
namespace RHI {
    class IRHIDevice;
    class IRHICommandList;
    class IRHITexture;
    class FRHITexture2D;
}}

namespace MonsterEngine
{

// Forward declarations
class FScene;

namespace Renderer
{

// Forward declarations within Renderer namespace
class FScene;
class FViewInfo;
class FLightSceneInfo;
class FLightSceneProxy;
class FPrimitiveSceneInfo;
class FPrimitiveSceneProxy;
class FSceneRenderer;

// Bring RHI types into scope
using MonsterRender::RHI::IRHIDevice;
using MonsterRender::RHI::IRHICommandList;
using MonsterRender::RHI::IRHITexture;
using MonsterRender::RHI::FRHITexture2D;

// ============================================================================
// EShadowDepthRenderMode - Shadow depth rendering mode
// ============================================================================

/**
 * @enum EShadowDepthRenderMode
 * @brief Shadow depth rendering mode enumeration
 * 
 * Reference: UE5 EShadowDepthRenderMode
 */
enum class EShadowDepthRenderMode : uint8
{
    /** Standard shadow depth rendering */
    Normal = 0,
    
    /** Emissive-only objects for RSM injection */
    EmissiveOnly,
    
    /** GI blocking volumes */
    GIBlockingVolumes,
};

// ============================================================================
// EShadowDepthCacheMode - Shadow depth caching mode
// ============================================================================

/**
 * @enum EShadowDepthCacheMode
 * @brief Shadow map caching mode enumeration
 * 
 * Reference: UE5 EShadowDepthCacheMode
 */
enum class EShadowDepthCacheMode : uint8
{
    /** Only movable primitives rendered */
    MovablePrimitivesOnly = 0,
    
    /** Only static primitives rendered */
    StaticPrimitivesOnly,
    
    /** CSM scrolling mode */
    CSMScrolling,
    
    /** No caching */
    Uncached,
};

// ============================================================================
// FShadowDepthType - Shadow depth type descriptor
// ============================================================================

/**
 * @struct FShadowDepthType
 * @brief Describes the type of shadow depth pass
 * 
 * Used for shader permutation selection and pipeline state caching.
 * Reference: UE5 FShadowDepthType
 */
struct FShadowDepthType
{
    /** Whether this is a directional light shadow */
    bool bDirectionalLight;
    
    /** Whether this is a one-pass point light shadow (cube map) */
    bool bOnePassPointLightShadow;
    
    /** Constructor */
    FShadowDepthType(bool InDirectionalLight = false, bool InOnePassPointLightShadow = false)
        : bDirectionalLight(InDirectionalLight)
        , bOnePassPointLightShadow(InOnePassPointLightShadow)
    {}
    
    /** Equality operator */
    bool operator==(const FShadowDepthType& Other) const
    {
        return bDirectionalLight == Other.bDirectionalLight &&
               bOnePassPointLightShadow == Other.bOnePassPointLightShadow;
    }
    
    /** Inequality operator */
    bool operator!=(const FShadowDepthType& Other) const
    {
        return !(*this == Other);
    }
};

// ============================================================================
// FShadowBiasParameters - Shadow depth bias parameters
// ============================================================================

/**
 * @struct FShadowBiasParameters
 * @brief Shadow depth bias configuration
 * 
 * Contains all bias parameters used to prevent shadow acne and peter-panning.
 * Reference: UE5 shadow bias parameters in FProjectedShadowInfo
 */
struct FShadowBiasParameters
{
    /** Constant depth bias applied in clip space */
    float ConstantDepthBias;
    
    /** Slope-scaled depth bias based on surface angle */
    float SlopeScaledDepthBias;
    
    /** Maximum slope bias clamp value */
    float MaxSlopeDepthBias;
    
    /** Normal offset bias for receiver surfaces */
    float NormalOffsetBias;
    
    /** Receiver depth bias for shadow projection */
    float ReceiverDepthBias;
    
    /** Default constructor with reasonable defaults */
    FShadowBiasParameters()
        : ConstantDepthBias(0.0005f)
        , SlopeScaledDepthBias(2.0f)
        , MaxSlopeDepthBias(0.1f)
        , NormalOffsetBias(0.01f)
        , ReceiverDepthBias(0.0f)
    {}
};

// ============================================================================
// FShadowMapRenderTargets - Shadow map render targets container
// ============================================================================

/**
 * @class FShadowMapRenderTargets
 * @brief Container for shadow map render targets
 * 
 * Manages color and depth targets used for shadow map rendering.
 * Reference: UE5 FShadowMapRenderTargets
 */
class FShadowMapRenderTargets
{
public:
    /** Color targets (for translucent shadows, VSM, etc.) */
    TArray<IRHITexture*> ColorTargets;
    
    /** Depth target for shadow depth */
    IRHITexture* DepthTarget;
    
    /** Constructor */
    FShadowMapRenderTargets()
        : DepthTarget(nullptr)
    {}
    
    /** Destructor */
    ~FShadowMapRenderTargets() = default;
    
    /**
     * Get the size of the render targets
     * @return Size in pixels
     */
    FIntPoint getSize() const;
    
    /**
     * Check if render targets are valid
     * @return true if valid
     */
    bool isValid() const
    {
        return DepthTarget != nullptr || ColorTargets.Num() > 0;
    }
    
    /**
     * Release all render targets
     */
    void release();
};

// ============================================================================
// FShadowMap - Shadow map texture wrapper
// ============================================================================

/**
 * @class FShadowMap
 * @brief Shadow map texture management
 * 
 * Wraps a shadow map depth texture with associated metadata.
 * Supports both 2D shadow maps and cube shadow maps for point lights.
 * 
 * Reference: UE5 shadow map management in FProjectedShadowInfo
 */
class FShadowMap
{
public:
    /** Constructor */
    FShadowMap();
    
    /** Destructor */
    ~FShadowMap();
    
    // Non-copyable
    FShadowMap(const FShadowMap&) = delete;
    FShadowMap& operator=(const FShadowMap&) = delete;
    
    // Movable
    FShadowMap(FShadowMap&& Other) noexcept;
    FShadowMap& operator=(FShadowMap&& Other) noexcept;
    
    /**
     * Initialize the shadow map
     * @param InDevice RHI device
     * @param InResolution Shadow map resolution
     * @param bInCubeMap Whether this is a cube map for point lights
     * @return true if successful
     */
    bool initialize(IRHIDevice* InDevice, uint32 InResolution, bool bInCubeMap = false);
    
    /**
     * Release shadow map resources
     */
    void release();
    
    /**
     * Check if shadow map is initialized
     * @return true if initialized
     */
    bool isInitialized() const { return m_depthTexture != nullptr; }
    
    // Accessors
    IRHITexture* getDepthTexture() const { return m_depthTexture; }
    uint32 getResolution() const { return m_resolution; }
    uint32 getBorderSize() const { return m_borderSize; }
    bool isCubeMap() const { return m_bCubeMap; }
    
    /**
     * Get the effective resolution (excluding border)
     * @return Inner resolution
     */
    uint32 getInnerResolution() const
    {
        return m_resolution > 2 * m_borderSize ? m_resolution - 2 * m_borderSize : 0;
    }

private:
    /** RHI device reference */
    IRHIDevice* m_device;
    
    /** Shadow map depth texture */
    IRHITexture* m_depthTexture;
    
    /** Shadow map resolution (total, including border) */
    uint32 m_resolution;
    
    /** Border size for PCF filtering */
    uint32 m_borderSize;
    
    /** Whether this is a cube map */
    bool m_bCubeMap;
    
    /** Thread safety mutex */
    mutable std::mutex m_mutex;
};

// ============================================================================
// FProjectedShadowInfo - Projected shadow information
// ============================================================================

/**
 * @class FProjectedShadowInfo
 * @brief Complete information about a projected shadow
 * 
 * Contains all data necessary to render and project a shadow:
 * - Light source information
 * - View and projection matrices
 * - Shadow map allocation
 * - Depth bias parameters
 * - Shadow type flags
 * 
 * Reference: UE5 FProjectedShadowInfo
 */
class FProjectedShadowInfo
{
public:
    // ========================================================================
    // Type Definitions
    // ========================================================================
    
    /** Array type for primitive scene infos */
    using PrimitiveArrayType = TArray<const FPrimitiveSceneInfo*>;

public:
    // ========================================================================
    // Construction and Destruction
    // ========================================================================
    
    /** Default constructor */
    FProjectedShadowInfo();
    
    /** Destructor */
    ~FProjectedShadowInfo();
    
    // Non-copyable
    FProjectedShadowInfo(const FProjectedShadowInfo&) = delete;
    FProjectedShadowInfo& operator=(const FProjectedShadowInfo&) = delete;
    
    // ========================================================================
    // Shadow Setup Methods
    // ========================================================================
    
    /**
     * Setup for per-object shadow projection
     * @param InLightSceneInfo Light source
     * @param InParentSceneInfo Parent primitive
     * @param InResolutionX Horizontal resolution
     * @param InResolutionY Vertical resolution
     * @param InBorderSize Border size for filtering
     * @param InMaxScreenPercent Maximum screen coverage
     * @param bInPreShadow Whether this is a preshadow
     * @param bInTranslucentShadow Whether for translucent shadow
     * @return true if setup successful
     */
    bool setupPerObjectProjection(
        FLightSceneInfo* InLightSceneInfo,
        const FPrimitiveSceneInfo* InParentSceneInfo,
        uint32 InResolutionX,
        uint32 InResolutionY,
        uint32 InBorderSize,
        float InMaxScreenPercent,
        bool bInPreShadow,
        bool bInTranslucentShadow);
    
    /**
     * Setup for whole scene shadow projection (directional light)
     * @param InLightSceneInfo Light source
     * @param InDependentView View this shadow depends on
     * @param InResolutionX Horizontal resolution
     * @param InResolutionY Vertical resolution
     * @param InBorderSize Border size for filtering
     */
    void setupWholeSceneProjection(
        FLightSceneInfo* InLightSceneInfo,
        FViewInfo* InDependentView,
        uint32 InResolutionX,
        uint32 InResolutionY,
        uint32 InBorderSize);
    
    /**
     * Setup directional light shadow with explicit parameters
     * This is the main entry point for directional light shadow setup
     * @param InLightSceneInfo Light source
     * @param InDependentView View this shadow depends on
     * @param InLightDirection Light direction (normalized)
     * @param InShadowBounds Shadow bounding sphere
     * @param InResolutionX Horizontal resolution
     * @param InResolutionY Vertical resolution
     * @param InBorderSize Border size for filtering
     * @param InCascadeIndex Cascade index for CSM (-1 for single shadow)
     * @return true if setup successful
     */
    bool setupDirectionalLightShadow(
        FLightSceneInfo* InLightSceneInfo,
        FViewInfo* InDependentView,
        const FVector& InLightDirection,
        const FSphere& InShadowBounds,
        uint32 InResolutionX,
        uint32 InResolutionY,
        uint32 InBorderSize,
        int32 InCascadeIndex = -1);
    
    /**
     * Compute shadow view and projection matrices for directional light
     * Called internally after setupDirectionalLightShadow
     */
    void computeDirectionalLightMatrices();
    
    /**
     * Setup for point light shadow (cube map)
     * @param InLightSceneInfo Light source
     * @param InResolution Shadow map resolution per face
     * @param InBorderSize Border size for filtering
     */
    void setupPointLightShadow(
        FLightSceneInfo* InLightSceneInfo,
        uint32 InResolution,
        uint32 InBorderSize);
    
    /**
     * Setup for spot light shadow
     * @param InLightSceneInfo Light source
     * @param InResolutionX Horizontal resolution
     * @param InResolutionY Vertical resolution
     * @param InBorderSize Border size for filtering
     */
    void setupSpotLightShadow(
        FLightSceneInfo* InLightSceneInfo,
        uint32 InResolutionX,
        uint32 InResolutionY,
        uint32 InBorderSize);
    
    // ========================================================================
    // Matrix Computation
    // ========================================================================
    
    /**
     * Compute view matrices for shadow depth rendering
     * @param OutViewMatrices Output view matrices
     * @param CubeFaceIndex Cube face index for point lights (-1 for non-cube)
     * @return true if successful
     */
    bool computeShadowDepthViewMatrices(FMatrix* OutViewMatrices, int32 CubeFaceIndex = -1) const;
    
    /**
     * Get screen to shadow matrix
     * @param View The view to transform from
     * @return Screen to shadow space transformation matrix
     */
    FMatrix getScreenToShadowMatrix(const FViewInfo& View) const;
    
    /**
     * Get world to shadow matrix
     * @param OutShadowmapMinMax Output min/max values
     * @return World to shadow space transformation matrix
     */
    FMatrix getWorldToShadowMatrix(FVector4f& OutShadowmapMinMax) const;
    
    // ========================================================================
    // Depth Bias Management
    // ========================================================================
    
    /**
     * Update shader depth bias values
     * Called after shadow setup to compute final bias values
     */
    void updateShaderDepthBias();
    
    /**
     * Compute transition size for soft PCF
     * @return Transition size value
     */
    float computeTransitionSize() const;
    
    // ========================================================================
    // Render Target Management
    // ========================================================================
    
    /**
     * Allocate shadow depth render target
     * @param InDevice RHI device for resource creation
     * @return true if allocation succeeded
     */
    bool allocateRenderTargets(IRHIDevice* InDevice);
    
    /**
     * Release shadow depth render target
     */
    void releaseRenderTargets();
    
    /**
     * Check if render targets are allocated
     * @return true if allocated
     */
    bool hasRenderTargets() const { return RenderTargets.isValid(); }
    
    /**
     * Render shadow depth for this shadow
     * @param RHICmdList Command list for GPU commands
     * @param SceneRenderer Scene renderer reference
     */
    void renderDepth(IRHICommandList& RHICmdList, class FSceneRenderer* SceneRenderer);
    
    /**
     * Set viewport and scissor for shadow rendering
     * @param RHICmdList Command list for GPU commands
     */
    void setStateForView(IRHICommandList& RHICmdList) const;
    
    // ========================================================================
    // Accessors - Bias Parameters
    // ========================================================================
    
    float getShaderDepthBias() const { return m_shaderDepthBias; }
    float getShaderSlopeDepthBias() const { return m_shaderSlopeDepthBias; }
    float getShaderMaxSlopeDepthBias() const { return m_shaderMaxSlopeDepthBias; }
    float getShaderReceiverDepthBias() const;
    
    // ========================================================================
    // View Rect Methods
    // ========================================================================
    
    /**
     * Get inner view rect (excluding border)
     * @return Inner view rectangle
     */
    FIntRect getInnerViewRect() const
    {
        return FIntRect(
            static_cast<int32>(X + BorderSize),
            static_cast<int32>(Y + BorderSize),
            static_cast<int32>(X + BorderSize + ResolutionX),
            static_cast<int32>(Y + BorderSize + ResolutionY));
    }
    
    /**
     * Get outer view rect (including border)
     * @return Outer view rectangle
     */
    FIntRect getOuterViewRect() const
    {
        return FIntRect(
            static_cast<int32>(X),
            static_cast<int32>(Y),
            static_cast<int32>(X + 2 * BorderSize + ResolutionX),
            static_cast<int32>(Y + 2 * BorderSize + ResolutionY));
    }
    
    // ========================================================================
    // Query Methods
    // ========================================================================
    
    /**
     * Check if this is a whole scene directional shadow
     * @return true if whole scene directional shadow
     */
    bool isWholeSceneDirectionalShadow() const
    {
        return bWholeSceneShadow && bDirectionalLight;
    }
    
    /**
     * Check if this is a whole scene point light shadow
     * @return true if whole scene point light shadow
     */
    bool isWholeScenePointLightShadow() const
    {
        return bWholeSceneShadow && bOnePassPointLightShadow;
    }
    
    /**
     * Check if should clamp to near plane
     * @return true if should clamp
     */
    bool shouldClampToNearPlane() const
    {
        return isWholeSceneDirectionalShadow() || (bPreShadow && bDirectionalLight);
    }
    
    /**
     * Get shadow depth type
     * @return Shadow depth type descriptor
     */
    FShadowDepthType getShadowDepthType() const
    {
        return FShadowDepthType(bDirectionalLight, bOnePassPointLightShadow);
    }
    
    /**
     * Check if has subject primitives
     * @return true if has subjects
     */
    bool hasSubjectPrims() const;
    
    /**
     * Get light scene info
     * @return Light scene info reference
     */
    FLightSceneInfo* getLightSceneInfo() const { return m_lightSceneInfo; }
    
    /**
     * Get parent scene info
     * @return Parent primitive scene info
     */
    const FPrimitiveSceneInfo* getParentSceneInfo() const { return m_parentSceneInfo; }

public:
    // ========================================================================
    // Public Members - Shadow View Data
    // ========================================================================
    
    /** View used for shadow depth rendering */
    FViewInfo* ShadowDepthView;
    
    /** Render targets for this shadow */
    FShadowMapRenderTargets RenderTargets;
    
    /** Shadow depth cache mode */
    EShadowDepthCacheMode CacheMode;
    
    /** Main view this shadow depends on (null for view-independent shadows) */
    FViewInfo* DependentView;
    
    /** Shadow ID within FVisibleLightInfo::AllProjectedShadows */
    int32 ShadowId;
    
    // ========================================================================
    // Public Members - Transform Matrices
    // ========================================================================
    
    /** Translation applied before shadow transform */
    FVector PreShadowTranslation;
    
    /** World to light view matrix */
    FMatrix TranslatedWorldToView;
    
    /** View to clip inner (excluding border) */
    FMatrix ViewToClipInner;
    
    /** View to clip outer (including border) */
    FMatrix ViewToClipOuter;
    
    /** World to clip inner matrix (for depth rendering) */
    FMatrix44f TranslatedWorldToClipInnerMatrix;
    
    /** World to clip outer matrix */
    FMatrix44f TranslatedWorldToClipOuterMatrix;
    
    /** Inverse receiver inner matrix */
    FMatrix44f InvReceiverInnerMatrix;
    
    // ========================================================================
    // Public Members - Depth Range
    // ========================================================================
    
    /** Inverse of max subject depth */
    float InvMaxSubjectDepth;
    
    /** Maximum subject Z in world space */
    float MaxSubjectZ;
    
    /** Minimum subject Z in world space */
    float MinSubjectZ;
    
    /** Minimum pre-subject Z */
    float MinPreSubjectZ;
    
    // ========================================================================
    // Public Members - Bounds
    // ========================================================================
    
    /** Shadow bounding sphere */
    FSphere ShadowBounds;
    
    // ========================================================================
    // Public Members - Allocation
    // ========================================================================
    
    /** X position in shadow atlas */
    uint32 X;
    
    /** Y position in shadow atlas */
    uint32 Y;
    
    /** Horizontal resolution (excluding border) */
    uint32 ResolutionX;
    
    /** Vertical resolution (excluding border) */
    uint32 ResolutionY;
    
    /** Border size for filtering */
    uint32 BorderSize;
    
    /** Maximum screen percentage */
    float MaxScreenPercent;
    
    // ========================================================================
    // Public Members - Fade
    // ========================================================================
    
    /** Per-view fade alpha values */
    TArray<float> FadeAlphas;
    
    // ========================================================================
    // Public Members - Flags
    // ========================================================================
    
    /** Whether allocated in shadow depth buffer */
    uint32 bAllocated : 1;
    
    /** Whether projection has been rendered */
    uint32 bRendered : 1;
    
    /** Whether allocated in preshadow cache */
    uint32 bAllocatedInPreshadowCache : 1;
    
    /** Whether depths are cached */
    uint32 bDepthsCached : 1;
    
    /** Whether this is a directional light shadow */
    uint32 bDirectionalLight : 1;
    
    /** Whether one-pass point light shadow (cube map) */
    uint32 bOnePassPointLightShadow : 1;
    
    /** Whether affects whole scene */
    uint32 bWholeSceneShadow : 1;
    
    /** Whether supports translucent shadows */
    uint32 bTranslucentShadow : 1;
    
    /** Whether using ray traced distance field */
    uint32 bRayTracedDistanceField : 1;
    
    /** Whether capsule shadow */
    uint32 bCapsuleShadow : 1;
    
    /** Whether preshadow */
    uint32 bPreShadow : 1;
    
    /** Whether self-shadow only */
    uint32 bSelfShadowOnly : 1;
    
    /** Whether per-object opaque shadow */
    uint32 bPerObjectOpaqueShadow : 1;
    
    /** Whether transmission enabled */
    uint32 bTransmission : 1;
    
    // ========================================================================
    // Public Members - Point Light Cube Shadow
    // ========================================================================
    
    /** View-projection matrices for each cube face */
    TArray<FMatrix> OnePassShadowViewProjectionMatrices;
    
    /** View matrices for each cube face */
    TArray<FMatrix> OnePassShadowViewMatrices;
    
    /** Face projection matrix for cube shadows */
    FMatrix OnePassShadowFaceProjectionMatrix;
    
    // ========================================================================
    // Public Members - Per-Object Shadow
    // ========================================================================
    
    /** Per-object shadow fade start distance */
    float PerObjectShadowFadeStart;
    
    /** Inverse per-object shadow fade length */
    float InvPerObjectShadowFadeLength;
    
    // ========================================================================
    // Public Members - Bias Configuration
    // ========================================================================
    
    /** Bias parameters */
    FShadowBiasParameters BiasParameters;

private:
    // ========================================================================
    // Private Members
    // ========================================================================
    
    /** Light scene info */
    FLightSceneInfo* m_lightSceneInfo;
    
    /** Parent primitive scene info (for per-object shadows) */
    const FPrimitiveSceneInfo* m_parentSceneInfo;
    
    /** Dynamic shadow casting primitives */
    PrimitiveArrayType m_dynamicSubjectPrimitives;
    
    /** Receiver primitives for preshadows */
    PrimitiveArrayType m_receiverPrimitives;
    
    /** Computed shader depth bias */
    float m_shaderDepthBias;
    
    /** Computed shader slope depth bias */
    float m_shaderSlopeDepthBias;
    
    /** Computed shader max slope depth bias */
    float m_shaderMaxSlopeDepthBias;
    
    /** Thread safety mutex */
    mutable std::mutex m_mutex;
    
private:
    // ========================================================================
    // Private Methods
    // ========================================================================
    
    /**
     * Compute light view matrix for directional light
     * @param LightDirection Light direction vector
     */
    void _computeDirectionalLightViewMatrix(const FVector& LightDirection);
    
    /**
     * Compute combined world-to-clip matrices
     * Called after view and projection matrices are set
     */
    void _computeWorldToClipMatrices();
    
    /**
     * Compute projection matrix for orthographic shadow
     * @param ShadowBoundsRadius Shadow bounds radius
     * @param NearPlane Near clip plane
     * @param FarPlane Far clip plane
     */
    void _computeOrthographicProjection(float ShadowBoundsRadius, float NearPlane, float FarPlane);
    
    /**
     * Compute projection matrix for perspective shadow
     * @param FOVAngle Field of view angle in radians
     * @param AspectRatio Aspect ratio
     * @param NearPlane Near clip plane
     * @param FarPlane Far clip plane
     */
    void _computePerspectiveProjection(float FOVAngle, float AspectRatio, float NearPlane, float FarPlane);
    
    /**
     * Setup cube face matrices for point light
     * @param LightPosition Light world position
     * @param LightRadius Light attenuation radius
     */
    void _setupCubeFaceMatrices(const FVector& LightPosition, float LightRadius);
};

// ============================================================================
// FShadowSceneRenderer - Shadow scene rendering manager
// ============================================================================

/**
 * @class FShadowSceneRenderer
 * @brief Manages shadow rendering for a scene
 * 
 * Responsible for:
 * - Shadow setup and allocation
 * - Shadow depth pass rendering
 * - Shadow projection
 * 
 * Reference: UE5 FShadowSceneRenderer
 */
class FShadowSceneRenderer
{
public:
    /** Constructor */
    FShadowSceneRenderer();
    
    /** Destructor */
    ~FShadowSceneRenderer();
    
    /**
     * Initialize the shadow scene renderer
     * @param InDevice RHI device
     * @return true if successful
     */
    bool initialize(IRHIDevice* InDevice);
    
    /**
     * Shutdown and release resources
     */
    void shutdown();
    
    /**
     * Allocate shadow maps for visible shadows
     * @param Scene The scene being rendered
     * @param Views Array of views
     * @return Number of shadows allocated
     */
    int32 allocateShadowMaps(FScene* Scene, const TArray<FViewInfo*>& Views);
    
    /**
     * Get all projected shadow infos
     * @return Array of projected shadow infos
     */
    const TArray<TSharedPtr<FProjectedShadowInfo>>& getProjectedShadows() const
    {
        return m_projectedShadows;
    }
    
    /**
     * Clear all shadow data for new frame
     */
    void clearShadows();

private:
    /** RHI device reference */
    IRHIDevice* m_device;
    
    /** All projected shadows for current frame */
    TArray<TSharedPtr<FProjectedShadowInfo>> m_projectedShadows;
    
    /** Shadow atlas for 2D shadows */
    TSharedPtr<FShadowMap> m_shadowAtlas;
    
    /** Point light shadow cube maps */
    TArray<TSharedPtr<FShadowMap>> m_pointLightShadowMaps;
    
    /** Maximum shadow atlas resolution */
    uint32 m_maxAtlasResolution;
    
    /** Maximum point light shadow resolution */
    uint32 m_maxPointLightResolution;
    
    /** Thread safety mutex */
    mutable std::mutex m_mutex;
};

} // namespace Renderer
} // namespace MonsterEngine
