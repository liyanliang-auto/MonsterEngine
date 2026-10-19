# Codex 3DGS 测试与临时文件整理

## 帧率测试（2026-10-02）

- 场景：`resources/point_cloud/bonsai_30k.ply`，约 124 万个 Gaussian；1920×1080，默认静止视角（FOV 60°）。
- 设备：NVIDIA GeForce RTX 3060；Windows，Vulkan，`x64/Debug/MonsterEngine.exe`。
- 方法：关闭 `MONSTER_SPLAT_DIAG` 和逐帧 `stderr` trace；基准模式关闭 Vulkan validation。首个已提交帧后预热 5 秒，用 `std::chrono::steady_clock` 统计接下来约 30 秒的完成帧数；平均 FPS = 帧间隔数 / 实际采样秒数。
- 复测命令（PowerShell，在项目根目录运行）：`$env:MONSTER_SPLAT_BENCH_SECONDS='30'; Remove-Item Env:MONSTER_SPLAT_DIAG -ErrorAction SilentlyContinue; .\x64\Debug\MonsterEngine.exe --splat`。程序打印 `[SPLAT_BENCH]` 后自动退出。

| 轮次 | 完成帧数 | 时长（秒） | 平均 FPS |
|---|---:|---:|---:|
| 1 | 397 | 30.023 | 13.190 |
| 2 | 636 | 30.037 | 21.140 |
| 3 | 650 | 30.052 | 21.596 |

三轮按总帧间隔 / 总时间合并为 **18.64 FPS**。第一轮显著偏低，原因未确认；后两轮合并为 **21.37 FPS**。后两轮的 5 秒分段帧率从开头约 27–29 FPS 降至末段约 19 FPS，提示持续运行时存在波动，不能用开头瞬时数值代表长时间表现。

测试期间移除了 `Source/Platform/Vulkan/VulkanRHICommandList.cpp::draw` 中每帧三行的 `stderr` trace；基准模式下只保留必要错误输出。`Source/SplatSceneApplication.cpp` 增加了可选基准模式，并在关闭时等待 GPU 空闲，避免最后一帧尚在使用资源时销毁对象。最终两轮退出码为 0，`stderr` 中未发现 `ERROR`、`VUID` 或逐帧 trace。

## 临时文件归档

3DGS 渲染问题的根因、修复与关键日志数值已记录在 `Vulkan原生3DGS_splat_pass开发文档.md` 第 16 章。以下临时文件已从项目根目录或 `log` 顶层移入归档，原始数据仍可恢复查看：

- `devDocument/3DGS/diagnostic_artifacts/legacy_probes/`：原项目根目录的 `_cl_all.txt`、`_cl_err.txt`、`_cl_help.txt`、`_cl_out.txt`、`_logcat_check.cpp`、`crash.txt`。前五个是编译器/日志类别探针；`crash.txt` 是旧的标准错误重定向输出，其结论已记入 `调查zoom时画面会糊_新会话上下文.md`。
- `log/archive/codex_3dgs_2026-09-30_to_10-02/`：10 个 `splat_bench*.txt` 原始输出、45 个 3DGS 调查期间生成的 `MonsterEngine-backup-*.log`，以及测试后留下的 `MonsterEngine.log`。合计 56 个文件，约 2.205 GiB；关键帧率数据见上表。

此次只整理了文件位置，**未释放磁盘空间**。2026 年 8 月的旧日志、项目代码、PLY 模型和截图未纳入本次归档。
