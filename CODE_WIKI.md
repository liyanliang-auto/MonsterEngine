# MonsterEngine Code Wiki

更新：2026-10-18（北京时间）。用途：AI方案与开发的源码导航；不是功能验收报告。
本轮起始仓库 HEAD：`e275df73277eb58d65e2a9fe6221ee7f707815b8`；工作区含未提交修改，HEAD不能单独代表当前内容。
已知原有修改包括 `Source/Renderer/Splat/Streaming/SplatGPUPagePool.cpp` 以及未跟踪开发文档/素材；后续开发不得覆盖。
本索引以当前源码为准；历史性能、测试数量仅在对应文档和构建身份下成立。
构建与验证入口见根目录 [AGENTS.md](AGENTS.md)；一般使用见 [README.md](README.md)、[docs/3dgs.md](docs/3dgs.md)。

## 1. 项目定位与当前边界

- C++20，主要开发/验证环境为Windows，Vulkan和桌面OpenGL后端。
- 原生Vulkan Compute 3DGS、稳定Radix排序、16×16 Tile合成。
- 具备PLY加载、离线LOD、后台分页、GPU驻留预算、相机序列和遥测。
- Android已有Gradle客户端、JNI、单原生渲染owner与双ABI monster_android构建target；完整构建/模拟器验收以本轮实际日志为准。
- 尚无物理手机；ARM真机、实际16KiB页运行、图像质量阈值与AC17长测未获验收，不能由源码或语法检查外推。
- 不要把前述百万高斯桌面展示结果外推到手机；小场景与能力探针需单独验收。

## 2. 目录职责

| 目录/文件 | 职责 |
| --- | --- |
| `main.cpp` | 桌面CLI解析、样例和测试分发 |
| `Include/`、`Source/` | 头文件与实现，按Core/RHI/Platform/Renderer/Engine等模块对应 |
| `Source/Core` | Application、窗口工厂、日志、内存、线程、异步I/O、Shader文件/编译工具 |
| `Include/Math` | 模板向量/矩阵/相机投影，SSE/FPU实现与SIMD选择 |
| `Source/RHI` | 设备/资源/命令接口、并行命令和GPU原语 |
| `Source/Platform/Vulkan` | Vulkan loader、设备/队列/Swapchain、资源、同步与命令实现 |
| `Source/Platform/OpenGL` | 桌面OpenGL路径；不等同Android GLES支持 |
| `Source/Platform/GLFW` | 桌面窗口、鼠标键盘输入 |
| `Source/Renderer/Splat` | 高斯加载、投影、排序、Raster、LOD/Streaming、序列/导出 |
| `Source/Engine`、`Source/Renderer` | 场景、材质、灯光、Deferred、后处理等通用渲染能力 |
| `Source/Editor` | 桌面编辑器和ImGui适配 |
| `Shaders/Splat` | GLSL Compute及present shader，子目录Sort/Render |
| `Tools/SplatLODBuilder` | 离线生成分页LOD资源 |
| `Tools/SplatMobileLODBuilder` | 独立有界外存LOD构建与原生场景校验CLI |
| `Tools/Android` | Shader清单、程序化场景/资源打包、双ABI构建和ELF/ZIP静态检查 |
| `android/app` | Kotlin UI、SAF场景库、前台LOD任务、诊断与JNI桥；场景导入经私有staging、原生校验和fsync后原子发布 |
| `Source/Platform/Android` | 原生资源、独立能力探针、render/build owner、JNI和性能策略 |
| `cmake/MonsterSources.cmake` | 显式生产源清单；Core/Vulkan/Splat/Windows/Android分组 |
| `cmake/MonsterAndroid.cmake` | Android共享库、静态libc++、生成资源和16KiB链接参数 |
| `Source/Tests/3DGSLODStreaming` | CPU/GPU/integration正式验证入口与子专题fixture |
| `cmake/SplatLODStreaming.cmake` | CPU构建器、离线Shader编译和正式测试target |
| `resources/3dgs_sequences` | 场景/相机/性能配置样例；外部模型需另行准备 |
| `devDocument/3DGS` | 总计划、实现说明、实验限制及本轮Android方案 |
| `specs/20261017-175742-crnp` | 本轮spec-forge状态、编码交接及实际验证日志；交接PASS不是全局验收 |

## 3. 主应用与引擎关系

```text
main.cpp
  -> SplatSceneApplication : Application
      -> Application::initialize : FMemoryManager / WindowFactory / Engine
      -> Application::run / tick : pollEvents -> onUpdate -> onRender
      -> Engine::initialize : MemorySystem + RHIFactory::createDevice
      -> VulkanDevice : 资源/命令/交换链/完成token
      -> FSplatPipeline : Compute高斯渲染 -> 离屏输出 -> present pass
```

- `Include/Core/Application.h` / `Source/Core/Application.cpp`：拥有窗口和Engine，当前run是阻塞桌面循环；Android不能在UI线程调用。
- `Include/Engine.h` / `Source/Engine.cpp`：拥有IRHIDevice；Engine::run目前是基础示例循环，不能误认为完整Viewer入口。
- `Include/SplatSceneApplication.h` / `Source/SplatSceneApplication.cpp`：组合场景、相机、LOD、GPU页池、Benchmark、ImGui及present。
- 关键方法：`onInitialize`、`onUpdate`、`onRender`、`destroyScene`、`loadModel`、`initSplatPipeline`、`initPresentPass`、`onShutdown`、`onWindowResize`。
- `FSplatViewerSession`：平台无关场景worker准备、相机、分块上传、compute、fullscreen present和诊断；Windows普通D路径和Android共用。桌面实验/导出/ImGui仍属Windows shell。
- `FSplatStreamingController`：共享D模式选择、后台读取、传输lease、页池、active set和真实submission关联。
- `LOD/SplatLODRuntimeOptions.cpp`：Windows/Android共享的严格运行配置解析与校验；Benchmark记录和图像导出仍留在桌面shell。
- `FProcessRuntimeLease`同时固定两套现有allocator生命周期；renderer/build owner与工作线程退休后才归还。

## 4. 3DGS调用链与数据

### 4.1 PLY与GPU源数据

- `FSplatPLYLoader`：`Include/Renderer/Splat/SplatPLYLoader.h`；实现同名cpp。
- `LoadCPU`是严格CPU解析；`loadAndUpload`进行预算准入及GPU上传；目前该调用包含同步工作，不适合阻塞Android UI或Surface销毁队列。
- 源码支持ASCII、binary little endian、binary big endian PLY，头文件注释只写little endian，存在文档滞后。
- 必需属性：x/y/z、f_dc_0..2、opacity、scale_0..2、rot_0..3。
- f_rest数量0/9/24/45对应SH0/1/2/3，必须连续；普通点云PLY不是自动支持的3DGS。
- 解析执行scale exp、opacity sigmoid、四元数归一化及SH重排；资源导入必须保留这些语义。
- `FSplatGPUData`：positions/scales/rotations/opacities/SH，含逻辑字节、暂存、上传和转换遥测。
- `SplatSHStorage.h`：FP32或打包binary16存储。存储FP16不等于执行FP16算术。

### 4.2 Compute管线

- `FSplatPipeline`：`Include/Renderer/Splat/SplatPipeline.h` / cpp。
- 入口：`SetCapacityBudgets` -> `initialize` -> `setGaussianData` -> `setCamera` / `SetActiveSet` -> `ReserveFrameId` -> `execute` -> `MarkFrameSubmitted`。
- `CollectCompletedFrameStats`异步取得完成样本；应按frame/countFrame关联，不把延迟回读错配到当前帧。
- 主链：Preprocess -> PrefixSum/Scan -> AssignKeys -> Radix Histogram/Offsets/Scatter -> TileBoundaries -> Render。
- `SplatPass.h/cpp`、`SplatSortPass.h/cpp`、`SplatRenderPass.h/cpp`分别封装相应阶段。
- `Shaders/Splat/splat_preprocess.comp`：协方差投影、SH颜色、Tile覆盖与可见性。
- `Shaders/Splat/Sort`：scan、indirect args、键分配、稳定排序、tile边界。
- `Shaders/Splat/Render/splat_render.comp`：16×16工作组，rgba8 storage image，前到后透明度合成；原路径和共享batch64可切换。
- 排序键实际包含uint64_t；“48位排序”仍不能据此当作不需要shaderInt64。
- scatter有subgroup扩展声明，但当前主体用32位shared flags/atomicOr/bitCount；扩展声明不等于实际subgroup指令需求。应核查最终SPIR-V capabilities。
- 各Pass常用256线程；手机兼容性需检查invocations、各轴大小和compiled shared-memory用量。
- RGB21F投影颜色压缩与SH FP16是独立开关；不要合并成单一“FP16优化”。
- `SplatSplitFusionPass`和fusion shader已存在，但不能自动计入Android首版验证范围。

### 4.3 Shader路径

- `SplatTypes.h::SplatShaderPath`：优先MONSTER_SPLAT_SHADER_DIR，再编译定义，再fallback路径。
- `ShaderCompiler::readFileBytes`支持注入IResourceProvider；Android资源provider从APK读取并对inventory大小/hash校验，必要时持久化私有cache。compileFromFile仍是host工具路径。
- `cmake/SplatLODStreaming.cmake`用host glslc/spirv-val离线编译并生成最终SPIR-V需求/身份清单，target-env=vulkan1.2；AndroidAssets生成双ABI独立资源树，Gradle merge依赖原生/资源生成。
- 不要把已有离线编译描述为全无Shader构建系统，也不要依赖Windows生成目录在Android存在。

## 5. LOD与Streaming

- `LOD/SplatSceneFormat`：版本化manifest/页资源读写与验证。
- `LOD/SplatLODBuilder`：离线层级/代理/分页；`Tools/SplatLODBuilder/main.cpp`为CLI。
- `LOD/SplatLODSelector`：按相机、误差、驻留和预算选择互斥覆盖集合。
- `Streaming/SplatPageStreamer`：后台读取、优先级、请求替换、generation取消、ready结果与CPU缓存预算。
- `DrainReady`转移结果后仍有预算lease；销毁payload后用`ReleaseTransferred`归还，不能提前撤销计费。
- `Streaming/SplatGPUPagePool`：固定source/staging slab、上传、驻留、active index和GPU完成后回收。
- 关键方法：`Initialize`、`TryQueueUpload`、`ProtectSelection`、`Tick`、`BuildActiveSet`、`MarkFrameSubmitted`、`DrainReleasedTransfers`、`Shutdown`。
- `Tick`应不等磁盘/不等待GPU；Shutdown是可等待慢路径。选择保护、真实submission token和generation必须贯穿生命周期。
- 手机首版小场景、LOD分页、手机有界构建与后台恢复应分别验收；这四项均已纳入首版需求。已有桌面Streaming不等于Android生命周期已处理。
- `FSplatPLYStream`提供严格有界PLY探针/批处理与取消；`SplatProxyMath`保留加权质量和累积RMS；`SplatBuildJournal`维护durable事务/发布恢复；`SplatStreamingLODBuilder`执行外存分区/归约。MobileLOD-1为manifest v3/page v1，独立于旧builder的融合版本。
- `SplatSceneValidation`校验真实manifest/pages/身份；Kotlin SceneRepository/ScenePackage采用私有staging、原子发布、文件lease与删除tombstone。
- `SplatBudgetCoordinator`是线程安全聚合账户；builder独占前需实际退休viewer资源。默认768MiB是临时开发准入策略，不是可用RAM测量。

## 6. Vulkan与RHI

- `IRHIDevice`、`IRHICommandList`、`IRHIBuffer/Texture`、`IRHISwapChain`位于Include/RHI。
- `RHIFactory`位于Source/RHI/RHI.cpp；Windows/Android选择Vulkan，Windows保留OpenGL shell，Android不链接桌面窗口入口。
- `VulkanAPI`维护函数表、动态加载、global/instance/device函数。
- Android loader明确使用libvulkan.so；独立能力probe保有局部dispatch/loader lifetime，不破坏正在运行的全局Vulkan表。
- `VulkanDevice.cpp`：instance、物理设备、逻辑设备、queues、Swapchain相关方法、timestamp和completion。
- `VulkanDevice_Swapchain.cpp`也包含交换链实现，后续改动先核对生产编译清单，避免改到未编译片段或重复定义。
- `VulkanUtils::createSurface`支持Win32/Xlib/Android；AttachExternalSurface/DetachExternalSurface配对native window引用，退休GPU/presentation后才ACK；timeout保留资源并重试。
- `beginFrame`/`presentFrame`/`cancelFrame`返回真实acquire/submit/present结果和completion token；录制不算提交。移动端强制单采样，上传帧无输出也须提交并建立WSI布局。
- Android owner在beginFrame之后显式begin/end命令录制；WSI预算按格式/样本/请求图像数+2预留并校验实际图像数量，opaque驱动分配仍非测量。`VK_SUBOPTIMAL_KHR`保留已提交帧与读回有效性，不单独触发静止Surface的swapchain重建；`VK_ERROR_OUT_OF_DATE_KHR`触发重建，`SURFACE_LOST`走detach/重新attach，`DEVICE_LOST`走受限恢复。upright渲染要求Surface支持IDENTITY变换，否则明确拒绝；不假称已做预旋转。
- Compute离屏行序为bottom-up，present负高度viewport与诊断PNG编码分别转换到显示方向；PNG元数据rowOrigin=top_left。导出报告nativeReportScope=library_load_only区分初始加载报告与viewer当前GPU阶段观察。
- instance要求Vulkan1.2；logical device仅请求已支持/实际需要的features。AndroidCapabilities区分支持/启用/执行，实际owner device需UUID等身份唯一匹配报告。
- timestamp代码已有validBits/period与延迟resolve；Android还需实际队列/完成验证。
- 内存层包含FVulkanMemoryManager、FVulkanResourceManager以及VulkanBuffer/Texture，必须沿真实创建路径核对actual backing。

## 7. Android实现导航与边界

| 范围 | 事实与后续要求 |
| --- | --- |
| 平台/数学 | Core/Platform.h统一Android优先分支；ARM使用受控FPU回退，非NEON性能实现 |
| 渲染owner | AndroidRenderHost：有界优先控制队列、单合并帧、generation/valid/windowInUse、detach退休ACK；Suspend不join解析 |
| 构建 | Gradle固定版本/双ABI，MONSTER_BUILD_ENGINE=ON；build_android.ps1输出构建身份，inspect_native_package.py检验真实ELF/ZIP，非运行证据 |
| JNI | AndroidJNI统一schema1有界解码、registry句柄、资源身份/私有路径、shared account与runtime lease；私有files根路径先canonicalize再做包含校验，允许canonical等价根别名并拒绝越界/符号链接；禁止UI慢路径 |
| 资源/UI | MainActivity/ViewerController/ViewerSurfaceView、SceneImportController/Repository/Package：SAF私有副本、短读安全分类与hash、PLY头校验、失败事务清理、受限原子发布；导入错误保留私有原因并用有界提示，替换场景打开失败保留现有lease |
| 后台任务 | LodBuildService/BuildJobStore + AndroidBuildJobHost：用户触发dataSync FGS、取消/暂停/断点恢复；系统中断后需明确恢复 |
| 预算/压力 | BudgetCoordinator + AndroidPerformancePolicy共用native聚合账户；安全退休后归还，WSI opaque backing保守估计不冒充测量 |
| 能力/诊断 | 独立部分报告＋真实GPU completion/readback观察；pixel smoke只证实窄条件，非图像阈值验收；用户触发私有导出 |
| 发布边界 | 无物理手机；实际16KiB运行、AC17、图像标准、授权/签名/商店审核仍待证据。CI不会发布 |

- `AndroidJNI::InspectPrivatePath/Confined` 区分缺失条目的 ENOENT/not_found 与权限、ENOTDIR 等检查失败；只有 `mayNotExist` 允许不存在的末级，且父级须为现存目录。文件根别名从首个 canonical 匹配点确定，根下符号链接（含悬空链接）、缺失祖先、遍历与越界仍拒绝；`BuildJobStore` 可保留尚不存在的 journal/output 名称。
- `NativeBridge.createBuild` 保持返回 `NativeHandle`；失败时立即在同一 worker 消费 JNI `nativeTakeBuildCreationError` 的有界阶段/原因，再经 `takeBuildCreationFailure()` 写入 `LodBuildService` 私有任务错误。原生和 Kotlin 失败缓存均按线程隔离并消费清除，诊断包含私有细节，不直接作为公开导出内容。检查点兼容、显式恢复与原子发布契约不变。

## 8. 相关计划与证据

- [3DGS总计划](devDocument/3DGS/3DGS渲染性能优化总计划.md)：P04与其他专题依赖。
- [Android方案](devDocument/3DGS/Android_Vulkan适配_PLAN.md)：本轮逐轮澄清生成，状态以文档顶部为准。
- [环境事实核查](devDocument/3DGS/Android_Environment_Audit.md)：Studio/SDK/MuMu/RenderDoc工具，非APK运行验收。
- [Filament参考核查](devDocument/3DGS/Android_Filament_Reference_Audit.md)：本地HEAD、Surface握手、JNI与Khronos来源。
- [正式验证历史](docs/validation/3dgs/README.md)：原始记录的范围与构建身份。
- [Controls测试说明](Source/Tests/3DGSControl/README.md)：历史命令及限制。

Android使用、兼容schema、工具链与授权边界见[android/README.md](android/README.md)、[发布清单](android/RELEASE_CHECKLIST.md)。当前源码已完成模块集成；本索引不证明完整Android/Windows构建或运行通过。新增验收必须附本轮实际退出码、源码清单、Shader/配置、APK/EXE及设备身份，勿将语法检查/编码交接当作全局完成。
