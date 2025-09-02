# 基础引擎使用指南

本文档展示如何使用MonsterRender引擎进行基本的初始化、资源管理和渲染操作。

## 引擎初始化

### 基本初始化
```cpp
#include "Core/CoreMinimal.h"
#include "Engine.h"

using namespace MonsterRender;

int main() {
    // 设置日志级别
    Logger::getInstance().setMinLevel(ELogLevel::Debug);
    MR_LOG_INFO("Starting MonsterRender Application");
    
    // 创建引擎实例
    Engine engine;
    
    // 配置RHI创建信息
    RHI::RHICreateInfo rhiCreateInfo;
    rhiCreateInfo.preferredBackend = RHI::ERHIBackend::Vulkan;
    rhiCreateInfo.enableValidation = true;       // 开启验证层（调试版本）
    rhiCreateInfo.enableDebugMarkers = true;     // 开启调试标记
    rhiCreateInfo.applicationName = "My Application";
    rhiCreateInfo.applicationVersion = 1;
    rhiCreateInfo.windowWidth = 1920;
    rhiCreateInfo.windowHeight = 1080;
    
    // 初始化引擎
    if (!engine.initialize(rhiCreateInfo)) {
        MR_LOG_ERROR("Failed to initialize engine");
        return -1;
    }
    
    // 运行主循环
    engine.run();
    
    // 引擎会在析构时自动清理资源
    return 0;
}
```

### 高级初始化配置
```cpp
// 自定义后端选择
RHI::RHICreateInfo createInfo;

// 检查可用后端
auto availableBackends = RHI::RHIFactory::getAvailableBackends();
MR_LOG_INFO("Available RHI backends:");
for (auto backend : availableBackends) {
    MR_LOG_INFO("  - " + String(RHI::RHIFactory::getBackendName(backend)));
}

// 根据平台选择最佳后端
if (RHI::RHIFactory::isBackendAvailable(RHI::ERHIBackend::Vulkan)) {
    createInfo.preferredBackend = RHI::ERHIBackend::Vulkan;
} else if (RHI::RHIFactory::isBackendAvailable(RHI::ERHIBackend::D3D12)) {
    createInfo.preferredBackend = RHI::ERHIBackend::D3D12;
} else {
    createInfo.preferredBackend = RHI::ERHIBackend::None; // 自动选择
}

// 条件性调试配置
#ifdef _DEBUG
    createInfo.enableValidation = true;
    createInfo.enableDebugMarkers = true;
#else
    createInfo.enableValidation = false;
    createInfo.enableDebugMarkers = false;
#endif
```

## 直接使用RHI接口

### 创建RHI设备
```cpp
#include "RHI/RHI.h"

// 直接创建RHI设备（不使用Engine类）
RHI::RHICreateInfo createInfo;
createInfo.preferredBackend = RHI::ERHIBackend::Vulkan;
createInfo.enableValidation = true;

auto device = RHI::RHIFactory::createDevice(createInfo);
if (!device) {
    MR_LOG_FATAL("Failed to create RHI device");
    return false;
}

// 获取设备能力信息
const auto& capabilities = device->getCapabilities();
MR_LOG_INFO("GPU Device: " + capabilities.deviceName);
MR_LOG_INFO("GPU Vendor: " + capabilities.vendorName);
MR_LOG_INFO("Video Memory: " + std::to_string(capabilities.dedicatedVideoMemory / 1024 / 1024) + " MB");
```

### 资源创建和管理
```cpp
// 创建顶点缓冲区
struct Vertex {
    float position[3];
    float color[3];
};

TArray<Vertex> vertices = {
    {{0.0f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}},  // 红色顶点
    {{0.5f,  0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}},  // 绿色顶点
    {{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}}   // 蓝色顶点
};

// 创建缓冲区描述
RHI::BufferDesc vertexBufferDesc;
vertexBufferDesc.size = sizeof(Vertex) * vertices.size();
vertexBufferDesc.usage = RHI::EResourceUsage::VertexBuffer;
vertexBufferDesc.cpuAccessible = true;  // 允许CPU写入
vertexBufferDesc.debugName = "Triangle Vertex Buffer";

// 创建缓冲区
auto vertexBuffer = device->createBuffer(vertexBufferDesc);
if (!vertexBuffer) {
    MR_LOG_ERROR("Failed to create vertex buffer");
    return false;
}

// 上传顶点数据
if (void* mappedData = vertexBuffer->map()) {
    std::memcpy(mappedData, vertices.data(), vertexBufferDesc.size);
    vertexBuffer->unmap();
    MR_LOG_INFO("Vertex data uploaded successfully");
} else {
    MR_LOG_ERROR("Failed to map vertex buffer");
}
```

### 纹理创建
```cpp
// 创建渲染目标纹理
RHI::TextureDesc colorTargetDesc;
colorTargetDesc.width = 1920;
colorTargetDesc.height = 1080;
colorTargetDesc.format = RHI::EPixelFormat::R8G8B8A8_UNORM;
colorTargetDesc.usage = RHI::EResourceUsage::RenderTarget;
colorTargetDesc.debugName = "Main Color Target";

auto colorTarget = device->createTexture(colorTargetDesc);

// 创建深度缓冲区
RHI::TextureDesc depthBufferDesc;
depthBufferDesc.width = 1920;
depthBufferDesc.height = 1080;
depthBufferDesc.format = RHI::EPixelFormat::D32_FLOAT;
depthBufferDesc.usage = RHI::EResourceUsage::DepthStencil;
depthBufferDesc.debugName = "Main Depth Buffer";

auto depthBuffer = device->createTexture(depthBufferDesc);
```

## 着色器管理

### 加载着色器字节码
```cpp
// 从文件加载着色器字节码的辅助函数
TOptional<TArray<uint8>> loadShaderBytecode(const String& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        MR_LOG_ERROR("Failed to open shader file: " + filename);
        return {};
    }
    
    size_t fileSize = static_cast<size_t>(file.tellg());
    TArray<uint8> bytecode(fileSize);
    
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytecode.data()), fileSize);
    file.close();
    
    return bytecode;
}

// 创建着色器
auto vertexShaderBytecode = loadShaderBytecode("shaders/triangle.vert.spv");
auto pixelShaderBytecode = loadShaderBytecode("shaders/triangle.frag.spv");

if (!vertexShaderBytecode || !pixelShaderBytecode) {
    MR_LOG_ERROR("Failed to load shader bytecode");
    return false;
}

auto vertexShader = device->createVertexShader(*vertexShaderBytecode);
auto pixelShader = device->createPixelShader(*pixelShaderBytecode);

if (!vertexShader || !pixelShader) {
    MR_LOG_ERROR("Failed to create shaders");
    return false;
}
```

### 管道状态创建
```cpp
// 配置管道状态
RHI::PipelineStateDesc pipelineDesc;
pipelineDesc.vertexShader = vertexShader;
pipelineDesc.pixelShader = pixelShader;
pipelineDesc.primitiveTopology = RHI::EPrimitiveTopology::TriangleList;

// 配置混合状态（透明度混合）
pipelineDesc.blendState.blendEnable = true;
pipelineDesc.blendState.srcColorBlend = RHI::EBlendFactor::SrcAlpha;
pipelineDesc.blendState.destColorBlend = RHI::EBlendFactor::InvSrcAlpha;
pipelineDesc.blendState.colorBlendOp = RHI::EBlendOp::Add;

// 配置光栅化状态
pipelineDesc.rasterizerState.fillMode = RHI::EFillMode::Solid;
pipelineDesc.rasterizerState.cullMode = RHI::ECullMode::Back;
pipelineDesc.rasterizerState.frontCounterClockwise = false;

// 配置深度测试
pipelineDesc.depthStencilState.depthEnable = true;
pipelineDesc.depthStencilState.depthWriteEnable = true;
pipelineDesc.depthStencilState.depthFunc = RHI::EComparisonFunc::Less;

// 设置渲染目标格式
pipelineDesc.renderTargetFormats = {RHI::EPixelFormat::R8G8B8A8_UNORM};
pipelineDesc.depthStencilFormat = RHI::EPixelFormat::D32_FLOAT;
pipelineDesc.debugName = "Triangle Pipeline";

// 创建管道状态
auto pipelineState = device->createPipelineState(pipelineDesc);
if (!pipelineState) {
    MR_LOG_ERROR("Failed to create pipeline state");
    return false;
}
```

## 渲染命令记录

### 基本渲染循环
```cpp
// 获取即时命令列表
auto* cmdList = device->getImmediateCommandList();
if (!cmdList) {
    MR_LOG_ERROR("No immediate command list available");
    return;
}

// 开始记录命令
cmdList->begin();
{
    MR_SCOPED_DEBUG_EVENT(cmdList, "Main Render Pass");
    
    // 设置视口
    RHI::Viewport viewport(1920.0f, 1080.0f);
    cmdList->setViewport(viewport);
    
    // 设置渲染目标
    cmdList->setRenderTargets({colorTarget}, depthBuffer);
    
    // 清除渲染目标
    const float clearColor[4] = {0.2f, 0.3f, 0.4f, 1.0f}; // 深蓝色背景
    cmdList->clearRenderTarget(colorTarget, clearColor);
    cmdList->clearDepthStencil(depthBuffer, true, false, 1.0f, 0);
    
    // 绘制三角形
    {
        MR_SCOPED_DEBUG_EVENT(cmdList, "Draw Triangle");
        
        cmdList->setPipelineState(pipelineState);
        cmdList->setVertexBuffers(0, {vertexBuffer});
        cmdList->draw(3); // 绘制3个顶点
    }
}
cmdList->end();

// 执行命令并呈现
device->executeCommandLists({});  // 即时命令列表会自动执行
device->present();
```

### 多命令列表渲染
```cpp
// 创建多个命令列表进行并行记录
auto shadowPassCmdList = device->createCommandList();
auto geometryPassCmdList = device->createCommandList();
auto lightingPassCmdList = device->createCommandList();

// 并行记录不同的渲染过程
std::thread shadowThread([&]() {
    shadowPassCmdList->begin();
    {
        MR_SCOPED_DEBUG_EVENT(shadowPassCmdList.get(), "Shadow Pass");
        // ... 阴影渲染命令 ...
    }
    shadowPassCmdList->end();
});

std::thread geometryThread([&]() {
    geometryPassCmdList->begin();
    {
        MR_SCOPED_DEBUG_EVENT(geometryPassCmdList.get(), "Geometry Pass");
        // ... 几何渲染命令 ...
    }
    geometryPassCmdList->end();
});

// 等待命令记录完成
shadowThread.join();
geometryThread.join();

// 按顺序执行命令列表
lightingPassCmdList->begin();
{
    MR_SCOPED_DEBUG_EVENT(lightingPassCmdList.get(), "Lighting Pass");
    // ... 光照渲染命令 ...
}
lightingPassCmdList->end();

// 批量提交执行
device->executeCommandLists({shadowPassCmdList, geometryPassCmdList, lightingPassCmdList});
device->present();
```

## 错误处理和调试

### 资源验证
```cpp
// 检查资源创建是否成功
auto buffer = device->createBuffer(bufferDesc);
if (!buffer) {
    MR_LOG_ERROR("Failed to create buffer: " + bufferDesc.debugName);
    return false;
}

// 验证资源属性
MR_ASSERT(buffer->getSize() == bufferDesc.size);
MR_ASSERT((buffer->getUsage() & bufferDesc.usage) == bufferDesc.usage);

// 设置调试名称便于调试工具识别
buffer->setDebugName("My Important Buffer");
```

### 内存监控
```cpp
// 定期检查内存使用情况
uint64 usedBytes, availableBytes;
device->getMemoryStats(usedBytes, availableBytes);

MR_LOG_INFO("GPU Memory - Used: " + std::to_string(usedBytes / 1024 / 1024) + " MB, "
           "Available: " + std::to_string(availableBytes / 1024 / 1024) + " MB");

// 内存不足时触发垃圾回收
if (availableBytes < 100 * 1024 * 1024) { // 少于100MB
    MR_LOG_WARNING("Low GPU memory, triggering garbage collection");
    device->collectGarbage();
}
```

### 调试标记使用
```cpp
// 使用调试事件组织渲染代码
cmdList->beginEvent("Scene Rendering");
{
    cmdList->beginEvent("Opaque Objects");
    // ... 渲染不透明物体 ...
    cmdList->endEvent();
    
    cmdList->beginEvent("Transparent Objects");
    // ... 渲染透明物体 ...
    cmdList->endEvent();
    
    cmdList->beginEvent("UI Rendering");
    // ... 渲染UI ...
    cmdList->endEvent();
}
cmdList->endEvent();

// 使用RAII辅助宏简化代码
{
    MR_SCOPED_DEBUG_EVENT(cmdList, "Post Processing");
    
    // ... 后处理渲染 ...
    
    // 自动调用endEvent()
}
```

## 资源生命周期管理

### 智能指针最佳实践
```cpp
class Renderer {
    // 使用TSharedPtr管理共享资源
    TSharedPtr<RHI::IRHITexture> m_colorTarget;
    TSharedPtr<RHI::IRHITexture> m_depthBuffer;
    
    // 使用TArray存储相关资源
    TArray<TSharedPtr<RHI::IRHIBuffer>> m_vertexBuffers;
    TArray<TSharedPtr<RHI::IRHIPipelineState>> m_pipelineStates;
    
public:
    void initialize(RHI::IRHIDevice* device) {
        // 创建共享的渲染目标
        RHI::TextureDesc desc(1920, 1080, RHI::EPixelFormat::R8G8B8A8_UNORM, 
                              RHI::EResourceUsage::RenderTarget);
        m_colorTarget = device->createTexture(desc);
        
        // 资源可以在多个地方共享使用
        m_mainFramebuffer->setColorTarget(m_colorTarget);
        m_postProcessor->setInputTexture(m_colorTarget);
    }
    
    void cleanup() {
        // 智能指针自动管理资源释放
        // 显式重置以控制释放顺序
        m_vertexBuffers.clear();
        m_pipelineStates.clear();
        m_depthBuffer.reset();
        m_colorTarget.reset();
    }
};
```

### 资源缓存管理
```cpp
class ResourceManager {
    TMap<String, TSharedPtr<RHI::IRHITexture>> m_textureCache;
    TMap<String, TSharedPtr<RHI::IRHIBuffer>> m_bufferCache;
    
public:
    TSharedPtr<RHI::IRHITexture> getTexture(const String& name) {
        auto it = m_textureCache.find(name);
        if (it != m_textureCache.end()) {
            return it->second; // 返回缓存的纹理
        }
        
        // 加载新纹理
        auto texture = loadTextureFromFile(name);
        if (texture) {
            m_textureCache[name] = texture;
        }
        return texture;
    }
    
    void clearUnusedResources() {
        // 清理只有缓存持有引用的资源
        for (auto it = m_textureCache.begin(); it != m_textureCache.end();) {
            if (it->second.use_count() == 1) { // 只有缓存持有引用
                it = m_textureCache.erase(it);
            } else {
                ++it;
            }
        }
    }
};
```

## 性能优化建议

### 批处理渲染
```cpp
// 收集所有渲染对象
TArray<RenderObject> opaqueObjects;
TArray<RenderObject> transparentObjects;

// 按材质排序减少状态切换
std::sort(opaqueObjects.begin(), opaqueObjects.end(), 
         [](const RenderObject& a, const RenderObject& b) {
             return a.materialId < b.materialId;
         });

// 批量渲染相同材质的物体
uint32 currentMaterialId = UINT32_MAX;
for (const auto& object : opaqueObjects) {
    if (object.materialId != currentMaterialId) {
        cmdList->setPipelineState(object.material->getPipelineState());
        currentMaterialId = object.materialId;
    }
    
    cmdList->setVertexBuffers(0, {object.vertexBuffer});
    cmdList->setIndexBuffer(object.indexBuffer);
    cmdList->drawIndexed(object.indexCount);
}
```

### 资源重用
```cpp
// 重用命令列表避免重复分配
class FrameCommandLists {
    TArray<TSharedPtr<RHI::IRHICommandList>> m_commandLists;
    uint32 m_currentIndex = 0;
    
public:
    TSharedPtr<RHI::IRHICommandList> getNextCommandList(RHI::IRHIDevice* device) {
        if (m_currentIndex >= m_commandLists.size()) {
            m_commandLists.push_back(device->createCommandList());
        }
        
        auto cmdList = m_commandLists[m_currentIndex++];
        cmdList->reset(); // 重置而非重新创建
        return cmdList;
    }
    
    void resetFrame() {
        m_currentIndex = 0; // 下一帧重新开始使用
    }
};
```

---

## 相关文档
- [创建简单三角形](RenderTriangle.md)
- [自定义RHI后端](CustomBackend.md)
- [RHI接口详解](../RHI/RHIOverview.md)
- [Vulkan后端详解](../Vulkan/VulkanOverview.md)
