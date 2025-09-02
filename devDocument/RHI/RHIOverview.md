# RHI (渲染硬件接口) 概述

RHI (Render Hardware Interface) 是MonsterRender引擎的核心抽象层，提供了统一的图形API接口，支持多种后端实现。

## 设计目标

### 1. API无关性
RHI层完全隐藏底层图形API的差异，应用代码无需了解具体使用的是Vulkan、D3D12还是OpenGL。

### 2. 现代图形API支持
针对现代显式图形API (Vulkan, D3D12) 进行设计，同时向后兼容传统API (D3D11, OpenGL)。

### 3. 高性能
最小化抽象层开销，支持多线程渲染，充分利用现代GPU特性。

### 4. 类型安全
使用强类型设计，编译期检查错误，减少运行时问题。

## 核心组件

### IRHIDevice - 设备接口
设备是RHI系统的核心，负责资源创建和命令执行：

```cpp
class IRHIDevice {
public:
    // 资源创建
    virtual TSharedPtr<IRHIBuffer> createBuffer(const BufferDesc& desc) = 0;
    virtual TSharedPtr<IRHITexture> createTexture(const TextureDesc& desc) = 0;
    virtual TSharedPtr<IRHIPipelineState> createPipelineState(const PipelineStateDesc& desc) = 0;
    
    // 着色器创建
    virtual TSharedPtr<IRHIVertexShader> createVertexShader(TSpan<const uint8> bytecode) = 0;
    virtual TSharedPtr<IRHIPixelShader> createPixelShader(TSpan<const uint8> bytecode) = 0;
    
    // 命令执行
    virtual TSharedPtr<IRHICommandList> createCommandList() = 0;
    virtual void executeCommandLists(TSpan<TSharedPtr<IRHICommandList>> commandLists) = 0;
    virtual IRHICommandList* getImmediateCommandList() = 0;
    
    // 同步和呈现
    virtual void waitForIdle() = 0;
    virtual void present() = 0;
    
    // 调试和性能
    virtual void setDebugName(const String& name) = 0;
    virtual void getMemoryStats(uint64& usedBytes, uint64& availableBytes) = 0;
};
```

### IRHIResource - 资源基类
所有GPU资源的统一基类：

```cpp
class IRHIResource {
public:
    virtual ~IRHIResource() = default;
    
    // 基本信息
    const String& getDebugName() const;
    void setDebugName(const String& name);
    virtual uint32 getSize() const = 0;
    virtual EResourceUsage getUsage() const = 0;
    
protected:
    String m_debugName;
};
```

**资源类型层次**：
- `IRHIBuffer` - GPU缓冲区
- `IRHITexture` - 纹理和图像
- `IRHIShader` - 着色器程序
- `IRHIPipelineState` - 管道状态对象

### IRHICommandList - 命令列表
记录和执行渲染命令：

```cpp
class IRHICommandList {
public:
    // 命令列表生命周期
    virtual void begin() = 0;
    virtual void end() = 0;
    virtual void reset() = 0;
    
    // 资源绑定
    virtual void setPipelineState(TSharedPtr<IRHIPipelineState> pipelineState) = 0;
    virtual void setVertexBuffers(uint32 startSlot, TSpan<TSharedPtr<IRHIBuffer>> vertexBuffers) = 0;
    virtual void setIndexBuffer(TSharedPtr<IRHIBuffer> indexBuffer, bool is32Bit = true) = 0;
    
    // 渲染状态
    virtual void setViewport(const Viewport& viewport) = 0;
    virtual void setScissorRect(const ScissorRect& scissorRect) = 0;
    virtual void setRenderTargets(TSpan<TSharedPtr<IRHITexture>> renderTargets, 
                                TSharedPtr<IRHITexture> depthStencil = nullptr) = 0;
    
    // 绘制命令
    virtual void draw(uint32 vertexCount, uint32 startVertexLocation = 0) = 0;
    virtual void drawIndexed(uint32 indexCount, uint32 startIndexLocation = 0, 
                           int32 baseVertexLocation = 0) = 0;
    virtual void drawInstanced(uint32 vertexCountPerInstance, uint32 instanceCount,
                             uint32 startVertexLocation = 0, uint32 startInstanceLocation = 0) = 0;
    
    // 资源管理
    virtual void clearRenderTarget(TSharedPtr<IRHITexture> renderTarget, const float32 clearColor[4]) = 0;
    virtual void clearDepthStencil(TSharedPtr<IRHITexture> depthStencil, bool clearDepth = true, 
                                 bool clearStencil = false, float32 depth = 1.0f, uint8 stencil = 0) = 0;
    
    // 调试支持
    virtual void beginEvent(const String& name) = 0;
    virtual void endEvent() = 0;
};
```

## 资源管理

### 缓冲区 (IRHIBuffer)
用于存储顶点、索引、常量等数据：

```cpp
struct BufferDesc {
    uint32 size = 0;                    // 缓冲区大小（字节）
    EResourceUsage usage = EResourceUsage::None;  // 使用方式
    bool cpuAccessible = false;         // CPU是否可访问
    String debugName;                   // 调试名称
};

// 使用示例
BufferDesc vertexBufferDesc(sizeof(vertices), EResourceUsage::VertexBuffer);
auto vertexBuffer = device->createBuffer(vertexBufferDesc);
```

### 纹理 (IRHITexture)
用于图像数据和渲染目标：

```cpp
struct TextureDesc {
    uint32 width = 1;
    uint32 height = 1;
    uint32 depth = 1;
    uint32 mipLevels = 1;
    uint32 arraySize = 1;
    EPixelFormat format = EPixelFormat::R8G8B8A8_UNORM;
    EResourceUsage usage = EResourceUsage::ShaderResource;
    String debugName;
};

// 使用示例
TextureDesc colorTargetDesc(1920, 1080, EPixelFormat::R8G8B8A8_UNORM, EResourceUsage::RenderTarget);
auto colorTarget = device->createTexture(colorTargetDesc);
```

### 管道状态 (IRHIPipelineState)
封装完整的图形管线状态：

```cpp
struct PipelineStateDesc {
    TSharedPtr<IRHIVertexShader> vertexShader;      // 顶点着色器
    TSharedPtr<IRHIPixelShader> pixelShader;        // 像素着色器
    EPrimitiveTopology primitiveTopology;           // 图元拓扑
    BlendState blendState;                          // 混合状态
    RasterizerState rasterizerState;                // 光栅化状态
    DepthStencilState depthStencilState;            // 深度模板状态
    TArray<EPixelFormat> renderTargetFormats;       // 渲染目标格式
    EPixelFormat depthStencilFormat;                // 深度缓冲格式
};
```

## 工厂模式

### RHIFactory
负责创建和管理不同后端的RHI设备：

```cpp
class RHIFactory {
public:
    // 创建设备
    static TUniquePtr<IRHIDevice> createDevice(const RHICreateInfo& createInfo);
    
    // 后端查询
    static TArray<ERHIBackend> getAvailableBackends();
    static bool isBackendAvailable(ERHIBackend backend);
    static const char* getBackendName(ERHIBackend backend);
    static ERHIBackend selectBestBackend();
};
```

### 后端选择
支持多种图形API后端：

```cpp
enum class ERHIBackend : uint32 {
    None = 0,
    D3D11,      // Direct3D 11
    D3D12,      // Direct3D 12
    Vulkan,     // Vulkan
    OpenGL,     // OpenGL
    Metal       // Metal (macOS/iOS)
};

// 自动选择最佳后端
RHICreateInfo createInfo;
createInfo.preferredBackend = ERHIBackend::None; // 自动选择
auto device = RHIFactory::createDevice(createInfo);
```

## 渲染流程示例

### 基础三角形渲染
```cpp
// 1. 创建设备
RHICreateInfo createInfo;
createInfo.preferredBackend = ERHIBackend::Vulkan;
createInfo.enableValidation = true;
auto device = RHIFactory::createDevice(createInfo);

// 2. 创建资源
BufferDesc vertexBufferDesc(sizeof(vertices), EResourceUsage::VertexBuffer);
auto vertexBuffer = device->createBuffer(vertexBufferDesc);

// 上传顶点数据
if (void* mappedData = vertexBuffer->map()) {
    memcpy(mappedData, vertices.data(), sizeof(vertices));
    vertexBuffer->unmap();
}

// 3. 创建着色器
auto vertexShader = device->createVertexShader(vertexShaderBytecode);
auto pixelShader = device->createPixelShader(pixelShaderBytecode);

// 4. 创建管道状态
PipelineStateDesc pipelineDesc;
pipelineDesc.vertexShader = vertexShader;
pipelineDesc.pixelShader = pixelShader;
pipelineDesc.primitiveTopology = EPrimitiveTopology::TriangleList;
auto pipelineState = device->createPipelineState(pipelineDesc);

// 5. 记录渲染命令
auto cmdList = device->createCommandList();
cmdList->begin();
{
    cmdList->setPipelineState(pipelineState);
    cmdList->setVertexBuffers(0, {vertexBuffer});
    cmdList->setViewport({0, 0, 1920, 1080});
    cmdList->draw(3); // 绘制3个顶点（一个三角形）
}
cmdList->end();

// 6. 执行命令
device->executeCommandLists({cmdList});
device->present();
```

## 同步机制

### 显式同步
现代API需要显式管理同步：

```cpp
// 等待所有GPU工作完成
device->waitForIdle();

// 资源状态转换（Vulkan/D3D12需要）
cmdList->transitionResource(texture, 
    EResourceUsage::ShaderResource, 
    EResourceUsage::RenderTarget);
```

### 资源屏障
```cpp
// 插入资源屏障确保正确同步
cmdList->resourceBarrier();
```

## 调试支持

### 验证层
```cpp
RHICreateInfo createInfo;
createInfo.enableValidation = true;    // 开启验证层
createInfo.enableDebugMarkers = true;  // 开启调试标记
```

### 调试事件
```cpp
// 手动调试标记
cmdList->beginEvent("Render Opaque Objects");
// ... 渲染命令 ...
cmdList->endEvent();

// RAII调试事件
{
    MR_SCOPED_DEBUG_EVENT(cmdList, "Shadow Pass");
    // ... 阴影渲染 ...
} // 自动调用endEvent
```

### 资源调试名称
```cpp
buffer->setDebugName("Vertex Buffer - Triangle");
texture->setDebugName("Color Target - Main");
```

## 错误处理

### 返回值检查
```cpp
auto device = RHIFactory::createDevice(createInfo);
if (!device) {
    MR_LOG_FATAL("Failed to create RHI device");
    return false;
}
```

### 资源验证
```cpp
auto buffer = device->createBuffer(bufferDesc);
MR_ASSERT(buffer != nullptr);
MR_ASSERT(buffer->getSize() == bufferDesc.size);
```

## 性能优化

### 批处理
```cpp
// 批量执行命令列表
TArray<TSharedPtr<IRHICommandList>> commandLists = {
    shadowPassCmdList,
    geometryPassCmdList,
    lightingPassCmdList,
    postProcessCmdList
};
device->executeCommandLists(commandLists);
```

### 资源重用
```cpp
// 重置命令列表进行重用
cmdList->reset();
cmdList->begin();
// ... 新的渲染命令 ...
```

### 内存管理
```cpp
// 获取内存使用统计
uint64 usedBytes, availableBytes;
device->getMemoryStats(usedBytes, availableBytes);

// 触发垃圾回收
device->collectGarbage();
```

---

## 相关文档
- [设备接口详解](DeviceInterface.md)
- [资源管理详解](ResourceManagement.md)
- [命令列表详解](CommandList.md)
- [Vulkan后端实现](../Vulkan/VulkanOverview.md)
