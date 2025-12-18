// Copyright Monster Engine. All Rights Reserved.

/**
 * @file ShadowDepthPass.cpp
 * @brief Shadow depth rendering pass implementation
 * 
 * Implements shadow map generation from light perspectives.
 * Reference: UE5 Engine/Source/Runtime/Renderer/Private/ShadowDepthRendering.cpp
 */

#include "Renderer/ShadowDepthPass.h"
#include "Renderer/ShadowRendering.h"
#include "Renderer/Scene.h"
#include "Renderer/SceneView.h"
#include "RHI/IRHIDevice.h"
#include "RHI/IRHICommandList.h"
#include "RHI/IRHIResource.h"
#include "RHI/RHIResources.h"
#include "Core/Logging/LogMacros.h"
#include "Math/MathUtility.h"

// Define log category for shadow depth pass
DEFINE_LOG_CATEGORY_STATIC(LogShadowDepthPass, Log, All);

namespace MonsterEngine
{
namespace Renderer
{

// ============================================================================
// FShadowDepthPass Implementation
// ============================================================================

FShadowDepthPass::FShadowDepthPass(MonsterRender::RHI::IRHIDevice* InDevice)
    : FRenderPassBase(FRenderPassConfig())
    , m_device(InDevice)
    , m_bInitialized(false)
{
    // Configure render pass for shadow depth
    Config.PassType = ERenderPassType::ShadowDepth;
    Config.PassName = TEXT("ShadowDepth");
    Config.bClearDepth = true;
    Config.ClearDepth = 1.0f;
    Config.bClearColor = false;
    Config.bEnabled = true;
    Config.Priority = 0;  // Shadow passes run first
    
    // Configure depth-stencil state for shadow depth
    Config.DepthStencilState.bDepthTestEnable = true;
    Config.DepthStencilState.bDepthWriteEnable = true;
    Config.DepthStencilState.DepthCompareFunc = ECompareFunc::Less;
    Config.DepthStencilState.bStencilTestEnable = false;
    
    // Configure rasterizer state for shadow depth
    Config.RasterizerState = FRasterizerState::ShadowDepth();
    Config.RasterizerState.CullMode = ECullMode::Back;
    
    // Configure blend state (no blending for depth-only)
    Config.BlendState.bBlendEnable = false;
    Config.BlendState.ColorWriteMask = 0;  // No color writes
    
    MR_LOG(LogShadowDepthPass, Verbose, "FShadowDepthPass created");
}

FShadowDepthPass::~FShadowDepthPass()
{
    release();
    MR_LOG(LogShadowDepthPass, Verbose, "FShadowDepthPass destroyed");
}

bool FShadowDepthPass::initialize()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_bInitialized)
    {
        MR_LOG(LogShadowDepthPass, Warning, "FShadowDepthPass already initialized");
        return true;
    }
    
    if (!m_device)
    {
        MR_LOG(LogShadowDepthPass, Error, "FShadowDepthPass::initialize - No RHI device");
        return false;
    }
    
    MR_LOG(LogShadowDepthPass, Log, "Initializing FShadowDepthPass");
    
    // Load shaders
    if (!_loadShaders())
    {
        MR_LOG(LogShadowDepthPass, Error, "FShadowDepthPass::initialize - Failed to load shaders");
        return false;
    }
    
    // Create pipeline state
    if (!_createPipelineState())
    {
        MR_LOG(LogShadowDepthPass, Error, "FShadowDepthPass::initialize - Failed to create pipeline state");
        return false;
    }
    
    // Create uniform buffer
    if (!_createUniformBuffer())
    {
        MR_LOG(LogShadowDepthPass, Error, "FShadowDepthPass::initialize - Failed to create uniform buffer");
        return false;
    }
    
    m_bInitialized = true;
    MR_LOG(LogShadowDepthPass, Log, "FShadowDepthPass initialized successfully");
    
    return true;
}

void FShadowDepthPass::release()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_bInitialized)
    {
        return;
    }
    
    MR_LOG(LogShadowDepthPass, Log, "Releasing FShadowDepthPass resources");
    
    // Release resources
    m_uniformBuffer.Reset();
    m_pipelineState.Reset();
    m_pipelineStateTwoSided.Reset();
    m_vertexShader.Reset();
    m_pixelShader.Reset();
    
    m_bInitialized = false;
    
    MR_LOG(LogShadowDepthPass, Log, "FShadowDepthPass resources released");
}

bool FShadowDepthPass::ShouldExecute(const FRenderPassContext& Context) const
{
    // Execute if we have valid context and shadow rendering is enabled
    if (!FRenderPassBase::ShouldExecute(Context))
    {
        return false;
    }
    
    // Check if there are any shadow-casting lights
    if (Context.VisibleLights && Context.VisibleLights->Num() > 0)
    {
        return true;
    }
    
    return false;
}

void FShadowDepthPass::Setup(FRenderPassContext& Context)
{
    FRenderPassBase::Setup(Context);
    
    // Additional setup for shadow depth pass
    MR_LOG(LogShadowDepthPass, Verbose, "FShadowDepthPass::Setup");
}

void FShadowDepthPass::Execute(FRenderPassContext& Context)
{
    if (!m_bInitialized || !Context.RHICmdList)
    {
        MR_LOG(LogShadowDepthPass, Warning, "FShadowDepthPass::Execute - Not initialized or no command list");
        return;
    }
    
    MR_LOG(LogShadowDepthPass, Verbose, "FShadowDepthPass::Execute begin");
    
    // Shadow depth rendering is typically done per-shadow in RenderShadowDepthMaps
    // This Execute is called as part of the render pass system
    
    MR_LOG(LogShadowDepthPass, Verbose, "FShadowDepthPass::Execute end");
}

void FShadowDepthPass::Cleanup(FRenderPassContext& Context)
{
    FRenderPassBase::Cleanup(Context);
    
    MR_LOG(LogShadowDepthPass, Verbose, "FShadowDepthPass::Cleanup");
}

void FShadowDepthPass::renderShadowDepth(
    MonsterRender::RHI::IRHICommandList& RHICmdList,
    const FProjectedShadowInfo* ShadowInfo,
    FShadowMap* ShadowMap)
{
    if (!m_bInitialized)
    {
        MR_LOG(LogShadowDepthPass, Warning, "FShadowDepthPass::renderShadowDepth - Not initialized");
        return;
    }
    
    if (!ShadowInfo || !ShadowMap)
    {
        MR_LOG(LogShadowDepthPass, Warning, "FShadowDepthPass::renderShadowDepth - Invalid parameters");
        return;
    }
    
    MR_LOG(LogShadowDepthPass, Verbose, "Rendering shadow depth for shadow at resolution %ux%u",
           ShadowInfo->ResolutionX, ShadowInfo->ResolutionY);
    
    // Update uniform buffer with shadow parameters
    updateUniformBuffer(ShadowInfo);
    
    // Set render target
    _setRenderTarget(RHICmdList, ShadowMap);
    
    // Set viewport
    _setViewport(RHICmdList, ShadowInfo);
    
    // Bind pipeline state
    _bindPipelineState(RHICmdList);
    
    // Render shadow-casting primitives
    _renderShadowPrimitives(RHICmdList, ShadowInfo);
    
    MR_LOG(LogShadowDepthPass, Verbose, "Shadow depth rendering complete");
}

void FShadowDepthPass::renderShadowDepthBatch(
    MonsterRender::RHI::IRHICommandList& RHICmdList,
    const TArray<FProjectedShadowInfo*>& Shadows)
{
    if (!m_bInitialized)
    {
        MR_LOG(LogShadowDepthPass, Warning, "FShadowDepthPass::renderShadowDepthBatch - Not initialized");
        return;
    }
    
    MR_LOG(LogShadowDepthPass, Verbose, "Rendering batch of %d shadows", Shadows.Num());
    
    for (FProjectedShadowInfo* ShadowInfo : Shadows)
    {
        if (ShadowInfo && ShadowInfo->RenderTargets.isValid())
        {
            // Use render targets from shadow info
            // TODO: Create FShadowMap wrapper or use RenderTargets directly
        }
    }
    
    MR_LOG(LogShadowDepthPass, Verbose, "Batch shadow depth rendering complete");
}

void FShadowDepthPass::clearShadowDepth(
    MonsterRender::RHI::IRHICommandList& RHICmdList,
    FShadowMap* ShadowMap)
{
    if (!ShadowMap)
    {
        return;
    }
    
    MR_LOG(LogShadowDepthPass, Verbose, "Clearing shadow depth target");
    
    // Set render target and clear
    _setRenderTarget(RHICmdList, ShadowMap);
    
    // TODO: Issue clear command through RHI
    // RHICmdList.ClearDepthStencilTarget(ShadowMap->getDepthTexture(), 1.0f, 0);
}

void FShadowDepthPass::setConfig(const FShadowDepthPassConfig& InConfig)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_shadowConfig = InConfig;
    
    // Update rasterizer state based on config
    Config.RasterizerState.DepthBias = InConfig.DepthBiasConstant;
    Config.RasterizerState.SlopeScaledDepthBias = InConfig.DepthBiasSlopeScale;
    
    MR_LOG(LogShadowDepthPass, Verbose, "Shadow depth pass config updated: resolution=%u, depthBias=%.2f",
           InConfig.ShadowMapResolution, InConfig.DepthBiasConstant);
}

void FShadowDepthPass::updateUniformBuffer(const FProjectedShadowInfo* ShadowInfo)
{
    if (!ShadowInfo)
    {
        return;
    }
    
    // Update uniform parameters from shadow info
    m_uniformParams.LightViewMatrix = ShadowInfo->TranslatedWorldToView;
    m_uniformParams.LightProjectionMatrix = ShadowInfo->ViewToClipOuter;
    m_uniformParams.LightViewProjectionMatrix = FMatrix(ShadowInfo->TranslatedWorldToClipOuterMatrix);
    
    // Set light position based on light type
    // Get light info from shadow info
    FLightSceneInfo* LightInfo = ShadowInfo->getLightSceneInfo();
    if (ShadowInfo->bDirectionalLight)
    {
        // Directional light: position is direction, w = 0
        // Use PreShadowTranslation as reference direction
        FVector LightDir = ShadowInfo->PreShadowTranslation.GetSafeNormal();
        m_uniformParams.LightPosition = FVector4f(
            static_cast<float>(LightDir.X),
            static_cast<float>(LightDir.Y),
            static_cast<float>(LightDir.Z),
            0.0f);
    }
    else
    {
        // Point/spot light: position is world position, w = 1
        FVector LightPos = ShadowInfo->ShadowBounds.Center;
        m_uniformParams.LightPosition = FVector4f(
            static_cast<float>(LightPos.X),
            static_cast<float>(LightPos.Y),
            static_cast<float>(LightPos.Z),
            1.0f);
    }
    
    // Light direction from PreShadowTranslation
    FVector LightDirection = ShadowInfo->PreShadowTranslation.GetSafeNormal();
    m_uniformParams.LightDirection = FVector4f(
        static_cast<float>(LightDirection.X),
        static_cast<float>(LightDirection.Y),
        static_cast<float>(LightDirection.Z),
        0.0f);
    
    // Set bias parameters
    m_uniformParams.DepthBias = ShadowInfo->getShaderDepthBias();
    m_uniformParams.SlopeScaledBias = ShadowInfo->getShaderSlopeDepthBias();
    m_uniformParams.NormalOffsetBias = ShadowInfo->BiasParameters.NormalOffsetBias;
    m_uniformParams.ShadowDistance = ShadowInfo->MaxSubjectZ - ShadowInfo->MinSubjectZ;
    m_uniformParams.InvMaxSubjectDepth = ShadowInfo->InvMaxSubjectDepth;
    m_uniformParams.bClampToNearPlane = ShadowInfo->bDirectionalLight ? 1.0f : 0.0f;
    
    // Update push constants
    m_pushConstants.bAlphaTest = 0;  // TODO: Determine from material
    m_pushConstants.bPointLight = ShadowInfo->bOnePassPointLightShadow ? 1 : 0;
    
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
    
    MR_LOG(LogShadowDepthPass, Verbose, "Uniform buffer updated: depthBias=%.4f, slopeBias=%.4f",
           m_uniformParams.DepthBias, m_uniformParams.SlopeScaledBias);
}

// ============================================================================
// Internal Methods
// ============================================================================

bool FShadowDepthPass::_loadShaders()
{
    MR_LOG(LogShadowDepthPass, Log, "Loading shadow depth shaders");
    
    // TODO: Load shaders through shader manager
    // The shader files are:
    // - Shaders/Forward/ShadowDepth.vert
    // - Shaders/Forward/ShadowDepth.frag
    
    // For now, just log that we would load shaders
    MR_LOG(LogShadowDepthPass, Log, "Shader paths: %s, %s", 
           SHADOW_DEPTH_VERT_PATH, SHADOW_DEPTH_FRAG_PATH);
    
    // TODO: Implement actual shader loading
    // m_vertexShader = m_device->createVertexShader(SHADOW_DEPTH_VERT_PATH);
    // m_pixelShader = m_device->createPixelShader(SHADOW_DEPTH_FRAG_PATH);
    
    return true;  // Return true for now to allow initialization to continue
}

bool FShadowDepthPass::_createPipelineState()
{
    MR_LOG(LogShadowDepthPass, Log, "Creating shadow depth pipeline state");
    
    // TODO: Create pipeline state through RHI device
    // PipelineStateDesc desc;
    // desc.vertexShader = m_vertexShader;
    // desc.pixelShader = m_pixelShader;
    // desc.depthStencilState = Config.DepthStencilState;
    // desc.rasterizerState = Config.RasterizerState;
    // desc.blendState = Config.BlendState;
    // m_pipelineState = m_device->createPipelineState(desc);
    
    // Create two-sided variant for certain shadow casters
    // desc.rasterizerState.CullMode = ECullMode::None;
    // m_pipelineStateTwoSided = m_device->createPipelineState(desc);
    
    return true;  // Return true for now
}

bool FShadowDepthPass::_createUniformBuffer()
{
    MR_LOG(LogShadowDepthPass, Log, "Creating shadow depth uniform buffer");
    
    // TODO: Create uniform buffer through RHI device
    // BufferDesc desc;
    // desc.size = sizeof(FShadowDepthPassUniformParameters);
    // desc.usage = EResourceUsage::UniformBuffer;
    // desc.debugName = "ShadowDepthPassUniformBuffer";
    // m_uniformBuffer = m_device->createBuffer(desc);
    
    return true;  // Return true for now
}

void FShadowDepthPass::_bindPipelineState(MonsterRender::RHI::IRHICommandList& RHICmdList)
{
    // TODO: Bind pipeline state through command list
    // RHICmdList.setPipelineState(m_pipelineState.Get());
    
    // Bind uniform buffer
    // RHICmdList.setUniformBuffer(0, m_uniformBuffer.Get());
    
    MR_LOG(LogShadowDepthPass, Verbose, "Pipeline state bound");
}

void FShadowDepthPass::_renderShadowPrimitives(
    MonsterRender::RHI::IRHICommandList& RHICmdList,
    const FProjectedShadowInfo* ShadowInfo)
{
    if (!ShadowInfo)
    {
        return;
    }
    
    MR_LOG(LogShadowDepthPass, Verbose, "Rendering shadow primitives for shadow %d",
           ShadowInfo->ShadowId);
    
    // TODO: Iterate through shadow-casting primitives and render them
    // for (FPrimitiveSceneInfo* Primitive : ShadowInfo->DynamicSubjectPrimitives)
    // {
    //     if (Primitive && Primitive->bCastDynamicShadow)
    //     {
    //         // Get mesh batches from primitive
    //         // Render each mesh batch
    //     }
    // }
}

void FShadowDepthPass::_setRenderTarget(
    MonsterRender::RHI::IRHICommandList& RHICmdList,
    FShadowMap* ShadowMap)
{
    if (!ShadowMap)
    {
        return;
    }
    
    // TODO: Set render target through command list
    // RHICmdList.setRenderTarget(nullptr, ShadowMap->getDepthTexture());
    
    MR_LOG(LogShadowDepthPass, Verbose, "Render target set for shadow map");
}

void FShadowDepthPass::_setViewport(
    MonsterRender::RHI::IRHICommandList& RHICmdList,
    const FProjectedShadowInfo* ShadowInfo)
{
    if (!ShadowInfo)
    {
        return;
    }
    
    // Calculate viewport from shadow info
    int32 ViewportX = ShadowInfo->X + ShadowInfo->BorderSize;
    int32 ViewportY = ShadowInfo->Y + ShadowInfo->BorderSize;
    int32 ViewportWidth = ShadowInfo->ResolutionX - 2 * ShadowInfo->BorderSize;
    int32 ViewportHeight = ShadowInfo->ResolutionY - 2 * ShadowInfo->BorderSize;
    
    // TODO: Set viewport through command list
    // RHICmdList.setViewport(ViewportX, ViewportY, ViewportWidth, ViewportHeight, 0.0f, 1.0f);
    
    MR_LOG(LogShadowDepthPass, Verbose, "Viewport set: %d,%d %dx%d",
           ViewportX, ViewportY, ViewportWidth, ViewportHeight);
}

// ============================================================================
// FShadowDepthPassProcessor Implementation
// ============================================================================

FShadowDepthPassProcessor::FShadowDepthPassProcessor(
    FScene* InScene,
    const FProjectedShadowInfo* InShadowInfo)
    : m_scene(InScene)
    , m_shadowInfo(InShadowInfo)
{
    MR_LOG(LogShadowDepthPass, Verbose, "FShadowDepthPassProcessor created");
}

FShadowDepthPassProcessor::~FShadowDepthPassProcessor()
{
    clear();
    MR_LOG(LogShadowDepthPass, Verbose, "FShadowDepthPassProcessor destroyed");
}

bool FShadowDepthPassProcessor::addMeshBatch(const FMeshBatch& MeshBatch, bool bCastShadow)
{
    if (!bCastShadow)
    {
        return false;
    }
    
    // Add mesh batch to collection
    m_meshBatches.Add(MeshBatch);
    
    // Determine if alpha testing is required
    // TODO: Check material for masked blend mode
    bool bRequiresAlphaTest = false;
    m_meshRequiresAlphaTest.Add(bRequiresAlphaTest);
    
    return true;
}

void FShadowDepthPassProcessor::process(
    MonsterRender::RHI::IRHICommandList& RHICmdList,
    FShadowDepthPass* ShadowDepthPass)
{
    if (!ShadowDepthPass || m_meshBatches.Num() == 0)
    {
        return;
    }
    
    MR_LOG(LogShadowDepthPass, Verbose, "Processing %d mesh batches for shadow depth",
           m_meshBatches.Num());
    
    // TODO: Process each mesh batch
    // for (int32 i = 0; i < m_meshBatches.Num(); ++i)
    // {
    //     const FMeshBatch& MeshBatch = m_meshBatches[i];
    //     bool bAlphaTest = m_meshRequiresAlphaTest[i];
    //     
    //     // Set push constants for alpha test
    //     // Draw mesh batch
    // }
}

void FShadowDepthPassProcessor::clear()
{
    m_meshBatches.Empty();
    m_meshRequiresAlphaTest.Empty();
}

} // namespace Renderer
} // namespace MonsterEngine
