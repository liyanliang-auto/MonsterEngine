# 3DGS渲染性能优化：RadixSort

> 更新日期：2026-10-16（Asia/Shanghai）。Spec-forge 批次：`20261016-162032-h8yx`。Release 构建、374项显式回归及独立审查已通过；生产 Matrix 和 Final 12次运行及原始数据审计完成。blocks16在A/D均比32慢，默认保持32。计时开销6次运行与审计已完成。用户随后明确要求减少测试、已有前后对比即可，生产capture不再执行（原AC9未验收，非PASS）；独立证据核验已通过，流水线COMPLETE（回归WARN：候选不采用、原AC9按用户要求免做）。

## 1. 目标、上下文与闭环

本轮以[工作交接](D:/code/MonsterEngine/devDocument/3DGS/3DGS_RadixSort优化_工作交接_2026-10-16.md)作为初始上下文，沿用[Tight Tile Culling 专题](D:/code/MonsterEngine/devDocument/3DGS/3DGS渲染性能优化_Tight_Tile_Culling.md)的需求、原理、接口、基线、验证、性能归因、证据和面试解释结构。实施范围为完整稳定 GPU Radix 排序的每工作组负载调参，以及支持该实验的配置、容量保护、分阶段时间戳和证据工具。

闭环为“需求确认→spec-forge 计划→变更前基线→编码→测试→独立审查→最终回归与性能对比→证据核验→采用决定”。需求已归一化到[spec.md](D:/code/MonsterEngine/specs/20261016-162032-h8yx/spec.md)，文件归属与共享契约见[job-manifest.json](D:/code/MonsterEngine/specs/20261016-162032-h8yx/job-manifest.json)。本报告不把历史 Tight Culling 数字作为本轮新基线，不把工具编码完成等同于实验执行完成。

保留 256 bin、8 bit LSD、完整 32 bit depth、完整 64 bit key 与精确稳定语义。48 bit 路径仅在最高 16 bit 保证为零时使用。Scan、Raster、LOD、剔除算法、深度量化及 Android 移植不作为本轮加速来源。所有工作保留在本地，原始未跟踪资料与历史证据不覆盖。

## 2. 需求、验收矩阵与采用门槛

| AC | 要求 | 当前证据与状态 |
| --- | --- | --- |
| 1 | 旧 blocks32 可选择；仅接受 4/8/16/32，记录 requested/effective | PASS：4/8/16/32运行时与非法值覆盖，默认32 |
| 2 | 48/64 bit 精确稳定 key/value；activeIdx、even 输出所有权不变 | PASS：48/64位、完整key/value稳定CPU oracle |
| 3 | 空/单条、边界、重复、集中/随机、百万条、direct/indirect、归零、overflow | PASS：GPU276项包含旧55与新增221覆盖 |
| 4 | 主机与普通/split prepare、分配/dispatch 容量一致 | PASS：20 byte ABI、普通/split prepare与容量GPU验证 |
| 5 | 每帧 Histogram/Offsets/Scatter 聚合6/8轮；查询完整性 | 聚合测试及真实采样审计通过；计时开销见8.6 |
| 6 | 新同设备构建、部署身份、模型与配置、唯一证据路径 | 新Release/独立部署及Final 619项身份复查PASS（593 source/19 shader/6 input/EXE） |
| 7 | blocks4/8/16/32 随机/重复/集中大规模微基准 | PASS：微基准2304样本/192配置运行，同一进程 |
| 8 | 固定全量 A 与 D 漫游，最终至少3组交替/反向配对 | Final A/D各3组独立交替配对完整执行，summary RECORDED/errors=[] |
| 9 | 真实同相机原始 RGBA8 全通道逐字节对照 | 用户要求快速收尾后不再执行；原AC9未验收，非PASS；已有64×64合成GPU一致性保留 |
| 10 | 收益超过噪声且整帧 p95 无实质退化才采用默认 | 不采用16：A/D所有配对均慢于32；维持32 |
| 11 | 新 EXE/SPIR-V 构建、回归、独立 review 和证据核验 | Release与374项回归PASS，review APPROVE；独立证据核验PASS，按用户缩减范围完成 |
| 12 | 专题文档、总计划、失败与限制、复现资料 | 本文与总计划已回填最终结果 |

不承诺固定加速百分比。候选正确但没有超过噪声的整帧收益，可以形成“不采用”的完整实验结论。没有执行的项目仍为未完成，不能以“不采用”绕过必要验证。Android/OpenGL 运行与硬件计数器不在已验证范围。

## 3. 数据流与稳定性推导

### 3.1 排序位置与键值契约

数据流：Preprocess → Scan → PrepareDispatch → AssignKeys → RadixSort（Histogram → Offsets → Scatter，循环6/8次）→ TileBounds → Raster。`key=(uint64(tileID)<<32)|depthBits`，value 为 activeIdx；消费端通过活跃源映射读取高斯。更改工作组划分不允许更改键、深度精度或透明度合成顺序。

### 3.2 一轮 Scatter 为什么稳定

设本轮 digit 为 b，输入位置 i 属于工作组 w、工作组内块 k、块内 lane l。其目标位置为：

`dst(i) = 所有小于 b 的桶总数 + 同桶在较早工作组的条目数 + 同桶在本组较早块的条目数 + 同桶在当前块较早 lane 的条目数`。

Histogram 对每组的每桶计数，Offsets 形成前两项；Scatter 按 k 从小到大处理，每块用 `bin_flags[256][8]` 的32位 bitset与 `bitCount`计算较早 lane 的同桶数。当前桶最后一个有效 lane 用 `atomicAdd(global_offsets[bin], count)`推进下一块起点，块末 barrier 保证可见。位图按 local invocation ID 排序，稳定性不依赖原子到达先后或原生 subgroup 宽度恒为32。

所以同 digit 的 i<j 必有 dst(i)<dst(j)，不同 digit 的范围互不相交。有效位置仅被写入一次，所有有效输入应恰好出现一次。尾块无效 lane 不设置位、不写输出，但仍参与组内 barrier；不能因尾部逐线程提前退出而破坏同步。工作组级容量/overflow guard 必须在组内一致的条件下返回。

### 3.3 多轮 LSD 与输出所有权

第 r 轮稳定按第 r 个低位字节排序；归纳可得 r 轮后按最低8r位有序，并保留这些位完全相等的原始输入顺序。6轮完成48 bit，8轮完成64 bit，两者都是偶数轮，ping-pong 最终回到 even key/value buffer。全键相等时 value 必须保持原输入顺序，不能只检查 key 单调性。

48 bit 合法性来自高16 bit恒零；生产选择条件为 Tile 总数不超过65536。深度始终保留低32 bit。测试必须包含高字节非零的64 bit键，避免错误地把64 bit路径降为48 bit。上述推导是审查依据，实际 shader、间接计数和同步仍须由独立 CPU stable oracle 检查完整 key/value 序列。

## 4. 实现、API、ABI 与资源约束

### 4.1 源码导航

| 文件 | 职责 |
| --- | --- |
| [SplatSortTypes.h](D:/code/MonsterEngine/Include/Renderer/Splat/SplatSortTypes.h) | 合法负载、安全ceil除法、Radix与Prepare push constants |
| [SplatSortPass.h](D:/code/MonsterEngine/Include/Renderer/Splat/SplatSortPass.h)、[cpp](D:/code/MonsterEngine/Source/Renderer/Splat/SplatSortPass.cpp) | 按配置分配、6/8轮排序、每轮唯一查询名 |
| [SplatPipeline.h](D:/code/MonsterEngine/Include/Renderer/Splat/SplatPipeline.h)、[cpp](D:/code/MonsterEngine/Source/Renderer/Splat/SplatPipeline.cpp) | 初始化前设置、预算、实际分配统计、查询聚合 |
| [Histogram](D:/code/MonsterEngine/Shaders/Splat/Sort/splat_radix_histogram.comp)、[Offsets](D:/code/MonsterEngine/Shaders/Splat/Sort/splat_radix_offsets.comp)、[Scatter](D:/code/MonsterEngine/Shaders/Splat/Sort/splat_radix_scatter.comp) | 配置一致性与容量guard；保留精确排序结构 |
| [Prepare](D:/code/MonsterEngine/Shaders/Splat/Sort/splat_prepare_dispatch.comp)、[Split Prepare](D:/code/MonsterEngine/Shaders/Splat/Sort/splat_prepare_split_dispatch.comp) | 同一负载产生GPU间接参数与overflow状态 |
| [SplatLODBenchmark](D:/code/MonsterEngine/Source/Renderer/Splat/LOD/SplatLODBenchmark.cpp)、[SplatSceneApplication](D:/code/MonsterEngine/Source/SplatSceneApplication.cpp) | 严格JSON、配置身份、CSV、应用前分配控制 |
| 既有Vulkan时间戳后端 | 复用既有query pool、提交归属与查询回收；本轮未修改后端 |

实现清单见[coding-manifest-J0-1.json](D:/code/MonsterEngine/specs/20261016-162032-h8yx/coding-manifest-J0-1.json)；实验工具清单见[coding-manifest-J0-2.json](D:/code/MonsterEngine/specs/20261016-162032-h8yx/coding-manifest-J0-2.json)。对应Release及GPU执行已通过；最终部署与identity见第8章。

### 4.2 实际控制与 ABI

JSON `radixSortBlocksPerWorkgroup` 为整数4、8、16或32，默认32。`radixSortPhaseTiming` 为bool，默认true；应用仅在 benchmark 且非 captureOnly 时启用额外子阶段查询，普通交互不启用。`SetRadixSortBlocksPerWorkgroup(uint32)`和`SetRadixPhaseTiming(bool)`仅允许初始化前调用，避免缓冲分配后改变工作组契约。非法配置返回失败，不能静默改成参考值。

`FRadixSortPushConstants`仍为16 byte：numElements、shift、numWorkgroups、numBlocksPerWorkgroup，各uint32。`FSplatPrepareDispatchPushConstants`为20 byte：activeCount、sortCapacity、gridX、gridY、radixBlocks；radixBlocks offset16，主机static_assert约束大小与偏移。普通与split producer必须同时更新并重编译，不能混用旧16 byte prepare SPIR-V。

每组容量 C=256×B，组数 G=ceil(N/C)，用商和余数相加避免 `N+C-1` 溢出。分配按sortCapacity而非当前帧条目计算组上限。GPU count路径仍读GPU dispatch-state，`UINT32_MAX`是shader中区分GPU count的标志，不新增每帧CPU count readback。必须保留空帧/归零、sortCapacity越界与65535工作组界限保护，正常与split/fusion生产者使用同一B。

metadata记录 `radixSortBlocksPerWorkgroupRequested/Effective`、`radixSortPhaseTimingRequested/Effective`；frames.csv记录 `radix_blocks_requested/effective`、`radix_phase_timing`。配置SHA256包含负载与计时开关；capture身份也包含requested/effective，便于核对实际生效。

## 5. 工作组粒度、内存与计时代价

### 5.1 工作负载取舍

| B | 每组条目C | 64,000,000容量组数G | 直方图逻辑字节 G×256×4 |
| ---: | ---: | ---: | ---: |
| 4 | 1024 | 62500 | 64,000,000 |
| 8 | 2048 | 31250 | 32,000,000 |
| 16 | 4096 | 15625 | 16,000,000 |
| 32 | 8192 | 7813 | 8,000,512 |

减少B会缩短组内串行块循环、增加并发工作组，也会增加 Histogram/Offsets 的工作、直方图容量和同步调度成本。极大capacity下小B会触及组数上限，配置须被拒绝。旧256桶偏移预计算已存在，本轮不把它重新计作新增优化。

key ping-pong 逻辑总量为16×capacity byte，value为8×capacity byte；64M对应1,024,000,000与512,000,000 byte，均不随B减少。表内为逻辑尺寸；实际内存以资源allocation backing为准，可能含对齐。新增 `radix_keys_bytes`、`radix_values_bytes`、`radix_histogram_bytes`为实际专用分配统计，已包含在temporary_bytes中，禁止再加一次。source/resident、输出图像、staging、allocator保留容量分别解释，条目下降不意味着实际分配下降。

### 5.2 GPU 分阶段时间戳

每轮使用唯一 `RadixHistogram/r`、`RadixOffsets/r`、`RadixScatter/r` begin/end，按原始frame ID聚合。只有三阶段各自的6/8轮位图都完整才发布无后缀阶段总时间；重复轮被去重，未完整结果不补零。聚合等待窗口有128帧上限，最终统计必须报告missing/late，不把未到齐的帧作为完整样本。

既有后端提供逐命令缓冲查询生命周期保护，本轮未修改Vulkan后端：每个区间独立query pool内有两个查询，记录提交token，完成后才解析与回收；未提交命令取消时清理。记录上限4096，耗尽报告unavailable，不回退为CPU时间。查询使用真实timestampPeriod与validBits处理tick差，缺失begin/end不得伪造零耗时。新增时间戳生命周期、不可用及聚合测试已通过。

实际原始文件名为 `gpu_samples.csv`，包含frame/stage/gpu_ms；不是设计阶段泛称的gpu-timings.csv。frames.csv新增 `radix_histogram_ms`、`radix_offsets_ms`、`radix_scatter_ms`，保留radix_ms。WholeFrame、Pipeline、RadixSort及子阶段是包含关系，不能相加。子阶段差额可含barrier、查询边界及其他开销，不直接等于某类硬件stall。每帧新增18/24个区间，即36/48个时间戳写入，需用 `radixSortPhaseTiming=false/true`同条件对照开销；当前无计数器，不能宣称已证明带宽/occupancy瓶颈。

## 6. 本轮已执行的变更前基线

完整报告：[baseline-regression-report.md](D:/code/MonsterEngine/specs/20261016-162032-h8yx/baseline-regression-report.md)，统计快照：[perf-baseline.json](D:/code/MonsterEngine/specs/20261016-162032-h8yx/perf-baseline.json)。此处为本轮新执行的旧实现基线，不是变更后验收。

### 6.1 构建、测试与身份

Release configure/build均exit0，VS2019 x64，VulkanSDK1.3.296.0 glslc，新编译后部署到独立baseline/deployed。CPU47/47、GPU55/55、integration15/15、builder/viewer CLI9/9、Python工具11/11，共137项显式case/assertion通过；另legacy --test-all与--test-splat-controls均exit0。legacy日志含预期headless surface错误，不表示真实窗口surface覆盖。完整逐命令、cwd和退出码见[baseline/test-results.json](D:/code/MonsterEngine/TestFile/3DGSLODStreaming/radix-sort-20261016-162032-h8yx/baseline/test-results.json)。

NVIDIA GeForce RTX3060，deviceID9476，driverVersion2500395008。592项输入hash在基线末复查0变化；EXE SHA256 `e7c4403a33e1f6072714c2fb43af1bbb4850cdef6c6c966870c9f17aeb5e02cc`。完整EXE/SPIR-V身份、场景物理路径和配置位于[baseline目录](D:/code/MonsterEngine/TestFile/3DGSLODStreaming/radix-sort-20261016-162032-h8yx/baseline)，保留source-manifest、deployed-manifest与source-identity-recheck。

### 6.2 A/D 新基线实测

两者1920×1080、SH3、tightTileCulling=2、vsync=false、validation=false、activeCapacity6,131,954、sortCapacity64M、temporary预算4GiB。10s预热后连续3轮各60s采样，sampled=1且excluded=0，按frame连接GPU样本；进程均exit0、metadata RECORDED。以下数字顺序为均值/中位数/p95。

| 指标 | A 全量驻留、固定相机0 | D 漫游 |
| --- | ---: | ---: |
| WholeFrame GPU ms | 33.0227 / 32.7986 / 36.5497 | 24.1843 / 23.5918 / 30.0497 |
| RadixSort ms | 8.54316 / 8.71606 / 9.94707 | 6.73267 / 6.64483 / 8.97475 |
| Frame interval ms | 33.8441 / 33.6200 / 37.2137 | 135.7226 / 129.6252 / 213.9122 |
| Scan ms | 4.67275 / 4.31130 / 6.09398 | 2.19207 / 1.96944 / 3.39280 |
| Sort entries | 4,349,767.26 / 4,349,768 / 4,349,768 | 3,412,471.19 / 3,384,046 / 4,285,604 |
| 实际 temporary byte | 2,067,998,240 | 2,067,998,240 |
| 总帧 / 有效采样帧 | 5651 / 5319 | 1667 / 1325 |
| loading / degraded / overflow（总帧） | 0 / 0 / 0 | 1 / 556 / 0 |

A三轮WholeFrame均值33.1167、33.1131、32.8398ms，Radix8.56217、8.56405、8.50354ms；D分别23.6191、24.6740、24.3568ms与6.55026、6.94866、6.73231ms。它们是连续轮次，不是独立启动的候选/参考交替配对。两者missingGPUFrames、lateGPUSamples、missingCountFrames均0，excluded0；D降级帧保留，不静默过滤。D的帧间隔显著高于GPU区间，不能用GPU时间倒数宣称真实FPS。

原始[A metadata](D:/code/MonsterEngine/TestFile/3DGSLODStreaming/radix-sort-20261016-162032-h8yx/baseline/performance-A/metadata.json)、[A frames](D:/code/MonsterEngine/TestFile/3DGSLODStreaming/radix-sort-20261016-162032-h8yx/baseline/performance-A/frames.csv)、[A GPU](D:/code/MonsterEngine/TestFile/3DGSLODStreaming/radix-sort-20261016-162032-h8yx/baseline/performance-A/gpu_samples.csv)，以及[D metadata](D:/code/MonsterEngine/TestFile/3DGSLODStreaming/radix-sort-20261016-162032-h8yx/baseline/performance-D/metadata.json)、[D frames](D:/code/MonsterEngine/TestFile/3DGSLODStreaming/radix-sort-20261016-162032-h8yx/baseline/performance-D/frames.csv)、[D GPU](D:/code/MonsterEngine/TestFile/3DGSLODStreaming/radix-sort-20261016-162032-h8yx/baseline/performance-D/gpu_samples.csv)完整保留。

### 6.3 失败、异常与归因边界

1. 首次A窗口被最小化，已有metadata但无实际render帧，CPU平稳；恢复真实窗口后才开始约29FPS运行。首次resident时间约344.010405s含暂停，不能作为加载速度成绩。实际提交后完整执行10+180s，未缩短采样。
2. A配置继承functionalSmoke=true，metadata如实标记functional smoke；虽然采样长度已扩展为10s+60s×3，仍保留协议标签限制。最终共同新二进制A/B必须使用false，不能改写旧metadata掩盖。
3. A固定相机sort entries仍在4,349,760–4,349,768间变化，跨度8。此变更前上游非确定性必须保留并调查；若图像不同，先核对输入条目与选择身份，再判断排序错误。
4. 旧基线只有总Radix计时，Histogram/Offsets/Scatter均不可用，不能填0或推算。分阶段比较需要新二进制blocks32共同参考。
5. 历史Tight Culling模式2的32.9811ms整帧/8.2466ms Radix只作上下文；它是另一轮短测，与本轮33.0227/8.54316不能直接作为候选收益计算。

## 7. 构建、运行与分析复现

所有命令在 `D:/code/MonsterEngine` 执行，PowerShell示例中的新部署/输出路径由验证阶段实际生成后记录，不与历史部署混用。现有基线脚本和日志可重现旧实现，新实现已经在candidate/build构建并冻结部署到candidate/deployed-20261016-184000；继续分析使用该匹配部署。

```powershell
# 已执行的基线构建入口与工具链，完整configure参数见日志。
& 'D:/software/VS2019/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe' --build 'TestFile/3DGSLODStreaming/radix-sort-20261016-162032-h8yx/baseline/build' --config Release --parallel 8
# 已执行的基线回归/统计脚本；不要用重跑覆盖旧输出。
# TestFile/3DGSLODStreaming/radix-sort-20261016-162032-h8yx/baseline/run-regression.ps1
# TestFile/3DGSLODStreaming/radix-sort-20261016-162032-h8yx/baseline/summarize-baseline.ps1
```

实验脚本[run_comparison.ps1](D:/code/MonsterEngine/TestFile/3DGSLODStreaming/radix-sort-20261016-162032-h8yx/run_comparison.ps1)参数为Executable、ShaderDirectory、ConfigA、ConfigD、OutputRoot；Candidate默认为8，仅为工具参数默认，不是引擎默认或已选胜者。Phase=Matrix/Final/All；Matrix生产A按32/4/8/16各运行一次，Final每个A/D负载至少3组，按32→候选、候选→32交替。脚本自动创建时间戳+随机后缀子目录，记录配置/源码/EXE/shader/输入hash及每次命令、退出、原始文件hash。起步与进度超时保存失败记录，不能把partial计作完成。

实际被测对象是图形渲染应用，运行时须保持窗口已恢复且可见；最小化会触发引擎跳过渲染。脚本采用Normal窗口启动，不能通过隐藏/最小化运行收集性能成绩。该要求来自已观察到的渲染暂停行为。

以下为复现参数示例；既有Matrix与Final已完成，不要覆盖原始输出。Final实际Candidate=16，运行身份见第8章：

```powershell
$tools = 'D:/code/MonsterEngine/TestFile/3DGSLODStreaming/radix-sort-20261016-162032-h8yx'
& "$tools/run_comparison.ps1" -Executable $newExe -ShaderDirectory $newShaders -ConfigA "$tools/baseline/performance-A.json" -ConfigD "$tools/baseline/performance-D.json" -OutputRoot "$tools/candidate" -Candidate 16 -Phase Final -Repetitions 3 -WarmupSeconds 10 -SampleSeconds 60 -Rounds 3
& $python "$tools/summarize_radix.py" $runManifest --output $newSummary
& $python "$tools/compare_captures.py" $referenceCaptureDir $candidateCaptureDir --output $newCaptureReport
```

`$python`可使用 `C:/Users/Administrator/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe`。summarize_radix按manifest汇总逐run与逐pair的mean/median/p95、身份、缺失与requested/effective；输出以独占创建避免覆盖。compare_captures比较真实GPU读回的每个RGBA通道、尺寸、格式、相机与选择身份，报告SHA256、changedChannelsRGBA及maxByteDifference。RGBA8相同仅证明量化后相同，排序oracle仍为精确语义验收来源。

正式计时固定validation=false、captureOnly=false、benchmark=true、VSync关闭；图像验证另开validation。缩短协议须显式ShortenedProtocol并保存真实时长及functionalSmoke标签，不自动满足默认采用门槛。微基准必须独立运行生产shader，保留随机种子、分布、条目、预热、样本、GPU原始计时与编译身份。匹配Release构建、工具回归、微基准、生产Matrix与Final均已执行；各实验使用实际记录的完整协议，结果见第8章。

## 8. 变更后测试、性能与最终证据记录

本章集中登记实际结果。新Release构建、374项全套回归、stable oracle、微基准、生产Matrix与Final分析已完成。真实capture、计时开销对照和最终证据核验按下文实际状态登记。必须附实际命令、退出码、报告链接、失败修复与剩余限制；不把待执行设计写成结果。

### 8.1 必须覆盖的正确性项目

2026-10-16最终候选Release重建 exit 0，日志见`specs/20261016-162032-h8yx/logs/regression-refresh-20261016-183445/build.log`。CPU 47/47、GPU 276/276、集成20/20、CLI 9/9、Radix工具11/11、既有比较工具11/11通过；legacy `--test-all` 与 `--test-splat-controls` 均exit 0。原始日志位于`specs/20261016-162032-h8yx/logs/regression-refresh-20261016-183709/`。GPU套件执行新增stable key/value oracle与间接调度、时间戳生命周期、同相机GPU图像质量覆盖；独立微基准已有2,304样本/192次配置运行（同一进程，非192次独立启动），stable CPU oracle结果通过，完整汇总见`candidate/microbenchmark-01/summary.json`。这些结果不替代下一阶段真实渲染测量。

48/64 bit独立stable CPU oracle覆盖N=0/1、255/256/257、各B组容量前后及非整倍数、跨组全相等/重复键、集中桶、随机、高字节非零、百万条；比较完整key/value排列以检测遗漏/重复/顺序。direct、GPU indirect、有效count→0、capacity边界、overflow、尾部sentinel、安全even输出与activeIdx映射分别登记。新增测试不得只重述主机公式而跳过实际GPU shader。

### 8.2 必须报告的性能项目

生产Matrix已完成：同一新Release EXE `4237de3ff1706fc6ea328ac349e19ca2d68852de1af0a450a4895131dd3d48da`、独立部署`candidate/deployed-20261016-184000`、RTX3060、1920×1080、SH3、Tight2、固定A相机0、phase timing=true，functionalSmoke=false。32→4→8→16各独立启动，均10s warmup+60s×3，exit0，身份与原始帧/查询/计数/哈希审计无错误。完整报告：[Matrix summary](D:/code/MonsterEngine/TestFile/3DGSLODStreaming/radix-sort-20261016-162032-h8yx/candidate/production-matrix/20261016-184148-0a5e7121/summary.json)，manifest与原始目录同处。

| B | 样本帧 | WholeFrame mean / p95 ms | Radix mean / p95 ms | Histogram / Offsets / Scatter mean ms | actual temporary bytes |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 32 | 6215 | 27.9130 / 29.2965 | 7.3635 / 7.7883 | 0.6952 / 1.2399 / 5.3759 | 2,067,998,240 |
| 4 | 4446 | 39.4008 / 40.8676 | 18.8089 / 19.3066 | 0.7417 / 14.7674 / 3.2472 | 2,123,997,728 |
| 8 | 5597 | 31.1076 / 32.4186 | 10.5572 / 10.9620 | 0.7003 / 5.2985 / 4.5061 | 2,091,997,728 |
| 16 | 5966 | 29.1160 / 30.5097 | 8.5133 / 8.9397 | 0.6792 / 2.6348 / 5.1473 | 2,075,997,728 |

以上为每配置一次Matrix；最终独立重复证据见后文Final明细。小B的Scatter下降被Offsets上升抵消；只能归因为观测到的阶段成本取舍，未采样硬件计数器，不宣称带宽、stall或occupancy实证。16为最强非默认配置，选择它进入Final A/D各3组交替配对以验证负实验；当前维持32。Phase=true/false的blocks32固定A独立3配对结果见8.6；生产RGBA读回按用户最新缩减要求不再执行，不标PASS。旧基线WholeFrame33.0227ms不能直接解释为本次27.9130ms优化收益；它跨构建、跨时段且计时实现不同，排序调参只使用同二进制共同参考。

先比较同一新二进制、同一计时开关下的blocks32与4/8/16；各分布微基准与真实渲染分开。最终参考/候选A和D各至少3个独立启动交替配对，保留10s+60s×3协议、每次运行次序、原始样本和未完成/排除原因；报告WholeFrame、Radix及三子阶段mean/median/p95、frame interval、sort entries、loading/degraded/overflow、实际内存与missing/late。必要时在计时关闭条件确认采用结论，不能把查询开销改善误归因给排序。

### 8.3 当前采用决定

保持引擎默认blocks32，不采用blocks4/8/16。微基准四分布均未胜32；Final全部6对中16的Radix与WholeFrame均更慢，增加直方图分配7,999,488 byte。结果支持本RTX3060/Bicycle/1080p/SH3/Tight2配置的负实验结论，不推广为所有设备/场景的最优配置。

### 8.4 Final正式重复生产实验（已完成）

同一冻结EXE与19份SPIR-V，A/D各3组独立启动配对；repeat0=32→16、repeat1=16→32、repeat2=32→16。每次10秒预热后60秒×3轮，共12次，exit0、timedOut=false；functionalSmoke=false、validation=false、phase=true。Final分析器exit0、summary.status=RECORDED、errors=[]，逐原始文件SHA256、GPU父/子区间、count连接、容量及有效采样窗口审计通过。D为同一路径的动态系统实验，最终相机hash允许随墙钟略变，其余配置/设备/输入身份必须匹配。

[Final summary](D:/code/MonsterEngine/TestFile/3DGSLODStreaming/radix-sort-20261016-162032-h8yx/candidate/production-final/20261016-190017-772eb975/summary.json) 与同目录run-manifest/identity保留完整运行命令、配置和raw hash。以下三元数字为 mean / median / p95；run均值聚合不混称合并帧p95。

| 负载 | B | 采样帧 | 3个run Whole均值的均值ms | Radix同口径ms | 帧间隔同口径ms |
| --- | --- | --- | --- | --- | --- |
| A | 32 | 18567 | 28.02620 | 7.36701 | 29.08308 |
| A | 16 | 17790 | 29.29931 | 8.51570 | 30.35470 |
| D | 32 | 5927 | 20.67724 | 5.94206 | 91.10061 |
| D | 16 | 5830 | 21.58869 | 6.83230 | 92.62277 |

| 负载/r/B | 帧 | Whole ms | Radix ms | 帧间隔ms | loading/degraded/overflow采样 |
| --- | --- | --- | --- | --- | --- |
| A/0/32 | 6204 | 27.9675 / 27.8243 / 29.3360 | 7.3668 / 7.2791 / 7.7881 | 29.0139 / 28.9651 / 30.4549 | 0/0/0 |
| A/0/16 | 5950 | 29.1847 / 29.0340 / 30.5881 | 8.5133 / 8.4192 / 8.9410 | 30.2500 / 30.1837 / 31.7521 | 0/0/0 |
| A/1/16 | 5898 | 29.4591 / 29.3291 / 30.7653 | 8.5183 / 8.4208 / 8.9451 | 30.5184 / 30.4636 / 31.9160 | 0/0/0 |
| A/1/32 | 6176 | 28.0798 / 27.9211 / 29.5112 | 7.3684 / 7.2821 / 7.7991 | 29.1451 / 29.0846 / 30.6914 | 0/0/0 |
| A/2/32 | 6187 | 28.0313 / 27.8696 / 29.4414 | 7.3658 / 7.2812 / 7.7991 | 29.0902 / 29.0358 / 30.6316 | 0/0/0 |
| A/2/16 | 5942 | 29.2541 / 29.1057 / 30.6463 | 8.5156 / 8.4208 / 8.9462 | 30.2957 / 30.2527 / 31.7335 | 0/0/0 |
| D/0/32 | 1988 | 20.6286 / 20.1133 / 25.0907 | 5.9258 / 5.8025 / 7.4072 | 90.5458 / 90.6876 / 114.3613 | 0/249/0 |
| D/0/16 | 1948 | 21.6013 / 21.0337 / 26.1841 | 6.8383 / 6.6650 / 8.5424 | 92.4057 / 92.1315 / 116.1856 | 0/190/0 |
| D/1/16 | 1937 | 21.5960 / 21.0271 / 26.3370 | 6.8328 / 6.6721 / 8.5810 | 92.9387 / 92.7059 / 118.7829 | 0/194/0 |
| D/1/32 | 1961 | 20.7108 / 20.2538 / 25.1064 | 5.9517 / 5.8497 / 7.3781 | 91.7880 / 91.7454 / 116.1132 | 0/195/0 |
| D/2/32 | 1978 | 20.6923 / 20.1690 / 25.1765 | 5.9487 / 5.8375 / 7.4051 | 90.9680 / 91.0275 / 116.0663 | 0/196/0 |
| D/2/16 | 1945 | 21.5688 / 21.0291 / 26.1590 | 6.8257 / 6.6458 / 8.5323 | 92.5239 / 92.4827 / 117.4792 | 0/188/0 |

| 负载/r/B | Histogram ms | Offsets ms | Scatter ms | sort entries |
| --- | --- | --- | --- | --- |
| A/0/32 | 0.6945 / 0.6849 / 0.6958 | 1.2424 / 1.2281 / 1.2529 | 5.3774 / 5.3107 / 5.6919 | 4349766.7074 / 4349768.0000 / 4349768.0000 |
| A/0/16 | 0.6811 / 0.6713 / 0.6821 | 2.6350 / 2.6046 / 2.8140 | 5.1451 / 5.0881 / 5.4453 | 4349766.0768 / 4349768.0000 / 4349768.0000 |
| A/1/16 | 0.6843 / 0.6713 / 0.6851 | 2.6368 / 2.6043 / 2.8778 | 5.1444 / 5.0890 / 5.4467 | 4349763.7243 / 4349767.0000 / 4349767.0000 |
| A/1/32 | 0.6940 / 0.6850 / 0.6956 | 1.2441 / 1.2297 / 1.2610 | 5.3777 / 5.3118 / 5.7043 | 4349766.4511 / 4349768.0000 / 4349768.0000 |
| A/2/32 | 0.6960 / 0.6851 / 0.6961 | 1.2428 / 1.2298 / 1.2512 | 5.3744 / 5.3106 / 5.6919 | 4349766.7866 / 4349768.0000 / 4349768.0000 |
| A/2/16 | 0.6834 / 0.6713 / 0.6832 | 2.6364 / 2.6046 / 2.8147 | 5.1431 / 5.0887 / 5.4414 | 4349763.9763 / 4349767.0000 / 4349768.0000 |
| D/0/32 | 0.5578 / 0.5317 / 0.6896 | 1.0381 / 1.0472 / 1.2931 | 4.2755 / 4.1335 / 5.4169 | 3443606.5463 / 3393087.5000 / 4294691.5000 |
| D/0/16 | 0.5601 / 0.5377 / 0.6770 | 2.1307 / 2.0543 / 2.7251 | 4.0926 / 3.9716 / 5.1226 | 3464177.5780 / 3399690.0000 / 4294295.4000 |
| D/1/16 | 0.5608 / 0.5379 / 0.6768 | 2.1249 / 2.0548 / 2.7052 | 4.0929 / 3.9716 / 5.1653 | 3460970.5209 / 3399326.0000 / 4293943.6000 |
| D/1/32 | 0.5589 / 0.5332 / 0.6908 | 1.0472 / 1.0492 / 1.2975 | 4.2915 / 4.1644 / 5.3738 | 3463495.4646 / 3399639.0000 / 4293663.0000 |
| D/2/32 | 0.5609 / 0.5331 / 0.6907 | 1.0430 / 1.0478 / 1.3056 | 4.2909 / 4.1812 / 5.3731 | 3459634.8544 / 3398945.5000 / 4294584.1000 |
| D/2/16 | 0.5605 / 0.5380 / 0.6775 | 2.1205 / 2.0538 / 2.6870 | 4.0903 / 3.9711 / 5.1170 | 3459996.6329 / 3399440.0000 / 4294692.6000 |

逐独立配对，正号表示16更慢：

| 负载/r | ΔWhole mean ms | ΔRadix mean ms | ΔWhole p95 ms | ΔRadix p95 ms | Δ帧间隔mean ms |
| --- | --- | --- | --- | --- | --- |
| A/0 | +1.21720 | +1.14643 | +1.25205 | +1.15289 | +1.23611 |
| A/1 | +1.37934 | +1.14991 | +1.25413 | +1.14596 | +1.37329 |
| A/2 | +1.22281 | +1.14972 | +1.20494 | +1.14706 | +1.20547 |
| D/0 | +0.97273 | +0.91252 | +1.09345 | +1.13529 | +1.85986 |
| D/1 | +0.88516 | +0.88119 | +1.23064 | +1.20293 | +1.15069 |
| D/2 | +0.87648 | +0.87699 | +0.98249 | +1.12721 | +1.55594 |

逐轮（每run内部连续轮次，非新增独立启动）；完整子阶段/帧间隔/内存逐轮统计在summary.rounds：

| 负载/r/B/轮 | 帧 | Whole ms | Radix ms | sort mean |
| --- | --- | --- | --- | --- |
| A/0/32/0 | 2073 | 27.9067 / 27.7666 / 29.2843 | 7.3641 / 7.2767 / 7.7849 | 4349767.006 |
| A/0/32/1 | 2069 | 27.9492 / 27.8054 / 29.3385 | 7.3644 / 7.2799 / 7.7851 | 4349766.101 |
| A/0/32/2 | 2062 | 28.0470 / 27.8934 / 29.3981 | 7.3720 / 7.2807 / 7.7963 | 4349767.016 |
| A/0/16/0 | 1988 | 29.0848 / 28.9419 / 30.4824 | 8.5143 / 8.4146 / 8.9396 | 4349766.992 |
| A/0/16/1 | 1981 | 29.2306 / 29.0641 / 30.6470 | 8.5127 / 8.4215 / 8.9453 | 4349764.786 |
| A/0/16/2 | 1981 | 29.2390 / 29.0967 / 30.6651 | 8.5129 / 8.4202 / 8.9404 | 4349766.448 |
| A/1/16/0 | 1970 | 29.4062 / 29.2671 / 30.7290 | 8.5049 / 8.4142 / 8.9381 | 4349765.586 |
| A/1/16/1 | 1964 | 29.4908 / 29.3643 / 30.7772 | 8.5262 / 8.4236 / 8.9502 | 4349762.566 |
| A/1/16/2 | 1964 | 29.4806 / 29.3369 / 30.7820 | 8.5237 / 8.4227 / 8.9471 | 4349763.015 |
| A/1/32/0 | 2059 | 28.0605 / 27.8974 / 29.4826 | 7.3704 / 7.2811 / 7.7983 | 4349766.499 |
| A/1/32/1 | 2056 | 28.1276 / 27.9909 / 29.5496 | 7.3697 / 7.2829 / 7.8008 | 4349765.756 |
| A/1/32/2 | 2061 | 28.0513 / 27.8830 / 29.4763 | 7.3650 / 7.2825 / 7.7964 | 4349767.096 |
| A/2/32/0 | 2061 | 28.0482 / 27.8800 / 29.4371 | 7.3616 / 7.2790 / 7.7960 | 4349766.643 |
| A/2/32/1 | 2063 | 28.0315 / 27.8763 / 29.4158 | 7.3688 / 7.2820 / 7.7953 | 4349766.487 |
| A/2/32/2 | 2063 | 28.0142 / 27.8473 / 29.4857 | 7.3671 / 7.2820 / 7.8066 | 4349767.230 |
| A/2/16/0 | 1985 | 29.1510 / 29.0200 / 30.4916 | 8.5156 / 8.4154 / 8.9420 | 4349766.760 |
| A/2/16/1 | 1978 | 29.3309 / 29.1666 / 30.7453 | 8.5157 / 8.4214 / 8.9480 | 4349763.976 |
| A/2/16/2 | 1979 | 29.2808 / 29.1507 / 30.6485 | 8.5153 / 8.4240 / 8.9510 | 4349761.185 |
| D/0/32/0 | 663 | 20.5126 / 20.0057 / 25.2333 | 5.8544 / 5.7255 / 7.4060 | 3405080.324 |
| D/0/32/1 | 660 | 20.6707 / 20.1491 / 25.0248 | 5.9590 / 5.8476 / 7.3893 | 3462553.042 |
| D/0/32/2 | 665 | 20.7025 / 20.2199 / 25.0488 | 5.9640 / 5.8432 / 7.4030 | 3463212.859 |
| D/0/16/0 | 653 | 21.5792 / 21.0736 / 25.9793 | 6.8209 / 6.6424 / 8.5032 | 3454463.622 |
| D/0/16/1 | 649 | 21.5893 / 21.0680 / 26.2335 | 6.8405 / 6.6743 / 8.5441 | 3465464.387 |
| D/0/16/2 | 646 | 21.6357 / 20.9904 / 26.2396 | 6.8538 / 6.6832 / 8.5492 | 3472704.009 |
| D/1/16/0 | 645 | 21.6059 / 21.0667 / 26.2123 | 6.8232 / 6.6885 / 8.5938 | 3455484.518 |
| D/1/16/1 | 642 | 21.6047 / 20.9878 / 26.4348 | 6.8400 / 6.6585 / 8.5613 | 3463723.539 |
| D/1/16/2 | 650 | 21.5775 / 21.0200 / 26.3151 | 6.8354 / 6.6716 / 8.5714 | 3463695.189 |
| D/1/32/0 | 651 | 20.7118 / 20.2992 / 25.0672 | 5.9440 / 5.8462 / 7.4427 | 3459360.106 |
| D/1/32/1 | 658 | 20.6869 / 20.1588 / 25.1850 | 5.9500 / 5.8494 / 7.3472 | 3463840.728 |
| D/1/32/2 | 652 | 20.7340 / 20.2668 / 25.1829 | 5.9610 / 5.8518 / 7.3637 | 3467276.040 |
| D/2/32/0 | 658 | 20.6726 / 20.1748 / 25.1100 | 5.9333 / 5.8262 / 7.3603 | 3452484.520 |
| D/2/32/1 | 661 | 20.6912 / 20.1596 / 25.2010 | 5.9621 / 5.8582 / 7.4520 | 3462776.927 |
| D/2/32/2 | 659 | 20.7130 / 20.1708 / 25.2061 | 5.9507 / 5.8341 / 7.3780 | 3463622.730 |
| D/2/16/0 | 650 | 21.5720 / 21.1021 / 26.1528 | 6.8173 / 6.6668 / 8.4968 | 3454511.306 |
| D/2/16/1 | 649 | 21.5620 / 21.0268 / 26.1075 | 6.8181 / 6.6266 / 8.5372 | 3460531.169 |
| D/2/16/2 | 646 | 21.5723 / 20.9978 / 26.1587 | 6.8418 / 6.6393 / 8.5501 | 3464978.906 |

总帧标志补充（含预热/加载，不与采样帧混用）：

| 负载/r/B | 总帧 | loading/degraded/overflow/excluded总帧 |
| --- | --- | --- |
| A/0/32 | 6580 | 0/0/0/0 |
| A/0/16 | 6310 | 0/0/0/0 |
| A/1/16 | 6258 | 0/0/0/0 |
| A/1/32 | 6550 | 0/0/0/0 |
| A/2/32 | 6562 | 0/0/0/0 |
| A/2/16 | 6303 | 0/0/0/0 |
| D/0/32 | 2326 | 1/555/0/0 |
| D/0/16 | 2187 | 1/397/0/0 |
| D/1/16 | 2176 | 1/401/0/0 |
| D/1/32 | 2210 | 1/412/0/0 |
| D/2/32 | 2223 | 1/409/0/0 |
| D/2/16 | 2185 | 1/396/0/0 |

所有运行missingGPUFrames、lateGPUSamples、missingCountFrames=0，采样excluded=0、overflow=0；A loading/degraded=0。D degraded采样按表保留，不能剔除或宣称动态负载恒定。固定A sort count仍有个位数变化；D配对sort count均值差异最多约20571条，因此D作为系统行为支持，不替代A隔离结论。

实际专用分配：key=1,024,000,000 byte、value=512,000,000 byte；histogram32=8,000,512、16=16,000,000 byte；temporary分别2,067,998,240/2,075,997,728 byte。32→16增加7,999,488 byte，分配由sortCapacity64M决定，不随当帧sort count缩小。

A 3个run Whole均值范围：32为27.9675–28.0798ms，16为29.1847–29.4591ms；Radix分别7.3658–7.3684与8.5133–8.5183ms。候选退化1.2172–1.3793ms Whole与1.1464–1.1499ms Radix，方向可重复。A平均Whole +4.54%、Radix +15.59%；D对应+4.41%/+14.98%。所有配对Whole和Radix p95也退化；没有采用16的证据。A的Offsets增加约1.393ms，抵消Scatter减少约0.232ms。这是阶段时间观测，不是硬件counter归因。

### 8.5 独立排序微基准补充

[Microbenchmark summary](D:/code/MonsterEngine/TestFile/3DGSLODStreaming/radix-sort-20261016-162032-h8yx/candidate/microbenchmark-01/summary.json)：2304 GPU samples，192配置运行在同一进程内，原始字段independentRuns不代表192个进程。N=1,048,579与4,349,768、48/64位、四分布、四blocks；3重复、4预热+12采样，顺序奇数重复反转；最后采样输出完整key/value stable CPU oracle通过。逐配置/重复total与phase的mean/median/p95均在summary；上传与readback不计排序区间。

| 约435万条/48位total mean ms | B4 | B8 | B16 | B32 |
| --- | --- | --- | --- | --- |
| random | 24.6922 | 16.3145 | 15.4533 | 14.6211 |
| duplicate | 18.0823 | 8.4207 | 5.5396 | 4.1759 |
| concentrated | 23.5472 | 14.7662 | 13.5551 | 12.9381 |
| tile-depth | 22.0774 | 13.3057 | 11.6329 | 10.7375 |

四分布均未胜32。微基准与生产测量分开报告；没有硬件带宽、occupancy、stall counters，不将阶段取舍解释成已证明硬件瓶颈。

### 8.6 子阶段计时开销实验（已完成）

[完整summary](D:/code/MonsterEngine/TestFile/3DGSLODStreaming/radix-sort-20261016-162032-h8yx/candidate/phase-overhead/20261016-203422-42e95dc9/summary.json)，同目录保留identity、run-manifest和全部原始文件。固定A/blocks32，同一部署，true→false、false→true、true→false共6次独立启动，每次10秒预热+60秒×3，全部exit0、无timeout，hash/父区间/count/配置/相机审计errors=[]。false时三个子阶段CSV为空且无对应GPU查询，保持Unavailable，不填0。以下三元为mean / median / p95：

| repeat/phase | 采样帧 | Whole ms | Radix ms | 帧间隔ms |
| --- | --- | --- | --- | --- |
| 0/True | 5374 | 32.73235 / 32.50414 / 35.42982 | 8.20571 / 7.77078 / 9.76026 | 33.49466 / 33.29985 / 36.20269 |
| 0/False | 5356 | 32.80482 / 32.55286 / 35.68978 | 8.18371 / 7.82336 / 9.65698 | 33.60814 / 33.34465 / 36.51078 |
| 1/False | 5476 | 32.07030 / 32.16941 / 34.64426 | 7.82820 / 7.51139 / 9.44398 | 32.86773 / 32.92845 / 35.56435 |
| 1/True | 5327 | 32.94987 / 32.72307 / 36.33952 | 8.75360 / 8.89213 / 10.01208 | 33.79419 / 33.52580 / 37.09987 |
| 2/True | 5270 | 33.28983 / 33.26379 / 36.59187 | 8.82429 / 9.06998 / 10.25044 | 34.15729 / 34.00785 / 37.42861 |
| 2/False | 5177 | 33.95283 / 33.88093 / 37.15772 | 8.98317 / 9.28851 / 10.31064 | 34.76356 / 34.63590 / 37.89306 |

| repeat | on-off Whole mean ms | on-off Radix mean ms | on-off 帧间隔mean ms |
| --- | --- | --- | --- |
| 0 | -0.072464 | +0.022005 | -0.113475 |
| 1 | +0.879579 | +0.925399 | +0.926464 |
| 2 | -0.663002 | -0.158873 | -0.606273 |

逐轮均值（全部mean/median/p95、内存、sort count与标志见summary.rounds）：

| repeat/phase/轮 | Whole mean ms | Radix mean ms | 帧间隔mean ms |
| --- | --- | --- | --- |
| 0/True/0 | 32.67898 | 8.17787 | 33.42589 |
| 0/True/1 | 32.75422 | 8.22359 | 33.51184 |
| 0/True/2 | 32.76403 | 8.21575 | 33.54650 |
| 0/False/0 | 33.31594 | 8.43336 | 34.17407 |
| 0/False/1 | 32.53547 | 8.00063 | 33.32025 |
| 0/False/2 | 32.57597 | 8.12348 | 33.34440 |
| 1/False/0 | 31.25583 | 7.77161 | 32.08785 |
| 1/False/1 | 32.50881 | 7.91251 | 33.27502 |
| 1/False/2 | 32.47651 | 7.80258 | 33.26930 |
| 1/True/0 | 33.14677 | 8.74182 | 33.99444 |
| 1/True/1 | 32.83117 | 8.73188 | 33.67691 |
| 1/True/2 | 32.87347 | 8.78703 | 33.71304 |
| 2/True/0 | 32.84949 | 8.49468 | 33.70221 |
| 2/True/1 | 33.44253 | 8.97772 | 34.30051 |
| 2/True/2 | 33.58705 | 9.00745 | 34.47912 |
| 2/False/0 | 34.10112 | 8.96553 | 34.94914 |
| 2/False/1 | 33.76524 | 9.00019 | 34.60162 |
| 2/False/2 | 33.99385 | 8.98359 | 34.74176 |

三配对Whole差值从-0.66300到+0.87958ms、Radix从-0.15887到+0.92540ms，方向不稳定；这些独立长测包含系统/频率/负载漂移，不能从粗均值分离纯timestamp指令成本，也不能宣称零开销。未采集频率/硬件计数器，不能指定漂移原因。该时段Whole约32–34ms，而Final约28–29ms；跨时段差异不能归因于排序。保持32，因此不追加16关闭计时长测。

窗口以Normal启动并通过computer-use按返回窗口身份恢复/确认；部分启动时激活超时与瞬时遮挡保留在日志，没有假称每次截图均成功。正式采样实际完整推进；六run最大相邻采样elapsed间隙均小于0.205秒，无长暂停，全部loading/degraded/overflow/excluded=0，missing/late=0。启动/就绪等待不作为加载成绩。

### 8.7 用户调整范围与未执行的生产图像验证

用户在最后一轮计时临近完成时明确要求：“你测试时间太长了，不需要太严谨，也不需要测试太多，与优化前有一个比对就可以。差不多就可以收尾了”。据此完成已剩约29秒的运行后停止新增GPU实验，不启动生产capture；该场景每次启动约一分钟，继续双运行不符合快速收尾要求。原始范围不回写成已通过，变更记录见[user-scope-relaxation.json](D:/code/MonsterEngine/specs/20261016-162032-h8yx/user-scope-relaxation.json)。

**原AC9真实生产同相机RGBA8读回未执行（NOT_EXECUTED_USER_WAIVED），不是PASS。** 已完成64×64合成场景四配置全通道一致性和完整key/value stable oracle；这些证据不替代生产图像，也不证明原始生产场景的量化前等价。旧固定A sort count 4,349,760–4,349,768变化保留为上游风险。已准备capture脚本但未运行，不产生虚构capture结论。

### 8.8 最终身份、回归与性能比较边界

[最终identity审计](D:/code/MonsterEngine/TestFile/3DGSLODStreaming/radix-sort-20261016-162032-h8yx/candidate/final-closure-identity-audit.json)：593源码+19SPIR-V+6输入+EXE共619项，以及原部署146文件全部匹配；[构建/部署对照](D:/code/MonsterEngine/TestFile/3DGSLODStreaming/radix-sort-20261016-162032-h8yx/candidate/build-deployment-match.json)确认EXE和19份SPIR-V一致。保留原始运行日志与异常，无commit/push、无删除既有资料。

[perf-final](D:/code/MonsterEngine/specs/20261016-162032-h8yx/perf-final.json)保留native A/D结果与6配对。[CLI比较](D:/code/MonsterEngine/specs/20261016-162032-h8yx/perf-compare.json)使用另存的原始帧间隔适配快照；历史perf-baseline原样保留，因为其native schema无JMeter Total而被CLI拒绝，错误和退出码见perf-compare-original-schema.json。适配的throughput为1000/平均真实frame_ms，p99由原始frame_ms计算；不是GPU区间倒数、HTTP吞吐或HTTP错误率。历史比较通用阈值PASS只说明未触发该粗门槛，跨构建/时段、旧functionalSmoke标签不支持排序收益归因。新EXE的32→16帧间隔throughput -4.19%、p99 +3.85%，通用阈值也PASS，但Radix约+15%和整帧约+4.5%的稳定退化明确不支持采用。

回归结论为WARN：构建/374项功能回归/部署通过，负性能实验不采用，生产图像原AC9按用户新范围未验证。独立review为APPROVE；随后由spec-forge独立evidence-verifier只读核验实际证据与缩减范围。未声称原12项全部生产验收PASS。

## 9. 面试解释与后续问题

本轮问题是每组串行处理8192条是否适合当前GPU。优化轴把每组工作改成1024/2048/4096/8192条，保持键、稳定性和透明度次序；代价是更多组、offset工作与直方图显存。稳定性由桶前缀、工作组前缀、块前缀和lane前缀共同保证，最终再用GPU与独立CPU oracle完整比较。

可讲的已测事实是同一新EXE下，blocks16对比32在固定A的Radix增加约15.59%、WholeFrame增加约4.54%；D分别增加约14.98%和4.41%。较小组的Scatter节省被Offsets增长抵消。旧实现A中Radix8.54ms/整帧33.02ms只作历史上下文。调参胜负要同时看GPU子阶段、整帧p95、内存、输入分布及重复噪声；减少排序轮数必须有高位恒零证明，减少深度位数则是另一个允许质量变化的实验。移动端需要能力查询、真实设备与持续运行证据，桌面配置不能直接推广。
