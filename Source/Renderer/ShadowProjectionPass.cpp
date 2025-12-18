// Copyright Monster Engine. All Rights Reserved.

/**
 * @file ShadowProjectionPass.cpp
 * @brief Shadow projection pass implementation
 * 
 * Implements shadow map projection onto lit surfaces during lighting pass.
 * Reference: UE5 Engine/Source/Runtime/Renderer/Private/ShadowRendering.cpp
 */

#include "Renderer/ShadowProjectionPass.h"
#include "Renderer/ShadowRendering.h"
#include "Renderer/Scene.h"
#include "Renderer/SceneView.h"
#include "RHI/IRHIDevice.h"
#include "RHI/IRHICommandList.h"
#include "RHI/IRHIResource.h"
#include "RHI/RHIResources.h"
#include "RHI/RHIDefinitions.h"
#include "Core/Logging/LogMacros.h"
#include "Math/MathUtility.h"

// Define log category for shadow projection pass
DEFINE_LOG_CATEGORY_STATIC(LogShadowProjection, Log, All);

namespace MonsterEngine
{
namespace Renderer
{

// ============================================================================
// FShadowProjectionPass Implementation
// ============================================================================

FShadowProjectionPass::FShadowProjectionPass(MonsterRender::RHI::IRHIDevice* InDevice)
    : FRenderPassBase(FRenderPassConfig())
    , m_device(InDevice)
    , m_bInitialized(false)
{
    // Configure render pass for shadow projection
    Config.PassType = ERenderPassType::ShadowProjection;
    Config.PassName = TEXT("ShadowProjection");
    Config.bClearDepth = false;
    Config.bClearColor = false;
    Config.bEnabled = true;
    Config.Priority = 100;  // After shadow depth, before main lighting
    
    // Configure depth-stencil state (read depth, no write)
    Config.DepthStencilState.bDepthTestEnable = true;
    Config.DepthStencilState.bDepthWriteEnable = false;
    Config.DepthStencilState.DepthCompareFunc = ECompareFunc::Always;
    Config.DepthStencilState.bStencilTestEnable = false;
    
    // Configure rasterizer state
    Config.RasterizerState.FillMode = EFillMode::Solid;
    Config.RasterizerState.CullMode = ECullMode::None;
    
    // Configure blend state (multiply shadow factor)
    Config.BlendState.bBlendEnable = true;
    Config.BlendState.SrcColorBlend = EBlendFactor::Zero;
    Config.BlendState.DstColorBlend = EBlendFactor::SrcColor;
    Config.BlendState.ColorBlendOp = EBlendOp::Add;
    Config.BlendState.ColorWriteMask = 0x0F;  // RGBA
    
    MR_LOG(LogShadowProjection, Verbose, "FShadowProjectionPass created");
}

FShadowProjectionPass::~FShadowProjectionPass()
{
    release();
    MR_LOG(LogShadowProjection, Verbose, "FShadowProjectionPass destroyed");
}

bool FShadowProjectionPass::initialize()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_bInitialized)
    {
        MR_LOG(LogShadowProjection, Warning, "FShadowProjectionPass already initialized");
        return true;
    }
    
    if (!m_device)
    {
        MR_LOG(LogShadowProjection, Error, "FShadowProjectionPass::initialize - No RHI device");
        return false;
    }
    
    MR_LOG(LogShadowProjection, Log, "Initializing FShadowProjectionPass");
    
    // Load shaders
    if (!_loadShaders())
    {
        MR_LOG(LogShadowProjection, Error, "FShadowProjectionPass::initialize - Failed to load shaders");
        return false;
    }
    
    // Create pipeline state
    if (!_createPipelineState())
    {
        MR_LOG(LogShadowProjection, Error, "FShadowProjectionPass::initialize - Failed to create pipeline state");
        return false;
    }
    
    // Create uniform buffer
    if (!_createUniformBuffer())
    {
        MR_LOG(LogShadowProjection, Error, "FShadowProjectionPass::initialize - Failed to create uniform buffer");
        return false;
    }
    
    // Create shadow sampler
    if (!_createShadowSampler())
    {
        MR_LOG(LogShadowProjection, Error, "FShadowProjectionPass::initialize - Failed to create shadow sampler");
        return false;
    }
    
    m_bInitialized = true;
    MR_LOG(LogShadowProjection, Log, "FShadowProjectionPass initialized successfully");
    
    return true;
}

void FShadowProjectionPass::release()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_bInitialized)
    {
        return;
    }
    
    MR_LOG(LogShadowProjection, Log, "Releasing FShadowProjectionPass resources");
    
    // Release resources
    m_uniformBuffer.Reset();
    m_pipelineState.Reset();
    m_pipelineStatePCF.Reset();
    m_vertexShader.Reset();
    m_pixelShader.Reset();
    m_pixelShaderPCF.Reset();
    m_shadowSampler.Reset();
    m_fullScreenQuadVB.Reset();
    
    m_bInitialized = false;
    
    MR_LOG(LogShadowProjection, Log, "FShadowProjectionPass resources released");
}

bool FShadowProjectionPass::ShouldExecute(const FRenderPassContext& Context) const
{
    if (!FRenderPassBase::ShouldExecute(Context))
    {
        return false;
    }
    
    // Execute if we have shadows to project
    return m_bInitialized;
}

void FShadowProjectionPass::Setup(FRenderPassContext& Context)
{
    FRenderPassBase::Setup(Context);
    MR_LOG(LogShadowProjection, Verbose, "FShadowProjectionPass::Setup");
}

void FShadowProjectionPass::Execute(FRenderPassContext& Context)
{
    if (!m_bInitialized || !Context.RHICmdList)
    {
        MR_LOG(LogShadowProjection, Warning, 
               "FShadowProjectionPass::Execute - Not initialized or no command list");
        return;
    }
    
    MR_LOG(LogShadowProjection, Verbose, "FShadowProjectionPass::Execute begin");
    
    // Shadow projection is typically done per-light in RenderShadowProjections
    // This Execute is called as part of the render pass system
    
    MR_LOG(LogShadowProjection, Verbose, "FShadowProjectionPass::Execute end");
}

void FShadowProjectionPass::Cleanup(FRenderPassContext& Context)
{
    FRenderPassBase::Cleanup(Context);
    MR_LOG(LogShadowProjection, Verbose, "FShadowProjectionPass::Cleanup");
}

void FShadowProjectionPass::projectShadow(
    MonsterRender::RHI::IRHICommandList& RHICmdList,
    const FProjectedShadowInfo* ShadowInfo,
    const FViewInfo* View,
    const FLightSceneInfo* LightInfo,
    MonsterRender::RHI::IRHITexture* ShadowMaskTexture)
{
    if (!m_bInitialized)
    {
        MR_LOG(LogShadowProjection, Warning, 
               "FShadowProjectionPass::projectShadow - Not initialized");
        return;
    }
    
    if (!ShadowInfo || !View || !ShadowMaskTexture)
    {
        MR_LOG(LogShadowProjection, Warning, 
               "FShadowProjectionPass::projectShadow - Invalid parameters");
        return;
    }
    
    // Check if shadow has valid render targets
    if (!ShadowInfo->RenderTargets.isValid())
    {
        MR_LOG(LogShadowProjection, Warning, 
               "FShadowProjectionPass::projectShadow - Shadow has no render targets");
        return;
    }
    
    MR_LOG(LogShadowProjection, Verbose, 
           "Projecting shadow %d onto view, resolution: %ux%u",
           ShadowInfo->ShadowId, ShadowInfo->ResolutionX, ShadowInfo->ResolutionY);
    
    // Begin debug event
    RHICmdList.beginEvent("ShadowProjection");
    
    // Update uniform buffer with shadow parameters
    updateUniformBuffer(ShadowInfo, View);
    
    // Bind pipeline state
    _bindPipelineState(RHICmdList, ShadowInfo);
    
    // Bind shadow depth texture
    if (ShadowInfo->RenderTargets.DepthTarget)
    {
        TSharedPtr<MonsterRender::RHI::IRHITexture> DepthTexture(
            ShadowInfo->RenderTargets.DepthTarget, 
            [](MonsterRender::RHI::IRHITexture*){});
        RHICmdList.setShaderResource(0, DepthTexture);
    }
    
    // Bind shadow sampler
    if (m_shadowSampler)
    {
        RHICmdList.setSampler(0, m_shadowSampler);
    }
    
    // Draw full screen quad
    _drawFullScreenQuad(RHICmdList, View);
    
    // End debug event
    RHICmdList.endEvent();
    
    MR_LOG(LogShadowProjection, Verbose, "Shadow projection complete");
}

void FShadowProjectionPass::projectShadowsForLight(
    MonsterRender::RHI::IRHICommandList& RHICmdList,
    const TArray<FProjectedShadowInfo*>& Shadows,
    const FViewInfo* View,
    const FLightSceneInfo* LightInfo,
    MonsterRender::RHI::IRHITexture* ShadowMaskTexture)
{
    if (!m_bInitialized || !View || !ShadowMaskTexture)
    {
        return;
    }
    
    MR_LOG(LogShadowProjection, Verbose, 
           "Projecting %d shadows for light", Shadows.Num());
    
    // Project each shadow
    for (FProjectedShadowInfo* ShadowInfo : Shadows)
    {
        if (ShadowInfo && ShadowInfo->bAllocated && ShadowInfo->bRendered)
        {
            projectShadow(RHICmdList, ShadowInfo, View, LightInfo, ShadowMaskTexture);
        }
    }
}

TSharedPtr<MonsterRender::RHI::IRHITexture> FShadowProjectionPass::createShadowMaskTexture(
    uint32 Width, uint32 Height)
{
    if (!m_device)
    {
        MR_LOG(LogShadowProjection, Error, 
               "FShadowProjectionPass::createShadowMaskTexture - No device");
        return nullptr;
    }
    
    using namespace MonsterRender::RHI;
    
    TextureDesc Desc;
    Desc.width = Width;
    Desc.height = Height;
    Desc.depth = 1;
    Desc.mipLevels = 1;
    Desc.arraySize = 1;
    Desc.format = EPixelFormat::R8_UNORM;  // Single channel shadow mask
    Desc.usage = static_cast<EResourceUsage>(
        static_cast<uint32>(EResourceUsage::RenderTarget) |
        static_cast<uint32>(EResourceUsage::ShaderResource));
    Desc.debugName = "ShadowMaskTexture";
    
    TSharedPtr<IRHITexture> Texture = m_device->createTexture(Desc);
    if (!Texture)
    {
        MR_LOG(LogShadowProjection, Error, 
               "FShadowProjectionPass::createShadowMaskTexture - Failed to create texture");
        return nullptr;
    }
    
    MR_LOG(LogShadowProjection, Log, 
           "Created shadow mask texture: %ux%u", Width, Height);
    
    return Texture;
}

void FShadowProjectionPass::setConfig(const FShadowProjectionPassConfig& InConfig)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config = InConfig;
    
    MR_LOG(LogShadowProjection, Verbose, 
           "Shadow projection config updated: quality=%u, PCF=%s, kernelSize=%u",
           InConfig.ShadowQuality, 
           InConfig.bUsePCF ? "true" : "false",
           InConfig.PCFKernelSize);
}

void FShadowProjectionPass::updateUniformBuffer(
    const FProjectedShadowInfo* ShadowInfo,
    const FViewInfo* View)
{
    if (!ShadowInfo || !View)
    {
        return;
    }
    
    // Compute screen to shadow matrix
    m_uniformParams.ScreenToShadowMatrix = _computeScreenToShadowMatrix(ShadowInfo, View);
    
    // Set shadow UV bounds
    float InvResX = 1.0f / static_cast<float>(ShadowInfo->ResolutionX + 2 * ShadowInfo->BorderSize);
    float InvResY = 1.0f / static_cast<float>(ShadowInfo->ResolutionY + 2 * ShadowInfo->BorderSize);
    float BorderU = static_cast<float>(ShadowInfo->BorderSize) * InvResX;
    float BorderV = static_cast<float>(ShadowInfo->BorderSize) * InvResY;
    
    m_uniformParams.ShadowUVMinMax = FVector4f(
        BorderU, BorderV,
        1.0f - BorderU, 1.0f - BorderV);
    
    // Set shadow buffer size
    float ResX = static_cast<float>(ShadowInfo->ResolutionX + 2 * ShadowInfo->BorderSize);
    float ResY = static_cast<float>(ShadowInfo->ResolutionY + 2 * ShadowInfo->BorderSize);
    m_uniformParams.ShadowBufferSize = FVector4f(ResX, ResY, 1.0f / ResX, 1.0f / ResY);
    
    // Set shadow parameters
    m_uniformParams.ShadowParams = FVector4f(
        ShadowInfo->getShaderDepthBias(),
        ShadowInfo->getShaderSlopeDepthBias(),
        ShadowInfo->InvMaxSubjectDepth,
        ShadowInfo->bDirectionalLight ? 1.0f : 0.0f);
    
    // Set light position and type
    FLightSceneInfo* LightInfo = ShadowInfo->getLightSceneInfo();
    if (ShadowInfo->bDirectionalLight)
    {
        FVector LightDir = ShadowInfo->PreShadowTranslation.GetSafeNormal();
        m_uniformParams.LightPositionAndType = FVector4f(
            static_cast<float>(LightDir.X),
            static_cast<float>(LightDir.Y),
            static_cast<float>(LightDir.Z),
            0.0f);  // Directional light
    }
    else
    {
        FVector LightPos = ShadowInfo->ShadowBounds.Center;
        m_uniformParams.LightPositionAndType = FVector4f(
            static_cast<float>(LightPos.X),
            static_cast<float>(LightPos.Y),
            static_cast<float>(LightPos.Z),
            ShadowInfo->bOnePassPointLightShadow ? 1.0f : 2.0f);  // Point or spot
    }
    
    // Set light direction and radius
    FVector LightDirection = ShadowInfo->PreShadowTranslation.GetSafeNormal();
    m_uniformParams.LightDirectionAndRadius = FVector4f(
        static_cast<float>(LightDirection.X),
        static_cast<float>(LightDirection.Y),
        static_cast<float>(LightDirection.Z),
        static_cast<float>(ShadowInfo->ShadowBounds.W));
    
    // Set transition scale
    m_uniformParams.TransitionScale = ShadowInfo->computeTransitionSize();
    m_uniformParams.SoftTransitionScale = 1.0f;
    m_uniformParams.ProjectionDepthBias = ShadowInfo->getShaderReceiverDepthBias();
    
    // TODO: Upload uniform buffer to GPU
    // if (m_uniformBuffer)
    // {
    //     void* mappedData = m_uniformBuffer->map();
    //     if (mappedData)
    //     {
    //         memcpy(mappedData, &m_uniformParams, sizeof(m_uniformParams));
    //         m_uniformBuffer->unmap();
    //     }
    // }
    
    MR_LOG(LogShadowProjection, Verbose, 
           "Uniform buffer updated: depthBias=%.4f, transitionScale=%.2f",
           m_uniformParams.ShadowParams.X, m_uniformParams.TransitionScale);
}

// ============================================================================
// Internal Methods
// ============================================================================

bool FShadowProjectionPass::_loadShaders()
{
    MR_LOG(LogShadowProjection, Log, "Loading shadow projection shaders");
    
    // TODO: Load shaders through shader manager
    MR_LOG(LogShadowProjection, Log, "Shader paths: %s, %s", 
           SHADOW_PROJECTION_VERT_PATH, SHADOW_PROJECTION_FRAG_PATH);
    
    // TODO: Implement actual shader loading
    // m_vertexShader = m_device->createVertexShader(SHADOW_PROJECTION_VERT_PATH);
    // m_pixelShader = m_device->createPixelShader(SHADOW_PROJECTION_FRAG_PATH);
    // m_pixelShaderPCF = m_device->createPixelShader(SHADOW_PROJECTION_PCF_FRAG_PATH);
    
    return true;  // Return true for now to allow initialization to continue
}

bool FShadowProjectionPass::_createPipelineState()
{
    MR_LOG(LogShadowProjection, Log, "Creating shadow projection pipeline state");
    
    // TODO: Create pipeline state through RHI device
    // PipelineStateDesc desc;
    // desc.vertexShader = m_vertexShader;
    // desc.pixelShader = m_pixelShader;
    // desc.depthStencilState = Config.DepthStencilState;
    // desc.rasterizerState = Config.RasterizerState;
    // desc.blendState = Config.BlendState;
    // m_pipelineState = m_device->createPipelineState(desc);
    
    // Create PCF variant
    // desc.pixelShader = m_pixelShaderPCF;
    // m_pipelineStatePCF = m_device->createPipelineState(desc);
    
    return true;  // Return true for now
}

bool FShadowProjectionPass::_createUniformBuffer()
{
    MR_LOG(LogShadowProjection, Log, "Creating shadow projection uniform buffer");
    
    // TODO: Create uniform buffer through RHI device
    // BufferDesc desc;
    // desc.size = sizeof(FShadowProjectionUniformParameters);
    // desc.usage = EResourceUsage::UniformBuffer;
    // desc.debugName = "ShadowProjectionUniformBuffer";
    // m_uniformBuffer = m_device->createBuffer(desc);
    
    return true;  // Return true for now
}

bool FShadowProjectionPass::_createShadowSampler()
{
    MR_LOG(LogShadowProjection, Log, "Creating shadow comparison sampler");
    
    // TODO: Create shadow comparison sampler
    // SamplerDesc desc;
    // desc.filter = ESamplerFilter::Comparison;
    // desc.addressU = ESamplerAddressMode::Clamp;
    // desc.addressV = ESamplerAddressMode::Clamp;
    // desc.comparisonFunc = ECompareFunc::LessEqual;
    // m_shadowSampler = m_device->createSampler(desc);
    
    return true;  // Return true for now
}

void FShadowProjectionPass::_bindPipelineState(
    MonsterRender::RHI::IRHICommandList& RHICmdList,
    const FProjectedShadowInfo* ShadowInfo)
{
    // Select pipeline based on configuration
    TSharedPtr<MonsterRender::RHI::IRHIPipelineState> Pipeline = 
        m_config.bUsePCF ? m_pipelineStatePCF : m_pipelineState;
    
    if (Pipeline)
    {
        RHICmdList.setPipelineState(Pipeline);
    }
    
    // Bind uniform buffer
    if (m_uniformBuffer)
    {
        RHICmdList.setConstantBuffer(0, m_uniformBuffer);
    }
    
    // Set depth-stencil state (read depth, no write)
    RHICmdList.setDepthStencilState(true, false, 7);  // 7 = Always
    
    // Set blend state for shadow mask multiplication
    // Dst = Dst * Src (multiply shadow factor)
    RHICmdList.setBlendState(
        true,   // Enable blend
        0,      // Zero (src color)
        3,      // SrcColor (dst color) 
        0,      // Add
        0,      // Zero (src alpha)
        1,      // One (dst alpha)
        0,      // Add
        0x0F);  // RGBA write mask
    
    // Set rasterizer state (no culling for full screen quad)
    RHICmdList.setRasterizerState(0, 0, false, 0.0f, 0.0f);
    
    MR_LOG(LogShadowProjection, Verbose, "Pipeline state bound");
}

void FShadowProjectionPass::_drawFullScreenQuad(
    MonsterRender::RHI::IRHICommandList& RHICmdList,
    const FViewInfo* View)
{
    if (!View)
    {
        return;
    }
    
    // Set viewport to view rect
    using namespace MonsterRender::RHI;
    Viewport VP;
    VP.x = static_cast<float>(View->ViewRect.Min.X);
    VP.y = static_cast<float>(View->ViewRect.Min.Y);
    VP.width = static_cast<float>(View->ViewRect.Max.X - View->ViewRect.Min.X);
    VP.height = static_cast<float>(View->ViewRect.Max.Y - View->ViewRect.Min.Y);
    VP.minDepth = 0.0f;
    VP.maxDepth = 1.0f;
    RHICmdList.setViewport(VP);
    
    // Set scissor rect
    ScissorRect Scissor;
    Scissor.left = View->ViewRect.Min.X;
    Scissor.top = View->ViewRect.Min.Y;
    Scissor.right = View->ViewRect.Max.X;
    Scissor.bottom = View->ViewRect.Max.Y;
    RHICmdList.setScissorRect(Scissor);
    
    // Draw full screen triangle (3 vertices, no vertex buffer needed)
    // The vertex shader generates positions from vertex ID
    RHICmdList.draw(3, 0);
    
    MR_LOG(LogShadowProjection, Verbose, "Full screen quad drawn");
}

FMatrix44f FShadowProjectionPass::_computeScreenToShadowMatrix(
    const FProjectedShadowInfo* ShadowInfo,
    const FViewInfo* View) const
{
    if (!ShadowInfo || !View)
    {
        return FMatrix44f::Identity;
    }
    
    // Compute the transformation from screen space to shadow UV space
    // ScreenPos -> WorldPos -> ShadowViewPos -> ShadowClipPos -> ShadowUV
    
    // Get inverse view-projection matrix from view
    FMatrix InvViewProj = View->ViewMatrices.GetInvViewProjectionMatrix();
    
    // Get shadow world-to-clip matrix
    FMatrix ShadowWorldToClip = FMatrix(ShadowInfo->TranslatedWorldToClipOuterMatrix);
    
    // Combine: ScreenToWorld * WorldToShadowClip
    FMatrix ScreenToShadow = InvViewProj * ShadowWorldToClip;
    
    // Apply scale and bias to convert from [-1,1] to [0,1] UV space
    FMatrix ClipToUV(
        FPlane(0.5f,  0.0f, 0.0f, 0.0f),
        FPlane(0.0f, -0.5f, 0.0f, 0.0f),
        FPlane(0.0f,  0.0f, 1.0f, 0.0f),
        FPlane(0.5f,  0.5f, 0.0f, 1.0f)
    );
    
    FMatrix FinalMatrix = ScreenToShadow * ClipToUV;
    
    // Convert to float matrix
    return FMatrix44f(
        static_cast<float>(FinalMatrix.M[0][0]), static_cast<float>(FinalMatrix.M[0][1]),
        static_cast<float>(FinalMatrix.M[0][2]), static_cast<float>(FinalMatrix.M[0][3]),
        static_cast<float>(FinalMatrix.M[1][0]), static_cast<float>(FinalMatrix.M[1][1]),
        static_cast<float>(FinalMatrix.M[1][2]), static_cast<float>(FinalMatrix.M[1][3]),
        static_cast<float>(FinalMatrix.M[2][0]), static_cast<float>(FinalMatrix.M[2][1]),
        static_cast<float>(FinalMatrix.M[2][2]), static_cast<float>(FinalMatrix.M[2][3]),
        static_cast<float>(FinalMatrix.M[3][0]), static_cast<float>(FinalMatrix.M[3][1]),
        static_cast<float>(FinalMatrix.M[3][2]), static_cast<float>(FinalMatrix.M[3][3])
    );
}

} // namespace Renderer
} // namespace MonsterEngine
