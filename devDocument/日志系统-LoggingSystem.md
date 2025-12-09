# 日志系统

MonsterRender的日志系统提供了分级日志记录、格式化输出和调试支持，是引擎调试和监控的重要工具。

## 设计特点

### 1. 分级日志
支持多个日志级别，便于控制输出详细程度：

```cpp
enum class ELogLevel : uint8_t {
    Trace = 0,    // 详细跟踪信息
    Debug = 1,    // 调试信息  
    Info = 2,     // 一般信息
    Warning = 3,  // 警告
    Error = 4,    // 错误
    Fatal = 5     // 致命错误（程序终止）
};
```

### 2. 单例模式
使用线程安全的单例模式确保全局访问：

```cpp
class Logger {
public:
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }
};
```

### 3. 时间戳支持
自动添加时间戳，便于问题追踪：

```cpp
// 输出格式：[HH:MM:SS] [LEVEL] Message
[14:30:25] [INFO] Engine initialized successfully
[14:30:26] [ERROR] Failed to create buffer
```

## 基本使用

### 设置日志级别
```cpp
// 设置最小日志级别（只输出此级别及以上的日志）
Logger::getInstance().setMinLevel(ELogLevel::Info);
```

### 日志记录宏
```cpp
MR_LOG_TRACE("Detailed trace information");
MR_LOG_DEBUG("Debug information"); 
MR_LOG_INFO("General information");
MR_LOG_WARNING("Warning message");
MR_LOG_ERROR("Error occurred");
MR_LOG_FATAL("Fatal error - program will terminate");
```

### 使用示例
```cpp
void Engine::initialize() {
    MR_LOG_INFO("Starting engine initialization...");
    
    if (!createRHIDevice()) {
        MR_LOG_ERROR("Failed to create RHI device");
        return false;
    }
    
    MR_LOG_DEBUG("RHI device created successfully");
    MR_LOG_INFO("Engine initialization completed");
    return true;
}
```

## 配置和定制

### 运行时级别调整
```cpp
// 开发阶段：显示所有日志
#ifdef _DEBUG
    Logger::getInstance().setMinLevel(ELogLevel::Trace);
#else
    // 发布版本：只显示重要信息
    Logger::getInstance().setMinLevel(ELogLevel::Warning);
#endif
```

### 条件日志
```cpp
void VulkanDevice::createBuffer() {
    MR_LOG_DEBUG("Creating buffer with size: " + std::to_string(size));
    
    VkResult result = vkCreateBuffer(...);
    if (result != VK_SUCCESS) {
        MR_LOG_ERROR("Vulkan buffer creation failed with code: " + std::to_string(result));
        return nullptr;
    }
    
    MR_LOG_TRACE("Buffer created successfully, handle: " + std::to_string(handle));
}
```

## 性能考虑

### 编译时优化
在发布版本中可以完全禁用某些日志级别：

```cpp
// 发布版本中禁用TRACE和DEBUG日志
#ifdef NDEBUG
    #define MR_LOG_TRACE(message) ((void)0)
    #define MR_LOG_DEBUG(message) ((void)0) 
#else
    #define MR_LOG_TRACE(message) MonsterRender::Logger::getInstance().log(MonsterRender::ELogLevel::Trace, message)
    #define MR_LOG_DEBUG(message) MonsterRender::Logger::getInstance().log(MonsterRender::ELogLevel::Debug, message)
#endif
```

### 字符串优化
避免不必要的字符串构造：

```cpp
// 低效：即使不输出也会构造字符串
MR_LOG_DEBUG("Processing vertex " + std::to_string(i) + " of " + std::to_string(count));

// 高效：先检查日志级别
if (Logger::getInstance().willLog(ELogLevel::Debug)) {
    MR_LOG_DEBUG("Processing vertex " + std::to_string(i) + " of " + std::to_string(count));
}
```

## 扩展功能

### 文件输出（未来功能）
```cpp
// 计划支持文件输出
Logger::getInstance().setOutputFile("engine.log");
Logger::getInstance().enableFileOutput(true);
```

### 格式化支持（未来功能）
```cpp
// 计划支持printf风格格式化
MR_LOG_INFO("Created buffer: size=%d, usage=%x", size, usage);
```

### 异步日志（未来功能）
```cpp
// 计划支持异步日志记录减少性能影响
Logger::getInstance().setAsyncMode(true);
```

## 调试集成

### RHI调试标记
日志系统与RHI调试标记系统集成：

```cpp
void VulkanCommandList::beginEvent(const String& name) {
    MR_LOG_TRACE("Begin debug event: " + name);
    
    // 发送到图形调试工具
    VkDebugUtilsLabelEXT labelInfo{};
    labelInfo.pLabelName = name.c_str();
    vkCmdBeginDebugUtilsLabelEXT(m_commandBuffer, &labelInfo);
}
```

### 错误上下文
结合断言系统提供丰富的错误上下文：

```cpp
#define MR_CHECK_MSG(condition, message) \
    do { \
        if (!(condition)) { \
            MR_LOG_ERROR("Check failed: " message); \
            MR_LOG_ERROR("  File: " __FILE__ ":" STRINGIFY(__LINE__)); \
            MR_LOG_ERROR("  Function: " __FUNCTION__); \
            std::abort(); \
        } \
    } while(0)
```

## 最佳实践

### 日志级别选择指南
- **TRACE**: 详细的执行流程，循环内部状态
- **DEBUG**: 函数调用，资源创建/销毁
- **INFO**: 重要的程序状态变化，初始化完成
- **WARNING**: 不影响功能但需要注意的问题
- **ERROR**: 功能失败但程序可以继续
- **FATAL**: 无法恢复的错误，程序必须终止

### 消息格式建议
```cpp
// 好的日志消息：明确、具体、包含上下文
MR_LOG_ERROR("Failed to create vertex buffer: size=1024, usage=VertexBuffer, error=OUT_OF_MEMORY");

// 不好的日志消息：模糊、无上下文
MR_LOG_ERROR("Creation failed");
```

### 敏感信息保护
```cpp
// 避免记录敏感信息
MR_LOG_DEBUG("User logged in: username=" + username); // 不要这样做

// 使用匿名化信息
MR_LOG_DEBUG("User logged in: userID=" + std::to_string(hashedID));
```

---

## 相关文档
- [断言和调试](AssertionSystem.md)
- [核心类型系统](CoreTypes.md)
- [RHI调试支持](../RHI/RHIOverview.md#调试支持)
