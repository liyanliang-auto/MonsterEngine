# RHI资源管理详解

RHI资源管理系统负责GPU资源的创建、生命周期管理和内存优化，是渲染引擎的核心组件。

## 资源层次结构

### 基础资源类
```cpp
class IRHIResource {
public:
    virtual ~IRHIResource() = default;
    
    // 基本属性
    const String& getDebugName() const;
    void setDebugName(const String& name);
    virtual uint32 getSize() const = 0;
    virtual EResourceUsage getUsage() const = 0;
    
    // 生命周期管理
    uint32 getRefCount() const { return m_refCount.load(); }
    
protected:
    String m_debugName;
    mutable std::atomic<uint32> m_refCount{0};
};
```

### 缓冲区资源
```cpp
class IRHIBuffer : public IRHIResource {
public:
    IRHIBuffer(const BufferDesc& desc);
    
    // 内存访问
    virtual void* map() = 0;
    virtual void unmap() = 0;
    virtual bool isMapped() const = 0;
    
    // 数据操作
    virtual void updateData(TSpan<const uint8> data, uint32 offset = 0) = 0;
    virtual void copyFrom(const IRHIBuffer* source, uint32 srcOffset = 0, uint32 dstOffset = 0, uint32 size = 0) = 0;
    
    // 属性查询
    const BufferDesc& getDesc() const { return m_desc; }
    bool isVertexBuffer() const { return (m_desc.usage & EResourceUsage::VertexBuffer) != 0; }
    bool isIndexBuffer() const { return (m_desc.usage & EResourceUsage::IndexBuffer) != 0; }
    bool isUniformBuffer() const { return (m_desc.usage & EResourceUsage::UniformBuffer) != 0; }
    
protected:
    BufferDesc m_desc;
    void* m_mappedData = nullptr;
};
```

### 纹理资源
```cpp
class IRHITexture : public IRHIResource {
public:
    IRHITexture(const TextureDesc& desc);
    
    // 纹理操作
    virtual void updateData(TSpan<const uint8> data, uint32 mipLevel = 0, uint32 arraySlice = 0) = 0;
    virtual void generateMips() = 0;
    virtual void copyFrom(const IRHITexture* source, uint32 srcMip = 0, uint32 dstMip = 0) = 0;
    
    // 视图创建
    virtual TSharedPtr<IRHITextureView> createView(const TextureViewDesc& desc) = 0;
    virtual TSharedPtr<IRHITextureView> createRenderTargetView(uint32 mipLevel = 0, uint32 arraySlice = 0) = 0;
    virtual TSharedPtr<IRHITextureView> createDepthStencilView(uint32 mipLevel = 0, uint32 arraySlice = 0) = 0;
    
    // 属性查询
    const TextureDesc& getDesc() const { return m_desc; }
    bool isRenderTarget() const { return (m_desc.usage & EResourceUsage::RenderTarget) != 0; }
    bool isDepthStencil() const { return (m_desc.usage & EResourceUsage::DepthStencil) != 0; }
    
protected:
    TextureDesc m_desc;
    TArray<TSharedPtr<IRHITextureView>> m_views;
};
```

## 资源创建和描述

### 缓冲区创建
```cpp
// 顶点缓冲区
BufferDesc vertexBufferDesc;
vertexBufferDesc.size = sizeof(Vertex) * vertexCount;
vertexBufferDesc.usage = EResourceUsage::VertexBuffer;
vertexBufferDesc.cpuAccessible = true; // CPU可写
vertexBufferDesc.debugName = "Mesh Vertex Buffer";

auto vertexBuffer = device->createBuffer(vertexBufferDesc);

// 统一缓冲区 (Uniform Buffer)
BufferDesc uniformBufferDesc;
uniformBufferDesc.size = sizeof(ViewProjectionMatrix);
uniformBufferDesc.usage = EResourceUsage::UniformBuffer;
uniformBufferDesc.cpuAccessible = true; // 每帧更新
uniformBufferDesc.debugName = "View Projection UBO";

auto uniformBuffer = device->createBuffer(uniformBufferDesc);
```

### 纹理创建
```cpp
// 2D纹理
TextureDesc textureDesc;
textureDesc.width = 1024;
textureDesc.height = 1024;
textureDesc.mipLevels = 0; // 自动计算mip级别数
textureDesc.format = EPixelFormat::R8G8B8A8_UNORM;
textureDesc.usage = EResourceUsage::ShaderResource | EResourceUsage::TransferDst;
textureDesc.debugName = "Diffuse Texture";

auto texture = device->createTexture(textureDesc);

// 渲染目标
TextureDesc renderTargetDesc;
renderTargetDesc.width = 1920;
renderTargetDesc.height = 1080;
renderTargetDesc.format = EPixelFormat::R8G8B8A8_UNORM;
renderTargetDesc.usage = EResourceUsage::RenderTarget | EResourceUsage::ShaderResource;
renderTargetDesc.debugName = "Main Color Target";

auto colorTarget = device->createTexture(renderTargetDesc);

// 立方体贴图
TextureDesc cubeMapDesc;
cubeMapDesc.width = 512;
cubeMapDesc.height = 512;
cubeMapDesc.arraySize = 6; // 6个面
cubeMapDesc.format = EPixelFormat::R16G16B16A16_FLOAT;
cubeMapDesc.usage = EResourceUsage::ShaderResource;
cubeMapDesc.debugName = "Environment Cube Map";

auto cubeMap = device->createTexture(cubeMapDesc);
```

## 内存管理策略

### 智能指针使用
```cpp
// 资源共享：多个对象可以引用同一资源
class Material {
    TSharedPtr<IRHITexture> m_diffuseTexture;   // 可能被多个材质共享
    TSharedPtr<IRHITexture> m_normalTexture;    // 可能被多个材质共享
    TSharedPtr<IRHIBuffer> m_materialBuffer;    // 材质特有数据
};

// 独占资源：只有一个所有者
class RenderTarget {
    TUniquePtr<IRHITexture> m_colorTexture;     // 独占所有权
    TUniquePtr<IRHITexture> m_depthTexture;     // 独占所有权
};

// 弱引用：避免循环依赖
class TextureView {
    TWeakPtr<IRHITexture> m_parentTexture;      // 不延长父纹理生命周期
};
```

### 资源池管理
```cpp
template<typename ResourceType, typename DescType>
class ResourcePool {
public:
    TSharedPtr<ResourceType> acquire(const DescType& desc) {
        // 查找可重用的资源
        for (auto& resource : m_availableResources) {
            if (resource->isCompatible(desc)) {
                m_availableResources.erase(
                    std::find(m_availableResources.begin(), m_availableResources.end(), resource));
                return resource;
            }
        }
        
        // 没有找到，创建新资源
        auto newResource = m_device->createResource<ResourceType>(desc);
        return newResource;
    }
    
    void release(TSharedPtr<ResourceType> resource) {
        if (resource && resource.use_count() == 1) {
            resource->reset(); // 重置资源状态
            m_availableResources.push_back(resource);
        }
    }
    
private:
    IRHIDevice* m_device;
    TArray<TSharedPtr<ResourceType>> m_availableResources;
};

// 使用资源池
class ResourceManager {
    ResourcePool<IRHIBuffer, BufferDesc> m_bufferPool;
    ResourcePool<IRHITexture, TextureDesc> m_texturePool;
    
public:
    TSharedPtr<IRHIBuffer> acquireBuffer(const BufferDesc& desc) {
        return m_bufferPool.acquire(desc);
    }
    
    void releaseBuffer(TSharedPtr<IRHIBuffer> buffer) {
        m_bufferPool.release(buffer);
    }
};
```

## 数据上传和更新

### 缓冲区数据操作
```cpp
void updateVertexBuffer(TSharedPtr<IRHIBuffer> buffer, TSpan<const Vertex> vertices) {
    // 方法1：映射内存直接写入（适合CPU可访问的缓冲区）
    if (buffer->getDesc().cpuAccessible) {
        if (void* mappedData = buffer->map()) {
            std::memcpy(mappedData, vertices.data(), vertices.size_bytes());
            buffer->unmap();
        }
    } 
    // 方法2：使用更新函数（内部可能创建临时缓冲区）
    else {
        buffer->updateData(TSpan<const uint8>(
            reinterpret_cast<const uint8*>(vertices.data()), 
            vertices.size_bytes()));
    }
}

// 大数据异步上传
void uploadLargeBuffer(TSharedPtr<IRHIBuffer> targetBuffer, TSpan<const uint8> data) {
    // 创建临时上传缓冲区
    BufferDesc stagingDesc;
    stagingDesc.size = data.size_bytes();
    stagingDesc.usage = EResourceUsage::TransferSrc;
    stagingDesc.cpuAccessible = true;
    stagingDesc.debugName = "Staging Buffer";
    
    auto stagingBuffer = device->createBuffer(stagingDesc);
    
    // 将数据写入临时缓冲区
    if (void* mappedData = stagingBuffer->map()) {
        std::memcpy(mappedData, data.data(), data.size_bytes());
        stagingBuffer->unmap();
    }
    
    // 异步复制到目标缓冲区
    auto copyCommandList = device->createCommandList();
    copyCommandList->begin();
    copyCommandList->copyBuffer(stagingBuffer.get(), targetBuffer.get());
    copyCommandList->end();
    
    device->executeCommandLists({copyCommandList});
    // stagingBuffer会在适当时机自动释放
}
```

### 纹理数据操作
```cpp
void loadTextureFromFile(TSharedPtr<IRHITexture> texture, const String& filePath) {
    // 加载图像数据（假设有图像加载库）
    ImageData imageData = loadImage(filePath);
    
    // 检查格式兼容性
    MR_ASSERT(imageData.format == texture->getDesc().format);
    MR_ASSERT(imageData.width == texture->getDesc().width);
    MR_ASSERT(imageData.height == texture->getDesc().height);
    
    // 上传纹理数据
    texture->updateData(TSpan<const uint8>(imageData.pixels, imageData.size));
    
    // 如果需要，生成mip map
    if (texture->getDesc().mipLevels > 1) {
        texture->generateMips();
    }
}

// 立方体贴图加载
void loadCubeMap(TSharedPtr<IRHITexture> cubeTexture, const TArray<String>& faceFiles) {
    MR_ASSERT(faceFiles.size() == 6);
    MR_ASSERT(cubeTexture->getDesc().arraySize == 6);
    
    for (uint32 face = 0; face < 6; ++face) {
        ImageData faceData = loadImage(faceFiles[face]);
        cubeTexture->updateData(
            TSpan<const uint8>(faceData.pixels, faceData.size), 
            0, // mip level 0
            face // array slice (cube face)
        );
    }
}
```

## 资源状态管理

### 资源状态跟踪
```cpp
enum class EResourceState {
    Unknown,
    VertexAndConstantBuffer,
    IndexBuffer,
    RenderTarget,
    UnorderedAccess,
    DepthWrite,
    DepthRead,
    ShaderResource,
    StreamOut,
    IndirectArgument,
    CopyDest,
    CopySource,
    Present
};

class ResourceStateTracker {
    TMap<IRHIResource*, EResourceState> m_resourceStates;
    
public:
    void transitionResource(IRHICommandList* cmdList, IRHIResource* resource, 
                          EResourceState newState) {
        auto currentState = getCurrentState(resource);
        if (currentState != newState) {
            cmdList->transitionResource(resource, currentState, newState);
            m_resourceStates[resource] = newState;
        }
    }
    
private:
    EResourceState getCurrentState(IRHIResource* resource) {
        auto it = m_resourceStates.find(resource);
        return (it != m_resourceStates.end()) ? it->second : EResourceState::Unknown;
    }
};
```

### 自动状态管理
```cpp
class SmartResourceBinding {
    IRHICommandList* m_commandList;
    ResourceStateTracker* m_stateTracker;
    
public:
    void setRenderTarget(TSharedPtr<IRHITexture> colorTarget, 
                        TSharedPtr<IRHITexture> depthTarget = nullptr) {
        // 自动转换资源状态
        if (colorTarget) {
            m_stateTracker->transitionResource(m_commandList, colorTarget.get(), 
                                             EResourceState::RenderTarget);
        }
        
        if (depthTarget) {
            m_stateTracker->transitionResource(m_commandList, depthTarget.get(), 
                                             EResourceState::DepthWrite);
        }
        
        // 设置渲染目标
        m_commandList->setRenderTargets(colorTarget ? TArray{colorTarget} : TArray<TSharedPtr<IRHITexture>>{}, 
                                       depthTarget);
    }
    
    void setShaderResource(uint32 slot, TSharedPtr<IRHITexture> texture) {
        if (texture) {
            m_stateTracker->transitionResource(m_commandList, texture.get(), 
                                             EResourceState::ShaderResource);
            // 绑定着色器资源...
        }
    }
};
```

## 资源缓存和优化

### 资源缓存系统
```cpp
class ResourceCache {
public:
    template<typename ResourceType, typename DescType>
    TSharedPtr<ResourceType> getOrCreate(const String& name, const DescType& desc) {
        auto it = m_cache.find(name);
        if (it != m_cache.end()) {
            // 检查类型匹配
            if (auto resource = std::dynamic_pointer_cast<ResourceType>(it->second)) {
                return resource;
            }
        }
        
        // 创建新资源
        auto resource = m_device->create<ResourceType>(desc);
        resource->setDebugName(name);
        
        m_cache[name] = resource;
        return resource;
    }
    
    void removeUnused() {
        for (auto it = m_cache.begin(); it != m_cache.end();) {
            if (it->second.use_count() == 1) { // 只有缓存持有引用
                MR_LOG_DEBUG("Removing unused resource: " + it->first);
                it = m_cache.erase(it);
            } else {
                ++it;
            }
        }
    }
    
private:
    TMap<String, TSharedPtr<IRHIResource>> m_cache;
    IRHIDevice* m_device;
};
```

### 内存预算管理
```cpp
class MemoryBudgetManager {
public:
    struct MemoryBudget {
        uint64 maxTextureMemory = 1024 * 1024 * 1024; // 1GB
        uint64 maxBufferMemory = 256 * 1024 * 1024;   // 256MB
        uint64 maxRenderTargetMemory = 512 * 1024 * 1024; // 512MB
    };
    
    bool canAllocate(EResourceUsage usage, uint64 size) const {
        switch (usage) {
            case EResourceUsage::ShaderResource:
                return m_currentTextureMemory + size <= m_budget.maxTextureMemory;
            case EResourceUsage::VertexBuffer:
            case EResourceUsage::IndexBuffer:
            case EResourceUsage::UniformBuffer:
                return m_currentBufferMemory + size <= m_budget.maxBufferMemory;
            case EResourceUsage::RenderTarget:
            case EResourceUsage::DepthStencil:
                return m_currentRenderTargetMemory + size <= m_budget.maxRenderTargetMemory;
            default:
                return true;
        }
    }
    
    void onResourceCreated(IRHIResource* resource) {
        uint64 size = resource->getSize();
        auto usage = resource->getUsage();
        
        if (usage & EResourceUsage::ShaderResource) {
            m_currentTextureMemory += size;
        }
        if (usage & (EResourceUsage::VertexBuffer | EResourceUsage::IndexBuffer | EResourceUsage::UniformBuffer)) {
            m_currentBufferMemory += size;
        }
        if (usage & (EResourceUsage::RenderTarget | EResourceUsage::DepthStencil)) {
            m_currentRenderTargetMemory += size;
        }
        
        MR_LOG_DEBUG("Memory usage - Textures: " + std::to_string(m_currentTextureMemory / 1024 / 1024) + "MB, "
                    "Buffers: " + std::to_string(m_currentBufferMemory / 1024 / 1024) + "MB, "
                    "RenderTargets: " + std::to_string(m_currentRenderTargetMemory / 1024 / 1024) + "MB");
    }
    
private:
    MemoryBudget m_budget;
    uint64 m_currentTextureMemory = 0;
    uint64 m_currentBufferMemory = 0;
    uint64 m_currentRenderTargetMemory = 0;
};
```

## 调试和验证

### 资源泄漏检测
```cpp
class ResourceLeakDetector {
    static std::atomic<uint32> s_totalResourceCount;
    static TMap<IRHIResource*, String> s_activeResources;
    static std::mutex s_mutex;
    
public:
    static void onResourceCreated(IRHIResource* resource, const String& debugName) {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_activeResources[resource] = debugName;
        s_totalResourceCount.fetch_add(1);
        
        MR_LOG_TRACE("Resource created: " + debugName + " (Total: " + std::to_string(s_totalResourceCount.load()) + ")");
    }
    
    static void onResourceDestroyed(IRHIResource* resource) {
        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = s_activeResources.find(resource);
        if (it != s_activeResources.end()) {
            MR_LOG_TRACE("Resource destroyed: " + it->second);
            s_activeResources.erase(it);
            s_totalResourceCount.fetch_sub(1);
        }
    }
    
    static void checkLeaks() {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (!s_activeResources.empty()) {
            MR_LOG_ERROR("Resource leaks detected:");
            for (const auto& pair : s_activeResources) {
                MR_LOG_ERROR("  - " + pair.second);
            }
        }
    }
};
```

### 资源验证
```cpp
void validateBufferUsage(IRHIBuffer* buffer, EResourceUsage expectedUsage) {
    MR_ASSERT(buffer != nullptr);
    
    auto actualUsage = buffer->getUsage();
    MR_ASSERT((actualUsage & expectedUsage) == expectedUsage);
    
    if (expectedUsage & EResourceUsage::VertexBuffer) {
        MR_ASSERT(buffer->getDesc().size % sizeof(float) == 0); // 顶点数据对齐
    }
    
    if (expectedUsage & EResourceUsage::IndexBuffer) {
        auto size = buffer->getDesc().size;
        MR_ASSERT(size % sizeof(uint16) == 0 || size % sizeof(uint32) == 0); // 索引数据对齐
    }
}

void validateTextureFormat(IRHITexture* texture, EPixelFormat expectedFormat) {
    MR_ASSERT(texture != nullptr);
    MR_ASSERT(texture->getDesc().format == expectedFormat);
    
    // 验证尺寸约束
    auto desc = texture->getDesc();
    MR_ASSERT(desc.width > 0 && desc.height > 0 && desc.depth > 0);
    MR_ASSERT(desc.mipLevels > 0 && desc.arraySize > 0);
    
    // 验证mip级别数量
    uint32 maxMips = static_cast<uint32>(std::floor(std::log2(std::max(desc.width, desc.height)))) + 1;
    MR_ASSERT(desc.mipLevels <= maxMips);
}
```

---

## 相关文档
- [RHI概述](RHIOverview.md)
- [设备接口详解](DeviceInterface.md)
- [Vulkan资源实现](../Vulkan/ResourceImplementation.md)
