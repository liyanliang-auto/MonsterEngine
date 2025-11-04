# Vulkan GPU 内存管理系统 - 四层架构设计

**MonsterEngine - UE5 风格的 GPU 内存管理系统**

---

## 目录

1. [系统概述](#系统概述)
2. [四层架构设计](#四层架构设计)
3. [各层详细说明](#各层详细说明)
4. [类图与架构图](#类图与架构图)
5. [代码实现](#代码实现)
6. [使用示例](#使用示例)
7. [性能优化](#性能优化)
8. [调试与监控](#调试与监控)
9. [下一步开发计划](#下一步开发计划)

---

## 系统概述

MonsterEngine 的 Vulkan GPU 内存管理系统参考 UE5 的显存管理架构，采用**四层分层设计**：

```
┌─────────────────────────────────────────────────────┐
│           ① RHI 层 (Platform-Agnostic)              │
│   FRHIResource, FRHIBuffer, FRHITexture             │
└─────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────┐
│         ② ResourceManager 层 (Logical)              │
│   FVulkanResourceManager, FVulkanBuffer,            │
│   FVulkanTexture                                    │
└─────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────┐
│         ③ PoolManager 层 (Physical Memory)          │
│   FVulkanMemoryPool, FVulkanPoolManager             │
└─────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────┐
│         ④ Vulkan API 层 (Driver)                    │
│   vkAllocateMemory, vkBindBufferMemory,             │
│   vkBindImageMemory                                 │
└─────────────────────────────────────────────────────┘
```

### 设计目标

- **平台无关**：RHI 层提供统一接口，支持多平台扩展
- **内存高效**：通过内存池和子分配减少碎片化
- **线程安全**：支持多线程并发分配和释放
- **调试友好**：详尽的统计信息和调试工具
- **延迟释放**：GPU 资源安全释放，避免 use-after-free

---

## 四层架构设计

### 层级职责划分

| 层级 | 模块 | 职责 | 主要类 |
|------|------|------|--------|
| **① RHI 层** | 平台无关接口 | 定义统一的资源抽象，引用计数管理 | `FRHIResource`, `FRHIBuffer`, `FRHITexture`, `TRefCountPtr` |
| **② ResourceManager 层** | 逻辑资源管理 | 管理具体资源对象的生命周期、延迟释放 | `FVulkanResourceManager`, `FVulkanBuffer`, `FVulkanTexture` |
| **③ PoolManager 层** | 物理内存管理 | 管理大块内存（Pages），子分配策略 | `FVulkanMemoryPool`, `FVulkanPoolManager` |
| **④ Vulkan API 层** | 驱动交互 | 与 Vulkan 驱动交互，分配实际显存 | `vkAllocateMemory`, `vkBindBufferMemory`, `vkBindImageMemory` |

---

## 各层详细说明

### ① RHI 层 (Platform-Agnostic)

**核心类：**

#### `FRHIResource`
```cpp
class FRHIResource {
    std::atomic<uint32> RefCount;  // 引用计数（线程安全）
    bool bCommitted;                // 是否已提交到 GPU
    String DebugName;               // 调试名称
    
public:
    uint32 AddRef() const;
    uint32 Release() const;
    uint32 GetRefCount() const;
};
```

**特点：**
- 所有 RHI 资源的基类
- 使用原子操作实现线程安全的引用计数
- 支持调试名称，方便问题定位

#### `TRefCountPtr<T>`
```cpp
template<typename T>
class TRefCountPtr {
    T* Pointer;
    
public:
    TRefCountPtr(T* InPointer);
    ~TRefCountPtr();
    
    // 自动管理引用计数
};
```

**特点：**
- 智能指针，自动管理 AddRef/Release
- 支持拷贝、移动语义
- 线程安全

#### `FRHIBuffer` / `FRHITexture`
```cpp
class FRHIBuffer : public FRHIResource {
    uint32 Size;
    EResourceUsage Usage;
    uint32 Stride;
    
    virtual void* Lock(uint32 Offset, uint32 Size) = 0;
    virtual void Unlock() = 0;
    virtual uint64 GetGPUVirtualAddress() const { return 0; }
};

class FRHITexture : public FRHIResource {
    TextureDesc Desc;
    
public:
    uint32 GetWidth() const;
    uint32 GetHeight() const;
    uint32 GetMipLevels() const;
    EPixelFormat GetFormat() const;
};
```

**子类：**
- `FRHIVertexBuffer`
- `FRHIIndexBuffer`
- `FRHIUniformBuffer`
- `FRHITexture2D`
- `FRHITextureCube`

---

### ② ResourceManager 层 (Logical Resource)

**核心类：**

#### `FVulkanResourceManager`
```cpp
class FVulkanResourceManager {
    VulkanDevice* Device;
    FVulkanMemoryManager* MemoryManager;
    
    std::vector<FVulkanBuffer*> ActiveBuffers;
    std::vector<FVulkanTexture*> ActiveTextures;
    std::vector<FDeferredReleaseEntry> DeferredReleases;
    
public:
    FRHIBufferRef CreateBuffer(uint32 Size, EResourceUsage Usage, ...);
    FRHITextureRef CreateTexture(const TextureDesc& Desc, ...);
    
    void DeferredRelease(FRHIResource* Resource, uint64 FrameNumber);
    void ProcessDeferredReleases(uint64 CompletedFrameNumber);
};
```

**职责：**
1. **资源创建**：创建 Buffer 和 Texture 对象
2. **生命周期管理**：追踪活动资源
3. **延迟释放**：GPU 安全释放（等待 3 帧后释放）
4. **统计信息**：提供资源使用统计

#### `FVulkanBuffer`
```cpp
class FVulkanBuffer : public FRHIBuffer {
    VulkanDevice* Device;
    VkBuffer Buffer;
    FVulkanAllocation Allocation;
    void* MappedPtr;
    bool bPersistentMapped;
    
public:
    bool Initialize();
    void Destroy();
    
    void* Map(uint32 Offset, uint32 Size) override;
    void Unmap() override;
};
```

**特点：**
- 封装 `VkBuffer`
- 支持持久映射（Host Visible 内存）
- 自动绑定内存

#### `FVulkanTexture`
```cpp
class FVulkanTexture : public FRHITexture {
    VulkanDevice* Device;
    VkImage Image;
    VkImageView ImageView;
    FVulkanAllocation Allocation;
    VkImageLayout CurrentLayout;
    
public:
    bool Initialize();
    void Destroy();
    bool CreateImageView();
};
```

**特点：**
- 封装 `VkImage` 和 `VkImageView`
- 支持 2D、3D、Cube 纹理
- 自动创建 ImageView
- 追踪 Image Layout

---

### ③ PoolManager 层 (Physical Memory)

**核心类：**

#### `FVulkanMemoryPool`
```cpp
class FVulkanMemoryPool {
    struct FMemoryPage {
        VkDeviceMemory DeviceMemory;
        uint64 Size;
        uint32 MemoryTypeIndex;
        bool bMapped;
        void* MappedPointer;
        std::unique_ptr<FVulkanMemoryManager> SubAllocator;
    };
    
    std::vector<std::unique_ptr<FMemoryPage>> Pages;
    
public:
    bool Allocate(uint64 Size, uint64 Alignment, FVulkanAllocation& OutAllocation);
    void Free(const FVulkanAllocation& Allocation);
    uint32 TrimEmptyPages();  // 清理空闲页
};
```

**特点：**
- **分页管理**：将大块内存（64MB - 256MB）组织成 Page
- **子分配**：每个 Page 内部使用 `FVulkanMemoryManager` 进行子分配
- **自动扩展**：不足时自动创建新 Page
- **碎片整理**：清理完全空闲的 Page

#### `FVulkanPoolManager`
```cpp
class FVulkanPoolManager {
    std::vector<std::unique_ptr<FVulkanMemoryPool>> Pools;  // 每个内存类型一个 Pool
    
public:
    bool Allocate(const FAllocationRequest& Request, FVulkanAllocation& OutAllocation);
    void Free(const FVulkanAllocation& Allocation);
    uint32 TrimAllPools();
};
```

**职责：**
1. **管理多个 Pool**：每个内存类型（Device Local、Host Visible 等）对应一个 Pool
2. **智能分配**：根据请求选择合适的内存类型
3. **大对象处理**：超过阈值（16MB）使用独立分配
4. **统计汇总**：提供全局内存统计

**内存类型示例：**
| 内存类型索引 | 属性 | 用途 |
|------------|------|------|
| 0 | `DEVICE_LOCAL` | GPU 本地内存，性能最佳 |
| 1 | `HOST_VISIBLE | HOST_COHERENT` | CPU 可见，用于上传数据 |
| 2 | `DEVICE_LOCAL | HOST_VISIBLE` | 混合内存（部分硬件支持） |

---

### ④ Vulkan API 层 (Driver)

**关键 API：**

#### `vkAllocateMemory`
```cpp
VkMemoryAllocateInfo allocInfo{};
allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
allocInfo.allocationSize = 64 * 1024 * 1024;  // 64MB
allocInfo.memoryTypeIndex = memoryTypeIndex;

VkDeviceMemory deviceMemory;
vkAllocateMemory(device, &allocInfo, nullptr, &deviceMemory);
```

#### `vkBindBufferMemory`
```cpp
vkBindBufferMemory(device, buffer, deviceMemory, offset);
```

#### `vkBindImageMemory`
```cpp
vkBindImageMemory(device, image, deviceMemory, offset);
```

**注意事项：**
- Vulkan 规范限制最大同时分配数（通常 4096）
- 大量小分配会导致性能下降
- 必须选择正确的内存类型

---

## 类图与架构图

### 1. 四层架构流程图

```
用户请求创建 Buffer (1MB)
         │
         ↓
┌────────────────────────┐
│  ① RHI 层               │
│  FRHIBufferRef ref =    │
│    CreateBuffer(...)    │
└───────────┬────────────┘
            │
            ↓
┌────────────────────────┐
│  ② ResourceManager 层   │
│  FVulkanBuffer* buffer  │
│  = new FVulkanBuffer    │
│  buffer->Initialize()   │
└───────────┬────────────┘
            │
            ↓
┌────────────────────────┐
│  ③ PoolManager 层       │
│  poolMgr->Allocate(     │
│    request, allocation) │
│  - 查找合适的 Pool       │
│  - 从 Page 子分配       │
└───────────┬────────────┘
            │
            ↓
┌────────────────────────┐
│  ④ Vulkan API 层        │
│  vkCreateBuffer()       │
│  vkAllocateMemory()     │  (如果需要新 Page)
│  vkBindBufferMemory()   │
└────────────────────────┘
```

### 2. 类继承关系图 (UML)

```
┌───────────────────┐
│   FRHIResource    │
│  (引用计数基类)     │
└────────┬──────────┘
         │
    ┌────┴────┐
    │         │
┌───┴───────┐ ┌───────────┐
│FRHIBuffer │ │FRHITexture│
└─────┬─────┘ └─────┬─────┘
      │             │
      │             │
┌─────┴────────┐ ┌──┴──────────┐
│FVulkanBuffer │ │FVulkanTexture│
│ - VkBuffer   │ │ - VkImage    │
│ - Allocation │ │ - VkImageView│
└──────────────┘ └──────────────┘
```

### 3. 内存池层次结构

```
FVulkanPoolManager
│
├─ FVulkanMemoryPool (MemoryType 0: Device Local)
│  ├─ FMemoryPage #0 (64MB)
│  │  └─ FVulkanMemoryManager (子分配器)
│  │     ├─ Allocation 1 (4MB, Buffer)
│  │     ├─ Allocation 2 (8MB, Texture)
│  │     └─ ...
│  │
│  └─ FMemoryPage #1 (64MB)
│     └─ ...
│
├─ FVulkanMemoryPool (MemoryType 1: Host Visible)
│  └─ FMemoryPage #0 (32MB)
│     └─ ...
│
└─ ...
```

### 4. 线程安全设计

```
Thread 1: CreateBuffer()
Thread 2: CreateTexture()
Thread 3: Free()
Thread 4: ProcessDeferredReleases()
         │
         ↓
┌─────────────────────┐
│  ResourceManager    │
│  ┌───────────────┐  │
│  │ std::mutex    │  │  ← ActiveBuffers/Textures
│  └───────────────┘  │
│  ┌───────────────┐  │
│  │ std::mutex    │  │  ← DeferredReleases
│  └───────────────┘  │
└──────────┬──────────┘
           │
           ↓
┌─────────────────────┐
│  PoolManager        │
│  ┌───────────────┐  │
│  │ std::mutex    │  │  ← Pools
│  └───────────────┘  │
└─────────────────────┘
         │
         ↓
  ┌─────────────────┐
  │ FMemoryPool     │
  │ ┌─────────────┐ │
  │ │ std::mutex  │ │  ← Pages
  │ └─────────────┘ │
  └─────────────────┘
```

---

## 代码实现

### 核心文件结构

```
MonsterEngine/
├── Include/
│   ├── RHI/
│   │   ├── RHIResources.h          ← RHI 层接口
│   │   └── RHIDefinitions.h
│   └── Platform/
│       └── Vulkan/
│           ├── FVulkanResourceManager.h  ← ResourceManager 层
│           ├── FVulkanMemoryPool.h       ← PoolManager 层
│           └── FVulkanMemoryManager.h    ← 底层内存分配器
│
└── Source/
    ├── Platform/
    │   └── Vulkan/
    │       ├── FVulkanResourceManager.cpp
    │       ├── FVulkanMemoryPool.cpp
    │       └── FVulkanMemoryManager.cpp
    │
    └── Tests/
        └── VulkanGPUMemorySystemTest.cpp  ← 综合测试
```

### 关键代码片段

#### 1. 创建 Buffer (完整流程)

```cpp
// ① RHI 层：用户代码
FRHIBufferRef vertexBuffer = resourceMgr.CreateBuffer(
    1024 * 1024,  // 1MB
    EResourceUsage::VertexBuffer | EResourceUsage::TransferDst,
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    sizeof(Vertex)
);

// ② ResourceManager 层：CreateBuffer 实现
FRHIBufferRef FVulkanResourceManager::CreateBuffer(...) {
    FVulkanBuffer* buffer = new FVulkanBuffer(Device, Size, Usage, MemoryFlags, Stride);
    
    if (!buffer->Initialize()) {
        delete buffer;
        return nullptr;
    }
    
    ActiveBuffers.push_back(buffer);
    return FRHIBufferRef(buffer);  // 自动 AddRef
}

// FVulkanBuffer::Initialize
bool FVulkanBuffer::Initialize() {
    // 创建 VkBuffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.size = Size;
    bufferInfo.usage = ConvertUsageFlags(Usage);
    vkCreateBuffer(device, &bufferInfo, nullptr, &Buffer);
    
    // 查询内存需求
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, Buffer, &memReqs);
    
    // ③ PoolManager 层：请求内存分配
    FAllocationRequest request{};
    request.Size = memReqs.size;
    request.Alignment = memReqs.alignment;
    request.MemoryTypeBits = memReqs.memoryTypeBits;
    request.RequiredFlags = MemoryFlags;
    
    if (!memMgr->Allocate(request, Allocation)) {
        vkDestroyBuffer(device, Buffer, nullptr);
        return false;
    }
    
    // ④ Vulkan API 层：绑定内存
    vkBindBufferMemory(device, Buffer, Allocation.DeviceMemory, Allocation.Offset);
    
    return true;
}
```

#### 2. 延迟释放 (GPU 安全)

```cpp
// 帧 0：用户释放 Buffer
{
    FRHIBufferRef buffer = CreateBuffer(...);
    
    // buffer 被 GPU 使用中
    commandList->BindVertexBuffer(buffer);
    commandList->Draw(...);
    
    // 请求延迟释放
    resourceMgr.DeferredRelease(buffer.Get(), currentFrameNumber);
    buffer.SafeRelease();  // 清空智能指针
}

// 帧 1, 2, 3: GPU 可能仍在使用
resourceMgr.ProcessDeferredReleases(completedFrameNumber);
// → 资源仍在队列中，不释放

// 帧 4: GPU 已完成
resourceMgr.ProcessDeferredReleases(completedFrameNumber);
// → completedFrameNumber >= (0 + 3)，安全释放
```

---

## 使用示例

### 示例 1：创建顶点和索引缓冲区

```cpp
FVulkanMemoryManager* memMgr = device->GetMemoryManager();
FVulkanResourceManager resourceMgr(device, memMgr);

// 创建顶点缓冲区 (64KB, Device Local)
auto vertexBuffer = resourceMgr.CreateBuffer(
    64 * 1024,
    EResourceUsage::VertexBuffer | EResourceUsage::TransferDst,
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    sizeof(float) * 3  // stride = 3 floats
);

// 创建索引缓冲区 (32KB, Device Local)
auto indexBuffer = resourceMgr.CreateBuffer(
    32 * 1024,
    EResourceUsage::IndexBuffer | EResourceUsage::TransferDst,
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    sizeof(uint32)
);

// 创建 Staging Buffer (Host Visible, 用于上传数据)
auto stagingBuffer = resourceMgr.CreateBuffer(
    96 * 1024,
    EResourceUsage::TransferSrc,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
    0
);

// 映射并写入数据
void* data = stagingBuffer->Lock(0, 64 * 1024);
memcpy(data, vertexData, 64 * 1024);
stagingBuffer->Unlock();

// 使用 Transfer Queue 拷贝到 GPU
// ...
```

### 示例 2：创建纹理

```cpp
TextureDesc desc{};
desc.width = 2048;
desc.height = 2048;
desc.depth = 1;
desc.mipLevels = 11;  // full mip chain
desc.arraySize = 1;
desc.format = EPixelFormat::R8G8B8A8_UNORM;
desc.usage = EResourceUsage::ShaderResource | EResourceUsage::TransferDst;

auto texture = resourceMgr.CreateTexture(
    desc,
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
);

// 获取 Vulkan 句柄
FVulkanTexture* vulkanTex = static_cast<FVulkanTexture*>(texture.Get());
VkImage image = vulkanTex->GetHandle();
VkImageView imageView = vulkanTex->GetView();
```

### 示例 3：多线程并发分配

```cpp
std::vector<std::thread> threads;

for (int i = 0; i < 4; ++i) {
    threads.emplace_back([&, i]() {
        for (int j = 0; j < 100; ++j) {
            auto buffer = resourceMgr.CreateBuffer(
                4096,  // 4KB
                EResourceUsage::UniformBuffer,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                0
            );
            
            // buffer 自动通过引用计数管理
        }
    });
}

for (auto& thread : threads) {
    thread.join();
}

// 所有资源安全释放
```

---

## 性能优化

### 1. 内存池配置

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `DEFAULT_PAGE_SIZE` | 64MB | 每个 Page 的大小 |
| `LARGE_ALLOCATION_THRESHOLD` | 16MB | 超过此大小使用独立分配 |
| `DEFERRED_RELEASE_FRAMES` | 3 | 延迟释放帧数 |

**优化建议：**
- **桌面平台**：增大 Page 大小到 128MB - 256MB
- **移动平台**：减小到 32MB - 64MB
- **独立分配阈值**：根据场景调整（如大纹理较多，可降低到 8MB）

### 2. 减少碎片化

```cpp
// 定期清理空闲页
void PerformGarbageCollection() {
    uint32 freedPages = poolMgr.TrimAllPools();
    MR_LOG_INFO("清理了 " + std::to_string(freedPages) + " 个空闲页");
}

// 在关卡切换时调用
void OnLevelTransition() {
    PerformGarbageCollection();
}
```

### 3. 批量分配

```cpp
// 不推荐：逐个分配
for (int i = 0; i < 1000; ++i) {
    CreateBuffer(1024, ...);
}

// 推荐：批量分配（减少锁竞争）
std::vector<FRHIBufferRef> buffers;
buffers.reserve(1000);

for (int i = 0; i < 1000; ++i) {
    buffers.push_back(CreateBuffer(1024, ...));
}
```

---

## 调试与监控

### 1. 统计信息

```cpp
// ResourceManager 统计
FVulkanResourceManager::FResourceStats stats;
resourceMgr.GetResourceStats(stats);

std::cout << "缓冲区数量: " << stats.NumBuffers << std::endl;
std::cout << "纹理数量: " << stats.NumTextures << std::endl;
std::cout << "缓冲区内存: " << stats.BufferMemory / (1024 * 1024) << " MB" << std::endl;
std::cout << "纹理内存: " << stats.TextureMemory / (1024 * 1024) << " MB" << std::endl;
std::cout << "待释放资源: " << stats.PendingReleases << std::endl;

// PoolManager 统计
FVulkanPoolManager::FManagerStats poolStats;
poolMgr.GetStats(poolStats);

std::cout << "内存池数: " << poolStats.NumPools << std::endl;
std::cout << "总分配: " << poolStats.TotalAllocated / (1024 * 1024) << " MB" << std::endl;
std::cout << "实际使用: " << poolStats.TotalUsed / (1024 * 1024) << " MB" << std::endl;
```

### 2. 调试名称

```cpp
auto buffer = CreateBuffer(...);
buffer->SetDebugName("PlayerWeaponVertexBuffer");

// Vulkan 调试工具（RenderDoc, PIX）将显示此名称
```

### 3. 内存泄漏检测

```cpp
// 引擎关闭时检查
resourceMgr.ReleaseUnusedResources();

FResourceStats finalStats;
resourceMgr.GetResourceStats(finalStats);

if (finalStats.NumBuffers > 0 || finalStats.NumTextures > 0) {
    MR_LOG_WARNING("内存泄漏检测:");
    MR_LOG_WARNING("  残留缓冲区: " + std::to_string(finalStats.NumBuffers));
    MR_LOG_WARNING("  残留纹理: " + std::to_string(finalStats.NumTextures));
}
```

---

## 下一步开发计划

### 1. 近期目标（1-2 周）

#### 1.1 完善格式映射
```cpp
// 当前：仅支持 R8G8B8A8_UNORM
// 目标：支持完整的 EPixelFormat 到 VkFormat 映射

VkFormat ConvertPixelFormat(EPixelFormat format) {
    switch (format) {
        case EPixelFormat::R8G8B8A8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
        case EPixelFormat::R32G32B32A32_FLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case EPixelFormat::BC1_UNORM: return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
        // ... 添加更多格式
        default: return VK_FORMAT_UNDEFINED;
    }
}
```

#### 1.2 实现 FVulkanResourceMultiBuffer
```cpp
// 参考 UE5 的 FVulkanResourceMultiBuffer
// 用于动态 Uniform Buffer（每帧更新）

class FVulkanResourceMultiBuffer : public FRHIBuffer {
    static constexpr uint32 NUM_BUFFERS = 3;  // Triple buffering
    FVulkanBuffer* Buffers[NUM_BUFFERS];
    uint32 CurrentBufferIndex;
    
public:
    void* Lock(uint32 Offset, uint32 Size) override;
    void AdvanceFrame();  // 切换到下一个 Buffer
};
```

#### 1.3 集成 Render Graph
```cpp
// 将资源管理与 Render Graph 集成
class FRenderGraph {
    void RegisterTexture(FRHITextureRef texture);
    void RegisterBuffer(FRHIBufferRef buffer);
    
    // 自动处理资源转换和同步
    void Execute();
};
```

### 2. 中期目标（3-4 周）

#### 2.1 实现 GPU Crash Debugging
```cpp
// 集成 AMD GPU Crash Analyzer / NVIDIA Aftermath

class FVulkanGPUCrashTracker {
    void MarkBufferUsage(FRHIBufferRef buffer, const char* location);
    void MarkTextureUsage(FRHITextureRef texture, const char* location);
    
    // GPU 崩溃时输出最后访问的资源
    void OnGPUCrash();
};
```

#### 2.2 实现 Memory Budget System
```cpp
// 参考 UE5 的 RHI.ResourceTableCachingMode

class FMemoryBudget {
    uint64 TotalBudget;
    uint64 TextureBudget;
    uint64 BufferBudget;
    
    bool CanAllocate(uint64 size, EResourceType type);
    void OnAllocation(uint64 size, EResourceType type);
    void OnFree(uint64 size, EResourceType type);
};
```

#### 2.3 实现 Resource Streaming
```cpp
// 与纹理流送系统集成
class FResourceStreaming {
    void StreamInTexture(FRHITextureRef texture, uint32 targetMipLevel);
    void StreamOutTexture(FRHITextureRef texture, uint32 targetMipLevel);
};
```

### 3. 长期目标（1-2 月）

#### 3.1 支持多 GPU
```cpp
class FVulkanMultiGPUResourceManager {
    std::vector<FVulkanResourceManager*> PerGPUManagers;
    
    // AFR (Alternate Frame Rendering)
    void DistributeResources();
};
```

#### 3.2 实现 Memory Defragmentation
```cpp
// 参考 Vulkan Memory Allocator (VMA) 的碎片整理

class FMemoryDefragmenter {
    struct FDefragmentationStats {
        uint64 BytesMoved;
        uint32 AllocationssMoved;
        float Duration;
    };
    
    FDefragmentationStats Defragment(uint32 maxBytesToMove);
};
```

#### 3.3 实现跨平台 RHI 后端
```cpp
// 扩展到 D3D12, Metal

class FD3D12ResourceManager : public IRHIResourceManager {
    // D3D12 实现
};

class FMetalResourceManager : public IRHIResourceManager {
    // Metal 实现
};
```

### 4. 性能目标

| 指标 | 目标 | 当前状态 |
|------|------|----------|
| **分配延迟** | < 50μs (小对象) | 待测量 |
| **释放延迟** | < 10μs | 待测量 |
| **内存碎片率** | < 10% | 待测量 |
| **并发分配吞吐** | > 10000 allocs/sec | 待测量 |
| **内存开销** | < 5% (元数据) | 待测量 |

### 5. 测试计划

#### 5.1 单元测试扩展
- [ ] 每个层的独立单元测试
- [ ] 边界条件测试（超大分配、零字节分配等）
- [ ] 多线程压力测试（10+ 线程）

#### 5.2 集成测试
- [ ] 与 Render Pipeline 集成测试
- [ ] 实际游戏场景测试（开放世界、室内场景等）
- [ ] 长时间运行稳定性测试（24 小时+）

#### 5.3 性能基准测试
- [ ] 与 UE5 的性能对比
- [ ] 与 Vulkan Memory Allocator (VMA) 对比
- [ ] 不同硬件平台的性能测试

---

## 附录：参考资料

### UE5 源码参考
- `Engine/Source/Runtime/RHI/Public/RHIResources.h`
- `Engine/Source/Runtime/VulkanRHI/Private/VulkanResources.h`
- `Engine/Source/Runtime/VulkanRHI/Private/VulkanMemory.h`
- `Engine/Source/Runtime/VulkanRHI/Private/VulkanMemory.cpp`

### Vulkan 规范
- Vulkan 1.3 Specification: Chapter 10 (Memory Allocation)
- Vulkan Memory Allocator (VMA) Library

### 相关文档
- [MonsterEngine 架构和设计.md](./引擎的架构和设计.md)
- [Vulkan 内存管理器测试说明.md](./Vulkan内存管理器测试说明.md)
- [纹理流送系统开发指南.md](./Texture_Streaming_System_Guide.md)

---

**文档版本：** 1.0  
**最后更新：** 2025-11-04  
**作者：** MonsterEngine 开发团队  
**参考标准：** UE5 RHI Architecture + Vulkan 1.3 Specification

