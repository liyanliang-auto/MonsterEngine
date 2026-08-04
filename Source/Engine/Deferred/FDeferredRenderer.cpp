// Copyright Monster Engine. All Rights Reserved.

/**
 * @file FDeferredRenderer.cpp
 * @brief Deferred Renderer MVP Implementation
 *
 * References:
 *   - FCubeSceneProxy::CreatePipelineState  (Pipeline template)
 *   - CubeSceneApplication::createPBRPipeline (Shader loading template)
 *   - CubeSceneApplication::initializeViewportRenderTarget (RT creation template)
 */

#include "Engine/Deferred/FDeferredRenderer.h"

#include "Core/CoreMinimal.h"
#include "Core/ShaderCompiler.h"
#include "Math/Matrix.h"

#include <cstring>  // std::memcpy
#include <vector>

DEFINE_LOG_CATEGORY_STATIC(LogDeferredRenderer, All, All);

namespace MonsterEngine
{
namespace Deferred
{

// ============================================================================
// Constants
// ============================================================================

namespace
{
    /** Fixed formats for GBuffer render targets (MVP version) */
    constexpr MonsterRender::RHI::EPixelFormat kNormalFormat =
        MonsterRender::RHI::EPixelFormat::R32G32B32A32_FLOAT;

    constexpr MonsterRender::RHI::EPixelFormat kAlbedoFormat =
        MonsterRender::RHI::EPixelFormat::R8G8B8A8_UNORM;

    /** Vertex stride, matches FCubeLitVertex (pos3 + normal3 + uv2) */
    constexpr uint32 kCubeLitVertexStride = sizeof(float) * 8;

    /** Shader SPIR-V relative paths */
    constexpr const char* kGeometryVsPath = "Shaders/Deferred/GeometryPass.vert.spv";
    constexpr const char* kGeometryPsPath = "Shaders/Deferred/GeometryPass.frag.spv";
    constexpr const char* kLightingVsPath = "Shaders/Deferred/LightingPass.vert.spv";
    constexpr const char* kLightingPsPath = "Shaders/Deferred/LightingPass.frag.spv";
    constexpr const char* kTAAVsPath      = "Shaders/Deferred/TAAPass.vert.spv";
    constexpr const char* kTAAPsPath      = "Shaders/Deferred/TAAPass.frag.spv";
    constexpr const char* kFXAAVsPath     = "Shaders/PostProcess/FXAAPass.vert.spv";
    constexpr const char* kFXAAPsPath     = "Shaders/PostProcess/FXAAPass.frag.spv";

} // anonymous namespace


// ============================================================================
// Constructor / Destructor
// ============================================================================

FDeferredRenderer::FDeferredRenderer() = default;

FDeferredRenderer::~FDeferredRenderer()
{
    Shutdown();
}


// ============================================================================
// Lifecycle
// ============================================================================

bool FDeferredRenderer::Initialize(
    MonsterRender::RHI::IRHIDevice* InDevice,
    uint32 InWidth,
    uint32 InHeight)
{
    if (bInitialized)
    {
        MR_LOG(LogDeferredRenderer, Warning, "Initialize called on already-initialized renderer");
        return true;
    }

    if (!InDevice)
    {
        MR_LOG(LogDeferredRenderer, Error, "Initialize: null device");
        return false;
    }

    if (InWidth == 0 || InHeight == 0)
    {
        MR_LOG(LogDeferredRenderer, Error, "Initialize: invalid dimensions %ux%u", InWidth, InHeight);
        return false;
    }

    // MVP currently only supports Vulkan (OpenGL support to be added later)
    if (InDevice->getRHIBackend() != MonsterRender::RHI::ERHIBackend::Vulkan)
    {
        MR_LOG(LogDeferredRenderer, Error,
            "Initialize: Deferred MVP only supports Vulkan, got %s",
            MonsterRender::RHI::GetRHIBackendName(InDevice->getRHIBackend()));
        return false;
    }

    Device = InDevice;

    MR_LOG(LogDeferredRenderer, Log, "=== Initializing Deferred Renderer (%ux%u) ===", InWidth, InHeight);

    // Creation order: Shader → UBO → Sampler → Pipeline → GBuffer
    // Shader and Pipeline are created once; GBuffer is recreated on Resize
    if (!LoadShaders())            { Shutdown(); return false; }
    if (!CreateUniformBuffers())   { Shutdown(); return false; }
    if (!CreateSampler())          { Shutdown(); return false; }
    if (!CreateGeometryPipeline()) { Shutdown(); return false; }
    if (!CreateLightingPipeline()) { Shutdown(); return false; }
    if (!CreateGBuffer(InWidth, InHeight)) { Shutdown(); return false; }
    
    // Create FXAA resources (optional, for spatial anti-aliasing)
    if (!CreateFXAAResources())    { Shutdown(); return false; }
    if (!CreateFXAAPipeline())     { Shutdown(); return false; }

    bInitialized = true;
    MR_LOG(LogDeferredRenderer, Log, "Deferred Renderer initialized successfully");
    return true;
}

void FDeferredRenderer::Shutdown()
{
    if (!Device && !bInitialized)
    {
        return;
    }

    // Resources released in reverse creation order (TSharedPtr auto-releases)
    ReleaseGBuffer();
    
    // Release FXAA resources
    ReleaseFXAAResources();

    LightingPipeline.Reset();
    GeometryPipeline.Reset();
    GBufferSampler.Reset();
    SceneUniformBuffer.Reset();
    TransformUniformBuffer.Reset();
    LightingPS.Reset();
    LightingVS.Reset();
    GeometryPS.Reset();
    GeometryVS.Reset();

    Device = nullptr;
    bInitialized = false;
}

bool FDeferredRenderer::Resize(uint32 InWidth, uint32 InHeight)
{
    if (!bInitialized)
    {
        MR_LOG(LogDeferredRenderer, Error, "Resize before Initialize");
        return false;
    }

    if (InWidth == 0 || InHeight == 0)
    {
        MR_LOG(LogDeferredRenderer, Error, "Resize: invalid dimensions %ux%u", InWidth, InHeight);
        return false;
    }

    if (InWidth == GBuffer.Width && InHeight == GBuffer.Height)
    {
        return true;  // No need to recreate
    }

    MR_LOG(LogDeferredRenderer, Log, "Resizing GBuffer from %ux%u to %ux%u",
           GBuffer.Width, GBuffer.Height, InWidth, InHeight);

    ReleaseGBuffer();
    return CreateGBuffer(InWidth, InHeight);
}


// ============================================================================
// Uniform Buffer Updates
// ============================================================================

void FDeferredRenderer::UpdateTransformUBO(
    const Math::FMatrix44f& Model,
    const Math::FMatrix44f& View,
    const Math::FMatrix44f& Proj,
    const Math::FVector4f&  CameraPosition)
{
    if (!TransformUniformBuffer)
    {
        MR_LOG(LogDeferredRenderer, Error, "UpdateTransformUBO: UBO not ready");
        return;
    }

    FDeferredTransformUBO Data;
    Data.Model         = Model;
    Data.View          = View;
    Data.Proj          = Proj;

    // NormalMatrix = inverse-transpose(Model)
    // In row-vector convention: worldNormal = normal * mat3(NormalMatrix)
    Data.NormalMatrix  = Model.Inverse().GetTransposed();

    Data.CameraPos     = CameraPosition;

    // TAA: Store previous frame MVP matrices for motion vector calculation
    static Math::FMatrix44f previousModel = Model;
    static Math::FMatrix44f previousView = View;
    static Math::FMatrix44f previousProj = Proj;
    
    Data.PreviousModel = previousModel;
    Data.PreviousView  = previousView;
    Data.PreviousProj  = previousProj;
    
    // Update previous matrices for next frame
    previousModel = Model;
    previousView = View;
    previousProj = Proj;

    WriteUniformBuffer(TransformUniformBuffer, &Data, sizeof(Data));
}

void FDeferredRenderer::UpdateSceneUBO(
    const Math::FMatrix44f& InvViewProj,
    const Math::FVector4f&  CameraPosition,
    const Math::FVector4f&  DirLightDirection,
    const Math::FVector4f&  DirLightColorIntensity,
    const Math::FVector4f&  PointLightPositionRadius,
    const Math::FVector4f&  PointLightColorIntensity,
    float AmbientFactor)
{
    if (!SceneUniformBuffer)
    {
        MR_LOG(LogDeferredRenderer, Error, "UpdateSceneUBO: UBO not ready");
        return;
    }

    FDeferredSceneUBO Data;
    Data.InvViewProj                 = InvViewProj;
    Data.CameraPos                   = CameraPosition;
    Data.DirLightDirection           = DirLightDirection;
    Data.DirLightColorIntensity      = DirLightColorIntensity;
    Data.PointLightPositionRadius    = PointLightPositionRadius;
    Data.PointLightColorIntensity    = PointLightColorIntensity;
    Data.Ambient                     = Math::FVector4f(AmbientFactor, 0.0f, 0.0f, 0.0f);

    // TAA parameters
    Data.PreviousViewProj            = PreviousViewProj;
    Data.JitterOffset                = Math::FVector4f(CurrentJitter.X, CurrentJitter.Y, 
                                                       PreviousJitter.X, PreviousJitter.Y);
    Data.TAAParams                   = Math::FVector4f(TAAConfig.BlendFactor, TAAConfig.Sharpness,
                                                       TAAConfig.EnableSharpening ? 1.0f : 0.0f, 0.0f);

    WriteUniformBuffer(SceneUniformBuffer, &Data, sizeof(Data));
}


// ============================================================================
// Internal Build Steps
// ============================================================================

bool FDeferredRenderer::CreateGBuffer(uint32 InWidth, uint32 InHeight)
{
    using namespace MonsterRender::RHI;

    // ---- Normal Target (RGBA32F) ----
    {
        TextureDesc Desc;
        Desc.width     = InWidth;
        Desc.height    = InHeight;
        Desc.depth     = 1;
        Desc.mipLevels = 1;
        Desc.arraySize = 1;
        Desc.format    = kNormalFormat;
        Desc.usage     = EResourceUsage::RenderTarget | EResourceUsage::ShaderResource;
        Desc.debugName = "Deferred GBuffer Normal";

        GBuffer.NormalTarget = Device->createTexture(Desc);
        if (!GBuffer.NormalTarget)
        {
            MR_LOG(LogDeferredRenderer, Error, "Failed to create GBuffer Normal target");
            return false;
        }
    }

    // ---- Albedo Target (RGBA8) ----
    {
        TextureDesc Desc;
        Desc.width     = InWidth;
        Desc.height    = InHeight;
        Desc.depth     = 1;
        Desc.mipLevels = 1;
        Desc.arraySize = 1;
        Desc.format    = kAlbedoFormat;
        Desc.usage     = EResourceUsage::RenderTarget | EResourceUsage::ShaderResource;
        Desc.debugName = "Deferred GBuffer Albedo";

        GBuffer.AlbedoTarget = Device->createTexture(Desc);
        if (!GBuffer.AlbedoTarget)
        {
            MR_LOG(LogDeferredRenderer, Error, "Failed to create GBuffer Albedo target");
            return false;
        }
    }

    // ---- Depth Target (match device depth format, typically D32_FLOAT) ----
    // Use ShaderResource flag so Lighting Pass can sample to reconstruct Position
    {
        TextureDesc Desc;
        Desc.width     = InWidth;
        Desc.height    = InHeight;
        Desc.depth     = 1;
        Desc.mipLevels = 1;
        Desc.arraySize = 1;
        Desc.format    = Device->getDepthFormat();
        Desc.usage     = EResourceUsage::DepthStencil | EResourceUsage::ShaderResource;
        Desc.debugName = "Deferred GBuffer Depth";

        GBuffer.DepthTarget = Device->createTexture(Desc);
        if (!GBuffer.DepthTarget)
        {
            MR_LOG(LogDeferredRenderer, Error, "Failed to create GBuffer Depth target");
            return false;
        }
    }

    // ---- Motion Vector Target (RG16F) for TAA ----
    {
        TextureDesc Desc;
        Desc.width     = InWidth;
        Desc.height    = InHeight;
        Desc.depth     = 1;
        Desc.mipLevels = 1;
        Desc.arraySize = 1;
        Desc.format    = EPixelFormat::R32G32_FLOAT;
        Desc.usage     = EResourceUsage::RenderTarget | EResourceUsage::ShaderResource;
        Desc.debugName = "Motion Vector RT";

        MotionVectorTarget = Device->createTexture(Desc);
        if (!MotionVectorTarget)
        {
            MR_LOG(LogDeferredRenderer, Error, "Failed to create Motion Vector RT");
            return false;
        }
    }

    GBuffer.Width  = InWidth;
    GBuffer.Height = InHeight;

    MR_LOG(LogDeferredRenderer, Log,
           "GBuffer created: Normal=RGBA32F, Albedo=RGBA8, Depth=device-default, MotionVector=RG16F (%ux%u)",
           InWidth, InHeight);
    return true;
}

void FDeferredRenderer::ReleaseGBuffer()
{
    GBuffer.NormalTarget.Reset();
    GBuffer.AlbedoTarget.Reset();
    GBuffer.DepthTarget.Reset();
    MotionVectorTarget.Reset();
    GBuffer.Width  = 0;
    GBuffer.Height = 0;
}

bool FDeferredRenderer::LoadShaders()
{
    std::vector<MonsterRender::uint8> SpvGeomVS = MonsterRender::ShaderCompiler::readFileBytes(kGeometryVsPath);
    std::vector<MonsterRender::uint8> SpvGeomPS = MonsterRender::ShaderCompiler::readFileBytes(kGeometryPsPath);
    std::vector<MonsterRender::uint8> SpvLitVS  = MonsterRender::ShaderCompiler::readFileBytes(kLightingVsPath);
    std::vector<MonsterRender::uint8> SpvLitPS  = MonsterRender::ShaderCompiler::readFileBytes(kLightingPsPath);

    if (SpvGeomVS.empty() || SpvGeomPS.empty() || SpvLitVS.empty() || SpvLitPS.empty())
    {
        MR_LOG(LogDeferredRenderer, Error,
               "LoadShaders: missing SPIR-V. VS(%zu) PS(%zu) LitVS(%zu) LitPS(%zu)",
               SpvGeomVS.size(), SpvGeomPS.size(), SpvLitVS.size(), SpvLitPS.size());
        return false;
    }

    GeometryVS = Device->createVertexShader(std::span<const MonsterRender::uint8>(SpvGeomVS.data(), SpvGeomVS.size()));
    GeometryPS = Device->createPixelShader (std::span<const MonsterRender::uint8>(SpvGeomPS.data(), SpvGeomPS.size()));
    LightingVS = Device->createVertexShader(std::span<const MonsterRender::uint8>(SpvLitVS.data(),  SpvLitVS.size()));
    LightingPS = Device->createPixelShader (std::span<const MonsterRender::uint8>(SpvLitPS.data(),  SpvLitPS.size()));

    if (!GeometryVS || !GeometryPS || !LightingVS || !LightingPS)
    {
        MR_LOG(LogDeferredRenderer, Error, "Failed to create shader objects from SPIR-V");
        return false;
    }

    MR_LOG(LogDeferredRenderer, Log, "All 4 Deferred shaders loaded and created");
    return true;
}

bool FDeferredRenderer::CreateUniformBuffers()
{
    using namespace MonsterRender::RHI;

    // Transform UBO
    {
        BufferDesc Desc;
        Desc.size          = sizeof(FDeferredTransformUBO);  // 272 bytes
        Desc.usage         = EResourceUsage::UniformBuffer;
        Desc.cpuAccessible = true;
        Desc.debugName     = "Deferred Transform UBO";

        TransformUniformBuffer = Device->createBuffer(Desc);
        if (!TransformUniformBuffer)
        {
            MR_LOG(LogDeferredRenderer, Error, "Failed to create Transform UBO");
            return false;
        }
    }

    // Scene UBO
    {
        BufferDesc Desc;
        Desc.size          = sizeof(FDeferredSceneUBO);      // 160 bytes
        Desc.usage         = EResourceUsage::UniformBuffer;
        Desc.cpuAccessible = true;
        Desc.debugName     = "Deferred Scene UBO";

        SceneUniformBuffer = Device->createBuffer(Desc);
        if (!SceneUniformBuffer)
        {
            MR_LOG(LogDeferredRenderer, Error, "Failed to create Scene UBO");
            return false;
        }
    }

    MR_LOG(LogDeferredRenderer, Log,
           "Uniform buffers created (Transform=%u bytes, Scene=%u bytes)",
           static_cast<uint32>(sizeof(FDeferredTransformUBO)),
           static_cast<uint32>(sizeof(FDeferredSceneUBO)));
    return true;
}

bool FDeferredRenderer::CreateSampler()
{
    using namespace MonsterRender::RHI;

    SamplerDesc Desc;
    Desc.filter         = ESamplerFilter::Bilinear;    // Linear is sufficient for GBuffer sampling, no mip
    Desc.addressU       = ESamplerAddressMode::Clamp;  // Prevent out-of-bounds sampling to wrap edges
    Desc.addressV       = ESamplerAddressMode::Clamp;
    Desc.addressW       = ESamplerAddressMode::Clamp;
    Desc.maxAnisotropy  = 1;                            // Fullscreen sampling doesn't need anisotropy
    Desc.debugName      = "Deferred GBuffer Sampler";

    GBufferSampler = Device->createSampler(Desc);
    if (!GBufferSampler)
    {
        MR_LOG(LogDeferredRenderer, Error, "Failed to create GBuffer sampler");
        return false;
    }

    return true;
}

bool FDeferredRenderer::CreateGeometryPipeline()
{
    using namespace MonsterRender::RHI;

    PipelineStateDesc Desc;
    Desc.vertexShader      = GeometryVS;
    Desc.pixelShader       = GeometryPS;
    Desc.primitiveTopology = EPrimitiveTopology::TriangleList;

    // ---- Vertex layout (matches FCubeLitVertex: pos3 + normal3 + uv2) ----
    VertexAttribute PosAttr;
    PosAttr.location     = 0;
    PosAttr.format       = EVertexFormat::Float3;
    PosAttr.offset       = 0;
    PosAttr.semanticName = "POSITION";

    VertexAttribute NormalAttr;
    NormalAttr.location     = 1;
    NormalAttr.format       = EVertexFormat::Float3;
    NormalAttr.offset       = sizeof(float) * 3;
    NormalAttr.semanticName = "NORMAL";

    VertexAttribute UVAttr;
    UVAttr.location     = 2;
    UVAttr.format       = EVertexFormat::Float2;
    UVAttr.offset       = sizeof(float) * 6;
    UVAttr.semanticName = "TEXCOORD";

    Desc.vertexLayout.attributes.push_back(PosAttr);
    Desc.vertexLayout.attributes.push_back(NormalAttr);
    Desc.vertexLayout.attributes.push_back(UVAttr);
    Desc.vertexLayout.stride = kCubeLitVertexStride;

    // ---- Rasterizer (consistent with Cube pipeline: CW is front after viewport Y-flip) ----
    Desc.rasterizerState.fillMode                = EFillMode::Solid;
    Desc.rasterizerState.cullMode                = ECullMode::Back;
    Desc.rasterizerState.frontCounterClockwise   = false;

    // ---- Depth (enable testing and writing) ----
    Desc.depthStencilState.depthEnable      = true;
    Desc.depthStencilState.depthWriteEnable = true;
    Desc.depthStencilState.depthCompareOp   = ECompareOp::Less;

    // ---- Blend (disabled, GBuffer doesn't blend) ----
    Desc.blendState.blendEnable = false;

    // ---- MRT render targets ----
    // location 0 -> Normal (RGBA32F)
    // location 1 -> Albedo (RGBA8)
    Desc.renderTargetFormats.push_back(kNormalFormat);
    Desc.renderTargetFormats.push_back(kAlbedoFormat);
    Desc.depthStencilFormat = Device->getDepthFormat();

    Desc.debugName = "Deferred Geometry Pipeline";

    GeometryPipeline = Device->createPipelineState(Desc);
    if (!GeometryPipeline)
    {
        MR_LOG(LogDeferredRenderer, Error, "Failed to create Geometry Pipeline");
        return false;
    }

    MR_LOG(LogDeferredRenderer, Log,
           "Geometry Pipeline created (2 MRT: Normal RGBA32F + Albedo RGBA8)");
    return true;
}

bool FDeferredRenderer::CreateLightingPipeline()
{
    using namespace MonsterRender::RHI;

    PipelineStateDesc Desc;
    Desc.vertexShader      = LightingVS;
    Desc.pixelShader       = LightingPS;
    Desc.primitiveTopology = EPrimitiveTopology::TriangleList;

    // ---- Vertex layout empty (LightingPass.vert uses gl_VertexIndex to generate fullscreen triangle) ----
    Desc.vertexLayout.stride = 0;
    // Desc.vertexLayout.attributes remains default empty

    // ---- Rasterizer ----
    //   Fullscreen triangle vertices outside NDC are clipped by hardware. Disable backface culling to avoid orientation issues
    Desc.rasterizerState.fillMode              = EFillMode::Solid;
    Desc.rasterizerState.cullMode              = ECullMode::None;
    Desc.rasterizerState.frontCounterClockwise = false;

    // ---- Depth all disabled (lighting composition doesn't need and shouldn't write depth) ----
    Desc.depthStencilState.depthEnable      = false;
    Desc.depthStencilState.depthWriteEnable = false;
    Desc.depthStencilState.depthCompareOp   = ECompareOp::Always;

    // ---- Blend disabled (directly overwrite final color) ----
    Desc.blendState.blendEnable = false;

    // ---- Output: target is current viewport color (same as Cube) ----
    Desc.renderTargetFormats.push_back(Device->getSwapChainFormat());
    Desc.depthStencilFormat = EPixelFormat::Unknown;  // No depth attachment used

    Desc.debugName = "Deferred Lighting Pipeline";

    LightingPipeline = Device->createPipelineState(Desc);
    if (!LightingPipeline)
    {
        MR_LOG(LogDeferredRenderer, Error, "Failed to create Lighting Pipeline");
        return false;
    }

    MR_LOG(LogDeferredRenderer, Log, "Lighting Pipeline created (fullscreen triangle, no VBO)");
    return true;
}

bool FDeferredRenderer::WriteUniformBuffer(
    const TSharedPtr<MonsterRender::RHI::IRHIBuffer>& Buffer,
    const void* Data,
    uint32 Size)
{
    if (!Buffer || !Data || Size == 0)
    {
        return false;
    }


    void* Mapped = Buffer->map();
    if (!Mapped)
    {
        MR_LOG(LogDeferredRenderer, Error, "Failed to map uniform buffer '%s'",
               Buffer->getDebugName().c_str());
        return false;
    }

    std::memcpy(Mapped, Data, Size);
    Buffer->unmap();
    return true;
}

// ============================================================================
// TAA (Temporal Anti-Aliasing) Implementation
// ============================================================================

bool FDeferredRenderer::CreateTAAResources()
{
    using namespace MonsterRender::RHI;

    // Get current GBuffer dimensions
    uint32 width = GBuffer.Width;
    uint32 height = GBuffer.Height;

    // ---- Lighting Target (RGBA8) ----
    {
        TextureDesc Desc;
        Desc.width     = width;
        Desc.height    = height;
        Desc.depth     = 1;
        Desc.mipLevels = 1;
        Desc.arraySize = 1;
        Desc.format    = EPixelFormat::R8G8B8A8_UNORM;
        Desc.usage     = EResourceUsage::RenderTarget | EResourceUsage::ShaderResource;
        Desc.debugName = "Lighting RT";

        LightingTarget = Device->createTexture(Desc);
        if (!LightingTarget)
        {
            MR_LOG(LogDeferredRenderer, Error, "Failed to create Lighting RT");
            return false;
        }
    }

    // ---- History Target (RGBA8) ----
    {
        TextureDesc Desc;
        Desc.width     = width;
        Desc.height    = height;
        Desc.depth     = 1;
        Desc.mipLevels = 1;
        Desc.arraySize = 1;
        Desc.format    = EPixelFormat::R8G8B8A8_UNORM;
        Desc.usage     = EResourceUsage::RenderTarget | EResourceUsage::ShaderResource;
        Desc.debugName = "History RT";

        HistoryTarget = Device->createTexture(Desc);
        if (!HistoryTarget)
        {
            MR_LOG(LogDeferredRenderer, Error, "Failed to create History RT");
            return false;
        }
    }

    MR_LOG(LogDeferredRenderer, Log, "TAA resources created: Lighting RT + History RT (%ux%u)", width, height);
    return true;
}

bool FDeferredRenderer::CreateTAAPipeline()
{
    using namespace MonsterRender::RHI;

    // Load TAA shaders
    std::vector<MonsterRender::uint8> SpvTAAVS = MonsterRender::ShaderCompiler::readFileBytes(kTAAVsPath);
    std::vector<MonsterRender::uint8> SpvTAAPS = MonsterRender::ShaderCompiler::readFileBytes(kTAAPsPath);

    if (SpvTAAVS.empty() || SpvTAAPS.empty())
    {
        MR_LOG(LogDeferredRenderer, Error, "Failed to load TAA shader SPIR-V files");
        return false;
    }

    TAAVS = Device->createVertexShader(std::span<const MonsterRender::uint8>(SpvTAAVS.data(), SpvTAAVS.size()));
    TAAPS = Device->createPixelShader(std::span<const MonsterRender::uint8>(SpvTAAPS.data(), SpvTAAPS.size()));

    if (!TAAVS || !TAAPS)
    {
        MR_LOG(LogDeferredRenderer, Error, "Failed to create TAA shader objects");
        return false;
    }

    // Create TAA pipeline state
    PipelineStateDesc Desc;
    Desc.vertexShader   = TAAVS;
    Desc.pixelShader    = TAAPS;
    Desc.primitiveTopology = EPrimitiveTopology::TriangleList;

    // No vertex input (fullscreen triangle generated in shader)
    Desc.vertexLayout = {};

    // Rasterizer state
    Desc.rasterizerState.cullMode = ECullMode::None;
    Desc.rasterizerState.fillMode = EFillMode::Solid;

    // Depth/stencil disabled
    Desc.depthStencilState.depthEnable = false;
    Desc.depthStencilState.depthWriteEnable = false;
    Desc.depthStencilState.depthCompareOp   = ECompareOp::Always;

    // Blend disabled
    Desc.blendState.blendEnable = false;

    // Output to swapchain
    Desc.renderTargetFormats.push_back(Device->getSwapChainFormat());
    Desc.depthStencilFormat = EPixelFormat::Unknown;

    Desc.debugName = "TAA Pipeline";

    TAAPipeline = Device->createPipelineState(Desc);
    if (!TAAPipeline)
    {
        MR_LOG(LogDeferredRenderer, Error, "Failed to create TAA pipeline");
        return false;
    }

    MR_LOG(LogDeferredRenderer, Log, "TAA pipeline created successfully");
    return true;
}

void FDeferredRenderer::RenderTAAPass(MonsterRender::RHI::IRHICommandList* CmdList)
{
    if (!CmdList || !TAAPipeline)
    {
        MR_LOG(LogDeferredRenderer, Error, "Invalid command list or TAA pipeline");
        return;
    }

    CmdList->setPipelineState(TAAPipeline);
    
    // Bind textures
    CmdList->setShaderResource(0, LightingTarget);
    CmdList->setShaderResource(1, MotionVectorTarget);
    CmdList->setShaderResource(2, HistoryTarget);
    CmdList->setConstantBuffer(3, SceneUniformBuffer);
    
    // TODO: Set render target to swapchain backbuffer
    // CmdList->setRenderTargets(TSpan<TSharedPtr<IRHITexture>>(&backBuffer, 1), nullptr);
    
    // Draw fullscreen triangle
    CmdList->draw(3, 0);
}

void FDeferredRenderer::CopyToHistory(MonsterRender::RHI::IRHICommandList* CmdList)
{
    if (!CmdList)
    {
        MR_LOG(LogDeferredRenderer, Error, "Invalid command list for history copy");
        return;
    }

    // TODO: Implement blit from backbuffer to HistoryTarget
    // CmdList->blitTexture(Device->getCurrentBackBuffer(), HistoryTarget.Get());
}

void FDeferredRenderer::OnResize(uint32 NewWidth, uint32 NewHeight)
{
    if (NewWidth == GBuffer.Width && NewHeight == GBuffer.Height)
    {
        return;
    }

    MR_LOG(LogDeferredRenderer, Log, "Resizing renderer: %ux%u -> %ux%u", 
           GBuffer.Width, GBuffer.Height, NewWidth, NewHeight);

    // Release old resources
    ReleaseGBuffer();
    LightingTarget.Reset();
    HistoryTarget.Reset();

    // Recreate with new size
    CreateGBuffer(NewWidth, NewHeight);
    CreateTAAResources();

    // Reset frame index to avoid stale history
    FrameIndex = 0;

    MR_LOG(LogDeferredRenderer, Log, "Resize completed");
}

void FDeferredRenderer::OnSceneChanged()
{
    // Clear history buffer to avoid ghosting from previous scene
    if (Device && HistoryTarget)
    {
        // TODO: Use proper command list creation
        // MonsterRender::RHI::IRHICommandList* cmdList = FRHICommandListPool::CreateCommandList(...);
        // if (cmdList)
        // {
        //     Math::FVector4f clearColor(0.0f, 0.0f, 0.0f, 1.0f);
        //     cmdList->clearRenderTarget(HistoryTarget, clearColor);
        //     FRHICommandListExecutor::SubmitCommandListToRHIThread(cmdList);
        // }
    }

    // Reset frame index
    FrameIndex = 0;

    MR_LOG(LogDeferredRenderer, Log, "Scene changed, TAA history cleared");
}

float FDeferredRenderer::Halton(uint32 Index, uint32 Base)
{
    float result = 0.0f;
    float f = 1.0f;
    uint32 i = Index;
    
    while (i > 0)
    {
        f = f / static_cast<float>(Base);
        result = result + f * static_cast<float>(i % Base);
        i = i / Base;
    }
    
    return result;
}

Math::FVector2f FDeferredRenderer::GenerateJitter(uint32 FrameIndex)
{
    // Use 8-sample Halton sequence pattern
    uint32 sampleIndex = FrameIndex % 8;
    
    // Generate Halton sequence values (1-based indexing)
    float halton2 = Halton(sampleIndex + 1, 2);
    float halton3 = Halton(sampleIndex + 1, 3);
    
    // Convert from [0, 1] to [-0.5, 0.5] range
    Math::FVector2f jitter;
    jitter.X = halton2 - 0.5f;
    jitter.Y = halton3 - 0.5f;
    
    return jitter;
}

Math::FMatrix44f FDeferredRenderer::ApplyJitter(
    const Math::FMatrix44f& Proj,
    const Math::FVector2f& Jitter,
    uint32 Width,
    uint32 Height)
{
    Math::FMatrix44f jitteredProj = Proj;
    
    // Convert pixel offset to NDC offset
    // NDC range is [-1, 1], so pixel offset needs to be scaled by 2/width
    float ndcOffsetX = (Jitter.X * 2.0f) / static_cast<float>(Width);
    float ndcOffsetY = (Jitter.Y * 2.0f) / static_cast<float>(Height);
    
    // Apply jitter to projection matrix translation component
    // For row-major matrices: m[row][col]
    jitteredProj.M[3][0] += ndcOffsetX;
    jitteredProj.M[3][1] += ndcOffsetY;
    
    return jitteredProj;
}

// ============================================================================
// FXAA (Fast Approximate Anti-Aliasing) Implementation
// ============================================================================

bool FDeferredRenderer::CreateFXAAResources()
{
    using namespace MonsterRender::RHI;
    
    MR_LOG(LogDeferredRenderer, Log, "Creating FXAA resources...");
    
    // Create Uniform Buffer
    BufferDesc uboDesc;
    uboDesc.size = sizeof(PostProcess::FFXAAUniformBuffer);
    uboDesc.usage = EResourceUsage::UniformBuffer;
    uboDesc.cpuAccessible = true;
    uboDesc.debugName = "FXAA UBO";
    FXAAUniformBuffer = Device->createBuffer(uboDesc);
    if (!FXAAUniformBuffer)
    {
        MR_LOG(LogDeferredRenderer, Error, "Failed to create FXAA UBO");
        return false;
    }
    
    // Create Sampler (Linear + Clamp, required by FXAA algorithm)
    SamplerDesc samplerDesc;
    samplerDesc.filter = ESamplerFilter::Bilinear;
    samplerDesc.addressU = ESamplerAddressMode::Clamp;
    samplerDesc.addressV = ESamplerAddressMode::Clamp;
    samplerDesc.addressW = ESamplerAddressMode::Clamp;
    FXAASampler = Device->createSampler(samplerDesc);
    if (!FXAASampler)
    {
        MR_LOG(LogDeferredRenderer, Error, "Failed to create FXAA sampler");
        return false;
    }
    
    // Load Shaders
    auto vertCode = MonsterRender::ShaderCompiler::readFileBytes(kFXAAVsPath);
    auto fragCode = MonsterRender::ShaderCompiler::readFileBytes(kFXAAPsPath);
    
    if (vertCode.empty() || fragCode.empty())
    {
        MR_LOG(LogDeferredRenderer, Error, "Failed to load FXAA shaders");
        return false;
    }
    
    FXAAVS = Device->createVertexShader(vertCode);
    FXAAPS = Device->createPixelShader(fragCode);
    
    if (!FXAAVS || !FXAAPS)
    {
        MR_LOG(LogDeferredRenderer, Error, "Failed to create FXAA shader objects");
        return false;
    }
    
    MR_LOG(LogDeferredRenderer, Log, "FXAA resources created successfully");
    return true;
}

bool FDeferredRenderer::CreateFXAAPipeline()
{
    using namespace MonsterRender::RHI;
    
    MR_LOG(LogDeferredRenderer, Log, "Creating FXAA pipeline...");
    
    PipelineStateDesc psoDesc;
    psoDesc.vertexShader = FXAAVS;
    psoDesc.pixelShader = FXAAPS;
    psoDesc.primitiveTopology = EPrimitiveTopology::TriangleList;
    
    // No vertex input (fullscreen triangle generated in VS)
    psoDesc.vertexLayout = {};
    
    // Rasterizer: No culling
    psoDesc.rasterizerState.cullMode = ECullMode::None;
    psoDesc.rasterizerState.frontCounterClockwise = true;
    psoDesc.rasterizerState.fillMode = EFillMode::Solid;
    
    // Depth/Stencil: Disabled (post-process pass)
    psoDesc.depthStencilState.depthEnable = false;
    psoDesc.depthStencilState.depthWriteEnable = false;
    psoDesc.depthStencilState.stencilEnable = false;
    
    // Blend: Disabled (replace mode)
    psoDesc.blendState.blendEnable = false;
    
    // Render Target: Swapchain format (RGBA8)
    psoDesc.renderTargetFormats.push_back(EPixelFormat::R8G8B8A8_UNORM);
    
    FXAAPipeline = Device->createPipelineState(psoDesc);
    if (!FXAAPipeline)
    {
        MR_LOG(LogDeferredRenderer, Error, "Failed to create FXAA pipeline");
        return false;
    }
    
    MR_LOG(LogDeferredRenderer, Log, "FXAA pipeline created successfully");
    return true;
}

void FDeferredRenderer::UpdateFXAAUniformBuffer(uint32 Width, uint32 Height)
{
    PostProcess::FFXAAUniformBuffer ubo;
    ubo.RcpFrame = Math::FVector2f(1.0f / Width, 1.0f / Height);
    ubo.QualitySubpix = FXAAConfig.QualitySubpix;
    ubo.QualityEdgeThreshold = FXAAConfig.QualityEdgeThreshold;
    ubo.QualityEdgeThresholdMin = FXAAConfig.QualityEdgeThresholdMin;
    ubo.Preset = FXAAConfig.Preset;
    
    if (!WriteUniformBuffer(FXAAUniformBuffer, &ubo, sizeof(ubo)))
    {
        MR_LOG(LogDeferredRenderer, Error, "Failed to update FXAA UBO");
    }
}

void FDeferredRenderer::RenderFXAAPass(
    MonsterRender::RHI::IRHICommandList* CmdList,
    TSharedPtr<MonsterRender::RHI::IRHITexture> InputTexture)
{
    using namespace MonsterRender::RHI;
    
    if (!FXAAConfig.EnableFXAA)
    {
        return;  // FXAA disabled
    }
    
    if (!CmdList || !InputTexture)
    {
        MR_LOG(LogDeferredRenderer, Error, "RenderFXAAPass: null command list or input texture");
        return;
    }
    
    if (!FXAAPipeline || !FXAAUniformBuffer || !FXAASampler)
    {
        MR_LOG(LogDeferredRenderer, Error, "RenderFXAAPass: FXAA resources not initialized");
        return;
    }
    
    // NOTE: This should only be called when TAA is disabled
    // Caller is responsible for mutual exclusion
    
    // Set pipeline
    CmdList->setPipelineState(FXAAPipeline);
    
    // Bind resources
    CmdList->setConstantBuffer(0, FXAAUniformBuffer);
    CmdList->setShaderResource(1, InputTexture);
    CmdList->setSampler(0, FXAASampler);
    
    // Draw fullscreen triangle (3 vertices, no vertex buffer)
    CmdList->draw(3, 0);
}

void FDeferredRenderer::ReleaseFXAAResources()
{
    FXAAPipeline.Reset();
    FXAAPS.Reset();
    FXAAVS.Reset();
    FXAASampler.Reset();
    FXAAUniformBuffer.Reset();
    
    MR_LOG(LogDeferredRenderer, Log, "FXAA resources released");
}

} // namespace Deferred
} // namespace MonsterEngine
