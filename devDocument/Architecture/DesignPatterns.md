# 设计模式和原则

MonsterRender引擎采用多种设计模式和原则，确保代码的可维护性、可扩展性和性能。

## 核心设计原则

### SOLID原则

#### 单一职责原则 (SRP)
每个类只负责一个功能领域：

```cpp
// 好的设计：职责分离
class Logger {        // 只负责日志记录
    void log(ELogLevel level, const String& message);
};

class FileManager {   // 只负责文件操作  
    TArray<uint8> readFile(const String& path);
    bool writeFile(const String& path, TSpan<const uint8> data);
};

// 避免：多重职责
class LoggerAndFileManager { // 违反SRP
    void log(ELogLevel level, const String& message);
    TArray<uint8> readFile(const String& path);
};
```

#### 开闭原则 (OCP)
对扩展开放，对修改关闭：

```cpp
// 基础接口：对修改关闭
class IRHIDevice {
public:
    virtual ~IRHIDevice() = default;
    virtual TSharedPtr<IRHIBuffer> createBuffer(const BufferDesc& desc) = 0;
};

// 扩展实现：对扩展开放
class VulkanDevice : public IRHIDevice {
    TSharedPtr<IRHIBuffer> createBuffer(const BufferDesc& desc) override;
};

class D3D12Device : public IRHIDevice {
    TSharedPtr<IRHIBuffer> createBuffer(const BufferDesc& desc) override;  
};
```

#### 依赖倒置原则 (DIP)
依赖抽象而非具体实现：

```cpp
// 高层模块依赖抽象
class Renderer {
    IRHIDevice* m_device; // 依赖抽象接口，而非具体实现
public:
    Renderer(IRHIDevice* device) : m_device(device) {}
    
    void render() {
        auto buffer = m_device->createBuffer(desc); // 不关心具体实现
    }
};
```

### RAII原则
资源获取即初始化，确保资源正确管理：

```cpp
class VulkanBuffer : public IRHIBuffer {
public:
    VulkanBuffer(VulkanDevice* device, const BufferDesc& desc) {
        // 构造时获取资源
        vkCreateBuffer(device->getDevice(), &bufferInfo, nullptr, &m_buffer);
        vkAllocateMemory(device->getDevice(), &allocInfo, nullptr, &m_memory);
        vkBindBufferMemory(device->getDevice(), m_buffer, m_memory, 0);
    }
    
    ~VulkanBuffer() {
        // 析构时自动释放资源
        if (m_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_device->getDevice(), m_buffer, nullptr);
        }
        if (m_memory != VK_NULL_HANDLE) {
            vkFreeMemory(m_device->getDevice(), m_memory, nullptr);
        }
    }
};
```

## 设计模式应用

### 工厂模式
用于创建不同RHI后端：

```cpp
class RHIFactory {
public:
    static TUniquePtr<IRHIDevice> createDevice(const RHICreateInfo& createInfo) {
        switch (createInfo.preferredBackend) {
            case ERHIBackend::Vulkan:
                return MakeUnique<VulkanDevice>();
            case ERHIBackend::D3D12:
                return MakeUnique<D3D12Device>();
            default:
                return nullptr;
        }
    }
};
```

### 抽象工厂模式
用于创建一系列相关对象：

```cpp
class IRHIDevice {
public:
    // 抽象工厂：创建一系列相关的RHI对象
    virtual TSharedPtr<IRHIBuffer> createBuffer(const BufferDesc& desc) = 0;
    virtual TSharedPtr<IRHITexture> createTexture(const TextureDesc& desc) = 0;
    virtual TSharedPtr<IRHICommandList> createCommandList() = 0;
    virtual TSharedPtr<IRHIPipelineState> createPipelineState(const PipelineStateDesc& desc) = 0;
};

// 具体工厂：Vulkan对象家族
class VulkanDevice : public IRHIDevice {
    TSharedPtr<IRHIBuffer> createBuffer(const BufferDesc& desc) override {
        return MakeShared<VulkanBuffer>(this, desc);
    }
    // ... 其他Vulkan对象创建
};
```

### 命令模式
RHI命令列表的实现：

```cpp
// 命令接口
class IRenderCommand {
public:
    virtual ~IRenderCommand() = default;
    virtual void execute(VkCommandBuffer cmdBuffer) = 0;
};

// 具体命令
class DrawCommand : public IRenderCommand {
    uint32 m_vertexCount;
public:
    DrawCommand(uint32 vertexCount) : m_vertexCount(vertexCount) {}
    
    void execute(VkCommandBuffer cmdBuffer) override {
        vkCmdDraw(cmdBuffer, m_vertexCount, 1, 0, 0);
    }
};

// 命令列表：命令调用者
class VulkanCommandList {
    TArray<TUniquePtr<IRenderCommand>> m_commands;
public:
    void draw(uint32 vertexCount) {
        m_commands.push_back(MakeUnique<DrawCommand>(vertexCount));
    }
    
    void execute() {
        for (const auto& cmd : m_commands) {
            cmd->execute(m_commandBuffer);
        }
    }
};
```

### 单例模式
用于全局唯一的系统：

```cpp
// 线程安全的单例
class Logger {
public:
    static Logger& getInstance() {
        static Logger instance; // C++11保证线程安全
        return instance;
    }
    
    void log(ELogLevel level, const String& message);
    
private:
    Logger() = default; // 私有构造函数
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
};
```

### 策略模式
用于可插拔的算法：

```cpp
// 策略接口
class IMemoryAllocator {
public:
    virtual ~IMemoryAllocator() = default;
    virtual void* allocate(size_t size) = 0;
    virtual void deallocate(void* ptr) = 0;
};

// 具体策略
class LinearAllocator : public IMemoryAllocator {
    void* allocate(size_t size) override { /* 线性分配 */ }
    void deallocate(void* ptr) override { /* 线性释放 */ }
};

class PoolAllocator : public IMemoryAllocator {
    void* allocate(size_t size) override { /* 池分配 */ }
    void deallocate(void* ptr) override { /* 池释放 */ }
};

// 使用策略的上下文
class VulkanDevice {
    TUniquePtr<IMemoryAllocator> m_allocator;
public:
    void setMemoryAllocator(TUniquePtr<IMemoryAllocator> allocator) {
        m_allocator = std::move(allocator);
    }
    
    void* allocateMemory(size_t size) {
        return m_allocator->allocate(size);
    }
};
```

### 观察者模式
用于事件通知：

```cpp
// 观察者接口
class IResourceObserver {
public:
    virtual ~IResourceObserver() = default;
    virtual void onResourceCreated(IRHIResource* resource) = 0;
    virtual void onResourceDestroyed(IRHIResource* resource) = 0;
};

// 被观察者
class ResourceManager {
    TArray<IResourceObserver*> m_observers;
    
public:
    void addObserver(IResourceObserver* observer) {
        m_observers.push_back(observer);
    }
    
    void removeObserver(IResourceObserver* observer) {
        m_observers.erase(std::find(m_observers.begin(), m_observers.end(), observer));
    }
    
private:
    void notifyResourceCreated(IRHIResource* resource) {
        for (auto* observer : m_observers) {
            observer->onResourceCreated(resource);
        }
    }
};

// 具体观察者
class MemoryTracker : public IResourceObserver {
    void onResourceCreated(IRHIResource* resource) override {
        m_totalMemory += resource->getSize();
        MR_LOG_DEBUG("Resource created, total memory: " + std::to_string(m_totalMemory));
    }
    
private:
    uint64 m_totalMemory = 0;
};
```

### PIMPL模式
用于隐藏实现细节：

```cpp
// 公共头文件
class VulkanDevice : public IRHIDevice {
public:
    VulkanDevice();
    ~VulkanDevice();
    
    bool initialize(const RHICreateInfo& createInfo);
    // ... 公共接口
    
private:
    class Impl; // 前向声明
    TUniquePtr<Impl> m_impl; // 指向实现的指针
};

// 私有实现文件
class VulkanDevice::Impl {
public:
    // 真正的Vulkan实现细节
    VkInstance instance;
    VkDevice device;
    VkPhysicalDevice physicalDevice;
    // ... 其他Vulkan对象
    
    bool initializeImpl(const RHICreateInfo& createInfo);
};

VulkanDevice::VulkanDevice() : m_impl(MakeUnique<Impl>()) {}
VulkanDevice::~VulkanDevice() = default;

bool VulkanDevice::initialize(const RHICreateInfo& createInfo) {
    return m_impl->initializeImpl(createInfo);
}
```

## 内存管理模式

### 智能指针策略
```cpp
// 所有权清晰的智能指针使用
class ResourceCache {
    TMap<String, TSharedPtr<IRHITexture>> m_textureCache;     // 共享所有权
    TUniquePtr<IMemoryAllocator> m_allocator;                // 独占所有权
    TWeakPtr<IRHIDevice> m_device;                           // 非拥有引用
};
```

### 对象生命周期管理
```cpp
// 明确的生命周期管理
class Renderer {
public:
    bool initialize(IRHIDevice* device) {
        m_device = device; // 非拥有指针
        
        // 创建生命周期绑定到Renderer的资源
        BufferDesc desc(1024, EResourceUsage::VertexBuffer);
        m_vertexBuffer = device->createBuffer(desc); // 拥有所有权
        
        return m_vertexBuffer != nullptr;
    }
    
    ~Renderer() {
        // 智能指针自动清理，或者显式清理
        m_vertexBuffer.reset();
    }
    
private:
    IRHIDevice* m_device;                        // 不拥有
    TSharedPtr<IRHIBuffer> m_vertexBuffer;       // 拥有
};
```

## 错误处理模式

### 异常安全保证
```cpp
class VulkanDevice {
public:
    TSharedPtr<IRHIBuffer> createBuffer(const BufferDesc& desc) {
        auto buffer = MakeShared<VulkanBuffer>(this, desc);
        
        // 强异常安全保证：要么完全成功，要么完全失败
        try {
            buffer->initialize(desc);
            return buffer;
        } catch (...) {
            // buffer会被自动析构，不会泄漏资源
            return nullptr;
        }
    }
};
```

### 错误码 vs 异常
```cpp
// 性能关键路径：使用错误码
enum class EResult {
    Success,
    OutOfMemory,
    InvalidParameter,
    DeviceLost
};

EResult VulkanCommandList::draw(uint32 vertexCount) {
    if (vertexCount == 0) {
        return EResult::InvalidParameter;
    }
    
    // 直接调用Vulkan API，不抛异常
    vkCmdDraw(m_commandBuffer, vertexCount, 1, 0, 0);
    return EResult::Success;
}

// 初始化路径：可以使用异常
VulkanDevice::VulkanDevice() {
    if (!VulkanAPI::initialize()) {
        throw std::runtime_error("Failed to initialize Vulkan API");
    }
}
```

## 性能优化模式

### 对象池
```cpp
template<typename T>
class ObjectPool {
    TArray<TUniquePtr<T>> m_available;
    TArray<TUniquePtr<T>> m_inUse;
    
public:
    TUniquePtr<T> acquire() {
        if (m_available.empty()) {
            return MakeUnique<T>();
        }
        
        auto obj = std::move(m_available.back());
        m_available.pop_back();
        return obj;
    }
    
    void release(TUniquePtr<T> obj) {
        if (obj) {
            obj->reset(); // 重置对象状态
            m_available.push_back(std::move(obj));
        }
    }
};

// 使用对象池管理命令列表
class VulkanDevice {
    ObjectPool<VulkanCommandList> m_commandListPool;
    
public:
    TSharedPtr<IRHICommandList> createCommandList() override {
        auto cmdList = m_commandListPool.acquire();
        return TSharedPtr<IRHICommandList>(cmdList.release(), 
            [this](IRHICommandList* ptr) {
                m_commandListPool.release(TUniquePtr<VulkanCommandList>(
                    static_cast<VulkanCommandList*>(ptr)));
            });
    }
};
```

### 延迟初始化
```cpp
class TextureManager {
    mutable TMap<String, TSharedPtr<IRHITexture>> m_textureCache;
    
public:
    const TSharedPtr<IRHITexture>& getTexture(const String& name) const {
        auto it = m_textureCache.find(name);
        if (it == m_textureCache.end()) {
            // 延迟加载：只有在需要时才加载
            auto texture = loadTextureFromFile(name);
            it = m_textureCache.emplace(name, texture).first;
        }
        return it->second;
    }
};
```

---

## 相关文档
- [总体架构设计](OverallArchitecture.md)
- [模块依赖关系](ModuleDependencies.md)
- [核心类型系统](../Core/CoreTypes.md)
