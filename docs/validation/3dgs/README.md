# 3DGS 关键验证记录

这些日志是已完成验证的测试名称与结果摘录，保留 PASS/FAIL/SUMMARY 行；完整本地采集目录、模型、机器日志与脚本不随仓库发布。记录不等同于提交后自动重跑的性能基准。

## 发布前独立验证（2026-10-13）

从暂存区导出独立源码，排除本地模型、分页数据及采集目录，构建引擎、构建器和全部正式测试。发现并修复 GPU 读回测试依赖旧临时 SPV 的问题：测试 Shader 源码现由 CMake 自动编译。

- [publish-cpu.log](publish-cpu.log)：47/47。
- [publish-gpu.log](publish-gpu.log)：33/33。
- [publish-integration.log](publish-integration.log)：14/14。
- [publish-check.log](publish-check.log)：构建、CTest 3/3、旧测试入口退出状态，以及独立构建源码/EXE 哈希。

上述共 94 项正式检查通过，另运行 `--test-all` 与 `--test-splat-controls`，退出均为 0。此验证针对源码完整性和功能，不是 Bicycle 性能基准。

## 历史修复记录

| 日志 | 结果 | 范围 |
| --- | --- | --- |
| [cpu.log](cpu.log) | 47/47 | 离线构建、页面校验、预算选择、读取/缓存生命周期 |
| [gpu.log](gpu.log) | 33/33 | 真实 Vulkan、48/64 位稳定排序、百万条输入、页池/完成同步 |
| [integration.log](integration.log) | 14/14 | 配置、原始/叶节点图像一致性、UI/输入、场景切换、显式预算失败 |
| [cli.log](cli.log) | 9/9 | 构建器与 viewer 参数错误、预算和模式检查 |

CPU/GPU/CLI 日志来自 2026-10-11 的修复验证；其 viewer EXE SHA256 为 `cb5d5e66243a64c0eeac88949d3fe02ec379c464296f631d8daba0f469432674`。集成 14 项来自后续普通 A 容量/UI 修复构建；对应 viewer EXE SHA256 为 `e88ef4fc731583048f084f2a1c3597344dbcf66643b045dd8a5856ca1df41004`。它们不是同一构建、同一运行的合并成绩。发布整理又将默认资源路径改为仓库相对路径，独立检查构建和测试结果见 `publish-check.log`。

README 的 1920×1080 全量作品图和视频使用外部训练的 Bicycle 模型（6,131,954 高斯、SH3）。真实窗口录屏保留标题栏的实际 FPS；导出视频为 24 编码 FPS。高预算 D 四个静态视角与全量的线性 RGB PSNR 约 76.60/54.75/59.77/80.74 dB，仅描述固定视角差异。该 D 采集出现请求预算拒绝累计计数 206，不将其描述为压力验证零异常。任意近场、Debug 流畅度、长期稳定性均未据此验收。
