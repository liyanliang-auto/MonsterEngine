# MonsterEngine 内存管理系统开发文档

## 📋 文档概述

本文档详细描述了 MonsterEngine 的内存管理系统，包括架构设计、实现细节、使用方法和测试指南。

**版本**: 1.0  
**日期**: 2025-11-01  
**参考**: Unreal Engine 5 Memory Management System

---

## 🏗️ 系统架构

### 架构图

```
┌─────────────────────────────────────────────┐
│            应用层 (Application)              │
│   使用 FMemory 静态接口进行内存操作          │
└─────────────────┬───────────────────────────┘
                  │
┌─────────────────▼───────────────────────────┐
│          FMemory (静态工具类)                │
│  提供: Malloc/Free/Memcpy/Memset 等         │
└─────────────────┬───────────────────────────┘
                  │
┌─────────────────▼───────────────────────────┐
│      FMemoryManager (单例管理器)             │
│  管理全局分配器，系统初始化和配置            │
└─────────────────┬───────────────────────────┘
                  │
┌─────────────────▼───────────────────────────┐
│         FMalloc (抽象基类)                   │
│  定义内存分配器接口                          │
└─────────────────┬───────────────────────────┘
                  │
┌─────────────────▼───────────────────────────┐
│      FMallocBinned2 (具体实现)               │
│  高性能多线程分箱分配器                      │
│  - 小对象: 16-1024字节 (分箱)               │
│  - 大对象: >1024字节 (OS直接分配)           │
│  - TLS缓存: 线程本地无锁快速路径             │
└──────────────────────────────────────────────┘
```

---

## 📦 核心组件

### 1. FMemory - 静态内存操作类

**文件位置**: `Include/Core/HAL/FMemory.h`

**功能概述**:
- 提供全局静态接口，封装所有内存操作
- 对标 UE5 的 `FMemory` 类
- 线程安全，高性能

**主要接口**:

```cpp
class FMemory {
public:
    // ========== 基础内存操作 ==========
    
    // 内存复制（优化版 memcpy）
    static void* Memcpy(void* Dest, const void* Src, SIZE_T Count);
    
    // 内存移动（支持重叠区域）
    static void* Memmove(void* Dest, const void* Src, SIZE_T Count);
    
    // 内存比较
    static int32 Memcmp(const void* Buf1, const void* Buf2, SIZE_T Count);
    
    // 内存填充
    static void* Memset(void* Dest, uint8 Value, SIZE_T Count);
    
    // 内存清零
    static void Memzero(void* Dest, SIZE_T Count);
    
    // 内存交换
    static void* Memswap(void* Ptr1, void* Ptr2, SIZE_T Size);
    
    // ========== 内存分配 ==========
    
    // 分配内存
    static void* Malloc(SIZE_T Count, uint32 Alignment = DEFAULT_ALIGNMENT);
    
    // 重新分配
    static void* Realloc(void* Original, SIZE_T Count, uint32 Alignment = DEFAULT_ALIGNMENT);
    
    // 释放内存
    static void Free(void* Original);
    
    // 获取分配大小
    static SIZE_T GetAllocSize(void* Original);
    
    // ========== 对齐工具 ==========
    
    // 检查指针是否对齐
    static bool IsAligned(const void* Ptr, SIZE_T Alignment);
    
    // 对齐指针
    static void* Align(void* Ptr, SIZE_T Alignment);
    
    // ========== 模板辅助函数 ==========
    
    // 分配数组
    template<typename T>
    static T* MallocArray(SIZE_T Count, uint32 Alignment = alignof(T));
    
    // 创建对象（placement new）
    template<typename T, typename... Args>
    static T* New(Args&&... InArgs);
    
    // 删除对象
    template<typename T>
    static void Delete(T* Obj);
    
    // 创建数组对象
    template<typename T>
    static T* NewArray(SIZE_T Count);
    
    // 删除数组对象
    template<typename T>
    static void DeleteArray(T* Array, SIZE_T Count);
};
```

**使用示例**:

```cpp
// 基础分配
void* buffer = FMemory::Malloc(1024);
FMemory::Memset(buffer, 0, 1024);
FMemory::Free(buffer);

// 类型安全分配
struct MyData { int x, y, z; };
MyData* data = FMemory::New<MyData>(10, 20, 30);
FMemory::Delete(data);

// 数组分配
int32* array = FMemory::NewArray<int32>(100);
FMemory::DeleteArray(array, 100);
```

---

### 2. FMemoryManager - 全局内存管理器

**文件位置**: `Include/Core/HAL/FMemoryManager.h`

**功能概述**:
- 单例模式，管理全局内存分配器
- 系统初始化和关闭
- 收集系统内存统计信息
- 支持 Huge Pages（大页）

**主要接口**:

```cpp
class FMemoryManager {
public:
    // 获取单例
    static FMemoryManager& Get();
    
    // 初始化内存系统
    bool Initialize();
    
    // 关闭内存系统
    void Shutdown();
    
    // 获取当前分配器
    FMalloc* GetAllocator() const;
    
    // 设置自定义分配器
    void SetAllocator(TUniquePtr<FMalloc> NewAllocator);
    
    // 系统内存统计
    struct FGlobalMemoryStats {
        uint64 TotalPhysicalMemory;      // 总物理内存
        uint64 AvailablePhysicalMemory;  // 可用物理内存
        uint64 TotalVirtualMemory;       // 总虚拟内存
        uint64 AvailableVirtualMemory;   // 可用虚拟内存
        uint64 PageSize;                 // 页大小
        uint64 LargePageSize;            // 大页大小
    };
    
    void GetGlobalMemoryStats(FGlobalMemoryStats& OutStats);
    
    // Huge Pages 支持
    bool IsHugePagesAvailable() const;
    bool EnableHugePages(bool bEnable);
};
```

**使用示例**:

```cpp
// 初始化内存系统
FMemoryManager::Get().Initialize();

// 获取系统信息
FMemoryManager::FGlobalMemoryStats stats;
FMemoryManager::Get().GetGlobalMemoryStats(stats);

MR_LOG_INFO("Total RAM: " + std::to_string(stats.TotalPhysicalMemory / (1024*1024)) + " MB");
MR_LOG_INFO("Page Size: " + std::to_string(stats.PageSize) + " bytes");

// 自定义分配器
auto customAllocator = MakeUnique<FMallocBinned2>();
FMemoryManager::Get().SetAllocator(std::move(customAllocator));
```

---

### 3. FMalloc - 分配器抽象基类

**文件位置**: `Include/Core/HAL/FMalloc.h`

**功能概述**:
- 定义内存分配器接口
- 所有自定义分配器必须继承此类
- 提供统计和调试支持

**主要接口**:

```cpp
class FMalloc {
public:
    virtual ~FMalloc() = default;
    
    // 分配内存
    virtual void* Malloc(SIZE_T Size, uint32 Alignment = DEFAULT_ALIGNMENT) = 0;
    
    // 重新分配
    virtual void* Realloc(void* Original, SIZE_T Size, uint32 Alignment = DEFAULT_ALIGNMENT) = 0;
    
    // 释放内存
    virtual void Free(void* Original) = 0;
    
    // 获取分配大小（可选）
    virtual SIZE_T GetAllocationSize(void* Original);
    
    // 堆验证（调试用）
    virtual bool ValidateHeap();
    
    // 获取总分配内存
    virtual uint64 GetTotalAllocatedMemory();
    
    // 整理内存（返还系统）
    virtual void Trim();
    
    // 统计信息
    struct FMemoryStats {
        uint64 TotalAllocated;    // 已分配总量
        uint64 TotalReserved;     // 已保留总量
        uint64 AllocationCount;   // 分配次数
        uint64 FreeCount;         // 释放次数
    };
    
    virtual void GetMemoryStats(FMemoryStats& OutStats);
};
```

---

### 4. FMallocBinned2 - 高性能分箱分配器

**文件位置**: `Include/Core/HAL/FMallocBinned2.h`

**功能概述**:
- 对标 UE5 的 `FMallocBinned2`
- 针对小对象优化（16-1024 字节）
- 多线程友好，TLS 缓存
- 大对象直接系统分配

**架构特点**:

```
┌──────────────────────────────────────────┐
│  FMallocBinned2 核心设计                  │
├──────────────────────────────────────────┤
│                                           │
│  🔹 分箱策略 (Size Classes)              │
│     Bin 0:  16 bytes                     │
│     Bin 1:  32 bytes                     │
│     Bin 2:  64 bytes                     │
│     Bin 3: 128 bytes                     │
│     Bin 4: 256 bytes                     │
│     Bin 5: 512 bytes                     │
│     Bin 6: 1024 bytes                    │
│                                           │
│  🔹 TLS 缓存 (Thread-Local Cache)        │
│     - 每个线程独立缓存                    │
│     - 无锁快速路径                        │
│     - 16个对象/bin                        │
│                                           │
│  🔹 页管理 (Page Allocation)             │
│     - 64KB 页大小                         │
│     - Free List 管理                     │
│     - 延迟释放                            │
│                                           │
│  🔹 大对象 (>1024 bytes)                 │
│     - 直接系统分配 (malloc/VirtualAlloc) │
│     - 避免碎片化                          │
│                                           │
└──────────────────────────────────────────┘
```

**性能特性**:

| 特性 | 说明 | 优势 |
|------|------|------|
| **分箱分配** | 按大小分类 | 减少碎片，快速查找 |
| **TLS 缓存** | 线程本地存储 | 无锁，极快分配 |
| **页池化** | 64KB 页复用 | 减少系统调用 |
| **对齐优化** | 缓存行对齐 | 避免伪共享 |
| **统计跟踪** | 详细性能数据 | 便于调优 |

**使用示例**:

```cpp
FMallocBinned2 allocator;

// 小对象分配（走分箱）
void* small = allocator.Malloc(64);  // 使用 Bin 2
allocator.Free(small);

// 大对象分配（直接系统）
void* large = allocator.Malloc(8192);  // 直接 OS 分配
allocator.Free(large);

// 获取统计信息
FMalloc::FMemoryStats stats;
allocator.GetMemoryStats(stats);
MR_LOG_INFO("Total Allocated: " + std::to_string(stats.TotalAllocated));
```

---

## 🧪 测试系统

### 测试文件

**位置**: `Source/Tests/MemoryManagerTest.cpp`

### 测试套件结构

```
Memory Management Test Suite
│
├── FMemory 基础测试
│   ├── Basic Operations (Memcpy, Memset, Memzero)
│   └── Alignment Check
│
├── FMemoryManager 测试
│   ├── Initialization
│   ├── Basic Allocation
│   └── System Stats
│
├── FMallocBinned2 测试
│   ├── Small Allocations (16-1024B)
│   ├── Large Allocations (>1024B)
│   ├── Aligned Allocations
│   └── Statistics Tracking
│
├── 压力测试
│   ├── Random Allocation Pattern (1000次)
│   └── Multi-threaded Allocations (4线程)
│
└── 边界情况测试
    ├── Null Pointer
    ├── Zero Size
    └── Realloc Edge Cases
```

### 运行测试

**方法 1: 命令行参数**
```bash
MonsterEngine.exe --test-memory
# 或
MonsterEngine.exe -tm
```

**方法 2: 修改 main.cpp**
```cpp
// 在 main.cpp 中设置
bool runTests = true;  // 强制运行测试
```

**方法 3: Visual Studio 调试**
1. 右键项目 → 属性
2. 调试 → 命令参数
3. 输入: `--test-memory`
4. F5 启动调试

### 测试输出示例

```
======================================
  Memory System Test Mode
======================================

--- FMemory Basic Tests ---
✓ PASSED: FMemory::Basic Operations (0.12ms)
✓ PASSED: FMemory::Alignment Check (0.05ms)

--- FMemoryManager Tests ---
✓ PASSED: FMemoryManager::Initialization (1.23ms)
  Total Physical Memory: 16384 MB
  Available Physical Memory: 8192 MB
  Page Size: 4096 bytes
✓ PASSED: FMemoryManager::Basic Allocation (0.34ms)

--- FMallocBinned2 Tests ---
✓ PASSED: FMallocBinned2::Small Allocations (0.56ms)
✓ PASSED: FMallocBinned2::Large Allocations (0.78ms)
✓ PASSED: FMallocBinned2::Aligned Allocations (0.42ms)
✓ PASSED: FMallocBinned2::Statistics Tracking (0.91ms)
  Total Allocated: 6 KB
  Total Reserved: 128 KB
  Allocation Count: 100

--- Stress Tests ---
✓ PASSED: Stress Test::Random Allocation Pattern (12.34ms)
✓ PASSED: Stress Test::Multi-threaded Allocations (45.67ms)

--- Edge Cases ---
✓ PASSED: Edge Cases::Null and Zero Size (0.08ms)

======================================
  Test Summary
======================================
Total Tests: 11
Passed: 11
Failed: 0

🎉 All tests passed!
======================================
```

---

## 🔧 调试指南

### 启用详细日志

```cpp
// 在 main.cpp 中
Logger::getInstance().setMinLevel(ELogLevel::Debug);
```

### 内存泄漏检测

**Windows (Visual Studio)**:
```cpp
// 在 main.cpp 开头添加
#ifdef _DEBUG
    #define _CRTDBG_MAP_ALLOC
    #include <stdlib.h>
    #include <crtdbg.h>
#endif

int main() {
    #ifdef _DEBUG
        _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    #endif
    
    // ... 正常代码
}
```

### 堆验证

```cpp
FMalloc* allocator = FMemoryManager::Get().GetAllocator();

// 执行操作
void* ptr = allocator->Malloc(1024);
allocator->Free(ptr);

// 验证堆完整性
if (!allocator->ValidateHeap()) {
    MR_LOG_ERROR("Heap corruption detected!");
}
```

### 性能分析

```cpp
// 获取详细统计
FMalloc::FMemoryStats stats;
allocator->GetMemoryStats(stats);

MR_LOG_INFO("=== Memory Statistics ===");
MR_LOG_INFO("Total Allocated: " + std::to_string(stats.TotalAllocated / 1024) + " KB");
MR_LOG_INFO("Total Reserved: " + std::to_string(stats.TotalReserved / 1024) + " KB");
MR_LOG_INFO("Allocation Count: " + std::to_string(stats.AllocationCount));
MR_LOG_INFO("Free Count: " + std::to_string(stats.FreeCount));
MR_LOG_INFO("Active Allocations: " + std::to_string(stats.AllocationCount - stats.FreeCount));
```

---

## 📊 性能优化建议

### 1. 减少小对象分配

**❌ 不好的做法**:
```cpp
for (int i = 0; i < 1000; ++i) {
    MyObject* obj = new MyObject();  // 1000次分配
    // ... 使用
    delete obj;
}
```

**✅ 推荐做法**:
```cpp
std::vector<MyObject> objects;
objects.reserve(1000);  // 预分配
for (int i = 0; i < 1000; ++i) {
    objects.emplace_back();  // 原地构造
}
```

### 2. 对象池

```cpp
template<typename T>
class TObjectPool {
    std::vector<T*> pool;
    
public:
    T* Acquire() {
        if (pool.empty()) {
            return FMemory::New<T>();
        }
        T* obj = pool.back();
        pool.pop_back();
        return obj;
    }
    
    void Release(T* obj) {
        pool.push_back(obj);
    }
};
```

### 3. 内存对齐

```cpp
// GPU 缓冲区应该 256 字节对齐
struct alignas(256) GPUBuffer {
    float data[64];
};

GPUBuffer* buffer = FMemory::New<GPUBuffer>();
MR_ASSERT(FMemory::IsAligned(buffer, 256));
```

### 4. 批量分配

```cpp
// 一次性分配大块内存，然后自己管理
void* bulk = FMemory::Malloc(1024 * 1024);  // 1MB

// 从大块中分配小对象
MyObject* objects = static_cast<MyObject*>(bulk);
for (int i = 0; i < 100; ++i) {
    new (&objects[i]) MyObject();  // Placement new
}
```

---

## 🚨 常见问题

### Q1: 内存分配失败怎么办？

**A**: 检查以下几点：
1. 系统是否内存不足
2. 是否请求了不合理的大小（如负数）
3. 对齐要求是否合理（必须是 2 的幂）

```cpp
void* ptr = FMemory::Malloc(size);
if (!ptr) {
    MR_LOG_ERROR("Out of memory! Requested: " + std::to_string(size));
    // 尝试释放缓存或优雅降级
    FMemoryManager::Get().GetAllocator()->Trim();
    ptr = FMemory::Malloc(size);  // 重试
}
```

### Q2: 多线程环境下是否安全？

**A**: 是的，`FMallocBinned2` 是线程安全的。
- TLS 缓存提供无锁快速路径
- 每个分箱有独立的互斥锁
- 大对象使用系统分配器（本身线程安全）

### Q3: 如何检测内存泄漏？

**A**: 使用以下方法：
1. Visual Studio 内置的 CRT 调试堆
2. 在程序结束前检查统计信息
3. 使用 Valgrind (Linux) 或 Dr. Memory (Windows)

```cpp
// 程序开始
FMalloc::FMemoryStats startStats;
allocator->GetMemoryStats(startStats);

// ... 运行程序 ...

// 程序结束
FMalloc::FMemoryStats endStats;
allocator->GetMemoryStats(endStats);

uint64 leaked = (endStats.AllocationCount - endStats.FreeCount) - 
                (startStats.AllocationCount - startStats.FreeCount);
if (leaked > 0) {
    MR_LOG_WARNING("Potential leak: " + std::to_string(leaked) + " allocations");
}
```

### Q4: 大页（Huge Pages）有什么用？

**A**: 大页可以提升性能：
- 减少 TLB 缺失
- 降低页表开销
- 适合大型数据结构

```cpp
if (FMemoryManager::Get().IsHugePagesAvailable()) {
    FMemoryManager::Get().EnableHugePages(true);
    MR_LOG_INFO("Huge Pages enabled");
}
```

---

## 📚 参考资源

### Unreal Engine 5 参考

- `Engine/Source/Runtime/Core/Public/HAL/MallocBinned2.h`
- `Engine/Source/Runtime/Core/Private/HAL/MallocBinned2.cpp`
- `Engine/Source/Runtime/Core/Public/HAL/FMemory.h`

### 学习资源

1. **论文**: 
   - "Scalable Memory Allocation using jemalloc"
   - "TCMalloc: Thread-Caching Malloc"

2. **书籍**:
   - "Game Engine Architecture" by Jason Gregory
   - "Memory Management: Algorithms and Implementation"

3. **视频**:
   - GDC Talk: "Memory Management in Unreal Engine"
   - CppCon: "High Performance Memory Management"

---

## 📝 版本历史

| 版本 | 日期 | 修改内容 |
|------|------|----------|
| 1.0 | 2025-11-01 | 初始版本，基础内存管理系统 |

---

## 👥 贡献者

- **架构设计**: 参考 UE5 设计
- **实现**: MonsterEngine Team
- **测试**: 全面测试套件

---

## 📄 许可证

本项目遵循 MIT 许可证。

---

**最后更新**: 2025-11-01  
**文档维护**: MonsterEngine Development Team

