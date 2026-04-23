# MonsterEngine 延迟渲染 MVP 设计文档

**版本**：v1.0  
**日期**：2026-04-23  
**状态**：设计确认，待实现

---

## 1. 背景与目标

### 1.1 背景
MonsterEngine 目前已实现 Forward Rendering 路径（`CubeSceneApplication::renderWithRDG`），但缺少 Deferred Rendering 实现。面试高频考点需要实际的延迟渲染经验。

### 1.2 MVP 目标
- **最小闭环**：Cube + Floor 场景用 Deferred 渲染
- **光源**：1 平行光 + 1 点光源
- **光照模型**：Blinn-Phong
- **仅支持 Vulkan**（OpenGL 后补）
- **不做**：Shadow / PBR / MSAA / 半透明

### 1.3 非目标
- 不修改现有骨架代码（`FDeferredShadingSceneRenderer` 等）
- 不扩展 `EPixelFormat`（MVP 用 RGBA32F 绕过）
- 不重构 Scene/Primitive 系统

---

## 2. 架构决策

### 2.1 绕开骨架代码

**问题**：现有 `FDeferredShadingSceneRenderer`、`FDeferredShadingRenderer` 都是空壳。

**决策**：在 `CubeSceneApplication` 新增 `renderWithDeferred()` 方法，模仿 `renderWithRDG` 的模式。

### 2.2 复用 RDG 系统

RDG ([FRDGBuilder](cci:2://file:///e:/MonsterEngine/Include/RDG/RDGBuilder.h:77:0-304:1)) 已完整，直接用它管理：
- GBuffer 纹理创建（自动生命周期）
- 两个 Pass 的依赖（RAW barrier 自动插入）
- 资源状态转换（`ERHIAccess::RTV` → `ERHIAccess::SRVGraphics`）

### 2.3 复用 Cube Shader 加载模式

`FForwardShaderCompiler` 是 TODO 不能用。参考 `FCubeSceneProxy::CreateShaders` 的模式：

```cpp
std::vector<uint8> spv = ShaderCompiler::readFileBytes("Shaders/xxx.spv");
shader = Device->createVertexShader(TSpan<const uint8>(spv));
```

---

## 3. GBuffer 设计

### 3.1 布局

| RT           | 格式              | 内容                       | 大小/像素 |
| ------------ | ----------------- | -------------------------- | --------- |
| RT0: gNormal | RGBA32F           | 世界法线 (xyz)，w 保留     | 16 bytes  |
| RT1: gAlbedo | RGBA8             | Albedo (rgb)，w 未用       | 4 bytes   |
| Depth        | D24_UNORM_S8_UINT | 自动写入，供 Position 重建 | 4 bytes   |

**总大小（1920×1080）**：24 bytes × 2,073,600 ≈ **50 MB**

### 3.2 为什么用 RGBA32F 存 Normal？

**`EPixelFormat` 枚举缺少 RGBA16F**（`@e:\MonsterEngine\Include\RHI\RHIDefinitions.h:360`）。MVP 用 RGBA32F 绕过，后续优化可：
- 方案 A：扩展 `EPixelFormat` 加 `R16G16B16A16_FLOAT`
- 方案 B：Normal 压缩为 RG16F（八面体编码）

---

## 4. Shader 设计

### 4.1 GeometryPass.vert

**职责**：MVP 变换，世界空间数据传递给 frag

```glsl
#version 450 core

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(set = 0, binding = 0) uniform TransformUBO {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 modelInverseTranspose;
    vec4 cameraPos;
} ubo;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vWorldNormal;
layout(location = 2) out vec2 vTexCoord;

void main() {
    vec4 worldPos4 = ubo.model * vec4(inPosition, 1.0);
    vWorldPos = worldPos4.xyz;
    vWorldNormal = normalize(mat3(ubo.modelInverseTranspose) * inNormal);
    vTexCoord = inTexCoord;
    gl_Position = ubo.proj * ubo.view * worldPos4;
}
```

### 4.2 GeometryPass.frag

**职责**：输出 GBuffer（2 个 RT）

```glsl
#version 450 core

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vWorldNormal;
layout(location = 2) in vec2 vTexCoord;

layout(set = 0, binding = 1) uniform sampler2D albedoMap;

layout(location = 0) out vec4 outNormal;
layout(location = 1) out vec4 outAlbedo;

void main() {
    outNormal = vec4(normalize(vWorldNormal), 0.0);
    outAlbedo = texture(albedoMap, vTexCoord);
}
```

### 4.3 LightingPass.vert

**职责**：用 `gl_VertexIndex` 生成全屏三角形，不需要 VBO

```glsl
#version 450 core

layout(location = 0) out vec2 vScreenUV;

void main() {
    vec2 uv = vec2(
        (gl_VertexIndex << 1) & 2,
        gl_VertexIndex & 2
    );
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
    vScreenUV = uv;
}

// CPU: cmdList->draw(3, 0);
```

### 4.4 LightingPass.frag

**职责**：采样 GBuffer，Position 重建，Blinn-Phong 光照

```glsl
#version 450 core

layout(location = 0) in vec2 vScreenUV;

layout(set = 0, binding = 0) uniform sampler2D gNormal;
layout(set = 0, binding = 1) uniform sampler2D gAlbedo;
layout(set = 0, binding = 2) uniform sampler2D gDepth;

layout(set = 0, binding = 3) uniform SceneUBO {
    mat4 invViewProj;
    vec4 cameraPos;
    vec4 dirLightDirection;
    vec4 dirLightColorIntensity;
    vec4 pointLightPositionRadius;
    vec4 pointLightColorIntensity;
    vec4 ambient;
} scene;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 normal = texture(gNormal, vScreenUV).xyz;
    vec3 albedo = texture(gAlbedo, vScreenUV).rgb;
    float depth = texture(gDepth, vScreenUV).r;
    
    if (depth >= 1.0) {
        outColor = vec4(0.1, 0.1, 0.15, 1.0);
        return;
    }
    
    // Position 重建（Vulkan Depth 范围是 [0, 1]，无需再映射）
    vec4 clipPos = vec4(vScreenUV * 2.0 - 1.0, depth, 1.0);
    vec4 worldPos4 = scene.invViewProj * clipPos;
    vec3 worldPos = worldPos4.xyz / worldPos4.w;
    
    vec3 N = normalize(normal);
    vec3 V = normalize(scene.cameraPos.xyz - worldPos);
    
    // 环境光
    vec3 ambientColor = albedo * scene.ambient.x;
    
    // 平行光
    vec3 L = -scene.dirLightDirection.xyz;
    vec3 H = normalize(L + V);
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    vec3 dirDiffuse = NdotL * scene.dirLightColorIntensity.xyz * albedo;
    vec3 dirSpecular = pow(NdotH, 32.0) * scene.dirLightColorIntensity.xyz;
    vec3 dirLight = (dirDiffuse + dirSpecular) * scene.dirLightColorIntensity.w;
    
    // 点光源
    vec3 lightPos = scene.pointLightPositionRadius.xyz;
    float radius = scene.pointLightPositionRadius.w;
    vec3 toLight = worldPos - lightPos;
    float distance = length(toLight);
    float attenuation = clamp(1.0 - distance / radius, 0.0, 1.0);
    attenuation *= attenuation;
    
    vec3 pL = -normalize(toLight);
    vec3 pH = normalize(pL + V);
    float pNdotL = max(dot(N, pL), 0.0);
    float pNdotH = max(dot(N, pH), 0.0);
    vec3 pointDiffuse = pNdotL * scene.pointLightColorIntensity.xyz * albedo;
    vec3 pointSpecular = pow(pNdotH, 32.0) * scene.pointLightColorIntensity.xyz;
    vec3 pointLight = (pointDiffuse + pointSpecular) * scene.pointLightColorIntensity.w * attenuation;
    
    outColor = vec4(ambientColor + dirLight + pointLight, 1.0);
}
```

---

## 5. Uniform Buffer 布局（std140）

### 5.1 TransformUBO (272 bytes)

| Offset | Size | 字段                  | C++ 类型     |
| ------ | ---- | --------------------- | ------------ |
| 0      | 64   | Model                 | `FMatrix44f` |
| 64     | 64   | View                  | `FMatrix44f` |
| 128    | 64   | Proj                  | `FMatrix44f` |
| 192    | 64   | ModelInverseTranspose | `FMatrix44f` |
| 256    | 16   | CameraPos             | `FVector4f`  |

### 5.2 SceneUBO (160 bytes)

| Offset | Size | 字段                     | 说明                        |
| ------ | ---- | ------------------------ | --------------------------- |
| 0      | 64   | InvViewProj              | 逆 ViewProj 矩阵            |
| 64     | 16   | CameraPos                | xyz = pos，w 未用           |
| 80     | 16   | DirLightDirection        | xyz = dir，w 未用           |
| 96     | 16   | DirLightColorIntensity   | xyz = color，w = intensity  |
| 112    | 16   | PointLightPositionRadius | xyz = pos，w = radius       |
| 128    | 16   | PointLightColorIntensity | xyz = color，w = intensity  |
| 144    | 16   | Ambient                  | x = ambientFactor，yzw 未用 |

### 5.3 C++ Struct（完整定义）

```cpp
// Include/Engine/Deferred/DeferredUniformTypes.h
#pragma once
#include "Math/Matrix.h"
#include "Math/Vector4.h"

namespace MonsterEngine::Deferred {

struct alignas(16) FDeferredTransformUBO {
    Math::FMatrix44f Model;
    Math::FMatrix44f View;
    Math::FMatrix44f Proj;
    Math::FMatrix44f ModelInverseTranspose;
    Math::FVector4f  CameraPos;
};
static_assert(sizeof(FDeferredTransformUBO) == 272);
static_assert(offsetof(FDeferredTransformUBO, CameraPos) == 256);

struct alignas(16) FDeferredSceneUBO {
    Math::FMatrix44f InvViewProj;
    Math::FVector4f  CameraPos;
    Math::FVector4f  DirLightDirection;
    Math::FVector4f  DirLightColorIntensity;
    Math::FVector4f  PointLightPositionRadius;
    Math::FVector4f  PointLightColorIntensity;
    Math::FVector4f  Ambient;
};
static_assert(sizeof(FDeferredSceneUBO) == 160);
static_assert(offsetof(FDeferredSceneUBO, Ambient) == 144);

} // namespace MonsterEngine::Deferred
```

---

## 6. 文件组织

```
📁 新增文件：
Shaders/Deferred/
├── GeometryPass.vert
├── GeometryPass.frag
├── LightingPass.vert
└── LightingPass.frag

Include/Engine/Deferred/
├── DeferredUniformTypes.h    (UBO struct 定义)
└── FDeferredRenderer.h       (封装延迟渲染逻辑)

Source/Engine/Deferred/
└── FDeferredRenderer.cpp

📝 修改文件：
Include/CubeSceneApplication.h      (+ renderWithDeferred 声明)
Source/CubeSceneApplication.cpp     (+ renderWithDeferred 实现)
MonsterEngine.vcxproj               (把新文件加到项目)
```

---

## 7. 实施步骤

### Step 1: Shader 层（1-2h）
- [ ] 写 4 个 GLSL Shader
- [ ] 用 glslc 编译成 .spv
- [ ] 验证 Shader 能加载

### Step 2: C++ 基础设施（3-4h）
- [ ] `DeferredUniformTypes.h`（UBO struct + static_assert）
- [ ] `FDeferredRenderer` 类声明
- [ ] `CreateGeometryPipeline()`（模仿 CubeSceneProxy）
- [ ] `CreateLightingPipeline()`
- [ ] Pipeline 的 MRT 配置（`renderTargetFormats` 加 2 个）

### Step 3: RDG 集成（2-3h）
- [ ] `CubeSceneApplication::renderWithDeferred()` 函数
- [ ] 用 RDG 创建 3 个 GBuffer 纹理
- [ ] GeometryPass（`builder.writeTexture` × 2 + `writeDepth`）
- [ ] LightingPass（`builder.readTexture` × 3 + `readDepth`）
- [ ] 命令行开关 `--deferred`

### Step 4: 调试验证（3-4h）
- [ ] GBuffer 可视化模式（输出 Normal 作为颜色）
- [ ] 光照正确性对比 Forward
- [ ] RenderDoc 抓帧确认 GBuffer 内容

**总计**：10-12 小时

---

## 8. 风险与缓解

| 风险                 | 影响              | 缓解方案                   |
| -------------------- | ----------------- | -------------------------- |
| uniform 对齐错误     | 画面花屏          | `static_assert` 编译时检查 |
| Vulkan Y 轴翻转      | 画面上下颠倒      | 继承 Cube 的 viewport 设置 |
| MRT Pipeline 未测试  | GBuffer 写入失败  | Step 2 先写测试：单色输出  |
| invViewProj 矩阵错误 | Position 重建错乱 | Step 4 可视化 WorldPos     |

---

## 9. 验收标准

### MVP 完成标志
- [ ] `--deferred` 命令行能跑通
- [ ] Cube + Floor 场景正确显示
- [ ] 光照和 Forward 路径视觉一致
- [ ] RenderDoc 能看到 2 个 RT 的 GBuffer
- [ ] 无 Vulkan Validation Error

### 性能目标（非强制）
- [ ] 1080p 稳定 60 FPS
- [ ] GBuffer 占用 < 100 MB

---

## 10. 后续迭代方向

### Phase 2（MVP 之后）
- 多光源支持（动态数组）
- Shadow 集成
- PBR 材质
- 扩展 `EPixelFormat` 加 RGBA16F

### Phase 3（远期）
- Forward+ 对比方案
- Tiled Deferred
- 面试 Demo 录屏

---

**附录 A：参考代码位置**

- RDG 使用参考：`@e:\MonsterEngine\Source\CubeSceneApplication.cpp:2247-2429`
- Shader 加载参考：`@e:\MonsterEngine\Source\Engine\Proxies\CubeSceneProxy.cpp:362-476`
- Pipeline 创建参考：`@e:\MonsterEngine\Source\Engine\Proxies\CubeSceneProxy.cpp:479-547`
- Vulkan MRT 支持：`@e:\MonsterEngine\Source\Platform\Vulkan\VulkanCommandListContext.cpp:243-500`
- EPixelFormat 枚举：`@e:\MonsterEngine\Include\RHI\RHIDefinitions.h:360-386`