# MonsterEngine Android Vulkan Viewer Specification

版本：1.0。日期：2026-10-17（北京时间）。状态：由已确认技术方案转换的实施规格；尚未实施或验收。

本规格仅保存为本地文档，不创建、更新或发布至issue tracker。本次转换不启动开发流水线，不代表授权执行全部开发阶段。

规范用语：“必须”表示验收要求；“建议”表示可在实现时以等价方案替代的设计选择；“候选/初始配置”必须经实测固化，不代表已验证兼容或性能承诺。

## Problem Statement

### Title

为MonsterEngine交付支持能力诊断、小场景与大场景流式渲染、手机端LOD构建及后台恢复的Android Vulkan Viewer。

MonsterEngine已有Windows Vulkan 3DGS渲染、LOD和Streaming能力，但用户无法将它作为Android应用安装和使用，无法在手机上可靠判断GPU能力、浏览场景、导入大PLY并构建LOD，也缺少覆盖生命周期、资源预算和后台任务的移动端交付方式。

桌面程序的窗口、构建、平台宏和部分依赖绑定Windows；现有离线构建器需要全量源数据及额外中间数组。简单增加JNI入口不能解决ARM编译、Surface销毁、手机内存、后台终止和资源恢复问题。

用户需要复用现有渲染内核，并形成可安装、可诊断、可复现验证的Android应用基础。商业发布必须建立在明确的设备支持范围与证据之上，不能以模拟器能力自报或桌面性能代替真机结论。

## Solution

采用Kotlin应用界面、SurfaceView、JNI命令边界及单一C++渲染owner线程，复用MonsterEngine RHI、Vulkan和3DGS管线。Filament与Khronos提供集成和平台参考，不作为替换渲染内核的运行时依赖。

首版必须同时覆盖四条闭环：内置小场景渲染、预构建LOD资源包流式渲染、手机将大PLY构建为LOD后流式渲染、后台构建被中断后的恢复。小场景是实施先行基线，不能代替其余三条首版要求。

应用在能力不足时仍提供诊断；在可用设备上提供场景导入、相机交互、质量设置、任务进度和诊断导出。后台构建采用受系统限制约束的前台服务与持久化检查点，控制内存并保留所有叶节点高斯。

首版平台基线为Android API26、Vulkan1.2，分别支持arm64-v8a真机与x86_64模拟器。性能与商业支持范围按设备、驱动、场景、质量和持续时间分别验收。

## User Stories

### User Story

作为MonsterEngine用户，我希望在Android上诊断设备、浏览大小3DGS场景、在手机构建LOD并恢复后台长任务，以便完成从资源导入到真机渲染和证据导出的完整流程。

以下故事按编号US01–US40引用；编号是本地规格标识，不对应外部issue。

1. **US01** 作为Android用户，我希望安装与设备ABI匹配的应用，以便直接启动Viewer。
2. **US02** 作为设备所有者，我希望设备不满足渲染要求时仍能查看能力报告，以便了解具体限制。
3. **US03** 作为开发者，我希望报告区分loader、设备、Surface和实际执行结果，以便定位失败阶段。
4. **US04** 作为用户，我希望打开内置小场景，以便在准备自己的数据前确认应用可用。
5. **US05** 作为用户，我希望通过系统文件选择器导入已有3DGS PLY，以便使用本地模型。
6. **US06** 作为用户，我希望无效或不支持的PLY得到清楚提示，以便纠正输入。
7. **US07** 作为用户，我希望大PLY进入显式LOD构建流程，以便完整使用资源而非被静默截断。
8. **US08** 作为用户，我希望构建前知道空间与资源要求，以便决定是否开始。
9. **US09** 作为用户，我希望手机构建LOD时内存受控，以便处理超过运行驻留规模的场景。
10. **US10** 作为数据所有者，我希望所有原始高斯完整保存在叶节点，以便保留完整细节。
11. **US11** 作为用户，我希望构建进度显示真实阶段和处理量，以便判断任务状态。
12. **US12** 作为用户，我希望切换到其他应用时构建仍可继续，以便不必一直停留在Viewer。
13. **US13** 作为用户，我希望通过通知或任务页暂停、取消和返回构建任务，以便控制长任务。
14. **US14** 作为用户，我希望拒绝通知权限后仍能在应用内管理任务，以便不丢失控制入口。
15. **US15** 作为用户，我希望任务被系统终止后可以从有效检查点恢复，以便减少重复工作。
16. **US16** 作为用户，我希望系统时限或资源不足时任务安全停止并解释原因，以便稍后继续。
17. **US17** 作为用户，我希望构建成功的场景出现在资源库并可打开，以便验证构建结果。
18. **US18** 作为用户，我希望导入本地分页资源包，以便复用已有构建成果。
19. **US19** 作为用户，我希望导出手机构建的资源包，以便保存和转移成果。
20. **US20** 作为用户，我希望大场景按视角分页加载，以便总场景规模不受活动高斯上限直接限制。
21. **US21** 作为用户，我希望细节页未就绪时仍看到完整粗层，以便漫游时避免缺块。
22. **US22** 作为用户，我希望坏页或慢I/O得到可解释的降级，以便区分质量下降和应用故障。
23. **US23** 作为用户，我希望单指环绕、双指平移/缩放并重置视角，以便自然浏览场景。
24. **US24** 作为用户，我希望切换场景或取消加载后不再看到旧任务的迟到结果，以便操作结果一致。
25. **US25** 作为用户，我希望旋转、锁屏和切后台后能恢复显示，以便正常使用手机功能。
26. **US26** 作为用户，我希望GPU错误后进行有界恢复并保留手动重试入口，以便避免无限重启。
27. **US27** 作为用户，我希望画质与预算设置能保存并显示有效值，以便理解实际运行质量。
28. **US28** 作为用户，我希望看到内部渲染尺寸与明确口径的帧率，以便判断体验。
29. **US29** 作为用户，我希望内存压力下应用降低占用或安全重载，以便避免失控分配。
30. **US30** 作为用户，我希望失败或取消的导入不会破坏已有场景，以便安心尝试资源。
31. **US31** 作为用户，我希望删除正在使用的资源时应用协调任务与渲染退出，以便不产生文件占用或崩溃。
32. **US32** 作为开发者，我希望导出匹配应用、Shader、场景和配置身份的诊断包，以便复现问题。
33. **US33** 作为数据所有者，我希望诊断默认不包含模型和私人文件路径，以便控制分享范围。
34. **US34** 作为开发者，我希望GPU计时不可用时明确标记，以便不误用CPU耗时。
35. **US35** 作为引擎维护者，我希望Windows与Android共用渲染session，以便修复不会分叉到两套管线。
36. **US36** 作为引擎维护者，我希望Android适配保持原Windows行为，以便继续使用现有验证基础。
37. **US37** 作为开发者，我希望工具链、依赖和Shader可重复构建，以便AI或其他开发者能够复现。
38. **US38** 作为测试者，我希望分别看到模拟器、ARM编译、真机和页大小验证结果，以便准确判断支持程度。
39. **US39** 作为产品负责人，我希望真机持续性能有固定设备和质量条件，以便形成可信目标。
40. **US40** 作为后续AI开发者，我希望工作包、验收标准与证据有明确关联，以便分阶段实施且不遗漏首版大场景能力。

## Implementation Decisions

本节ID为IR01–IR27。模块、类型与协议名称用于约束职责；具体源码位置沿用技术方案导航，在任务分解时核实，本节不固定易过时的文件路径或提供实现代码。

### IR01 — 首版范围与平台门槛

必须实现US01–US38、US40对应的首版行为；US39的具体性能结论须等目标真机和实验配置确定。支持API26起、arm64-v8a与x86_64；Vulkan渲染要求loader和所选physical device分别满足1.2，并满足实际Shader、队列、格式和容量需求。API级别与Vulkan能力分别判断。

现有Int64排序作为首版基线；缺失时保留诊断，不静默启用改变排序精度的路径。48位排序轮数减少不消除shaderInt64需求。双uint32逻辑键兼容路径不在首版实施范围。

### IR02 — 共享核心与应用壳

必须分离共享Core、Vulkan、Splat Runtime、CPU LOD构建、Windows应用壳、Android JNI及Kotlin应用。Android共享库不得依赖GLFW、Win32导入库、桌面OpenGL或ImGui客户端入口。

从现有SplatSceneApplication抽取共享Viewer Session；Windows和Android必须复用同一渲染、相机及LOD调度核心，不能复制整个桌面渲染循环形成独立Android实现。桌面Benchmark身份、ImGui和视频导出留在桌面壳层。先保留生产源清单和桌面基线，再渐进拆分target。

### IR03 — 平台与ARM可编译性

平台识别必须Android优先，统一PLATFORM数值宏并审计错误的存在性判断。PCH不能另行定义矛盾平台。ARM首版使用受控FPU回退，验证公共数学接口；不伪造编译器架构宏或声称缺失的NEON实现已经存在。

必须核对allocator对齐、实际OS页大小、线程/日志/时间/路径和文件大小接口。分桶页与OS页分开；Android默认不请求huge pages。两套现有内存入口及失败清理保持明确所有权。

### IR04 — 可重复构建与依赖锁

必须提交Gradle wrapper及校验信息，固定SDK/NDK/CMake、插件、依赖与libc++策略；Android ABI分别可构建。不得依赖开发机的全局Gradle或绝对SDK路径。

继承PLAN中的候选组合：AGP8.13.2、Gradle8.13、Kotlin2.3.10、compile/target36、min26、NDK30.0.16248370、CMake3.22.1；Gradle JVM候选为JBR21、字节码17。Build Tools在35.0.0与已装36.1.0中验证后固定。候选组合必须在M0通过干净构建后再称为已验证；等价替换需记录原因与兼容证据。

### IR05 — Shader与资源供给

复用现有离线Shader编译，区分host工具与Android目标程序；编译产物必须覆盖间接include依赖并生成身份清单。最终SPIR-V须验证合法性及真实capability、工作组和共享内存需求，不能仅依扩展声明推定要求。

Shader、默认配置和授权小场景通过统一资源供给接口读取，或校验后发布到版本化私有缓存。手机不运行外部Shader编译器，也不依赖桌面生成目录。只启用实际使用的GPU能力。

### IR06 — 能力探针与部分诊断

探针独立于强制Int64的完整渲染设备初始化。必须覆盖native库载入失败、Vulkan loader、instance、逐物理设备能力、队列/内存/格式、Surface present及小型执行smoke。

vkEnumerateInstanceVersion须可选动态查询，缺失按loader1.0处理；低版本可结束并提供部分报告，但不能调用尚未可用的核心入口。必须区分supported、requested、enabled与实际执行通过，不能仅返回单个布尔值。

无Surface时保留待探测状态。设备名称和系统feature列表是辅助信息，不能替代应用查询；模拟器营销名称不得当真机GPU身份。

### IR07 — 能力报告契约

报告必须版本化，包含应用/构建/工作区、ABI与模拟/翻译标记、APK/native/Shader/config身份、loader/instance/device版本、vendor/device/driver、所需/支持/请求/启用能力、limits/queues/formats、Surface结果、smoke结果、失败阶段与VkResult。

状态必须能表达diagnosticsOnly、renderEligible、graphicsSmokePassed、splatSmokePassed和streamingPassed，或语义等价的分层结果；时间戳和预算信息分别有available标记。失败不得抹掉此前已取得的诊断。

### IR08 — 单owner线程与JNI边界

必须由单一native owner线程创建、调用和销毁渲染相关对象。UI Choreographer仅投递轻量frame token，最多保留一个待执行帧请求；MOVE与配置可合并，DOWN/UP/CANCEL必须保序。无Surface时不忙轮询，不叠加第二套HandlerThread帧循环或桌面run循环。

JNI采用可校验句柄及generation；释放幂等，异常在边界转换。不得跨线程保存JNIEnv；JavaVM、AssetManager和回调有明确生命周期。UI不得直接销毁GPU资源，owner不得同步等待UI回调。

构建、页面I/O和渲染分工独立；全局日志/allocator由有效lease保护，Activity销毁不能释放仍被后台服务使用的设施。

### IR09 — Surface状态与销毁契约

只有应用resumed、有效非零Surface、引擎和场景就绪且无致命错误时才渲染。pause、detach、场景释放与device lost是不同状态。

ANativeWindow引用必须配对；generation有效性检查与windowInUse标记在同一同步协议内完成，避免检查后失效。尺寸事件可合并，旧代事件必须丢弃。

surfaceDestroyed返回前必须停止旧窗口使用，完成必要后端回收并回执；仅post消息不算完成，持有引用也不代表许可仍有效。detach/quit优先，长解析/哈希/上传不得堵塞owner队列。等待不持有对端所需锁。

acquire必须有限timeout；TIMEOUT/NOT_READY不产生submission并返回控制队列。驱动永久阻塞需要明确诊断与退出限制，不承诺所有故障均可进程内恢复。

### IR10 — 提交、呈现与资源回收

必须区分未提交、成功提交、present结果及真实completion token；仅实际新提交可通知pipeline与GPU页池。重置fence但未提交的路径不能导致后续永久等待。

首版复用离屏rgba8 Compute输出后以graphics pass呈现，使用正确barrier/layout和颜色空间，不假设swapchain可作storage image。OUT_OF_DATE、SUBOPTIMAL、SURFACE_LOST及DEVICE_LOST必须分别处理。

frame slot资源只有完成后可复用；present等待的binary semaphore按swapchain image或等价、已证明安全的协议管理，普通submit fence不能证明present消费。acquire成功后遇到取消/resize/pause必须合法消费信号并处理image，不能任意early return。

支持并启用swapchain maintenance1时优先使用present fences；否则记录兼容回收策略及其规范局限。resize与永久detach分别验证；未验证关闭安全的设备不能标为商业支持通过。场景资源先于device销毁，正常帧不得用waitIdle掩盖同步问题。

### IR11 — 生命周期与有界恢复

切后台停止渲染，Surface重建尽量保留引擎/场景；内存压力可释放场景，在返回时重载。device lost每个故障事件最多自动重建一次，失败进入诊断和手动重试，不无界循环。

使用scene、surface、residency和slot generation拒绝过期结果；已记录未提交工作必须明确丢弃或安全完成，不得伪造完成token。

### IR12 — Scene Store事务

通过SAF与ContentResolver有界复制输入至私有存储，不把content URI当原生文件路径。仅完整验证并原子发布的场景可见；临时导入、构建任务和已发布资源分别管理。

导入记录内容hash、大小、版本和内部ID。取消/失败只清理本任务临时资源，保留旧场景。删除使用中的资源先协调渲染/任务退出并归还lease；worker只发布descriptor，不直接切换GPU场景。

### IR13 — PLY输入与语义

复用现有ASCII、little-endian及big-endian 3DGS PLY语义，包括位置、scale激活、opacity sigmoid、四元数归一化和SH重排。必须检查属性完整性、连续SH布局、短读、溢出、非法数值和数量/大小准入，保留输入行originalId。

共享有界流式解析与header probe，原CPU全量接口作为兼容collector保留。小场景可在预算内加载并分块上传；大PLY进入显式LOD构建，不静默截断。普通点云PLY不能冒充已支持的高斯输入。

### IR14 — 分页资源包

首版支持本地ZIP容器，建议扩展名.monsterlod；包含版本化外层描述、既有二进制manifest、分页数据和可选预览/构建摘要。外层描述不能把既有二进制场景协议替换为JSON。

解包必须限制实际累计字节、条目与磁盘占用，拒绝越界路径、符号链接、重复规范化路径和不支持的加密/压缩形式。校验manifest、引用范围、page hash及解码有效性后原子发布；不从ZIP直接随机streaming。必须提供生成包和手机产物导出方式。

### IR15 — 真正的LOD/Streaming

必须复用既有动态streaming语义、selector、page streamer和GPU page pool，场景总源数允许超过驻留及active上限；全叶驻留或全部page pinned不能充当流式完成证据。

必须保持父子互斥完整cut、ready祖先兜底、子表示完整ready后替换、真实GPU完成后回收及generation防ABA。transferred CPU payload必须真正释放后才归还预算lease。按帧提交关联active mapping、上传和telemetry。

根pinned表示、source slab、active/sort容量与最小CPU progress工作集必须准入。坏细页保留祖先；根损坏停止打开。I/O重试有界，过期请求可取消，背压不无限等待。

### IR16 — 渲染预算与默认值

首轮基线为内置约3万–10万高斯；内部最长边≤1280且像素≤921600；activeCapacity=100000、sortCapacity=2000000、temporaryGPUBytes=256MiB，可配置。这不是大场景总高斯数上限。

继承初始建议：source GPU128MiB、staging16MiB、CPU cache64MiB、读取1–2线程、上传2MiB/帧并设CPU时间预算、质量2–4px、迟滞0.2。最终值必须按实际backing、range与进度预算验证。页上传可跨帧。

首版分页路径SH FP32，原Raster、radix32及fusion关闭为基线；SH FP16现有边界不得被宣称已覆盖分页。其他优化独立验证。sort溢出不得展示半排序结果或越界；允许保留上一完整图并降低下一帧复杂度，必须报告原因。

### IR17 — 全局预算协调

CPU cache、decoded ready、source/staging/temporary GPU backing、swapchain、metadata及builder须统一计费，移动端不能以“独立显存”掩盖总RAM。

构建与重负载Viewer默认互斥，构建时释放GPU场景或保留轻量界面；并发查看必须重新准入。内存压力依次停止预取、缩减缓存、回收安全页，必要时释放场景；不得突破硬预算换取表面成功。

### IR18 — 手机有界LOD构建

必须新增有界磁盘分块构建流程，不能把旧LoadCPU加旧Build包装成手机大场景支持。复用现有格式与proxy数学，保留每个原始高斯且每条恰好属于一个叶表示。

初始build working-memory预算256MiB、单job和1–2 CPU worker为实施建议；缓冲、索引、排序临时容器、metadata与解压均计费。记录252B磁盘记录与C++内存结构padding的区别，临时序列化必须版本化且跨ABI明确，不直接dump struct。

### IR19 — 构建阶段与退化输入

流程必须包含源复制/hash、有界规范化、磁盘分区、完整叶page输出、bottom-up代理、验证及最终发布。chunk具备记录数、校验和与durable边界；ASCII恢复不得任意seek字节。

大节点稳定划分为最多8个子节点，叶/page上限建议8192；中心相同或浮点退化时按originalId稳定均分并记录策略。深度/协议边界必须明确，不能生成不进展的递归或无界内存。

parent只读已完成child表示；每节点proxy数量有界，建议上限8192并允许更小root。局部归约的最坏临时工作集必须准入，不仅限制最终proxy数。

### IR20 — Proxy、误差与格式兼容

复用现有协方差、面积/opacity、SH和moment matching数学语义；每proxy累计geometry/appearance误差及权重必须持久化到构建sidecar，不能读child Gaussian后将历史误差重置为零。

node RMS误差、coverage、centerBounds与support必须包含后代和代理的必要保守信息，不只保存代理中心范围。新分区/归约算法使用独立builderVersion与完整buildConfig，不宣称与旧桌面代理逐字节相同。

首版新builder输出现有manifest v3/page v1，不生成fusion group；已有manifest版本在原兼容范围和移动端准入下读取。叶集合按originalId比较；代理另做浮点与图像质量验证，几何RMS不能当PSNR保证。

### IR21 — 磁盘准入、检查点与发布

空间估计必须覆盖原始复制、中间记录、分区重叠、最终叶/代理、检查点与可选包；按阶段live-set设安全余量并反复检查可用空间。子输出durable提交之前不得删除唯一父输入。

任务状态覆盖创建、复制、规范化、分区、叶构建、归约、验证、发布和完成，以及暂停/取消/失败/等待资源。显示真实阶段和处理量，不伪造准确ETA。

检查点必须包含source身份、parser/config/builder/schema身份、已完成任务DAG、chunk/page hash和可回滚状态；事务/rename/fsync的顺序必须保证半写文件不可冒充完成。终止后最多重做未提交工作单元；身份不兼容不得盲续。

最终manifest验证并持久化后才原子发布场景。任务metadata同样计入预算，不能通过无限增长的内存索引绕过有界承诺。

### IR22 — 后台长任务服务

首版必须支持用户在可见Activity启动的dataSync前台服务，service不持有Activity/Surface，UI只是持久化job store的观察者。任务可在Viewer暂停后继续；不依赖Activity生存或自动重启。

API26通知channel和startForegroundService启动路径必须及时进入前台，再执行耗时复制/hash/native工作；适配API34所需类型/manifest权限及低版本API调用。通知拒绝不应错误阻止合法FGS，但必须保留应用内管理入口。

Android15+且target≥35时按dataSync后台累计6小时/24小时及同类型共享额度处理，前台交互重置和额度耗尽异常按平台规则处理；超时后迅速停止，不等待长checkpoint或长native join。不从开机广播自动启动，不通过重启服务绕过限额。

native解析、partition和merge必须在有界小步轮询暂停/取消；平时定期提交检查点，不能依赖onDestroy。Task Manager停止、强停、LMK及普通界面销毁分别处理。WorkManager不是无限后台执行替代；有界wakelock/线程资源在暂停、失败或完成时释放。

### IR23 — 用户交互与设置

提供场景列表、内置场景、导入、任务页、Viewer、设置及报告/导出入口。触控包含orbit、双指pan/pinch和重置，处理pointer ID、CANCEL、旋转与viewport映射。

设置持久化并区分请求值与有效值。预算变化如需重建，必须计入旧新资源重叠峰值。一般错误提示可理解，高级诊断保留底层原因。

### IR24 — 遥测和导出

必须关联frame、scene/surface/config generation、actual active/sort、上传/驻留/lease、degraded和计时可用性；异步回读归属原始帧，不错配当前帧。

CPU帧间隔、GPU区间、提交/显示节奏与视频编码FPS口径分开；嵌套GPU区间不能直接相加。截图读取内部离屏输出完成后再worker编码，标明分辨率/颜色空间，UI截图另计。

诊断必须包含能力、有效配置、构建和资源身份、帧/streaming记录及任务报告；默认不含原始场景和私有路径，用户主动分享。不可用信息如实缺省并说明，不伪造测量。

### IR25 — 后续性能与发布阶段

Swappy、动态质量/温控调优和持续功耗在M3独立测量；不同时启动两套帧节奏控制。真机30FPS是绑定具体配置的目标，不是本规格对未知设备的保证。

M4覆盖签名、AAB/渠道包、升级兼容、兼容矩阵和依赖/素材授权。首版已完成不自动等于商业发布已通过；正式发布前按所选渠道和当时系统版本核对要求。

### IR26 — 兼容验证与工作区保护

必须保留原Windows渲染与正式验证接口，维护用户已有未提交修改。Android不得使用桌面导入库或未打包资源；4KiB与16KiB的库、包和运行验证独立记录。

Filament DEFAULT后端sample、系统Vulkan JSON、模拟器自报名称和ARM编译成功均不能替代MonsterEngine真实Vulkan执行或ARM真机性能。RenderDoc仅作为有真实捕获与匹配环境时的辅助工具。

### IR27 — 实施边界与完成声明

实施分M0–M2首版、M3优化、M4发布，工作包与原PLAN J00–J12保持关联。每阶段必须记录实际命令、退出码、身份和证据；本地文档转换不初始化pipeline状态或外部issue。

尚无设备的验收标记Pending/Not Run，不能以WARN或模拟器结果写为真机PASS。所有阶段不允许用小场景成功替代手机构建、后台恢复和真实大场景分页。

## Testing Decisions

### 验证入口与已有基础

测试优先通过可观察的用户流程、共享Viewer Session及CPU构建任务接口。对内部算法只建立必要的数据/协议oracle，不新增只匹配类名、代码文本或私有调用顺序的测试。

复用现有正式CPU/GPU/integration验证、PLY/场景格式验证、稳定排序oracle、完成token/页池生命周期fixture、相机/配置控制测试及GPU读回。Android新增的高层入口主要是应用驱动流程与后台job生命周期；二者均观察同一持久化场景库，避免每层另造mock接口。

允许受控故障注入验证低能力、设备丢失、坏页、慢I/O、提交失败、取消和进程中断；它们不能取代真实GPU图像、真实系统生命周期和真机性能。桌面先验证新CPU builder，再在ARM上核对叶数据与构建契约。

PLAN已经确认以下验收边界，本次转换直接沿用，不重新访谈。所有AC01–AC17保留原编号；下表给出可执行的通过条件与证据方向，不宣称测试已运行。

### 验收标准与追踪矩阵

| AC | 场景、操作和预期结果 | 最低证据 | IR / US |
| --- | --- | --- | --- |
| AC01 | 从干净源码和锁定依赖构建arm64-v8a、x86_64产物；缺工具/Shader必须明确失败，不复用旧APK冒充成功 | 工具链锁、日志/退出码、APK/native/SPV/config hash | IR03–05；US01/37 |
| AC02 | loader/native载入失败、低版本、缺Int64、无Surface时仍显示正确的部分报告；支持设备完成逐级probe，支持/启用/执行不混淆 | 原生运行报告＋有界故障注入，失败阶段可查 | IR01/06/07；US02/03/38 |
| AC03 | 基础图形及完整3DGS Compute链得到有效图像；相机、方向、颜色正确，非空壳或固定贴图 | 模拟器真实运行、GPU读回、同配置Windows参考、必要真机补测 | IR02/05/08/10/23；US04/23 |
| AC04 | 合法PLY可导入；坏属性/SH/数值/截断/数量溢出、预算不足与取消均有确定结果，旧资源不受损 | CPU fixtures＋Android SAF流程、有效/失败导入记录 | IR12/13/17；US05–08/30 |
| AC05 | Surface重建、旋转、零尺寸、锁屏和前后台回归重复20次；无旧代访问、死锁或重复释放；后台不继续渲染 | 生命周期日志、资源计数、同步/validation证据；resize和永久detach分测 | IR08–11；US25/29 |
| AC06 | 快速切换20次并穿插加载取消；旧worker结果不会进入新场景；CPU lease与GPU token正确退休 | 场景generation、pin/lease/实际提交trace | IR08/11/12/15；US24/31 |
| AC07 | 源场景显著大于驻留/active容量，漫游触发真实读取/上传/驱逐；始终满足完整cut和ancestor兜底 | 预算配置、source/active/resident计数、流式曲线与覆盖断言 | IR15–17；US20/21 |
| AC08 | 坏细页/坏根页、慢I/O、背压和sort溢出分别触发；安全降级或明确停止，不越界、不展示半排序图或永久缺块 | 故障fixture、降级原因、图像与容量日志 | IR10/15–17；US22/29 |
| AC09 | 新builder产物合法，originalId叶集合不丢不重，激活属性与共享parser一致；累计proxy误差不重置，保守边界正确 | CPU oracle、ValidateManifest/DecodePage、叶对齐与多档图像实验 | IR13/18–21；US09/10/17 |
| AC10 | 10万、100万、约613万输入逐级测量；构建工作集不因N增长无界增加，磁盘不足安全失败/恢复，不误发布场景 | 受控working set、RSS、磁盘live-set和job report；无法运行的规模明确Not Run | IR17–21；US08/09/16 |
| AC11 | 用户启动后切后台任务继续；可暂停/取消；通知拒绝仍可应用内控制；进程终止及系统超时后可从有效检查点由合法入口恢复 | API26/34/35/36相关覆盖，目标系统真机，关键提交故障点/Task Manager/强停/LMK记录 | IR08/17/21/22；US11–16 |
| AC12 | 本地资源包打包、导入、手机构建产物导出能往返；越界路径、重复条目、坏hash及过大解压被拒绝，旧场景保持可用 | 合法/异常包fixtures、事务发布和内容一致性记录 | IR12/14/21；US18/19/30 |
| AC13 | device lost故障事件最多自动重建一次；失败提供诊断和手动重试，无无限自动重启 | 故障注入计数与状态记录；真实驱动故障是否经历单列 | IR10/11；US26 |
| AC14 | 导出的报告/截图/有效配置/CSV可关联同一构建、场景与原始帧；不可用指标明确；默认不带模型或私有路径 | schema/hash/frame关联检查及导出包检查 | IR07/23/24；US27/28/32–34 |
| AC15 | 共享核心抽取与Android适配后，原Windows运行及对应正式CPU/GPU/integration检查无新增退化 | 匹配源码的实际构建/回归与图像证据，不复制旧测试数字 | IR02/03/13/26；US35/36 |
| AC16 | 所有native依赖与APK布局满足4KiB/16KiB要求，在16KiB环境完成原生加载和核心运行验证 | ELF/ZIP检查及实际16KiB运行结果；4KiB MuMu单列 | IR04/26；US01/38 |
| AC17 | 在确定的真机、驱动、场景、相机/质量/分辨率和热状态下运行30分钟，记录稳定性与持续帧率；30FPS目标据实作结论 | 完整配置、逐帧/内存/热状态可得信息、持续运行报告 | IR16/17/24/25/27；US28/39 |

### 数据、门限与证据规则

- 内置tiny fixture先检查确定性数据、排序语义及完整管线，再做真实场景。新builder使用同一算法跨Windows/ARM比较；旧桌面builder代理只作质量参考，不作逐字节oracle。
- 图像比较固定场景/相机/内部尺寸/颜色空间及配置。M2图像实验前必须预注册numeric门限和比较方法；当前PLAN未给具体值，本次转换不自行发明PSNR或最大误差承诺。
- builder256MiB是初始受控工作集预算建议，不等同整个进程RSS上限；必须同时报告RSS和Java/native/GPU/metadata等计费范围，防止通过漏记分配“通过”。根pinned与最小进度预算必须真实可用。
- 性能先使用匹配配置实测，不把几百万条构建承诺为固定秒数。目标手机、总RAM、磁盘支持规模和持续帧率门限在相应实验前冻结，未冻结不能给出该项PASS。
- 模拟器功能、两ABI构建、ARM真机、4KiB/16KiB及正式性能分别标记。GPU时间戳不可用不妨碍明确报告CPU帧数据，但不可替代GPU Pass证据。
- 测试优先可观察行为及资源不变量；长测按阶段和配置变化执行，不在每次局部修改后重复完整规模矩阵。

### 阶段完成定义

M0必须有可重复构建与两个ABI诊断APK；M1必须通过基础图形和Surface同步回归；M2必须合并通过小场景、预构建包流式、手机构建产物流式和后台恢复四条首版闭环。AC不因阶段命名而被自动降级或免验。

尚无真机时可交付明确标记的模拟器阶段产物，但真机部分、AC17以及未执行的16KiB环境检查保持Pending；不得将“模拟器交付”描述为完整真机或商业发布验收通过。M3/M4具有独立完成报告。

## Out of Scope

- 账号、登录、支付、云场景管理、HTTP/网络流式下载及服务端建设。
- SPZ或任意点云格式支持、普通PLY自动转高斯、训练、剪枝和蒸馏。
- Android GLES回退、第二图形后端、NEON性能优化和完整移动编辑器。
- 首版双uint32排序兼容路径、降低深度精度以绕过Int64门槛。
- 将既有SH FP16支持扩展到LOD/Streaming、默认开启fusion或叠加多项未经验证的优化。
- 与旧桌面LOD代理逐字节相同、所有GPU逐像素相同、无限输入规模或固定手机构建耗时的保证。
- 绕过Android后台启动/运行时限、无限保活或无人交互的自动开机续建。
- 本次文档转换中的代码实现、SDK补装、APK测试、流水线启动、自动提交和任何issue tracker操作。

**明确不属于排除项：**首版手机大PLY构建、大场景LOD/Streaming、后台长任务及检查点恢复。它们必须按本规格实施，不能在任务分解时移到首版之外。

## Further Notes

### 来源与优先级

主要来源为[Android技术方案](Android_Vulkan适配_PLAN.md)，版本1.0；本次转换输入SHA256为`12dc176bdcc7cbaaed0909ac0ef7d1c87e5fb372e177253ab9aeeed1160d117c`。辅助入口为[CODE_WIKI](../../CODE_WIKI.md)、[AGENTS](../../AGENTS.md)、[环境核查](Android_Environment_Audit.md)和[Filament参考核查](Android_Filament_Reference_Audit.md)。

本规格将已确认决策规范化，未改变PLAN的首版范围。当前工作区含用户修改，实施前须检查实际源码、生产清单与基线，不能仅凭历史HEAD认定构建身份。若发现方案和源码冲突，记录冲突及解决决策，不静默删减用户需求。源码导航与示例路径保留在PLAN/知识库中，不在实现决策重复固定。

### 工作包追踪

下列本地工作包保留PLAN J00–J12依赖；不是外部issue或已执行的调度状态。

| 包 | 依赖 | 交付行为 | 验收关联 |
| --- | --- | --- | --- |
| J00 | 无 | 工作区/生产目标/Windows基线与工具链候选验证 | AC01/15的基线 |
| J01 | J00 | 跨平台宏、ARM数学/allocator与共享核心可编译 | AC01/15/16 |
| J02 | J00 | Gradle应用、JNI诊断与独立probe | AC01/02/14；M0 |
| J03 | J01/J02 | Android Surface、Vulkan生命周期和提交结果 | AC03/05/13；M1 |
| J04 | J01 | 显式共享库、Shader供给和共享session | AC01/03/15 |
| J05 | J03/J04 | 小场景完整渲染、触摸、设置、读回和遥测 | AC03/05/14；M2a |
| J06 | J04 | 事务Scene Store、SAF、资源包生成/导入/导出 | AC04/12 |
| J07 | J05/J06 | 有界大场景LOD/Streaming与背压 | AC06/07/08；M2b |
| J08 | J01/J06 | 有界PLY/代理数学、新LOD builder与检查点 | AC04/09/10 |
| J09 | J02/J08 | FGS通知/任务管理、超时及进程恢复 | AC11；M2c |
| J10 | J07/J09 | 手机构建到原子发布、流式打开的完整首版 | AC01–16按平台逐项核验；M2 |
| J11 | J10 | 帧节奏、真机调参和持续性能 | AC17；M3 |
| J12 | J10 | 商业发布候选、签名/包/升级/兼容矩阵 | M4发布专属证据及相关AC复核 |

J04先通过Windows共享session回归；J08先通过纯CPU新builder与小数据oracle，再和Android服务整合。实施分解须给每个AC指定实际测试/证据责任，保留共享文件所有权和集成验证，不把并行编码完成等同全部验收完成。

### 待按阶段冻结的环境参数

目标真机型号/驱动/内存、最终总RAM预算、构建支持的磁盘/数量规模、图像数字门限、持续性能有效配置及发布签名/渠道身份尚未给出。这些是实验和发布前的显式输入，不能编造，也不阻碍M0工具链与探针准备。

### 本次转换检查

规格应核对PLAN D01–D12、J00–J12及AC01–AC17完整覆盖；格式和追踪检查不代表Android实现或测试完成。to-spec技能默认的issue发布动作已按用户要求替换为本地文档输出，不需要issue标签、tracker设置或额外发布权限。
