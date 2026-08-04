# MonsterEngine 引擎架构（2026-07-09 版）

> 本文档基于对 `D:\code\MonsterEngine` 仓库 **真实代码的静态扫描**（`Include/` + `Source/` + `Shaders/`）整理而成，并对照 `devDocument/` 下既有文档校正偏差。
> 关键论断均附 `文件:行号` 形式的代码锚点，便于在继续开发时直接定位。
> 说明：既有 `引擎的架构和设计.md` 等文档描述的是**早期接口**（如 `RHIFactory`、`TriangleRenderer`、`IRHIBuffer` 老接口），已与当前代码严重脱节，**本文以当前代码为准**。

---

## 0. 速读摘要（给后续开发的你）

- **定位**：参考 UE5 RHI 架构的自研 C++20 渲染引擎，Vulkan / OpenGL 双后端。
- **骨架健康**：分层清晰（Core → RHI → Platform → Renderer/Engine）、头/实现按模块镜像、智能指针 + RAII 全面落地。
- **已可工作的地基**：Vulkan/OpenGL 后端、PBR 头盔渲染、传统前向立方体+阴影、Deferred(GBuffer+TAA/FXAA)、Vulkan GPU 内存管理器、ImGui 编辑器框架、日志/TaskGraph/容器/数学/智能指针。
- **⚠️ 必须知道的两点坑（详见第 9 节）**：
  1. **默认渲染路径（`m_bEnableParallelRendering=true` + `m_bUseRDG=true`）目前画不出东西**——并行分派把命令列表回收而不提交 GPU，RDG 的 Shadow/PostPass 是空 `TODO`。能出图的是传统路径（`else` 分支，关掉两开关）或 `FParallelSceneRenderer` 路径（其单线程回退仍是 `TODO`）。
  2. **`MeshDrawCommand` / `RenderQueue` / 虚拟纹理 / 纹理流送 是"纸面完整但未接入主循环"的重资产**——只在各自 .cpp、`SceneRenderer.cpp` 与 `Tests/` 中出现，默认帧循环走的是 `proxy->DrawWithLighting` 直绘。

---

## 1. 引擎概览

| 项 | 内容 |
|----|------|
| 语言 | C++20（CMake 3.20+，MSVC / Visual Studio 2022） |
| 图形 API | Vulkan 1.0+（主后端）、OpenGL 4.5+（Windows）、OpenGL ES 3.0+（计划中） |
| 平台 | Windows（已落地）；Android / Linux / macOS（计划） |
| 架构原型 | Unreal Engine 5 的 RHI + SceneProxy + RenderDependencyGraph 思路 |
| 参考实现 | UE5、Google Filament、Vulkan Tutorial、LearnOpenGL |
| 构建 | `CMakeLists.txt`（主）+ 遗留 `.sln/.vcxproj`（并存） |
| 入口 | `main.cpp` → 默认 `CubeSceneApplication`（支持 `--cube-scene` / `--deferred` / `--imgui-test`） |

**设计目标**：跨平台、多后端抽象、现代 C++、模块化、GPU 驱动渲染、调试友好（Validation Layer + Debug Markers + `MR_LOG`）。

---

## 2. 整体分层架构

```
┌──────────────────────────────────────────────────────────────┐
│ 应用层 Application   main.cpp · CubeSceneApplication · Editor   │
└───────────────────────────────┬──────────────────────────────┘
                                 │ uses
┌───────────────────────────────▼──────────────────────────────┐
│ 引擎层 Engine         Engine · Scene Graph · Actor/Component    │
│                        Camera · Material · Mesh · Texture ·     │
│                        ShaderManager                            │
└───────────────────────────────┬──────────────────────────────┘
                                 │ uses
┌───────────────────────────────▼──────────────────────────────┐
│ 高级渲染层 Renderer    FScene · 可见性 · MeshDrawCommand ·       │
│                        RenderQueue · PBR · Deferred · Shadow ·  │
│                        Forward · PostProcess · FParallelScene…  │
└──────────┬─────────────────────────────────────┬──────────────┘
           │ uses                                  │ uses
┌──────────▼───────────────────┐   ┌──────────────▼──────────────┐
│ RHI 抽象层                    │   │ RDG 渲染依赖图              │
│ IRHIDevice/CommandList/…      │   │ FRDGBuilder/Pass/Resource   │
└──────────┬───────────────────┘   └──────────────┬──────────────┘
           │ implements                             │ uses
┌──────────▼─────────────────────────────────────────────────────┐
│ 平台实现层 Platform   Vulkan/ (VulkanDevice…) · OpenGL/ · GLFW   │
└───────────────────────────────┬─────────────────────────────────┘
                                 │ calls
┌───────────────────────────────▼─────────────────────────────────┐
│ 图形 API 层            Vulkan │ OpenGL │ (D3D12 / Metal 占位未实现)│
└─────────────────────────────────────────────────────────────────┘
```

**依赖方向（单向向下）**：Application → Engine → Renderer → RHI → Platform → API；RDG 依赖 RHI 并被 Renderer 调用。Core / Containers / Math 为横向基础层，被所有上层依赖。**严禁下层依赖上层、Core 依赖任何引擎模块**（见 `devDocument/Architecture/ModuleDependencies.md`）。

---

## 3. 模块详解

### 3.1 Core（`Include/Core` · `Source/Core`）
引擎基础设施与生命周期。
- **`Application.h`**：应用框架基类，`run()` → `onInitialize()`/`onUpdate()`/`onRender()` → `present()`。
- **内存**：`Core/HAL/FMemoryManager.h`（`FMemoryManager` 单例：`Initialize/Shutdown/GetAllocator`）+ `FMallocBinned2`（`Source/Core/HAL/FMallocBinned2.cpp`，UE5 风格分箱分配器）。
- **日志**：`Core/Logging/`（`MR_LOG` 宏、`DEFINE_LOG_CATEGORY_STATIC`、文件/控制台/调试输出设备）。`main.cpp:80` 最先初始化。
- **并发**：`FTaskGraph.h`（`QueueTask` + `FGraphEvent`）、`FRunnable`/`FRunnableThread`、`FGraphEvent`。
- **HAL / IO / Input / Window / Assert / Color / ShaderCompiler**：平台抽象底座、文件 IO、输入事件、窗口、断言、着色器离线编译（`ShaderCompiler` 调 `glslc`/`dxc`，含时间戳热重载与 `.spv` 缓存）。

### 3.2 Containers（`Include/Containers` · header-only）
UE5 风格容器与智能指针：`TArray`/`TMap`/`TSet`/`TSparseArray`/`TBitArray`/`FString`/`FName`/`FText`，以及 `TSharedPtr`/`TUniquePtr`/`TWeakPtr`（`MakeShared`/`MakeUnique`）。仅 `Source/Containers/Text.cpp` 有实现，其余模板内联。

### 3.3 Math（`Include/Math` · header-only）
`FVector`/`FVector4`/`FMatrix`/`FQuat`/`FTransform`/`FLinearColor` 等。`Source/Math/` 为空（纯头文件内联），编译期成本集中在头文件，已纳入 PCH。

### 3.4 RHI（`Include/RHI` · `Source/RHI`）
渲染硬件抽象层——引擎与具体图形 API 之间唯一的耦合边界。
- **`IRHIDevice`**（`IRHIDevice.h`）：资源工厂 + 队列/交换链/呈现。`createBuffer/Texture/VertexShader/PixelShader/PipelineState/Sampler`、`createDescriptorSetLayout/PipelineLayout/allocateDescriptorSet`、`getImmediateCommandList()`、`present()`、`waitForIdle()`、`getRHIBackend()`。
- **`IRHICommandList`**（`IRHICommandList.h`）：录制状态机 `ERHICommandListState`（NotAllocated→Recording→Recorded→Executing→Executed）。`begin/end/reset/FinishRecording`；资源绑定 `setVertexBuffers/bindDescriptorSets/pushConstants`；绘制 `draw/drawIndexed/drawInstanced`；`setRenderTargets/clearRenderTarget/endRenderPass`；**屏障** `transitionResource(EResourceUsage)` 与 `transitionResource(ERHIAccess)` 双版本 + `resourceBarrier()`；UE5 风格 `FGraphEventRef` 派发依赖。
- **`IRHIResource`** 族：`IRHIBuffer`/`IRHITexture`/`IRHIVertexShader`/`IRHIPixelShader`/`IRHIPipelineState`/`IRHISampler`。
- **`IRHIDescriptorSet` / `IRHISwapChain`**：multi-descriptor-set 设计（Set0/1/2，由 PBR `FPBRDescriptorSetManager` 体现）。
- **`RHI.h` / `RHIDefinitions.h` / `RHIResources.h` / `RHIBarriers.h`**：工厂入口、类型枚举、资源句柄、屏障定义。
- **多线程命令系统**（关键架构资产）：
  - `FRHIThread`（`FRHIThread.h`）：单例 RHI 线程（`FRunnable`），`QueueTask`/`WaitForTasks`/`IsInRHIThread`。
  - `FRHICommandListPool`（`FRHICommandListPool.h`）：单例命令列表池，`AllocateCommandList`/`RecycleCommandList`，`ECommandListType`(Graphics/Compute/Transfer)，`FScopedCommandList` RAII。
  - `FRHICommandListParallelTranslator`（`FRHICommandListParallelTranslator.cpp`）：为每个 worker 线程分配 Vulkan secondary command buffer，经 `VulkanRHICommandListRecorder::ReplayToVulkanCommandBuffer()` 回放，**翻译并收集到 `FTranslatedCommandBufferCollection`**（`ExecuteInPrimaryCommandBuffer` 内 `vkCmdExecuteCommands`）。这是**唯一真正可工作的并行录制路径**。
  - `FTranslatedCommandBufferCollection`：聚合 secondary command buffer 供主命令缓冲执行。

### 3.5 Platform（`Source/Platform`）
具体后端实现。
- **Vulkan**（`Source/Platform/Vulkan`，~25 个 .cpp）：`VulkanDevice`/`VulkanRHICommandList`/`VulkanRHICommandListRecorder`/`FVulkanMemoryManager`/`FVulkanResourceManager`、Descriptor 全套、`CommandBuffer/Pool/Context`、`PipelineState`、`Sampler`、`Shader`、`Texture`、`Swapchain`、`ResourceStateTracker`。`VulkanDevice.cpp:197` 接线 UE5 风格 sub-allocation 的 `FVulkanMemoryManager`。
- **OpenGL**（`Source/Platform/OpenGL`，~11 个 .cpp）：`FOpenGLDevice`/`OpenGLCommandList`/`OpenGLContext`/`OpenGLDescriptorPoolManager`/`OpenGLPipeline`/`OpenGLResources`/`OpenGLShaders`/`OpenGLSwapChain`。仅 `#if PLATFORM_WINDOWS`。
- **GLFW**（`Source/Platform/GLFW`）：跨平台窗口与输入；Vulkan surface 集成、事件回调、多窗口。
- **后端矩阵（真实代码）**：`Source/RHI/RHI.cpp::createDevice()` 实证——Vulkan ✅、OpenGL ✅（Windows）、**D3D12 / D3D11 / Metal ❌ 仅有 `MR_LOG_WARNING("...not yet implemented")` 占位，无任何源文件**、Android/ES ❌ 无专属后端接线（着色器有 `_GL` 变体但无 ES 后端）。

### 3.6 Renderer（`Include/Renderer` · `Source/Renderer`）
高级渲染系统。
- **`FScene`**（`Scene.h:530`）：并行数组管理 primitives/lights，`BeginFrame/EndFrame`。
- **可见性/剔除**：`SceneVisibility.h`（`FSceneVisibilityManager`/`FFrustumCuller`/`FDistanceCuller`/`FOcclusionQueryManager`，MaxPendingQueries=4096）；`RendererSceneRenderer.cpp` 含完整 `ComputeViewVisibility()`/`OcclusionCull()`/`GatherDynamicMeshElements()` 与 `FForwardShadingSceneRenderer::Render()`。
- **`MeshDrawCommand`**（`MeshDrawCommand.h/.cpp`）：UE5 风格 `FMeshDrawCommand`/`FParallelMeshDrawCommandPass`/`FMeshPassProcessor`（`FDepthPass/BasePass/ShadowDepthPass` MeshProcessor）、`SubmitDraw`/`BuildRenderingCommands`/`DispatchDraw`。**框架完整，但默认帧循环未调用**（见第 9 节）。
- **`RenderQueue`**（`RenderQueue.h/.cpp`）：`FRenderQueue`/`FRenderQueueManager` 渲染队列。**同上，未接入主路径**。
- **PBR**（`Renderer/PBR/`）：`FPBRRenderer`/`FPBRMaterial`/`FPBRDescriptorSetManager`/`FPBRDescriptorSetLayouts`/`PBRDefaultTextures`。metallic-roughness、Cook-Torrance、clear coat、IBL、AO、emissive。
- **Deferred**（`Engine/Deferred/FDeferredRenderer.h`）：GBuffer + TAA + FXAA（`CreateTAAPipeline`/`GenerateJitter(Halton)`/`RenderTAAPass`/`RenderFXAAPass`）。
- **Shadow**（`ShadowDepthPass.h`/`ShadowProjectionPass.h`/`ShadowRendering.h`）：级联阴影相关 pass 与函数。
- **Forward**（`ForwardRenderPasses.h`/`ForwardShaderCompiler.h`/`RenderPasses.h`/`RenderPass.h`）：前向渲染 pass 集合。
- **PostProcess**（`PostProcess/`）：后处理 pass。
- **`FParallelSceneRenderer`**（`ParallelSceneRenderer.h`）：并行场景渲染器，`Render()` 流程 BeginFrame→InitViews→DispatchParallelRenderPasses→WaitForParallelTasks→ExecuteSecondaryCommandBuffers→EndFrame；其单线程回退为 `// This will be implemented when integrating with CubeSceneApplication`（`:73`）。
- **纹理**：`FTextureLoader`/`FTexturePool`/`FTextureStreamingManager`/`FVirtualTextureSystem`（后两者见第 9 节）。

### 3.7 RDG（`Include/RDG` · `Source/RDG`）
渲染依赖图（仿 UE5 `FRDGBuilder`）。
- **`FRDGBuilder`**（`RDGBuilder.cpp`）：`createTexture/createBuffer`/`registerExternalTexture`（外部 RHI 纹理包装为 `FRDGTexture`）；编译期 `_cullUnusedPasses`/`_compile`（依赖排序 + 插入 `FRDGTransition`）；`_allocateResources`（为 RDG 自有纹理调 `createTexture`）；`execute` 按 pass 顺序 `transitionResource`→`_setupRenderTargets`→`pass->execute(rhiCmdList)`→`endRenderPass`；`_validateGraph`（`#if RDG_ENABLE_DEBUG`）。
- **`FRDGPass`**（`RDGPass.h:149`）：`execute(IRHICommandList&)` 纯虚；`TRDGLambdaPass`（普通 lambda）、`TRDGParallelPass`（并行，lambda 返回 `TArray<FGraphEventRef>`）。
- **`FRDGResource`/`FRDGTexture`/`FRDGBuffer`**：子资源状态追踪（`FRDGSubresourceState`，`RDGResource.h:55`，按 `ERHIAccess` 自动插入屏障）。
- **衔接**：通过 `registerExternalTexture` 接入 Renderer 的 ColorTarget/DepthTarget/ShadowMap；pass lambda 内直接使用 `IRHICommandList`。
- **状态**：框架完整实现，**但默认 demo 的 RDG 路径大半为空壳**（Shadow/PostPass execute 为 `// TODO`，`_releaseResources` 为 `// TODO: Implement in Phase 4` 桩）。

### 3.8 Engine（`Include/Engine` · `Source/Engine`）
引擎核心与场景系统。
- **`Engine`**：核心类 `initialize/shutdown/run`、持有 `TUniquePtr<IRHIDevice>`。
- **场景图**：`Scene.h`/`SceneInterface.h`/`SceneTypes.h`；`FPrimitiveSceneProxy`/`FPrimitiveSceneInfo`/`FLightSceneProxy`/`FLightSceneInfo`/`LightPrimitiveInteraction`；`ConvexVolume`/`Octree`/`SceneOctree`。`Source/Engine/SceneRenderer.cpp` 含另一套 `FSceneRenderer`（`RenderDepthPrepass/BasePass/ShadowDepths/Lighting/Translucency/PostProcess`）。
- **Actor/Component**：`Actor.h`/`Actors/`、`Components/`（`UActor`/`UComponent`、`UCubeMeshComponent`/`UFloorMeshComponent`/`ULightComponent`/`UPrimitiveComponent`），驱动 `GetSceneProxy()` 产出 `FPrimitiveSceneProxy`。
- **相机**：`Camera/CameraManager.h`（`FCameraManager`：`SetViewTarget`/`BlendViewTargets`/`UpdateCamera`/`LimitViewPitch`）+ `FFPSCameraController`。
- **材质**：`Material/MaterialTypes.h`（`EMaterialShadingModel` 含 ClearCoat/Subsurface/Hair/Cloth、`EMaterialBlendMode`、`EMaterialDomain`、参数值结构）；落地实现为 `FPBRMaterial`。
- **网格/纹理**：`Mesh/`、`Texture/Texture2D.h`、`Asset/GLTFLoader.cpp`（glTF 2.0 加载）。
- **ShaderManager**：`Shader/ShaderManager.h`（`CompileVertexShader/CompilePixelShader`/`CheckForChanges()` 热重载/`SaveCachedBytecode`/`LoadCachedBytecode`）。
- **注意**：存在 **两套 Scene 体系**——`Engine` 命名空间走 `UActor/UComponent`，`Renderer` 命名空间走 `FPrimitiveSceneProxy` 直接驱动；且 `Engine::FSceneRenderer`、`Renderer::FSceneRenderer`、`FParallelSceneRenderer` **三套场景渲染器并存**，默认帧循环实际**一个都没真正调用**（走 `CubeSceneApplication` 自带 `render*()` 函数）。

### 3.9 Editor（`Include/Editor` · `Source/Editor`）
ImGui 集成：`FEditorApplication` + `ImGuiContext`/`ImGuiRenderer`/`ImGuiInputHandler`（统计面板、材质参数编辑、场景层级查看器）。`main.cpp` 仅 `--imgui-test` 触发 `createImGuiTestApplication()`，未接 `FEditorApplication`。

### 3.10 Shaders（`Shaders/`）
GLSL 源 + 预编译 `.spv`（编译脚本 `compile_shaders.bat`）。
- 目录：`Common`(BRDF/光照/阴影工具) · `Deferred`(Geometry/Lighting/TAA) · `Forward` · `Lighting` · `Material` · `PBR` · `PostProcess`。
- 变体：Vulkan 用 `.spv`，并提供 `_GL` GLSL 变体供 OpenGL 后端。

### 3.11 跨切面系统
- **内存管理**：CPU 侧 `FMemory`/`FMemoryManager`/`FMallocBinned2`；GPU 侧 `FVulkanMemoryManager`（VMA 风格 sub-allocation、资源追踪、预算）。
- **日志**：`MR_LOG(LogCategory, Level, fmt, ...)`，级别 `VeryVerbose/Verbose/Log/Warning/Error/Fatal`。
- **智能指针/容器/数学**：见 3.2 / 3.3。
- **TaskGraph**：`FTaskGraph::QueueTask` + `FGraphEvent`，被 RDG 并行 pass、并行分派、`dispatch*` 大量使用。

---

## 4. 渲染主流程（调用链）

**启动**：`main.cpp:74` → 默认 `CubeSceneApplication`（或 `--deferred`/`--imgui-test`）→ `Application::run()` → `onInitialize()`（经 `RHIFactory::createDevice()` 建 `IRHIDevice`，默认 Vulkan）。

**每帧 `onRender()`**（`CubeSceneApplication.cpp:516` 分支，已逐行核对）：

```
onRender(cmdList)
 ├─ m_bEnableParallelRendering && m_bUseRDG        → renderWithRDGParallel(...)   [默认 ⚠️ 空壳]
 ├─ m_bEnableParallelRendering && m_parallelRenderer → renderWithParallelRenderer() [经 FParallelSceneRenderer]
 ├─ m_bUseRDG /*&& shadows*/                       → renderWithRDG(...)
 └─ else                                            → 传统路径（真正出图 ✅）
       renderShadowDepthPass(...)     // :567
       renderCubeWithShadows(...)     // :599  proxy->DrawWithLighting()
       renderHelmetWithPBR(...)       // :611
     → endRenderPass / end / present
```

**传统可工作路径的逐对象绘制**（证据 `CubeSceneApplication.cpp:567-621`）：`proxy->DrawWithLighting(cmdList, view, proj, camPos, lights)` 直接录制到即时命令列表并提交 GPU。

**并行路径的数据流**（`renderWithParallelRenderer` → `FParallelSceneRenderer::Render`）：`DispatchParallelRenderPasses` 调用 `dispatchBasePass/dispatchPBRPass/dispatchShadowDepthPass`（各自 `FTaskGraph::QueueTask` → 从 `FRHICommandListPool` 取命令列表 → 录制几何 → 经 `FRHICommandListParallelTranslator` 翻译为 Vulkan secondary command buffer → 收集到 `FTranslatedCommandBufferCollection` → 主命令缓冲 `vkCmdExecuteCommands` 执行）。**注意**：`dispatch*` 当前在录制后直接 `RecycleCommandList` 而未提交翻译器（见第 9 节）。

---

## 5. 支持的功能清单（逐项状态）

| 功能 | 状态 | 证据 / 备注 |
|------|------|------|
| **PBR**（metallic-roughness, Cook-Torrance, IBL, clear coat, AO, emissive, normal mapping） | ✅ 已实现 | `Renderer/PBR/PBRMaterial.h` + `PBRRenderer.h`；`Shaders/PBR/*`；`renderHelmetWithPBR`(:611) |
| **Forward 渲染** | ✅ 着色器+pass | `Shaders/Forward/ForwardLit`；`ForwardRenderPasses` |
| **Clustered Forward** | ❌ 未实现 | 全仓无实现 |
| **Deferred 渲染**（GBuffer+TAA+FXAA） | ✅ MVP 可用 | `Engine/Deferred/FDeferredRenderer.h`；`--deferred` 触发；`Shaders/Deferred/*` |
| **阴影**（Shadow Map / CSM 级联） | ⚠️ 资源就绪，默认未接入 | `ShadowDepthPass/ShadowProjectionPass` + `renderShadowDepthPass`(:2099)/`renderCubeWithShadows`(:2176)；传统路径可用，RDG 路径为 TODO |
| **后处理 TAA / FXAA** | ⚠️ 资源/着色器就绪，接入不全 | `FDeferredRenderer::RenderTAAPass/RenderFXAAPass`；`Shaders/Deferred/TAAPass`、`PostProcess/FXAAPass`；RDG PostPass 未串起 |
| **材质系统** | ✅ 类型完备 | `Material/MaterialTypes.h` 多 ShadingModel；落地点 `FPBRMaterial` |
| **纹理系统**（mipmap / 加载 / 默认纹理） | ✅ 可用 | `FTextureLoader`/`FTexturePool`/`Texture2D`；`.spv` 由 `ShaderManager` 加载 |
| **虚拟纹理 VirtualTexture** | ⚠️ 仅声明+测试，主路径未接入 | `FVirtualTextureSystem.h/.cpp` 仅被自身+`Tests/` 引用，渲染代码零引用 |
| **纹理流送 TextureStreaming** | ⚠️ 仅声明+测试，主路径未接入 | `FTextureStreamingManager` 同上 |
| **场景管理（Scene Graph / Proxy）** | ✅ 两套并存 | `Renderer::FScene` + `Engine` 命名空间 `FScene`/`UActor` |
| **相机系统** | ✅ 可用 | `CameraManager` + `FFPSCameraController` |
| **可见性/剔除**（Frustum/Occlusion/Distance + Octree 空间划分） | ✅ 实现但未接入主循环 | `SceneVisibility.h` + `RendererSceneRenderer.cpp`；默认帧循环走直绘 |
| **MeshDrawCommand / RenderQueue** | ⚠️ 框架完整，未接入主循环 | `MeshDrawCommand.cpp`/`RenderQueue.cpp` 仅被 `Tests/CubeSceneRendererTest*` 引用 |
| **Shader 系统**（GLSL→SPIR-V、热重载、字节码缓存） | ✅ 可用 | `Engine/Shader/ShaderManager.h`；`Core/ShaderCompiler`；`compile_shaders.bat` |
| **RDG 渲染依赖图** | ⚠️ 框架完整，默认路径空壳 | `RDGBuilder` 实现完整；默认 demo 的 Shadow/Post Pass 为 TODO |
| **ImGui 编辑器** | ✅ 框架实现，仅测试入口 | `Editor/` + `--imgui-test`；`FEditorApplication` 未接入主程序 |
| **GPU 内存管理**（VMA 风格 sub-allocation） | ✅ 可用 | `FVulkanMemoryManager`（`VulkanDevice.cpp:197` 接线） |
| **CPU 内存管理**（FMallocBinned2） | ✅ 可用 | `Core/HAL/FMallocBinned2.cpp` |
| **容器 / 数学 / 智能指针 / 日志 / TaskGraph** | ✅ 可用 | 见 3.2/3.3/3.1 |
| **多线程并行命令录制** | ⚠️ 路径存在但未闭环 | `FRHICommandListParallelTranslator` 能翻译+执行（真正工作的并行路径），但 `dispatch*` 未提交；`FParallelSceneRenderer` 单线程回退为 TODO |
| **后端：Vulkan** | ✅ Windows 落地 | `Source/Platform/Vulkan` |
| **后端：OpenGL** | ✅ Windows 落地 | `Source/Platform/OpenGL` |
| **后端：D3D12 / D3D11 / Metal** | ❌ 占位未实现 | 仅工厂分支/日志提示，无源文件 |
| **后端：Android / OpenGL ES** | ❌ 计划中 | 无专属后端接线 |

图例：✅ 已落地可用　⚠️ 已实现/资源就绪但未完全接入主渲染循环或默认路径　❌ 未实现（仅占位/计划）

---

## 6. 关键技术点与设计模式

- **工厂模式**：`RHIFactory::createDevice()` 按 `preferredBackend` 选择后端实现（当前仅 Vulkan/OpenGL 真实分支）。
- **抽象工厂**：`IRHIDevice` 作为各平台资源家族的工厂（`createBuffer`→`VulkanBuffer` 等）。
- **接口隔离**：`IRHIDevice` / `IRHICommandList` / `IRHIResource`(+子类) 职责分离。
- **RAII + 智能指针**：所有 GPU 资源生命周期由 `TSharedPtr/TUniquePtr` 管理；`IRHIResource` 禁止拷贝、允许移动。
- **命令模式**：`IRHICommandList` 各方法即延迟执行的渲染命令。
- **资源状态追踪 / Barrier**：`EResourceUsage` 与 `ERHIAccess` 双版本 `transitionResource`；RDG 以 `FRDGSubresourceState` 按 `ERHIAccess` 自动插入屏障（`RDGBuilder::_executeTransitions`）。
- **任务图并行**：`FTaskGraph::QueueTask` + `FGraphEvent` 驱动并行分派与 RDG 并行 pass。
- **描述符集自动生成**：由 SPIR-V 反射（`VulkanShader::performReflection`）聚合生成 `VkDescriptorSetLayout`/`VkPipelineLayout`，UE5 风格绑定 API（`SetShaderUniformBuffer/Texture/Sampler`）。
- **PCH**：`Core/MonsterEnginePCH.h` 经 `target_precompile_headers` 加速编译（ImGui/GLTFLoader 除外）。

---

## 7. 内存管理策略

- **CPU**：`FMemory`/`FMemoryManager` 单例 + `FMallocBinned2` 分箱分配；容器/智能指针零裸 `new`。
- **GPU（Vulkan）**：`FVulkanMemoryManager` 实现 VMA 风格 sub-allocation、`FVulkanResourceManager` 资源追踪、`ResourceStateTracker` 状态机；预算/统计可查。
- **命令列表**：`FRHICommandListPool` 对象池 + RAII `FScopedCommandList` 复用，降低分配开销。

---

## 8. 性能设计要点

- **Descriptor/Command/Pipeline 缓存**：`FPBRDescriptorSetManager`、Pipeline State 缓存、`FRHICommandListPool` 复用。
- **GPU 驱动渲染**：资源状态追踪最小化无效转换；并行命令录制（`FRHICommandListParallelTranslator`）。
- **CPU 侧**：TaskGraph 并行、内存池、`FMallocBinned2`、SIMD 数学（计划）。
- **调试**：Vulkan Validation Layers、`VK_EXT_debug_utils` 标记/事件、`MR_LOG` 分级日志、RenderDoc 集成。

---

## 9. ⚠️ 当前实现状态与需注意的坑（继续开发必读）

> 以下为静态扫描确认的**真实现状**，与 `devDocument` 中部分"已完成"文档不符，开发前务必知悉。

### 9.1 默认渲染路径画不出东西（最高优先级）
- 默认 `m_bEnableParallelRendering = true`（`CubeSceneApplication.cpp:90`）+ `m_bUseRDG = true`（`:516` 分支）→ 走 `renderWithRDGParallel`（`:520`）。
- `dispatchBasePass/dispatchPBRPass/dispatchShadowDepthPass`（`:2608/:2709/:2758`）把几何录进 `FRHICommandListPool` 的命令列表后，**直接 `RecycleCommandList` 回收（`:2700/:2749/:2808`），未提交给 `FRHICommandListParallelTranslator`**（代码注释 `// TODO: Submit to parallel translator`）。
- `renderWithRDGParallel` 内 `ShadowDepthPass`/`PostProcessPass` 的 execute lambda 为 `// TODO` 空实现（`:2865` 等）。
- **结果**：默认 Vulkan demo 的 `present()` 呈现近乎空白帧。**能出图的是传统 `else` 分支（`:538`，需关掉两开关）或 `renderWithParallelRenderer`（走 `FParallelSceneRenderer` 真正翻译+执行 secondary command buffer）**。
- **建议**：要么让 `dispatch*` 真正 `QueueParallelTranslate` + `ExecuteInPrimaryCommandBuffer`，要么默认切到 `FParallelSceneRenderer` 路径并补全其单线程回退。

### 9.2 "纸面完整但未接入主循环"的重资产
- `MeshDrawCommand` / `RenderQueue`：完整 UE5 风格框架，**仅被 `Tests/CubeSceneRendererTest*` 引用**；默认帧循环走 `proxy->DrawWithLighting` 直绘，未使用排序/合并。
- `FVirtualTextureSystem` / `FTextureStreamingManager`：仅有 `.h/.cpp` + `Tests/` + `devDocument` 文档，**主渲染代码零引用**。`devDocument/虚拟纹理系统实现文档.md`/`纹理流送系统实现文档.md` 称"已接入"不实。
- **建议**：明确这些子系统是"接进主循环"还是"从文档降级标注"，避免误导后续接手者。

### 9.3 三套场景渲染器并存
`Engine::FSceneRenderer`（`Source/Engine/SceneRenderer.cpp`）、`Renderer::FSceneRenderer`（`RendererSceneRenderer.cpp`）、`FParallelSceneRenderer` 并存；默认帧循环实际调用的是 `CubeSceneApplication` 自带的 `render*()` 函数，三者都未被统一驱动。**建议**后续统一到 `FParallelSceneRenderer` 或 `Renderer::FSceneRenderer` 单一入口。

### 9.4 文档严重过时
- `devDocument/引擎的架构和设计.md` 描述的是 `RHIFactory`/`TriangleRenderer`/`IRHIBuffer`/`IRHITexture` 等**旧接口与类**，实际代码已演进为 multi-descriptor-set + `IRHIDevice`/`IRHICommandList`/`FRDGBuilder` 形态，`TriangleRenderer` 已不存在（仅 `Shaders/Triangle.*` 残留）。
- `cubescene-multithread-integration`、`ParallelRenderingIntegrationProgress`、`Vulkan多线程渲染面条文档` 等大量"进度/集成"文档描述的是尚未在主循环落地的设计。
- **建议**：以本文（或刷新后的主架构文档）为准，逐步重写上述过时文档。

---

## 10. 后续开发切入建议（基于现状）

1. **先修默认路径**：让 `dispatch*` 真正提交翻译器，或默认切到 `FParallelSceneRenderer`——这是让 demo "开箱出图" 的前提。
2. **统一渲染入口**：收敛 `Engine/Renderer/Parallel` 三套场景渲染器为单一 `FParallelSceneRenderer` 驱动，`MeshDrawCommand`/`RenderQueue` 才有用武之地。
3. **决策重资产去留**：`VirtualTexture`/`TextureStreaming` 要么接进 `Texture2D` 加载管线，要么文档降级。
4. **补后端**：D3D12/Metal/Android-ES 目前为零，按需从工厂分支起步。
5. **刷新 devDocument**：以本文为基线，修订架构主文档与"已完成"类文档，消除漂移。

---

## 11. 附录：既有文档对照

| 既有文档 | 状态 | 说明 |
|----------|------|------|
| `引擎的架构和设计.md`（+ v4/大页补充） | ⚠️ 过时 | 描述旧接口（RHIFactory/TriangleRenderer/IRHIBuffer），与当前代码不符 |
| `Architecture/OverallArchitecture.md` · `ModuleDependencies.md` · `DesignPatterns.md` | ✅ 仍适用 | 分层/依赖/设计模式概念依然准确 |
| `RDG系统开发文档-第1~4部分` | ✅ 框架参考 | RDG 设计描述准确，但默认路径接入为空壳 |
| `Vulkan内存管理系统*` · `FMemory*` | ✅ 准确 | GPU/CPU 内存管理描述与代码一致 |
| `虚拟纹理系统实现文档.md` · `纹理流送系统实现文档.md` | ⚠️ 夸大 | 称"已接入"，实际主路径零引用 |
| `DeferredRenderingDesign.md` · `TAA-*` · `ShadowMapping_*` | ✅ 设计参考 | 设计准确，接入程度见第 5/9 节 |
| `Cube渲染流程*` · `CubeScene*` | ⚠️ 部分过时 | 含早期 TriangleRenderer 流程，当前为 CubeSceneApplication |

---

*文档生成：2026-07-09 ｜ 依据 `Include/`+`Source/`+`Shaders/` 静态扫描 + `devDocument/` 对照 ｜ 关键论断附 `文件:行号` 锚点。*
