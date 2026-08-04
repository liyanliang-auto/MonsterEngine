# MonsterEngine 仓库结构梳理

> 生成时间：2026-07-09 ｜ 用途：梳理当前仓库文件结构、暴露问题、给出整理建议
> 当前分支：`main` ｜ Git 工作区：干净（无未提交改动）

---

## 1. 真实目录结构（按实际磁盘）

```
MonsterEngine/
├── CMakeLists.txt                 # 主构建（CMake，MSVC/Windows）
├── MonsterEngine.sln              # VS 原生构建（遗留，与 CMake 并存）
├── MonsterEngine.vcxproj
├── MonsterEngine.vcxproj.filters
├── main.cpp                       # 程序入口（419 行）
├── Vulkan_PBR_helmet.jpg          # 文档配图（应归入 docs/images）
│
├── Include/                       # 公共头文件（228 个 .h）
│   ├── Core/          (36)        # 日志/内存/断言/应用框架/HAL/IO/Logging/Templates
│   ├── Containers/    (18)        # TArray/TMap/TSet/FString/FName（header-only）
│   ├── Math/          (18)        # Vector/Matrix/Quaternion（header-only）
│   ├── RHI/           (16)        # Render Hardware Interface 抽象层 + 头
│   ├── Platform/      (36)        # Vulkan/ OpenGL/ GLFW 后端实现头
│   ├── Renderer/      (26)        # PBR 渲染器 + Scene.h/SceneTypes.h
│   ├── Engine/        (52)        # 场景/相机/材质/网格/贴图/光照/Actor/组件/Deferred/PostProcess
│   ├── Editor/        (4)         # ImGui 集成头
│   ├── RDG/           (6)         # Render Dependency Graph
│   ├── Serialization/
│   ├── Windows/
│   └── Tests/         (9)         # 测试相关头（与 Source/Tests 镜像）
│
├── Source/                       # 实现文件（180 个 .cpp，镜像 Include 结构）
│   ├── Core/          (21)
│   ├── Containers/    (1)
│   ├── Math/          (0)         # 全内联
│   ├── RHI/           (12)
│   ├── Platform/      (35)        # Vulkan/OpenGL/GLFW 实现
│   ├── Renderer/      (24)
│   ├── Engine/        (44)
│   ├── Editor/        (4)         # ImGui 集成实现
│   ├── RDG/           (2)
│   └── Tests/         (16)        # 单元测试/集成测试 cpp（被编进主可执行体）
│
├── Shaders/                      # Shader 源码（90 个：.vert/.frag/.spv/.glsl/.bat）
│   ├── Common/        (5)         # BRDF/光照/阴影工具
│   ├── Deferred/      (13)
│   ├── Forward/       (22)
│   ├── Lighting/      (4)
│   ├── Material/      (4)
│   ├── PBR/           (8)
│   ├── PostProcess/   (5)
│   └── *.bat/根文件    (29)        # compile_shaders.bat + 散落 shader
│
├── Tests/                       # ⚠️ 顶层测试目录（2 个 cpp，未被 CMake 引用）
│   └── Engine/
│       ├── Deferred/TAATests.cpp
│       └── PostProcess/FXAATests.cpp
│
├── resources/                   # 运行时资源（10 个）
│   ├── models/DamagedHelmet/    # gltf + bin
│   └── textures/
│
├── docs/                        # 对外文档（6 个：5 jpg + 1 md）
│   └── images/samples/
│
├── devDocument/                 # 开发笔记/设计文档（104 个 md，按模块/日期）
│   ├── Architecture/ Core/ Containers/ Math/ RHI/ Vulkan/ Vulkan-triangle/
│   ├── Examples/ SmartPointer/ plans/ test-results/
│
├── 3rd-party/                   # 第三方库（共 230 个文件）
│   ├── cgltf/         (1)        # glTF 加载器（单头）
│   ├── stb/           (1)        # STB 图像库（单头）
│   ├── imgui/         (0) ⚠️     # 空目录！CMake 期望其 .cpp，实际缺失
│   └── glfw-3.4.bin.WIN64/ (228) # 预编译二进制（不应入库）
│
├── .github/superpowers/         # AI 助手规则/技能（51 文件，非引擎源码）
└── .windsurf/skills/            # AI 助手规则/技能（52 文件，非引擎源码）
```

---

## 2. 模块职责速查

| 模块 | 头 / 实现 | 职责 |
|------|----------|------|
| **Core** | 36 / 21 | 日志(MR_LOG)、内存(FMemory/FMallocBinned2)、断言、应用框架、HAL、IO |
| **Containers** | 18 / 1 | UE 风格容器 TArray/TMap/TSet/FString/FName（header-only） |
| **Math** | 18 / 0 | 向量/矩阵/四元数（header-only，全内联） |
| **RHI** | 16 / 12 | 渲染硬件抽象层：跨 Vulkan/OpenGL 的统一接口 |
| **Platform** | 36 / 35 | Vulkan / OpenGL / GLFW 后端具体实现 |
| **Renderer** | 26 / 24 | PBR 渲染器（Cook-Torrance）、Scene 管理 |
| **Engine** | 52 / 44 | 场景图、相机、材质、网格、贴图、光照、Actor/组件、Deferred、PostProcess |
| **Editor** | 4 / 4 | ImGui 调试 GUI 集成 |
| **RDG** | 6 / 2 | Render Dependency Graph（渲染依赖图） |

**整体评价**：分层（RHI → Platform → Renderer → Engine）清晰，头/实现按模块镜像，符合 UE5 风格与 CMake `source_group` 组织，**骨架设计是健康的**。问题集中在"边界外的散落物"和"文档与现实的漂移"。

---

## 3. 暴露的结构问题（按严重程度）

### 🔴 P0 — 会导致构建/运行异常
1. **`3rd-party/imgui` 为空（0 文件）**
   - `CMakeLists.txt` 第 113 行 `file(GLOB IMGUI_SOURCES "3rd-party/imgui/*.cpp")` 取到空列表；
   - 但 `Source/Editor/ImGui/ImGuiContext.cpp` 等直接 `#include "imgui.h"`。
   - **后果**：若仓库未另外提供 imgui，CMake 构建会缺符号。需确认 imgui 是否应为 vendored 源码或 git submodule。
2. **顶层 `Tests/` 是"孤儿"，未被 CMake 编译**
   - `CMakeLists.txt` 只 glob `Source/Tests/*.cpp` 进 `ALL_SOURCES`；顶层 `Tests/Engine/Deferred/TAATests.cpp`、`Tests/Engine/PostProcess/FXAATests.cpp` 完全不在构建中。
   - **后果**：TAA / FXAA 测试永远不会被编译运行，与 `Source/Tests` 15 个测试割裂。
3. **测试代码三处分散**
   - `Tests/`(2) + `Source/Tests/`(16 cpp) + `Include/Tests/`(9 h)。同一套测试被拆到三个位置，且顶层那份还游离于构建之外。

### 🟡 P1 — 文档与真实结构漂移（误导接手者）
4. **README "目录结构"段不准确**
   - 写 `Include/Renderer/Scene/`（目录），实为 `Include/Renderer/Scene.h`（文件）；
   - Shaders 章节漏列 `Deferred/`、`PostProcess/`；
   - 声称 3rd-party 含 imgui，实际为空。
5. **README 引用的规则文件路径疑似拼错**
   - 第 1081 行：`.windsurf/rules/wind-monster-peoject-rule.md`（"peoject" 应为 "project"）。
   - 需核对 `.windsurf/rules/` 下真实文件名，否则贡献者找不到规范。

### 🟢 P2 — 仓库整洁度 / 可维护性
6. **双构建系统并存**：根目录同时有 `CMakeLists.txt`（主）与 `.sln/.vcxproj/.vcxproj.filters`（VS 原生，README 列为"方法3"）。两套需同步维护，建议明确 CMake 为主、标注 vcxproj 为遗留。
7. **`glfw-3.4.bin.WIN64`（228 文件）预编译二进制入库**：二进制一般不应纳入 git，建议改用 vcpkg/fetch-content 或加入 `.gitignore`。
8. **`.github/superpowers/`(51) + `.windsurf/skills/`(52) ≈ 103 文件 AI 工具目录**：非引擎源码，污染仓库主题，建议与引擎代码明确区分（或移出/子模块化）。
9. **`Vulkan_PBR_helmet.jpg` 散落在根目录**：属文档配图，应移入 `docs/images/`。
10. **`main.cpp`(419 行) 含 Application 样板**：可抽 `Source/Engine/Application` 进一步模块化（次要）。

---

## 4. 推荐的整理方案（待你确认后再执行）

> 以下为"物理整理"提案。**文档本身（第 1–3 节）不改动任何文件**，仅第 4 节需要你拍板是否落地。

**阶段 A — 修复构建正确性（P0）**
- A1. 补齐 `3rd-party/imgui` 源码（vendored 或 git submodule），确保 `#include "imgui.h"` 可解析。
- A2. 统一测试位置：将 `Tests/` 下 `TAATests.cpp`/`FXAATests.cpp` 并入 `Source/Tests/`（或整体迁移到 `Tests/` 并在 CMake 中改为 glob `Tests/`），删除重复/孤儿目录，保证全部测试进构建。

**阶段 B — 校正文档（P1）**
- B1. 更新 README 目录结构段落，与实际对齐（Scene.h、补充 Deferred/PostProcess、修正 imgui 描述）。
- B2. 修正 `.windsurf/rules/` 路径拼写，或更新 README 引用。

**阶段 C — 整洁度（P2）**
- C1. `git mv Vulkan_PBR_helmet.jpg docs/images/`，并修正 README 中图片引用相对路径。
- C2. 将 `glfw-3.4.bin.WIN64` 移出 git（`.gitignore` + 改用包管理器），或至少在 README 注明其为外部依赖。
- C3. 明确构建系统主次：在 README 标注 `.vcxproj` 为遗留/可选，CMake 为推荐。
- C4. 评估 `.github`/`.windsurf` 是否保留在仓库内（可考虑独立仓库或明确其用途说明）。

---

## 5. 文件规模总览

| 区域 | 文件数 | 说明 |
|------|-------|------|
| Include | 228 | 全部头文件 |
| Source | 180 | 全部实现（含 16 测试） |
| Shaders | 90 | vert/frag/spv/glsl/bat |
| devDocument | 104 | 设计/博客笔记 |
| 3rd-party | 230 | 含 228 个 glfw 预编译二进制 |
| docs | 6 | 对外文档与配图 |
| resources | 10 | 模型 + 纹理 |
| Tests(顶层) | 2 | 孤儿测试 |
| .github + .windsurf | 103 | AI 工具目录 |
| **合计** | **~953** | 引擎源码 ≈ 408（Include+Source），其余为资源/文档/工具 |
