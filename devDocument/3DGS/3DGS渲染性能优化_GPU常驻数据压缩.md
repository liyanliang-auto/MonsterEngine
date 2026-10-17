# 3DGS渲染性能优化：GPU常驻数据压缩

> 日期：2026-10-17（北京时间）。P02第一轮。
> 当前状态：P02第一轮全量A的SH FP16常驻存储、FP32计算已完成实现、必要验证和单组Bicycle性能/内存/画质及完整FPS窗口取证；FP16为可选模式，默认保留FP32。
> 关联：[总计划第5、7.1、12章](./3DGS渲染性能优化总计划.md)。详细本地验证记录未随仓库发布。

## 1. 需求确认与范围

用户要求先完成SH系数FP16常驻存储，读取后仍用FP32计算，保留FP32回退；固定SH3、排序、剔除、混合及Raster batch64，完成Bicycle优化前后性能、实际GPU内存、图像差异与完整FPS窗口截图对比。用户最新要求简化测试：必要功能检查＋一组Bicycle配对，不扩展无关矩阵或长测。

本轮只支持全量常驻A路径。位置、scale/rotation、opacity、协方差投影及Raster投影属性精度保持原状；不实现SH降阶、磁盘页codec、LOD分层压缩、流式压缩或Android适配。SH FP16成果记入P02，不重复记为P03完成。

SH3为16组RGB、48个标量：SH逻辑容量192→96 B/高斯；其余源属性52 B/高斯，源属性合计244→148 B/高斯，理论减少39.34%。以上是布局计算，不能替代实际GPU分配或整进程显存测量，也不预先证明FPS提升。

## 2. 实现与开关

| 部分 | 实现与约束 |
| --- | --- |
| 配置 | JSON `shStorageMode=0`为默认FP32，`1`为packed FP16；UI的`SH storage (reload on Apply)`在Apply重载时生效，并显示实际模式 |
| CPU打包 | [SplatSHStorage.h](../../Include/Renderer/Splat/SplatSHStorage.h)提供容量计算、float→half及打包；保持Gaussian-major、系数/RGB顺序，两个binary16合成一个uint32，首标量在低16位 |
| 数值策略 | 最近偶数舍入，保留有符号零和可表示subnormal；FP16请求遇到NaN、Infinity或绝对值大于65504时，在GPU分配和发布可用输出前明确拒绝，不静默钳位 |
| 上传 | [SplatPLYLoader.cpp](../../Source/Renderer/Splat/SplatPLYLoader.cpp)按选定表示计算源属性预算、SH buffer、分块staging与上传字节；全局奇数尾项补零高半字，并处理分块word对齐 |
| Shader | [splat_preprocess.comp](../../Shaders/Splat/splat_preprocess.comp)以`uint shWords[]`读取，FP16用`unpackHalf2x16`还原FP32，FP32用`uintBitsToFloat`；SH求值顺序不变，无FP16算术依赖 |
| ABI/边界 | 48 B Preprocess push constant复用offset 36 padding传模式，CPU静态断言；Pass及Pipeline核对SH模式、阶数、buffer容量与访问范围 |
| 元数据 | 分别记录`shStorageModeRequested`、`shStorageModeEffective`、布局/非法输入策略、SH/source/staging字节及转换/上传成本；实际配置hash包含模式 |

兼容保护：`shStorageMode=1`用于非A、fullLeaf或projectedFusion时明确拒绝；模型加载再次检查，融合管线也拒绝压缩输入。旧loader调用、FSplatGPUData与LOD/Streaming pool默认FP32。上述路径仍使用原FP32逻辑，不声称已接入压缩。全局SH默认仍0，Raster全局默认仍0；本次Bicycle两边显式使用Raster模式1。

## 3. 必要验证与证据状态

变更前完成全项目增量Release构建及393/393个显式计数检查，另controls退出0；既有P01/P05修改被保存为基线。基线报告保留在本地验证目录。按用户后续“简化测试”要求，变更后只构建受影响目标、执行必要SH专项检查和一组Bicycle对照，没有重复全量393项或长测。

| 变更后检查 | 真实结果与证据 |
| --- | --- |
| Release构建 | `cmake --build <candidate/build> --config Release --target SplatLODTestsGPU MonsterEngine -- /m:4`，exit 0；生产Preprocess SPIR-V重新构建 |
| 转换/布局/边界 | 5项SH专项中的host部分通过：有符号零、subnormal、最大值、tie最近偶数、非法/越界拒绝、原子打包、低高半字顺序、奇数尾项和空输入 |
| 实际Vulkan | 同一5/5专项覆盖生产Preprocess FP32→FP16→FP32、SH3/SH0尾部、活动索引、实际分配/输入容量保护与不支持路径；validation未匹配错误 |
| 工具修复 | 首轮review发现timing/capture有意不同配置hash被误判为全局不一致（HIGH）；仅修复Python analyzer，改为每种运行内核对hash，仍全局核对相机/选择/模型/EXE/shader等；2/2专项重新验证通过 |
| 最终源码review | 第二轮APPROVE，无剩余发现；复用未变更C++/shader的成功构建和5/5实际GPU结果，不重复构建 |
| 生产对比 | 一组计时＋独立validation图像对，每次进程exit 0、metadata RECORDED；最终analyzer PASS，完整日志错误扫描无匹配 |

专项测试、修复、review和最终回归报告保留在本地验证目录。变更后7项为5项保留SH检查＋2项工具检查，不冒称再次执行393项。

## 4. Bicycle单组协议与画质门槛

同一最终Release EXE与SPIR-V先测FP32后测FP16，再分别进行validation开启的图像捕获。固定Bicycle PLY `resources/point_cloud/bicycle/point_cloud_30000.ply`，复用既有`scene.mgs`和`resources/3dgs_sequences/bicycle_lod_mid_far.json`。A全量、1920×1080、固定相机t=0、冻结选择、SH3、batch64=1、tight2、radix32、VSync及fusion关闭。每条件预热3秒、采样8秒、1轮；图像单独settle 3秒。容量和预算保持一致；各配对只改SH存储模式。

采样记录有效帧数、Preprocess/Raster/WholeFrame与帧间隔的均值/中位/p95，FPS=`1000/平均帧间隔ms`。WholeFrame包含各Pass，不能再与Pass相加。核对源码快照、EXE、SPIR-V、模型/manifest/sequence、相机、选择、GPU/驱动与配置身份。比较配置hash不包含部分P01/P05开关，因此工具还独立核对固定开关和其余配置。活跃高斯数量必须一致；既有tight2微小排序条目波动记录范围，不强制逐帧条目相等。

**在实测前预登记工程画质门槛：normalized linear RGB MAE≤1/255、PSNR≥40 dB、最大绝对误差≤16/255，并且目视无损坏。** 这是本轮量化实验的工程选择，不是用户给出的容差；不得看过结果后放宽。比较GPU读回的linear RGBA8_UNORM，分别报告RGB/RGBA/alpha的MAE、RMSE、PSNR、最大差、变化通道数和变化像素数。该读回无法证明量化前浮点结果无误差，FP16也不要求逐字节一致。

## 5. 性能与实际内存对比

下表全部来自本轮同一最终Release的真实短配对。FP32→FP16按此顺序执行；每边3秒预热＋8秒采样、1轮。仅SH存储精度改变。

| 指标 | FP32（优化前） | FP16（优化后） | FP16变化 |
| --- | ---: | ---: | ---: |
| Preprocess GPU均值/ms | 3.921297 | 3.782959 | -3.528% |
| Raster GPU均值/ms | 6.944470 | 6.913571 | -0.445% |
| WholeFrame GPU均值/ms | 25.663272 | 26.196041 | +2.076% |
| 平均帧间隔/ms | 26.554976 | 26.865511 | +1.169% |
| FPS（1000/平均帧间隔） | 37.6577 | 37.2224 | -1.156% |
| 有效采样帧数 | 301 | 298 | 无缺失或迟到GPU样本 |
| 原生标题栏Render FPS | 38 | 36 | 另行截图的约1秒短窗整数 |

| GPU/帧间隔统计/ms | FP32 median / p95 | FP16 median / p95 |
| --- | ---: | ---: |
| Preprocess | 3.524960 / 5.842976 | 3.145216 / 5.855861 |
| Raster | 6.511136 / 8.601600 | 6.516416 / 7.985648 |
| WholeFrame | 25.009024 / 29.497920 | 25.925552 / 28.665997 |
| FrameInterval | 26.126200 / 30.415600 | 26.662250 / 29.232000 |

| 实际内存/上传指标（B） | FP32 | FP16 |
| --- | ---: | ---: |
| SH逻辑 | 1,177,335,168 | 588,667,584 |
| SH尾padding | 0 | 0 |
| 全部源属性逻辑 | 1,496,196,776 | 907,529,192 |
| SH实际reserved | 1,177,335,168 | 588,667,584 |
| SH实际backing | 1,177,335,168 | 588,667,584 |
| SH queried requirement | 1,177,335,168 | 588,667,584 |
| SH queried alignment | 16 | 16 |
| 全部源属性实际reserved | 1,496,196,784 | 907,529,200 |
| 全部源属性实际backing | 1,496,196,784 | 907,529,200 |
| 初始上传payload | 1,496,196,776 | 907,529,192 |
| 初始staging逻辑 | 33,554,392 | 33,554,412 |
| 初始staging对齐 | 16 | 16 |
| 初始staging峰值backing | 33,554,400 | 33,554,416 |
| engine pooled reserved范围 | 0–0 | 0–0 |
| engine pooled used范围 | 0–0 | 0–0 |
| engine dedicated committed范围 | 3,564,195,024–3,564,195,024 | 2,975,527,440–2,975,527,440 |
| 驱动/进程总VRAM | 不可得 | 不可得 |

SH实际backing节约 **588,667,584 B（50%）**；全源实际backing节约 **588,667,584 B＝561.40 MiB（39.3443%）**。全源逻辑244→148 B/高斯，实际源分配比逻辑量多8 B；这不等同于整个驱动进程显存降低39.34%。排序、投影和Raster临时数据表示未改变，实测`temporary_bytes`两边均2,067,998,240 B、output image均8,847,360 B。engine allocator口径仅覆盖引擎分配，不是驱动free VRAM。

| CPU启动成本/ms | FP32 | FP16 |
| --- | ---: | ---: |
| SH转换（验证＋打包） | 0.0000 | 39459.7962 |
| 初始上传（create/stage/copy/wait，包含打包） | 892.6173 | 37127.2120 |

**FP16本轮CPU转换存在明显约39.5秒成本**。SH转换计时包含预验证与上传内打包，初始上传包含打包但不含PLY解析和预验证，两者重叠，不能相加或据此声称纯上传带宽改善。独立GPU上传耗时不可得。这是保留可选模式、默认继续FP32的重要原因；本轮没有额外扩展CPU打包性能优化。

本组Preprocess均值下降，但WholeFrame GPU和实际帧间隔未改善；不能把小幅短测变化解释为稳定加速或退化。单组无置信区间；cache condition=unknown。设备NVIDIA GeForce RTX 3060，driver raw 2500395008，CPU Intel64 Family 6 Model 167 Stepping 1，RAM59,957,436,416 B；IMMEDIATE present、VSync关闭。WholeFrame排除present/compositor延迟，各GPU scope重叠，不相加。

活动高斯两边范围[6131954, 6131954] / [6131954, 6131954]；排序条目范围[4349760, 4349768] / [4349760, 4349767]，保留既有tight2微小波动。最终EXE `e729b8d7def9196dcbae1e6ebc21fe594dc0e462faeb716dbf870ee84bd8d3aa`；Preprocess SPIR-V `7c786e85e2773bb08ec67c38b20431b523de2c6d4187922c6c1bdf60183d4833`。

耐久原始证据、部署清单、源码hash、CSV、原始RGBA和应用日志保留在本地验证目录，未随仓库发布；仓库内保留本报告、公开README、复现工具和必要展示图片。原始证据没有覆盖或改写。

## 6. 图像差异与完整FPS窗口截图

| 通道 | MAE | RMSE | PSNR/dB | 最大绝对误差 | 变化通道数 |
| --- | ---: | ---: | ---: | ---: | ---: |
| RGB | 1.745188816e-05 | 0.0002616080601 | 71.646978 | 0.003921568627 | 27,684 |
| RGBA | 1.308891612e-05 | 0.0002265592259 | 72.896365 | 0.003921568627 | 27,684 |
| alpha | 0 | 0 | ∞ | 0 | 0 |

变化RGB像素27,518/2,073,600，变化RGBA像素27,518。预登记数值门槛 **PASS**；目视两张原始GPU读回与完整实际窗口，场景、轮廓与细节一致，未见破碎、错色或其他可见损坏。上述误差来自linear RGBA8_UNORM读回，不代表内部FP32中间结果完全一致。

**优化前：SH FP32，原生标题Render FPS 38。**

![SH FP32 Bicycle完整实际窗口与实时FPS](./GPUResidentCompression/images/bicycle-sh32-window-fps.jpg)

**优化后：SH FP16存储、FP32计算，原生标题Render FPS 36。**

![SH FP16 Bicycle完整实际窗口与实时FPS](./GPUResidentCompression/images/bicycle-sh16-window-fps.jpg)

两张原始JPEG由窗口采集工具直接取得并原样保存；包含完整原生边框、标题栏和渲染画面，没有裁剪、合成或改写FPS。`Render FPS`来自Application.cpp约1秒窗口的整数帧率，与第5节8秒配对FPS分开。

展示会话沿用同一部署/模型/相机/精度配置，仅把sampleSeconds延长到300秒以留出取图时间。截图后向各实际窗口发送Escape，因此展示采集未完成完整采样周期，如实记录且不计入成功计时样本。

目视验收已完成；截图尺寸为工具返回的962×570逻辑窗口像素，渲染/读回仍为1920×1080。

GPU原始线性读回的无损PNG（仅为观看将GPU原始行序垂直翻转；数值分析仍使用未变换RGBA字节）：[FP32](./GPUResidentCompression/images/bicycle-sh32-linear.png)、[FP16](./GPUResidentCompression/images/bicycle-sh16-linear.png)；[RGB绝对差×32可视化](./GPUResidentCompression/images/bicycle-difference-x32.png)只用于放大误差，数值来自未放大原始字节。

## 7. 复现方式

先构建并隔离部署最终Release及对应生产SPIR-V，在仓库根目录执行[launcher与analyzer说明](./GPUResidentCompression/tools/README.md)，替换真实EXE/shader/output路径：

```powershell
$exe = "<Release-executable>"
$shaders = "<compiled-shader-directory>"
$out = "<new-output-directory>"
& ./devDocument/3DGS/GPUResidentCompression/tools/run_comparison.ps1 -Executable $exe -ShaderDirectory $shaders -OutputRoot $out
python ./devDocument/3DGS/GPUResidentCompression/tools/analyze_comparison.py "$out/run-manifest.json" --output "<summary.json>"
```

复制[示例配置](./GPUResidentCompression/tools/comparison-config.example.json)到本地并填写资源路径；不要把包含本机路径的配置提交。OutputRoot必须是新目录，失败和超时保留原始记录；不能覆盖后把失败标成成功。另用窗口采集工具捕获两个完整窗口，记录截图及标题FPS，再作目视判定。若生产代码/shader在证据后修复，必须使用新身份重跑受影响证据。

## 8. 采用决定与后续

**P02第一轮全量A功能、必要检查、单组Bicycle内存/计时/图像/完整FPS窗口证据已完成；保留FP16为可选模式，默认继续FP32。** 本轮达到实际源属性backing减少39.34%、SH减半的目标，FP32解码计算与量化质量门槛通过。但配对FPS 37.6577→37.2224（-1.156%），没有证明整帧收益，且约39.5秒转换开销明显。默认切换和CPU打包性能优化应在后续独立评估，不借容量节约宣称帧率提升。

单组固定视角短配对没有置信区间，不能作持续性能、跨设备或Android结论；驱动/进程总VRAM不可得。LOD/Streaming压缩、投影颜色压缩、P03 SH降阶及分层存储仍待开展，本轮仅提供可复用存储接口，P03不记完成。

最终独立证据核验为PASS：核对源码/构建/部署及证据哈希，独立重算计时CSV，检查文档链接并目视核对两张完整FPS窗口。代码、测试、review、回归和证据门禁均通过；本次提交不包含本机验证目录或部署产物。
