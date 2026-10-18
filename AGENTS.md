# MonsterEngine：AI开发入口

## 工作区与事实

- 先读根目录CODE_WIKI.md，再按需求读取相应模块源码；源码与匹配构建证据优先于旧开发文档。
- 主源码为本仓库；Android方案位于devDocument/3DGS/Android_Vulkan适配_PLAN.md。
- 当前主验证平台是Windows/Vulkan。Android客户端/原生target已落地；源码、语法检查或工具探针不能算完成APK/渲染验收。
- 保留用户未提交修改及未跟踪文件；开始修改前检查git status。
- 本文命令是入口说明，不代表本次会话已经执行通过。

## Windows构建

在仓库根目录运行，先解析本机cmake/MSBuild/glslc真实路径；不要假设PATH已配置。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target MonsterEngine SplatLODBuilder
```

CMake最低3.20，C++20，MSVC/Windows SDK；3DGS Shader需要glslc（可设置SPLAT_GLSLC）。
当前Windows viewer链接bundled Win64 GLFW和Vulkan导入库；CMake生产清单为cmake/MonsterSources.cmake，共享核心和Windows shell分开。
VS工程也可用MSBuild编译；如与CMake不一致，应定位清单差异，不能仅凭EXE存在宣布本次构建成功。
程序从根目录运行以找到资源，或显式指定配置/Shader目录。

## 正式3DGS测试

```powershell
cmake -S . -B build-verify -G "Visual Studio 17 2022" -A x64 -DSPLAT_BUILD_CPU_TESTS=ON -DSPLAT_BUILD_GPU_TESTS=ON -DSPLAT_BUILD_INTEGRATION_TESTS=ON
cmake --build build-verify --config Release --target SplatLODTestsCPU SplatLODTestsGPU SplatLODTestsIntegration
ctest --test-dir build-verify -C Release --output-on-failure
```

- CPU-only资源构建可用MONSTER_BUILD_ENGINE=OFF，详见cmake/SplatLODStreaming.cmake。
- GPU/integration target需要真实Vulkan viewer运行时及编译生成Shader；不能用mock替代真实GPU验收。
- 桌面应用另有`--test-all`、`--test-splat-controls`入口；先读main.cpp核实当前参数。
- 测试数量和历史基线可能随专题变化；报告本次实际命令/退出码/断言，不复制旧数字。
- 按改动影响范围选择测试。发布/跨平台重构应覆盖共享核心、原Windows入口与相应Android能力。

## 手动运行与性能

```powershell
./build/bin/Release/MonsterEngine.exe --splat "path/to/model.ply"
./build/bin/Release/MonsterEngine.exe --splat-config resources/3dgs_sequences/bicycle_realtime_1080p.json
```

- 模型、分页数据、FFmpeg等外部依赖不保证随仓库存在。
- 性能实验遵循对应devDocument/3DGS专题：同构建/同场景/同相机，只改单变量；记录源版本、Shader/配置、设备和采样窗口。
- CPU帧间隔、GPU区间、present频率、视频编码FPS分别报告。
- 长测或压力验收按明确场景和预算执行；未执行项标为未验证，不编造统一压力命令。

## Android实施阶段

- 先读android/README.md。PowerShell 7与JDK21；Gradle8.13、AGP8.13.2、Kotlin2.3.10、SDK36、BuildTools36.1.0、NDK30.0.16248370、CMake3.22.1已固定。解析JAVA_HOME、ANDROID_HOME、SPLAT_GLSLC、SPLAT_SPIRV_VAL、Python3_EXECUTABLE真实路径。
- `./Tools/Android/build_android.ps1 -Variant Debug`构建双ABI APK；`-Variant Profile`提供可分析版本；`-Variant Release -Bundle`构建AAB。外部环境签名变量及`-RequireSigning`见android/RELEASE_CHECKLIST.md；不自动发布。
- Gradle原生参数必须为MONSTER_BUILD_ENGINE=ON，target为monster_android；OFF仅供独立CPU工具使用。不能通过关闭引擎来掩盖原生失败。
- `python Tools/Android/inspect_native_package.py <实际APK或AAB> --output <报告.json>`检查真实ELF/PT_LOAD及APK本地ZIP偏移；AAB还需检查生成的安装APK。
- 构建脚本记录source/tool/package/native/asset身份与实际退出结果；完整链接/打包证据优先于语法、Kotlin单编或旧APK。全局验证日志位于本轮specs目录，勿将这些命令本身当作已执行证据。
- 模拟器功能、ARM原生构建、真机性能、16KiB实际页运行分别记录。目前用户仅有模拟器，物理手机、实际16KiB运行、图像阈值和AC17长测仍待验证；不可用模拟器代签真机结果。
- 默认768MiB聚合上限是临时开发准入策略，不是测得可用RAM或支持机型阈值。Java/native/GPU共用账户，WSI保守估计与实际分配信息分别报告。
- 本机路径、设备序列号和私有模型路径放在本地配置/证据，避免硬编码进产品构建。
- 共享库不应依赖GLFW/Win32/OpenGL/ImGui客户端入口。
- 升级/场景/检查点兼容、第三方完整授权文本和外部发布待办见android文档；未知schema不自动迁移，保留原始输入与用户状态。

## 文档与流水线

- CODE_WIKI.md是紧凑源码索引；修改架构、构建或关键API后同步更新相关章节。
- 需求澄清用grill-me/grilling，每轮问题数遵循用户限制。
- spec-forge实施时读取其Codex runtime adapter，使用调度器与CLI更新状态；规划不自动开启开发流水线。
