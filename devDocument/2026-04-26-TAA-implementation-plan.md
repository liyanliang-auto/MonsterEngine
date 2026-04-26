# TAA (Temporal Anti-Aliasing) 实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 在 MonsterEngine 延迟渲染管线中实现完整的 TAA（时间抗锯齿）功能，包含 Jitter、Motion Vector、History Buffer、Temporal Reprojection、Variance Clipping、Sharpening 六个核心模块。

**架构：** 扩展现有的 FDeferredRenderer，新增 Motion Vector RT、Lighting RT、History RT 三张纹理，新增 TAA Pass 作为第三个渲染 Pass。修改 Geometry Pass 输出 Motion Vector，Lighting Pass 输出到临时 RT，TAA Pass 读取三张纹理并输出到 Swapchain。

**技术栈：** C++20, Vulkan/OpenGL, GLSL 450, MonsterEngine RHI, TArray/TSharedPtr

---

## 文件结构

### 新增文件
- `Include/Engine/Deferred/DeferredUniformTypes.h` - 扩展 UBO 定义
- `Shaders/Deferred/TAAPass.vert` - TAA Pass 顶点着色器
- `Shaders/Deferred/TAAPass.frag` - TAA Pass 片段着色器
- `Tests/Engine/Deferred/TAATests.cpp` - TAA 单元测试

### 修改文件
- `Include/Engine/Deferred/FDeferredRenderer.h` - 添加 TAA 成员和方法
- `Source/Engine/Deferred/FDeferredRenderer.cpp` - 实现 TAA 功能
- `Shaders/Deferred/GeometryPass.vert` - 添加 Motion Vector 计算
- `Shaders/Deferred/GeometryPass.frag` - 输出 Motion Vector

---

## 任务 1：扩展 UBO 定义

**文件：**
- 修改：`Include/Engine/Deferred/DeferredUniformTypes.h`
- 测试：`Tests/Engine/Deferred/TAATests.cpp`

- [ ] **步骤 1：编写 UBO 布局测试**

创建测试文件 `Tests/Engine/Deferred/TAATests.cpp`：

```cpp
#include <gtest/gtest.h>
#include "Engine/Deferred/DeferredUniformTypes.h"

TEST(TAATests, TransformUBOLayout) {
    EXPECT_EQ(sizeof(FDeferredTransformUBO), 352);
    EXPECT_EQ(offsetof(FDeferredTransformUBO, Model), 0);
    EXPECT_EQ(offsetof(FDeferredTransformUBO, View), 64);
    EXPECT_EQ(offsetof(FDeferredTransformUBO, Proj), 128);
    EXPECT_EQ(offsetof(FDeferredTransformUBO, NormalMatrix), 192);
    EXPECT_EQ(offsetof(FDeferredTransformUBO, CameraPos), 256);
    EXPECT_EQ(offsetof(FDeferredTransformUBO, PreviousModel), 272);
}

TEST(TAATests, SceneUBOLayout) {
    EXPECT_EQ(sizeof(FDeferredSceneUBO), 256);
    EXPECT_EQ(offsetof(FDeferredSceneUBO, InvViewProj), 0);
    EXPECT_EQ(offsetof(FDeferredSceneUBO, PreviousViewProj), 160);
    EXPECT_EQ(offsetof(FDeferredSceneUBO, JitterOffset), 224);
    EXPECT_EQ(offsetof(FDeferredSceneUBO, TAAParams), 240);
}
```

- [ ] **步骤 2：运行测试验证失败**

```powershell
cd e:\MonsterEngine
.\x64\Debug\MonsterEngineTests.exe --gtest_filter=TAATests.* 2>&1
```

预期：FAIL，报错 "PreviousModel not found" 或 "size mismatch"

- [ ] **步骤 3：扩展 TransformUBO**

修改 `Include/Engine/Deferred/DeferredUniformTypes.h`：

```cpp
struct FDeferredTransformUBO {
    FMatrix44f Model;
    FMatrix44f View;
    FMatrix44f Proj;
    FMatrix44f NormalMatrix;
    FVector4f  CameraPos;
    FMatrix44f PreviousModel;  // TAA: Previous frame model matrix
    FVector4f  Padding;        // Alignment padding
};

static_assert(sizeof(FDeferredTransformUBO) == 352, 
    "TransformUBO size must be 352 bytes");
static_assert(offsetof(FDeferredTransformUBO, PreviousModel) == 272,
    "PreviousModel offset must be 272");
```

- [ ] **步骤 4：扩展 SceneUBO**

在同一文件中修改：

```cpp
struct FDeferredSceneUBO {
    FMatrix44f InvViewProj;
    FVector4f  CameraPos;
    FVector4f  DirLightDirection;
    FVector4f  DirLightColorIntensity;
    FVector4f  PointLightPositionRadius;
    FVector4f  PointLightColorIntensity;
    FVector4f  Ambient;
    FMatrix44f PreviousViewProj;  // TAA: Previous frame VP matrix
    FVector4f  JitterOffset;      // TAA: xy=current jitter, zw=previous jitter
    FVector4f  TAAParams;         // TAA: x=blendFactor, y=sharpness, z=enableSharpening
};

static_assert(sizeof(FDeferredSceneUBO) == 256,
    "SceneUBO size must be 256 bytes");
static_assert(offsetof(FDeferredSceneUBO, PreviousViewProj) == 160,
    "PreviousViewProj offset must be 160");
static_assert(offsetof(FDeferredSceneUBO, JitterOffset) == 224,
    "JitterOffset offset must be 224");
static_assert(offsetof(FDeferredSceneUBO, TAAParams) == 240,
    "TAAParams offset must be 240");
```

- [ ] **步骤 5：运行测试验证通过**

```powershell
.\x64\Debug\MonsterEngineTests.exe --gtest_filter=TAATests.* 2>&1
```

预期：PASS，所有 UBO 布局测试通过

- [ ] **步骤 6：Commit**

```powershell
git add Include/Engine/Deferred/DeferredUniformTypes.h Tests/Engine/Deferred/TAATests.cpp
git commit -m "feat(deferred): extend UBO for TAA support"
```

---

## 任务 2：添加 TAA 成员到 FDeferredRenderer

**文件：**
- 修改：`Include/Engine/Deferred/FDeferredRenderer.h`

- [ ] **步骤 1：添加 TAA 纹理成员**

在 `FDeferredRenderer` 类的 private 部分添加：

```cpp
// TAA textures
TSharedPtr<IRHITexture> m_motionVectorTarget;  // Motion Vector RT (RG16F)
TSharedPtr<IRHITexture> m_lightingTarget;      // Lighting RT (RGBA8, temp)
TSharedPtr<IRHITexture> m_historyTarget;       // History RT (RGBA8)
```

- [ ] **步骤 2：添加 TAA Pipeline 成员**

```cpp
// TAA pipeline
TSharedPtr<IRHIPipelineState> m_taaPipeline;
```

- [ ] **步骤 3：添加 TAA 状态成员**

```cpp
// TAA state
uint32 m_frameIndex;
FVector2f m_currentJitter;
FVector2f m_previousJitter;
FMatrix44f m_previousViewProj;
```

- [ ] **步骤 4：添加 TAA 配置结构**

```cpp
// TAA configuration
struct FTAAConfig {
    bool enableTAA = true;
    bool enableSharpening = false;
    float blendFactor = 0.1f;
    float sharpness = 0.3f;
} m_taaConfig;
```

- [ ] **步骤 5：添加 TAA 方法声明**

在 public 部分添加：

```cpp
// TAA methods
bool CreateTAAResources();
bool CreateTAAPipeline();
void RenderTAAPass(IRHICommandList* cmdList);
void CopyToHistory(IRHICommandList* cmdList);

// Jitter generation
float Halton(uint32 index, uint32 base);
FVector2f GenerateJitter(uint32 frameIndex);
FMatrix44f ApplyJitter(const FMatrix44f& proj, const FVector2f& jitter,
                       uint32 width, uint32 height);

// Lifecycle
void OnResize(uint32 newWidth, uint32 newHeight);
void OnSceneChanged();
```

- [ ] **步骤 6：Commit**

```powershell
git add Include/Engine/Deferred/FDeferredRenderer.h
git commit -m "feat(deferred): add TAA members and methods to FDeferredRenderer"
```

---

## 任务 3：实现 Halton 序列和 Jitter 生成

**文件：**
- 修改：`Source/Engine/Deferred/FDeferredRenderer.cpp`
- 测试：`Tests/Engine/Deferred/TAATests.cpp`

- [ ] **步骤 1：编写 Halton 序列测试**

在 `TAATests.cpp` 添加：

```cpp
TEST(TAATests, HaltonSequenceGeneration) {
    FDeferredRenderer renderer;
    
    EXPECT_FLOAT_EQ(renderer.Halton(1, 2), 0.5f);
    EXPECT_FLOAT_EQ(renderer.Halton(2, 2), 0.25f);
    EXPECT_FLOAT_EQ(renderer.Halton(3, 2), 0.75f);
    EXPECT_FLOAT_EQ(renderer.Halton(1, 3), 0.333333f);
    EXPECT_FLOAT_EQ(renderer.Halton(2, 3), 0.666666f);
}

TEST(TAATests, JitterGeneration) {
    FDeferredRenderer renderer;
    
    for (uint32 i = 0; i < 8; i++) {
        FVector2f jitter = renderer.GenerateJitter(i);
        EXPECT_GE(jitter.x, -0.5f);
        EXPECT_LE(jitter.x, 0.5f);
        EXPECT_GE(jitter.y, -0.5f);
        EXPECT_LE(jitter.y, 0.5f);
    }
    
    FVector2f jitter0 = renderer.GenerateJitter(0);
    FVector2f jitter8 = renderer.GenerateJitter(8);
    EXPECT_EQ(jitter0.x, jitter8.x);
    EXPECT_EQ(jitter0.y, jitter8.y);
}
```

- [ ] **步骤 2：运行测试验证失败**

```powershell
.\x64\Debug\MonsterEngineTests.exe --gtest_filter=TAATests.Halton* 2>&1
```

预期：FAIL，报错 "Halton method not found"

- [ ] **步骤 3：实现 Halton 方法**

在 `FDeferredRenderer.cpp` 添加：

```cpp
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
```

- [ ] **步骤 4：实现 GenerateJitter 方法**

```cpp
FVector2f FDeferredRenderer::GenerateJitter(uint32 frameIndex) {
    uint32 sampleIndex = frameIndex % 8;
    
    float halton2 = Halton(sampleIndex + 1, 2);
    float halton3 = Halton(sampleIndex + 1, 3);
    
    FVector2f jitter;
    jitter.x = halton2 - 0.5f;
    jitter.y = halton3 - 0.5f;
    
    return jitter;
}
```

- [ ] **步骤 5：运行测试验证通过**

```powershell
.\x64\Debug\MonsterEngineTests.exe --gtest_filter=TAATests.Halton* 2>&1
```

预期：PASS

- [ ] **步骤 6：实现 ApplyJitter 方法**

```cpp
FMatrix44f FDeferredRenderer::ApplyJitter(
    const FMatrix44f& proj,
    const FVector2f& jitter,
    uint32 width,
    uint32 height)
{
    FMatrix44f jitteredProj = proj;
    
    float ndcOffsetX = (jitter.x * 2.0f) / width;
    float ndcOffsetY = (jitter.y * 2.0f) / height;
    
    jitteredProj.m[3][0] += ndcOffsetX;
    jitteredProj.m[3][1] += ndcOffsetY;
    
    return jitteredProj;
}
```

- [ ] **步骤 7：Commit**

```powershell
git add Source/Engine/Deferred/FDeferredRenderer.cpp Tests/Engine/Deferred/TAATests.cpp
git commit -m "feat(deferred): implement Halton sequence and jitter generation"
```

---

## 任务 4：创建 Motion Vector RT

**文件：**
- 修改：`Source/Engine/Deferred/FDeferredRenderer.cpp`

- [ ] **步骤 1：修改 CreateGBuffer 方法**

在 `CreateGBuffer()` 方法中添加 Motion Vector RT 创建：

```cpp
bool FDeferredRenderer::CreateGBuffer() {
    // ... 现有的 Normal RT, Albedo RT, Depth RT 创建代码 ...
    
    // Create Motion Vector RT
    TextureDesc motionVectorDesc;
    motionVectorDesc.width = m_width;
    motionVectorDesc.height = m_height;
    motionVectorDesc.format = EPixelFormat::R16G16_FLOAT;
    motionVectorDesc.usage = ETextureUsage::RenderTarget | ETextureUsage::ShaderResource;
    motionVectorDesc.debugName = "Motion Vector RT";
    
    m_motionVectorTarget = m_device->createTexture(motionVectorDesc);
    if (!m_motionVectorTarget) {
        MR_LOG_ERROR("Failed to create Motion Vector RT");
        return false;
    }
    
    MR_LOG_INFO("Motion Vector RT created: %ux%u RG16F", m_width, m_height);
    return true;
}
```

- [ ] **步骤 2：修改 ReleaseGBuffer 方法**

在 `ReleaseGBuffer()` 方法中添加：

```cpp
void FDeferredRenderer::ReleaseGBuffer() {
    m_normalTarget.reset();
    m_albedoTarget.reset();
    m_motionVectorTarget.reset();  // 新增
    m_depthTarget.reset();
}
```

- [ ] **步骤 3：编译验证**

```powershell
& "E:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" MonsterEngine.sln /t:Build /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:minimal 2>&1 | Select-Object -Last 10
```

预期：编译成功

- [ ] **步骤 4：Commit**

```powershell
git add Source/Engine/Deferred/FDeferredRenderer.cpp
git commit -m "feat(deferred): create Motion Vector RT in GBuffer"
```

---

## 任务 5：修改 Geometry Pass Shader 输出 Motion Vector

**文件：**
- 修改：`Shaders/Deferred/GeometryPass.vert`
- 修改：`Shaders/Deferred/GeometryPass.frag`

- [ ] **步骤 1：修改顶点着色器 UBO**

在 `GeometryPass.vert` 中修改 TransformUBO：

```glsl
layout(set = 0, binding = 0) uniform TransformUBO {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 normalMatrix;
    vec4 cameraPos;
    mat4 previousModel;  // 新增
    vec4 padding;
} ubo;
```

- [ ] **步骤 2：添加 Motion Vector 输出**

在顶点着色器输出部分添加：

```glsl
layout(location = 3) out vec2 vMotionVector;
```

- [ ] **步骤 3：计算 Motion Vector**

在 `main()` 函数中添加：

```glsl
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
    
    // ... 现有的输出代码 ...
    gl_Position = clipPos;
}
```

- [ ] **步骤 4：修改片段着色器输入**

在 `GeometryPass.frag` 中添加：

```glsl
layout(location = 3) in vec2 vMotionVector;
```

- [ ] **步骤 5：添加 Motion Vector 输出**

```glsl
layout(location = 2) out vec2 outMotionVector;
```

- [ ] **步骤 6：输出 Motion Vector**

在 `main()` 函数中添加：

```glsl
void main() {
    outNormal = vec4(normalize(vWorldNormal), 0.0);
    
    vec4 albedo = texture(albedoMap, vTexCoord);
    outAlbedo = vec4(albedo.rgb, 1.0);
    
    outMotionVector = vMotionVector;  // 新增
}
```

- [ ] **步骤 7：Commit**

```powershell
git add Shaders/Deferred/GeometryPass.vert Shaders/Deferred/GeometryPass.frag
git commit -m "feat(shader): add Motion Vector output to Geometry Pass"
```

---

## 任务 6：创建 TAA Resources

**文件：**
- 修改：`Source/Engine/Deferred/FDeferredRenderer.cpp`

- [ ] **步骤 1：实现 CreateTAAResources 方法**

```cpp
bool FDeferredRenderer::CreateTAAResources() {
    // Create Lighting RT
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
    
    // Create History RT
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

- [ ] **步骤 2：在 Initialize 中调用**

在 `Initialize()` 方法中添加：

```cpp
bool FDeferredRenderer::Initialize(IRHIDevice* device, uint32 width, uint32 height) {
    // ... 现有初始化代码 ...
    
    if (!CreateTAAResources()) {
        MR_LOG_ERROR("Failed to create TAA resources");
        return false;
    }
    
    m_frameIndex = 0;
    m_previousJitter = FVector2f(0.0f, 0.0f);
    m_previousViewProj = FMatrix44f::Identity();
    
    return true;
}
```

- [ ] **步骤 3：实现资源释放**

添加 `ReleaseTAAResources()` 方法：

```cpp
void FDeferredRenderer::ReleaseTAAResources() {
    m_lightingTarget.reset();
    m_historyTarget.reset();
    m_taaPipeline.reset();
}
```

- [ ] **步骤 4：编译验证**

```powershell
& "E:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" MonsterEngine.sln /t:Build /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:minimal 2>&1 | Select-Object -Last 10
```

预期：编译成功

- [ ] **步骤 5：Commit**

```powershell
git add Source/Engine/Deferred/FDeferredRenderer.cpp
git commit -m "feat(deferred): create TAA resources (Lighting RT + History RT)"
```

---

## 任务 7：编写 TAA Pass Shaders

**文件：**
- 创建：`Shaders/Deferred/TAAPass.vert`
- 创建：`Shaders/Deferred/TAAPass.frag`

- [ ] **步骤 1：创建 TAA Pass 顶点着色器**

创建 `Shaders/Deferred/TAAPass.vert`：

```glsl
#version 450 core

layout(location = 0) out vec2 vScreenUV;

void main() {
    // Generate fullscreen triangle
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vScreenUV = uv;
    
    vec2 pos = uv * 2.0 - 1.0;
    gl_Position = vec4(pos, 0.0, 1.0);
}
```

- [ ] **步骤 2：创建 TAA Pass 片段着色器（第1部分：头部和工具函数）**

创建 `Shaders/Deferred/TAAPass.frag`：

```glsl
#version 450 core

layout(location = 0) in vec2 vScreenUV;

layout(set = 0, binding = 0) uniform sampler2D currentColor;
layout(set = 0, binding = 1) uniform sampler2D motionVector;
layout(set = 0, binding = 2) uniform sampler2D historyColor;

layout(set = 0, binding = 3) uniform SceneUBO {
    mat4 invViewProj;
    vec4 cameraPos;
    vec4 dirLightDirection;
    vec4 dirLightColorIntensity;
    vec4 pointLightPositionRadius;
    vec4 pointLightColorIntensity;
    vec4 ambient;
    mat4 previousViewProj;
    vec4 jitterOffset;
    vec4 taaParams;
} scene;

layout(location = 0) out vec4 outColor;

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
```

- [ ] **步骤 3：添加 Variance Clipping 函数**

在同一文件中继续添加：

```glsl
vec3 clipHistory(vec3 historyColor, vec3 currentColor, vec2 uv, vec2 texelSize) {
    vec3 c0 = texture(currentColor, uv + vec2(-1, -1) * texelSize).rgb;
    vec3 c1 = texture(currentColor, uv + vec2( 0, -1) * texelSize).rgb;
    vec3 c2 = texture(currentColor, uv + vec2( 1, -1) * texelSize).rgb;
    vec3 c3 = texture(currentColor, uv + vec2(-1,  0) * texelSize).rgb;
    vec3 c4 = currentColor;
    vec3 c5 = texture(currentColor, uv + vec2( 1,  0) * texelSize).rgb;
    vec3 c6 = texture(currentColor, uv + vec2(-1,  1) * texelSize).rgb;
    vec3 c7 = texture(currentColor, uv + vec2( 0,  1) * texelSize).rgb;
    vec3 c8 = texture(currentColor, uv + vec2( 1,  1) * texelSize).rgb;
    
    vec3 mean = (c0 + c1 + c2 + c3 + c4 + c5 + c6 + c7 + c8) / 9.0;
    
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
    
    vec3 colorMin = mean - stdDev * 1.5;
    vec3 colorMax = mean + stdDev * 1.5;
    
    return clamp(historyColor, colorMin, colorMax);
}
```

- [ ] **步骤 4：添加 Sharpening 函数和 Main 函数**

```glsl
vec3 sharpen(vec3 color, vec2 uv, vec2 texelSize, float sharpness) {
    vec3 c0 = texture(currentColor, uv).rgb;
    vec3 c1 = texture(currentColor, uv + vec2(-1,  0) * texelSize).rgb;
    vec3 c2 = texture(currentColor, uv + vec2( 1,  0) * texelSize).rgb;
    vec3 c3 = texture(currentColor, uv + vec2( 0, -1) * texelSize).rgb;
    vec3 c4 = texture(currentColor, uv + vec2( 0,  1) * texelSize).rgb;
    
    vec3 blurred = (c0 + c1 + c2 + c3 + c4) / 5.0;
    return color + (color - blurred) * sharpness;
}

void main() {
    vec2 texelSize = 1.0 / textureSize(currentColor, 0);
    
    vec3 current = texture(currentColor, vScreenUV).rgb;
    vec2 motion = texture(motionVector, vScreenUV).rg;
    vec2 historyUV = vScreenUV - motion;
    
    bool validHistory = (historyUV.x >= 0.0 && historyUV.x <= 1.0 &&
                         historyUV.y >= 0.0 && historyUV.y <= 1.0);
    
    vec3 finalColor = current;
    
    if (validHistory) {
        vec3 history = sampleCatmullRom(historyColor, historyUV, texelSize);
        vec3 clippedHistory = clipHistory(history, current, vScreenUV, texelSize);
        
        float blendFactor = scene.taaParams.x;
        finalColor = mix(clippedHistory, current, blendFactor);
        
        if (scene.taaParams.z > 0.5) {
            float sharpness = scene.taaParams.y;
            finalColor = sharpen(finalColor, vScreenUV, texelSize, sharpness);
        }
    }
    
    outColor = vec4(finalColor, 1.0);
}
```

- [ ] **步骤 5：Commit**

```powershell
git add Shaders/Deferred/TAAPass.vert Shaders/Deferred/TAAPass.frag
git commit -m "feat(shader): add TAA Pass shaders with Variance Clipping"
```

---

## 任务 8：创建 TAA Pipeline

**文件：**
- 修改：`Source/Engine/Deferred/FDeferredRenderer.cpp`

- [ ] **步骤 1：实现 CreateTAAPipeline 方法**

```cpp
bool FDeferredRenderer::CreateTAAPipeline() {
    // Load shaders
    auto vertShader = m_device->createShader("Shaders/Deferred/TAAPass.vert", EShaderStage::Vertex);
    auto fragShader = m_device->createShader("Shaders/Deferred/TAAPass.frag", EShaderStage::Fragment);
    
    if (!vertShader || !fragShader) {
        MR_LOG_ERROR("Failed to load TAA shaders");
        return false;
    }
    
    // Create pipeline state
    PipelineStateDesc pipelineDesc;
    pipelineDesc.vertexShader = vertShader;
    pipelineDesc.fragmentShader = fragShader;
    pipelineDesc.primitiveTopology = EPrimitiveTopology::TriangleList;
    pipelineDesc.depthTest = false;
    pipelineDesc.depthWrite = false;
    pipelineDesc.cullMode = ECullMode::None;
    
    m_taaPipeline = m_device->createPipelineState(pipelineDesc);
    if (!m_taaPipeline) {
        MR_LOG_ERROR("Failed to create TAA pipeline");
        return false;
    }
    
    MR_LOG_INFO("TAA pipeline created successfully");
    return true;
}
```

- [ ] **步骤 2：在 Initialize 中调用**

在 `Initialize()` 方法中添加：

```cpp
if (!CreateTAAPipeline()) {
    MR_LOG_ERROR("Failed to create TAA pipeline");
    return false;
}
```

- [ ] **步骤 3：编译验证**

```powershell
& "E:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" MonsterEngine.sln /t:Build /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:minimal 2>&1 | Select-Object -Last 10
```

预期：编译成功

- [ ] **步骤 4：Commit**

```powershell
git add Source/Engine/Deferred/FDeferredRenderer.cpp
git commit -m "feat(deferred): create TAA pipeline"
```

---

## 任务 9：实现 TAA 渲染流程

**文件：**
- 修改：`Source/Engine/Deferred/FDeferredRenderer.cpp`

- [ ] **步骤 1：修改 UpdateSceneUBO 添加 TAA 参数**

```cpp
void FDeferredRenderer::UpdateSceneUBO(const Camera& camera, const SceneLighting& lighting) {
    FDeferredSceneUBO sceneData;
    
    // 现有数据
    sceneData.InvViewProj = (camera.viewMatrix * camera.projectionMatrix).Inverse();
    sceneData.CameraPos = FVector4f(camera.position, 1.0f);
    sceneData.DirLightDirection = FVector4f(lighting.dirLight.direction, 0.0f);
    sceneData.DirLightColorIntensity = FVector4f(lighting.dirLight.color, lighting.dirLight.intensity);
    sceneData.PointLightPositionRadius = FVector4f(lighting.pointLight.position, lighting.pointLight.radius);
    sceneData.PointLightColorIntensity = FVector4f(lighting.pointLight.color, lighting.pointLight.intensity);
    sceneData.Ambient = FVector4f(lighting.ambientFactor, 0.0f, 0.0f, 0.0f);
    
    // TAA 数据
    sceneData.PreviousViewProj = m_previousViewProj;
    sceneData.JitterOffset = FVector4f(m_currentJitter.x, m_currentJitter.y, 
                                       m_previousJitter.x, m_previousJitter.y);
    sceneData.TAAParams = FVector4f(m_taaConfig.blendFactor, m_taaConfig.sharpness,
                                    m_taaConfig.enableSharpening ? 1.0f : 0.0f, 0.0f);
    
    void* mappedData = m_sceneUBO->map();
    memcpy(mappedData, &sceneData, sizeof(FDeferredSceneUBO));
    m_sceneUBO->unmap();
}
```

- [ ] **步骤 2：实现 RenderTAAPass 方法**

```cpp
void FDeferredRenderer::RenderTAAPass(IRHICommandList* cmdList) {
    cmdList->setPipelineState(m_taaPipeline);
    
    cmdList->setTexture(0, m_lightingTarget);
    cmdList->setTexture(1, m_motionVectorTarget);
    cmdList->setTexture(2, m_historyTarget);
    cmdList->setUniformBuffer(3, m_sceneUBO);
    
    cmdList->setRenderTarget(0, m_device->getCurrentBackBuffer());
    
    cmdList->draw(3, 0);
}
```

- [ ] **步骤 3：实现 CopyToHistory 方法**

```cpp
void FDeferredRenderer::CopyToHistory(IRHICommandList* cmdList) {
    cmdList->blitTexture(m_device->getCurrentBackBuffer(), m_historyTarget);
}
```

- [ ] **步骤 4：修改 Render 方法集成 TAA**

```cpp
void FDeferredRenderer::Render(IRHICommandList* cmdList, const TArray<RenderObject>& objects,
                                const Camera& camera, const SceneLighting& lighting) {
    // Generate jitter
    m_currentJitter = GenerateJitter(m_frameIndex);
    
    // Apply jitter to projection
    FMatrix44f jitteredProj = ApplyJitter(camera.projectionMatrix, m_currentJitter, m_width, m_height);
    
    // Update UBOs
    UpdateTransformUBO(camera, jitteredProj);
    UpdateSceneUBO(camera, lighting);
    
    // Render Geometry Pass
    RenderGeometryPass(cmdList, objects);
    
    // Render Lighting Pass to Lighting RT
    cmdList->setRenderTarget(0, m_lightingTarget);
    RenderLightingPass(cmdList);
    
    // Render TAA Pass to Swapchain
    RenderTAAPass(cmdList);
    
    // Copy to history
    CopyToHistory(cmdList);
    
    // Update previous frame data
    m_previousJitter = m_currentJitter;
    m_previousViewProj = camera.viewMatrix * camera.projectionMatrix;
    m_frameIndex++;
}
```

- [ ] **步骤 5：编译验证**

```powershell
& "E:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" MonsterEngine.sln /t:Build /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:minimal 2>&1 | Select-Object -Last 10
```

预期：编译成功

- [ ] **步骤 6：Commit**

```powershell
git add Source/Engine/Deferred/FDeferredRenderer.cpp
git commit -m "feat(deferred): integrate TAA into rendering pipeline"
```

---

## 任务 10：实现错误处理

**文件：**
- 修改：`Source/Engine/Deferred/FDeferredRenderer.cpp`

- [ ] **步骤 1：实现 OnResize 方法**

```cpp
void FDeferredRenderer::OnResize(uint32 newWidth, uint32 newHeight) {
    if (newWidth == m_width && newHeight == m_height) {
        return;
    }
    
    MR_LOG_INFO("Resizing renderer: %ux%u -> %ux%u", m_width, m_height, newWidth, newHeight);
    
    ReleaseGBuffer();
    ReleaseTAAResources();
    
    m_width = newWidth;
    m_height = newHeight;
    
    CreateGBuffer();
    CreateTAAResources();
    
    m_frameIndex = 0;
    
    MR_LOG_INFO("Resize completed");
}
```

- [ ] **步骤 2：实现 OnSceneChanged 方法**

```cpp
void FDeferredRenderer::OnSceneChanged() {
    IRHICommandList* cmdList = m_device->createCommandList();
    cmdList->clearRenderTarget(m_historyTarget, FVector4f(0, 0, 0, 1));
    m_device->submitCommandList(cmdList);
    
    m_frameIndex = 0;
    
    MR_LOG_INFO("Scene changed, TAA history cleared");
}
```

- [ ] **步骤 3：编译验证**

```powershell
& "E:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" MonsterEngine.sln /t:Build /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:minimal 2>&1 | Select-Object -Last 10
```

预期：编译成功

- [ ] **步骤 4：Commit**

```powershell
git add Source/Engine/Deferred/FDeferredRenderer.cpp
git commit -m "feat(deferred): add error handling for resize and scene change"
```

---

## 任务 11：运行并验证 TAA 功能

**文件：**
- 无

- [ ] **步骤 1：运行程序**

```powershell
cd e:\MonsterEngine
.\x64\Debug\MonsterEngine.exe --deferred 2>&1
```

预期：程序启动，显示延迟渲染场景，TAA 自动启用

- [ ] **步骤 2：检查日志**

```powershell
Get-Content E:\MonsterEngine\MonsterEngine.log -Tail 20
```

预期：看到 "TAA resources created"、"TAA pipeline created" 等日志

- [ ] **步骤 3：使用 RenderDoc 捕获一帧**

```powershell
& "C:\Program Files\RenderDoc\renderdoccmd.exe" capture --working-dir "E:\MonsterEngine" "E:\MonsterEngine\x64\Debug\MonsterEngine.exe" --deferred 2>&1
```

预期：成功捕获，可以查看 GBuffer、Lighting RT、TAA Pass

- [ ] **步骤 4：验证 Motion Vector**

在 RenderDoc 中检查 Motion Vector RT，应该看到：
- 静止物体：Motion Vector 接近 (0, 0)
- 移动物体：Motion Vector 显示运动方向

- [ ] **步骤 5：验证 TAA 效果**

对比 TAA 开启前后的画面质量：
- 边缘应该更平滑
- 无明显拖影
- 8 帧后画面稳定

---

## 任务 12：性能测试

**文件：**
- 测试：`Tests/Engine/Deferred/TAATests.cpp`

- [ ] **步骤 1：添加性能测试**

在 `TAATests.cpp` 添加：

```cpp
TEST(TAAPerformanceTests, FrameTime) {
    auto device = CreateTestDevice();
    FDeferredRenderer renderer;
    renderer.Initialize(device.get(), 1920, 1080);
    
    auto cmdList = device->createCommandList();
    
    // Warm up
    for (uint32 i = 0; i < 10; i++) {
        renderer.Render(cmdList, testObjects, testCamera, testLighting);
    }
    
    // Benchmark
    auto startTime = std::chrono::high_resolution_clock::now();
    const uint32 frameCount = 100;
    
    for (uint32 i = 0; i < frameCount; i++) {
        renderer.Render(cmdList, testObjects, testCamera, testLighting);
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    
    float avgFrameTime = static_cast<float>(duration) / frameCount;
    
    MR_LOG_INFO("TAA Performance: %.2f ms/frame (%.1f FPS)", avgFrameTime, 1000.0f / avgFrameTime);
    
    EXPECT_LT(avgFrameTime, 20.0f);
}
```

- [ ] **步骤 2：运行性能测试**

```powershell
.\x64\Debug\MonsterEngineTests.exe --gtest_filter=TAAPerformanceTests.* 2>&1
```

预期：TAA Pass < 2ms，整体帧率 > 50 FPS

- [ ] **步骤 3：Commit**

```powershell
git add Tests/Engine/Deferred/TAATests.cpp
git commit -m "test(deferred): add TAA performance tests"
```

---

## 任务 13：最终验收

**文件：**
- 无

- [ ] **步骤 1：运行所有单元测试**

```powershell
.\x64\Debug\MonsterEngineTests.exe --gtest_filter=TAATests.* 2>&1
```

预期：所有测试通过

- [ ] **步骤 2：视觉验证测试**

运行程序并验证以下场景：
- ✅ 静态场景：8 帧后完全稳定
- ✅ 相机旋转：边缘平滑，无明显拖影
- ✅ 物体移动：Motion Vector 正确，边缘平滑

- [ ] **步骤 3：性能验收**

验证性能指标：
- ✅ TAA Pass < 2ms (1080p)
- ✅ 内存增加 ~24MB (1080p)
- ✅ 整体帧率 > 50 FPS

- [ ] **步骤 4：代码审查**

检查代码质量：
- ✅ 所有注释使用英文
- ✅ 符合 MonsterEngine 代码规范
- ✅ 无内存泄漏
- ✅ 无编译警告

- [ ] **步骤 5：最终 Commit**

```powershell
git add -A
git commit -m "feat(deferred): complete TAA implementation with all 6 modules"
git push origin feature_deferred_shader
```

---

## 验收标准

### 功能性
- ✅ Halton 序列生成正确
- ✅ Jitter 应用到投影矩阵
- ✅ Motion Vector 正确计算
- ✅ 历史帧正确混合
- ✅ Variance Clipping 工作正常
- ✅ 锐化功能可选启用

### 质量
- ✅ 静态场景：8 帧后完全稳定
- ✅ 相机旋转：边缘平滑，无明显拖影
- ✅ 物体移动：Motion Vector 正确，边缘平滑
- ✅ 抗锯齿质量：明显优于无 AA

### 性能
- ✅ TAA Pass 耗时 < 2ms (1080p)
- ✅ 内存增加 ~24MB (1080p)
- ✅ 整体帧率 > 50 FPS (1080p, 简单场景)

### 鲁棒性
- ✅ 首帧渲染正常（无历史数据）
- ✅ 窗口 Resize 正常
- ✅ 场景切换正常
- ✅ 无内存泄漏
- ✅ 无 GPU 错误

---

**预计完成时间**：4 天（19 小时）

**下一步**：选择执行方式（子代理驱动或内联执行）
