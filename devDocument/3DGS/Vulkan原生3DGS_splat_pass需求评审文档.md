# MonsterEngine Vulkan 原生 3DGS Splat Pass 开发文档

> **文档性质**：技术方案 + 执行记录
> **创建日期**：2026-08-04
> **关联需求**：`D:\WorkBuddy_SafeZone\Personal-Growth-Library\MonsterEngine_doc\MonsterEngine Vulkan 原生 splat pass设计文档.md`
> **参考仓库根目录**：`D:\code\me-viewer-refs\`

---

## 目录

1. [架构现状分析](#1-架构现状分析)
2. [基础设施缺口与补齐方案](#2-基础设施缺口与补齐方案)
3. [参考仓库对照表](#3-参考仓库对照表)
4. [实现阶段拆解](#4-实现阶段拆解)
5. [文件落点总览](#5-文件落点总览)
6. [执行顺序依赖图](#6-执行顺序依赖图)
7. [风险与对策](#7-风险与对策)

---

## 1. 架构现状分析

### 1.1 MonsterEngine 已有能力

| 模块 | 状态 | 详情 | 关键文件 |
|------|------|------|----------|
| **RDG (Render Dependency Graph)** | ✅ 完整 | `ERDGPassFlags::Compute` 已定义，可以直接注册 Compute Pass | `Include/RDG/RDGBuilder.h` |
| **Vulkan 后端** | ✅ 完整 | Device / MemoryManager / CommandBuffer / DescriptorSet / PipelineLayout 全套 | `Include/Platform/Vulkan/VulkanDevice.h` |
| **Descriptor 系统** | ✅ 完整 | Slot-Based，支持 multi-set layout + push constants | `Include/Platform/Vulkan/VulkanDescriptorSetLayout.h` |
| **Shader 编译管线** | ✅ 部分 | GLSL → SPIR-V (glslc)，但只支持 Vertex/Fragment | `Include/Core/ShaderCompiler.h` |
| **EShaderStage::Compute** | ✅ 已定义 | 枚举值存在，但缺少对应的 Shader 子类和 Pipeline 创建 | `Include/RHI/RHIDefinitions.h#L588` |
| **GPU 内存管理** | ✅ 完整 | `FVulkanMemoryManager` 子分配，原生支持 Storage Buffer | `Include/Platform/Vulkan/FVulkanMemoryManager.h` |
| **Command Buffer 管理** | ✅ 完整 | 每帧 Ring Buffer，支持 multi-frame in flight | `Include/Platform/Vulkan/VulkanCommandBuffer.h` |
| **Multi-Descriptor Set 绑定** | ✅ 完整 | `bindDescriptorSets` / `pushConstants` 已实现到 VkCmd 调用 | `Source/Platform/Vulkan/VulkanRHICommandList.cpp` |
| **Render Pass 系统** | ✅ 完整 | 前向渲染管线，含 Depth / Opaque / Skybox / Shadow / PBR | `Source/Renderer/ForwardRenderPasses.cpp` |

### 1.2 缺失的基础设施

| 缺口 | 位置 | 缺失内容 | 优先级 |
|------|------|----------|--------|
| **Compute Shader 类** | `Include/Platform/Vulkan/VulkanShader.h` | 无 `VulkanComputeShader` 子类，无 `IRHIComputeShader` 接口定义 | **P0** |
| **Compute Pipeline 创建** | `Source/Platform/Vulkan/VulkanDevice.cpp` | 无 `vkCreateComputePipelines` 调用，无 `createComputePipelineState()` | **P0** |
| **IRHICommandList::dispatch()** | `Include/RHI/IRHICommandList.h` | 无 `dispatch(groupCountX, groupCountY, groupCountZ)` 纯虚方法 | **P0** |
| **ShaderCompiler Compute 支持** | `Include/Core/ShaderCompiler.h` | `EShaderStageKind` 目前只有 Vertex/Fragment，无 Compute | **P0** |
| **Storage Buffer Descriptor 更新** | `Include/Platform/Vulkan/VulkanDescriptorSetLayout.h` | 需确认 `updateUniformBuffer` 是否支持 `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER` | **P0** |
| **IRHIDevice::createComputeShader()** | `Include/RHI/IRHIDevice.h` | 无创建 Compute Shader 的接口 | **P0** |
| **GPU Radix Sort 多 Pass** | 不存在 | 需要 9 阶段 GPU 排序（Count/Reduce/Scan/Scatter） | **P1**（可先用 CPU fallback） |

---

## 2. 基础设施缺口与补齐方案

### 2.1 Compute Shader 类（P0）

**涉及文件**：
- `Include/RHI/IRHIResource.h` — 新增 `IRHIComputeShader` 接口
- `Include/Platform/Vulkan/VulkanShader.h` — 新增 `VulkanComputeShader` 类

**方案**：

```cpp
// IRHIResource.h — 新增
class IRHIComputeShader : public virtual IRHIShader
{
public:
    virtual ~IRHIComputeShader() = default;
};

// VulkanShader.h — 新增
class VulkanComputeShader : public VulkanShader, public IRHIComputeShader
{
public:
    VulkanComputeShader(VulkanDevice* device, TSpan<const uint8> bytecode)
        : VulkanShader(device, EShaderStage::Compute, bytecode) {}
    
    uint32 getSize() const override { return VulkanShader::getSize(); }
    EResourceUsage getUsage() const override { return VulkanShader::getUsage(); }
    ERHIBackend getBackendType() const override { return ERHIBackend::Vulkan; }
};
```

`VulkanShader` 基类在构造时根据 `EShaderStage::Compute` 设置 `VK_SHADER_STAGE_COMPUTE_BIT`。

---

### 2.2 Compute Pipeline 创建（P0）

**涉及文件**：
- `Include/RHI/IRHIDevice.h` — 新增 `createComputePipelineState()` 纯虚方法
- `Include/RHI/RHIDefinitions.h` — 新增 `ComputePipelineStateDesc`
- `Source/Platform/Vulkan/VulkanDevice.cpp` — 实现 `vkCreateComputePipelines`

**方案**：

```cpp
// RHIDefinitions.h — 新增
struct ComputePipelineStateDesc
{
    TSharedPtr<class IRHIComputeShader> computeShader;
    TSharedPtr<class IRHIPipelineLayout> pipelineLayout;  // 复用已有接口
    String debugName;
};

// IRHIDevice.h — 新增纯虚方法
virtual TSharedPtr<IRHIPipelineState> createComputePipelineState(
    const ComputePipelineStateDesc& desc) = 0;
```

`VulkanDevice` 中实现：
1. 获取 `VkShaderModule`（从 `VulkanComputeShader`）
2. 填充 `VkComputePipelineCreateInfo`（stage + layout）
3. 调用 `vkCreateComputePipelines`
4. 返回包装的 Pipeline State

---

### 2.3 IRHICommandList::dispatch()（P0）

**涉及文件**：
- `Include/RHI/IRHICommandList.h` — 新增纯虚方法
- `Include/Platform/Vulkan/VulkanRHICommandList.h` — override 声明
- `Source/Platform/Vulkan/VulkanRHICommandList.cpp` — 实现

**方案**：

```cpp
// IRHICommandList.h — 新增
virtual void dispatch(uint32 groupCountX, uint32 groupCountY, 
                      uint32 groupCountZ) = 0;

// VulkanRHICommandList.cpp — 实现
void FVulkanRHICommandListImmediate::dispatch(
    uint32 groupCountX, uint32 groupCountY, uint32 groupCountZ) override
{
    FVulkanCmdBuffer* cmdBuffer = GetVulkanCommandBuffer();
    if (cmdBuffer && cmdBuffer->getHandle() != VK_NULL_HANDLE)
    {
        vkCmdDispatch(cmdBuffer->getHandle(), 
                      groupCountX, groupCountY, groupCountZ);
    }
}
```

---

### 2.4 ShaderCompiler Compute 支持（P0）

**涉及文件**：
- `Include/Core/ShaderCompiler.h` — 扩展 `EShaderStageKind`
- `Source/Core/ShaderCompiler.cpp` — 扩展 `getStageArgGLSLC()`

**方案**：

```cpp
// ShaderCompiler.h
enum class EShaderStageKind : uint32 {
    Vertex,
    Fragment,
    Compute   // 新增
};

// ShaderCompiler.cpp — getStageArgGLSLC 新增分支
case EShaderStageKind::Compute: return "comp";
```

编译 compute shader：
```
glslc -fshader-stage=comp --target-env=vulkan1.2 \
  -o splat_preprocess.comp.spv splat_preprocess.comp
```

---

### 2.5 Storage Buffer Descriptor 支持（P0）

**涉及文件**：
- `Include/RHI/IRHIDescriptorSet.h` — 确认/新增 `updateStorageBuffer()`
- `Include/Platform/Vulkan/VulkanDescriptorSetLayout.h` — 对应实现

**当前状态**：`VulkanDescriptorSet` 已有 `updateUniformBuffer()`，使用 `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`。

**方案**：新增 `updateStorageBuffer()` 方法，内部使用 `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER`。

```cpp
// IRHIDescriptorSet.h — 新增
virtual void updateStorageBuffer(uint32 binding, TSharedPtr<IRHIBuffer> buffer, 
                                 uint32 offset = 0, uint32 range = 0) = 0;

// VulkanDescriptorSet.h — 实现
void updateStorageBuffer(uint32 binding, TSharedPtr<IRHIBuffer> buffer, 
                         uint32 offset = 0, uint32 range = 0) override;
```

---

### 2.6 IRHIDevice::createComputeShader()（P0）

**涉及文件**：`Include/RHI/IRHIDevice.h`

```cpp
virtual TSharedPtr<IRHIComputeShader> createComputeShader(
    TSpan<const uint8> bytecode) = 0;
```

---

## 3. 参考仓库对照表

### 3.1 核心参考：3dgs-vulkan-cpp

**路径**：`D:\code\me-viewer-refs\3dgs-vulkan-cpp\vulkan-3dgs\src\Shaders\`

| 阶段 | MonsterEngine Shader | 对拍文件 | 本地路径 |
|------|---------------------|----------|----------|
| Preprocess | `splat_preprocess.comp` | `preprocess.comp` | `3dgs-vulkan-cpp/vulkan-3dgs/src/Shaders/preprocess.comp` |
| PrefixSum | `splat_prefixsum.comp` | `prefixsum.comp` | `3dgs-vulkan-cpp/vulkan-3dgs/src/Shaders/prefixsum.comp` |
| AssignKeys | `splat_idkeys.comp` | `idkeys.comp` | `3dgs-vulkan-cpp/vulkan-3dgs/src/Shaders/idkeys.comp` |
| RadixSort | `splat_radixsort.comp` | `radix_sort/radixsort.comp` | `3dgs-vulkan-cpp/vulkan-3dgs/src/Shaders/radix_sort/` |
| TileBounds | `splat_boundaries.comp` | `tile_boundaries.comp` | `3dgs-vulkan-cpp/vulkan-3dgs/src/Shaders/tile_boundaries.comp` |
| Render/Blend | `splat_render.comp` | `render.comp` | `3dgs-vulkan-cpp/vulkan-3dgs/src/Shaders/render.comp` |

### 3.2 宿主编排参考：3DGS.cpp

**路径**：`D:\code\me-viewer-refs\3DGS.cpp\`

| 文件 | 用途 |
|------|------|
| `src/Renderer.h` | Compute Pipeline 生命周期管理 + Descriptor 绑定顺序 |
| `src/Renderer.cpp` | 每帧 dispatch 编排（6 个 compute 阶段的 cmd buffer 录制） |
| `src/3dgs.cpp` | 主循环（record → submit → present） |
| `src/GSScene.cpp` | `.ply` 解析 + GPU upload |

### 3.3 排序参考：vk3dGaussianSplatting

**路径**：`D:\code\me-viewer-refs\vk3dGaussianSplatting\`

| 用途 | 详情 |
|------|------|
| GPU Radix Sort | 9 阶段拆分：Count → Reduce → Scan → Scatter（FidelityFX 范式） |
| AMD GPU 适配 | `SUBGROUP_SIZE=64` 注意事项 |

### 3.4 不 clone 的参考（仅在线浏览）

| 仓库 | 原因 |
|------|------|
| `antimatter15/splat` | WebGL 单文件，README 讲清数学本质，clone 无意义 |
| `kestrelm/splatapult` | OpenGL，API 不匹配 |
| `kallr/osg_3dgs` | OpenSceneGraph |
| `aras-p/UnityGaussianSplatting` | Unity |
| `graphdeco-inria/gaussian-splatting` | 纯 CUDA，Vulkan 用不上 |
| `hbb1/2d-gaussian-splatting` | 已有本地 `D:\code\2d-gaussian-splatting` |

---

## 4. 实现阶段拆解

### Phase 0 — 基础设施补齐（P0，必须先做）

> 所有后续阶段的前置依赖。约 1h。

#### 0.1 Compute Shader 类体系

**涉及文件**：
| 文件 | 动作 |
|------|------|
| `Include/RHI/IRHIResource.h` | 新增 `IRHIComputeShader` 接口 |
| `Include/Platform/Vulkan/VulkanShader.h` | 新增 `VulkanComputeShader` 子类 |
| `Source/Platform/Vulkan/VulkanShader.cpp` | 实现（大部分复用基类） |

#### 0.2 Compute Pipeline 创建链路

**涉及文件**：
| 文件 | 动作 |
|------|------|
| `Include/RHI/RHIDefinitions.h` | 新增 `ComputePipelineStateDesc` |
| `Include/RHI/IRHIDevice.h` | 新增 `createComputePipelineState()` + `createComputeShader()` |
| `Source/Platform/Vulkan/VulkanDevice.cpp` | 实现两个方法 |
| `Include/Platform/Vulkan/VulkanPipelineState.h` | 新增 `VulkanComputePipelineState` 或扩展 |

#### 0.3 CommandList::dispatch()

**涉及文件**：
| 文件 | 动作 |
|------|------|
| `Include/RHI/IRHICommandList.h` | 新增 `dispatch()` 纯虚方法 |
| `Include/Platform/Vulkan/VulkanRHICommandList.h` | 新增 override 声明 |
| `Source/Platform/Vulkan/VulkanRHICommandList.cpp` | `vkCmdDispatch` 实现 |

#### 0.4 ShaderCompiler Compute 支持

**涉及文件**：
| 文件 | 动作 |
|------|------|
| `Include/Core/ShaderCompiler.h` | `EShaderStageKind` 加 `Compute` |
| `Source/Core/ShaderCompiler.cpp` | `getStageArgGLSLC` 加 `"comp"` 分支 |

#### 0.5 Storage Buffer Descriptor 支持

**涉及文件**：
| 文件 | 动作 |
|------|------|
| `Include/RHI/IRHIDescriptorSet.h` | 新增 `updateStorageBuffer()` |
| `Include/Platform/Vulkan/VulkanDescriptorSetLayout.h` | 新增 override 声明 |
| `Source/Platform/Vulkan/VulkanDescriptorSetLayout.cpp` | 实现 `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER` |

**验收**：编译通过，能创建 Compute Pipeline 并 Dispatch。

---

### Phase 1 — CPU 验证基线（FR-9，上午 ~1.5h）

**目的**：CPU 端跑通完整数学管线，验证数值正确性，作为 GPU 版 Golden Reference。

**文件落点**：
```
Source/Renderer/Splat/CPU/
├── SplatCPU.h
└── SplatCPU.cpp
```

**功能**：

| 步骤 | 内容 | 参考 |
|------|------|------|
| ① `.ply` 解析 | 读取 vertex element 列表，提取 xyz / scales[3] / rot[4] / opacity / sh[16×3] | `3DGS.cpp/src/GSScene.cpp` |
| ② 3D 协方差 | `scale → Σ = RSS^T`（quaternion → rotation matrix → M = S·R → Σ = M^T·M） | `preprocess.comp::computeCov3D()` |
| ③ 视锥剔除 | 世界→视变换，clip space 判断 | `preprocess.comp::inFrustum()` |
| ④ Jacobian 投影 | J = [fx/tz, 0, -fx·tx/tz²; 0, fy/tz, -fy·ty/tz²; 0, 0, 0] | `preprocess.comp::computeCov2D()` |
| ⑤ 2D conic | cov 求逆 × det，加低通滤波 0.3 | `preprocess.comp` L351-358 |
| ⑥ 3-sigma 半径 | λ = mid ± sqrt(mid² - det)，radius = ceil(3·√max(λ₁, λ₂)) | `preprocess.comp` L361-365 |
| ⑦ SH 颜色 | `computeColorFromSH()`，支持 degree 0-3 | `preprocess.comp` L127-239 |
| ⑧ 深度排序 | `std::sort(key = viewDepth, descending)` | C++ STL |
| ⑨ Alpha blend | `C += c · α · T; T *= (1 - α)`，T < 0.0001 提前终止 | `render.comp` L57-109 |
| ⑩ 出图 | stb_image_write 输出 `splat_cpu_baseline.png` | — |

**验收**：
- 解析 scene-0061 30k `.ply` 无报错
- `splat_cpu_baseline.png` 可见场景轮廓
- 数值合理性（depth 分布、radii 范围 > 0、tile 覆盖数 > 0）

---

### Phase 2 — SplatPass 注册 + Preprocess Compute（FR-1 + FR-3，下午前半 ~1.5h）

**目的**：在 MonsterEngine RDG 管线中注册 SplatPass，实现第一个 Compute Shader（Preprocess），验证 GPU 端 dispatch 链路闭环。

**文件落点**：

```
Include/Renderer/Splat/
├── SplatPass.h              # SplatPass 数据结构 + 接口

Source/Renderer/Splat/
├── SplatPass.cpp            # SplatPass 实现 + RDG pass 注册
├── SplatTypes.h             # Gaussian 数据布局定义
├── SplatPLYLoader.h         # .ply 解析 → Storage Buffer
├── SplatPLYLoader.cpp

Shaders/Splat/
├── splat_common.glsl         # 公共 SH/协方差/投影函数（从 preprocess.comp 抽取）
├── splat_preprocess.comp     # 对标 preprocess.comp
├── compile_splat.bat         # GLSL → SPIR-V 离线编译脚本
```

#### SplatPass 注册

在 `FSceneRenderer::Render()` 中或通过独立入口，创建 RDG Pass：

```cpp
builder.AddPass("SplatPreprocess", ERDGPassFlags::Compute,
    [&](FRDGPassBuilder& PassBuilder) {
        // 输入 Storage Buffers（只读）
        PassBuilder.ReadBuffer(PositionsBuffer,  ERHIAccess::SRVCompute);
        PassBuilder.ReadBuffer(ScalesBuffer,     ERHIAccess::SRVCompute);
        PassBuilder.ReadBuffer(RotationsBuffer,  ERHIAccess::SRVCompute);
        PassBuilder.ReadBuffer(OpacitiesBuffer,  ERHIAccess::SRVCompute);
        PassBuilder.ReadBuffer(SHCoeffsBuffer,   ERHIAccess::SRVCompute);
        PassBuilder.ReadBuffer(CameraUniform,    ERHIAccess::SRVCompute);
        // 输出 Storage Buffers（写入）
        PassBuilder.WriteBuffer(RadiiBuffer,         ERHIAccess::UAVCompute);
        PassBuilder.WriteBuffer(DepthBuffer,         ERHIAccess::UAVCompute);
        PassBuilder.WriteBuffer(RGBBuffer,           ERHIAccess::UAVCompute);
        PassBuilder.WriteBuffer(ConicOpacityBuffer,  ERHIAccess::UAVCompute);
        PassBuilder.WriteBuffer(PointsXYBuffer,      ERHIAccess::UAVCompute);
        PassBuilder.WriteBuffer(TilesTouchedBuffer,  ERHIAccess::UAVCompute);
        PassBuilder.WriteBuffer(BoundingBoxBuffer,   ERHIAccess::UAVCompute);
    },
    [=](RHI::IRHICommandList& RHICmdList) {
        // 绑定 Pipeline + Descriptor Set + Push Constants
        RHICmdList.bindDescriptorSet(PipelineLayout, 0, DescriptorSet);
        RHICmdList.pushConstants(PipelineLayout, EShaderStage::Compute,
                                 0, sizeof(PushConsts), &PushConsts);
        RHICmdList.dispatch((gaussianCount + 255) / 256, 1, 1);
    });
```

#### splat_preprocess.comp 编写

直接对拍 `3dgs-vulkan-cpp/vulkan-3dgs/src/Shaders/preprocess.comp`：

| Binding | 类型 | 内容 | 备注 |
|---------|------|------|------|
| 1 | readonly buffer | vec4 positions[] | xyz (world space) |
| 2 | readonly buffer | vec4 scales[] | 3D scale |
| 3 | readonly buffer | vec4 rotations[] | quaternion (r, x, y, z) |
| 4 | readonly buffer | float opacities[] | |
| 5 | readonly buffer | float sh_coefficients[] | 每高斯 shDegree×3 个 float |
| 6 | uniform | CameraUniforms | view/proj/camPos/focal/tan_fov/imageSize/shDegree |
| 7 | writeonly buffer | int radii[] | 像素半径 |
| 8 | writeonly buffer | float depth[] | view-space 深度 |
| 9 | writeonly buffer | vec4 rgb[] | SH 计算的颜色 |
| 10 | writeonly buffer | vec4 conicOpacity[] | conic(3) + opacity |
| 11 | writeonly buffer | vec2 pointsXY[] | 屏幕坐标 |
| 12 | writeonly buffer | uint tilesTouched[] | 每高斯覆盖 tile 数 |
| 13 | writeonly buffer | uvec4 bbox[] | tile 包围盒 (minX, minY, maxX, maxY) |

Push Constants: `gaussianCount`, `near`, `far`, `culling`

Local Size: `256 × 1 × 1`

**关键函数提取到 `splat_common.glsl`**：
- `computeCov3D()` — 3D 协方差
- `computeCov2D()` — Jacobian 投影
- `computeColorFromSH()` — SH 球谐颜色
- `inFrustum()` — 视锥剔除

**验收**：
- RDG 图可见 SplatPass
- Preprocess dispatch 无 Vulkan validation error
- 输出 buffer 数值与 CPU 基线一致（容差 < 1e-3）

---

### Phase 3 — 排序链路（FR-4/5/6/7，下午中段 ~1.5h）

**目的**：实现 GPU 端排序链路的 prefix sum、key assignment 和 tile boundaries。

**文件落点**：

```
Shaders/Splat/
├── splat_prefixsum.comp      # FR-4: ping-pong prefix sum
├── splat_idkeys.comp          # FR-5: 构建排序键
├── splat_boundaries.comp      # FR-7: 每 tile 起止索引
```

#### FR-4 PrefixSum

对拍 `3dgs-vulkan-cpp/vulkan-3dgs/src/Shaders/prefixsum.comp`：

- **输入**：`tilesTouched[N]`（每高斯覆盖 tile 数）
- **输出**：每高斯的 tile 条目起始偏移
- **策略**：两阶段 Ping-Pong
  - Stage 1：局部前缀和（256 threads → 1 block sum）
  - Stage 2：全局修正（将前一 block 累加偏置加到所有元素）

#### FR-5 AssignKeys

对拍 `3dgs-vulkan-cpp/vulkan-3dgs/src/Shaders/idkeys.comp`：

- **输入**：depth + boundingBox + gaussianID
- **输出**：KeyPayload 数组，每高斯 `tilesTouched[i]` 个条目
  - `key = (tileID << 32) | depthBits`
  - `value = gaussianID`

#### FR-6 RadixSort（短期策略：CPU Fallback）

> **GPU Radix Sort 9 阶段实现（Count/Reduce/Scan/Scatter）延后到 W5。**
> 当前阶段使用 CPU 侧 `std::sort` 替代。

CPU Fallback 流程：
1. Preprocess 完成后，从 GPU 读回 `tilesTouched` + `depth` + `boundingBox`
2. Host 端逐高斯枚举 tile 覆盖，拼装 `(key, value)` pair
3. `std::sort` 按 key 排序
4. 上传排序结果回 GPU → 继续 TileBounds + Render

#### FR-7 TileBounds

对拍 `3dgs-vulkan-cpp/vulkan-3dgs/src/Shaders/tile_boundaries.comp`：

- **输入**：排序后的 KeyPayload 数组
- **输出**：`ranges[tileCount] = {startIdx, endIdx}`

**验收**：
- PrefixSum 结果 = `std::partial_sum` 对照一致
- 排序索引序列 = CPU `std::sort` 对照一致
- 每 tile 区间非空且覆盖其高斯

---

### Phase 4 — Render/Blend（FR-8，下午后半 ~1h）

**目的**：实现 tile-based alpha blend，渲染到 swapchain image。

**文件落点**：

```
Shaders/Splat/
├── splat_render.comp    # tile-based alpha blend → swapchain image
```

对拍 `3dgs-vulkan-cpp/vulkan-3dgs/src/Shaders/render.comp`：

- **工作组**：`TILE_WIDTH × TILE_HEIGHT` (16×16)，每个工作组处理一个 tile
- **输入**：
  - `ranges[tile_id]` — 该 tile 的高斯起止索引
  - `gaussianId[]` — 排序后的高斯索引
  - `color[]` / `conic[]` / `pos2d[]` — Preprocess 输出
- **输出**：`image2D`（swapchain image，需要 `VK_IMAGE_LAYOUT_GENERAL`）
- **逻辑**：
  ```
  T = 1.0
  C = vec3(0)
  for gaussian in range(start, end):
      power = -0.5 * (conic.x*dx² + conic.z*dy²) - conic.y*dx*dy
      α = min(0.99, conic.w * exp(power))
      if α < 1/255: continue
      if T*(1-α) < 0.0001: break
      C += color * α * T
      T *= (1-α)
  imageStore(output, pixel, vec4(C, 1-T))
  ```

**注意**：swapchain image 需要从 `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR` 或 `VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL` transition 到 `VK_IMAGE_LAYOUT_GENERAL`（因为 compute shader 以 `image2D` 写入）。

**验收**：
- 渲染出 scene-0061 可见图像
- 无明显崩坏/黑屏/颜色错误

---

### Phase 5 — 收口（FR-10，傍晚 ~0.5h）

1. 渲染截图 → `splat_vulkan.png`
2. 与 CPU baseline + naive 渲染对比 → `splat_compare.png`
3. 写 1 段总结（运行状态 + 差异 + Vulkan compute vs CPU sort FPS 对比）
4. 落定面试话术：「我用自研 Vulkan 引擎原生实时渲染 3DGS」
5. 更新设计文档 §11 TODO 勾选状态

---

## 5. 文件落点总览

```
MonsterEngine/
├── devDocument/3DGS/
│   └── Vulkan原生3DGS_splat_pass开发文档.md    ← 本文档
│
├── Include/
│   ├── RHI/
│   │   ├── IRHIResource.h           [修改] 新增 IRHIComputeShader
│   │   ├── IRHICommandList.h        [修改] 新增 dispatch()
│   │   ├── IRHIDevice.h             [修改] 新增 createComputeShader + createComputePipelineState
│   │   └── RHIDefinitions.h         [修改] 新增 ComputePipelineStateDesc
│   │
│   ├── Platform/Vulkan/
│   │   ├── VulkanShader.h           [修改] 新增 VulkanComputeShader
│   │   ├── VulkanPipelineState.h    [修改] 新增 VulkanComputePipelineState
│   │   ├── VulkanRHICommandList.h   [修改] 新增 dispatch() override
│   │   └── VulkanDescriptorSetLayout.h [修改] 新增 updateStorageBuffer()
│   │
│   ├── Core/
│   │   └── ShaderCompiler.h         [修改] EShaderStageKind 加 Compute
│   │
│   └── Renderer/Splat/
│       └── SplatPass.h              [新建] SplatPass 入口
│
├── Source/
│   ├── Platform/Vulkan/
│   │   ├── VulkanShader.cpp         [修改] 扩展
│   │   ├── VulkanDevice.cpp         [修改] 新增 Compute 系列方法
│   │   ├── VulkanRHICommandList.cpp [修改] 新增 dispatch()
│   │   └── VulkanDescriptorSetLayout.cpp [修改] 新增 updateStorageBuffer()
│   │
│   ├── Core/
│   │   └── ShaderCompiler.cpp       [修改] getStageArgGLSLC 加 "comp"
│   │
│   └── Renderer/Splat/
│       ├── SplatPass.cpp            [新建] SplatPass 实现 + RDG 注册
│       ├── SplatTypes.h             [新建] Gaussian 数据布局定义
│       ├── SplatPLYLoader.h         [新建] .ply 解析
│       ├── SplatPLYLoader.cpp       [新建]
│       └── CPU/
│           ├── SplatCPU.h           [新建] CPU 验证基线
│           └── SplatCPU.cpp         [新建]
│
└── Shaders/Splat/
    ├── splat_common.glsl            [新建] 公共函数（协方差/SH/投影）
    ├── splat_preprocess.comp        [新建] FR-3 Preprocess
    ├── splat_prefixsum.comp         [新建] FR-4 PrefixSum
    ├── splat_idkeys.comp            [新建] FR-5 AssignKeys
    ├── splat_boundaries.comp        [新建] FR-7 TileBounds
    ├── splat_render.comp            [新建] FR-8 Render/Blend
    └── compile_splat.bat            [新建] 离线编译脚本
```

---

## 6. 执行顺序依赖图

```
Phase 0 (基础设施 ~1h)
  │
  ├── 0.1 ComputeShader 类
  ├── 0.2 ComputePipeline 创建链路
  ├── 0.3 IRHICommandList::dispatch()
  ├── 0.4 ShaderCompiler Compute
  └── 0.5 Storage Buffer Descriptor
        │
        │   编译通过 + 可 dispatch
        ▼
Phase 1 (CPU 基线 ~1.5h) ── 可与 Phase 0 部分并行
  │
  ├── 解析 .ply
  ├── 数学管线 (cov3D/投影/SH/sort/blend)
  └── 输出 CPU 基线图
        │
        │   数学正确性确认
        ▼
Phase 2 (Preprocess ~1.5h) ── 依赖 Phase 0 + Phase 1
  │
  ├── SplatPass 注册
  ├── splat_preprocess.comp → SPIR-V
  ├── descriptor set 构建
  └── dispatch 验证
        │
        │   首个 GPU compute pass 跑通
        ▼
Phase 3 (排序链路 ~1.5h) ── 依赖 Phase 2
  │
  ├── splat_prefixsum.comp
  ├── splat_idkeys.comp
  ├── CPU sort fallback (短期)
  └── splat_boundaries.comp
        │
        │   排序链路数值验证通过
        ▼
Phase 4 (Render ~1h) ── 依赖 Phase 3
  │
  ├── splat_render.comp
  └── swapchain image 合成
        │
        │   可见渲染图像
        ▼
Phase 5 (收口 ~0.5h) ── 依赖 Phase 1 + Phase 4
  │
  └── 对比图 + 说明 + 话术
```

---

## 7. 风险与对策

| 风险 | 概率 | 影响 | 对策 |
|------|------|------|------|
| MonsterEngine 已有 3DGS 前向代码**不在**当前源码树 | 中 | Phase 2 少了一个复用点 | CPU 基线阶段（Phase 1）在 Splat 模块内**独立实现全部数学**，不依赖外部前向 |
| `.ply` 格式不兼容（2DGS scale[2] vs vanilla 3DGS scale[3]） | 中 | PLY 解析失败 | Phase 0 第一步先确认 `.ply` 字段；若不兼容，扩展解析器兼容两种格式 |
| GPU Radix Sort 实现复杂、时间不够 | 高 | FR-6 无法在 W4 交付 | **Phase 3 用 CPU sort fallback**（host 端 `std::sort` + GPU upload），GPU radix sort 延后到 W5 |
| Vulkan Compute Pipeline 创建接口链路不全 | 中 | Phase 2 阻塞 | Phase 0 按需补齐，只加最小必要接口（不重构现有 Pipeline 体系） |
| Descriptor Set 更新 Storage Buffer 路径不通 | 低 | Phase 2 阻塞 | Phase 0 确认；若 `updateUniformBuffer` 不支持 Storage，新增专用方法 |
| Swapchain image 作为 compute `image2D` 写入需要 transition | 低 | Phase 4 黑屏 | RDG 自动 barrier 可处理；若跳过了自动 barrier，手动插入 transition |
| AMD GPU `SUBGROUP_SIZE=64` 与 N 卡不同 | 低 | 排序/prefix sum 结果错误 | 先用 N 卡验证；AMD 适配时改 `local_size` 参数 |
| MonsterEngine CMake / 项目文件需同步修改 | 中 | 编译失败 | Phase 0 结束后补全 CMakeLists.txt 的新文件引用 |

---

*文档结束 — 本文档为 MonsterEngine Vulkan 原生 3DGS Splat Pass 开发的执行方案。实现时以关联设计文档的 FR 清单为准，参考仓库代码只做算法对照。*
