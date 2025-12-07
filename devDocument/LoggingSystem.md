# MonsterEngine 日志系统架构文档

## 概述

MonsterEngine 的日志系统参考 UE5 的 `UE_LOG` 架构设计，采用分层架构实现高效、灵活的日志记录。

## 架构图

```
┌─────────────────────────────────────────────────────────────┐
│                      MR_LOG 宏                               │
│  MR_LOG(CategoryName, Verbosity, Format, ...)               │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                   FLogCategory                               │
│  - 编译时 Verbosity 过滤 (if constexpr)                      │
│  - 运行时 Verbosity 过滤 (IsSuppressed)                      │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│            FOutputDeviceRedirector (GLog)                    │
│  - 日志重定向器（单例）                                      │
│  - 管理多个 OutputDevice                                     │
│  - 多线程缓冲与刷新                                          │
└─────────────────────────────────────────────────────────────┘
                              │
              ┌───────────────┼───────────────┐
              ▼               ▼               ▼
┌──────────────────┐ ┌──────────────────┐ ┌──────────────────┐
│ FOutputDeviceFile│ │FOutputDeviceConsole│ │FOutputDeviceDebug│
│   (异步文件写入)  │ │   (彩色控制台)     │ │ (调试器输出)     │
└──────────────────┘ └──────────────────┘ └──────────────────┘
```

## 核心组件

### 1. ELogVerbosity - 日志级别

定义在 `Include/Core/Logging/LogVerbosity.h`：

| 级别 | 值 | 说明 |
|------|---|------|
| NoLogging | 0 | 禁用日志 |
| Fatal | 1 | 致命错误（崩溃） |
| Error | 2 | 错误 |
| Warning | 3 | 警告 |
| Display | 4 | 显示（控制台+文件） |
| Log | 5 | 日志（仅文件） |
| Verbose | 6 | 详细 |
| VeryVerbose | 7 | 非常详细 |

### 2. FLogCategory - 日志分类

定义在 `Include/Core/Logging/LogCategory.h`：

```cpp
// 头文件中声明
DECLARE_LOG_CATEGORY_EXTERN(LogRenderer, Log, All)

// 源文件中定义
DEFINE_LOG_CATEGORY(LogRenderer)

// 或静态定义（仅单文件可见）
DEFINE_LOG_CATEGORY_STATIC(LogMyModule, Log, All)
```

### 3. MR_LOG 宏

定义在 `Include/Core/Logging/LogMacros.h`：

```cpp
// 带分类的日志
MR_LOG(LogRenderer, Warning, "Failed to load texture: %s", filename);
MR_LOG(LogVulkan, Log, "Device initialized successfully");

// 条件日志
MR_CLOG(condition, LogRenderer, Error, "Error occurred");

// 简单日志（使用 LogTemp 分类）
MR_LOG_INFO("Hello World");
MR_LOG_WARNING("Something might be wrong");
MR_LOG_ERROR("Something went wrong!");
```

### 4. FOutputDevice - 输出设备基类

定义在 `Include/Core/Logging/OutputDevice.h`：

```cpp
class FOutputDevice {
public:
    virtual void Serialize(const char* Message, ELogVerbosity::Type Verbosity, const char* Category) = 0;
    virtual void Flush() {}
    virtual void TearDown() {}
    
    virtual bool CanBeUsedOnAnyThread() const { return false; }
    virtual bool CanBeUsedOnMultipleThreads() const { return false; }
    virtual bool CanBeUsedOnPanicThread() const { return false; }
};
```

### 5. FOutputDeviceRedirector (GLog)

定义在 `Include/Core/Logging/OutputDeviceRedirector.h`：

- 单例模式，通过 `GLog` 宏访问
- 管理多个输出设备
- 多线程缓冲支持
- Panic 模式（崩溃处理）

### 6. 具体输出设备

| 类 | 文件 | 功能 |
|----|------|------|
| FOutputDeviceFile | OutputDeviceFile.h/cpp | 异步文件写入 |
| FOutputDeviceConsole | OutputDeviceConsole.h/cpp | 彩色控制台输出 |
| FOutputDeviceDebug | OutputDeviceDebug.h/cpp | 调试器输出 (OutputDebugString) |

## 使用方法

### 初始化日志系统

```cpp
#include "Core/Logging/Logging.h"

int main() {
    // 初始化日志系统
    MonsterRender::InitializeLogging(
        "MyApp.log",  // 日志文件名（nullptr 自动生成）
        true,         // 启用控制台输出
        true,         // 启用调试器输出
        true          // 启用文件输出
    );
    
    // 应用代码...
    MR_LOG(LogCore, Log, "Application started");
    
    // 关闭日志系统
    MonsterRender::ShutdownLogging();
    return 0;
}
```

### 定义自定义日志分类

```cpp
// MyModule.h
#include "Core/Logging/Logging.h"
DECLARE_LOG_CATEGORY_EXTERN(LogMyModule, Log, All)

// MyModule.cpp
DEFINE_LOG_CATEGORY(LogMyModule)

void MyFunction() {
    MR_LOG(LogMyModule, Log, "Function called");
    MR_LOG(LogMyModule, Warning, "Value is %d", value);
}
```

### 多线程日志

```cpp
// 主线程设置
GLog->SetCurrentThreadAsPrimaryThread();

// 其他线程的日志会被缓冲
std::thread worker([]() {
    MR_LOG(LogCore, Log, "Worker thread log");  // 被缓冲
});

// 主线程定期刷新
while (running) {
    GLog->FlushThreadedLogs();  // 刷新缓冲的日志
    // ...
}
```

## 编译时过滤

在 Release 版本中，Verbose 和 VeryVerbose 级别的日志会被完全剔除：

```cpp
// Debug 版本：编译所有日志
// Release 版本：只编译 Fatal, Error, Warning, Display, Log

#ifndef COMPILED_IN_MINIMUM_VERBOSITY
    #if defined(NDEBUG) || defined(MR_SHIPPING)
        #define COMPILED_IN_MINIMUM_VERBOSITY ELogVerbosity::Log
    #else
        #define COMPILED_IN_MINIMUM_VERBOSITY ELogVerbosity::VeryVerbose
    #endif
#endif
```

## 内置日志分类

| 分类 | 用途 |
|------|------|
| LogTemp | 临时日志（默认） |
| LogCore | 核心引擎 |
| LogInit | 初始化 |
| LogExit | 退出 |
| LogMemory | 内存管理 |
| LogRenderer | 渲染器 |
| LogRHI | RHI 层 |
| LogVulkan | Vulkan 后端 |
| LogShaders | 着色器 |
| LogTextures | 纹理 |
| LogPlatform | 平台层 |
| LogWindow | 窗口 |
| LogInput | 输入 |

## 文件结构

```
Include/Core/Logging/
├── Logging.h                 # 主入口头文件
├── LogMacros.h              # MR_LOG 宏定义
├── LogCategory.h            # 日志分类
├── LogVerbosity.h           # 日志级别
├── OutputDevice.h           # 输出设备基类
├── OutputDeviceRedirector.h # 日志重定向器
├── OutputDeviceFile.h       # 文件输出
├── OutputDeviceConsole.h    # 控制台输出
└── OutputDeviceDebug.h      # 调试器输出

Source/Core/Logging/
├── Logging.cpp              # 初始化/关闭
├── LogCategories.cpp        # 内置分类定义
├── OutputDevice.cpp         # 基类实现
├── OutputDeviceRedirector.cpp
├── OutputDeviceFile.cpp
├── OutputDeviceConsole.cpp
└── OutputDeviceDebug.cpp
```

## 与 UE5 的对比

| 特性 | UE5 | MonsterEngine |
|------|-----|---------------|
| 主宏 | UE_LOG | MR_LOG |
| 条件宏 | UE_CLOG | MR_CLOG |
| 全局日志 | GLog | GLog |
| 分类声明 | DECLARE_LOG_CATEGORY_EXTERN | DECLARE_LOG_CATEGORY_EXTERN |
| 分类定义 | DEFINE_LOG_CATEGORY | DEFINE_LOG_CATEGORY |
| 编译时过滤 | if constexpr | if constexpr |
| 异步文件写入 | FAsyncWriter | FAsyncWriter |
| 多线程缓冲 | FBufferedLine | FBufferedLine |
