# MonsterRender 引擎一年开发计划（2025-2026）

## 📋 文档概述

**目标**: 在一年内将 MonsterRender 打造成一个功能完整的现代渲染引擎，具备多线程渲染、前向/延迟管线、实时阴影等核心功能，对标 UE5 渲染架构。

**版本**: 1.0  
**制定日期**: 2025-11-20  
**计划周期**: 2025 年 1 月 - 2026 年 1 月（12 个月）  
**参考架构**: UE5 Rendering Architecture  

---

## 🎯 终极目标

### 核心功能
- ✅ 完整的 RHI 抽象层（支持 Vulkan、D3D12）
- ✅ 多线程渲染架构
- ✅ 前向渲染管线（Forward+）
- ✅ 延迟渲染管线（Deferred Shading）
- ✅ 实时阴影系统（CSM、PCSS、VSM）
- ✅ 基于物理的渲染（PBR）
- ✅ 后处理系统（Bloom、Tone Mapping、SSAO 等）
- ✅ GPU 驱动渲染（Indirect Drawing）
- ✅ 资源流送系统
- ✅ 场景图与剔除系统

### 性能目标
- 60 FPS @ 1080p（中等复杂场景）
- 支持 10,000+ 绘制调用/帧（GPU Driven）
- < 16ms 帧时间（含 CPU + GPU）
- 多线程命令录制（3-4 个工作线程）

### 平台支持
- Windows 10/11 (x64)
- Linux (未来)
- Vulkan 1.2+
- D3D12 (未来)

---

## 📅 季度规划概览

```
Q1 (1-3月)   Q2 (4-6月)      Q3 (7-9月)        Q4 (10-12月)
┌─────────┐  ┌──────────┐   ┌───────────┐    ┌───────────┐
│基础渲染 │  │高级渲染  │   │延迟管线   │    │生产工具   │
│架构完善 │→ │前向管线  │ → │多线程架构 │ →  │性能优化   │
│材质系统 │  │阴影系统  │   │GPU 驱动   │    │编辑器基础 │
└─────────┘  └──────────┘   └───────────┘    └───────────┘
```

---

## 🗓️ 第一季度（1-3月）：基础渲染架构完善

### 主题：打牢地基，完善基础渲染管线

### 第 1 个月：渲染架构重构与材质系统（1月）

#### Week 1-2: 场景图与渲染抽象

**目标**: 建立完整的场景管理和渲染抽象层

**任务清单**:
- [ ] **场景图系统**
  - 实现 `Scene` 类：管理所有渲染对象
  - 实现 `SceneNode` 层次结构：变换、父子关系
  - 实现 `MeshComponent`、`LightComponent`
  - 场景遍历和查询接口
  
- [ ] **渲染抽象层**
  - 实现 `RenderProxy` 系统（类似 UE5 的 Proxy）
  - 实现 `RenderScene`：渲染线程的场景表示
  - 实现 `PrimitiveSceneProxy`：图元的渲染表示
  - 建立游戏线程和渲染线程的数据同步机制

- [ ] **网格系统**
  - 实现 `StaticMesh` 类
  - 实现网格加载器（支持 OBJ、glTF）
  - 实现 `MeshBuilder` 工具类
  - 顶点格式抽象（位置、法线、UV、切线等）

**参考 UE5 文件**:
- `Engine/Source/Runtime/Engine/Public/SceneManagement.h`
- `Engine/Source/Runtime/Renderer/Private/ScenePrivate.h`

**验收标准**:
- ✅ 能加载并显示多个网格模型
- ✅ 支持场景层次变换
- ✅ 渲染线程与游戏线程分离

---

#### Week 3-4: 材质系统基础

**目标**: 建立材质抽象和着色器系统

**任务清单**:
- [ ] **材质系统**
  - 实现 `Material` 基类
  - 实现 `MaterialInstance`：材质实例化
  - 材质参数系统（标量、向量、纹理）
  - 材质编辑接口（运行时修改参数）

- [ ] **着色器系统重构**
  - 实现 `ShaderCompiler`：GLSL → SPIR-V 编译
  - 实现着色器反射（解析 SPIR-V）
  - 实现 `ShaderCache`：编译缓存
  - 着色器热重载支持

- [ ] **纹理系统增强**
  - 纹理加载（PNG、JPG、KTX2）
  - Mipmap 生成
  - 纹理压缩（BC7）
  - 立方体纹理支持

**示例材质**:
```cpp
// 创建材质
auto material = MakeShared<Material>("PBR_Standard");
material->setShader("Shaders/PBR.vert.spv", "Shaders/PBR.frag.spv");

// 设置参数
auto matInstance = material->createInstance();
matInstance->setParameter("baseColor", Vector4(1.0f, 0.5f, 0.2f, 1.0f));
matInstance->setTexture("albedoMap", albedoTexture);
```

**参考 UE5**:
- `Engine/Source/Runtime/Engine/Public/Materials/Material.h`
- `Engine/Source/Runtime/ShaderCore/`

**验收标准**:
- ✅ 支持多种材质参数
- ✅ 着色器热重载工作正常
- ✅ 纹理加载和采样正常

---

### 第 2 个月：光照系统与前向渲染（2月）

#### Week 1-2: 光照系统

**目标**: 实现完整的光照抽象和计算

**任务清单**:
- [ ] **光源类型**
  - 实现 `DirectionalLight`（方向光）
  - 实现 `PointLight`（点光源）
  - 实现 `SpotLight`（聚光灯）
  - 实现 `SkyLight`（天空光/IBL）

- [ ] **光照数据结构**
  - 光源 Uniform Buffer（支持多光源）
  - 光照参数打包优化
  - 光源剔除（视锥体剔除）

- [ ] **基础光照着色器**
  - Blinn-Phong 光照模型
  - Lambert 漫反射
  - Specular 高光
  - 环境光遮蔽（简单版）

**光源数据结构示例**:
```cpp
struct LightData {
    Vector4 position;      // w: type (0=directional, 1=point, 2=spot)
    Vector4 direction;     // w: range
    Vector4 color;         // w: intensity
    Vector4 parameters;    // spotAngle, attenuation, etc.
};
```

**参考 UE5**:
- `Engine/Source/Runtime/Renderer/Private/Lights/`

**验收标准**:
- ✅ 支持至少 3 种光源类型
- ✅ 多光源场景渲染正确
- ✅ 光源参数可实时调整

---

#### Week 3-4: 前向渲染管线

**目标**: 实现完整的前向渲染流程

**任务清单**:
- [ ] **渲染通道系统**
  - 实现 `RenderPass` 抽象
  - 深度预通道（Depth Prepass）
  - 不透明物体通道（Opaque Pass）
  - 透明物体通道（Transparent Pass）
  - 天空盒通道（Skybox Pass）

- [ ] **排序与批处理**
  - 深度排序（前向后）
  - 材质排序（减少状态切换）
  - 实例化合批（相同网格）

- [ ] **渲染器架构**
  - 实现 `Renderer` 类
  - 实现 `RenderQueue` 系统
  - 实现 `DrawCall` 收集
  - 视锥体剔除

**渲染流程**:
```
1. 深度预通道 (Depth Prepass)
   ↓
2. 不透明物体渲染 (Opaque)
   ↓
3. 天空盒渲染 (Skybox)
   ↓
4. 透明物体渲染 (Transparent, 后向前排序)
```

**参考 UE5**:
- `Engine/Source/Runtime/Renderer/Private/BasePassRendering.cpp`

**验收标准**:
- ✅ 完整的前向渲染管线
- ✅ 正确的渲染排序
- ✅ 支持透明和不透明物体

---

### 第 3 个月：PBR 材质与 IBL（3月）

#### Week 1-2: PBR 材质系统

**目标**: 实现基于物理的材质渲染

**任务清单**:
- [ ] **PBR 理论实现**
  - Cook-Torrance BRDF
  - GGX 法线分布函数（NDF）
  - Schlick-GGX 几何遮蔽（G）
  - Fresnel-Schlick 菲涅尔项（F）

- [ ] **PBR 材质参数**
  - Albedo（基础色）
  - Metallic（金属度）
  - Roughness（粗糙度）
  - Normal Map（法线贴图）
  - Ambient Occlusion（AO）
  - Emissive（自发光）

- [ ] **PBR 着色器**
  - 实现 PBR 顶点着色器
  - 实现 PBR 片段着色器
  - 法线贴图计算（切线空间）

**PBR 公式参考**:
```glsl
// Cook-Torrance BRDF
vec3 BRDF = (kD * albedo / PI) + (DFG / (4 * NdotV * NdotL));

// D: GGX Normal Distribution
float D_GGX(float NdotH, float roughness);

// F: Fresnel-Schlick
vec3 F_Schlick(float VdotH, vec3 F0);

// G: Smith's Geometry Shadowing
float G_Smith(float NdotV, float NdotL, float roughness);
```

**参考**:
- [LearnOpenGL PBR](https://learnopengl.com/PBR/Theory)
- UE5 的 `StandardShading.ush`

**验收标准**:
- ✅ 材质看起来真实（金属/非金属）
- ✅ 粗糙度和金属度参数工作正常
- ✅ 法线贴图正确

---

#### Week 3-4: 图像基础光照（IBL）

**目标**: 实现环境光照和反射

**任务清单**:
- [ ] **环境贴图**
  - HDR 环境贴图加载（.hdr）
  - 立方体贴图生成
  - 全景转立方体工具

- [ ] **IBL 预计算**
  - Irradiance Map（辐照度贴图）：卷积计算
  - Prefiltered Environment Map（预过滤环境贴图）
  - BRDF Integration Map（BRDF 积分贴图）

- [ ] **IBL 着色器集成**
  - 漫反射 IBL（Irradiance）
  - 镜面反射 IBL（Prefiltered）
  - Split Sum Approximation

**IBL 预处理流程**:
```
HDR Environment Map
    ↓
Cubemap Conversion
    ↓
├─→ Irradiance Convolution (漫反射)
└─→ Specular Prefilter (镜面反射)
    
Separate: BRDF LUT Generation
```

**参考**:
- [LearnOpenGL IBL](https://learnopengl.com/PBR/IBL/Diffuse-irradiance)
- UE5 的 `ReflectionEnvironment.cpp`

**验收标准**:
- ✅ 支持 HDR 环境贴图
- ✅ 环境光照和反射真实
- ✅ 性能可接受（预计算离线）

---

### Q1 里程碑

**完成标志**:
- ✅ 场景图系统完整
- ✅ 材质系统可用
- ✅ PBR 光照正确
- ✅ 前向渲染管线稳定

**演示场景**:
- 显示 5-10 个不同材质的 PBR 球体
- 动态光源照亮场景
- 环境光照和反射真实

**代码质量**:
- 所有代码有注释
- 关键模块有单元测试
- 性能达标（60 FPS @ 简单场景）

---

## 🗓️ 第二季度（4-6月）：高级渲染与阴影系统

### 主题：阴影、天空、后处理

### 第 4 个月：实时阴影系统（4月）

#### Week 1-2: 阴影贴图基础

**目标**: 实现基础阴影贴图技术

**任务清单**:
- [ ] **阴影贴图渲染**
  - 实现 `ShadowMap` 类
  - 深度渲染到纹理
  - 光源视角矩阵计算
  - 阴影 Bias 调整

- [ ] **PCF 软阴影**
  - Percentage Closer Filtering (PCF)
  - 可调节的采样核
  - 阴影柔和度参数

- [ ] **点光源阴影**
  - Omnidirectional Shadow Map（立方体贴图）
  - 6 个面的渲染
  - 点光源阴影采样

**阴影采样示例**:
```glsl
float ShadowCalculation(vec4 fragPosLightSpace, sampler2D shadowMap) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    
    float currentDepth = projCoords.z;
    float bias = 0.005;
    
    // PCF
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x,y) * texelSize).r;
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}
```

**参考**:
- [LearnOpenGL Shadow Mapping](https://learnopengl.com/Advanced-Lighting/Shadows/Shadow-Mapping)
- UE5 的 `ShadowRendering.cpp`

**验收标准**:
- ✅ 方向光和点光源都有阴影
- ✅ 阴影边缘较柔和（PCF）
- ✅ 无明显阴影失真

---

#### Week 3-4: 级联阴影贴图（CSM）

**目标**: 实现大场景的高质量阴影

**任务清单**:
- [ ] **CSM 实现**
  - 视锥体分割（对数或均匀）
  - 多个级联层级（通常 3-4 层）
  - 每个级联的阴影贴图
  - 级联混合（避免接缝）

- [ ] **CSM 优化**
  - 稳定的阴影（避免闪烁）
  - 紧密的 AABB 计算
  - 级联可视化（调试）

- [ ] **PCSS 软阴影（可选）**
  - Percentage Closer Soft Shadows
  - 动态 Penumbra 大小
  - 基于遮挡物距离的软度

**CSM 分割示例**:
```cpp
// 对数分割
for (uint32 i = 0; i < numCascades; ++i) {
    float p = (i + 1) / static_cast<float>(numCascades);
    float log = nearPlane * pow(farPlane / nearPlane, p);
    float uniform = nearPlane + (farPlane - nearPlane) * p;
    
    cascadeSplits[i] = lambda * log + (1.0f - lambda) * uniform;
}
```

**参考**:
- [Microsoft CSM](https://docs.microsoft.com/en-us/windows/win32/dxtecharts/cascaded-shadow-maps)
- UE5 的 `ShadowSetup.cpp`

**验收标准**:
- ✅ 支持 3-4 层 CSM
- ✅ 近处和远处阴影都清晰
- ✅ 级联过渡平滑

---

### 第 5 个月：天空与大气（5月）

#### Week 1-2: 天空盒与程序化天空

**目标**: 实现天空渲染

**任务清单**:
- [ ] **立方体贴图天空**
  - 立方体贴图加载
  - 天空盒渲染（无限远）
  - HDR 天空支持

- [ ] **程序化天空**
  - 基于 Rayleigh 和 Mie 散射
  - 太阳位置控制
  - 昼夜循环
  - 天空颜色渐变

- [ ] **云层（简单版）**
  - 2D 云纹理
  - 云层动画
  - 云的透明混合

**程序化天空参考**:
```glsl
// Rayleigh 散射（蓝色天空）
vec3 rayleigh = rayleighCoefficient * (1.0 + cos(theta) * cos(theta));

// Mie 散射（太阳周围光晕）
vec3 mie = mieCoefficient * (1.0 - g*g) / pow(1.0 + g*g - 2.0*g*cos(theta), 1.5);

vec3 skyColor = rayleigh + mie;
```

**参考**:
- [Atmospheric Scattering](https://www.scratchapixel.com/lessons/procedural-generation-virtual-worlds/simulating-sky)
- UE5 的 `SkyAtmosphere.cpp`

**验收标准**:
- ✅ 支持静态和动态天空
- ✅ 天空颜色真实
- ✅ 太阳/月亮渲染正确

---

#### Week 3-4: 后处理基础

**目标**: 实现基础后处理效果

**任务清单**:
- [ ] **后处理框架**
  - 实现 `PostProcessPass` 基类
  - 全屏四边形渲染
  - RenderTarget 链式处理

- [ ] **Tone Mapping（色调映射）**
  - ACES Filmic
  - Reinhard
  - Uncharted 2
  - 曝光控制

- [ ] **Bloom（泛光）**
  - 亮度提取
  - 高斯模糊（分离卷积）
  - Downsampling / Upsampling
  - Bloom 强度控制

- [ ] **颜色分级**
  - 饱和度
  - 对比度
  - 色调偏移
  - LUT（查找表）支持

**Bloom 流程**:
```
原始渲染
    ↓
亮度提取 (Threshold)
    ↓
Downsampling (1/2, 1/4, 1/8...)
    ↓
高斯模糊 (每层)
    ↓
Upsampling + 混合
    ↓
与原图混合
```

**参考**:
- [LearnOpenGL Bloom](https://learnopengl.com/Advanced-Lighting/Bloom)
- UE5 的 `PostProcessing.cpp`

**验收标准**:
- ✅ Tone Mapping 效果明显
- ✅ Bloom 真实自然
- ✅ 后处理链可扩展

---

### 第 6 个月：环境光遮蔽与反射（6月）

#### Week 1-2: SSAO（屏幕空间环境光遮蔽）

**目标**: 实现 SSAO

**任务清单**:
- [ ] **SSAO 实现**
  - 深度和法线缓冲
  - 随机采样核生成
  - Hemisphere 采样
  - 噪声纹理（减少条带）

- [ ] **SSAO 优化**
  - 范围和强度参数
  - 模糊后处理（去噪）
  - 性能优化（降采样）

**SSAO 伪代码**:
```glsl
float occlusion = 0.0;
for(int i = 0; i < kernelSize; ++i) {
    vec3 samplePos = TBN * samples[i];  // 切线空间 → 世界空间
    samplePos = fragPos + samplePos * radius;
    
    vec4 offset = projection * view * vec4(samplePos, 1.0);
    offset.xyz /= offset.w;
    offset.xyz = offset.xyz * 0.5 + 0.5;
    
    float sampleDepth = texture(depthMap, offset.xy).r;
    float rangeCheck = smoothstep(0.0, 1.0, radius / abs(fragPos.z - sampleDepth));
    occlusion += (sampleDepth >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
}
occlusion = 1.0 - (occlusion / kernelSize);
```

**参考**:
- [LearnOpenGL SSAO](https://learnopengl.com/Advanced-Lighting/SSAO)
- UE5 的 `PostProcessAmbientOcclusion.cpp`

**验收标准**:
- ✅ 物体接触处有明显遮蔽
- ✅ SSAO 无明显噪点
- ✅ 性能可接受

---

#### Week 3-4: 屏幕空间反射（SSR）

**目标**: 实现 SSR

**任务清单**:
- [ ] **SSR 基础**
  - 光线步进（Ray Marching）
  - 深度缓冲查询
  - 反射向量计算
  - 衰减和边缘处理

- [ ] **SSR 优化**
  - 层级步进（Hierarchical Z-Buffer）
  - 粗糙度支持（模糊）
  - 屏幕边缘淡出

**SSR 算法**:
```glsl
vec3 reflectedColor = vec3(0.0);
vec3 rayDir = reflect(viewDir, normal);

// Ray marching in screen space
vec3 rayPos = fragPos;
for(int i = 0; i < maxSteps; ++i) {
    rayPos += rayDir * stepSize;
    
    vec4 projectedCoord = projection * view * vec4(rayPos, 1.0);
    vec2 screenUV = projectedCoord.xy / projectedCoord.w * 0.5 + 0.5;
    
    float sampledDepth = texture(depthMap, screenUV).r;
    if(sampledDepth < rayPos.z) {
        // Hit!
        reflectedColor = texture(sceneColor, screenUV).rgb;
        break;
    }
}
```

**参考**:
- [Screen Space Reflections in UE4](https://docs.unrealengine.com/en-US/RenderingAndGraphics/PostProcessEffects/ScreenSpaceReflection/)

**验收标准**:
- ✅ 镜面反射正确
- ✅ 反射有适当的衰减
- ✅ 性能达标

---

### Q2 里程碑

**完成标志**:
- ✅ 实时阴影系统完整（CSM）
- ✅ 天空和大气渲染真实
- ✅ 后处理链完善（Bloom、Tone Mapping、SSAO、SSR）

**演示场景**:
- 室外大场景（地形 + 植被）
- 动态昼夜循环
- 高质量阴影和环境光遮蔽

**性能目标**:
- 60 FPS @ 1080p（中等复杂场景）

---

## 🗓️ 第三季度（7-9月）：延迟渲染与多线程

### 主题：延迟管线、多线程渲染、GPU Driven

### 第 7 个月：延迟渲染管线（7月）

#### Week 1-2: G-Buffer 与延迟光照

**目标**: 实现延迟渲染管线

**任务清单**:
- [ ] **G-Buffer 设计**
  - RT0: Albedo (RGB) + Metallic (A)
  - RT1: Normal (RGB) + Roughness (A)
  - RT2: Position (RGB) + AO (A)
  - RT3: Emissive (RGB) + SpecularMask (A)
  - Depth Buffer

- [ ] **几何通道**
  - 渲染所有几何到 G-Buffer
  - 优化顶点着色器（只输出需要的数据）
  - 法线压缩（Octahedron Encoding）

- [ ] **光照通道**
  - 全屏四边形光照
  - 多光源累积
  - 光照计算（使用 G-Buffer）

**G-Buffer 布局**:
```cpp
struct GBufferLayout {
    RenderTexture* RT0_AlbedoMetallic;   // RGBA8
    RenderTexture* RT1_NormalRoughness;  // RGBA8 (Octahedron Normal)
    RenderTexture* RT2_PositionAO;       // RGBA16F
    RenderTexture* RT3_Emissive;         // RGBA16F
    RenderTexture* DepthStencil;         // D24S8
};
```

**延迟渲染流程**:
```
Geometry Pass
    ↓
填充 G-Buffer (4+ RT)
    ↓
Lighting Pass
    ↓
对每个光源累加光照
    ↓
Post Processing
```

**参考**:
- [LearnOpenGL Deferred Shading](https://learnopengl.com/Advanced-Lighting/Deferred-Shading)
- UE5 的 `DeferredShadingRenderer.cpp`

**验收标准**:
- ✅ G-Buffer 正确填充
- ✅ 延迟光照计算正确
- ✅ 支持大量光源（100+ 点光源）

---

#### Week 3-4: Tiled/Clustered Deferred Shading

**目标**: 优化延迟渲染性能

**任务清单**:
- [ ] **Tiled Deferred Shading**
  - 屏幕分割为 Tile (16x16 或 32x32)
  - 每个 Tile 计算影响的光源列表
  - Compute Shader 实现光照累积

- [ ] **Clustered Deferred Shading（可选）**
  - 3D 分簇（X, Y, Depth）
  - 每个 Cluster 的光源列表
  - 更高的光源剔除效率

- [ ] **性能优化**
  - 光源包围球剔除
  - 早期深度测试
  - 光照 Tile 可视化（调试）

**Tiled Shading 伪代码**:
```glsl
// Compute Shader
layout(local_size_x = 16, local_size_y = 16) in;

shared uint lightCount;
shared uint lightIndices[MAX_LIGHTS_PER_TILE];

void main() {
    // 1. 计算 Tile 的深度范围
    float minDepth = ..., maxDepth = ...;
    
    // 2. 剔除光源
    for (uint i = gl_LocalInvocationIndex; i < numLights; i += TILE_SIZE) {
        if (LightIntersectsTile(lights[i], minDepth, maxDepth)) {
            uint index = atomicAdd(lightCount, 1);
            lightIndices[index] = i;
        }
    }
    
    barrier();
    
    // 3. 光照计算
    vec3 lighting = vec3(0.0);
    for (uint i = 0; i < lightCount; ++i) {
        uint lightIndex = lightIndices[i];
        lighting += CalculateLight(lights[lightIndex], ...);
    }
    
    imageStore(outputImage, pixelCoord, vec4(lighting, 1.0));
}
```

**参考**:
- [Tiled Deferred Shading](http://www.aortiz.me/2018/12/21/CG.html)
- UE5 的 `LightGrid.cpp`

**验收标准**:
- ✅ 支持 1000+ 光源保持 60 FPS
- ✅ Tiled 可视化工作正常
- ✅ 光照质量无损失

---

### 第 8 个月：多线程渲染架构（8月）

#### Week 1-2: 渲染线程分离

**目标**: 实现游戏线程和渲染线程分离

**任务清单**:
- [ ] **线程架构**
  - 主线程（游戏逻辑）
  - 渲染线程（RHI 调用）
  - 线程间同步（双缓冲）

- [ ] **渲染命令队列**
  - 实现 `RenderCommand` 基类
  - 命令队列（无锁队列）
  - 命令分发和执行

- [ ] **数据同步**
  - Proxy 系统（数据快照）
  - 双缓冲场景数据
  - 避免数据竞争

**线程模型**:
```
Game Thread                 Render Thread
    ↓                           ↓
Update Logic            Wait for Commands
    ↓                           ↓
Create Render Cmds  →   Execute Commands
    ↓                           ↓
Swap Buffers            Submit to GPU
    ↓                           ↓
Next Frame              Present
```

**Render Command 示例**:
```cpp
class RenderCommand {
public:
    virtual void Execute(RHICommandList* cmdList) = 0;
};

class DrawMeshCommand : public RenderCommand {
    MeshProxy* mesh;
    MaterialProxy* material;
    
    void Execute(RHICommandList* cmdList) override {
        cmdList->setPipelineState(material->pipelineState);
        cmdList->setVertexBuffers(0, {mesh->vertexBuffer});
        cmdList->draw(mesh->vertexCount);
    }
};

// 游戏线程中
renderCommandQueue->enqueue(MakeUnique<DrawMeshCommand>(meshProxy, matProxy));

// 渲染线程中
while (auto cmd = renderCommandQueue->dequeue()) {
    cmd->Execute(cmdList);
}
```

**参考**:
- UE5 的 `RenderingThread.cpp`
- `Engine/Source/Runtime/RenderCore/Public/RenderCommandFence.h`

**验收标准**:
- ✅ 游戏线程和渲染线程独立运行
- ✅ 帧率提升（CPU 利用率提高）
- ✅ 无数据竞争和崩溃

---

#### Week 3-4: 并行命令录制

**目标**: 多个工作线程并行录制命令

**任务清单**:
- [ ] **任务系统**
  - 实现 `TaskGraph` 系统
  - 工作窃取算法
  - 任务依赖管理

- [ ] **并行命令列表**
  - 每个线程独立的 `CommandList`
  - 命令列表合并
  - 同步和提交

- [ ] **场景分割**
  - 空间分割（Octree/BVH）
  - 每个线程处理一部分场景
  - 负载均衡

**并行录制架构**:
```
Main Thread
    ↓
分割场景 (按空间或对象)
    ↓
┌────────┬────────┬────────┬────────┐
│Thread 1│Thread 2│Thread 3│Thread 4│
│CmdList1│CmdList2│CmdList3│CmdList4│
└────────┴────────┴────────┴────────┘
    ↓        ↓        ↓        ↓
    └────────┴────────┴────────┘
              ↓
      合并所有 CommandList
              ↓
        Submit to GPU
```

**参考**:
- UE5 的 `ParallelCommandListSet.cpp`
- D3D12 的 `Bundle` 和 `CommandList`

**验收标准**:
- ✅ 3-4 个线程并行录制
- ✅ CPU 利用率提高（多核）
- ✅ 帧率提升 20-30%

---

### 第 9 个月：GPU 驱动渲染（9月）

#### Week 1-2: Indirect Drawing

**目标**: 实现 GPU 驱动的绘制

**任务清单**:
- [ ] **Indirect Draw 基础**
  - 实现 `vkCmdDrawIndirect` / `glDrawArraysIndirect`
  - Indirect Draw Buffer（GPU 内存）
  - Draw Command 数据结构

- [ ] **GPU 剔除**
  - Compute Shader 视锥体剔除
  - Occlusion Culling（遮挡剔除）
  - LOD 选择

- [ ] **实例化增强**
  - Multi-Draw Indirect
  - 实例数据缓冲
  - GPU 端实例化

**Indirect Draw 数据结构**:
```cpp
struct DrawCommand {
    uint32 vertexCount;
    uint32 instanceCount;
    uint32 firstVertex;
    uint32 firstInstance;
};

// GPU 剔除 Compute Shader
layout(std430, binding = 0) buffer InputDrawCmds {
    DrawCommand inputCmds[];
};

layout(std430, binding = 1) buffer OutputDrawCmds {
    DrawCommand outputCmds[];
};

void main() {
    uint idx = gl_GlobalInvocationID.x;
    DrawCommand cmd = inputCmds[idx];
    
    // 视锥体剔除
    if (IsVisible(cmd.boundingBox, frustum)) {
        uint outIdx = atomicAdd(outputCount, 1);
        outputCmds[outIdx] = cmd;
    }
}
```

**参考**:
- [GPU Driven Rendering Pipelines](https://vkguide.dev/docs/gpudriven/gpu_driven_engines/)
- UE5 的 `GPUScene.cpp`

**验收标准**:
- ✅ 支持 10,000+ 物体
- ✅ GPU 剔除正确
- ✅ Draw Call 大幅减少

---

#### Week 3-4: GPU Scene 管理

**目标**: 完整的 GPU 驱动场景管理

**任务清单**:
- [ ] **GPU Scene Buffer**
  - 场景数据完全在 GPU（SSBO）
  - 变换矩阵缓冲
  - 材质参数缓冲
  - 实例数据缓冲

- [ ] **Bindless Resources**
  - Descriptor Indexing（Vulkan 1.2）
  - 纹理数组 / Bindless Textures
  - 减少绑定开销

- [ ] **性能优化**
  - 持久映射缓冲
  - 增量更新（只更新变化的）
  - 压缩数据格式

**GPU Scene 布局**:
```glsl
// GPU Scene Data (SSBO)
struct ObjectData {
    mat4 modelMatrix;
    uint materialIndex;
    uint meshIndex;
    vec4 boundingSphere;
};

layout(std430, binding = 0) readonly buffer ObjectBuffer {
    ObjectData objects[];
};

// Bindless Textures
layout(binding = 1) uniform sampler2D textures[];

void main() {
    uint objID = gl_InstanceIndex;
    ObjectData obj = objects[objID];
    
    // 使用 materialIndex 索引纹理
    vec4 albedo = texture(textures[obj.materialIndex], texCoord);
}
```

**参考**:
- [Bindless Rendering](https://developer.nvidia.com/bindless-graphics)
- UE5 的 `GPUScene.h`

**验收标准**:
- ✅ 支持 50,000+ 物体渲染
- ✅ CPU 开销极低
- ✅ 帧率稳定 60 FPS

---

### Q3 里程碑

**完成标志**:
- ✅ 延迟渲染管线完整（Tiled/Clustered）
- ✅ 多线程渲染架构稳定
- ✅ GPU 驱动渲染可用

**演示场景**:
- 包含 10,000+ 物体的大场景
- 1000+ 动态光源
- 多线程并行渲染

**性能目标**:
- 60 FPS @ 1080p（复杂场景）
- CPU 利用率 > 70%（多核）
- Draw Call < 100（GPU Driven）

---

## 🗓️ 第四季度（10-12月）：生产工具与优化

### 主题：编辑器、工具链、性能优化

### 第 10 个月：资源管理与流送（10月）

#### Week 1-2: 资源管理器

**目标**: 完善资源管理系统

**任务清单**:
- [ ] **资源管理器**
  - 实现 `ResourceManager` 单例
  - 资源注册和查找
  - 资源引用计数
  - 资源卸载策略

- [ ] **异步加载**
  - 后台线程加载
  - 流式加载（分块）
  - 加载优先级队列

- [ ] **资源热重载**
  - 文件监控（Win32 / inotify）
  - 资源更新通知
  - 运行时刷新

**资源管理器示例**:
```cpp
class ResourceManager {
public:
    template<typename T>
    TSharedPtr<T> Load(const String& path, bool async = false) {
        // 检查缓存
        if (auto cached = m_cache.find(path)) {
            return std::static_pointer_cast<T>(cached);
        }
        
        // 加载资源
        if (async) {
            return LoadAsync<T>(path);
        } else {
            return LoadSync<T>(path);
        }
    }
    
    void Unload(const String& path);
    void UnloadUnused();  // 卸载引用计数为 0 的资源
};
```

**参考**:
- UE5 的 `AssetManager.cpp`

**验收标准**:
- ✅ 资源管理统一
- ✅ 异步加载无卡顿
- ✅ 热重载工作正常

---

#### Week 3-4: 纹理流送系统

**目标**: 实现纹理 LOD 流送

**任务清单**:
- [ ] **Mipmap 流送**
  - 按需加载 Mipmap 层级
  - 距离和屏幕大小计算
  - 流送优先级

- [ ] **虚拟纹理（Virtual Texture）**
  - 分块纹理加载
  - 页表管理
  - 按需加载 Tile

- [ ] **流送调度**
  - 带宽限制
  - 内存预算管理
  - 流送统计和调试

**Mipmap 流送示例**:
```cpp
class TextureStreaming {
public:
    void Update(Camera* camera) {
        for (auto& texture : m_streamingTextures) {
            // 计算所需 Mip 级别
            float distance = Distance(camera->position, texture->worldPosition);
            float screenSize = ProjectedSize(texture, camera);
            
            int requiredMip = CalculateMipLevel(distance, screenSize);
            int currentMip = texture->GetCurrentMipLevel();
            
            if (requiredMip < currentMip) {
                // 需要更高分辨率
                StreamIn(texture, requiredMip);
            } else if (requiredMip > currentMip + 1) {
                // 可以释放高分辨率
                StreamOut(texture, requiredMip);
            }
        }
    }
};
```

**参考**:
- UE5 的 `TextureStreaming.cpp`
- [Virtual Texturing](https://docs.unrealengine.com/en-US/RenderingAndGraphics/VirtualTexturing/)

**验收标准**:
- ✅ 纹理内存占用优化
- ✅ 流送无明显卡顿
- ✅ 大场景纹理管理良好

---

### 第 11 个月：编辑器与工具链（11月）

#### Week 1-2: 基础编辑器框架

**目标**: 实现简单的可视化编辑器

**任务清单**:
- [ ] **编辑器窗口**
  - 使用 ImGui 构建 UI
  - 主窗口布局（Docking）
  - 菜单栏和工具栏

- [ ] **场景视图**
  - 3D 视口（可交互）
  - 摄像机控制（FPS / Orbit）
  - Gizmo（平移、旋转、缩放）

- [ ] **层次视图**
  - 场景对象树
  - 对象选择和高亮
  - 拖拽操作

- [ ] **属性面板**
  - 对象属性编辑
  - 材质参数调整
  - 实时预览

**编辑器布局**:
```
┌────────────────────────────────────────────┐
│ File  Edit  View  Tools  Help             │ ← 菜单栏
├──────────┬─────────────────────┬───────────┤
│          │                     │           │
│ Hierarchy│   3D Viewport       │Properties │
│  View    │                     │  Panel    │
│          │                     │           │
│          │                     │           │
├──────────┴─────────────────────┴───────────┤
│        Asset Browser / Console             │
└────────────────────────────────────────────┘
```

**参考**:
- [Dear ImGui](https://github.com/ocornut/imgui)
- UE5 Editor Architecture

**验收标准**:
- ✅ 编辑器界面友好
- ✅ 场景编辑流畅
- ✅ 实时预览正常

---

#### Week 3-4: 材质编辑器

**目标**: 可视化材质编辑

**任务清单**:
- [ ] **节点编辑器**
  - 节点图（Node Graph）
  - 节点连接
  - 节点类型（输入、运算、输出）

- [ ] **材质节点**
  - 纹理采样节点
  - 数学运算节点（加、乘、混合等）
  - PBR 输出节点

- [ ] **实时预览**
  - 材质球预览
  - 着色器自动生成
  - 错误提示

**材质节点示例**:
```
[Texture: Albedo]
        ↓
    [Multiply]  ←  [Color: Tint]
        ↓
[PBR Master Material]
        ↓
   [输出到管线]
```

**参考**:
- [ImNodes](https://github.com/Nelarius/imnodes)
- UE5 Material Editor

**验收标准**:
- ✅ 节点编辑直观
- ✅ 材质实时生成
- ✅ 支持常用 PBR 节点

---

### 第 12 个月：性能优化与发布（12月）

#### Week 1-2: 性能剖析与优化

**目标**: 全面性能优化

**任务清单**:
- [ ] **性能剖析工具**
  - CPU Profiler（Tracy / Optick）
  - GPU Profiler（RenderDoc / Nsight）
  - 内存剖析（Valgrind / Dr. Memory）

- [ ] **热点优化**
  - 识别 CPU 瓶颈
  - 优化热路径代码
  - 减少内存分配

- [ ] **GPU 优化**
  - 减少过绘制（Overdraw）
  - 优化着色器（减少指令）
  - 纹理压缩（BC7/ASTC）

- [ ] **内存优化**
  - 对象池化
  - 内存对齐
  - 减少内存碎片

**性能目标**:
```
目标                  当前    目标    优化
────────────────────────────────────────
帧率 @ 1080p         45 FPS  60 FPS  +33%
Draw Call            500     <100    -80%
CPU 时间             12ms    8ms     -33%
GPU 时间             18ms    12ms    -33%
内存占用             2GB     1.5GB   -25%
```

**参考**:
- [Tracy Profiler](https://github.com/wolfpld/tracy)
- UE5 的 `Profiler.cpp`

**验收标准**:
- ✅ 达到性能目标
- ✅ 无明显卡顿
- ✅ 内存占用合理

---

#### Week 3-4: 文档与发布

**目标**: 完善文档，准备发布

**任务清单**:
- [ ] **技术文档**
  - 架构文档更新
  - API 文档（Doxygen）
  - 用户手册

- [ ] **示例项目**
  - 基础场景示例
  - 材质示例
  - 光照示例

- [ ] **发布准备**
  - 编译发布版本
  - 打包资源
  - 编写 README

- [ ] **开源准备（可选）**
  - 代码审查
  - 许可证选择
  - GitHub 仓库准备

**文档清单**:
```
docs/
├── Architecture.md          # 架构文档
├── API/                    # API 文档
│   ├── RHI.md
│   ├── Renderer.md
│   └── Scene.md
├── Tutorials/              # 教程
│   ├── GettingStarted.md
│   ├── CreateMaterial.md
│   └── Lighting.md
└── Examples/               # 示例
    ├── BasicScene/
    ├── PBRMaterials/
    └── DeferredRendering/
```

**验收标准**:
- ✅ 文档完整
- ✅ 示例可运行
- ✅ 发布版本稳定

---

### Q4 里程碑

**完成标志**:
- ✅ 资源管理和流送系统完善
- ✅ 基础编辑器可用
- ✅ 性能优化达标
- ✅ 文档和示例完整

**最终演示**:
- 完整的户外/室内场景
- 动态昼夜循环
- 多种材质和光照
- 后处理效果齐全
- 编辑器实时编辑

---

## 📊 开发进度跟踪

### 月度检查点

| 月份 | 关键功能 | 验收标准 | 风险 |
|------|---------|---------|------|
| 1月 | 场景图 + 材质系统 | 显示多个模型，材质可编辑 | 中 |
| 2月 | 光照 + 前向渲染 | 多光源场景正常 | 低 |
| 3月 | PBR + IBL | PBR 材质真实 | 中 |
| 4月 | 阴影系统 | CSM 正常工作 | 高 |
| 5月 | 天空 + 后处理 | 天空真实，Bloom 正常 | 低 |
| 6月 | SSAO + SSR | 环境光遮蔽和反射正确 | 中 |
| 7月 | 延迟渲染 | G-Buffer 正确，光照累积 | 高 |
| 8月 | 多线程渲染 | 游戏/渲染线程分离 | 高 |
| 9月 | GPU Driven | 10,000+ 物体 @ 60 FPS | 高 |
| 10月 | 资源流送 | 纹理流送无卡顿 | 中 |
| 11月 | 编辑器 | 基础编辑器可用 | 中 |
| 12月 | 优化 + 发布 | 性能达标，文档完整 | 低 |

### 风险管理

**高风险项**:
1. **阴影系统（4月）**
   - 风险：CSM 实现复杂，调试困难
   - 缓解：提前研究 UE5 实现，留足调试时间

2. **延迟渲染（7月）**
   - 风险：G-Buffer 布局设计，性能优化
   - 缓解：先实现基础版本，再优化

3. **多线程渲染（8月）**
   - 风险：数据竞争，调试困难
   - 缓解：逐步重构，增量实现

4. **GPU Driven（9月）**
   - 风险：Compute Shader 调试，驱动兼容性
   - 缓解：先在 CPU 实现逻辑，再移植到 GPU

---

## 🛠️ 技术栈与工具

### 核心技术
- **语言**: C++20
- **图形 API**: Vulkan 1.2+, D3D12 (未来)
- **窗口**: GLFW 3.3+
- **数学库**: GLM
- **图像加载**: stb_image
- **模型加载**: Assimp / cgltf

### 开发工具
- **IDE**: Visual Studio 2022 / CLion
- **调试器**: RenderDoc, Nsight Graphics, PIX
- **剖析器**: Tracy, Optick
- **版本控制**: Git + GitHub
- **文档**: Doxygen, Markdown

### 第三方库
- **ImGui**: 编辑器 UI
- **ImNodes**: 节点编辑器
- **ImGuizmo**: 3D Gizmo
- **spdlog**: 日志系统（可选替代）
- **SPIRV-Cross**: 着色器反射

---

## 📚 学习资源

### 必读书籍
1. **Real-Time Rendering (4th Edition)**
2. **GPU Gems 系列**
3. **Game Engine Architecture**
4. **Physically Based Rendering (PBRT)**

### 在线资源
1. **LearnOpenGL** - 基础渲染教程
2. **UE5 源码** - 架构参考
3. **Vulkan Tutorial** - Vulkan 入门
4. **Graphics Programming Weekly** - 业界动态

### 技术博客
1. **NVIDIA Developer Blog**
2. **Khronos Blog**
3. **0FPS Blog** (Mikola Lysenko)
4. **Adrian Courrèges' Blog** (游戏渲染分析)

---

## 🎓 团队建议

### 单人开发
- **重点**: 核心渲染功能（Q1-Q3）
- **简化**: 编辑器功能最小化
- **策略**: 借鉴现有开源项目

### 小团队（2-3人）
- **分工**:
  - 人员 1: 渲染核心 + RHI
  - 人员 2: 场景管理 + 资源系统
  - 人员 3: 编辑器 + 工具链

### 优先级调整
**如果时间有限，可以削减**:
- Q2: SSR（屏幕空间反射）
- Q3: Clustered Shading（用 Tiled 即可）
- Q4: 材质编辑器（手动编写材质）

**必须完成（核心功能）**:
- Q1: 场景图 + 材质 + PBR
- Q2: 阴影（CSM）+ 后处理基础
- Q3: 延迟渲染 + 多线程
- Q4: 性能优化

---

## 📈 成功标准

### 技术指标
- ✅ 前向和延迟渲染管线完整
- ✅ 支持 PBR 材质
- ✅ 实时阴影（CSM）
- ✅ 多线程渲染
- ✅ GPU Driven 渲染（10,000+ 物体）
- ✅ 60 FPS @ 1080p

### 代码质量
- ✅ 架构清晰，模块化
- ✅ 代码注释完整
- ✅ 无内存泄漏
- ✅ 支持热重载

### 可用性
- ✅ 编辑器可用（基础功能）
- ✅ 文档完整
- ✅ 示例项目可运行

---

## 🔄 迭代与调整

### 每月回顾
- 评估进度
- 调整计划
- 记录问题

### 灵活调整
- 如果某个功能超时 > 2周，考虑简化
- 优先完成核心功能
- 非核心功能可以延后

### 持续学习
- 每周阅读 1-2 篇论文/博客
- 研究其他引擎实现
- 参与社区讨论

---

## 🎯 一年后的目标

**技术成果**:
- 一个功能完整的现代渲染引擎
- 支持前向和延迟渲染
- 多线程架构
- GPU 驱动渲染
- 基础编辑器

**个人成长**:
- 深入理解现代渲染技术
- 掌握 Vulkan 和 D3D12
- 熟悉大型项目架构
- 积累游戏引擎开发经验

**可展示成果**:
- GitHub 开源项目
- 技术博客文章
- 演示视频
- 示例场景

---

## 📞 社区与支持

### 讨论平台
- **Discord**: Graphics Programming 社区
- **Reddit**: r/GraphicsProgramming
- **知乎**: 图形渲染话题

### 开源项目参考
- **The Forge**: 多平台渲染框架
- **Filament**: Google 的 PBR 引擎
- **Hazel Engine**: 教学引擎
- **Lumix Engine**: 轻量级游戏引擎

---

## 📝 附录

### A. 功能检查清单

#### 核心渲染
- [ ] 场景图系统
- [ ] 材质系统
- [ ] PBR 光照
- [ ] 阴影系统（CSM）
- [ ] 天空渲染
- [ ] 后处理（Bloom、Tone Mapping、SSAO）

#### 高级渲染
- [ ] 延迟渲染
- [ ] Tiled Shading
- [ ] SSR（可选）
- [ ] 动态 GI（可选）

#### 架构
- [ ] 多线程渲染
- [ ] 并行命令录制
- [ ] GPU Driven 渲染
- [ ] 资源流送

#### 工具
- [ ] 基础编辑器
- [ ] 材质编辑器（可选）
- [ ] 性能剖析
- [ ] 资源管理

### B. 性能基准

| 场景复杂度 | 物体数量 | 光源数量 | 目标帧率 | 分辨率 |
|-----------|---------|---------|---------|--------|
| 简单 | < 1,000 | < 10 | 120 FPS | 1080p |
| 中等 | 1,000 - 5,000 | 10 - 100 | 60 FPS | 1080p |
| 复杂 | 5,000 - 10,000 | 100 - 1,000 | 60 FPS | 1080p |
| 极限 | > 10,000 | > 1,000 | 30 FPS | 1080p |

### C. 代码规范

参考引擎全局规则文档中的编码标准：
- PascalCase for classes
- camelCase for functions/variables
- m_ prefix for members
- SCREAMING_SNAKE_CASE for constants

---

## 🚀 开始行动

### 第一步（立即开始）
1. 阅读 UE5 源码中的 `SceneManagement.h`
2. 设计场景图类层次结构
3. 创建 `Scene` 和 `SceneNode` 类

### 保持动力
- **每周完成一个小功能**
- **记录开发日志和进度**
- **分享你的成果（博客、视频）**
- **与社区交流经验**

### 长期视野
这是一个一年的计划，但引擎开发是持续的过程。一年后：
- 你将拥有一个功能完整的渲染引擎
- 可以继续添加更高级的功能（光线追踪、Nanite、Lumen 等）
- 可以基于这个引擎开发游戏或应用

---

**祝你开发顺利！一年后见证你的成果！** 🎉

---

**文档版本**: 1.0  
**最后更新**: 2025-11-20  
**作者**: MonsterRender 团队  
**联系**: [待补充]

