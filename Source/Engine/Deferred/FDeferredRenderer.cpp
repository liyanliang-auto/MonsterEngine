// Copyright Monster Engine. All Rights Reserved.

/**
 * @file FDeferredRenderer.cpp
 * @brief 延迟渲染器 MVP 实现
 *
 * 参考：
 *   - FCubeSceneProxy::CreatePipelineState  (Pipeline 模板)
 *   - CubeSceneApplication::createPBRPipeline (Shader 加载模板)
 *   - CubeSceneApplication::initializeViewportRenderTarget (RT 创建模板)
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
// 常量定义
// ============================================================================

namespace
{
    /** GBuffer 各 RT 的固定格式（MVP 版本） */
    constexpr MonsterRender::RHI::EPixelFormat kNormalFormat =
        MonsterRender::RHI::EPixelFormat::R32G32B32A32_FLOAT;

    constexpr MonsterRender::RHI::EPixelFormat kAlbedoFormat =
        MonsterRender::RHI::EPixelFormat::R8G8B8A8_UNORM;

    /** 顶点步长，匹配 FCubeLitVertex（pos3 + normal3 + uv2） */
    constexpr uint32 kCubeLitVertexStride = sizeof(float) * 8;

    /** Shader SPIR-V 相对路径 */
    constexpr const char* kGeometryVsPath = "Shaders/Deferred/GeometryPass.vert.spv";
    constexpr const char* kGeometryPsPath = "Shaders/Deferred/GeometryPass.frag.spv";
    constexpr const char* kLightingVsPath = "Shaders/Deferred/LightingPass.vert.spv";
    constexpr const char* kLightingPsPath = "Shaders/Deferred/LightingPass.frag.spv";

} // anonymous namespace


// ============================================================================
// 构造 / 析构
// ============================================================================

FDeferredRenderer::FDeferredRenderer() = default;

FDeferredRenderer::~FDeferredRenderer()
{
    Shutdown();
}


// ============================================================================
// 生命周期
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

    // MVP 目前只支持 Vulkan（OpenGL 待后续扩展）
    if (InDevice->getRHIBackend() != MonsterRender::RHI::ERHIBackend::Vulkan)
    {
        MR_LOG(LogDeferredRenderer, Error,
            "Initialize: Deferred MVP only supports Vulkan, got %s",
            MonsterRender::RHI::GetRHIBackendName(InDevice->getRHIBackend()));
        return false;
    }

    Device = InDevice;

    MR_LOG(LogDeferredRenderer, Log, "=== Initializing Deferred Renderer (%ux%u) ===", InWidth, InHeight);

    // 顺序创建：Shader → UBO → Sampler → Pipeline → GBuffer
    // Shader 和 Pipeline 只创建一次；GBuffer 会随 Resize 重建
    if (!LoadShaders())            { Shutdown(); return false; }
    if (!CreateUniformBuffers())   { Shutdown(); return false; }
    if (!CreateSampler())          { Shutdown(); return false; }
    if (!CreateGeometryPipeline()) { Shutdown(); return false; }
    if (!CreateLightingPipeline()) { Shutdown(); return false; }
    if (!CreateGBuffer(InWidth, InHeight)) { Shutdown(); return false; }

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

    // 资源按创建逆序释放（TSharedPtr 自动 Release）
    ReleaseGBuffer();

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
        return true;  // 无需重建
    }

    MR_LOG(LogDeferredRenderer, Log, "Resizing GBuffer from %ux%u to %ux%u",
           GBuffer.Width, GBuffer.Height, InWidth, InHeight);

    ReleaseGBuffer();
    return CreateGBuffer(InWidth, InHeight);
}


// ============================================================================
// Uniform Buffer 更新
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
    // row-vector 下，法线变换为  worldNormal = normal * mat3(NormalMatrix)
    Data.NormalMatrix  = Model.Inverse().GetTransposed();

    Data.CameraPos     = CameraPosition;

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

    WriteUniformBuffer(SceneUniformBuffer, &Data, sizeof(Data));
}


// ============================================================================
// 内部构建步骤
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
    // 使用 ShaderResource 标志，以便 Lighting Pass 采样重建 Position
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

    GBuffer.Width  = InWidth;
    GBuffer.Height = InHeight;

    MR_LOG(LogDeferredRenderer, Log,
           "GBuffer created: Normal=RGBA32F, Albedo=RGBA8, Depth=device-default (%ux%u)",
           InWidth, InHeight);
    return true;
}

void FDeferredRenderer::ReleaseGBuffer()
{
    GBuffer.NormalTarget.Reset();
    GBuffer.AlbedoTarget.Reset();
    GBuffer.DepthTarget.Reset();
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
    Desc.filter         = ESamplerFilter::Bilinear;    // GBuffer 采样用 Linear 即可，无 mip
    Desc.addressU       = ESamplerAddressMode::Clamp;  // 防止越界采样到 wrap 边缘
    Desc.addressV       = ESamplerAddressMode::Clamp;
    Desc.addressW       = ESamplerAddressMode::Clamp;
    Desc.maxAnisotropy  = 1;                            // 全屏采样无需各向异性
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

    // ---- Vertex layout (匹配 FCubeLitVertex: pos3 + normal3 + uv2) ----
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

    // ---- Rasterizer (与 Cube pipeline 保持一致：viewport Y-flip 后 CW 为 front) ----
    Desc.rasterizerState.fillMode                = EFillMode::Solid;
    Desc.rasterizerState.cullMode                = ECullMode::Back;
    Desc.rasterizerState.frontCounterClockwise   = false;

    // ---- Depth (开启测试与写入) ----
    Desc.depthStencilState.depthEnable      = true;
    Desc.depthStencilState.depthWriteEnable = true;
    Desc.depthStencilState.depthCompareOp   = ECompareOp::Less;

    // ---- Blend (不启用，GBuffer 不做混合) ----
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

    // ---- Vertex layout 空（LightingPass.vert 用 gl_VertexIndex 生成全屏三角形） ----
    Desc.vertexLayout.stride = 0;
    // Desc.vertexLayout.attributes 保持默认空

    // ---- Rasterizer ----
    //   全屏三角形超出 NDC 的顶点会被硬件裁剪。关闭背面剔除避免方向问题
    Desc.rasterizerState.fillMode              = EFillMode::Solid;
    Desc.rasterizerState.cullMode              = ECullMode::None;
    Desc.rasterizerState.frontCounterClockwise = false;

    // ---- Depth 全部关闭（光照合成不需要也不应写深度） ----
    Desc.depthStencilState.depthEnable      = false;
    Desc.depthStencilState.depthWriteEnable = false;
    Desc.depthStencilState.depthCompareOp   = ECompareOp::Always;

    // ---- Blend 关闭（直接覆盖写入最终颜色） ----
    Desc.blendState.blendEnable = false;

    // ---- 输出：目标是当前 viewport color（与 Cube 相同） ----
    Desc.renderTargetFormats.push_back(Device->getSwapChainFormat());
    Desc.depthStencilFormat = EPixelFormat::Unknown;  // 不使用深度附件

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

} // namespace Deferred
} // namespace MonsterEngine
