# 3DGS 渲染性能优化：SH 降阶与实际存储裁减（P03）

文档状态：实现与审计记录。TS：`20261017-103519-2dex`。本报告依赖 [总计划](./3DGS渲染性能优化总计划.md) 和 [P02 GPU 常驻数据压缩报告](./3DGS渲染性能优化_GPU常驻数据压缩.md)。运行原始证据保留在本地验证目录，不随仓库提交。

## 1. 需求与边界

- 提供 SH0、SH1、SH2、SH3 的可选存储阶数；`-1` 表示沿用源阶数，默认路径保持 SH3/FP32。
- 阶数选择必须同时影响加载、GPU 上传、RHI buffer 大小、shader stride 和元数据；不能只改变显示求值。
- 删除未使用的 SH 尾部，实际减少 GPU allocation/backing 和上传字节；保留完整阶数回退。
- 保持高斯顺序、alpha/transmittance、混合顺序、非法记录防护、空/部分/full dispatch、wireframe、state/force-clear 和输出格式语义。
- culling 支持域和阈值边界必须有明确不等式及 below/equal/above 复核。
- P02 投影颜色压缩只能作为单变量后续实验；投影 center/conic 量化明确延期。

本次范围只覆盖全量 A 路径的 SH 降阶/物理裁减接口和比较协议。LOD/Streaming 的页级压缩、Android、长期运行和跨设备结论不在本报告内。

## 2. 设计

### 2.1 阶数和布局

每个高斯的 SH 标量数为：

`C(d) = 3 * (d + 1)^2`，其中 `d ∈ {0,1,2,3}`。

当前非 SH 属性为 position/scale/rotation 各 16 B、opacity 4 B，共 `B_attr = 52 B`。因此：

- FP32 SH：`B_sh(d) = 4 * C(d)`，`B_gaussian(d) = 52 + B_sh(d)`；
- packed FP16 SH：`B_sh16(d) = 4 * ceil(C(d) / 2)`，shader 读取后仍以 FP32 求值；
- 总逻辑字节：`N * B_gaussian(d)`；实际 RHI reserved/backing 以分配器返回值为准，尾部按 16 B 对齐并单独记录。

`FSplatPLYLoader::loadAndUpload` 先以有效阶数做 admission，再以同一阶数建立 SH buffer、staging stride 和上传循环。`sourceSHDegree` 保存 PLY 源阶数，`shDegree` 保存 resident 阶数；请求高于源阶数时钳制到源阶数，`-1` 保持原路径。SH shader 继续按 `shStride=(d+1)^2` 访问，RGB 因子在 `C(d)` 中保留。

### 2.2 SH0/1/2/3 容量公式（Bicycle）

证据中的 Bicycle 高斯数 `N=6,131,954`，未计 allocator 额外保留时的 FP32 理论值如下：

| 阶数 | RGB 系数/高斯 | SH B/高斯 | 属性总 B/高斯 | SH 总 B | 属性总 B |
|---:|---:|---:|---:|---:|---:|
| SH0 | 3 | 12 | 64 | 73,583,448 | 392,445,056 |
| SH1 | 12 | 48 | 100 | 294,333,792 | 613,195,400 |
| SH2 | 27 | 108 | 160 | 662,251,032 | 981,112,640 |
| SH3 | 48 | 192 | 244 | 1,177,335,168 | 1,496,196,776 |

SH3 到 SH0/1/2 的理论属性减少分别为 73.80%、59.02%、34.43%；这些是布局推导，不是 FPS 收益。P02 已有 SH3 FP16 实际证据为 source backing `1,496,196,784 -> 907,529,200 B`（-39.34%），SH backing `1,177,335,168 -> 588,667,584 B`；该结果仍属于 P02，不归因于本次 P03。

### 2.3 回退与兼容

`shStorageDegree=-1` 或旧配置缺少该键时走源 SH 阶数。`shStorageMode=1` 仍仅支持 full-resident A；LOD/Streaming、fullLeaf、projected fusion 组合继续拒绝。空场景释放资源但保留有效阶数元数据；非法 degree（小于 -1 或大于 3）在配置校验处拒绝。旧 scene manifest 通过 `sourceSHDegree` 校验，避免把裁减阶数误当源数据阶数。

## 3. 变更文件

J0 实现：

- `Include/Renderer/Splat/SplatPLYLoader.h`：新增源阶数元数据和请求阶数参数。
- `Source/Renderer/Splat/SplatPLYLoader.cpp`：按有效阶数进行 admission、stride、buffer 和上传裁减。
- `Include/Renderer/Splat/LOD/SplatLODBenchmark.h`：新增 `shStorageDegree` 与 `projectionColorCompression` 开关。
- `Source/Renderer/Splat/LOD/SplatLODBenchmark.cpp`：解析并校验 `-1..3` 阶数及投影颜色开关。
- `Source/SplatSceneApplication.cpp`：传递阶数，记录 source/effective degree、配置 hash 和投影颜色状态。

J1 工具：

- `devDocument/3DGS/P03_SHReduction/comparison-config.json`：SH0/1/2/3、存储模式及 P02 单变量协议。
- `devDocument/3DGS/P03_SHReduction/compare.ps1`：根据 metadata 计算 FPS、frame time 和实际 backing 差异。

现有 `Shaders/Splat/splat_preprocess.comp` 已使用 push constant 的 `shDegree/shStride` 并按阶数分支求值；本次没有复制一套 shader ABI。center/conic 仍使用原 FP32 buffer，`projectionColorCompression` 不改变这两个属性。

## 4. culling 与阈值边界复核

`Shaders/Splat/splat_tight_culling.glsl` 的支持域保持 fail-open 语义：

| 条件 | 保留/拒绝不等式 | below/equal/above 复核结论 |
|---|---|---|
| center 有限域 | `abs(center) <= 1,048,576` 且 finite | `1,048,575.99` 保留；`1,048,576` 保留；`1,048,576.01` 拒绝 tight support，回退旧矩形 |
| conic 对角 | `1e-12 <= a,c <= 1e12` | 低于/高于边界返回 unsafe；等于边界继续 determinant 检查 |
| 条件数 | `det > 1e-4 * a*c` 且 `min(a,c) >= 1e-4*max(a,c)` | 小于或等于边界 unsafe；刚高于边界可进入 tight support |
| opacity | `opacity > 0` | `0` 和负值 unsafe；最小正值继续计算 |
| 深度 | `pView.z < -0.0001` | `-0.00010001` 通过；`-0.0001` 与更大值拒绝 |
| tile 支持 | `best <= limit + margin` | 等号保留；仅严格大于 margin 后拒绝；非 finite fail-open 保留 |
| alpha 贡献阈值 | `rawLimit=2*log(max(opacity,1/255)*255)` | opacity 小于 `1/255` 使用 `1/255` 下限，避免零/负 log；等于和高于按实际值计算 |

矩形交集使用旧 tile-rounded bounds 作上界，不以 3-sigma tight rectangle 擅自扩大支持域；partial tile 使用连续 box minimum，包含最后不完整 tile。SH 降阶只改变颜色，不改变 center、covariance、conic、opacity 或上述阈值，因此没有新的 culling 域。J0/J1 的 focused checks 覆盖空/单条/部分/完整记录、最大 SH3、裁减 stride、对齐和配置边界；GPU 侧完整 P03 boundary 运行仍需由验证阶段补齐。

## 5. 验证与当前证据

可复核的构建/测试证据：

| 检查 | 结果 | 证据 |
|---|---|---|
| 基线 Release 构建 | PASS | 本地验证日志（不随仓库提交） |
| 基线 CTest CPU/GPU/Integration | PASS，3/3 | 本地验证日志（不随仓库提交） |
| `MonsterEngine.exe --test-all` | PASS | 本地验证日志（不随仓库提交） |
| `MonsterEngine.exe --test-splat-controls` | PASS | 本地验证日志（不随仓库提交） |
| J0 变更后 Release 构建 | PASS | 本地 spec-forge handoff（不随仓库提交） |
| J1 比较工具 | PASS | P03 comparison 工具与本报告中的可复现协议 |

这些结果证明代码和工具可构建；它们不等同于 SH0/1/2/3 的完整 Bicycle GPU 实测。上述要求已在续作实测中完成，具体数值见第 9 节；SH0/SH1、多视角和按 LOD 分层仍待补充。

## 6. Bicycle 比较协议与 P02 后续

协议入口：`devDocument/3DGS/P03_SHReduction/comparison-config.json`。每次只切换一个变量：

1. P03：固定 FP32，比较 SH3 基线与 SH2、SH1、SH0；再在选定阶数上单独比较 FP16 storage。不可把降阶和 FP16 的容量/FPS 变化合并归因。
2. P02 projection-color follow-up：以 P03 SH2 配置为基线，只打开 `projectionColorCompression=true`；第 9 节已记录真实 RGB21F 结果。中心和 conic 保持 FP32、未量化，明确延期。
3. 每个配对需保存 metadata、frames.csv、capacity 字段和 matching image。图像门槛沿用 P02：线性 RGBA8，报告 RGB/ RGBA MAE、PSNR、最大绝对误差、changed pixels，并先调查差异再作视觉等价结论。

P02 已完成的一组 SH3 FP32/packed-FP16 配对为 37.6577 -> 37.2224 FPS（-1.156%），WholeFrame 25.6633 -> 26.1960 ms（+2.076%）；它只说明 FP16 存储在该短样本没有整帧收益，不能替代 P03 SH 降阶曲线。

## 7. 截图、限制与待办

本报告没有把逻辑容量推导当作运行时性能结论。当前仓库中虽有 P02 的 RGBA8 readback 和窗口采集元数据，但本续作未能形成新的 native window titlebar live-FPS 截图，原因是当前会话 Computer Use sky RPC 未配置；第 9.2 节已明确记录。因此明确记录：**若验证环境无法提供原生标题栏截图或标题栏 FPS，native titlebar screenshots/FPS 状态保持 pending，不补写或推测数值。**

仍待完成：

- SH0/1/2/3 在真实 GPU 上的 capacity/backing、FPS/frame time、图像误差和截图配对；
- projection-color 单变量结果；
- center/conic quantization（P06 方向）及其近裁剪、退化 conic、阈值边界专项；
- 多视角、长时间、Android/其他 GPU 的稳定性与收益。

## 8. 结论

P03 的代码契约已落地：有效 SH 阶数贯穿加载、分配、上传、stride、shader 求值和元数据，实际 SH 尾部可裁减，旧 full-degree 路径可回退，配置和 culling/threshold 边界已明确。当前可交付的是实现、容量公式和 Bicycle SH2/RGB21F 实测闭环；SH2 画质门限失败，故不作为默认配置。投影颜色压缩门限通过并保留 FP32 回退；center/conic 量化继续延期。

## 9. 续作 TS=20261017-151238-2o2d：真实 Bicycle GPU 实测

本节为当前续作的真实运行结果，使用 Release `MonsterEngine.exe`、RTX 3060、1920×1080、VSync off、tight tile culling=2、radix32、raster shared memory=1、固定 Bicycle 相机和同一场景 manifest。运行原始证据位于 本地验证工件（不随仓库提交），分析结果位于 本地验证工件（不随仓库提交）。三组都使用同一可执行文件和同一重新编译的 SPIR-V；P03 只切换 SH 存储阶数，P02 只在 P03 SH2 基础上切换投影颜色压缩。

| 变体 | SH 存储阶数 | 投影颜色 | 平均帧时间 | FPS | SH backing | 投影颜色 backing |
|---|---:|---|---:|---:|---:|---:|
| baseline | 3 | FP32 vec4 | 27.3836 ms | 36.5181 | 1,177,335,168 B | 98,111,264 B |
| P03 | 2 | FP32 vec4 | 26.8935 ms | 37.1837 | 662,251,040 B | 98,111,264 B |
| P02 follow-up | 2 | RGB21F packed | 26.8990 ms | 37.1761 | 662,251,040 B | 49,055,632 B |

P03 SH3→SH2 将 SH backing 减少 515,084,128 B（约 43.75%），整帧 FPS 在本次短样本增加约 1.82%；这只是单机短样本结果。P02 在 SH2 上再次将投影颜色 backing 减半（约 49.9999%），该样本 FPS 与 P03 基本相同（约 -0.02%），说明颜色压缩的主要收益是常驻容量而不是当前场景的整帧耗时。

### 9.1 画质门限与单变量结论

使用同相机、线性 RGBA8 readback 计算：

| 对比 | MAE | PSNR | 最大绝对误差 | changed pixels | 门限 |
|---|---:|---:|---:|---:|---|
| baseline SH3 → P03 SH2 | 0.0161071 | 30.4712 dB | 0.309804 | 2,022,776 | FAIL |
| P03 SH2 FP32 → P02 RGB21F | 0.00002253 | 70.5369 dB | 0.003922 | 47,050 | PASS |

P03 SH2 的颜色变化超过预注册门限（MAE≤1/255、PSNR≥40 dB、最大误差≤16/255），因此当前 Bicycle 证据不支持直接采用 SH2 作为默认画质配置；代码保留 SH0/SH1/SH2/SH3 可配置能力，由产品场景决定是否接受画质/容量权衡。P02 RGB21F 在 SH2 基础上的门限通过，可作为独立投影颜色压缩选项；其 FP32 回退仍可用。center/conic 继续保持 FP32，量化延期。

### 9.2 原生窗口截图证据

当前桌面会话的 Computer Use sky RPC 未配置，无法从本次续作运行中取得新的原生窗口句柄和截图。因此没有伪造当前运行的标题栏 FPS。仓库中已有上一轮真实窗口证据，包含完整窗口边框和标题栏实时 FPS，可作为截图格式参考，但不作为本次 P03/P02 当前运行的截图结论：

- [历史 baseline 窗口（标题栏 Render FPS: 38）](../GPUResidentCompression/images/bicycle-sh32-window-fps.jpg)
- [历史 packed-FP16 窗口（标题栏 Render FPS: 36）](../GPUResidentCompression/images/bicycle-sh16-window-fps.jpg)

当前续作截图状态：**UNAVAILABLE（工具 RPC 未配置）**。性能数值仍以 GPU `frames.csv`/`metadata.json` 为准，标题栏读数不计入 timing pair。

### 9.3 culling 支持域与阈值复核

本次没有修改 center/conic，也没有扩大 tight culling 支持域。继续采用既有边界：center 有限且 `abs(center)≤1,048,576`；conic 对角在 `[1e-12,1e12]`；退化条件在 `det > 1e-4*a*c` 且 `min(a,c)≥1e-4*max(a,c)` 时才进入 tight 路径；opacity 必须 `>0`；深度支持 `pView.z < -0.0001`；tile 支持使用 `best≤limit+margin`。等号按上述逐条规则处理，非 finite 走 fail-open。SH 降阶和 RGB21F 只改变颜色存储，不改变 center/conic/opacity 或剔除阈值。

## 10. 续作验证结论

- Release 构建：PASS；重新编译的 preprocess/render SPIR-V：PASS。
- Bicycle baseline/P03/P02：三组真实 GPU 运行完成，GPU timing/capacity/readback 均有原始文件。
- P03 画质门限：FAIL，故 SH2 不作为默认配置结论。
- P02 投影颜色单变量门限：PASS，实际颜色 backing 从 98,111,264 B 降至 49,055,632 B。
- 原生标题栏截图：当前会话工具不可用，状态保持 UNAVAILABLE，不以历史截图冒充当前结果。
