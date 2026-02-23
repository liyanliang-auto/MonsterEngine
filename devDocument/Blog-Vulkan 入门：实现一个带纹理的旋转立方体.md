# Vulkan 入门：实现一个带纹理的旋转立方体

**效果：**

![](https://liyanliangpublic.oss-cn-hongkong.aliyuncs.com/img/202512061607330.gif)

## 1. 概述

本文介绍如何使用 **Vulkan API** 实现一个带纹理的旋转立方体渲染，从零开始构建一个完整的 3D 渲染流程。

## 2. 代码结构图

项目采用分层架构，将应用逻辑、RHI 抽象层和 Vulkan 实现分离，便于维护和扩展。

![deepseek_mermaid_20251206_d33b62](https://liyanliangpublic.oss-cn-hongkong.aliyuncs.com/img/202512061608835.png)

## 3. 类UML图

### 3.1 核心类关系

`CubeApplication` 管理应用生命周期，`CubeRenderer` 负责资源创建和渲染逻辑。两者通过 RHI 接口与底层 Vulkan 实现解耦。

![deepseek_mermaid_20251206_bd716f](https://liyanliangpublic.oss-cn-hongkong.aliyuncs.com/img/202512061605114.png)

```cpp
// CubeRenderer 初始化流程
bool CubeRenderer::initialize(RHI::IRHIDevice* device) {
    createVertexBuffer();    // 36 顶点数据
    createUniformBuffer();   // MVP 矩阵
    loadTextures();          // 2 张纹理
    createShaders();         // 顶点/片段着色器
    createPipelineState();   // 渲染管线
}
```

### 3.2 命令系统类图

命令系统采用三层封装：`IRHICommandList`（RHI 接口）→ `FVulkanRHICommandListImmediate`（Vulkan 实现）→ `FVulkanCmdBuffer`（原生封装）。

![deepseek_mermaid_20251206_07a4ca](https://liyanliangpublic.oss-cn-hongkong.aliyuncs.com/img/202512061611213.png)



## 4. 渲染流程图

每帧渲染遵循固定流程：准备帧 → 录制命令 → 提交执行 → 呈现。`CubeRenderer::render()` 在命令录制阶段被调用。

![deepseek_mermaid_20251206_d03f4f](https://liyanliangpublic.oss-cn-hongkong.aliyuncs.com/img/202512061613467.png)

```cpp
// 每帧渲染流程 (CubeApplication::onRender)
void onRender() {
    context->prepareForNewFrame();  // 切换命令缓冲区，获取交换链图像
    cmdList->begin();               // 开始录制命令
    cmdList->setRenderTargets(...); // 设置渲染目标（触发 BeginRenderPass）
    m_cubeRenderer->render(cmdList, deltaTime);  // 绑定资源并绘制
    cmdList->endRenderPass();       // 结束渲染通道
    cmdList->end();                 // 结束录制
    device->present();              // 提交并呈现
}
```

## 5. 命令系统

### 5.1 命令队列/缓冲/列表关系

Vulkan 命令系统分为三层：**Queue**（执行入口）→ **CommandBuffer**（命令容器）→ **CommandPool**（内存分配）。MonsterEngine 通过 `FVulkanCmdBuffer` 封装原生命令缓冲区。

![deepseek_mermaid_20251206_c85718](https://liyanliangpublic.oss-cn-hongkong.aliyuncs.com/img/202512061616542.png)

```cpp
// 命令录制与提交
vkBeginCommandBuffer(cmdBuffer, &beginInfo);
    vkCmdBeginRenderPass(...);
    vkCmdBindPipeline(...);
    vkCmdBindDescriptorSets(...);
    vkCmdDraw(36, 1, 0, 0);  // 绘制立方体
    vkCmdEndRenderPass(...);
vkEndCommandBuffer(cmdBuffer);
vkQueueSubmit(queue, 1, &submitInfo, fence);
```

### 5.2 Ring Buffer管理

使用 **3 个命令缓冲区轮换**（Triple Buffering），实现 CPU 录制与 GPU 执行并行，避免帧间等待。

![deepseek_mermaid_20251206_e06afd](https://liyanliangpublic.oss-cn-hongkong.aliyuncs.com/img/202512061619258.png)

命令缓冲区状态机：`ReadyForBegin` → `Recording` → `Ended` → `Submitted` → 等待 Fence → 重置 → `ReadyForBegin`

![deepseek_mermaid_20251206_7c7345](https://liyanliangpublic.oss-cn-hongkong.aliyuncs.com/img/202512061619055.png)



## 6. 同步机制

Vulkan 是显式同步 API，需要开发者手动管理 CPU-GPU 和 GPU-GPU 之间的同步。

### 6.1 同步原语

| 原语 | 用途 | 场景 |
|------|------|------|
| **Semaphore** | GPU-GPU同步 | 交换链图像获取→渲染→呈现 |
| **Fence** | GPU-CPU同步 | CPU等待GPU完成命令执行 |
| **Pipeline Barrier** | 命令缓冲内同步 | 资源状态/布局转换 |

```cpp
// Fence 使用示例：等待命令执行完成后重用命令缓冲区
vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
vkResetFences(device, 1, &fence);
vkResetCommandBuffer(cmdBuffer, 0);

// Semaphore 使用示例：帧同步
vkAcquireNextImageKHR(..., imageAvailableSemaphore, ...);  // 获取图像
vkQueueSubmit(..., waitSemaphore=imageAvailable, signalSemaphore=renderFinished, ...);
vkQueuePresentKHR(..., waitSemaphore=renderFinished, ...);  // 呈现
```

### 6.2 同步流程

帧渲染的同步时序：`AcquireImage`（信号 imageAvailable）→ `QueueSubmit`（等待 imageAvailable，信号 renderFinished）→ `Present`（等待 renderFinished）

![deepseek_mermaid_20251206_0fd82e](https://liyanliangpublic.oss-cn-hongkong.aliyuncs.com/img/202512061636386.png)



### 6.3 Image Memory Barrier示例

**Pipeline Barrier** 用于在命令缓冲区内同步资源状态。纹理上传需要两次布局转换：`UNDEFINED` → `TRANSFER_DST`（准备接收数据）→ `SHADER_READ_ONLY`（准备被着色器采样）。

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

## 7. 描述符系统

描述符（Descriptor）是 Vulkan 中将资源（Buffer、Texture）绑定到着色器的机制。立方体渲染使用 3 个绑定点：1 个 Uniform Buffer + 2 个纹理采样器。

### 7.1 描述符架构

描述符系统分为四层：`DescriptorSetLayout`（模板）→ `PipelineLayout`（管线布局）→ `DescriptorPool`（内存池）→ `DescriptorSet`（实例）。

![deepseek_mermaid_20251206_4dd7fc](https://liyanliangpublic.oss-cn-hongkong.aliyuncs.com/img/202512061639601.png)

```glsl
// Cube.vert - 顶点着色器
layout(set = 0, binding = 0) uniform UBO {
    mat4 model;       // 模型矩阵（旋转）
    mat4 view;        // 视图矩阵（相机）
    mat4 projection;  // 投影矩阵（透视）
} ubo;

// Cube.frag - 片段着色器
layout(set = 0, binding = 1) uniform sampler2D texture1;  // container.jpg
layout(set = 0, binding = 2) uniform sampler2D texture2;  // awesomeface.png
```

### 7.2 描述符更新流程

`FVulkanPendingState` 收集资源绑定，在 `draw()` 时通过 `updateAndBindDescriptorSets()` 统一更新和绑定描述符集。

![deepseek_mermaid_20251206_b6926f](https://liyanliangpublic.oss-cn-hongkong.aliyuncs.com/img/202512061647249.png)

```cpp
// CubeRenderer::render() 中的资源绑定
cmdList->setConstantBuffer(0, m_uniformBuffer);  // binding 0: MVP 矩阵
cmdList->setShaderResource(1, m_texture1);       // binding 1: container.jpg
cmdList->setShaderResource(2, m_texture2);       // binding 2: awesomeface.png
// draw() 时自动触发 vkUpdateDescriptorSets + vkCmdBindDescriptorSets
```



## 8. 管线管理

图形管线（Graphics Pipeline）定义了从顶点数据到最终像素的完整处理流程。Vulkan 管线是不可变的，需要预先创建。

### 8.1 管线状态创建

管线创建分为四步：着色器模块 → 着色器反射 → 管线布局 → 图形管线。

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

使用 `VkPipelineCache` 缓存管线编译结果，避免重复编译。MonsterEngine 通过哈希查找实现管线复用。

![deepseek_mermaid_20251206_8eb342](https://liyanliangpublic.oss-cn-hongkong.aliyuncs.com/img/202512061650661.png)

## 9. 渲染附件

渲染通道（RenderPass）定义了渲染目标的格式和操作。立方体渲染使用 1 个颜色附件 + 1 个深度附件。

### 9.1 颜色附件

交换链图像作为颜色附件，每帧清除为深蓝色背景。

```cpp
// 颜色附件配置
VkAttachmentDescription colorAttachment{};
colorAttachment.format = VK_FORMAT_B8G8R8A8_SRGB;
colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;    // 清除
colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;  // 保存
colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

// 清除颜色
VkClearValue clearColor = {{{0.2f, 0.3f, 0.3f, 1.0f}}};  // 深蓝色
```

### 9.2 深度模板附件

深度缓冲用于 Z-Buffer 深度测试，确保近处物体遮挡远处物体。

```cpp
// 深度附件配置
VkAttachmentDescription depthAttachment{};
depthAttachment.format = VK_FORMAT_D32_SFLOAT;
depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;      // 清除为 1.0
depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; // 不需要保存
depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

// 清除深度
VkClearValue clearDepth = {.depthStencil = {1.0f, 0}};
```

## 10. 性能优化策略

MonsterEngine 采用多种优化策略，减少 CPU 开销和 GPU 等待时间。

| 优化点 | 实现方式 | 效果 |
|--------|----------|------|
| **Triple Buffering** | Ring Buffer (3个命令缓冲) | GPU-CPU 并行，减少帧等待 |
| **描述符集缓存** | `FVulkanDescriptorSetCache` | 帧内复用相同绑定的描述符集 |
| **管线缓存** | `VulkanPipelineCache` | 哈希查找，避免重复编译 |
| **布局缓存** | `FVulkanDescriptorSetLayoutCache` | 避免重复创建相同布局 |
| **RenderPass缓存** | `FVulkanRenderPassCache` | 按配置缓存渲染通道 |
| **延迟状态绑定** | `FVulkanPendingState` | draw 前批量应用状态 |
| **内存池** | 描述符池每帧重置 | 避免单独分配/释放开销 |

```cpp
// 延迟状态绑定示例 (FVulkanPendingState)
void prepareForDraw(VkCommandBuffer cmdBuffer) {
    if (m_pipelineDirty) {
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    }
    if (m_descriptorsDirty) {
        updateAndBindDescriptorSets(cmdBuffer);  // 批量更新
    }
    if (m_vertexBuffersDirty) {
        vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &m_vertexBuffer, &offset);
    }
}
```

## 11. 总结

本文通过实现一个带纹理的旋转立方体，介绍了 Vulkan 渲染的核心概念：

1. **命令系统**：Queue → CommandBuffer → CommandPool 的层次关系
2. **同步机制**：Fence（CPU-GPU）、Semaphore（GPU-GPU）、Pipeline Barrier（命令内）
3. **描述符系统**：Layout → Pool → Set 的资源绑定流程
4. **管线管理**：不可变管线的创建与缓存
5. **渲染附件**：颜色附件 + 深度附件的配置

**完整代码见 GitHub 仓库:**

[MonsterEngine · GitHub](https://github.com/monsterengine)

## 参考资料

- UE5 VulkanRHI源码: `Engine/Source/Runtime/VulkanRHI/`
- LearnOpenGL坐标系统: https://learnopengl-cn.github.io/01%20Getting%20started/08%20Coordinate%20Systems/
- Vulkan规范: https://www.khronos.org/registry/vulkan/specs/
