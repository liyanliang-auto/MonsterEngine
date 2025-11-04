# Vulkan GPU 内存管理系统 - 快速参考卡

**5 分钟快速上手 MonsterEngine 的四层 GPU 内存架构**

---

## 🚀 快速开始

### 1. 基本使用

```cpp
// 获取资源管理器
FVulkanMemoryManager* memMgr = device->GetMemoryManager();
FVulkanResourceManager resourceMgr(device, memMgr);

// 创建 Vertex Buffer (Device Local)
auto vertexBuffer = resourceMgr.CreateBuffer(
    64 * 1024,                          // 64KB
    EResourceUsage::VertexBuffer,       // 用途
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, // 内存类型
    sizeof(float) * 3                   // 步长
);

// 创建 Uniform Buffer (Host Visible)
auto uniformBuffer = resourceMgr.CreateBuffer(
    256,                                // 256 bytes
    EResourceUsage::UniformBuffer,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
    0
);

// 映射并写入数据
void* data = uniformBuffer->Lock(0, 256);
memcpy(data, &constants, 256);
uniformBuffer->Unlock();
```

### 2. 纹理创建

```cpp
TextureDesc desc{};
desc.width = 1024;
desc.height = 1024;
desc.depth = 1;
desc.mipLevels = 10;
desc.arraySize = 1;
desc.format = EPixelFormat::R8G8B8A8_UNORM;
desc.usage = EResourceUsage::ShaderResource | EResourceUsage::TransferDst;

auto texture = resourceMgr.CreateTexture(
    desc,
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
);
```

---

## 📐 四层架构速查

| 层级 | 类 | 职责 | 示例代码 |
|------|---|------|---------|
| **① RHI 层** | `FRHIBuffer`, `FRHITexture` | 平台无关接口 | `FRHIBufferRef buffer = ...;` |
| **② ResourceManager** | `FVulkanResourceManager` | 资源生命周期管理 | `CreateBuffer(...)`, `DeferredRelease(...)` |
| **③ PoolManager** | `FVulkanPoolManager`, `FVulkanMemoryPool` | 内存池分页管理 | 自动调用，用户无需关心 |
| **④ Vulkan API** | `vkAllocateMemory`, `vkBindBufferMemory` | 驱动调用 | 底层实现，用户无需直接调用 |

---

## 🔑 核心概念

### 引用计数

```cpp
// 自动管理，无需手动 Release
FRHIBufferRef buffer = CreateBuffer(...);
{
    FRHIBufferRef buffer2 = buffer;  // RefCount = 2
}
// buffer2 离开作用域，RefCount = 1
// buffer 离开作用域，RefCount = 0，自动销毁
```

### 延迟释放

```cpp
// GPU 安全释放（等待 3 帧）
resourceMgr.DeferredRelease(buffer.Get(), currentFrameNumber);
buffer.SafeRelease();  // 清空智能指针

// 每帧处理
resourceMgr.ProcessDeferredReleases(completedFrameNumber);
```

### 内存类型

| 内存类型 | 标志 | 用途 |
|---------|------|------|
| Device Local | `DEVICE_LOCAL_BIT` | GPU 本地内存，性能最佳 |
| Host Visible | `HOST_VISIBLE_BIT \| HOST_COHERENT_BIT` | CPU 可见，用于上传 |
| Staging | `HOST_VISIBLE_BIT \| TRANSFER_SRC_BIT` | CPU 写入，Transfer 到 GPU |

---

## 📊 统计信息

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

---

## 🛠️ 常见操作

### 创建 Staging Buffer

```cpp
// 用于从 CPU 上传数据到 GPU
auto staging = resourceMgr.CreateBuffer(
    dataSize,
    EResourceUsage::TransferSrc,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
    0
);

void* mapped = staging->Lock(0, dataSize);
memcpy(mapped, data, dataSize);
staging->Unlock();

// 使用 Transfer Command List 拷贝到 Device Local Buffer
// ...
```

### 创建 Cube Map

```cpp
TextureDesc cubemapDesc{};
cubemapDesc.width = 512;
cubemapDesc.height = 512;
cubemapDesc.depth = 1;
cubemapDesc.mipLevels = 9;
cubemapDesc.arraySize = 6;  // Cube = 6 faces
cubemapDesc.format = EPixelFormat::R8G8B8A8_UNORM;
cubemapDesc.usage = EResourceUsage::ShaderResource | EResourceUsage::TransferDst;

auto cubemap = resourceMgr.CreateTexture(
    cubemapDesc,
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
);
```

### 内存池清理

```cpp
// 定期清理空闲页（建议在关卡切换时调用）
uint32 freedPages = poolMgr.TrimAllPools();
MR_LOG_INFO("清理了 " + std::to_string(freedPages) + " 个空闲页");
```

---

## ⚠️ 常见问题

### Q1: 如何选择内存类型？

```cpp
// GPU 读写（顶点、纹理等）→ DEVICE_LOCAL_BIT
auto vb = CreateBuffer(..., VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, ...);

// CPU 频繁更新（UBO、动态数据）→ HOST_VISIBLE + HOST_COHERENT
auto ubo = CreateBuffer(..., 
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
    ...);

// CPU 写入，传输到 GPU → HOST_VISIBLE (Staging)
auto staging = CreateBuffer(..., VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, ...);
```

### Q2: Buffer/Texture 何时释放？

```cpp
// 方式 1：自动释放（推荐）
{
    auto buffer = CreateBuffer(...);
    // buffer 离开作用域时，RefCount 降为 0，自动调用析构
}

// 方式 2：延迟释放（GPU 使用中）
auto buffer = CreateBuffer(...);
DrawCall(buffer);  // GPU 使用
resourceMgr.DeferredRelease(buffer.Get(), frameNumber);
buffer.SafeRelease();  // 等待 3 帧后释放
```

### Q3: 内存泄漏如何检测？

```cpp
// 引擎关闭时检查
resourceMgr.ReleaseUnusedResources();

FResourceStats stats;
resourceMgr.GetResourceStats(stats);

if (stats.NumBuffers > 0 || stats.NumTextures > 0) {
    MR_LOG_WARNING("内存泄漏: " + std::to_string(stats.NumBuffers) + " buffers, " +
                   std::to_string(stats.NumTextures) + " textures");
}
```

---

## 🧪 运行测试

```bash
# 运行 GPU 内存系统测试
MonsterEngine.exe --test-gpu-memory

# 测试包含：
# 1. RHI 层引用计数
# 2. ResourceManager 缓冲区/纹理管理
# 3. PoolManager 内存池
# 4. 并发分配（多线程）
# 5. 延迟释放机制
# 6. 实际场景（游戏资产加载）
```

---

## 📈 性能配置

### 调整内存池大小

```cpp
// 在 FVulkanPoolManager.h 中
static constexpr uint64 DEFAULT_PAGE_SIZE = 64 * 1024 * 1024;  // 64MB

// 桌面平台：128MB - 256MB
// 移动平台：32MB - 64MB
```

### 调整大对象阈值

```cpp
// 在 FVulkanPoolManager.h 中
static constexpr uint64 LARGE_ALLOCATION_THRESHOLD = 16 * 1024 * 1024;  // 16MB

// 大纹理较多：降低到 8MB
// 小对象较多：提高到 32MB
```

---

## 📚 相关文档

- **完整文档：** [Vulkan_GPU内存管理系统_四层架构.md](./Vulkan_GPU内存管理系统_四层架构.md)
- **架构设计：** [引擎的架构和设计.md](./引擎的架构和设计.md) (第四章)
- **测试说明：** [VulkanGPUMemorySystemTest.cpp](../Source/Tests/VulkanGPUMemorySystemTest.cpp)
- **UE5 参考：** `Engine/Source/Runtime/VulkanRHI/Private/VulkanMemory.h`

---

**快速参考版本：** 1.0  
**最后更新：** 2025-11-04  
**适用于：** MonsterEngine Vulkan RHI

