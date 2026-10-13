# MonsterEngine

![Windows](https://img.shields.io/badge/platform-Windows-blue)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)
![Vulkan](https://img.shields.io/badge/API-Vulkan-red)
![OpenGL](https://img.shields.io/badge/API-OpenGL-orange)

MonsterEngine 是一个使用 C++20 开发的渲染引擎，包含 Vulkan / OpenGL 后端、PBR 渲染，以及基于 Vulkan Compute Shader 的 3D Gaussian Splatting（3DGS）管线。架构参考 Unreal Engine 的 RHI、资源管理和容器设计，主要验证环境为 Windows + Vulkan。

## 作品展示

### 3D Gaussian Splatting：Bicycle

外部训练的 Bicycle 模型包含 **6,131,954 个高斯、SH3 球谐系数**。下图来自 MonsterEngine 的实际运行窗口，关闭控制面板，内部渲染分辨率为 **1920×1080**。

![MonsterEngine Bicycle](docs/images/samples/bicycle_monsterengine_window.png)

[![Bicycle 相机路径预览](docs/images/samples/bicycle_monsterengine_path_window_preview.gif)](docs/images/samples/bicycle_monsterengine_path_window.mp4)

[完整窗口录屏](docs/images/samples/bicycle_monsterengine_path_window.mp4)沿 60 秒相机路径经过正面、俯视和侧面，再回到起点。开头短暂展示控制面板，随后隐藏 UI，全程保留引擎标题栏。另提供[1080p 高清逐帧导出视频](docs/images/samples/bicycle_3dgs_full_1080p.mp4)。两者的 24 FPS 为编码帧率；真实录屏的渲染速度以标题栏为准，逐帧导出用于展示画质。

模型数据来自外部训练成果；本项目实现 PLY 加载、GPU 渲染与排序、LOD 构建与选择、异步分页加载、交互控制和视频导出。

### PBR 渲染

Cook–Torrance BRDF、Metallic–Roughness 工作流、IBL、法线贴图和环境光遮蔽。

![PBR Helmet](docs/images/samples/Vulkan_PBR_helmet.jpg)

### 3DGS：Bonsai

![3DGS Bonsai](docs/images/samples/vulkan_3DGS_bonsai_30k.png)

## 核心功能

- **渲染抽象层**：统一 RHI 接口，Vulkan / OpenGL 资源与命令管理。
- **3DGS 管线**：高斯投影、扫描、Tile 分配、稳定基数排序和透明度合成；支持 SH3。
- **LOD 与流式加载**：离线稀疏八叉树与分页资源构建、屏幕误差选择、父子覆盖替换、异步读取、GPU 页池及显存预算。
- **运行控制**：WASD / 鼠标漫游、相机关键帧序列、GiB 预算编辑、GPU Pass 计时和 MP4 导出。
- **引擎基础**：自定义内存分配器、容器、智能指针、异步 I/O 和日志；纹理流式加载与虚拟纹理管理。

全量展示与低预算 LOD 使用不同配置。LOD 代理会影响细节，场景预算也不等于总显存占用。实现、运行方式及验证边界见 [3DGS 文档](docs/3dgs.md)。

## 构建与运行

Windows 需要 MSVC、CMake 3.20+、Windows SDK，以及提供 `glslc` 的 Vulkan SDK。第三方头文件及部分预编译库随仓库提供。

```powershell
git clone git@github.com:liyanliang-auto/MonsterEngine.git
cd MonsterEngine
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target MonsterEngine SplatLODBuilder

# 从项目根目录运行，模型文件需要自行准备
.\build\bin\Release\MonsterEngine.exe --splat "path\to\model.ply"
```

如果使用仓库自带的 Visual Studio 工程，请按本机工具集版本调整项目属性。推荐 Release 进行画质与性能展示。

| 操作 | 按键 |
| --- | --- |
| 移动 / 升降 | WASD / 空格、C |
| 环顾 / 调整视野 | 右键拖动 / 滚轮 |
| 显示或隐藏面板 / 退出 | F1 / Esc |

Bicycle 默认配置和相机序列位于 `resources/3dgs_sequences/`。模型与生成的分页资源不随仓库分发；[3DGS 文档](docs/3dgs.md)说明构建与配置步骤。视频导出另外需要可执行的 FFmpeg。

## 文档与验证

- [完整构建指南](BUILD.md)
- [3DGS 架构、运行与预算](docs/3dgs.md)
- [关键验证记录](docs/validation/3dgs/README.md)
- [引擎架构](devDocument/引擎的架构和设计.md)
- [Shader 文档](Shaders/README.md)

源码包含正式 C++ CPU、GPU 和集成测试。仓库保留关键测试结果，模型、采集脚本、临时截图及本地会话文件不随源码提交。

## 依赖与致谢

使用 Vulkan、GLFW、Dear ImGui、STB 和 cgltf；第三方依赖遵循各自许可证。架构与渲染学习参考 [Unreal Engine](https://github.com/EpicGames/UnrealEngine)、[Google Filament](https://github.com/google/filament)、Vulkan Tutorial 和 LearnOpenGL。
