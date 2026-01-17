# Cube渲染流程架构文档

**效果：**

![](https://liyanliangpublic.oss-cn-hongkong.aliyuncs.com/img/202512061607330.gif)



---

## 1. 概述

本文介绍如何使用 **Vulkan API** 实现一个带纹理的旋转立方体渲染。基于 **MonsterEngine** 渲染器的 RHI架构，从零开始构建一个完整的 3D 渲染流程。



---

## 2. 代码结构图

![deepseek_mermaid_20251206_d33b62](https://liyanliangpublic.oss-cn-hongkong.aliyuncs.com/img/202512061608835.png)



## 3. 类UML图

### 3.1 核心类关系

![deepseek_mermaid_20251206_bd716f](https://liyanliangpublic.oss-cn-hongkong.aliyuncs.com/img/202512061605114.png)



### 3.2 命令系统类图

![deepseek_mermaid_20251206_07a4ca](https://liyanliangpublic.oss-cn-hongkong.aliyuncs.com/img/202512061611213.png)



## 4. 渲染流程图

![deepseek_mermaid_20251206_d03f4f](https://liyanliangpublic.oss-cn-hongkong.aliyuncs.com/img/202512061613467.png)



## 5. 命令系统

### 5.1 命令队列/缓冲/列表关系

![deepseek_mermaid_20251206_c85718](https://liyanliangpublic.oss-cn-hongkong.aliyuncs.com/img/202512061616542.png)



### 5.2 Ring Buffer管理



![deepseek_mermaid_20251206_e06afd](https://liyanliangpublic.oss-cn-hongkong.aliyuncs.com/img/202512061619258.png)



![deepseek_mermaid_20251206_7c7345](https://liyanliangpublic.oss-cn-hongkong.aliyuncs.com/img/202512061619055.png)



## 6. 同步机制

### 6.1 同步原语

| 原语 | 用途 | 场景 |
|------|------|------|
| **Semaphore** | GPU-GPU同步 | 交换链图像获取→渲染→呈现 |
| **Fence** | GPU-CPU同步 | CPU等待GPU完成命令执行 |
| **Pipeline Barrier** | 命令缓冲内同步 | 资源状态/布局转换 |

### 6.2 同步流程



![deepseek_mermaid_20251206_0fd82e](https://liyanliangpublic.oss-cn-hongkong.aliyuncs.com/img/202512061636386.png)



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

![deepseek_mermaid_20251206_4dd7fc](https://liyanliangpublic.oss-cn-hongkong.aliyuncs.com/img/202512061639601.png)



### 7.2 描述符更新流程

![deepseek_mermaid_20251206_b6926f](https://liyanliangpublic.oss-cn-hongkong.aliyuncs.com/img/202512061647249.png)



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

![deepseek_mermaid_20251206_8eb342](https://liyanliangpublic.oss-cn-hongkong.aliyuncs.com/img/202512061650661.png)



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



**本文github:**

[MonsterEngine · GitHub](https://github.com/monsterengine)



## 参考资料

- UE5 VulkanRHI源码: `Engine/Source/Runtime/VulkanRHI/`
- LearnOpenGL坐标系统: https://learnopengl-cn.github.io/01%20Getting%20started/08%20Coordinate%20Systems/
- Vulkan规范: https://www.khronos.org/registry/vulkan/specs/
