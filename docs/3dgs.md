# 3D Gaussian Splatting

## 数据流程

外部训练的 PLY → CPU 构建稀疏八叉树与分页资源 → 屏幕误差 LOD 选择 → 异步读取与 GPU 页池 → 高斯投影、Tile 分配、稳定排序、透明度合成 → 窗口呈现。

源码入口：`Source/SplatSceneApplication.cpp`；渲染管线：`Source/Renderer/Splat/`；离线构建/选择：`LOD/`；GPU 页池/异步加载：`Streaming/`；Shader：`Shaders/Splat/`。`Tools/SplatLODBuilder` 是生产资源构建工具。

叶节点保留原始高斯属性，粗层使用代理高斯。选择器通过活动容量、场景内存预算及屏幕误差控制细化；父子替换保留覆盖，页回收等待真实 GPU 完成。稳定基数排序支持 48/64 位键，细节和排序溢出均有遥测。

## 准备模型

模型和生成页面不随仓库分发。将自行取得的 Bicycle PLY 放在 `resources/point_cloud/bicycle/point_cloud_30000.ply`；直接加载其他 PLY 可运行：

```powershell
.\build\bin\Release\MonsterEngine.exe --splat "path\to\model.ply"
```

Bicycle 的场景分页构建：

```powershell
.\build\bin\Release\SplatLODBuilder.exe --input resources/point_cloud/bicycle/point_cloud_30000.ply --output resources/3dgs_scenes/bicycle --config Tools/SplatLODBuilder/default-config.json
```

构建输出包括 `scene.mgs`、独立校验的 `pages/*.mgsp` 和 `build-report.json`。输出目录必须尚不存在。相机序列针对展示中使用的 Bicycle 坐标系，其他模型需自行设置相机。重新构建的资源可能与归档展示资源存在粗层差异。

## 运行配置

| 配置文件 | 模式 | 活动容量 | 场景预算 | 屏幕误差 |
| --- | --- | ---: | ---: | ---: |
| `lod_streaming_defaults.json` | D：LOD + 流式加载 | 150 万 | 1 GiB | 2 px |
| `bicycle_lod_high.json` | D：高画质 LOD | 613 万 | 2 GiB | 1 px |
| `bicycle_full_quality.json` | A：原始全量 | 613 万 | 2 GiB | 不参与 LOD 选择 |

```powershell
.\build\bin\Release\MonsterEngine.exe --splat-config resources/3dgs_sequences/bicycle_lod_high.json
.\build\bin\Release\MonsterEngine.exe --splat-config resources/3dgs_sequences/bicycle_full_quality.json
```

高画质配置另提供 6400 万排序容量、4 GiB 临时 GPU 预算。预算是允许的上限，场景预算不包含所有排序、渲染和 UI 资源。UI 用 GiB 显示预算，MiB 显示每帧上传，百万 M 显示容量；JSON 保留字节字段。配置支持 `validation`、`vsync`、SH 阶数、相机序列、CPU 缓存和读取并发等参数。

普通 A 模式同样使用配置容量；显式预算不足会失败，不会静默退回自动容量。无参数启动使用日常配置，需要先准备对应模型及分页数据。

## 相机路径与视频

打开 Sequence 面板，进入序列并点击播放，可重现仓库 Bicycle 视频的 60 秒路径。F1 关闭 UI；面板布局分区且不会互相覆盖。

逐帧视频导出需要 FFmpeg：

```powershell
.\build\bin\Release\MonsterEngine.exe --splat-config resources/3dgs_sequences/bicycle_full_quality.json --splat-sequence resources/3dgs_sequences/bicycle_lod_mid_far.json --splat-export output/bicycle.mp4 --splat-export-fps 24 --splat-ffmpeg "path\to\ffmpeg.exe"
```

现有原生导出从呈现前的线性纹理读取，窗口则经过 sRGB 呈现。README 的高清导出素材另生成了 sRGB 展示副本；直接导出的线性视频颜色可能比窗口暗。窗口录屏直接捕获实际呈现颜色。

## 验证与边界

正式 C++ 测试覆盖分页校验、预算、选择覆盖、并发读取生命周期、GPU 排序与完成同步、场景切换、可见 UI 和键盘输入。可选构建：

```powershell
cmake -S . -B build -DSPLAT_BUILD_CPU_TESTS=ON -DSPLAT_BUILD_GPU_TESTS=ON -DSPLAT_BUILD_INTEGRATION_TESTS=ON
cmake --build build --config Release --target SplatLODTestsCPU SplatLODTestsGPU SplatLODTestsIntegration
ctest --test-dir build -C Release --output-on-failure
```

高预算 D 的四个固定视角与全量图接近，不保证任意视角等同全量；低预算配置会损失细节。真实窗口帧率与逐帧导出编码帧率不同。历史验证的构建归属与局限见 [关键测试记录](validation/3dgs/README.md)，不将旧构建成绩视为所有硬件或所有路径的性能保证。
