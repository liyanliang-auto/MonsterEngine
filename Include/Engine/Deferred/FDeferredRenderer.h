// Copyright Monster Engine. All Rights Reserved.

#pragma once

/**
 * @file FDeferredRenderer.h
 * @brief 延迟渲染器（MVP 版本）
 *
 * 本类封装 Deferred Rendering MVP 所需的全部 GPU 资源与管线：
 *   - GBuffer（Normal RT + Albedo RT + Depth），自动跟随窗口尺寸
 *   - 两个 Pipeline：GeometryPipeline（MRT 写 GBuffer）、LightingPipeline（全屏光照合成）
 *   - 两个 Shader 对（4 个 IRHIShader）
 *   - 两个 Uniform Buffer（TransformUBO、SceneUBO）
 *   - 一个通用采样器（Linear + Clamp，用于采样 GBuffer）
 *
 * 职责边界：
 *   - 本类只负责"资源创建 + Uniform 更新"
 *   - RDG Pass 编排、Draw Call 调度由 CubeSceneApplication::renderWithDeferred() 负责
 *
 * 参考：
 *   - FCubeSceneProxy（Forward 路径，资源创建模板）
 *   - Shaders/Deferred/*.{vert,frag}（GLSL 对照）
 *   - devDocument/DeferredRenderingDesign.md（完整设计文档）
 */

#include "Core/Templates/SharedPointer.h"
#include "Engine/Deferred/DeferredUniformTypes.h"
#include "Math/MathFwd.h"
#include "Math/Vector2D.h"
#include "RHI/RHI.h"

namespace MonsterEngine
{
namespace Deferred
{

/**
 * GBuffer 的三张纹理
 *
 * MVP 布局：
 *   NormalTarget  : RGBA32F  世界空间法线（w 保留）
 *   AlbedoTarget  : RGBA8    基础颜色（a 保留）
 *   DepthTarget   : D24S8    深度（兼作 Position 重建）
 */
struct FDeferredGBuffer
{
    TSharedPtr<MonsterRender::RHI::IRHITexture> NormalTarget;
    TSharedPtr<MonsterRender::RHI::IRHITexture> AlbedoTarget;
    TSharedPtr<MonsterRender::RHI::IRHITexture> DepthTarget;

    /** 当前 GBuffer 的分辨率 */
    uint32 Width  = 0;
    uint32 Height = 0;

    /** 是否已成功分配所有 RT */
    bool IsValid() const
    {
        return NormalTarget && AlbedoTarget && DepthTarget
            && Width > 0 && Height > 0;
    }
};

/**
 * 延迟渲染器主类
 *
 * 用法：
 *   FDeferredRenderer Renderer;
 *   Renderer.Initialize(Device, 1920, 1080);
 *   // 每帧：
 *   Renderer.UpdateTransformUBO(model, view, proj, cameraPos);
 *   Renderer.UpdateSceneUBO(...);
 *   // 在 RDG Pass 里使用 GetXxx() 拿资源
 *   Renderer.Shutdown();
 */
class FDeferredRenderer
{
public:
    FDeferredRenderer();
    ~FDeferredRenderer();

    // 禁止拷贝 / 移动（持有大量 GPU 资源）
    FDeferredRenderer(const FDeferredRenderer&) = delete;
    FDeferredRenderer& operator=(const FDeferredRenderer&) = delete;
    FDeferredRenderer(FDeferredRenderer&&) = delete;
    FDeferredRenderer& operator=(FDeferredRenderer&&) = delete;

    // ========================================================================
    // 生命周期
    // ========================================================================

    /**
     * 初始化所有 GPU 资源
     * @param InDevice   RHI 设备（由调用方持有，本类只是引用）
     * @param InWidth    GBuffer 初始宽度
     * @param InHeight   GBuffer 初始高度
     * @return 是否成功
     */
    bool Initialize(
        MonsterRender::RHI::IRHIDevice* InDevice,
        uint32 InWidth,
        uint32 InHeight);

    /** 释放所有资源 */
    void Shutdown();

    /**
     * GBuffer 尺寸变化时调用（如窗口 resize）
     * 只重建 GBuffer 纹理，Pipeline / Shader / UBO 保持不变
     */
    bool Resize(uint32 InWidth, uint32 InHeight);

    /** 是否已完成初始化 */
    bool IsInitialized() const { return bInitialized; }

    // ========================================================================
    // Uniform Buffer 更新
    // ========================================================================

    /**
     * 更新 TransformUBO（对应 GeometryPass.vert）
     * @param Model          模型矩阵（Local → World）
     * @param View           视图矩阵（World → View）
     * @param Proj           投影矩阵（View → Clip）
     * @param CameraPosition 相机世界坐标
     *
     * 注意：内部会自动计算 NormalMatrix = inverse-transpose(Model)
     */
    void UpdateTransformUBO(
        const Math::FMatrix44f& Model,
        const Math::FMatrix44f& View,
        const Math::FMatrix44f& Proj,
        const Math::FVector4f&  CameraPosition);

    /**
     * 更新 SceneUBO（对应 LightingPass.frag）
     * 全部参数以世界空间表达。
     * row-vector 约定下：InvViewProj = inverse(View * Proj)
     */
    void UpdateSceneUBO(
        const Math::FMatrix44f& InvViewProj,
        const Math::FVector4f&  CameraPosition,
        const Math::FVector4f&  DirLightDirection,         // xyz = dir, w = 0
        const Math::FVector4f&  DirLightColorIntensity,    // xyz = color, w = intensity
        const Math::FVector4f&  PointLightPositionRadius,  // xyz = pos, w = radius
        const Math::FVector4f&  PointLightColorIntensity,  // xyz = color, w = intensity
        float AmbientFactor);

    // ========================================================================
    // 资源访问（供 RDG Pass / CommandList 使用）
    // ========================================================================

    const FDeferredGBuffer& GetGBuffer() const { return GBuffer; }

    TSharedPtr<MonsterRender::RHI::IRHIPipelineState> GetGeometryPipeline() const { return GeometryPipeline; }
    TSharedPtr<MonsterRender::RHI::IRHIPipelineState> GetLightingPipeline() const { return LightingPipeline; }

    TSharedPtr<MonsterRender::RHI::IRHIBuffer> GetTransformUBO() const { return TransformUniformBuffer; }
    TSharedPtr<MonsterRender::RHI::IRHIBuffer> GetSceneUBO()     const { return SceneUniformBuffer; }

    TSharedPtr<MonsterRender::RHI::IRHISampler> GetGBufferSampler() const { return GBufferSampler; }

    // TAA resource accessors
    TSharedPtr<MonsterRender::RHI::IRHITexture> GetMotionVectorTarget() const { return MotionVectorTarget; }
    TSharedPtr<MonsterRender::RHI::IRHITexture> GetLightingTarget() const { return LightingTarget; }
    TSharedPtr<MonsterRender::RHI::IRHITexture> GetHistoryTarget() const { return HistoryTarget; }
    TSharedPtr<MonsterRender::RHI::IRHIPipelineState> GetTAAPipeline() const { return TAAPipeline; }

    // ========================================================================
    // TAA (Temporal Anti-Aliasing) Methods
    // ========================================================================

    /**
     * Create TAA-specific resources (Motion Vector RT, Lighting RT, History RT)
     * @return true if all resources created successfully
     */
    bool CreateTAAResources();

    /**
     * Create TAA pipeline for temporal reprojection and filtering
     * @return true if pipeline created successfully
     */
    bool CreateTAAPipeline();

    /**
     * Render TAA pass (temporal reprojection + variance clipping + optional sharpening)
     * @param CmdList Command list to record draw commands
     */
    void RenderTAAPass(MonsterRender::RHI::IRHICommandList* CmdList);

    /**
     * Copy current frame result to history buffer for next frame's TAA
     * @param CmdList Command list to record copy command
     */
    void CopyToHistory(MonsterRender::RHI::IRHICommandList* CmdList);

    /**
     * Generate Halton sequence value for jitter pattern
     * @param Index Sequence index (1-based)
     * @param Base Prime number base (2 or 3 for 2D jitter)
     * @return Halton sequence value in [0, 1)
     */
    float Halton(uint32 Index, uint32 Base);

    /**
     * Generate 2D jitter offset using Halton sequence (8-sample pattern)
     * @param FrameIndex Current frame index
     * @return Jitter offset in [-0.5, 0.5] range
     */
    Math::FVector2f GenerateJitter(uint32 FrameIndex);

    /**
     * Apply jitter offset to projection matrix
     * @param Proj Original projection matrix
     * @param Jitter Jitter offset in pixel space
     * @param Width Viewport width
     * @param Height Viewport height
     * @return Jittered projection matrix
     */
    Math::FMatrix44f ApplyJitter(
        const Math::FMatrix44f& Proj,
        const Math::FVector2f& Jitter,
        uint32 Width,
        uint32 Height);

    /**
     * Handle viewport resize (recreate TAA resources)
     * @param NewWidth New viewport width
     * @param NewHeight New viewport height
     */
    void OnResize(uint32 NewWidth, uint32 NewHeight);

    /**
     * Handle scene change (clear history buffer)
     */
    void OnSceneChanged();

protected:
    // ========================================================================
    // 内部构建步骤（失败返回 false）
    // ========================================================================

    /** 创建 GBuffer 三张 RT */
    bool CreateGBuffer(uint32 InWidth, uint32 InHeight);

    /** 释放 GBuffer（保留其他资源） */
    void ReleaseGBuffer();

    /** 加载 4 个 SPIR-V Shader 并创建 IRHIShader */
    bool LoadShaders();

    /** 创建 Uniform Buffer（TransformUBO + SceneUBO） */
    bool CreateUniformBuffers();

    /** 创建通用采样器（Linear + Clamp） */
    bool CreateSampler();

    /** 创建 Geometry Pass 的 MRT Pipeline */
    bool CreateGeometryPipeline();

    /** 创建 Lighting Pass 的全屏合成 Pipeline */
    bool CreateLightingPipeline();

    /**
     * 写入 uniform buffer 的通用辅助
     * @return 是否成功 map/copy/unmap
     */
    bool WriteUniformBuffer(
        const TSharedPtr<MonsterRender::RHI::IRHIBuffer>& Buffer,
        const void* Data,
        uint32 Size);

protected:
    /** RHI 设备（不持有，外部 owner） */
    MonsterRender::RHI::IRHIDevice* Device = nullptr;

    /** GBuffer 纹理集合 */
    FDeferredGBuffer GBuffer;

    /** Shader 对象 */
    TSharedPtr<MonsterRender::RHI::IRHIVertexShader> GeometryVS;
    TSharedPtr<MonsterRender::RHI::IRHIPixelShader>  GeometryPS;
    TSharedPtr<MonsterRender::RHI::IRHIVertexShader> LightingVS;
    TSharedPtr<MonsterRender::RHI::IRHIPixelShader>  LightingPS;

    /** Pipeline 对象 */
    TSharedPtr<MonsterRender::RHI::IRHIPipelineState> GeometryPipeline;
    TSharedPtr<MonsterRender::RHI::IRHIPipelineState> LightingPipeline;

    /** Uniform Buffer */
    TSharedPtr<MonsterRender::RHI::IRHIBuffer> TransformUniformBuffer;
    TSharedPtr<MonsterRender::RHI::IRHIBuffer> SceneUniformBuffer;

    /** 采样 GBuffer 的通用采样器 */
    TSharedPtr<MonsterRender::RHI::IRHISampler> GBufferSampler;

    /** 是否完成初始化 */
    bool bInitialized = false;

    // ========================================================================
    // TAA (Temporal Anti-Aliasing) Resources
    // ========================================================================

    /** TAA textures */
    TSharedPtr<MonsterRender::RHI::IRHITexture> MotionVectorTarget;  // Motion Vector RT (RG16F)
    TSharedPtr<MonsterRender::RHI::IRHITexture> LightingTarget;      // Lighting RT (RGBA8, temp)
    TSharedPtr<MonsterRender::RHI::IRHITexture> HistoryTarget;       // History RT (RGBA8)

    /** TAA pipeline */
    TSharedPtr<MonsterRender::RHI::IRHIPipelineState> TAAPipeline;

    /** TAA shaders */
    TSharedPtr<MonsterRender::RHI::IRHIVertexShader> TAAVS;
    TSharedPtr<MonsterRender::RHI::IRHIPixelShader>  TAAPS;

    /** TAA state */
    uint32 FrameIndex = 0;
    Math::FVector2f CurrentJitter;
    Math::FVector2f PreviousJitter;
    Math::FMatrix44f PreviousViewProj;

    /** TAA configuration */
    struct FTAAConfig
    {
        bool EnableTAA = true;
        bool EnableSharpening = false;
        float BlendFactor = 0.1f;
        float Sharpness = 0.3f;
    } TAAConfig;
};

} // namespace Deferred
} // namespace MonsterEngine
