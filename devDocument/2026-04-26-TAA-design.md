# TAA (Temporal Anti-Aliasing) 功能设计文档

> **项目**: MonsterEngine  
> **功能**: TAA (Temporal Anti-Aliasing) 完整实现  
> **作者**: 李彦亮  
> **日期**: 2026-04-26  
> **版本**: 1.0

---

## 📋 文档摘要

本文档描述了在 MonsterEngine 延迟渲染管线中实现 TAA（时间抗锯齿）的完整设计方案。设计参考 UE5 的实现，包含 6 个核心模块：Jitter Camera Projection、Motion Vector、History Buffer、Temporal Reprojection、History Rejection (Variance Clipping)、Sharpening。

**预期效果**：
- 抗锯齿质量：等效 8x-16x MSAA
- 显存开销：~24 MB (1080p)
- 性能开销：~1-2 ms (1080p)

---

## 目录

1. [需求概述](#1-需求概述)
2. [架构设计](#2-架构设计)
3. [数据结构设计](#3-数据结构设计)
4. [核心算法设计](#4-核心算法设计)
5. [实现细节](#5-实现细节)
6. [测试策略](#6-测试策略)
7. [性能分析](#7-性能分析)
8. [风险与应对](#8-风险与应对)
9. [开发计划](#9-开发计划)

---

## 1. 需求概述

### 1.1 功能描述

在 MonsterEngine 的延迟渲染管线中实现 TAA（时间抗锯齿）功能，通过混合多帧结果实现高质量抗锯齿效果，替代传统的 MSAA 方案。

### 1.2 核心场景

- **延迟渲染模式**：在 `--deferred` 模式下自动启用 TAA
- **实时渲染**：每帧自动应用 TAA，无需用户干预
- **动态场景**：支持相机移动和物体运动的场景

### 1.3 技术需求

**完整实现（6 个核心模块）**：

1. **Jitter Camera Projection**：每帧对投影矩阵添加亚像素偏移（Halton 序列）
2. **Motion Vector**：新增 GBuffer 通道存储运动矢量（Per-Object）
3. **History Buffer**：存储上一帧的渲染结果
4. **Temporal Reprojection**：使用 Motion Vector 重投影历史帧
5. **History Rejection**：Variance Clipping 检测不可靠的历史数据
6. **Sharpening**：可选的 Unsharp Mask 锐化，补偿 TAA 模糊

### 1.4 设计决策

| 决策点 | 选择方案 | 理由 |
|--------|----------|------|
| 实现范围 | 完整实现（6 个模块） | 生产级质量，符合 UE5 标准 |
| Motion Vector | Per-Object | 支持物体运动，为未来扩展打基础 |
| Jitter 模式 | Halton 序列 | 低差异序列，质量最高 |
| History Rejection | Variance Clipping | 质量优于 Color Clamping |
| GBuffer 扩展 | 独立 Motion Vector RT (RG16F) | 精度可控，架构清晰 |
| TAA Pass 位置 | 集成到 FDeferredRenderer | 架构清晰，易于调试 |
| Sharpening | 可选 Unsharp Mask | 灵活，默认关闭 |
| UBO 扩展 | 混合方案（扩展 TransformUBO + SceneUBO） | 数据分布合理，无冗余 |

---

## 2. 架构设计

### 2.1 整体渲染流程

```
┌─────────────────────────────────────────────────────────────┐
│                    FDeferredRenderer                        │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  Pass 1: Geometry Pass                                     │
│  ├─ 输入: Scene Objects, Jittered Projection Matrix        │
│  ├─ 输出: GBuffer (4 张纹理)                               │
│  │   ├─ Normal RT (RGBA32F)                                │
│  │   ├─ Albedo RT (RGBA8)                                  │
│  │   ├─ Motion Vector RT (RG16F) ← 新增                    │
│  │   └─ Depth RT (D24S8)                                   │
│  └─ Shader: GeometryPass.vert/frag (修改)                  │
│                                                             │
│  Pass 2: Lighting Pass                                     │
│  ├─ 输入: GBuffer (读取 Normal, Albedo, Depth)             │
│  ├─ 输出: Lighting RT (RGBA8, 临时) ← 新增                 │
│  └─ Shader: LightingPass.vert/frag (无需修改)              │
│                                                             │
│  Pass 3: TAA Pass ← 新增                                   │
│  ├─ 输入:                                                  │
│  │   ├─ Lighting RT (当前帧)                              │
│  │   ├─ Motion Vector RT                                  │
│  │   ├─ History RT (上一帧结果)                           │
│  │   └─ SceneUBO (Jitter, Params)                         │
│  ├─ 输出: Swapchain (最终画面)                            │
│  └─ Shader: TAAPass.vert/frag (新增)                       │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### 2.2 核心模块

```cpp
class FDeferredRenderer {
private:
    // ========== 现有成员 ==========
    TSharedPtr<IRHITexture> m_normalTarget;
    TSharedPtr<IRHITexture> m_albedoTarget;
    TSharedPtr<IRHITexture> m_depthTarget;
    TSharedPtr<IRHIPipelineState> m_geometryPipeline;
    TSharedPtr<IRHIPipelineState> m_lightingPipeline;
    
    // ========== TAA 新增成员 ==========
    // Textures
    TSharedPtr<IRHITexture> m_motionVectorTarget;  // Motion Vector RT (RG16F)
    TSharedPtr<IRHITexture> m_lightingTarget;      // Lighting 临时 RT (RGBA8)
    TSharedPtr<IRHITexture> m_historyTarget;       // History RT (RGBA8)
    
    // Pipeline
    TSharedPtr<IRHIPipelineState> m_taaPipeline;   // TAA Pipeline
    
    // State
    uint32 m_frameIndex;                           // 帧计数器
    FVector2f m_currentJitter;                     // 当前帧 Jitter
    FVector2f m_previousJitter;                    // 上一帧 Jitter
    FMatrix44f m_previousViewProj;                 // 上一帧 VP 矩阵
    
    // Configuration
    struct FTAAConfig {
        bool enableTAA = true;
        bool enableSharpening = false;
        float blendFactor = 0.1f;      // 当前帧权重 (0.0 - 1.0)
        float sharpness = 0.3f;        // 锐化强度 (0.0 - 1.0)
    } m_taaConfig;
    
public:
    // ========== TAA 新增方法 ==========
    bool CreateTAAResources();
    bool CreateTAAPipeline();
    void RenderTAAPass(IRHICommandList* cmdList);
    void CopyToHistory(IRHICommandList* cmdList);
    
    // Jitter generation
    float Halton(uint32 index, uint32 base);
    FVector2f GenerateJitter(uint32 frameIndex);
    FMatrix44f ApplyJitter(const FMatrix44f& proj, 
                           const FVector2f& jitter,
                           uint32 width, uint32 height);
    
    // Lifecycle
    void OnResize(uint32 newWidth, uint32 newHeight);
    void OnSceneChanged();
};
```

### 2.3 数据流

```
Frame N:
  1. Generate Jitter(N) → Apply to Projection Matrix
  2. Geometry Pass → GBuffer (含 Motion Vector)
  3. Lighting Pass → Lighting RT
  4. TAA Pass:
     - Sample Lighting RT (current)
     - Sample Motion Vector RT
     - Reproject History RT using Motion Vector
     - Variance Clipping (history rejection)
     - Temporal Blend: mix(history, current, 0.1)
     - Optional Sharpening
     - Output to Swapchain
  5. Copy Swapchain → History RT (for Frame N+1)
  6. Store Jitter(N) → Previous Jitter
  7. Store ViewProj(N) → Previous ViewProj

Frame N+1:
  - Use Previous Jitter, Previous ViewProj from Frame N
  - Repeat above steps
```

---

## 3. 数据结构设计

### 3.1 UBO 扩展

#### TransformUBO（每物体更新）

```cpp
// 文件: Include/Engine/Deferred/DeferredUniformTypes.h

struct FDeferredTransformUBO {
    FMatrix44f Model;                // offset 0,   size 64
    FMatrix44f View;                 // offset 64,  size 64
    FMatrix44f Proj;                 // offset 128, size 64
    FMatrix44f NormalMatrix;         // offset 192, size 64
    FVector4f  CameraPos;            // offset 256, size 16
    
    // ========== TAA 新增 ==========
    FMatrix44f PreviousModel;        // offset 272, size 64
    FVector4f  Padding;              // offset 336, size 16 (对齐)
};
// 总大小：352 bytes

static_assert(sizeof(FDeferredTransformUBO) == 352, 
    "TransformUBO size mismatch");
static_assert(offsetof(FDeferredTransformUBO, PreviousModel) == 272,
    "PreviousModel offset mismatch");
```

**GLSL 对应**：
```glsl
layout(set = 0, binding = 0) uniform TransformUBO {
    mat4 model;                 // offset 0
    mat4 view;                  // offset 64
    mat4 proj;                  // offset 128
    mat4 normalMatrix;          // offset 192
    vec4 cameraPos;             // offset 256
    mat4 previousModel;         // offset 272 (新增)
    vec4 padding;               // offset 336
} ubo;
```

#### SceneUBO（每帧更新）

```cpp
struct FDeferredSceneUBO {
    FMatrix44f InvViewProj;                 // offset 0,   size 64
    FVector4f  CameraPos;                   // offset 64,  size 16
    FVector4f  DirLightDirection;           // offset 80,  size 16
    FVector4f  DirLightColorIntensity;      // offset 96,  size 16
    FVector4f  PointLightPositionRadius;    // offset 112, size 16
    FVector4f  PointLightColorIntensity;    // offset 128, size 16
    FVector4f  Ambient;                     // offset 144, size 16
    
    // ========== TAA 新增 ==========
    FMatrix44f PreviousViewProj;            // offset 160, size 64
    FVector4f  JitterOffset;                // offset 224, size 16
                                            // xy = 当前 Jitter, zw = 上一帧 Jitter
    FVector4f  TAAParams;                   // offset 240, size 16
                                            // x = blendFactor, y = sharpness
                                            // z = enableSharpening, w = 保留
};
// 总大小：256 bytes

static_assert(sizeof(FDeferredSceneUBO) == 256, 
    "SceneUBO size mismatch");
static_assert(offsetof(FDeferredSceneUBO, PreviousViewProj) == 160,
    "PreviousViewProj offset mismatch");
static_assert(offsetof(FDeferredSceneUBO, JitterOffset) == 224,
    "JitterOffset offset mismatch");
static_assert(offsetof(FDeferredSceneUBO, TAAParams) == 240,
    "TAAParams offset mismatch");
```

**GLSL 对应**：
```glsl
layout(set = 0, binding = 3) uniform SceneUBO {
    mat4 invViewProj;           // offset 0
    vec4 cameraPos;             // offset 64
    vec4 dirLightDirection;     // offset 80
    vec4 dirLightColorIntensity;// offset 96
    vec4 pointLightPositionRadius;   // offset 112
    vec4 pointLightColorIntensity;   // offset 128
    vec4 ambient;               // offset 144
    mat4 previousViewProj;      // offset 160 (新增)
    vec4 jitterOffset;          // offset 224 (新增)
    vec4 taaParams;             // offset 240 (新增)
} scene;
```

### 3.2 GBuffer 扩展

```cpp
// 文件: Include/Engine/Deferred/FDeferredRenderer.h

struct FGBufferTextures {
    TSharedPtr<IRHITexture> NormalTarget;       // RGBA32F
    TSharedPtr<IRHITexture> AlbedoTarget;       // RGBA8
    TSharedPtr<IRHITexture> MotionVectorTarget; // RG16F (新增)
    TSharedPtr<IRHITexture> DepthTarget;        // D24S8
};
```

**显存消耗（1920×1080）**：
| 纹理 | 格式 | 大小 |
|------|------|------|
| Normal | RGBA32F | 1920 × 1080 × 16 = 33 MB |
| Albedo | RGBA8 | 1920 × 1080 × 4 = 8.3 MB |
| **Motion Vector** | **RG16F** | **1920 × 1080 × 4 = 8.3 MB** ← 新增 |
| Depth | D24S8 | 1920 × 1080 × 4 = 8.3 MB |
| **Lighting RT** | **RGBA8** | **1920 × 1080 × 4 = 8.3 MB** ← 新增 |
| **History RT** | **RGBA8** | **1920 × 1080 × 4 = 8.3 MB** ← 新增 |
| **总计** | - | **~75 MB** (原 50 MB + 25 MB) |

---

## 4. 核心算法设计

### 4.1 Halton 序列生成

```cpp
/**
 * Generate Halton sequence for TAA jitter
 * Halton(2, 3) 是一种低差异序列，分布均匀
 * 
 * @param index Frame index (1-based)
 * @param base Base number (2 or 3)
 * @return Halton value in [0, 1]
 */
float FDeferredRenderer::Halton(uint32 index, uint32 base) {
    float result = 0.0f;
    float f = 1.0f;
    uint32 i = index;
    
    while (i > 0) {
        f = f / base;
        result = result + f * (i % base);
        i = i / base;
    }
    
    return result;
}

/**
 * Generate jitter offset for current frame
 * Uses Halton(2, 3) sequence with 8-sample pattern
 * 
 * @param frameIndex Current frame index (0-based)
 * @return Jitter offset in [-0.5, 0.5] range (in pixels)
 */
FVector2f FDeferredRenderer::GenerateJitter(uint32 frameIndex) {
    // Use 8-sample Halton sequence
    uint32 sampleIndex = frameIndex % 8;
    
    float halton2 = Halton(sampleIndex + 1, 2);  // Base 2
    float halton3 = Halton(sampleIndex + 1, 3);  // Base 3
    
    // Map from [0, 1] to [-0.5, 0.5]
    FVector2f jitter;
    jitter.x = halton2 - 0.5f;
    jitter.y = halton3 - 0.5f;
    
    return jitter;
}
```

**Halton(2, 3) 8-sample pattern**：
```
Frame 0: Halton(1, 2)=0.5,   Halton(1, 3)=0.333 → Jitter(0.0, -0.167)
Frame 1: Halton(2, 2)=0.25,  Halton(2, 3)=0.667 → Jitter(-0.25, 0.167)
Frame 2: Halton(3, 2)=0.75,  Halton(3, 3)=0.111 → Jitter(0.25, -0.389)
Frame 3: Halton(4, 2)=0.125, Halton(4, 3)=0.444 → Jitter(-0.375, -0.056)
Frame 4: Halton(5, 2)=0.625, Halton(5, 3)=0.778 → Jitter(0.125, 0.278)
Frame 5: Halton(6, 2)=0.375, Halton(6, 3)=0.222 → Jitter(-0.125, -0.278)
Frame 6: Halton(7, 2)=0.875, Halton(7, 3)=0.556 → Jitter(0.375, 0.056)
Frame 7: Halton(8, 2)=0.0625,Halton(8, 3)=0.889 → Jitter(-0.4375, 0.389)
```

### 4.2 Jittered Projection Matrix

```cpp
/**
 * Apply jitter to projection matrix
 * 
 * @param proj Original projection matrix (row-major)
 * @param jitter Jitter offset in pixels
 * @param width Render target width
 * @param height Render target height
 * @return Jittered projection matrix
 */
FMatrix44f FDeferredRenderer::ApplyJitter(
    const FMatrix44f& proj,
    const FVector2f& jitter,
    uint32 width,
    uint32 height)
{
    FMatrix44f jitteredProj = proj;
    
    // Convert pixel offset to NDC space
    // NDC range is [-1, 1], so pixel offset needs to be scaled by 2
    float ndcOffsetX = (jitter.x * 2.0f) / width;
    float ndcOffsetY = (jitter.y * 2.0f) / height;
    
    // Apply offset to projection matrix
    // For row-major matrix (UE5 convention):
    // proj[3][0] += ndcOffsetX (translation in X)
    // proj[3][1] += ndcOffsetY (translation in Y)
    jitteredProj.m[3][0] += ndcOffsetX;
    jitteredProj.m[3][1] += ndcOffsetY;
    
    return jitteredProj;
}
```

### 4.3 Motion Vector 计算

#### Geometry Pass Vertex Shader

```glsl
// 文件: Shaders/Deferred/GeometryPass.vert

#version 450 core

layout(set = 0, binding = 0) uniform TransformUBO {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 normalMatrix;
    vec4 cameraPos;
    mat4 previousModel;  // 新增
    vec4 padding;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vWorldNormal;
layout(location = 2) out vec2 vTexCoord;
layout(location = 3) out vec2 vMotionVector;  // 新增

void main() {
    // Current frame transformation
    vec4 worldPos = vec4(inPosition, 1.0) * ubo.model;
    vec4 viewPos  = worldPos * ubo.view;
    vec4 clipPos  = viewPos * ubo.proj;
    
    // Previous frame transformation
    mat4 previousMVP = ubo.previousModel * ubo.view * ubo.proj;
    vec4 previousClipPos = vec4(inPosition, 1.0) * previousMVP;
    
    // Calculate motion vector in NDC space
    vec2 currentNDC = clipPos.xy / clipPos.w;
    vec2 previousNDC = previousClipPos.xy / previousClipPos.w;
    vMotionVector = currentNDC - previousNDC;
    
    // Output to fragment shader
    vWorldPos = worldPos.xyz;
    vWorldNormal = normalize(inNormal * mat3(ubo.normalMatrix));
    vTexCoord = inTexCoord;
    
    gl_Position = clipPos;
}
```

#### Geometry Pass Fragment Shader

```glsl
// 文件: Shaders/Deferred/GeometryPass.frag

#version 450 core

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vWorldNormal;
layout(location = 2) in vec2 vTexCoord;
layout(location = 3) in vec2 vMotionVector;  // 新增

layout(set = 0, binding = 1) uniform sampler2D albedoMap;

layout(location = 0) out vec4 outNormal;
layout(location = 1) out vec4 outAlbedo;
layout(location = 2) out vec2 outMotionVector;  // 新增

void main() {
    // Output Normal
    outNormal = vec4(normalize(vWorldNormal), 0.0);
    
    // Output Albedo
    vec4 albedo = texture(albedoMap, vTexCoord);
    outAlbedo = vec4(albedo.rgb, 1.0);
    
    // Output Motion Vector
    outMotionVector = vMotionVector;
}
```

### 4.4 TAA Pass 完整算法

```glsl
// 文件: Shaders/Deferred/TAAPass.frag

#version 450 core

// ============================================================================
// Deferred Rendering - TAA Pass Fragment Shader
// Temporal Anti-Aliasing with Variance Clipping and Optional Sharpening
// ============================================================================

layout(location = 0) in vec2 vScreenUV;

// Input textures
layout(set = 0, binding = 0) uniform sampler2D currentColor;    // Lighting RT
layout(set = 0, binding = 1) uniform sampler2D motionVector;    // Motion Vector RT
layout(set = 0, binding = 2) uniform sampler2D historyColor;    // History RT

// Scene UBO (contains TAA params)
layout(set = 0, binding = 3) uniform SceneUBO {
    mat4 invViewProj;
    vec4 cameraPos;
    vec4 dirLightDirection;
    vec4 dirLightColorIntensity;
    vec4 pointLightPositionRadius;
    vec4 pointLightColorIntensity;
    vec4 ambient;
    mat4 previousViewProj;
    vec4 jitterOffset;      // xy = current, zw = previous
    vec4 taaParams;         // x = blendFactor, y = sharpness, z = enableSharpening
} scene;

layout(location = 0) out vec4 outColor;

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Sample texture with catmull-rom filtering (5-tap)
 * Better quality than bilinear for history sampling
 */
vec3 sampleCatmullRom(sampler2D tex, vec2 uv, vec2 texelSize) {
    vec2 position = uv / texelSize;
    vec2 centerPosition = floor(position - 0.5) + 0.5;
    vec2 f = position - centerPosition;
    vec2 f2 = f * f;
    vec2 f3 = f2 * f;
    
    vec2 w0 = -0.5 * f3 + f2 - 0.5 * f;
    vec2 w1 = 1.5 * f3 - 2.5 * f2 + 1.0;
    vec2 w2 = -1.5 * f3 + 2.0 * f2 + 0.5 * f;
    vec2 w3 = 0.5 * f3 - 0.5 * f2;
    
    vec2 s0 = w0 + w1;
    vec2 s1 = w2 + w3;
    vec2 f0 = w1 / s0;
    vec2 f1 = w3 / s1;
    
    vec2 t0 = centerPosition - 1.0 + f0;
    vec2 t1 = centerPosition + 1.0 + f1;
    
    return (texture(tex, t0 * texelSize).rgb * s0.x +
            texture(tex, t1 * texelSize).rgb * s1.x) * s0.y +
           (texture(tex, vec2(t0.x, t1.y) * texelSize).rgb * s0.x +
            texture(tex, vec2(t1.x, t1.y) * texelSize).rgb * s1.x) * s1.y;
}

/**
 * Variance clipping for history rejection
 * Computes color variance in 3x3 neighborhood and clamps history
 */
vec3 clipHistory(vec3 historyColor, vec3 currentColor, vec2 uv, vec2 texelSize) {
    // Sample 3x3 neighborhood
    vec3 c0 = texture(currentColor, uv + vec2(-1, -1) * texelSize).rgb;
    vec3 c1 = texture(currentColor, uv + vec2( 0, -1) * texelSize).rgb;
    vec3 c2 = texture(currentColor, uv + vec2( 1, -1) * texelSize).rgb;
    vec3 c3 = texture(currentColor, uv + vec2(-1,  0) * texelSize).rgb;
    vec3 c4 = currentColor;  // Center
    vec3 c5 = texture(currentColor, uv + vec2( 1,  0) * texelSize).rgb;
    vec3 c6 = texture(currentColor, uv + vec2(-1,  1) * texelSize).rgb;
    vec3 c7 = texture(currentColor, uv + vec2( 0,  1) * texelSize).rgb;
    vec3 c8 = texture(currentColor, uv + vec2( 1,  1) * texelSize).rgb;
    
    // Compute mean
    vec3 mean = (c0 + c1 + c2 + c3 + c4 + c5 + c6 + c7 + c8) / 9.0;
    
    // Compute variance
    vec3 m0 = c0 - mean;
    vec3 m1 = c1 - mean;
    vec3 m2 = c2 - mean;
    vec3 m3 = c3 - mean;
    vec3 m4 = c4 - mean;
    vec3 m5 = c5 - mean;
    vec3 m6 = c6 - mean;
    vec3 m7 = c7 - mean;
    vec3 m8 = c8 - mean;
    
    vec3 variance = (m0*m0 + m1*m1 + m2*m2 + m3*m3 + m4*m4 + 
                     m5*m5 + m6*m6 + m7*m7 + m8*m8) / 9.0;
    vec3 stdDev = sqrt(variance);
    
    // Clip history to mean ± 1.5 * stdDev
    vec3 colorMin = mean - stdDev * 1.5;
    vec3 colorMax = mean + stdDev * 1.5;
    
    return clamp(historyColor, colorMin, colorMax);
}

/**
 * Unsharp mask sharpening
 * Compensates for TAA blur
 */
vec3 sharpen(vec3 color, vec2 uv, vec2 texelSize, float sharpness) {
    // Sample 5-tap cross pattern
    vec3 c0 = texture(currentColor, uv).rgb;
    vec3 c1 = texture(currentColor, uv + vec2(-1,  0) * texelSize).rgb;
    vec3 c2 = texture(currentColor, uv + vec2( 1,  0) * texelSize).rgb;
    vec3 c3 = texture(currentColor, uv + vec2( 0, -1) * texelSize).rgb;
    vec3 c4 = texture(currentColor, uv + vec2( 0,  1) * texelSize).rgb;
    
    // Compute blurred color
    vec3 blurred = (c0 + c1 + c2 + c3 + c4) / 5.0;
    
    // Unsharp mask: original + (original - blurred) * sharpness
    return color + (color - blurred) * sharpness;
}

// ============================================================================
// Main
// ============================================================================

void main() {
    vec2 texelSize = 1.0 / textureSize(currentColor, 0);
    
    // 1. Sample current frame color
    vec3 current = texture(currentColor, vScreenUV).rgb;
    
    // 2. Sample motion vector
    vec2 motion = texture(motionVector, vScreenUV).rg;
    
    // 3. Calculate history UV (reproject to previous frame)
    vec2 historyUV = vScreenUV - motion;
    
    // 4. Check if history UV is valid
    bool validHistory = (historyUV.x >= 0.0 && historyUV.x <= 1.0 &&
                         historyUV.y >= 0.0 && historyUV.y <= 1.0);
    
    vec3 finalColor = current;
    
    if (validHistory) {
        // 5. Sample history with catmull-rom filtering
        vec3 history = sampleCatmullRom(historyColor, historyUV, texelSize);
        
        // 6. Variance clipping (history rejection)
        vec3 clippedHistory = clipHistory(history, current, vScreenUV, texelSize);
        
        // 7. Temporal blend
        float blendFactor = scene.taaParams.x;  // Default: 0.1
        finalColor = mix(clippedHistory, current, blendFactor);
        
        // 8. Optional sharpening
        if (scene.taaParams.z > 0.5) {  // enableSharpening
            float sharpness = scene.taaParams.y;
            finalColor = sharpen(finalColor, vScreenUV, texelSize, sharpness);
        }
    }
    
    outColor = vec4(finalColor, 1.0);
}
```

---

## 5. 实现细节

### 5.1 初始化流程

```cpp
bool FDeferredRenderer::Initialize(IRHIDevice* device, uint32 width, uint32 height) {
    m_device = device;
    m_width = width;
    m_height = height;
    m_frameIndex = 0;
    
    // 1. Create GBuffer (含 Motion Vector)
    if (!CreateGBuffer()) {
        MR_LOG_ERROR("Failed to create GBuffer");
        return false;
    }
    
    // 2. Create TAA resources
    if (!CreateTAAResources()) {
        MR_LOG_ERROR("Failed to create TAA resources");
        return false;
    }
    
    // 3. Load shaders
    if (!LoadShaders()) {
        MR_LOG_ERROR("Failed to load shaders");
        return false;
    }
    
    // 4. Create pipelines
    if (!CreatePipelines()) {
        MR_LOG_ERROR("Failed to create pipelines");
        return false;
    }
    
    // 5. Create UBOs
    if (!CreateUniformBuffers()) {
        MR_LOG_ERROR("Failed to create uniform buffers");
        return false;
    }
    
    // 6. Initialize previous frame data
    m_previousJitter = FVector2f(0.0f, 0.0f);
    m_previousViewProj = FMatrix44f::Identity();
    
    MR_LOG_INFO("FDeferredRenderer initialized successfully with TAA");
    return true;
}

bool FDeferredRenderer::CreateTAAResources() {
    // 1. Create Lighting RT (临时纹理，存储 Lighting Pass 结果)
    TextureDesc lightingDesc;
    lightingDesc.width = m_width;
    lightingDesc.height = m_height;
    lightingDesc.format = EPixelFormat::R8G8B8A8_UNORM;
    lightingDesc.usage = ETextureUsage::RenderTarget | ETextureUsage::ShaderResource;
    lightingDesc.debugName = "Lighting RT";
    
    m_lightingTarget = m_device->createTexture(lightingDesc);
    if (!m_lightingTarget) {
        MR_LOG_ERROR("Failed to create Lighting RT");
        return false;
    }
    
    // 2. Create History RT (存储上一帧的 TAA 结果)
    TextureDesc historyDesc;
    historyDesc.width = m_width;
    historyDesc.height = m_height;
    historyDesc.format = EPixelFormat::R8G8B8A8_UNORM;
    historyDesc.usage = ETextureUsage::RenderTarget | ETextureUsage::ShaderResource;
    historyDesc.debugName = "History RT";
    
    m_historyTarget = m_device->createTexture(historyDesc);
    if (!m_historyTarget) {
        MR_LOG_ERROR("Failed to create History RT");
        return false;
    }
    
    MR_LOG_INFO("TAA resources created: Lighting RT + History RT");
    return true;
}
```

### 5.2 渲染流程

```cpp
void FDeferredRenderer::Render(
    IRHICommandList* cmdList,
    const TArray<RenderObject>& objects,
    const Camera& camera,
    const SceneLighting& lighting)
{
    // 1. Generate jitter for current frame
    m_currentJitter = GenerateJitter(m_frameIndex);
    
    // 2. Apply jitter to projection matrix
    FMatrix44f jitteredProj = ApplyJitter(
        camera.projectionMatrix,
        m_currentJitter,
        m_width,
        m_height
    );
    
    // 3. Update UBOs
    UpdateTransformUBO(camera, jitteredProj);
    UpdateSceneUBO(camera, lighting);
    
    // 4. Render Geometry Pass → GBuffer (含 Motion Vector)
    RenderGeometryPass(cmdList, objects);
    
    // 5. Render Lighting Pass → Lighting RT (临时)
    RenderLightingPass(cmdList);
    
    // 6. Render TAA Pass → Swapchain (最终输出)
    RenderTAAPass(cmdList);
    
    // 7. Copy current output to history for next frame
    CopyToHistory(cmdList);
    
    // 8. Update previous frame data
    m_previousJitter = m_currentJitter;
    m_previousViewProj = camera.viewMatrix * camera.projectionMatrix;
    m_frameIndex++;
}

void FDeferredRenderer::RenderTAAPass(IRHICommandList* cmdList) {
    // 1. Set pipeline
    cmdList->setPipelineState(m_taaPipeline);
    
    // 2. Bind textures
    cmdList->setTexture(0, m_lightingTarget);      // Current color
    cmdList->setTexture(1, m_motionVectorTarget);  // Motion vector
    cmdList->setTexture(2, m_historyTarget);       // History color
    
    // 3. Bind UBO
    cmdList->setUniformBuffer(3, m_sceneUBO);
    
    // 4. Set render target (Swapchain)
    cmdList->setRenderTarget(0, m_device->getCurrentBackBuffer());
    
    // 5. Draw fullscreen triangle
    cmdList->draw(3, 0);
}

void FDeferredRenderer::CopyToHistory(IRHICommandList* cmdList) {
    // Copy current TAA output to history buffer for next frame
    cmdList->blitTexture(
        m_device->getCurrentBackBuffer(),  // Source
        m_historyTarget                    // Destination
    );
}
```

### 5.3 错误处理

```cpp
// 1. 首帧处理（没有历史数据）
// 在 TAAPass.frag 中自动处理：
// if (!validHistory) {
//     finalColor = current;  // 直接使用当前帧
// }

// 2. 窗口 Resize 处理
void FDeferredRenderer::OnResize(uint32 newWidth, uint32 newHeight) {
    if (newWidth == m_width && newHeight == m_height) {
        return;
    }
    
    MR_LOG_INFO("Resizing renderer: %ux%u -> %ux%u", 
        m_width, m_height, newWidth, newHeight);
    
    // 释放旧资源
    ReleaseGBuffer();
    ReleaseTAAResources();
    
    // 更新尺寸
    m_width = newWidth;
    m_height = newHeight;
    
    // 重新创建资源
    CreateGBuffer();
    CreateTAAResources();
    
    // 重置帧计数器（清空历史）
    m_frameIndex = 0;
    
    MR_LOG_INFO("Resize completed");
}

// 3. 场景切换处理
void FDeferredRenderer::OnSceneChanged() {
    // 清空历史缓冲
    IRHICommandList* cmdList = m_device->createCommandList();
    cmdList->clearRenderTarget(m_historyTarget, FVector4f(0, 0, 0, 1));
    m_device->submitCommandList(cmdList);
    
    // 重置帧计数器
    m_frameIndex = 0;
    
    MR_LOG_INFO("Scene changed, TAA history cleared");
}
```

---

## 6. 测试策略

### 6.1 单元测试

```cpp
// Halton 序列生成测试
TEST(TAATests, HaltonSequenceGeneration);

// Jitter 生成测试
TEST(TAATests, JitterGeneration);

// Jittered Projection 测试
TEST(TAATests, JitteredProjection);

// UBO 布局测试
TEST(TAATests, UBOLayout);
```

### 6.2 集成测试

```cpp
// 初始化测试
TEST(TAAIntegrationTests, Initialization);

// 首帧渲染测试
TEST(TAAIntegrationTests, FirstFrameRendering);

// 多帧渲染测试
TEST(TAAIntegrationTests, MultiFrameRendering);

// Resize 处理测试
TEST(TAAIntegrationTests, ResizeHandling);
```

### 6.3 视觉验证测试

| 测试场景 | 预期结果 |
|---------|---------|
| 静态场景 | 8 帧后完全稳定，无闪烁 |
| 相机旋转 | 画面平滑，无明显拖影 |
| 物体移动 | 运动物体边缘平滑，无拖影 |
| 快速移动 | Variance Clipping 避免严重拖影 |
| 锐化对比 | 提升清晰度，无过度锐化 |
| 抗锯齿质量 | 明显优于无 AA |

### 6.4 性能测试

```cpp
// 帧时间测试
TEST(TAAPerformanceTests, FrameTime);
// 预期：TAA Pass < 2ms (1080p)

// 内存使用测试
TEST(TAAPerformanceTests, MemoryUsage);
// 预期：内存增加 ~24MB (1080p)
```

### 6.5 验收标准

**功能性**：
- ✅ Halton 序列生成正确
- ✅ Jitter 应用到投影矩阵
- ✅ Motion Vector 正确计算
- ✅ 历史帧正确混合
- ✅ Variance Clipping 工作正常
- ✅ 锐化功能可选启用

**质量**：
- ✅ 静态场景：8 帧后完全稳定
- ✅ 相机旋转：边缘平滑，无明显拖影
- ✅ 物体移动：Motion Vector 正确，边缘平滑
- ✅ 抗锯齿质量：明显优于无 AA

**性能**：
- ✅ TAA Pass 耗时 < 2ms (1080p)
- ✅ 内存增加 ~24MB (1080p)
- ✅ 整体帧率 > 50 FPS (1080p, 简单场景)

**鲁棒性**：
- ✅ 首帧渲染正常（无历史数据）
- ✅ 窗口 Resize 正常
- ✅ 场景切换正常
- ✅ 无内存泄漏
- ✅ 无 GPU 错误

---

## 7. 性能分析

### 7.1 显存消耗

| 分辨率 | 原 GBuffer | TAA 新增 | 总计 | 增长 |
|--------|-----------|---------|------|------|
| 1080p  | 50 MB     | 25 MB   | 75 MB | +50% |
| 1440p  | 90 MB     | 44 MB   | 134 MB | +49% |
| 4K     | 190 MB    | 95 MB   | 285 MB | +50% |

**对比 MSAA 4x**：
| 分辨率 | TAA | MSAA 4x | 节省 |
|--------|-----|---------|------|
| 1080p  | 75 MB | 200 MB | 62% |
| 1440p  | 134 MB | 360 MB | 63% |
| 4K     | 285 MB | 760 MB | 62% |

### 7.2 性能开销

**预期性能（1080p）**：
- Geometry Pass: +0.2ms (Motion Vector 计算)
- Lighting Pass: 无变化
- TAA Pass: +1.5ms (Variance Clipping + Temporal Blend)
- 总开销: ~1.7ms

**对比 MSAA 4x**：
| 方案 | 性能开销 |
|------|---------|
| TAA | ~1.7ms |
| MSAA 4x | ~5-10ms |

### 7.3 带宽消耗

**每帧读写**：
- 读取：Lighting RT (8MB) + Motion Vector RT (8MB) + History RT (8MB) = 24 MB
- 写入：Swapchain (8MB) + History RT (8MB) = 16 MB
- 总计：40 MB/frame

---

## 8. 风险与应对

### 8.1 技术风险

| 风险 | 影响 | 概率 | 应对措施 |
|------|------|------|---------|
| Variance Clipping 过度钳制 | 丢失细节 | 中 | 调整 stdDev 系数（1.5 → 2.0） |
| 快速移动时拖影 | 画面模糊 | 中 | 降低 blendFactor（0.1 → 0.2） |
| Halton 序列收敛慢 | 需要更多帧稳定 | 低 | 使用 16-sample pattern |
| Sharpening 过度 | 光晕 | 低 | 默认关闭，用户可选 |
| Motion Vector 精度不足 | 拖影 | 低 | RG16F 精度足够 |

### 8.2 性能风险

| 风险 | 影响 | 概率 | 应对措施 |
|------|------|------|---------|
| TAA Pass 超过 2ms | 帧率下降 | 低 | 优化 Variance Clipping（减少采样点） |
| 显存超出预算 | 移动设备问题 | 低 | 提供配置选项（禁用 TAA） |
| 带宽瓶颈 | 移动设备性能差 | 中 | 使用更低分辨率的 History RT |

### 8.3 兼容性风险

| 风险 | 影响 | 概率 | 应对措施 |
|------|------|------|---------|
| RG16F 格式不支持 | 无法创建 Motion Vector RT | 低 | Fallback 到 RG32F |
| Blit 操作不支持 | 无法 Copy to History | 低 | 使用额外的 Copy Pass |

---

## 9. 开发计划

### 9.1 任务分解

| 任务 | 预计工时 | 依赖 |
|------|---------|------|
| 1. 扩展 UBO 定义 | 0.5h | 无 |
| 2. 创建 Motion Vector RT | 0.5h | 任务 1 |
| 3. 修改 Geometry Pass Shader | 1h | 任务 2 |
| 4. 创建 TAA Resources | 1h | 任务 2 |
| 5. 编写 TAA Pass Shader | 2h | 任务 4 |
| 6. 创建 TAA Pipeline | 1h | 任务 5 |
| 7. 实现 Halton Jitter | 1h | 无 |
| 8. 集成到渲染流程 | 2h | 任务 1-7 |
| 9. 错误处理（Resize, 首帧） | 1h | 任务 8 |
| 10. 单元测试 | 2h | 任务 7 |
| 11. 集成测试 | 2h | 任务 8-9 |
| 12. 视觉验证测试 | 2h | 任务 8-9 |
| 13. 性能测试与优化 | 2h | 任务 8-9 |
| 14. 文档与代码审查 | 1h | 任务 1-13 |
| **总计** | **19h** | - |

### 9.2 里程碑

| 里程碑 | 完成标准 | 预计时间 |
|--------|---------|---------|
| M1: 基础架构 | UBO 扩展 + Motion Vector RT 创建 | Day 1 上午 |
| M2: Geometry Pass | Motion Vector 正确输出 | Day 1 下午 |
| M3: TAA Pass | TAA Shader 编译通过 | Day 2 上午 |
| M4: 集成渲染 | 首帧渲染成功 | Day 2 下午 |
| M5: 功能完整 | 所有功能测试通过 | Day 3 上午 |
| M6: 质量验证 | 视觉测试通过 | Day 3 下午 |
| M7: 性能优化 | 性能测试达标 | Day 4 上午 |
| M8: 交付 | 代码审查通过 | Day 4 下午 |

### 9.3 开发顺序

**Day 1**：
1. 扩展 UBO 定义（TransformUBO + SceneUBO）
2. 创建 Motion Vector RT
3. 修改 Geometry Pass Shader（输出 Motion Vector）
4. 单元测试：UBO 布局验证

**Day 2**：
5. 创建 TAA Resources（Lighting RT + History RT）
6. 编写 TAA Pass Shader（完整算法）
7. 创建 TAA Pipeline
8. 实现 Halton Jitter 生成
9. 单元测试：Halton 序列、Jitter 生成

**Day 3**：
10. 集成到渲染流程（修改 Render 方法）
11. 错误处理（首帧、Resize、场景切换）
12. 集成测试：初始化、多帧渲染、Resize
13. 视觉验证测试：6 个场景

**Day 4**：
14. 性能测试与优化
15. 代码审查与文档完善
16. 最终验收

---

## 10. 附录

### 10.1 参考资料

**UE5 源码**：
- `Engine/Source/Runtime/Renderer/Private/PostProcess/TemporalAA.cpp`
- `Engine/Shaders/Private/TemporalAA.usf`

**学术论文**：
- "Temporal Reprojection Anti-Aliasing in INSIDE" (GDC 2016)
- "High Quality Temporal Supersampling" (SIGGRAPH 2014)

**技术文章**：
- [A Survey of Temporal Antialiasing Techniques](https://www.elopezr.com/temporal-aa-and-the-quest-for-the-holy-trail/)
- [Halton Sequence - Wikipedia](https://en.wikipedia.org/wiki/Halton_sequence)

### 10.2 术语表

| 术语 | 英文 | 说明 |
|------|------|------|
| 时间抗锯齿 | Temporal Anti-Aliasing (TAA) | 利用时间维度信息的抗锯齿技术 |
| 抖动 | Jitter | 投影矩阵的亚像素偏移 |
| 运动矢量 | Motion Vector | 像素在屏幕空间的运动方向和距离 |
| 历史帧 | History Frame | 上一帧的渲染结果 |
| 方差裁剪 | Variance Clipping | 基于颜色方差的历史帧验证方法 |
| 反锐化掩模 | Unsharp Mask | 经典的图像锐化算法 |
| Halton 序列 | Halton Sequence | 低差异序列，用于生成均匀分布的采样点 |

---

**文档版本历史**：
- v1.0 (2026-04-26): 初始版本，完整设计

**审核状态**：待审核

**下一步行动**：调用 writing-plans 技能创建实现计划
