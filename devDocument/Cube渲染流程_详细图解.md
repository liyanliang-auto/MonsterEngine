# Cube渲染流程详细图解

本文档是《Cube渲染流程架构文档》的补充，提供更详细的流程图和技术细节。

---

## 1. 资源初始化流程

```
CubeRenderer::initialize(device)
        │
        ▼
┌───────────────────────────────────────────────────────────────┐
│ 1. createVertexBuffer() - 36个顶点 (6面×2三角形×3顶点)        │
│    ┌─────────────────────────────────────────────────────────┐│
│    │ struct CubeVertex { float pos[3]; float texCoord[2]; } ││
│    │ BufferDesc: size=720B, usage=VertexBuffer, cpuAccess    ││
│    │ vkCreateBuffer → vkAllocateMemory → vkMapMemory         ││
│    └─────────────────────────────────────────────────────────┘│
└───────────────────────┬───────────────────────────────────────┘
                        │
┌───────────────────────▼───────────────────────────────────────┐
│ 2. createUniformBuffer() - MVP矩阵 (192字节)                  │
│    ┌─────────────────────────────────────────────────────────┐│
│    │ struct CubeUniformBufferObject {                        ││
│    │   float model[16];      // 64B                          ││
│    │   float view[16];       // 64B                          ││
│    │   float projection[16]; // 64B                          ││
│    │ };                                                      ││
│    │ BufferDesc: usage=UniformBuffer, cpuAccess=true         ││
│    └─────────────────────────────────────────────────────────┘│
└───────────────────────┬───────────────────────────────────────┘
                        │
┌───────────────────────▼───────────────────────────────────────┐
│ 3. loadTextures() - 加载container.jpg和awesomeface.png        │
│    ┌─────────────────────────────────────────────────────────┐│
│    │ FTextureLoader::LoadFromFile()                          ││
│    │   - 使用stb_image加载图像数据                            ││
│    │   - 创建Staging Buffer上传到GPU                          ││
│    │   - Image Layout Transition:                            ││
│    │     UNDEFINED → TRANSFER_DST → SHADER_READ_ONLY         ││
│    │   - 生成Mipmap (可选)                                    ││
│    │ 失败时创建Placeholder纹理 (棋盘格/渐变)                   ││
│    └─────────────────────────────────────────────────────────┘│
└───────────────────────┬───────────────────────────────────────┘
                        │
┌───────────────────────▼───────────────────────────────────────┐
│ 4. createSampler() - 创建纹理采样器                           │
│    ┌─────────────────────────────────────────────────────────┐│
│    │ SamplerDesc:                                            ││
│    │   filter = Trilinear                                    ││
│    │   addressU/V/W = Wrap                                   ││
│    │   maxAnisotropy = 16                                    ││
│    └─────────────────────────────────────────────────────────┘│
└───────────────────────┬───────────────────────────────────────┘
                        │
┌───────────────────────▼───────────────────────────────────────┐
│ 5. createShaders() - 加载预编译的SPV着色器                    │
│    ┌─────────────────────────────────────────────────────────┐│
│    │ Shaders/Cube.vert.spv → createVertexShader()            ││
│    │ Shaders/Cube.frag.spv → createPixelShader()             ││
│    │                                                         ││
│    │ Vertex Shader输入:                                      ││
│    │   layout(location=0) in vec3 aPos;                      ││
│    │   layout(location=1) in vec2 aTexCoord;                 ││
│    │   layout(binding=0) uniform UBO { mat4 model,view,proj }││
│    │                                                         ││
│    │ Fragment Shader输入:                                    ││
│    │   layout(binding=1) uniform sampler2D texture1;         ││
│    │   layout(binding=2) uniform sampler2D texture2;         ││
│    └─────────────────────────────────────────────────────────┘│
└───────────────────────┬───────────────────────────────────────┘
                        │
┌───────────────────────▼───────────────────────────────────────┐
│ 6. createPipelineState() - 创建图形管线                       │
│    ┌─────────────────────────────────────────────────────────┐│
│    │ VulkanPipelineState::initialize():                      ││
│    │   1. createShaderModules() - VkShaderModule             ││
│    │   2. reflectShaders() - 提取资源绑定信息                 ││
│    │   3. createPipelineLayout() - VkDescriptorSetLayout     ││
│    │   4. createGraphicsPipeline() - VkPipeline              ││
│    │                                                         ││
│    │ Pipeline配置:                                           ││
│    │   - Vertex Input: pos(Float3) + texCoord(Float2)        ││
│    │   - Topology: TriangleList                              ││
│    │   - Rasterizer: Solid, CullBack                         ││
│    │   - DepthStencil: Enable, Less                          ││
│    │   - Blend: Disabled                                     ││
│    │   - RenderTarget: B8G8R8A8_SRGB                         ││
│    │   - DepthFormat: D32_FLOAT                              ││
│    └─────────────────────────────────────────────────────────┘│
└───────────────────────────────────────────────────────────────┘
```

---

## 2. 帧渲染详细流程

```
┌──────────────────────────────────────────────────────────────────────────┐
│                      Frame N Rendering Detail                             │
└──────────────────────────────────────────────────────────────────────────┘

 ┌─────────────────────────────────────────────────────────────────────────┐
 │ Phase 1: Frame Preparation                                              │
 ├─────────────────────────────────────────────────────────────────────────┤
 │                                                                         │
 │  prepareForNewFrame():                                                  │
 │                                                                         │
 │  ┌──────────────────────────────────────────────────────────────┐      │
 │  │ 1. acquireNextSwapchainImage()                               │      │
 │  │    vkAcquireNextImageKHR(swapchain,                          │      │
 │  │                          UINT64_MAX,  // 无超时              │      │
 │  │                          imageAvailableSemaphore[frame],     │      │
 │  │                          VK_NULL_HANDLE,                     │      │
 │  │                          &imageIndex)                        │      │
 │  │    → 获取交换链图像索引                                       │      │
 │  └──────────────────────────────────────────────────────────────┘      │
 │                              │                                          │
 │  ┌──────────────────────────▼──────────────────────────────────┐      │
 │  │ 2. prepareForNewActiveCommandBuffer()                        │      │
 │  │    currentBufferIndex = (currentBufferIndex + 1) % 3         │      │
 │  │                                                              │      │
 │  │    if (activeCmdBuffer->isSubmitted()) {                     │      │
 │  │        vkWaitForFences(fence, VK_TRUE, timeout);             │      │
 │  │    }                                                         │      │
 │  │                                                              │      │
 │  │    vkResetFences(fence);                                     │      │
 │  │    vkResetCommandBuffer(cmdBuffer, 0);                       │      │
 │  │    activeCmdBuffer->markAsReadyForBegin();                   │      │
 │  └──────────────────────────────────────────────────────────────┘      │
 │                              │                                          │
 │  ┌──────────────────────────▼──────────────────────────────────┐      │
 │  │ 3. Reset Per-Frame Resources                                 │      │
 │  │    pendingState->reset();        // 清除缓存状态             │      │
 │  │    descriptorPool->reset();      // 重置描述符池             │      │
 │  │    descriptorSetCache->Reset();  // 清除帧内缓存             │      │
 │  └──────────────────────────────────────────────────────────────┘      │
 │                                                                         │
 └─────────────────────────────────────────────────────────────────────────┘

 ┌─────────────────────────────────────────────────────────────────────────┐
 │ Phase 2: Command Recording                                              │
 ├─────────────────────────────────────────────────────────────────────────┤
 │                                                                         │
 │  cmdList->begin():                                                      │
 │  ┌──────────────────────────────────────────────────────────────┐      │
 │  │ VkCommandBufferBeginInfo beginInfo = {};                     │      │
 │  │ beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT;   │      │
 │  │ vkBeginCommandBuffer(cmdBuffer, &beginInfo);                 │      │
 │  │ state = Recording;                                           │      │
 │  └──────────────────────────────────────────────────────────────┘      │
 │                              │                                          │
 │  cmdList->setRenderTargets():                                          │
 │  ┌──────────────────────────▼──────────────────────────────────┐      │
 │  │ VkRenderPassBeginInfo rpInfo = {};                           │      │
 │  │ rpInfo.renderPass = device->getRenderPass();                 │      │
 │  │ rpInfo.framebuffer = swapchainFramebuffers[imageIndex];      │      │
 │  │ rpInfo.renderArea = {{0,0}, swapchainExtent};                │      │
 │  │                                                              │      │
 │  │ VkClearValue clearValues[2];                                 │      │
 │  │ clearValues[0].color = {0.2f, 0.3f, 0.3f, 1.0f}; // 深蓝色  │      │
 │  │ clearValues[1].depthStencil = {1.0f, 0};                     │      │
 │  │                                                              │      │
 │  │ vkCmdBeginRenderPass(cmdBuffer, &rpInfo, INLINE);            │      │
 │  │ pendingState->setInsideRenderPass(true);                     │      │
 │  └──────────────────────────────────────────────────────────────┘      │
 │                                                                         │
 └─────────────────────────────────────────────────────────────────────────┘

 ┌─────────────────────────────────────────────────────────────────────────┐
 │ Phase 3: Cube Rendering (CubeRenderer::render)                          │
 ├─────────────────────────────────────────────────────────────────────────┤
 │                                                                         │
 │  ┌──────────────────────────────────────────────────────────────┐      │
 │  │ 1. update(deltaTime)                                         │      │
 │  │    rotationAngle = time; // 基于时间的旋转                    │      │
 │  └──────────────────────────────────────────────────────────────┘      │
 │                              │                                          │
 │  ┌──────────────────────────▼──────────────────────────────────┐      │
 │  │ 2. updateUniformBuffer()                                     │      │
 │  │    buildModelMatrix(ubo.model);  // 旋转矩阵                  │      │
 │  │    buildViewMatrix(ubo.view);    // 相机在(0,0,-3)           │      │
 │  │    buildProjectionMatrix(ubo.projection); // 45° FOV         │      │
 │  │                                                              │      │
 │  │    void* data = uniformBuffer->map();                        │      │
 │  │    memcpy(data, &ubo, sizeof(ubo));                          │      │
 │  │    uniformBuffer->unmap();                                   │      │
 │  └──────────────────────────────────────────────────────────────┘      │
 │                              │                                          │
 │  ┌──────────────────────────▼──────────────────────────────────┐      │
 │  │ 3. State Setup (记录到PendingState)                          │      │
 │  │                                                              │      │
 │  │    setPipelineState(m_pipelineState)                         │      │
 │  │      → pendingState->setGraphicsPipeline()                   │      │
 │  │                                                              │      │
 │  │    setConstantBuffer(0, m_uniformBuffer)                     │      │
 │  │      → pendingState->setUniformBuffer(0, buffer, 0, size)    │      │
 │  │                                                              │      │
 │  │    setShaderResource(1, m_texture1)                          │      │
 │  │      → pendingState->setTexture(1, imageView, sampler)       │      │
 │  │                                                              │      │
 │  │    setShaderResource(2, m_texture2)                          │      │
 │  │      → pendingState->setTexture(2, imageView, sampler)       │      │
 │  │                                                              │      │
 │  │    setVertexBuffers(0, {m_vertexBuffer})                     │      │
 │  │      → pendingState->setVertexBuffer(0, buffer, 0)           │      │
 │  │                                                              │      │
 │  │    setViewport(800, 600)                                     │      │
 │  │      → pendingState->setViewport()                           │      │
 │  │                                                              │      │
 │  │    setScissorRect(0, 0, 800, 600)                            │      │
 │  │      → pendingState->setScissor()                            │      │
 │  └──────────────────────────────────────────────────────────────┘      │
 │                              │                                          │
 │  ┌──────────────────────────▼──────────────────────────────────┐      │
 │  │ 4. draw(36, 0) - 触发实际Vulkan命令                          │      │
 │  │                                                              │      │
 │  │    prepareForDraw(): ← 延迟状态应用                          │      │
 │  │    ├─ vkCmdBindPipeline(GRAPHICS, pipeline)                  │      │
 │  │    ├─ vkCmdSetViewport(0, 1, &viewport)                      │      │
 │  │    ├─ vkCmdSetScissor(0, 1, &scissor)                        │      │
 │  │    ├─ vkCmdBindVertexBuffers(0, 1, &vb, &offset)             │      │
 │  │    └─ updateAndBindDescriptorSets():                         │      │
 │  │         ├─ 构建DescriptorSetKey (layout + bindings)          │      │
 │  │         ├─ 查找Cache (命中则直接使用)                         │      │
 │  │         ├─ 分配DescriptorSet (vkAllocateDescriptorSets)      │      │
 │  │         ├─ 更新DescriptorSet (vkUpdateDescriptorSets)        │      │
 │  │         │    - Write[0]: UniformBuffer binding 0             │      │
 │  │         │    - Write[1]: CombinedImageSampler binding 1      │      │
 │  │         │    - Write[2]: CombinedImageSampler binding 2      │      │
 │  │         └─ vkCmdBindDescriptorSets(GRAPHICS, layout, 0, 1)   │      │
 │  │                                                              │      │
 │  │    vkCmdDraw(36, 1, 0, 0);  // 36顶点, 1实例                 │      │
 │  └──────────────────────────────────────────────────────────────┘      │
 │                                                                         │
 └─────────────────────────────────────────────────────────────────────────┘

 ┌─────────────────────────────────────────────────────────────────────────┐
 │ Phase 4: Submit & Present                                               │
 ├─────────────────────────────────────────────────────────────────────────┤
 │                                                                         │
 │  cmdList->endRenderPass():                                              │
 │  ┌──────────────────────────────────────────────────────────────┐      │
 │  │ vkCmdEndRenderPass(cmdBuffer);                               │      │
 │  │ pendingState->setInsideRenderPass(false);                    │      │
 │  └──────────────────────────────────────────────────────────────┘      │
 │                              │                                          │
 │  cmdList->end():                                                        │
 │  ┌──────────────────────────▼──────────────────────────────────┐      │
 │  │ vkEndCommandBuffer(cmdBuffer);                               │      │
 │  │ state = Ended;                                               │      │
 │  └──────────────────────────────────────────────────────────────┘      │
 │                              │                                          │
 │  device->present():                                                     │
 │  ┌──────────────────────────▼──────────────────────────────────┐      │
 │  │ // Submit                                                    │      │
 │  │ VkSubmitInfo submitInfo = {};                                │      │
 │  │ submitInfo.waitSemaphoreCount = 1;                           │      │
 │  │ submitInfo.pWaitSemaphores = &imageAvailableSemaphore[frame];│      │
 │  │ submitInfo.pWaitDstStageMask = {COLOR_ATTACHMENT_OUTPUT};    │      │
 │  │ submitInfo.commandBufferCount = 1;                           │      │
 │  │ submitInfo.pCommandBuffers = &cmdBuffer;                     │      │
 │  │ submitInfo.signalSemaphoreCount = 1;                         │      │
 │  │ submitInfo.pSignalSemaphores = &renderFinishedSemaphore[fr]; │      │
 │  │                                                              │      │
 │  │ vkQueueSubmit(graphicsQueue, 1, &submitInfo, fence[frame]);  │      │
 │  │ activeCmdBuffer->markSubmitted();                            │      │
 │  └──────────────────────────────────────────────────────────────┘      │
 │                              │                                          │
 │  ┌──────────────────────────▼──────────────────────────────────┐      │
 │  │ // Present                                                   │      │
 │  │ VkPresentInfoKHR presentInfo = {};                           │      │
 │  │ presentInfo.waitSemaphoreCount = 1;                          │      │
 │  │ presentInfo.pWaitSemaphores = &renderFinishedSemaphore[fr];  │      │
 │  │ presentInfo.swapchainCount = 1;                              │      │
 │  │ presentInfo.pSwapchains = &swapchain;                        │      │
 │  │ presentInfo.pImageIndices = &imageIndex;                     │      │
 │  │                                                              │      │
 │  │ vkQueuePresentKHR(presentQueue, &presentInfo);               │      │
 │  │ currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;    │      │
 │  └──────────────────────────────────────────────────────────────┘      │
 │                                                                         │
 └─────────────────────────────────────────────────────────────────────────┘
```

---

## 3. 描述符绑定详细流程

```
┌──────────────────────────────────────────────────────────────────────────┐
│                 Descriptor Binding Detailed Flow                          │
└──────────────────────────────────────────────────────────────────────────┘

Step 1: 应用程序设置资源绑定
────────────────────────────────────────────────────────────────────────────
 setConstantBuffer(0, uniformBuffer)
 setShaderResource(1, texture1)
 setShaderResource(2, texture2)
          │
          ▼
 FVulkanPendingState:
   m_uniformBuffers[0] = {buffer, offset=0, range=192}
   m_textures[1] = {imageView1, sampler}
   m_textures[2] = {imageView2, sampler}
   m_descriptorsDirty = true


Step 2: Draw调用时处理 (prepareForDraw → updateAndBindDescriptorSets)
────────────────────────────────────────────────────────────────────────────

 ┌─────────────────────────────────────────────────────────────────────────┐
 │ buildDescriptorSetKey()                                                 │
 │   FVulkanDescriptorSetKey key;                                          │
 │   key.Layout = pipeline->getDescriptorSetLayouts()[0];                  │
 │   key.BufferBindings[0] = {buffer, 0, 192};                             │
 │   key.ImageBindings[1] = {imageView1, sampler, SHADER_READ_ONLY};       │
 │   key.ImageBindings[2] = {imageView2, sampler, SHADER_READ_ONLY};       │
 │   return key;                                                           │
 └─────────────────────────────────────────────────────────────────────────┘
          │
          ▼
 ┌─────────────────────────────────────────────────────────────────────────┐
 │ FVulkanDescriptorSetCache::GetOrAllocate(key)                           │
 │                                                                         │
 │   hash = key.GetHash();                                                 │
 │   if (FrameCache.contains(hash)) {                                      │
 │       // Cache Hit! 直接返回                                             │
 │       return FrameCache[hash];                                          │
 │   }                                                                     │
 │                                                                         │
 │   // Cache Miss - 需要分配和更新                                         │
 │   descriptorSet = AllocateAndUpdate(key);                               │
 │   FrameCache[hash] = descriptorSet;                                     │
 │   return descriptorSet;                                                 │
 └─────────────────────────────────────────────────────────────────────────┘
          │
          ▼
 ┌─────────────────────────────────────────────────────────────────────────┐
 │ AllocateAndUpdate(key)                                                  │
 │                                                                         │
 │   // 从描述符池分配                                                      │
 │   VkDescriptorSetAllocateInfo allocInfo = {};                           │
 │   allocInfo.descriptorPool = pool;                                      │
 │   allocInfo.descriptorSetCount = 1;                                     │
 │   allocInfo.pSetLayouts = &key.Layout;                                  │
 │   vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet);         │
 │                                                                         │
 │   // 构建写入信息                                                        │
 │   TArray<VkWriteDescriptorSet> writes;                                  │
 │                                                                         │
 │   // Binding 0: Uniform Buffer                                          │
 │   VkDescriptorBufferInfo bufferInfo = {buffer, 0, 192};                 │
 │   writes.push_back({                                                    │
 │       .dstSet = descriptorSet,                                          │
 │       .dstBinding = 0,                                                  │
 │       .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,              │
 │       .pBufferInfo = &bufferInfo                                        │
 │   });                                                                   │
 │                                                                         │
 │   // Binding 1: Texture1                                                │
 │   VkDescriptorImageInfo imageInfo1 = {sampler, imageView1, SHADER_READ};│
 │   writes.push_back({                                                    │
 │       .dstSet = descriptorSet,                                          │
 │       .dstBinding = 1,                                                  │
 │       .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,      │
 │       .pImageInfo = &imageInfo1                                         │
 │   });                                                                   │
 │                                                                         │
 │   // Binding 2: Texture2                                                │
 │   VkDescriptorImageInfo imageInfo2 = {sampler, imageView2, SHADER_READ};│
 │   writes.push_back({                                                    │
 │       .dstSet = descriptorSet,                                          │
 │       .dstBinding = 2,                                                  │
 │       .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,      │
 │       .pImageInfo = &imageInfo2                                         │
 │   });                                                                   │
 │                                                                         │
 │   // 执行更新                                                            │
 │   vkUpdateDescriptorSets(device, writes.size(), writes.data(), 0, null);│
 │                                                                         │
 │   return descriptorSet;                                                 │
 └─────────────────────────────────────────────────────────────────────────┘
          │
          ▼
 ┌─────────────────────────────────────────────────────────────────────────┐
 │ 绑定到命令缓冲                                                          │
 │   vkCmdBindDescriptorSets(cmdBuffer,                                    │
 │                           VK_PIPELINE_BIND_POINT_GRAPHICS,              │
 │                           pipelineLayout,                               │
 │                           0,              // firstSet                   │
 │                           1,              // setCount                   │
 │                           &descriptorSet, // pDescriptorSets            │
 │                           0,              // dynamicOffsetCount         │
 │                           nullptr);       // pDynamicOffsets            │
 └─────────────────────────────────────────────────────────────────────────┘
```

---

## 4. MVP矩阵计算

```
┌──────────────────────────────────────────────────────────────────────────┐
│                         MVP Matrix Calculation                            │
└──────────────────────────────────────────────────────────────────────────┘

Model Matrix (旋转动画):
────────────────────────────────────────────────────────────────────────────
  buildModelMatrix(float* out):
    // 绕轴(0.5, 1.0, 0.0)旋转
    rotationAngle = time;  // 随时间增加
    
    // 旋转矩阵公式 (Rodriguez公式):
    // R = I*cos(θ) + (1-cos(θ))*(n⊗n) + sin(θ)*[n]×
    
    axis = normalize(0.5, 1.0, 0.0)
    matrixRotate(out, rotationAngle, axis.x, axis.y, axis.z)


View Matrix (相机位置):
────────────────────────────────────────────────────────────────────────────
  buildViewMatrix(float* out):
    // 相机在(0, 0, -3)，看向原点
    // 简单的平移矩阵
    matrixTranslate(out, 0.0, 0.0, -3.0)
    
    // 等价于:
    // eye = (0, 0, -3)
    // center = (0, 0, 0)
    // up = (0, 1, 0)


Projection Matrix (透视投影 - Vulkan坐标系):
────────────────────────────────────────────────────────────────────────────
  buildProjectionMatrix(float* out):
    fov = 45°
    aspect = width / height
    near = 0.1
    far = 100.0
    
    // Vulkan透视矩阵 (RH, depth [0,1]):
    // 
    //  ┌                                           ┐
    //  │ 1/(aspect*tan(fov/2))    0         0    0 │
    //  │         0          -1/tan(fov/2)   0    0 │  ← Y取反(Vulkan Y轴向下)
    //  │         0                0       f/(n-f) -1│
    //  │         0                0     -fn/(f-n)  0│
    //  └                                           ┘
    
    matrix[0] = 1.0 / (aspect * tan(fov/2))
    matrix[5] = -1.0 / tan(fov/2)  // 关键: Y轴翻转
    matrix[10] = far / (near - far)
    matrix[11] = -1.0
    matrix[14] = -(far * near) / (far - near)
```

---

## 5. 纹理资源同步

```
┌──────────────────────────────────────────────────────────────────────────┐
│                      Texture Resource Synchronization                     │
└──────────────────────────────────────────────────────────────────────────┘

纹理上传流程:
────────────────────────────────────────────────────────────────────────────

  ┌───────────────┐
  │ CPU Memory    │  图像数据 (stb_image加载)
  └───────┬───────┘
          │ memcpy
          ▼
  ┌───────────────┐
  │ Staging Buffer│  HOST_VISIBLE | HOST_COHERENT
  └───────┬───────┘
          │ vkCmdCopyBufferToImage (需要Barrier)
          ▼
  ┌───────────────┐
  │ GPU Image     │  DEVICE_LOCAL (最优性能)
  └───────────────┘


Layout Transitions:
────────────────────────────────────────────────────────────────────────────

  UNDEFINED                    准备接收数据
       │
       ├─ Barrier 1:
       │    srcStage = TOP_OF_PIPE
       │    dstStage = TRANSFER
       │    srcAccess = 0
       │    dstAccess = TRANSFER_WRITE
       ▼
  TRANSFER_DST_OPTIMAL         数据传输目标
       │
       │  vkCmdCopyBufferToImage()
       │
       ├─ Barrier 2:
       │    srcStage = TRANSFER
       │    dstStage = FRAGMENT_SHADER
       │    srcAccess = TRANSFER_WRITE
       │    dstAccess = SHADER_READ
       ▼
  SHADER_READ_ONLY_OPTIMAL     着色器可读取


运行时纹理访问同步:
────────────────────────────────────────────────────────────────────────────

  在渲染循环中，纹理已经处于SHADER_READ_ONLY_OPTIMAL布局
  
  同步由RenderPass的subpass dependency保证:
  
  VkSubpassDependency dependency = {};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.srcAccessMask = 0;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
```

---

## 6. 完整数据流图

```
┌──────────────────────────────────────────────────────────────────────────┐
│                         Complete Data Flow                                │
└──────────────────────────────────────────────────────────────────────────┘

                           ┌─────────────────┐
                           │   CPU Memory    │
                           └────────┬────────┘
                                    │
          ┌─────────────────────────┼─────────────────────────┐
          │                         │                         │
          ▼                         ▼                         ▼
  ┌───────────────┐        ┌───────────────┐        ┌───────────────┐
  │ Vertex Data   │        │   MVP Data    │        │ Texture Data  │
  │ (720 bytes)   │        │ (192 bytes)   │        │ (256KB×2)     │
  └───────┬───────┘        └───────┬───────┘        └───────┬───────┘
          │                        │                        │
          │ createBuffer           │ map/unmap              │ staging upload
          ▼                        ▼                        ▼
  ┌───────────────┐        ┌───────────────┐        ┌───────────────┐
  │ VkBuffer      │        │ VkBuffer      │        │ VkImage       │
  │ VERTEX_BUFFER │        │ UNIFORM_BUFFER│        │ +VkImageView  │
  └───────┬───────┘        └───────┬───────┘        └───────┬───────┘
          │                        │                        │
          │                        │                        │
          └────────────────┬───────┴────────────────────────┘
                           │
                           ▼
                  ┌───────────────────┐
                  │ VkDescriptorSet   │
                  │  binding 0: UBO   │
                  │  binding 1: tex1  │
                  │  binding 2: tex2  │
                  └────────┬──────────┘
                           │ vkCmdBindDescriptorSets
                           ▼
                  ┌───────────────────┐
                  │ VkPipeline        │
                  │  + Vertex Shader  │
                  │  + Fragment Shader│
                  └────────┬──────────┘
                           │ vkCmdDraw(36)
                           ▼
                  ┌───────────────────┐
                  │ GPU Execution     │
                  │ 36 vertices       │
                  │ → 12 triangles    │
                  │ → 6 faces         │
                  └────────┬──────────┘
                           │
                           ▼
                  ┌───────────────────┐
                  │ Framebuffer       │
                  │ Color + Depth     │
                  └────────┬──────────┘
                           │ vkQueuePresentKHR
                           ▼
                  ┌───────────────────┐
                  │ Display           │
                  └───────────────────┘
```

---

## 参考文件

| 文件路径 | 描述 |
|----------|------|
| `Source/CubeApplication.cpp` | 应用入口和生命周期 |
| `Source/CubeRenderer.cpp` | 渲染逻辑实现 |
| `Include/CubeRenderer.h` | 渲染器接口定义 |
| `Source/Platform/Vulkan/VulkanDevice.cpp` | 设备管理 |
| `Source/Platform/Vulkan/VulkanCommandBuffer.cpp` | 命令缓冲管理 |
| `Source/Platform/Vulkan/VulkanPendingState.cpp` | 状态管理 |
| `Source/Platform/Vulkan/VulkanPipelineState.cpp` | 管线状态 |
| `Source/Platform/Vulkan/VulkanRHICommandList.cpp` | RHI命令列表 |
