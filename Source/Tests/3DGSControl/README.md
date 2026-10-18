# 3DGS ImGui 控制系统验证

## 已执行

| 检查 | 命令或证据 | 结果 |
| --- | --- | --- |
| Debug 编译 | `MSBuild MonsterEngine.sln /t:Build /p:Configuration=Debug /p:Platform=x64` | 通过 |
| Release 编译 | `MSBuild MonsterEngine.sln /t:Build /p:Configuration=Release /p:Platform=x64` | 通过 |
| 相机和序列 | `x64/Debug/MonsterEngine.exe --test-splat-controls` | 通过：位置、旋转、FOV 30–120、鼠标灵敏度、移动速度、近远裁剪、关键帧插值、播放/暂停/停止/循环、编辑、JSON 往返与错误文件 |
| 中文字形 | 同上 | 通过：检查 `SplatSceneApplication.cpp` 中所有基本汉字均在字体中 |
| GPU 参数效果 | 使用 Codex 工作区 Python 运行 `Source/Tests/3DGSControl/test_gpu_control_effects.py` | 通过：6 帧依次检查 FOV、近远裁剪、距离裁剪开关、线框、高斯缩放；MP4 有效且无 Vulkan validation error |
| 3DGS 画面吞吐 | `MONSTER_SPLAT_BENCH_SECONDS=5` 运行 3DGS 程序 | 当前 1920×1080 窗口/渲染：预热 5 秒后平均 48.382 次呈现/秒，见 `validation/benchmark_1920x1080.log`；旧 3840×2019 窗口/1600×840 渲染为 55.985，见 `validation/benchmark_5s.log`。均包含 UI 和呈现，不代表独立 GPU pass 耗时。 |
| 完整桌面截图 | `validation/capture_desktop.py` | 通过：`validation/ui_1920x1080.png` 显示当前默认窗口、中文标签及画面；`validation/final_ui_visible.png` 是旧最大化窗口截图 |
| UI 显示开关 | 使用 Codex 工作区 Python 运行 `Source/Tests/3DGSControl/test_ui_visibility.py` | 通过：`--splat-no-ui` 隐藏启动，F1 显示/再隐藏；当前 1920×1080 截图像素差分 45.0 / 0.0 |

GPU 对比测试的图片和 MP4 位于 `validation/`，最近一次在 1920×1080 窗口、1920×1080 内部渲染分辨率下执行。图块尺寸是只读的固定 16×16。

## 尚未自动化的验收

测试驱动的是控件对应的参数状态和渲染路径，没有逐个模拟鼠标点击 ImGui 控件；触控板手势、窗口拖动/缩放后的画面、取消导出和 FFmpeg 缺失时的 UI 提示还需交互验收。最终 UI 字形已由字体字形测试和完整桌面截图验证。项目旧有 `--test-all` 基线测试存在失败/挂起，本次未将其计为通过。
