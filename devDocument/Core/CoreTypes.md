# 核心类型系统

MonsterRender的核心类型系统提供了跨平台的基础类型定义、智能指针别名和容器类型，确保代码的一致性和可移植性。

## 基础类型定义

### 整数类型
```cpp
namespace MonsterRender {
    // 无符号整数
    using uint8  = std::uint8_t;   // 8位无符号整数  (0 to 255)
    using uint16 = std::uint16_t;  // 16位无符号整数 (0 to 65,535)
    using uint32 = std::uint32_t;  // 32位无符号整数 (0 to 4,294,967,295)
    using uint64 = std::uint64_t;  // 64位无符号整数
    
    // 有符号整数
    using int8   = std::int8_t;    // 8位有符号整数  (-128 to 127)
    using int16  = std::int16_t;   // 16位有符号整数 (-32,768 to 32,767)
    using int32  = std::int32_t;   // 32位有符号整数 (-2^31 to 2^31-1)
    using int64  = std::int64_t;   // 64位有符号整数
}
```

**使用场景**：
- `uint8` - 字节数据、像素值
- `uint16` - 小范围索引、端口号
- `uint32` - 对象ID、缓冲区大小、像素数据
- `uint64` - 内存地址、大文件大小

### 浮点类型
```cpp
namespace MonsterRender {
    using float32 = float;   // 32位浮点数 (IEEE 754)
    using float64 = double;  // 64位浮点数 (IEEE 754)
}
```

**使用建议**：
- `float32` - 图形计算、顶点数据、变换矩阵
- `float64` - 高精度计算、物理模拟

### 字符串类型
```cpp
namespace MonsterRender {
    using String = std::string;         // 可变字符串
    using StringView = std::string_view; // 只读字符串视图 (C++17)
}
```

**使用模式**：
```cpp
// 函数参数优先使用StringView（避免拷贝）
void setDebugName(StringView name);

// 成员变量使用String（拥有数据）
class Resource {
    String m_debugName;
};
```

## 智能指针系统

### 共享指针 - TSharedPtr
```cpp
template<typename T>
using TSharedPtr = std::shared_ptr<T>;

template<typename T, typename... Args>
constexpr TSharedPtr<T> MakeShared(Args&&... args) {
    return std::make_shared<T>(std::forward<Args>(args)...);
}
```

**使用场景**：
- GPU资源共享（纹理、缓冲区）
- 引用计数生命周期管理
- 多对象共享的数据

**示例**：
```cpp
// 创建共享资源
auto texture = MakeShared<VulkanTexture>(textureDesc);

// 多个对象可以共享同一纹理
material1->setTexture(texture);
material2->setTexture(texture);
```

### 独占指针 - TUniquePtr
```cpp
template<typename T>
using TUniquePtr = std::unique_ptr<T>;

template<typename T, typename... Args>
constexpr TUniquePtr<T> MakeUnique(Args&&... args) {
    return std::make_unique<T>(std::forward<Args>(args)...);
}
```

**使用场景**：
- 单一所有权资源
- PIMPL模式实现
- 工厂方法返回值

**示例**：
```cpp
// 创建独占设备
auto device = MakeUnique<VulkanDevice>();

// 只能移动，不能复制
auto anotherDevice = std::move(device); // OK
// auto copy = device; // 编译错误
```

### 弱引用 - TWeakPtr
```cpp
template<typename T>
using TWeakPtr = std::weak_ptr<T>;
```

**使用场景**：
- 避免循环引用
- 观察者模式
- 缓存系统

**示例**：
```cpp
class Material {
    TWeakPtr<Texture> m_texture; // 弱引用，避免循环依赖
public:
    void render() {
        if (auto texture = m_texture.lock()) {
            // 纹理仍然有效，可以使用
            texture->bind();
        }
    }
};
```

## 容器类型

### 动态数组 - TArray
```cpp
template<typename T>
using TArray = std::vector<T>;
```

**使用场景**：
- 顶点数据存储
- 渲染对象列表
- 动态大小集合

**示例**：
```cpp
// 顶点数据
TArray<Vertex> vertices = {
    {{0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}},
    {{0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}},
    {{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}
};

// 渲染命令列表
TArray<TSharedPtr<IRHICommandList>> commandLists;
```

### 内存视图 - TSpan
```cpp
template<typename T>
using TSpan = std::span<T>;
```

**使用场景**：
- 函数参数（避免容器类型约束）
- 数组视图操作
- 零拷贝数据传递

**示例**：
```cpp
// 接受任何连续内存容器
void uploadVertices(TSpan<const Vertex> vertices) {
    // 可以接受 TArray, std::array, C数组等
}

// 使用方式
TArray<Vertex> dynamicVertices;
std::array<Vertex, 3> staticVertices;

uploadVertices(dynamicVertices); // OK
uploadVertices(staticVertices);  // OK
uploadVertices({vertex1, vertex2, vertex3}); // OK
```

### 哈希映射 - TMap
```cpp
template<typename Key, typename Value>
using TMap = std::unordered_map<Key, Value>;
```

**使用场景**：
- 资源缓存（ID到资源的映射）
- 着色器变体管理
- 快速查找表

**示例**：
```cpp
// 资源管理器
class ResourceManager {
    TMap<String, TSharedPtr<Texture>> m_textureCache;
    TMap<uint32, TSharedPtr<Buffer>> m_bufferCache;
public:
    TSharedPtr<Texture> getTexture(const String& name);
    TSharedPtr<Buffer> getBuffer(uint32 id);
};
```

## 可选类型 - TOptional

```cpp
template<typename T>
using TOptional = std::optional<T>;
```

**使用场景**：
- 可能失败的操作返回值
- 可选配置参数
- 避免使用nullptr的情况

**示例**：
```cpp
// 查找操作可能失败
TOptional<uint32> findMemoryType(uint32 typeFilter, VkMemoryPropertyFlags properties) {
    for (uint32 i = 0; i < memoryProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && 
            (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i; // 找到合适的内存类型
        }
    }
    return {}; // 没有找到
}

// 使用方式
if (auto memoryType = findMemoryType(requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
    // 找到了合适的内存类型
    allocateMemory(*memoryType);
} else {
    MR_LOG_ERROR("No suitable memory type found");
}
```

## 函数类型 - TFunction

```cpp
template<typename T>
using TFunction = std::function<T>;
```

**使用场景**：
- 回调函数
- 事件处理
- 策略模式实现

**示例**：
```cpp
// 异步操作回调
using CompletionCallback = TFunction<void(bool success, const String& error)>;

class AsyncOperation {
public:
    void execute(CompletionCallback callback) {
        // 异步执行操作
        std::thread([callback]() {
            bool success = performOperation();
            callback(success, success ? "" : "Operation failed");
        }).detach();
    }
};
```

## 类型特性和约束

### 概念定义 (C++20)
```cpp
// 资源类型概念
template<typename T>
concept RHIResource = std::is_base_of_v<IRHIResource, T>;

// 可调用类型概念
template<typename F, typename... Args>
concept Callable = std::is_invocable_v<F, Args...>;

// 使用示例
template<RHIResource T>
TSharedPtr<T> createResource(const typename T::DescType& desc) {
    return MakeShared<T>(desc);
}
```

## 平台特定定义

### 编译器检测
```cpp
// 平台检测宏
#if defined(_WIN32)
    #define PLATFORM_WINDOWS 1
#else
    #define PLATFORM_WINDOWS 0
#endif

#if defined(__linux__)
    #define PLATFORM_LINUX 1
#else
    #define PLATFORM_LINUX 0
#endif
```

### 调用约定
```cpp
#if PLATFORM_WINDOWS
    #define MR_API __stdcall
    #define MR_EXPORT __declspec(dllexport)
    #define MR_IMPORT __declspec(dllimport)
#else
    #define MR_API
    #define MR_EXPORT __attribute__((visibility("default")))
    #define MR_IMPORT
#endif
```

## 最佳实践

### 类型选择指南

| 场景 | 推荐类型 | 说明 |
|------|----------|------|
| 索引值 | `uint32` | 足够大，避免溢出 |
| 字节数据 | `uint8` | 节省内存 |
| 浮点计算 | `float32` | GPU友好 |
| 资源共享 | `TSharedPtr` | 自动生命周期管理 |
| 独占所有权 | `TUniquePtr` | 明确所有权语义 |
| 函数参数 | `TSpan`, `StringView` | 避免不必要拷贝 |
| 容器选择 | `TArray` | 通用动态数组 |

### 命名约定
```cpp
// 类型别名使用T前缀
using TMyCustomType = MyCustomType;

// 成员变量使用m_前缀
class MyClass {
    TSharedPtr<Resource> m_resource;
    TArray<uint32> m_indices;
};

// 函数参数使用描述性名称
void setViewport(const Viewport& viewport);
void uploadData(TSpan<const uint8> data);
```

---

## 相关文档
- [内存管理](MemoryManagement.md)
- [日志系统](LoggingSystem.md)
- [RHI资源管理](../RHI/ResourceManagement.md)
