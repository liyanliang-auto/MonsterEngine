// Copyright Monster Engine. All Rights Reserved.

/**
 * @file ShadowRendering.cpp
 * @brief Shadow rendering implementation
 * 
 * Implements shadow map management and projected shadow information.
 * Reference: UE5 Engine/Source/Runtime/Renderer/Private/ShadowRendering.cpp
 */

#include "Renderer/ShadowRendering.h"
#include "Renderer/Scene.h"
#include "Renderer/SceneView.h"
#include "Renderer/SceneRenderer.h"
#include "RHI/RHIResources.h"
#include "RHI/IRHIDevice.h"
#include "RHI/IRHICommandList.h"
#include "Core/Logging/LogMacros.h"
#include "Math/MathFunctions.h"
#include "Math/MathUtility.h"
#include <cmath>

// Define log category for shadow rendering
DEFINE_LOG_CATEGORY_STATIC(LogShadowRendering, Log, All);

namespace MonsterEngine
{
namespace Renderer
{

// ============================================================================
// FShadowMapRenderTargets Implementation
// ============================================================================

FIntPoint FShadowMapRenderTargets::getSize() const
{
    if (DepthTarget)
    {
        return FIntPoint(
            static_cast<int32>(DepthTarget->getWidth()),
            static_cast<int32>(DepthTarget->getHeight()));
    }
    else if (ColorTargets.Num() > 0 && ColorTargets[0])
    {
        return FIntPoint(
            static_cast<int32>(ColorTargets[0]->getWidth()),
            static_cast<int32>(ColorTargets[0]->getHeight()));
    }
    
    return FIntPoint(0, 0);
}

void FShadowMapRenderTargets::release()
{
    // Note: Actual resource cleanup is handled by RHI device
    // We just clear references here
    DepthTarget = nullptr;
    ColorTargets.Empty();
}

// ============================================================================
// FShadowMap Implementation
// ============================================================================

FShadowMap::FShadowMap()
    : m_device(nullptr)
    , m_depthTexture(nullptr)
    , m_resolution(0)
    , m_borderSize(4)
    , m_bCubeMap(false)
{
}

FShadowMap::~FShadowMap()
{
    release();
}

FShadowMap::FShadowMap(FShadowMap&& Other) noexcept
    : m_device(Other.m_device)
    , m_depthTexture(Other.m_depthTexture)
    , m_resolution(Other.m_resolution)
    , m_borderSize(Other.m_borderSize)
    , m_bCubeMap(Other.m_bCubeMap)
{
    Other.m_device = nullptr;
    Other.m_depthTexture = nullptr;
    Other.m_resolution = 0;
}

FShadowMap& FShadowMap::operator=(FShadowMap&& Other) noexcept
{
    if (this != &Other)
    {
        release();
        
        m_device = Other.m_device;
        m_depthTexture = Other.m_depthTexture;
        m_resolution = Other.m_resolution;
        m_borderSize = Other.m_borderSize;
        m_bCubeMap = Other.m_bCubeMap;
        
        Other.m_device = nullptr;
        Other.m_depthTexture = nullptr;
        Other.m_resolution = 0;
    }
    return *this;
}

bool FShadowMap::initialize(IRHIDevice* InDevice, uint32 InResolution, bool bInCubeMap)
{
    std::lock_guard<std::mutex> Lock(m_mutex);
    
    if (!InDevice)
    {
        MR_LOG(LogShadowRendering, Error, "FShadowMap::initialize - Invalid device");
        return false;
    }
    
    if (InResolution == 0)
    {
        MR_LOG(LogShadowRendering, Error, "FShadowMap::initialize - Invalid resolution: 0");
        return false;
    }
    
    // Release any existing resources
    if (m_depthTexture)
    {
        release();
    }
    
    m_device = InDevice;
    m_resolution = InResolution;
    m_bCubeMap = bInCubeMap;
    
    // TODO: Create actual depth texture via RHI device
    // This will be implemented when integrating with the render pass system
    // For now, we just store the configuration
    
    MR_LOG(LogShadowRendering, Log, 
           "FShadowMap::initialize - Resolution: %u, CubeMap: %s",
           m_resolution, m_bCubeMap ? "true" : "false");
    
    return true;
}

void FShadowMap::release()
{
    std::lock_guard<std::mutex> Lock(m_mutex);
    
    // Note: Actual texture cleanup is handled by RHI device's deferred deletion
    m_depthTexture = nullptr;
    m_resolution = 0;
    m_device = nullptr;
    
    MR_LOG(LogShadowRendering, Log, "FShadowMap::release - Resources released");
}

// ============================================================================
// FProjectedShadowInfo Implementation
// ============================================================================

FProjectedShadowInfo::FProjectedShadowInfo()
    : ShadowDepthView(nullptr)
    , CacheMode(EShadowDepthCacheMode::Uncached)
    , DependentView(nullptr)
    , ShadowId(INDEX_NONE)
    , PreShadowTranslation(FVector::ZeroVector)
    , TranslatedWorldToView(FMatrix::Identity)
    , ViewToClipInner(FMatrix::Identity)
    , ViewToClipOuter(FMatrix::Identity)
    , TranslatedWorldToClipInnerMatrix(FMatrix44f::Identity)
    , TranslatedWorldToClipOuterMatrix(FMatrix44f::Identity)
    , InvReceiverInnerMatrix(FMatrix44f::Identity)
    , InvMaxSubjectDepth(0.0f)
    , MaxSubjectZ(0.0f)
    , MinSubjectZ(0.0f)
    , MinPreSubjectZ(0.0f)
    , ShadowBounds(FVector::ZeroVector, 0.0f)
    , X(0)
    , Y(0)
    , ResolutionX(0)
    , ResolutionY(0)
    , BorderSize(0)
    , MaxScreenPercent(0.0f)
    , bAllocated(0)
    , bRendered(0)
    , bAllocatedInPreshadowCache(0)
    , bDepthsCached(0)
    , bDirectionalLight(0)
    , bOnePassPointLightShadow(0)
    , bWholeSceneShadow(0)
    , bTranslucentShadow(0)
    , bRayTracedDistanceField(0)
    , bCapsuleShadow(0)
    , bPreShadow(0)
    , bSelfShadowOnly(0)
    , bPerObjectOpaqueShadow(0)
    , bTransmission(0)
    , OnePassShadowFaceProjectionMatrix(FMatrix::Identity)
    , PerObjectShadowFadeStart(0.0f)
    , InvPerObjectShadowFadeLength(0.0f)
    , m_lightSceneInfo(nullptr)
    , m_parentSceneInfo(nullptr)
    , m_shaderDepthBias(-1.0f)
    , m_shaderSlopeDepthBias(-1.0f)
    , m_shaderMaxSlopeDepthBias(-1.0f)
{
    // Reserve space for fade alphas
    FadeAlphas.Reserve(4);
    
    // Reserve space for cube face matrices (6 faces)
    OnePassShadowViewProjectionMatrices.Reserve(6);
    OnePassShadowViewMatrices.Reserve(6);
}

FProjectedShadowInfo::~FProjectedShadowInfo()
{
    // Clean up render targets
    RenderTargets.release();
    
    // Clear arrays
    FadeAlphas.Empty();
    OnePassShadowViewProjectionMatrices.Empty();
    OnePassShadowViewMatrices.Empty();
    m_dynamicSubjectPrimitives.Empty();
    m_receiverPrimitives.Empty();
}

bool FProjectedShadowInfo::setupPerObjectProjection(
    FLightSceneInfo* InLightSceneInfo,
    const FPrimitiveSceneInfo* InParentSceneInfo,
    uint32 InResolutionX,
    uint32 InResolutionY,
    uint32 InBorderSize,
    float InMaxScreenPercent,
    bool bInPreShadow,
    bool bInTranslucentShadow)
{
    std::lock_guard<std::mutex> Lock(m_mutex);
    
    if (!InLightSceneInfo)
    {
        MR_LOG(LogShadowRendering, Error, 
               "FProjectedShadowInfo::setupPerObjectProjection - Invalid light scene info");
        return false;
    }
    
    m_lightSceneInfo = InLightSceneInfo;
    m_parentSceneInfo = InParentSceneInfo;
    
    ResolutionX = InResolutionX;
    ResolutionY = InResolutionY;
    BorderSize = InBorderSize;
    MaxScreenPercent = InMaxScreenPercent;
    
    bPreShadow = bInPreShadow ? 1 : 0;
    bTranslucentShadow = bInTranslucentShadow ? 1 : 0;
    bWholeSceneShadow = 0;
    bPerObjectOpaqueShadow = !bInPreShadow && !bInTranslucentShadow ? 1 : 0;
    
    // Determine light type
    // TODO: Get actual light type from FLightSceneInfo when it's fully implemented
    bDirectionalLight = 0;
    bOnePassPointLightShadow = 0;
    
    MR_LOG(LogShadowRendering, Log,
           "FProjectedShadowInfo::setupPerObjectProjection - Resolution: %ux%u, Border: %u",
           ResolutionX, ResolutionY, BorderSize);
    
    return true;
}

void FProjectedShadowInfo::setupWholeSceneProjection(
    FLightSceneInfo* InLightSceneInfo,
    FViewInfo* InDependentView,
    uint32 InResolutionX,
    uint32 InResolutionY,
    uint32 InBorderSize)
{
    std::lock_guard<std::mutex> Lock(m_mutex);
    
    m_lightSceneInfo = InLightSceneInfo;
    m_parentSceneInfo = nullptr;
    DependentView = InDependentView;
    
    ResolutionX = InResolutionX;
    ResolutionY = InResolutionY;
    BorderSize = InBorderSize;
    
    bWholeSceneShadow = 1;
    bDirectionalLight = 1;
    bOnePassPointLightShadow = 0;
    bPerObjectOpaqueShadow = 0;
    
    // TODO: Compute actual view and projection matrices based on light and view frustum
    // This requires access to light direction and view frustum bounds
    
    MR_LOG(LogShadowRendering, Log,
           "FProjectedShadowInfo::setupWholeSceneProjection - Resolution: %ux%u, Border: %u",
           ResolutionX, ResolutionY, BorderSize);
}

bool FProjectedShadowInfo::setupDirectionalLightShadow(
    FLightSceneInfo* InLightSceneInfo,
    FViewInfo* InDependentView,
    const FVector& InLightDirection,
    const FSphere& InShadowBounds,
    uint32 InResolutionX,
    uint32 InResolutionY,
    uint32 InBorderSize,
    int32 InCascadeIndex)
{
    std::lock_guard<std::mutex> Lock(m_mutex);
    
    if (!InLightSceneInfo)
    {
        MR_LOG(LogShadowRendering, Error,
               "FProjectedShadowInfo::setupDirectionalLightShadow - Invalid light scene info");
        return false;
    }
    
    // Store references
    m_lightSceneInfo = InLightSceneInfo;
    m_parentSceneInfo = nullptr;
    DependentView = InDependentView;
    
    // Set resolution and border
    ResolutionX = InResolutionX;
    ResolutionY = InResolutionY;
    BorderSize = InBorderSize;
    
    // Set shadow flags for directional light
    bWholeSceneShadow = 1;
    bDirectionalLight = 1;
    bOnePassPointLightShadow = 0;
    bPerObjectOpaqueShadow = 0;
    bPreShadow = 0;
    bTranslucentShadow = 0;
    bSelfShadowOnly = 0;
    
    // Store shadow bounds
    ShadowBounds = InShadowBounds;
    
    // PreShadowTranslation moves the world origin to the shadow bounds center
    // This improves precision for shadow depth rendering
    PreShadowTranslation = -ShadowBounds.Center;
    
    // Compute depth range
    // For directional lights, we use the shadow bounds radius as the depth range
    float ShadowBoundsRadius = ShadowBounds.W;
    MinSubjectZ = -ShadowBoundsRadius;
    MaxSubjectZ = ShadowBoundsRadius;
    InvMaxSubjectDepth = 1.0f / (MaxSubjectZ - MinSubjectZ);
    
    // Store light direction for matrix computation
    // Note: We store the negated direction as PreShadowTranslation is used for direction reference
    FVector NormalizedLightDir = InLightDirection.GetSafeNormal();
    
    // Compute view matrix from light direction
    _computeDirectionalLightViewMatrix(NormalizedLightDir);
    
    // Compute orthographic projection matrix
    // Near plane is at -ShadowBoundsRadius, far plane is at +ShadowBoundsRadius
    float NearPlane = 0.0f;
    float FarPlane = ShadowBoundsRadius * 2.0f;
    _computeOrthographicProjection(ShadowBoundsRadius, NearPlane, FarPlane);
    
    // Compute combined world-to-clip matrices
    _computeWorldToClipMatrices();
    
    // Update shader depth bias
    updateShaderDepthBias();
    
    // Store cascade index for CSM (reserved for future use)
    ShadowId = InCascadeIndex;
    
    MR_LOG(LogShadowRendering, Log,
           "FProjectedShadowInfo::setupDirectionalLightShadow - Resolution: %ux%u, Bounds: (%.1f, %.1f, %.1f) R=%.1f, Cascade: %d",
           ResolutionX, ResolutionY,
           ShadowBounds.Center.X, ShadowBounds.Center.Y, ShadowBounds.Center.Z,
           ShadowBoundsRadius, InCascadeIndex);
    
    return true;
}

void FProjectedShadowInfo::computeDirectionalLightMatrices()
{
    std::lock_guard<std::mutex> Lock(m_mutex);
    
    if (!bDirectionalLight)
    {
        MR_LOG(LogShadowRendering, Warning,
               "FProjectedShadowInfo::computeDirectionalLightMatrices - Not a directional light shadow");
        return;
    }
    
    // Recompute matrices if needed
    // This is useful when shadow bounds change
    _computeWorldToClipMatrices();
    
    MR_LOG(LogShadowRendering, Verbose,
           "FProjectedShadowInfo::computeDirectionalLightMatrices - Matrices updated");
}

void FProjectedShadowInfo::_computeWorldToClipMatrices()
{
    // Compute the combined world-to-clip matrices
    // These are used for shadow depth rendering and shadow projection
    
    // Inner matrix (excluding border) - used for shadow depth rendering
    FMatrix WorldToClipInner = TranslatedWorldToView * ViewToClipInner;
    
    // Outer matrix (including border) - used for shadow projection
    FMatrix WorldToClipOuter = TranslatedWorldToView * ViewToClipOuter;
    
    // Convert to float32 matrices for shader use
    for (int32 Row = 0; Row < 4; ++Row)
    {
        for (int32 Col = 0; Col < 4; ++Col)
        {
            TranslatedWorldToClipInnerMatrix.M[Row][Col] = static_cast<float>(WorldToClipInner.M[Row][Col]);
            TranslatedWorldToClipOuterMatrix.M[Row][Col] = static_cast<float>(WorldToClipOuter.M[Row][Col]);
        }
    }
    
    // Compute inverse receiver matrix for shadow projection
    // This transforms from shadow clip space back to world space
    FMatrix InvWorldToClipInner = WorldToClipInner.Inverse();
    for (int32 Row = 0; Row < 4; ++Row)
    {
        for (int32 Col = 0; Col < 4; ++Col)
        {
            InvReceiverInnerMatrix.M[Row][Col] = static_cast<float>(InvWorldToClipInner.M[Row][Col]);
        }
    }
}

void FProjectedShadowInfo::setupPointLightShadow(
    FLightSceneInfo* InLightSceneInfo,
    uint32 InResolution,
    uint32 InBorderSize)
{
    std::lock_guard<std::mutex> Lock(m_mutex);
    
    m_lightSceneInfo = InLightSceneInfo;
    m_parentSceneInfo = nullptr;
    
    ResolutionX = InResolution;
    ResolutionY = InResolution;
    BorderSize = InBorderSize;
    
    bWholeSceneShadow = 1;
    bDirectionalLight = 0;
    bOnePassPointLightShadow = 1;
    bPerObjectOpaqueShadow = 0;
    
    // TODO: Get actual light position and radius from FLightSceneInfo
    FVector LightPosition = FVector::ZeroVector;
    float LightRadius = 1000.0f;
    
    // Setup cube face matrices
    _setupCubeFaceMatrices(LightPosition, LightRadius);
    
    MR_LOG(LogShadowRendering, Log,
           "FProjectedShadowInfo::setupPointLightShadow - Resolution: %u, Border: %u",
           InResolution, InBorderSize);
}

void FProjectedShadowInfo::setupSpotLightShadow(
    FLightSceneInfo* InLightSceneInfo,
    uint32 InResolutionX,
    uint32 InResolutionY,
    uint32 InBorderSize)
{
    std::lock_guard<std::mutex> Lock(m_mutex);
    
    m_lightSceneInfo = InLightSceneInfo;
    m_parentSceneInfo = nullptr;
    
    ResolutionX = InResolutionX;
    ResolutionY = InResolutionY;
    BorderSize = InBorderSize;
    
    bWholeSceneShadow = 1;
    bDirectionalLight = 0;
    bOnePassPointLightShadow = 0;
    bPerObjectOpaqueShadow = 0;
    
    // TODO: Compute perspective projection based on spot light cone angle
    // This requires access to light position, direction, and cone angles
    
    MR_LOG(LogShadowRendering, Log,
           "FProjectedShadowInfo::setupSpotLightShadow - Resolution: %ux%u, Border: %u",
           ResolutionX, ResolutionY, InBorderSize);
}

bool FProjectedShadowInfo::computeShadowDepthViewMatrices(FMatrix* OutViewMatrices, int32 CubeFaceIndex) const
{
    if (!OutViewMatrices)
    {
        return false;
    }
    
    if (bOnePassPointLightShadow && CubeFaceIndex >= 0 && CubeFaceIndex < 6)
    {
        // Return cube face view matrix
        if (CubeFaceIndex < OnePassShadowViewMatrices.Num())
        {
            *OutViewMatrices = OnePassShadowViewMatrices[CubeFaceIndex];
            return true;
        }
        return false;
    }
    else
    {
        // Return standard view matrix
        *OutViewMatrices = TranslatedWorldToView;
        return true;
    }
}

FMatrix FProjectedShadowInfo::getScreenToShadowMatrix(const FViewInfo& View) const
{
    // Compute screen to shadow matrix
    // ScreenPos -> ViewPos -> WorldPos -> ShadowPos
    
    // TODO: Implement full screen to shadow matrix computation
    // This requires access to View matrices
    
    FMatrix ScreenToShadow = FMatrix::Identity;
    
    // Apply shadow atlas offset and scale
    float InvResX = ResolutionX > 0 ? 1.0f / static_cast<float>(ResolutionX) : 0.0f;
    float InvResY = ResolutionY > 0 ? 1.0f / static_cast<float>(ResolutionY) : 0.0f;
    
    // Scale and offset for atlas position
    ScreenToShadow.M[0][0] = InvResX;
    ScreenToShadow.M[1][1] = InvResY;
    ScreenToShadow.M[3][0] = static_cast<float>(X) * InvResX;
    ScreenToShadow.M[3][1] = static_cast<float>(Y) * InvResY;
    
    return ScreenToShadow;
}

FMatrix FProjectedShadowInfo::getWorldToShadowMatrix(FVector4f& OutShadowmapMinMax) const
{
    // Compute world to shadow matrix with atlas offset
    FMatrix WorldToShadow = TranslatedWorldToView * ViewToClipInner;
    
    // Set shadowmap min/max (UV bounds in atlas)
    float InvAtlasWidth = 1.0f / 2048.0f;  // TODO: Get actual atlas size
    float InvAtlasHeight = 1.0f / 2048.0f;
    
    OutShadowmapMinMax.X = static_cast<float>(X + BorderSize) * InvAtlasWidth;
    OutShadowmapMinMax.Y = static_cast<float>(Y + BorderSize) * InvAtlasHeight;
    OutShadowmapMinMax.Z = static_cast<float>(X + BorderSize + ResolutionX) * InvAtlasWidth;
    OutShadowmapMinMax.W = static_cast<float>(Y + BorderSize + ResolutionY) * InvAtlasHeight;
    
    return WorldToShadow;
}

void FProjectedShadowInfo::updateShaderDepthBias()
{
    std::lock_guard<std::mutex> Lock(m_mutex);
    
    // Compute depth bias based on shadow resolution and type
    float DepthBiasScale = 1.0f;
    
    if (bDirectionalLight)
    {
        // Directional lights need more bias due to larger depth range
        DepthBiasScale = 2.0f;
    }
    else if (bOnePassPointLightShadow)
    {
        // Point lights need different bias handling
        DepthBiasScale = 1.5f;
    }
    
    // Compute final shader bias values
    m_shaderDepthBias = BiasParameters.ConstantDepthBias * DepthBiasScale;
    m_shaderSlopeDepthBias = BiasParameters.SlopeScaledDepthBias;
    m_shaderMaxSlopeDepthBias = BiasParameters.MaxSlopeDepthBias;
    
    MR_LOG(LogShadowRendering, Verbose,
           "FProjectedShadowInfo::updateShaderDepthBias - DepthBias: %f, SlopeBias: %f",
           m_shaderDepthBias, m_shaderSlopeDepthBias);
}

float FProjectedShadowInfo::computeTransitionSize() const
{
    // Compute transition size for soft PCF based on resolution
    if (ResolutionX == 0 || ResolutionY == 0)
    {
        return 1.0f;
    }
    
    // Transition size is inversely proportional to resolution
    float AvgResolution = static_cast<float>(ResolutionX + ResolutionY) * 0.5f;
    float TransitionSize = 1.0f / AvgResolution;
    
    // Apply light type specific scaling
    if (bDirectionalLight)
    {
        TransitionSize *= 2.0f;
    }
    
    return TransitionSize;
}

float FProjectedShadowInfo::getShaderReceiverDepthBias() const
{
    // Receiver depth bias for shadow projection
    return BiasParameters.ReceiverDepthBias + m_shaderDepthBias * 0.5f;
}

bool FProjectedShadowInfo::hasSubjectPrims() const
{
    return m_dynamicSubjectPrimitives.Num() > 0;
}

void FProjectedShadowInfo::addDynamicSubjectPrimitive(FPrimitiveSceneInfo* Primitive)
{
    if (Primitive)
    {
        std::lock_guard<std::mutex> Lock(m_mutex);
        m_dynamicSubjectPrimitives.Add(Primitive);
    }
}

bool FProjectedShadowInfo::allocateRenderTargets(IRHIDevice* InDevice)
{
    std::lock_guard<std::mutex> Lock(m_mutex);
    
    if (!InDevice)
    {
        MR_LOG(LogShadowRendering, Error, 
               "FProjectedShadowInfo::allocateRenderTargets - Invalid device");
        return false;
    }
    
    if (ResolutionX == 0 || ResolutionY == 0)
    {
        MR_LOG(LogShadowRendering, Error, 
               "FProjectedShadowInfo::allocateRenderTargets - Invalid resolution: %ux%u",
               ResolutionX, ResolutionY);
        return false;
    }
    
    // Release existing render targets
    releaseRenderTargets();
    
    // Calculate total size including border
    uint32 TotalWidth = ResolutionX + 2 * BorderSize;
    uint32 TotalHeight = ResolutionY + 2 * BorderSize;
    
    // Create depth texture descriptor
    using namespace MonsterRender::RHI;
    TextureDesc DepthDesc;
    DepthDesc.width = TotalWidth;
    DepthDesc.height = TotalHeight;
    DepthDesc.depth = 1;
    DepthDesc.mipLevels = 1;
    DepthDesc.arraySize = bOnePassPointLightShadow ? 6 : 1;  // 6 faces for cube map
    DepthDesc.format = EPixelFormat::D32_FLOAT;  // 32-bit depth for shadow maps
    DepthDesc.usage = static_cast<EResourceUsage>(
        static_cast<uint32>(EResourceUsage::DepthStencil) | 
        static_cast<uint32>(EResourceUsage::ShaderResource));
    
    // Create depth texture
    TSharedPtr<IRHITexture> DepthTexture = InDevice->createTexture(DepthDesc);
    if (!DepthTexture)
    {
        MR_LOG(LogShadowRendering, Error, 
               "FProjectedShadowInfo::allocateRenderTargets - Failed to create depth texture");
        return false;
    }
    
    // Store in render targets
    RenderTargets.DepthTarget = DepthTexture.Get();
    
    // Mark as allocated
    bAllocated = 1;
    
    MR_LOG(LogShadowRendering, Log, 
           "FProjectedShadowInfo::allocateRenderTargets - Allocated %ux%u depth target, CubeMap: %s",
           TotalWidth, TotalHeight, bOnePassPointLightShadow ? "true" : "false");
    
    return true;
}

void FProjectedShadowInfo::releaseRenderTargets()
{
    std::lock_guard<std::mutex> Lock(m_mutex);
    
    RenderTargets.release();
    bAllocated = 0;
    bRendered = 0;
    
    MR_LOG(LogShadowRendering, Verbose, 
           "FProjectedShadowInfo::releaseRenderTargets - Render targets released");
}

void FProjectedShadowInfo::renderDepth(IRHICommandList& RHICmdList, FSceneRenderer* SceneRenderer)
{
    if (!RenderTargets.isValid())
    {
        MR_LOG(LogShadowRendering, Warning, 
               "FProjectedShadowInfo::renderDepth - No render targets allocated");
        return;
    }
    
    MR_LOG(LogShadowRendering, Verbose, 
           "FProjectedShadowInfo::renderDepth - Begin rendering shadow %d, resolution: %ux%u",
           ShadowId, ResolutionX, ResolutionY);
    
    // Begin debug event
    RHICmdList.beginEvent("ShadowDepth");
    
    // Set render target (depth only, no color)
    TArray<TSharedPtr<IRHITexture>> EmptyColorTargets;
    TSharedPtr<IRHITexture> DepthTarget = TSharedPtr<IRHITexture>(RenderTargets.DepthTarget, [](IRHITexture*){});
    RHICmdList.setRenderTargets(TSpan<TSharedPtr<IRHITexture>>(EmptyColorTargets.GetData(), 0), DepthTarget);
    
    // Clear depth to 1.0 (far plane)
    RHICmdList.clearDepthStencil(DepthTarget, true, false, 1.0f, 0);
    
    // Set viewport and scissor
    setStateForView(RHICmdList);
    
    // Set depth-stencil state for shadow rendering
    // Depth test: Less, Depth write: Enabled
    RHICmdList.setDepthStencilState(true, true, 1);  // 1 = Less
    
    // Set rasterizer state with depth bias
    float DepthBias = getShaderDepthBias();
    float SlopeScaledBias = getShaderSlopeDepthBias();
    RHICmdList.setRasterizerState(
        0,  // Solid fill
        2,  // Back face culling
        false,  // Counter-clockwise front face
        DepthBias,
        SlopeScaledBias);
    
    // Set blend state (no blending, no color writes)
    RHICmdList.setBlendState(false, 0, 0, 0, 0, 0, 0, 0);
    
    // Render shadow-casting primitives
    int32 PrimitivesRendered = 0;
    int32 MeshBatchesRendered = 0;
    
    for (const FPrimitiveSceneInfo* PrimitiveInfo : m_dynamicSubjectPrimitives)
    {
        if (!PrimitiveInfo)
        {
            continue;
        }
        
        // Get mesh batches from primitive
        const TArray<FMeshBatch>& MeshBatches = PrimitiveInfo->GetMeshBatches();
        
        for (const FMeshBatch& MeshBatch : MeshBatches)
        {
            // Skip invalid batches or batches that don't cast shadows
            if (!MeshBatch.IsValid() || !MeshBatch.bCastShadow)
            {
                continue;
            }
            
            // Bind vertex buffer
            if (MeshBatch.VertexBuffer)
            {
                TSharedPtr<IRHIBuffer> VB(MeshBatch.VertexBuffer, [](IRHIBuffer*){});
                TArray<TSharedPtr<IRHIBuffer>> VBArray;
                VBArray.Add(VB);
                RHICmdList.setVertexBuffers(0, TSpan<TSharedPtr<IRHIBuffer>>(VBArray.GetData(), VBArray.Num()));
            }
            
            // Draw the mesh batch
            if (MeshBatch.IsIndexed())
            {
                // Bind index buffer
                if (MeshBatch.IndexBuffer)
                {
                    TSharedPtr<IRHIBuffer> IB(MeshBatch.IndexBuffer, [](IRHIBuffer*){});
                    RHICmdList.setIndexBuffer(IB, MeshBatch.bUse32BitIndices);
                }
                
                // Draw indexed
                if (MeshBatch.NumInstances > 1)
                {
                    RHICmdList.drawIndexedInstanced(
                        MeshBatch.NumIndices,
                        MeshBatch.NumInstances,
                        MeshBatch.FirstIndex,
                        MeshBatch.BaseVertexLocation,
                        0);
                }
                else
                {
                    RHICmdList.drawIndexed(
                        MeshBatch.NumIndices,
                        MeshBatch.FirstIndex,
                        MeshBatch.BaseVertexLocation);
                }
            }
            else
            {
                // Draw non-indexed
                if (MeshBatch.NumInstances > 1)
                {
                    RHICmdList.drawInstanced(
                        MeshBatch.NumVertices,
                        MeshBatch.NumInstances,
                        MeshBatch.FirstVertex,
                        0);
                }
                else
                {
                    RHICmdList.draw(MeshBatch.NumVertices, MeshBatch.FirstVertex);
                }
            }
            
            ++MeshBatchesRendered;
        }
        
        ++PrimitivesRendered;
    }
    
    MR_LOG(LogShadowRendering, Verbose, 
           "FProjectedShadowInfo::renderDepth - Rendered %d primitives, %d mesh batches",
           PrimitivesRendered, MeshBatchesRendered);
    
    // End render pass
    RHICmdList.endRenderPass();
    
    // End debug event
    RHICmdList.endEvent();
    
    // Mark as rendered
    bRendered = 1;
    
    MR_LOG(LogShadowRendering, Verbose, 
           "FProjectedShadowInfo::renderDepth - Shadow depth rendering complete");
}

void FProjectedShadowInfo::setStateForView(IRHICommandList& RHICmdList) const
{
    // Get the outer view rect (including border)
    FIntRect ViewRect = getOuterViewRect();
    
    // Set viewport using RHI types
    using namespace MonsterRender::RHI;
    Viewport VP;
    VP.x = static_cast<float>(ViewRect.Min.X);
    VP.y = static_cast<float>(ViewRect.Min.Y);
    VP.width = static_cast<float>(ViewRect.Max.X - ViewRect.Min.X);
    VP.height = static_cast<float>(ViewRect.Max.Y - ViewRect.Min.Y);
    VP.minDepth = 0.0f;
    VP.maxDepth = 1.0f;
    RHICmdList.setViewport(VP);
    
    // Set scissor rect to match viewport
    ScissorRect Scissor;
    Scissor.left = ViewRect.Min.X;
    Scissor.top = ViewRect.Min.Y;
    Scissor.right = ViewRect.Max.X;
    Scissor.bottom = ViewRect.Max.Y;
    RHICmdList.setScissorRect(Scissor);
    
    MR_LOG(LogShadowRendering, Verbose, 
           "FProjectedShadowInfo::setStateForView - Viewport: (%d, %d) - (%d, %d)",
           ViewRect.Min.X, ViewRect.Min.Y, ViewRect.Max.X, ViewRect.Max.Y);
}

void FProjectedShadowInfo::_computeDirectionalLightViewMatrix(const FVector& LightDirection)
{
    // Compute view matrix for directional light
    // Light looks along -LightDirection
    
    FVector Forward = -LightDirection.GetSafeNormal();
    FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
    
    if (Right.SizeSquared() < MR_SMALL_NUMBER)
    {
        // Light is pointing straight up or down
        Right = FVector::CrossProduct(FVector::ForwardVector, Forward);
    }
    Right.Normalize();
    
    FVector Up = FVector::CrossProduct(Forward, Right);
    Up.Normalize();
    
    // Build view matrix (column vectors are Right, Up, Forward)
    TranslatedWorldToView.M[0][0] = Right.X;
    TranslatedWorldToView.M[0][1] = Up.X;
    TranslatedWorldToView.M[0][2] = Forward.X;
    TranslatedWorldToView.M[0][3] = 0.0f;
    
    TranslatedWorldToView.M[1][0] = Right.Y;
    TranslatedWorldToView.M[1][1] = Up.Y;
    TranslatedWorldToView.M[1][2] = Forward.Y;
    TranslatedWorldToView.M[1][3] = 0.0f;
    
    TranslatedWorldToView.M[2][0] = Right.Z;
    TranslatedWorldToView.M[2][1] = Up.Z;
    TranslatedWorldToView.M[2][2] = Forward.Z;
    TranslatedWorldToView.M[2][3] = 0.0f;
    
    TranslatedWorldToView.M[3][0] = 0.0f;
    TranslatedWorldToView.M[3][1] = 0.0f;
    TranslatedWorldToView.M[3][2] = 0.0f;
    TranslatedWorldToView.M[3][3] = 1.0f;
}

void FProjectedShadowInfo::_computeOrthographicProjection(
    float ShadowBoundsRadius,
    float NearPlane,
    float FarPlane)
{
    // Build orthographic projection matrix
    float InvWidth = 1.0f / ShadowBoundsRadius;
    float InvHeight = 1.0f / ShadowBoundsRadius;
    float InvDepth = 1.0f / (FarPlane - NearPlane);
    
    ViewToClipInner.M[0][0] = InvWidth;
    ViewToClipInner.M[0][1] = 0.0f;
    ViewToClipInner.M[0][2] = 0.0f;
    ViewToClipInner.M[0][3] = 0.0f;
    
    ViewToClipInner.M[1][0] = 0.0f;
    ViewToClipInner.M[1][1] = InvHeight;
    ViewToClipInner.M[1][2] = 0.0f;
    ViewToClipInner.M[1][3] = 0.0f;
    
    ViewToClipInner.M[2][0] = 0.0f;
    ViewToClipInner.M[2][1] = 0.0f;
    ViewToClipInner.M[2][2] = InvDepth;
    ViewToClipInner.M[2][3] = 0.0f;
    
    ViewToClipInner.M[3][0] = 0.0f;
    ViewToClipInner.M[3][1] = 0.0f;
    ViewToClipInner.M[3][2] = -NearPlane * InvDepth;
    ViewToClipInner.M[3][3] = 1.0f;
    
    // Outer projection includes border
    float BorderScale = static_cast<float>(ResolutionX) / 
                       static_cast<float>(ResolutionX + 2 * BorderSize);
    ViewToClipOuter = ViewToClipInner;
    ViewToClipOuter.M[0][0] *= BorderScale;
    ViewToClipOuter.M[1][1] *= BorderScale;
}

void FProjectedShadowInfo::_computePerspectiveProjection(
    float FOVAngle,
    float AspectRatio,
    float NearPlane,
    float FarPlane)
{
    // Build perspective projection matrix for spot lights
    float TanHalfFOV = std::tan(FOVAngle * 0.5f);
    float InvTanHalfFOV = 1.0f / TanHalfFOV;
    float InvDepthRange = 1.0f / (FarPlane - NearPlane);
    
    ViewToClipInner.M[0][0] = InvTanHalfFOV / AspectRatio;
    ViewToClipInner.M[0][1] = 0.0f;
    ViewToClipInner.M[0][2] = 0.0f;
    ViewToClipInner.M[0][3] = 0.0f;
    
    ViewToClipInner.M[1][0] = 0.0f;
    ViewToClipInner.M[1][1] = InvTanHalfFOV;
    ViewToClipInner.M[1][2] = 0.0f;
    ViewToClipInner.M[1][3] = 0.0f;
    
    ViewToClipInner.M[2][0] = 0.0f;
    ViewToClipInner.M[2][1] = 0.0f;
    ViewToClipInner.M[2][2] = FarPlane * InvDepthRange;
    ViewToClipInner.M[2][3] = 1.0f;
    
    ViewToClipInner.M[3][0] = 0.0f;
    ViewToClipInner.M[3][1] = 0.0f;
    ViewToClipInner.M[3][2] = -NearPlane * FarPlane * InvDepthRange;
    ViewToClipInner.M[3][3] = 0.0f;
    
    // Outer projection includes border
    ViewToClipOuter = ViewToClipInner;
}

void FProjectedShadowInfo::_setupCubeFaceMatrices(const FVector& LightPosition, float LightRadius)
{
    // Clear existing matrices
    OnePassShadowViewMatrices.Empty();
    OnePassShadowViewProjectionMatrices.Empty();
    
    // Cube face directions (+X, -X, +Y, -Y, +Z, -Z)
    static const FVector CubeFaceDirections[6] = {
        FVector(+1.0f, 0.0f, 0.0f),  // +X
        FVector(-1.0f, 0.0f, 0.0f),  // -X
        FVector(0.0f, +1.0f, 0.0f),  // +Y
        FVector(0.0f, -1.0f, 0.0f),  // -Y
        FVector(0.0f, 0.0f, +1.0f),  // +Z
        FVector(0.0f, 0.0f, -1.0f),  // -Z
    };
    
    // Cube face up vectors
    static const FVector CubeFaceUpVectors[6] = {
        FVector(0.0f, -1.0f, 0.0f),  // +X
        FVector(0.0f, -1.0f, 0.0f),  // -X
        FVector(0.0f, 0.0f, +1.0f),  // +Y
        FVector(0.0f, 0.0f, -1.0f),  // -Y
        FVector(0.0f, -1.0f, 0.0f),  // +Z
        FVector(0.0f, -1.0f, 0.0f),  // -Z
    };
    
    // Create 90 degree FOV perspective projection
    float NearPlane = 1.0f;
    float FarPlane = LightRadius;
    float FOV = MR_PI * 0.5f;  // 90 degrees
    
    float TanHalfFOV = 1.0f;  // tan(45 degrees) = 1
    float InvDepthRange = 1.0f / (FarPlane - NearPlane);
    
    OnePassShadowFaceProjectionMatrix.M[0][0] = 1.0f;
    OnePassShadowFaceProjectionMatrix.M[0][1] = 0.0f;
    OnePassShadowFaceProjectionMatrix.M[0][2] = 0.0f;
    OnePassShadowFaceProjectionMatrix.M[0][3] = 0.0f;
    
    OnePassShadowFaceProjectionMatrix.M[1][0] = 0.0f;
    OnePassShadowFaceProjectionMatrix.M[1][1] = 1.0f;
    OnePassShadowFaceProjectionMatrix.M[1][2] = 0.0f;
    OnePassShadowFaceProjectionMatrix.M[1][3] = 0.0f;
    
    OnePassShadowFaceProjectionMatrix.M[2][0] = 0.0f;
    OnePassShadowFaceProjectionMatrix.M[2][1] = 0.0f;
    OnePassShadowFaceProjectionMatrix.M[2][2] = FarPlane * InvDepthRange;
    OnePassShadowFaceProjectionMatrix.M[2][3] = 1.0f;
    
    OnePassShadowFaceProjectionMatrix.M[3][0] = 0.0f;
    OnePassShadowFaceProjectionMatrix.M[3][1] = 0.0f;
    OnePassShadowFaceProjectionMatrix.M[3][2] = -NearPlane * FarPlane * InvDepthRange;
    OnePassShadowFaceProjectionMatrix.M[3][3] = 0.0f;
    
    // Build view matrix for each cube face
    for (int32 FaceIndex = 0; FaceIndex < 6; ++FaceIndex)
    {
        FVector Forward = CubeFaceDirections[FaceIndex];
        FVector Up = CubeFaceUpVectors[FaceIndex];
        FVector Right = FVector::CrossProduct(Up, Forward);
        
        FMatrix ViewMatrix;
        
        // Build look-at matrix
        ViewMatrix.M[0][0] = Right.X;
        ViewMatrix.M[0][1] = Up.X;
        ViewMatrix.M[0][2] = Forward.X;
        ViewMatrix.M[0][3] = 0.0f;
        
        ViewMatrix.M[1][0] = Right.Y;
        ViewMatrix.M[1][1] = Up.Y;
        ViewMatrix.M[1][2] = Forward.Y;
        ViewMatrix.M[1][3] = 0.0f;
        
        ViewMatrix.M[2][0] = Right.Z;
        ViewMatrix.M[2][1] = Up.Z;
        ViewMatrix.M[2][2] = Forward.Z;
        ViewMatrix.M[2][3] = 0.0f;
        
        // Translate by light position
        ViewMatrix.M[3][0] = -FVector::DotProduct(Right, LightPosition);
        ViewMatrix.M[3][1] = -FVector::DotProduct(Up, LightPosition);
        ViewMatrix.M[3][2] = -FVector::DotProduct(Forward, LightPosition);
        ViewMatrix.M[3][3] = 1.0f;
        
        OnePassShadowViewMatrices.Add(ViewMatrix);
        OnePassShadowViewProjectionMatrices.Add(ViewMatrix * OnePassShadowFaceProjectionMatrix);
    }
    
    MR_LOG(LogShadowRendering, Verbose,
           "FProjectedShadowInfo::_setupCubeFaceMatrices - Setup 6 cube faces for point light");
}

// ============================================================================
// FShadowSceneRenderer Implementation
// ============================================================================

FShadowSceneRenderer::FShadowSceneRenderer()
    : m_device(nullptr)
    , m_maxAtlasResolution(4096)
    , m_maxPointLightResolution(1024)
{
}

FShadowSceneRenderer::~FShadowSceneRenderer()
{
    shutdown();
}

bool FShadowSceneRenderer::initialize(IRHIDevice* InDevice)
{
    std::lock_guard<std::mutex> Lock(m_mutex);
    
    if (!InDevice)
    {
        MR_LOG(LogShadowRendering, Error, "FShadowSceneRenderer::initialize - Invalid device");
        return false;
    }
    
    m_device = InDevice;
    
    // Create shadow atlas
    m_shadowAtlas = MakeShared<FShadowMap>();
    if (!m_shadowAtlas->initialize(m_device, m_maxAtlasResolution, false))
    {
        MR_LOG(LogShadowRendering, Error, "FShadowSceneRenderer::initialize - Failed to create shadow atlas");
        return false;
    }
    
    MR_LOG(LogShadowRendering, Log, 
           "FShadowSceneRenderer::initialize - Atlas: %ux%u, MaxPointLight: %u",
           m_maxAtlasResolution, m_maxAtlasResolution, m_maxPointLightResolution);
    
    return true;
}

void FShadowSceneRenderer::shutdown()
{
    std::lock_guard<std::mutex> Lock(m_mutex);
    
    clearShadows();
    
    m_shadowAtlas.Reset();
    m_pointLightShadowMaps.Empty();
    m_device = nullptr;
    
    MR_LOG(LogShadowRendering, Log, "FShadowSceneRenderer::shutdown - Resources released");
}

int32 FShadowSceneRenderer::allocateShadowMaps(FScene* Scene, const TArray<FViewInfo*>& Views)
{
    std::lock_guard<std::mutex> Lock(m_mutex);
    
    if (!Scene || Views.Num() == 0)
    {
        return 0;
    }
    
    // TODO: Implement actual shadow allocation based on visible lights
    // This will:
    // 1. Iterate through visible lights
    // 2. Determine which lights need shadows
    // 3. Allocate space in shadow atlas
    // 4. Create FProjectedShadowInfo for each shadow
    
    int32 NumShadowsAllocated = 0;
    
    MR_LOG(LogShadowRendering, Verbose,
           "FShadowSceneRenderer::allocateShadowMaps - Allocated %d shadows",
           NumShadowsAllocated);
    
    return NumShadowsAllocated;
}

void FShadowSceneRenderer::clearShadows()
{
    m_projectedShadows.Empty();
    
    MR_LOG(LogShadowRendering, Verbose, "FShadowSceneRenderer::clearShadows - Shadows cleared");
}

} // namespace Renderer
} // namespace MonsterEngine
