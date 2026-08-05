# 3DGS Splat Pass 代码 Review 报告

> 分支：`feature_3dgs_vulkan_splat_pass`　|　审查范围：`Source/Renderer/Splat`、`Source/SplatSceneApplication.cpp`、`Shaders/Splat`、`RHI/Vulkan` 改动、开发文档
> 审查方式：静态代码 + Shader 阅读（未修改任何代码）
> 结论：**功能框架完整、RHI 打通，但存在若干可导致渲染结果错误或崩溃的确定性缺陷，当前代码大概率无法产出正确图像。**

---

## 一、🔴 严重缺陷（必须修复才能正确渲染）

### 1. 相机 UBO 从未被写入 —— 全部 Gaussian 被视锥剔除 → 黑屏
- **位置**：`Source/Renderer/Splat/SplatPass.cpp:276-302`（`FSplatPreprocessPass::updateCamera`）；调用链 `SplatSceneApplication.cpp:177 → SplatPipeline.cpp:140 → updateCamera`
- **问题**：`updateCamera()` 收到 `FCameraUniforms& camera` 后，**只把空的 `m_cameraBuffer` 重新绑到 descriptor set，从不把 camera 数据拷进 buffer**（函数内有 `TODO: Replace with persistent buffer + map/unmap`，承认未完成）。`m_cameraBuffer` 在 `SplatPass.cpp:71-84` 创建，但全代码库无任何 `Map/memcpy/initialData` 写入（已 grep 确认）。
- **后果**：preprocess shader 读到的 `viewMatrix/projMatrix` 全为 0 → `inFrustum()` 中 `pView.z(=0) >= -nearPlane` 成立 → **所有 Gaussian 被剔除** → 无 sort 记录 → 黑屏。即便关闭剔除，也是全 0 矩阵投影到原点的乱图。
- **修复**：在 `updateCamera` 中把 camera 数据写入 `m_cameraBuffer`（`Map` 后 `memcpy` 到 `m_cameraBuffer` 的 mapped 指针，或创建时带 `initialData`）。注意 `FCameraUniforms` 是 176 字节（见缺陷 13），`updateUniformBuffer` 的 size 要对齐到 176。

### 2. Radix Sort 只跑 4 轮，但排序键是 64 位 —— tile 未被分组，per-tile 渲染彻底错乱
- **位置**：`Source/Renderer/Splat/SplatSortPass.cpp:548`（`for (pass = 0; pass < 4; pass++)`，`shift = pass*8`）；键定义 `Shaders/Splat/Sort/splat_assign_keys.comp:85` `key = (tileIndex << 32) | depthBits`
- **问题**：LSD 基数排序每轮排 8 位。键是 `uint64`，tileID 在高位（bits 32+），depth 在低位（bits 0-31）。只排 4 轮 = 只排了低 32 位（depth），**高位 tileID 从未参与排序**。LSD 最后一轮决定主序，因此结果按 depth 排序、而非按 (tile, depth)。
- **后果**：`splat_tile_boundaries.comp` 按 `(key>>32)` 即 tileID 分组，但数组并未按 tile 聚集 → `tileRanges[tile]` 完全错误 → Render Pass 把不属于该 tile 的 Gaussian 混入、漏掉应属本 tile 的 Gaussian → 图像严重错乱（这是确定性错误，与硬件无关）。
- **修复**：改为 **8 轮**（shift 0,8,…,56）。8 轮后 LSD 主序为高位 tileID，得到正确的 (tile, depth) 排序。当前 ping-pong 逻辑（偶→奇→…→8 轮仍落 even）与 `m_finalInEven=true` 兼容，只需把 `pass < 4` 改成 `pass < 8`。

### 3. SH 颜色：shader 中 `shBase` 硬编码为 0 —— 所有 Gaussian 用 0 号的颜色
- **位置**：`Shaders/Splat/splat_preprocess.comp:104-106`（`int shBase = 0; // idx * coeffCount (0 for single-gaussian...)`）以及 `:109-202` 的 `shCoefficients[(shBase + k) * 3 + c]`
- **问题**：注释自己承认本应是 `idx * coeffCount`，但写死成 0。SH buffer 布局是每个 Gaussian 连续存 `coeffCount*3` 个 float（见 loader），因此除 Gaussian 0 外，其余全部读到 Gaussian 0 的系数。
- **后果**：整张图除 0 号 Gaussian 外颜色全错（全部复用 0 号 Gaussian 的 SH 颜色）。
- **修复**：把 `shBase` 作为 `idx * getSHCoeffCount(shDegree)`（buffer stride 实际固定为 16 系数=48 float/高斯，见缺陷 4）传入，或在 `main()` 中计算 `int shBase = int(idx) * 16;` 并传给 `computeColorFromSH`。

### 4. PLY 加载器 SH 重排越界 + 偏移错误 —— 颜色错 + 越界读
- **位置**：`Source/Renderer/Splat/SplatPLYLoader.cpp:183-188`
- **问题**：
  - **越界**：`vs.f_rest[(j - 1) + SH_N * 2 + 1]`，`SH_N=16`，`j` 最大 15 → 索引 `(15-1)+32+1 = 47`；而 `f_rest` 是 `float32[45]`（见 `VertexStorage`，行 95）。读 `f_rest[47]` 越界（虽在 struct 内相邻的 opacity/scale 字段，属 UB + 读到错误数据）。
  - **偏移错**：按 INRIA 3DGS PLY 标准，`f_rest` 通道分组布局为 `[r0..14, g0..14, b0..14]`。正确的重排应为 `f_rest[(j-1)]`（R）、`f_rest[(j-1)+15]`（G）、`f_rest[(j-1)+30]`（B）。代码用了 `+3 / +18 / +33`，整体偏移 +3，把第 j 个系数错读成第 j+1 个。
- **后果**：SH 系数张冠李戴，颜色失真；高 j 越界读到 opacity/scale 字段。
- **修复**：改为 `shData.Add(vs.f_rest[(j-1)]); shData.Add(vs.f_rest[(j-1)+15]); shData.Add(vs.f_rest[(j-1)+30]);`（假设 PLY 是 INRIA 通道分组格式；若模型是 gsplat 交错格式则另需对应公式）。同时确认 loader 实际 buffer stride（当前总是 48 float/高斯，与 `shDegree` 检测无关，见下条）。

### 5. `m_maxSortElements` 保守上界（gaussianCount×256）—— 显存爆炸 + 垃圾条目污染 + 潜在越界
- **位置**：`SplatPipeline.h:62`（默认 `maxTilesPerGaussian = 256`）；`SplatPipeline.cpp:65` `m_maxSortElements = gaussianCount * maxTilesPerGaussian`；排序 buffer 在 `SplatSortPass.cpp:295-296, 473-479` 按 `maxSortElements` 分配；`SplatPipeline.cpp:172` 直接以 `m_maxSortElements` 作为真实 `totalSortElements` 驱动后续所有 Pass。
- **问题（设计缺陷，源于文档错误 6 的妥协）**：
  - **显存**：bonsai（1,157,141 高斯）×256 = 约 2.96 亿条目 → keys 2.4 GB + values 1.2 GB，纯属浪费。
  - **垃圾条目**：AssignKeys 只写 `totalTiles`（真实值）条，其余 `maxSortElements - totalTiles` 条是 buffer 创建时的未定义/0 内容。Radix Sort 与 TileBoundaries 却按 `maxSortElements` 处理这些垃圾 → tile 分组被污染（与缺陷 2 叠加）。
  - **越界风险**：若任一 Gaussian 覆盖的 tile 数 > 256，`prefixSum[g-1] + tilesTouched[g]` 会超出 `maxSortElements` → AssignKeys 写出 buffer 边界 → GPU 崩溃。大半径 Gaussian 很容易超过 256 tile（80×45 网格下整屏 Gaussian 可覆盖数千 tile）。
- **修复**：恢复文档错误 6 原本的意图——用 staging buffer 读回真实的 `totalTiles`（注意要用正确的同步方式：单独 submit + fence，或 `vkCmdCopyBuffer` 到 readback 后再 `waitForIdle` 读，**不要在同一 command buffer 录制中途 waitForIdle**）。然后按真实 `totalTiles` 分配/处理排序 buffer，并以真实值驱动 Radix/TileBoundaries。

---

## 二、🟠 中等缺陷（强烈建议修复）

### 6. Scatter Shader `sums[lsID]` 越界读共享内存
- **位置**：`Shaders/Splat/Sort/splat_radix_scatter.comp:106` `subgroupBroadcast(subgroupExclusiveAdd(sums[lsID]), sID);`
- **问题**：`shared uint sums[RADIX_SORT_BINS / SUBGROUP_SIZE]` = `sums[8]`（256/32），而 `lsID = gl_SubgroupInvocationID` 范围 0..31。索引 `sums[8..31]` **越界**。应为 `sums[sID]`（gl_SubgroupID，范围 0..7）。这是参考 `VkRadixSort` 转录错误。
- **后果**：共享内存越界读（robustness/debug 下会被校验层捕获），且全局前缀和 `sums_prefix_sum` 计算错误 → scatter 写入位置错 → 排序结果错。建议与 `VkRadixSort` 原版逐行比对 scatter shader。

### 7. Scatter Shader 多余的 `atomicAdd(global_offsets[binID], count)`
- **位置**：`Shaders/Splat/Sort/splat_radix_scatter.comp:171-173`
- **问题**：`global_offsets[binID]` 在第 108 行已由直方图前缀和算好，**每个 workgroup 的绝对起始偏移**已正确（含 `local_histogram`）。此处又 `atomicAdd` 累加，会破坏该偏移；且多 workgroup 并发读 `global_offsets` 存在竞争/不一致。
- **后果**：与参考实现不符，scatter 位置错乱。建议删除该 `atomicAdd`（参考实现仅用前缀和算偏移，不做运行时累加）。

### 8. Subgroup 特性未真正启用
- **位置**：`Source/Platform/Vulkan/VulkanDevice.cpp:1112-1133`
- **问题**：`vulkan11Features{}` 全零初始化，仅 `pNext = &vulkan12Features`，未把 `shaderSubgroup / shaderSubgroupArithmetic / shaderSubgroupBallot` 置 `VK_TRUE`。64 位原子特性（1.2）已开，但 **subgroup 算术/投票特性（1.1）未开启**。Scatter/Histogram shader 用了 `subgroupAdd / subgroupExclusiveAdd / subgroupBroadcast / subgroupElect`。
- **后果**：严格驱动下 3DGS 的 radix compute pipeline 创建失败或校验报错。建议显式置 `vulkan11Features.shaderSubgroup = VK_TRUE; shaderSubgroupArithmetic = VK_TRUE; shaderSubgroupBallot = VK_TRUE;`。

### 9. 描述符集在录制中更新（error 8 同类隐患）
- **位置**：`SplatSortPass.cpp:343-349`（AssignKeys）、`:696-710`（TileBoundaries）、`SplatRenderPass.cpp:233-242`（Render）。三者每帧 `m_currentDsIndex = (... +1) % kMaxFramesInFlight`（=2）后**录制中** `updateStorageBuffer`。
- **问题**：文档错误 8 已指出“已绑定的 descriptor set 在录制中被更新会让 command buffer 失效”。RadixSort 用了预烘焙（永不更新）的 even/odd 双份集避开了该问题，但上述三个 Pass 仍是“切换索引 + 录制中更新”。只要引擎实际在飞帧数 > `kMaxFramesInFlight(2)`，就会再次触发该 VUID。
- **后果**：取决于引擎帧延迟，可能偶发 command buffer 失效/校验报错。建议与 RadixSort 一致：把输入 buffer 引用**预烘焙**进多份 descriptor set，录制中只 `bindDescriptorSet` 不 `update`。

### 10. `bindDescriptorSets` 取用的 command buffer 与 dispatch 可能不一致
- **位置**：`VulkanRHICommandList.cpp:1326-1328` 用 `getCommandBufferManager()->getActiveCmdBuffer()`；而 `setPipelineState`（:141）、`dispatch`（:1233）用 `getCmdBuffer()`。
- **问题**：若两者返回不同 handle，compute 的 descriptor set 会绑到另一条 CB 上，dispatch 看不到绑定 → 校验报错/渲染异常。
- **后果**：潜在“描述符未绑定”类问题。建议统一取 CB 的接口（确认 `getCmdBuffer()` 与 `getActiveCmdBuffer()` 等价）。

### 11. Preprocess 近/远裁剪面硬编码
- **位置**：`SplatPass.cpp:340-341`（`nearPlane=0.01f; farPlane=1000.0f`），与 `inFrustum`（`splat_common.glsl:87`）耦合。
- **问题**：far 写死 1000，若场景深度超过则被裁；且未从相机参数推导。当前 app 的 near=0.01 一致，但 far 与 `initCamera` 中的 `PerspectiveNearClipPlane=0.01` 无关联。
- **后果**：大场景可能误裁。建议从 `FCameraUniforms` 传入真实 near/far。

### 12. AssignKeys binding 3（TilesTouched）声明但未绑定
- **位置**：shader `splat_assign_keys.comp:32` 声明 `binding = 3`（TilesTouched，只读未使用）；C++ `SplatSortPass.cpp:265-276` 的 descriptor layout **不含 binding 3**（enum `EAssignKeysBinding::TilesTouched=3` 已定义但没 `Add` 进 layout）。
- **问题**：shader 未实际引用该 binding，通常编译器会优化掉，但属于 layout 与 shader 不一致的代码异味；若将来启用该绑定会立即触发校验 VUID。
- **后果**：当前大概率无害，但脆弱。建议要么 shader 删除该 binding，要么 C++ 补上 `Add`（并真正喂入 `preOut.tilesTouched`）。

---

## 三、🟡 轻微 / 代码质量 / 文档不符

### 13. `FCameraUniforms` 注释写 “160 bytes” 实为 176
- `SplatTypes.h:22` 注释 “std140 layout, 160 bytes”，但 `:39` `static_assert(sizeof == 176)`。注释错误（static_assert 已保护，不影响运行）。

### 14. 开发文档与代码不符
- 文档称 `FSplatPLYLoader::loadFromFile()`，实际接口是 `loadAndUpload()`（`SplatPLYLoader.h:72`）。
- 文档 8.1 流程注释 “draw(3) → 绘制 2 个三角形”，实际 `draw(3)` 是 1 个全屏三角形（正确，注释错）。
- 文档 2.1 / 9.4 称 radix 为 “4-pass/8-bit LSD 按 tile 再按 depth 排序”，但 4 轮无法对 64 位键按 tile 排序（见缺陷 2），文档描述与实际行为矛盾。

### 15. 分支混入了无关文件（范围污染）
- `git diff --stat main..feature_3dgs_vulkan_splat_pass` 包含 `.trae/skills/source-code-interpreter/SKILL.md`、`.workbuddy/wind-monster-peoject-rule.md`、`wind-monster-peoject-rule.md` 等，与 3DGS 功能无关。建议从本分支剔除以保持 PR 干净。

### 16. `resourceBarrier()` 需确认覆盖 storage buffer 的 SHADER_WRITE→SHADER_READ
- 全链路大量依赖无参 `cmdList->resourceBarrier()` 在 compute dispatch 之间做同步（如 `SplatPipeline.cpp:176-219`）。若该函数未对 storage buffer 施加 `VK_ACCESS_SHADER_WRITE_BIT → VK_ACCESS_SHADER_READ_BIT`（compute stage），会出现 RAW 竞争 → 结果不确定。建议确认其实现覆盖 compute storage 访问。

---

## 四、修复优先级建议

| 优先级 | 条目 | 理由 |
|--------|------|------|
| P0 | #1 相机 UBO | 不改则必然黑屏 |
| P0 | #2 Radix 4→8 轮 | 不改则 tile 分组完全错乱 |
| P0 | #3 SH shBase=0、#4 loader SH 越界 | 不改则颜色全错/越界 |
| P0 | #5 maxSortElements + 真实读回 | 不改则显存爆炸 + 垃圾污染 + 越界风险 |
| P1 | #6/#7 Scatter shader | 排序正确性核心，需对照参考逐行核对 |
| P1 | #8 Subgroup 特性 | 否则严格驱动 pipeline 创建失败 |
| P1 | #9/#10 描述符/CB 一致性 | 帧延迟大时偶发失效 |
| P2 | #11~#15 | 健壮性 / 文档 / 分支卫生 |

> 注：开发文档「验证状态」称“全链路正确执行、与 CPU 基线匹配”，但上述确定性缺陷（尤其 #1 黑屏、#2 tile 错乱、#3 颜色错）与“正确图像”相矛盾。建议重新用 CPU 基线（`Source/Renderer/Splat/CPU/SplatCPU.cpp`）逐像素对比验证，很可能当前实际输出为黑屏或错图。

---

## 五、运行时验证错误与修复记录（两轮修复）

> 在对上述 16 个问题中的 10 个进行代码修复后，运行程序出现以下 Vulkan Validation Layer 报错。以下记录每轮遇到的错误、根因分析和修复方案。

---

### 第一轮：初始化阶段与首帧渲染报错

#### 错误 1：`VUID-vkMapMemory-memory-00678` — 内存双重映射

- **现象**：PLY 加载时 `vkMapMemory()` 报 "memory has already be mapped"。
- **根因**：`FVulkanStagingBuffer::Map()`（`VulkanBuffer.cpp:445-461`）直接调用 `vkMapMemory`，但内存池（`FVulkanMemoryManager`）在初始化时已对该 `VkDeviceMemory` 做了持久映射（`vkMapMemory(…, &PersistentMappedPtr)`）。Vulkan 规范禁止在同一 `VkDeviceMemory` 上重复调用 `vkMapMemory`。
- **触发路径**：`VulkanBuffer::uploadInitialData()` -> 创建临时 `FVulkanStagingBuffer` -> `Map()` -> `vkMapMemory`（重复）。
- **修复**：`Map()` 中增加检查：若 `m_usesMemoryManager && m_allocation.MappedPointer` 为真，表示池已持久映射，直接返回 `m_allocation.MappedPointer`，不再调用 `vkMapMemory`。
- **文件**：`Source/Platform/Vulkan/VulkanBuffer.cpp`

#### 错误 2：`copyBuffer: Invalid buffer`（x2）

- **现象**：`SplatPipeline::ensureSortPassesInitialized()` 中两次 `copyBuffer` 报 "Invalid buffer — must be VulkanBuffer"。
- **根因**：`ensureSortPassesInitialized()` 原实现在调用 `copyBuffer(m_prefixSum.getBufferA(), …)` 和 `copyBuffer(m_stagingBuffer, m_prefixSum.getResultBuffer(), …)` **之后**才调用 `lazyInitSortPasses()` 初始化 prefixSum/assignKeys/radixSort。因此 `copyBuffer` 时 `m_prefixSum` 的 buffer 尚未创建（内部 `IRHIBuffer` 为空指针），`dynamic_cast<VulkanBuffer*>` 失败。
- **修复**：将 `lazyInitSortPasses()` 调用移到 `ensureSortPassesInitialized()` 开头，确保所有 sort pass buffer 在 `copyBuffer` 之前已创建。
- **文件**：`Source/Renderer/Splat/SplatPipeline.cpp`

#### 错误 3：`VUID-vkCmdDraw-None-08114` — FPresentPass 描述符集从未更新

- **现象**：`vkCmdDraw` 时 descriptor set 的 `splatOutput`（Set 0, Binding 0）从未被 `vkUpdateDescriptorSets` 更新。
- **根因**：`FPresentPass` 的描述符集在 `initPresentPass()` 中预分配，但未调用 `updateCombinedTextureSampler` 做初始更新（因为此时还没有 splat 输出纹理）。同时在 `prepareForNewFrame()` -> `FVulkanDescriptorPoolSetContainer::reset()` 中，每帧会重置该 pool，使得预分配的描述符集句柄失效。因此首帧绑定的 descriptor set 是一个空壳。
- **修复**：改为每帧在 `onRender()` 中重新调用 `m_device->allocateDescriptorSet()` 分配全新的描述符集，分配后立即 `updateCombinedTextureSampler` 再绑定。分配失败时提前退出并记录错误日志，防止静默跳过。
- **文件**：`Source/SplatSceneApplication.cpp`

#### 错误 4：`InvalidImageLayout` — 期望 GENERAL 实际 SHADER_READ_ONLY_OPTIMAL

- **现象**：compute shader 通过 `imageStore` 写入 splat 输出纹理，需要 `VK_IMAGE_LAYOUT_GENERAL`，但实际布局为 `SHADER_READ_ONLY_OPTIMAL`。
- **根因**：跨帧布局持久化问题——帧 N 结束时输出纹理被转为 `SHADER_READ_ONLY_OPTIMAL`（供 present pass 采样），帧 N+1 开始时应转回 `GENERAL`。Step 0 的 `transitionResource(ShaderResource->UnorderedAccess)` 本应处理此事，但首帧时纹理初始布局是 `UNDEFINED`（非 `SHADER_READ_ONLY_OPTIMAL`），barrier 的 `oldLayout` 与实际不匹配。
- **修复**：增加 `m_firstFrame` 标志：首帧使用 `None->UnorderedAccess`（映射为 `UNDEFINED->GENERAL`），后续帧使用 `ShaderResource->UnorderedAccess`。
- **文件**：`Source/SplatSceneApplication.cpp`、`Include/SplatSceneApplication.h`

#### 错误 5：`VUID-vkUpdateDescriptorSets-None-03047`（x4）— CB 挂起期间更新描述符集

- **现象**：4 次 `vkUpdateDescriptorSets` 调用时目标 descriptor set 正被一个处于 pending 状态的 `VkCommandBuffer` 引用。
- **根因**：
  - **3 次来自 `FSplatPreprocessPass::execute()`**：OUTPUT 描述符集（Set 1）的 7 个 `updateStorageBuffer` 调用发生在 `cmdList->begin()` 之后（CB 录制期间），且集可能被上一帧的 pending CB 引用。
  - **1 次来自 `FSplatPreprocessPass::updateCamera()`**：Camera UBO 绑定（Set 0, Binding 5）对所有 `kMaxFramesInFlight` 个描述符集做更新，其中部分集正被上一帧的 pending CB 引用。
- **修复**：
  - OUTPUT 描述符集：在 `allocateAndUpdateDescriptorSets()` 初始化时预烘焙全部 7 个 binding，`execute()` 中移除 `updateStorageBuffer` 调用。
  - Camera UBO 绑定：`updateCamera()` 改为只更新**下一个帧索引** (`(m_currentDsIndex+1) % kMaxFramesInFlight`) 的 descriptor set，不再更新所有帧的集。`execute()` 中移除 Camera UBO 更新。
- **文件**：`Source/Renderer/Splat/SplatPass.cpp`

---

### 第二轮：修复首轮错误后残存的报错

> 第一轮修复引入了 1 个新错误（VUID-00689），且仍有 3 个错误未完全消除。根因均指向 `ensureSortPassesInitialized()` 中的 **`cmdList->end()` + `cmdList->begin()` 模式**，该模式会对命令缓冲区产生副作用。

#### 第二轮-错误 1：`VUID-vkUnmapMemory-memory-00689` — 从未映射的内存被 Unmap

- **现象**：`vkUnmapMemory()` 报 "Unmapping Memory without memory being mapped"。
- **根因**：第一轮对 `Map()` 的修复使其在池已持久映射时直接复用 `m_allocation.MappedPointer`，**不再调用 `vkMapMemory`**。但 `Unmap()` 未做对应修改，仍无条件调用 `vkUnmapMemory`，导致从未 map 过的内存被 unmap。
- **修复**：`Unmap()` 中也增加检查：若 `m_usesMemoryManager && m_allocation.MappedPointer`，只清空 `m_mappedData`，跳过 `vkUnmapMemory`。
- **文件**：`Source/Platform/Vulkan/VulkanBuffer.cpp`

#### 第二轮-错误 2：`InvalidImageLayout`（仍然存在）

- **现象**：首帧仍然报 layout 不匹配，输出纹理在 compute shader 写入时处于 layout 错误状态。
- **深层根因**：`ensureSortPassesInitialized()` 内部流程：

  ```
  1. 录制 preprocess + copy + prefixSum + copyToStaging 命令
  2. cmdList->end()          -> vkEndCommandBuffer(CB)  停止录制
  3. m_device->waitForIdle() -> vkDeviceWaitIdle        无已提交工作，立即返回
  4. cmdList->begin()        -> vkBeginCommandBuffer(CB) 完全重置CB，步骤1的所有命令丢失
  ```

  - **`vkBeginCommandBuffer` 会完全重置命令缓冲区**，`end()` 之后通过 `begin()` 重新开始录制时，之前录制的所有命令（包括 `onRender()` Step 0 的布局转换 `UNDEFINED->GENERAL`）全部丢失。
  - 此外，步骤 2 的 `end()` 只调用了 `vkEndCommandBuffer`（停止录制），**并未 `vkQueueSubmit`**。步骤 3 的 `waitForIdle()` 调用 `vkDeviceWaitIdle`，但因为没有已提交的 GPU 工作，该函数立即返回。因此 staging buffer 读回的 `m_realSortElements` 是**未初始化的垃圾数据**，意味着 sort/render pass 使用错误的元素数量运行。

- **修复**：在 `FSplatPipeline::execute()` 中 `ensureSortPassesInitialized()` 返回后，立即对输出纹理重新应用 `None->UnorderedAccess` 布局转换（因为 CB 刚被重置，所有之前录制的转换均已丢失）。
- **文件**：`Source/Renderer/Splat/SplatPipeline.cpp`

#### 第二轮-错误 3：`VUID-vkUpdateDescriptorSets-None-03047`（x3，从 4 减到 3）

- **现象**：3 次 `vkUpdateDescriptorSets` 违规（dstBinding=0），均指向同一 `VkCommandBuffer`。
- **根因**：第一轮修复消除了 OUTPUT 描述符集的 7 次 update（减至 0），但 `execute()` 中仍对当前帧的 Camera UBO binding 做 `updateUniformBuffer`（在 `cmdList->begin()` 之后）。由于 `ensureSortPassesInitialized()` 在第一帧会额外调用一次 `begin()`（重置 CB），Camera UBO 的 descriptor update 处于 CB 录制期间，且该 CB 在此次 reset 前曾被使用过。
- **修复**：
  - 将 `SplatSceneApplication::onRender()` 中的 `buildCameraUniforms()` + `m_splatPipeline->setCamera()` **移到 `cmdList->begin()` 之前**。`setCamera()` 内部的 `updateCamera()` 会调用 `vkUpdateDescriptorSets` 预烘焙 camera UBO 到下一个帧的 descriptor set，此时 CB 尚未开始录制，避免 VUID-03047。
  - `FSplatPreprocessPass::updateCamera()` 改为只更新 `(m_currentDsIndex + 1) % kMaxFramesInFlight` 的 descriptor set（而非所有帧）。
  - 移除 `execute()` 中残留的 Camera UBO `updateUniformBuffer` 调用。
- **文件**：`Source/SplatSceneApplication.cpp`、`Source/Renderer/Splat/SplatPass.cpp`

#### 第二轮-错误 4：`VUID-vkCmdDraw-None-08114`（x2，仍然存在）

- **现象**：两个不同的 descriptor set（不同 VkDescriptorSet handle）均在 `vkCmdDraw` 时被报告 "never been updated"。
- **根因分析**（推测，因 `ensureSortPassesInitialized` 的 CB 重置仍可能是诱因）：
  - `ensureSortPassesInitialized()` 在第一帧重置 CB 后，render/sort 等 pass 的描述符集更新和绑定都发生在 CB 录制期间。若 `lazyInitSortPasses()` 分配了大量描述符集导致池空间紧张，后续 `onRender()` 中的 `allocateDescriptorSet()` 可能返回无效 set。
  - 此外 CB 重置后部分 pass 的描述符状态可能未正确绑定。
- **修复**：`onRender()` 中分配 FPresentPass 描述符集时，先将结果保存到临时变量；若分配失败（返回 null），则提前退出并记录错误日志，防止 `nullptr` 覆盖 `m_present.descriptorSets[]` 中的有效集。
- **文件**：`Source/SplatSceneApplication.cpp`

---

### 总结：`ensureSortPassesInitialized()` 的设计缺陷

以上第二轮残留错误共同指向 `ensureSortPassesInitialized()` 的根本设计问题：

```cpp
// SplatPipeline.cpp — 当前有缺陷的实现
cmdList->end();           // <- vkEndCommandBuffer，但从未 vkQueueSubmit
m_device->waitForIdle();  // <- vkDeviceWaitIdle，无已提交工作，瞬间返回
                          // <- 此处读回的 staging buffer 数据是垃圾！
cmdList->begin();         // <- 重置 CB，丢失所有之前录制的命令
```

**理想修复方案**（尚未实施，建议后续改进）：

1. `ensureSortPassesInitialized()` 应在 `cmdList->end()` 后通过 `vkQueueSubmit` + `vkQueueWaitIdle`（或 fence）真正提交并等待 GPU 完成。
2. 然后调用 `cmdList->begin()` 获取**全新的命令缓冲区**开始后续录制，而非在同一 CB 上 reset。
3. 或者：放弃首帧 readback 方案，直接用 `m_maxSortElements` 作为 upper bound（牺牲内存换取简洁性），消除整个 `ensureSortPassesInitialized` 路径。
