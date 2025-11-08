# MonsterRender 日志系统 - UE5风格实现

## 概述

MonsterRender的日志系统参考UE5的`UE_LOG`架构设计，提供了强大、灵活的日志记录功能。

## 文件结构

```
Include/Core/
  ├── LogVerbosity.h      # 日志详细级别定义
  ├── LogCategory.h       # 日志类别系统
  ├── LogMacros.h         # 日志宏定义
  ├── Log.h              # 统一入口点
  └── LogUsageExamples.h  # 使用示例和文档

Source/Core/
  └── Log.cpp             # 日志系统实现

Source/Tests/
  └── LogSystemTest.cpp   # 日志系统测试
```

## 核心特性

### 1. 日志详细级别 (LogVerbosity)

从高到低的日志级别：

- **Fatal** - 致命错误，导致程序崩溃
- **Error** - 错误，程序可以继续运行
- **Warning** - 警告信息
- **Display** - 重要的显示信息
- **Log** - 一般日志信息
- **Verbose** - 详细调试信息
- **VeryVerbose** - 非常详细的调试信息

### 2. 日志类别 (LogCategory)

预定义的日志类别：

- `LogTemp` - 临时日志
- `LogCore` - 核心系统
- `LogRHI` - 渲染硬件接口
- `LogRenderer` - 渲染器
- `LogMemory` - 内存管理
- `LogVulkan` - Vulkan后端
- `LogD3D12` - D3D12后端
- `LogShader` - 着色器系统
- `LogTexture` - 纹理系统
- `LogInput` - 输入系统

### 3. 主要宏定义

#### MR_LOG - 主日志宏

```cpp
MR_LOG(Category, Verbosity, Format, ...);
```

**示例：**

```cpp
// 基本用法
MR_LOG(Temp, Display, TEXT("Hello, World!"));

// 格式化输出
int health = 100;
MR_LOG(Temp, Display, TEXT("Player health: %d"), health);

// 多参数
float x = 10.5f, y = 20.3f;
MR_LOG(Renderer, Verbose, TEXT("Position: (%.2f, %.2f)"), x, y);

// 错误信息
MR_LOG(RHI, Error, TEXT("Failed to create buffer: size=%zu"), size);
```

#### MR_CLOG - 条件日志

```cpp
bool debugMode = true;
MR_CLOG(debugMode, Temp, Display, TEXT("Debug mode active"));
```

#### MR_ENSURE - 断言但不崩溃

```cpp
void* ptr = Allocate();
if (!MR_ENSURE(ptr != nullptr)) {
    // 处理错误
    return;
}

// 带消息
MR_ENSURE_MSG(count > 0, TEXT("Count must be positive: %d"), count);
```

#### MR_CHECK - Debug版本断言

```cpp
MR_CHECK(IsValid());
MR_CHECK_MSG(index < size, TEXT("Index %d out of bounds"), index);
```

#### MR_VERIFY - 所有版本的严格检查

```cpp
MR_VERIFY(Initialize());
MR_VERIFY_MSG(device != nullptr, TEXT("Device not initialized"));
```

## 快速开始

### 1. 基本使用

```cpp
#include "Core/Log.h"

void MyFunction() {
    // 简单日志
    MR_LOG(Temp, Display, TEXT("Function called"));
    
    // 带参数
    int value = 42;
    MR_LOG(Temp, Display, TEXT("Value is: %d"), value);
    
    // 不同级别
    MR_LOG(Core, Warning, TEXT("This is a warning"));
    MR_LOG(Core, Error, TEXT("This is an error"));
}
```

### 2. 创建自定义日志类别

**在头文件中 (MySystem.h):**

```cpp
#include "Core/Log.h"

// 声明日志类别
DECLARE_LOG_CATEGORY_EXTERN(MySystem, Log, All);

class MySystem {
    void DoWork();
};
```

**在实现文件中 (MySystem.cpp):**

```cpp
#include "MySystem.h"

// 定义日志类别
DEFINE_LOG_CATEGORY(MySystem);

void MySystem::DoWork() {
    MR_LOG(MySystem, Display, TEXT("System is working"));
    MR_LOG(MySystem, Verbose, TEXT("Processing data..."));
}
```

### 3. 运行时控制日志级别

```cpp
// 设置为只显示警告和更高级别
LogCategoryTemp.SetVerbosity(ELogVerbosity::Warning);

// 设置为显示所有日志
LogCategoryCore.SetVerbosity(ELogVerbosity::VeryVerbose);

// 完全禁用
LogCategoryRHI.SetVerbosity(ELogVerbosity::NoLogging);
```

### 4. 性能优化 - 条件检查

```cpp
// 对于昂贵的日志操作，先检查是否会记录
if (MR_LOG_ACTIVE(Temp, Verbose)) {
    std::string complexData = GenerateComplexData(); // 昂贵的操作
    MR_LOG(Temp, Verbose, TEXT("Data: %s"), complexData.c_str());
}
```

## 实际应用场景

### 场景1：系统初始化

```cpp
void InitializeEngine() {
    MR_LOG(Core, Display, TEXT("MonsterRender Engine v1.0 starting..."));
    
    if (!InitializeMemorySystem()) {
        MR_LOG(Core, Fatal, TEXT("Failed to initialize memory system"));
        return;
    }
    MR_LOG(Core, Display, TEXT("Memory system initialized"));
    
    if (!InitializeRHI()) {
        MR_LOG(Core, Error, TEXT("RHI initialization failed"));
        return;
    }
    MR_LOG(Core, Display, TEXT("RHI initialized successfully"));
    
    MR_LOG(Core, Display, TEXT("Engine initialization complete"));
}
```

### 场景2：资源加载

```cpp
bool LoadTexture(const char* path) {
    MR_LOG(Texture, Log, TEXT("Loading texture: %s"), path);
    
    if (!FileExists(path)) {
        MR_LOG(Texture, Error, TEXT("Texture file not found: %s"), path);
        return false;
    }
    
    size_t fileSize = GetFileSize(path);
    MR_LOG(Texture, Verbose, TEXT("Texture size: %zu bytes"), fileSize);
    
    // 加载纹理...
    
    MR_LOG(Texture, Display, TEXT("Texture loaded: %s"), path);
    return true;
}
```

### 场景3：GPU操作

```cpp
void CreateBuffer(size_t size) {
    MR_LOG(RHI, Verbose, TEXT("Creating buffer: size=%zu"), size);
    
    VkBufferCreateInfo createInfo = {};
    // 设置createInfo...
    
    VkBuffer buffer;
    VkResult result = vkCreateBuffer(device, &createInfo, nullptr, &buffer);
    
    if (result != VK_SUCCESS) {
        MR_LOG(Vulkan, Error, TEXT("vkCreateBuffer failed: error=%d"), result);
        return;
    }
    
    MR_LOG(RHI, Verbose, TEXT("Buffer created: handle=%p"), buffer);
}
```

### 场景4：性能监控

```cpp
void RenderFrame() {
    float startTime = GetTime();
    
    // 渲染逻辑...
    
    float frameTime = GetTime() - startTime;
    
    MR_CLOG(frameTime > 16.0f, Renderer, Warning, 
            TEXT("Frame time exceeded: %.2fms"), frameTime);
    
    MR_CLOG(frameTime > 33.0f, Renderer, Error, 
            TEXT("Severe frame drop: %.2fms"), frameTime);
    
    MR_LOG(Renderer, VeryVerbose, TEXT("Frame rendered in %.2fms"), frameTime);
}
```

## 向后兼容性

保留了旧的简化API：

```cpp
MR_LOG_TRACE("Trace message");
MR_LOG_DEBUG("Debug message");
MR_LOG_INFO("Info message");
MR_LOG_WARNING("Warning message");
MR_LOG_ERROR("Error message");
// MR_LOG_FATAL("Fatal message"); // 会导致崩溃
```

**建议：** 新代码使用完整的`MR_LOG`语法以获得更好的类别管理和格式化支持。

## 日志输出格式

```
[时间][类别][级别] 消息 (文件名:行号)
```

**示例输出：**

```
[14:23:45.123][Core][DISPLAY] Engine initialized
[14:23:45.156][RHI][VERBOSE] Creating device (VulkanDevice.cpp:234)
[14:23:45.178][Renderer][WARN] Frame time exceeded: 18.45ms
[14:23:45.201][Memory][ERROR] Allocation failed: size=1048576 (FMemory.cpp:156)
```

## 编译配置

### Debug版本
- 默认显示所有级别的日志（包括Verbose和VeryVerbose）
- 启用MR_CHECK宏
- 包含文件名和行号信息

### Release版本
- 默认只显示Log级别及以上的日志
- 禁用MR_CHECK宏（但MR_VERIFY仍然有效）
- 优化的日志输出

## 最佳实践

1. **选择合适的日志级别**
   - 使用Fatal只用于无法恢复的致命错误
   - Error用于错误但程序可以继续
   - Warning用于异常但可以处理的情况
   - Display用于用户可见的重要信息
   - Log用于一般开发信息
   - Verbose/VeryVerbose用于详细调试

2. **使用描述性的日志类别**
   - 为不同模块创建专门的日志类别
   - 便于过滤和调试特定系统

3. **提供足够的上下文**
   - 包含相关的变量值
   - 说明当前操作和状态

4. **避免高频日志**
   - 在热路径中使用Verbose级别
   - 考虑使用MR_LOG_ACTIVE进行条件检查

5. **验证关键资源**
   - 使用MR_ENSURE检查指针和资源
   - 使用MR_VERIFY进行关键性验证

## 测试

运行日志系统测试：

```cpp
#include "Core/Log.h"

namespace MonsterRender {
    void RunLogSystemTests(); // 在LogSystemTest.cpp中定义
}

int main() {
    MonsterRender::RunLogSystemTests();
    return 0;
}
```

## 与UE5的对比

| UE5 | MonsterRender | 说明 |
|-----|---------------|------|
| `UE_LOG` | `MR_LOG` | 主日志宏 |
| `UE_CLOG` | `MR_CLOG` | 条件日志 |
| `ensure` | `MR_ENSURE` | 非致命断言 |
| `check` | `MR_CHECK` | Debug断言 |
| `verify` | `MR_VERIFY` | Release断言 |
| `DECLARE_LOG_CATEGORY_EXTERN` | `DECLARE_LOG_CATEGORY_EXTERN` | 声明日志类别 |
| `DEFINE_LOG_CATEGORY` | `DEFINE_LOG_CATEGORY` | 定义日志类别 |

## 未来改进

- [ ] 支持日志文件输出
- [ ] 支持日志过滤器
- [ ] 支持彩色终端输出（非Windows）
- [ ] 支持远程日志
- [ ] 支持日志统计和分析
- [ ] 支持自定义日志输出器

## 参考

- UE5 LogMacros.h
- UE5 LogVerbosity.h
- UE5 OutputDevice系统

