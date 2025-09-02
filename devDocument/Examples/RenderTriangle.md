# 渲染简单三角形示例

本示例展示如何使用MonsterRender引擎渲染一个简单的彩色三角形。

## 完整示例代码

```cpp
#include "Core/CoreMinimal.h" 
#include "Engine.h"

using namespace MonsterRender;

class TriangleApp {
public:
    bool initialize() {
        // 创建引擎
        RHI::RHICreateInfo createInfo;
        createInfo.preferredBackend = RHI::ERHIBackend::Vulkan;
        createInfo.enableValidation = true;
        createInfo.applicationName = "Triangle Demo";
        
        if (!m_engine.initialize(createInfo)) {
            return false;
        }
        
        // 获取RHI设备
        m_device = m_engine.getRHIDevice();
        
        // 创建资源
        return createResources();
    }
    
    void run() {
        render();
        m_device->present();
    }
    
private:
    bool createResources() {
        // 定义三角形顶点
        struct Vertex {
            float position[3];
            float color[3];
        };
        
        TArray<Vertex> vertices = {
            {{0.0f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}}, // 底部红色
            {{0.5f,  0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}}, // 右上绿色
            {{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}}  // 左上蓝色
        };
        
        // 创建顶点缓冲区
        RHI::BufferDesc bufferDesc;
        bufferDesc.size = sizeof(vertices[0]) * vertices.size();
        bufferDesc.usage = RHI::EResourceUsage::VertexBuffer;
        bufferDesc.cpuAccessible = true;
        
        m_vertexBuffer = m_device->createBuffer(bufferDesc);
        if (!m_vertexBuffer) return false;
        
        // 上传顶点数据
        if (void* data = m_vertexBuffer->map()) {
            memcpy(data, vertices.data(), bufferDesc.size);
            m_vertexBuffer->unmap();
        }
        
        return createShaders() && createPipeline();
    }
    
    void render() {
        auto cmdList = m_device->getImmediateCommandList();
        
        cmdList->begin();
        {
            // 设置视口
            RHI::Viewport viewport(1920.0f, 1080.0f);
            cmdList->setViewport(viewport);
            
            // 设置管道和顶点缓冲区
            cmdList->setPipelineState(m_pipelineState);
            cmdList->setVertexBuffers(0, {m_vertexBuffer});
            
            // 绘制三角形
            cmdList->draw(3);
        }
        cmdList->end();
    }
    
private:
    Engine m_engine;
    RHI::IRHIDevice* m_device = nullptr;
    TSharedPtr<RHI::IRHIBuffer> m_vertexBuffer;
    TSharedPtr<RHI::IRHIPipelineState> m_pipelineState;
};

int main() {
    TriangleApp app;
    if (!app.initialize()) {
        return -1;
    }
    
    app.run();
    return 0;
}
```

## 着色器代码

### 顶点着色器 (triangle.vert)
```glsl
#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;

void main() {
    gl_Position = vec4(inPosition, 1.0);
    fragColor = inColor;
}
```

### 片段着色器 (triangle.frag)
```glsl
#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(fragColor, 1.0);
}
```

## 编译和运行

1. 使用glslc编译着色器：
```bash
glslc triangle.vert -o triangle.vert.spv
glslc triangle.frag -o triangle.frag.spv
```

2. 编译C++代码并运行

预期结果：显示一个彩色三角形，底部红色，右上绿色，左上蓝色。
