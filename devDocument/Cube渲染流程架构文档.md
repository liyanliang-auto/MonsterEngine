# Cube渲染流程架构文档

## 目录
1. [概述](#1-概述)
2. [代码结构图](#2-代码结构图)
3. [类UML图](#3-类uml图)
4. [渲染流程图](#4-渲染流程图)
5. [命令系统](#5-命令系统)
6. [同步机制](#6-同步机制)
7. [描述符系统](#7-描述符系统)
8. [管线管理](#8-管线管理)

---

## 1. 概述

MonsterEngine的Cube渲染演示展示了完整的Vulkan渲染流程：
- 3D立方体渲染与纹理映射
- MVP矩阵变换与旋转动画
- 深度测试与多纹理采样

### 核心文件

| 文件 | 描述 |
|------|------|
| `CubeApplication.cpp` | 应用程序入口，管理生命周期 |
| `CubeRenderer.cpp/h` | 立方体渲染器，管理渲染资源 |
| `VulkanDevice.cpp/h` | Vulkan设备管理 |
| `VulkanCommandBuffer.cpp/h` | 命令缓冲区管理(Ring Buffer) |
| `VulkanCommandListContext.cpp/h` | 命令列表上下文 |
| `VulkanPendingState.cpp/h` | 待定状态管理 |
| `VulkanPipelineState.cpp/h` | 管线状态管理 |
| `VulkanDescriptorSet.cpp/h` | 描述符集管理 |

---

## 2. 代码结构图

```
┌─────────────────────────────────────────────────────────────────────┐
│                     MonsterEngine 架构层次                           │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │                   Application Layer                          │   │
│  │  ┌─────────────────┐    ┌─────────────────┐                 │   │
│  │  │ CubeApplication │────│   CubeRenderer  │                 │   │
│  │  │   (生命周期)     │    │  (渲染逻辑)     │                 │   │
│  │  └────────┬────────┘    └────────┬────────┘                 │   │
│  └───────────┼──────────────────────┼──────────────────────────┘   │
│              │                      │                              │
│  ┌───────────▼──────────────────────▼──────────────────────────┐   │
│  │                    RHI Interface Layer                       │   │
│  │  ┌───────────────┐  ┌───────────────┐  ┌─────────────────┐  │   │
│  │  │  IRHIDevice   │  │IRHICommandList│  │IRHIPipelineState│  │   │
│  │  └───────┬───────┘  └───────┬───────┘  └────────┬────────┘  │   │
│  └──────────┼──────────────────┼───────────────────┼───────────┘   │
│             │                  │                   │                │
│  ┌──────────▼──────────────────▼───────────────────▼───────────┐   │
│  │                Vulkan Implementation Layer                   │   │
│  │                                                              │   │
│  │  ┌────────────────────────────────────────────────────────┐ │   │
│  │  │                    VulkanDevice                         │ │   │
│  │  │  Instance, PhysicalDevice, LogicalDevice               │ │   │
│  │  │  Swapchain, RenderPass, Framebuffers, SyncObjects      │ │   │
│  │  └──────────────────────────┬─────────────────────────────┘ │   │
│  │                             │                                │   │
│  │  ┌──────────────────────────┼────────────────────────────┐  │   │
│  │  │             Per-Frame Resources                        │  │   │
│  │  │  ┌─────────────────┐  ┌─────────────────┐             │  │   │
│  │  │  │FVulkanCmdBuffer │  │FVulkanPending   │             │  │   │
│  │  │  │Manager (Ring:3) │  │State            │             │  │   │
│  │  │  └────────┬────────┘  └────────┬────────┘             │  │   │
│  │  │           │                    │                       │  │   │
│  │  │  ┌────────▼────────────────────▼────────────────────┐ │  │   │
│  │  │  │        FVulkanCommandListContext                  │ │  │   │
│  │  │  │   (整合命令缓冲、状态、描述符管理)                  │ │  │   │
│  │  │  └──────────────────────────────────────────────────┘ │  │   │
│  │  └───────────────────────────────────────────────────────┘  │   │
│  │                                                              │   │
│  │  ┌────────────────────────────────────────────────────────┐ │   │
│  │  │                   Cache Systems                         │ │   │
│  │  │  PipelineCache | DescriptorSetLayoutCache | RenderPass │ │   │
│  │  └────────────────────────────────────────────────────────┘ │   │
│  └──────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 3. 类UML图

### 3.1 核心类关系

```
                         ┌─────────────────────┐
                         │     Application     │
                         │    «abstract»       │
                         └──────────┬──────────┘
                                    │ extends
                         ┌──────────▼──────────┐
                         │   CubeApplication   │
                         │ -m_cubeRenderer     │◇────┐
                         └─────────────────────┘     │ owns
                                                     │
                         ┌───────────────────────────▼─────────────┐
                         │             CubeRenderer                │
                         ├─────────────────────────────────────────┤
                         │ -m_device: IRHIDevice*                  │
                         │ -m_vertexBuffer: TSharedPtr<IRHIBuffer> │
                         │ -m_uniformBuffer: TSharedPtr<IRHIBuffer>│
                         │ -m_texture1, m_texture2                 │
                         │ -m_pipelineState                        │
                         │ -m_rotationAngle: float32               │
                         ├─────────────────────────────────────────┤
                         │ +initialize(device): bool               │
                         │ +render(cmdList, deltaTime): void       │
                         │ +update(deltaTime): void                │
                         │ -createVertexBuffer(): bool             │
                         │ -createUniformBuffer(): bool            │
                         │ -loadTextures(): bool                   │
                         │ -createPipelineState(): bool            │
                         │ -updateUniformBuffer(): void            │
                         └─────────────────────────────────────────┘
```

### 3.2 命令系统类图

```
┌───────────────────────────────────────────────────────────────┐
│              FVulkanCommandBufferManager                       │
│           (管理命令缓冲区的Ring Buffer)                         │
├───────────────────────────────────────────────────────────────┤
│ -m_commandPool: VkCommandPool                                 │
│ -m_cmdBuffers: TArray<FVulkanCmdBuffer>[3]                   │
│ -m_currentBufferIndex: uint32                                 │
├───────────────────────────────────────────────────────────────┤
│ +getActiveCmdBuffer(): FVulkanCmdBuffer*                      │
│ +submitActiveCmdBuffer(): void                                │
│ +prepareForNewActiveCommandBuffer(): void                     │
└────────────────────────────┬──────────────────────────────────┘
                             │ manages (1:3)
┌────────────────────────────▼──────────────────────────────────┐
│                    FVulkanCmdBuffer                            │
├───────────────────────────────────────────────────────────────┤
│ -m_commandBuffer: VkCommandBuffer                             │
│ -m_fence: VkFence                                             │
│ -m_state: EState {ReadyForBegin,Recording,Ended,Submitted}    │
├───────────────────────────────────────────────────────────────┤
│ +begin(): void                                                │
│ +end(): void                                                  │
│ +markSubmitted(): void                                        │
│ +refreshFenceStatus(): void                                   │
└───────────────────────────────────────────────────────────────┘
```

---

## 4. 渲染流程图

```
 CubeApplication::onRender()
         │
         ▼
 ┌─────────────────────────────────────────────────────────────┐
 │ 1. 获取设备和命令列表                                         │
 │    device = getEngine()->getRHIDevice()                     │
 │    cmdList = device->getImmediateCommandList()              │
 └───────────────────────┬─────────────────────────────────────┘
                         │
                         ▼
 ┌─────────────────────────────────────────────────────────────┐
 │ 2. 准备新帧 (context->prepareForNewFrame())                  │
 │    - 获取下一个交换链图像 (vkAcquireNextImageKHR)            │
 │    - 等待上一帧该槽位完成 (vkWaitForFences)                  │
 │    - 重置命令缓冲 (vkResetCommandBuffer)                    │
 │    - 切换Ring Buffer槽位                                    │
 └───────────────────────┬─────────────────────────────────────┘
                         │
                         ▼
 ┌─────────────────────────────────────────────────────────────┐
 │ 3. 开始命令记录 (cmdList->begin())                          │
 │    - vkBeginCommandBuffer()                                 │
 └───────────────────────┬─────────────────────────────────────┘
                         │
                         ▼
 ┌─────────────────────────────────────────────────────────────┐
 │ 4. 设置渲染目标 (cmdList->setRenderTargets())               │
 │    - 开始渲染通道 (vkCmdBeginRenderPass)                    │
 │    - 设置清除值 (颜色+深度)                                  │
 └───────────────────────┬─────────────────────────────────────┘
                         │
                         ▼
 ┌─────────────────────────────────────────────────────────────┐
 │ 5. CubeRenderer::render()                                   │
 │    5.1 更新Uniform Buffer (MVP矩阵)                         │
 │    5.2 setPipelineState() - 设置管线                        │
 │    5.3 setConstantBuffer(0) - 绑定MVP                       │
 │    5.4 setShaderResource(1,2) - 绑定纹理                    │
 │    5.5 setVertexBuffers() - 绑定顶点                        │
 │    5.6 setViewport/setScissorRect                           │
 │    5.7 draw(36, 0) - 绘制36个顶点                           │
 │        └─► prepareForDraw():                                │
 │            - vkCmdBindPipeline()                            │
 │            - vkCmdSetViewport/Scissor()                     │
 │            - vkCmdBindVertexBuffers()                       │
 │            - updateAndBindDescriptorSets()                  │
 │            - vkCmdDraw(36, 1, 0, 0)                         │
 └───────────────────────┬─────────────────────────────────────┘
                         │
                         ▼
 ┌─────────────────────────────────────────────────────────────┐
 │ 6. 结束渲染通道 (vkCmdEndRenderPass)                        │
 │ 7. 结束命令记录 (vkEndCommandBuffer)                        │
 └───────────────────────┬─────────────────────────────────────┘
                         │
                         ▼
 ┌─────────────────────────────────────────────────────────────┐
 │ 8. 提交并呈现 (device->present())                           │
 │    - vkQueueSubmit (等待imageAvailable, 发信号renderFinished)│
 │    - vkQueuePresentKHR (等待renderFinished)                 │
 └─────────────────────────────────────────────────────────────┘
```

---

## 5. 命令系统

### 5.1 命令队列/缓冲/列表关系

```
┌───────────────────────────────────────────────────────────────────┐
│                          VkQueue                                   │
│                      (Graphics Queue)                              │
│  GPU硬件队列，负责执行提交的命令缓冲                                │
└───────────────────────────────────────┬───────────────────────────┘
                                        ▲ vkQueueSubmit()
┌───────────────────────────────────────┴───────────────────────────┐
│                      VkCommandPool                                 │
│              (FVulkanCommandBufferManager)                         │
│  命令缓冲的分配器，与Graphics队列族绑定                             │
│  ┌─────────────┬─────────────┬─────────────┐                      │
│  │ CmdBuffer 0 │ CmdBuffer 1 │ CmdBuffer 2 │  Ring Buffer (3帧)   │
│  └─────────────┴─────────────┴─────────────┘                      │
└───────────────────────────────────────────────────────────────────┘
                             │
┌────────────────────────────▼──────────────────────────────────────┐
│                    FVulkanCmdBuffer                                │
│  状态机: ReadyForBegin → Recording → Ended → Submitted            │
│  每个命令缓冲关联一个VkFence用于GPU-CPU同步                         │
└───────────────────────────────────────────────────────────────────┘
                             │ wraps
┌────────────────────────────▼──────────────────────────────────────┐
│               FVulkanRHICommandListImmediate                       │
│  RHI层命令列表抽象，提供高级API                                     │
│  通过FVulkanPendingState管理延迟状态绑定                            │
└───────────────────────────────────────────────────────────────────┘
```

### 5.2 Ring Buffer管理

```
Frame Timeline (Triple Buffering):
═══════════════════════════════════════════════════════════════

     Frame 0          Frame 1          Frame 2          Frame 3
  ┌──────────┐     ┌──────────┐     ┌──────────┐     ┌──────────┐
  │ CmdBuf 0 │     │ CmdBuf 1 │     │ CmdBuf 2 │     │ CmdBuf 0 │
  │ Submit   │     │ Submit   │     │ Submit   │     │(WaitFence)│
  └────┬─────┘     └────┬─────┘     └────┬─────┘     └────┬─────┘
       ▼ GPU执行        ▼                ▼                ▼
  │ Fence 0  │     │ Fence 1  │     │ Fence 2  │     │ Fence 0  │
  │ Pending  │     │ Pending  │     │ Pending  │     │ Signaled │

prepareForNewActiveCommandBuffer():
1. currentIndex = (currentIndex + 1) % 3
2. vkWaitForFences(fence) - 等待GPU完成
3. vkResetCommandBuffer() - 重置
4. markAsReadyForBegin()
```

---

## 6. 同步机制

### 6.1 同步原语

| 原语 | 用途 | 场景 |
|------|------|------|
| **Semaphore** | GPU-GPU同步 | 交换链图像获取→渲染→呈现 |
| **Fence** | GPU-CPU同步 | CPU等待GPU完成命令执行 |
| **Pipeline Barrier** | 命令缓冲内同步 | 资源状态/布局转换 |

### 6.2 同步流程

```
 CPU                                              GPU
  │                                                │
  ├─► vkAcquireNextImageKHR ────────────────────→ │
  │   (signal: imageAvailableSemaphore)            │
  │                                                │
  ├─► vkWaitForFences(fence[N%3]) ◄────────────── │
  │   (等待N-2帧GPU完成)           fence signaled   │
  │                                                │
  ├─► Record Commands                              │
  │                                                │
  ├─► vkQueueSubmit ─────────────────────────────► │
  │   wait: imageAvailableSemaphore               GPU执行
  │   signal: renderFinishedSemaphore              │
  │   fence: fence[N%3]                            │
  │                                                │
  └─► vkQueuePresentKHR ─────────────────────────► │
      wait: renderFinishedSemaphore              呈现
```

### 6.3 Image Memory Barrier示例

```cpp
// 纹理上传前: UNDEFINED → TRANSFER_DST_OPTIMAL
VkImageMemoryBarrier barrier{};
barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
barrier.srcAccessMask = 0;
barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

vkCmdPipelineBarrier(cmdBuffer,
    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
    VK_PIPELINE_STAGE_TRANSFER_BIT,
    0, 0, nullptr, 0, nullptr, 1, &barrier);

// 上传后: TRANSFER_DST → SHADER_READ_ONLY
barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

vkCmdPipelineBarrier(cmdBuffer,
    VK_PIPELINE_STAGE_TRANSFER_BIT,
    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
    0, 0, nullptr, 0, nullptr, 1, &barrier);
```

---

## 7. 描述符系统

### 7.1 描述符架构

```
┌─────────────────────────────────────────────────────────────────┐
│              VkDescriptorSetLayout (描述符集布局)                │
│  定义描述符集结构:                                               │
│    binding 0: UNIFORM_BUFFER (Vertex) - MVP矩阵                │
│    binding 1: COMBINED_IMAGE_SAMPLER (Fragment) - 纹理1        │
│    binding 2: COMBINED_IMAGE_SAMPLER (Fragment) - 纹理2        │
│  缓存: FVulkanDescriptorSetLayoutCache (通过哈希避免重复创建)   │
└─────────────────────────────────────────────────────────────────┘
                              │
┌─────────────────────────────▼───────────────────────────────────┐
│                 VkPipelineLayout (管线布局)                      │
│  包含DescriptorSetLayout和可选的PushConstant                    │
└─────────────────────────────────────────────────────────────────┘
                              │
┌─────────────────────────────▼───────────────────────────────────┐
│               VkDescriptorPool (描述符池)                        │
│  FVulkanDescriptorPoolSetContainer配置:                         │
│    maxSets: 1024                                                │
│    UNIFORM_BUFFER: 512, COMBINED_IMAGE_SAMPLER: 512            │
│  每帧重置 (vkResetDescriptorPool)                               │
└─────────────────────────────────────────────────────────────────┘
                              │
┌─────────────────────────────▼───────────────────────────────────┐
│                VkDescriptorSet (描述符集)                        │
│  绑定的资源:                                                     │
│    binding 0: uniformBuffer                                     │
│    binding 1: (imageView1, sampler)                             │
│    binding 2: (imageView2, sampler)                             │
│  缓存: FVulkanDescriptorSetCache (帧内复用)                     │
└─────────────────────────────────────────────────────────────────┘
```

### 7.2 描述符更新流程

```
setConstantBuffer(0, uniformBuffer)    ──┐
setShaderResource(1, texture1)         ──┼─► FVulkanPendingState
setShaderResource(2, texture2)         ──┘    m_descriptorsDirty = true
                                               │
                                               ▼ draw()时触发
                                    prepareForDraw()
                                               │
                              ┌────────────────▼────────────────┐
                              │ updateAndBindDescriptorSets()   │
                              │   1. 构建FVulkanDescriptorSetKey│
                              │   2. 检查Cache是否命中          │
                              │   3. 未命中则分配新的描述符集   │
                              │   4. vkUpdateDescriptorSets()   │
                              │   5. vkCmdBindDescriptorSets()  │
                              └─────────────────────────────────┘
```

---

## 8. 管线管理

### 8.1 管线状态创建

```cpp
// VulkanPipelineState::initialize() 流程
1. createShaderModules()     - 从SPV创建VkShaderModule
2. reflectShaders()          - 反射获取资源绑定信息
3. createPipelineLayout()    - 创建VkDescriptorSetLayout和VkPipelineLayout
4. createGraphicsPipeline()  - 创建VkPipeline

// Cube管线配置:
PipelineStateDesc desc;
desc.vertexShader = vertexShader;
desc.pixelShader = pixelShader;
desc.primitiveTopology = TriangleList;
desc.rasterizerState.cullMode = Back;
desc.depthStencilState.depthEnable = true;
desc.depthStencilState.depthCompareOp = Less;
desc.blendState.blendEnable = false;
desc.renderTargetFormats = {B8G8R8A8_SRGB};
desc.depthStencilFormat = D32_FLOAT;
```

### 8.2 管线缓存优化

```
┌─────────────────────────────────────────────────────────────────┐
│                   VulkanPipelineCache                            │
├─────────────────────────────────────────────────────────────────┤
│ -m_pipelineCache: TMap<hash, TSharedPtr<VulkanPipelineState>>   │
├─────────────────────────────────────────────────────────────────┤
│ +getOrCreatePipelineState(desc): TSharedPtr<VulkanPipelineState>│
│   1. calculateDescHash(desc) - 计算描述哈希                     │
│   2. 查找缓存 m_pipelineCache[hash]                             │
│   3. 命中返回，未命中则创建新管线并缓存                          │
└─────────────────────────────────────────────────────────────────┘

优化策略:
- 基于哈希的快速查找
- 避免重复创建相同配置的管线
- 支持异步编译 (未来扩展)
```

---

## 9. 渲染附件

### 9.1 颜色附件

```
Swapchain Images (交换链图像):
  - 格式: B8G8R8A8_SRGB 或 R8G8B8A8_SRGB
  - 数量: 2-3 (双缓冲/三缓冲)
  - Load Op: CLEAR (清除为深蓝色 0.2, 0.3, 0.3, 1.0)
  - Store Op: STORE (保存渲染结果)
  - Final Layout: PRESENT_SRC_KHR
```

### 9.2 深度模板附件

```
Depth Buffer (深度缓冲):
  - 格式: D32_SFLOAT (优先) 或 D24_UNORM_S8_UINT
  - Load Op: CLEAR (清除为 1.0)
  - Store Op: DONT_CARE
  - Final Layout: DEPTH_STENCIL_ATTACHMENT_OPTIMAL

创建流程 (VulkanDevice::createDepthResources):
  1. 选择深度格式 (findDepthFormat)
  2. 创建VkImage
  3. 分配GPU内存 (DEVICE_LOCAL)
  4. 绑定内存到图像
  5. 创建VkImageView
```

---

## 10. 性能优化策略

| 优化点 | 实现方式 |
|--------|----------|
| **Triple Buffering** | Ring Buffer (3个命令缓冲)，GPU-CPU并行 |
| **描述符集缓存** | FVulkanDescriptorSetCache帧内复用 |
| **管线缓存** | VulkanPipelineCache哈希查找 |
| **布局缓存** | FVulkanDescriptorSetLayoutCache避免重复创建 |
| **RenderPass缓存** | FVulkanRenderPassCache按配置缓存 |
| **延迟状态绑定** | FVulkanPendingState在draw前批量应用 |
| **内存池** | 描述符池每帧重置而非单独释放 |

---

## 参考资料

- UE5 VulkanRHI源码: `Engine/Source/Runtime/VulkanRHI/`
- LearnOpenGL坐标系统: https://learnopengl-cn.github.io/01%20Getting%20started/08%20Coordinate%20Systems/
- Vulkan规范: https://www.khronos.org/registry/vulkan/specs/
