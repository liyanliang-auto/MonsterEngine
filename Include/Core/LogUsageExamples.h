#pragma once

/**
 * MonsterRender 日志系统使用示例
 * MonsterRender Logging System Usage Examples
 * 
 * 本文件提供日志系统的使用示例和最佳实践
 * This file provides usage examples and best practices for the logging system
 */

/*
================================================================================
基本用法 Basic Usage
================================================================================

1. 使用预定义的日志类别记录日志
   Log with predefined categories:

   MR_LOG(Temp, Display, TEXT("Hello, World!"));
   MR_LOG(Core, Warning, TEXT("System warning"));
   MR_LOG(RHI, Error, TEXT("RHI error occurred"));

2. 格式化输出
   Formatted output:

   int value = 42;
   float pi = 3.14159f;
   const char* name = "MonsterRender";
   
   MR_LOG(Temp, Display, TEXT("Integer: %d, Float: %.2f, String: %s"), value, pi, name);

3. 不同的日志级别（从高到低）
   Different log levels (from high to low):
   
   MR_LOG(Category, Fatal, TEXT("..."));      // 致命错误，程序会崩溃
   MR_LOG(Category, Error, TEXT("..."));      // 错误
   MR_LOG(Category, Warning, TEXT("..."));    // 警告
   MR_LOG(Category, Display, TEXT("..."));    // 显示级别信息
   MR_LOG(Category, Log, TEXT("..."));        // 一般日志
   MR_LOG(Category, Verbose, TEXT("..."));    // 详细日志
   MR_LOG(Category, VeryVerbose, TEXT("...")); // 非常详细的日志

================================================================================
条件日志 Conditional Logging
================================================================================

1. 使用 MR_CLOG 进行条件日志记录
   Use MR_CLOG for conditional logging:

   bool debugMode = true;
   MR_CLOG(debugMode, Temp, Display, TEXT("Debug mode is active"));
   
   void* ptr = GetPointer();
   MR_CLOG(ptr == nullptr, Temp, Error, TEXT("Pointer is null!"));

================================================================================
断言和验证 Asserts and Verifications
================================================================================

1. MR_ENSURE - 总是求值，失败时记录错误但不崩溃
   MR_ENSURE - Always evaluates, logs on failure but doesn't crash:

   void* ptr = Allocate();
   if (!MR_ENSURE(ptr != nullptr)) {
       // 处理错误
       return;
   }

2. MR_ENSURE_MSG - 带自定义消息的Ensure
   MR_ENSURE_MSG - Ensure with custom message:

   if (!MR_ENSURE_MSG(count > 0, TEXT("Count must be positive, got: %d"), count)) {
       return;
   }

3. MR_CHECK - 仅在Debug版本中检查
   MR_CHECK - Only checks in Debug builds:

   MR_CHECK(IsValid());
   MR_CHECK_MSG(index < size, TEXT("Index out of bounds: %d >= %d"), index, size);

4. MR_VERIFY - 在所有版本中检查，失败导致Fatal错误
   MR_VERIFY - Checks in all builds, failure is Fatal:

   MR_VERIFY(Initialize());
   MR_VERIFY_MSG(device != nullptr, TEXT("Device must be initialized"));

================================================================================
自定义日志类别 Custom Log Categories
================================================================================

1. 在头文件中声明：
   Declare in header file:

   // MySystem.h
   DECLARE_LOG_CATEGORY_EXTERN(MySystem, Log, All);

2. 在cpp文件中定义：
   Define in cpp file:

   // MySystem.cpp
   #include "Core/Log.h"
   DEFINE_LOG_CATEGORY(MySystem);
   
   或者使用自定义默认级别：
   Or with custom default verbosity:
   
   DEFINE_LOG_CATEGORY_WITH_VERBOSITY(MySystem, Display);

3. 使用自定义类别：
   Use custom category:

   MR_LOG(MySystem, Display, TEXT("My system initialized"));
   MR_LOG(MySystem, Warning, TEXT("Configuration file not found"));

================================================================================
运行时控制日志级别 Runtime Log Level Control
================================================================================

1. 修改日志类别的详细级别
   Modify log category verbosity:

   // 设置为只显示警告和错误
   LogCategoryTemp.SetVerbosity(ELogVerbosity::Warning);
   
   // 显示所有日志
   LogCategoryCore.SetVerbosity(ELogVerbosity::VeryVerbose);
   
   // 禁用所有日志
   LogCategoryRHI.SetVerbosity(ELogVerbosity::NoLogging);

2. 检查是否会记录
   Check if logging is active:

   if (MR_LOG_ACTIVE(Temp, Verbose)) {
       // 执行昂贵的日志准备操作
       std::string complexData = GenerateComplexData();
       MR_LOG(Temp, Verbose, TEXT("Data: %s"), complexData.c_str());
   }

================================================================================
常见使用场景 Common Usage Scenarios
================================================================================

1. 系统初始化
   System Initialization:

   MR_LOG(Core, Display, TEXT("Initializing %s version %s"), ENGINE_NAME, ENGINE_VERSION);
   
   if (!InitializeSubsystem()) {
       MR_LOG(Core, Fatal, TEXT("Failed to initialize subsystem"));
   }
   
   MR_LOG(Core, Display, TEXT("Initialization complete"));

2. 资源加载
   Resource Loading:

   const char* filePath = "Data/mesh.obj";
   MR_LOG(Renderer, Log, TEXT("Loading mesh: %s"), filePath);
   
   if (!FileExists(filePath)) {
       MR_LOG(Renderer, Error, TEXT("File not found: %s"), filePath);
       return false;
   }
   
   MR_LOG(Renderer, Display, TEXT("Mesh loaded successfully: %d vertices"), vertexCount);

3. GPU操作
   GPU Operations:

   MR_LOG(RHI, Verbose, TEXT("Creating buffer: size=%zu, usage=%d"), size, usage);
   
   if (FAILED(result)) {
       MR_LOG(RHI, Error, TEXT("Buffer creation failed: error=0x%X"), result);
       return nullptr;
   }
   
   MR_LOG(RHI, Verbose, TEXT("Buffer created: handle=%p"), buffer);

4. 性能监控
   Performance Monitoring:

   float frameTime = GetFrameTime();
   if (frameTime > TARGET_FRAME_TIME) {
       MR_LOG(Renderer, Warning, TEXT("Frame time exceeded: %.2fms > %.2fms"), 
              frameTime, TARGET_FRAME_TIME);
   }
   
   MR_CLOG(frameTime > 33.0f, Renderer, Error, TEXT("Severe frame drop: %.2fms"), frameTime);

5. 调试信息
   Debug Information:

   MR_LOG(Temp, VeryVerbose, TEXT("Function entered: %s"), __FUNCTION__);
   MR_LOG(Temp, VeryVerbose, TEXT("Processing item %d of %d"), i, total);
   MR_LOG(Temp, Verbose, TEXT("State changed from %d to %d"), oldState, newState);

================================================================================
最佳实践 Best Practices
================================================================================

1. 选择合适的日志级别：
   - Fatal: 只用于无法恢复的致命错误
   - Error: 用于错误条件，但程序可以继续运行
   - Warning: 用于异常情况但不是错误
   - Display: 用于重要的用户可见信息
   - Log: 用于一般的开发信息
   - Verbose/VeryVerbose: 用于详细的调试信息

2. 选择合适的日志类别：
   使用描述性的类别名称，便于过滤和搜索
   
3. 提供上下文信息：
   日志消息应该包含足够的上下文信息以便于调试
   
4. 避免在热路径中使用高频日志：
   对于高频调用的代码，使用Verbose或VeryVerbose级别
   
5. 使用MR_LOG_ACTIVE进行昂贵的日志操作：
   如果日志消息的生成很昂贵，先检查是否会记录

6. 对于指针和资源，总是验证其有效性：
   使用MR_ENSURE或MR_VERIFY检查关键资源

================================================================================
中文日志支持 Chinese Log Support
================================================================================

MonsterRender日志系统完全支持中文及其他Unicode字符！
MonsterRender logging system fully supports Chinese and other Unicode characters!

1. Windows平台自动配置：
   Windows platform auto-configuration:
   
   - 首次日志输出时自动设置控制台为UTF-8模式
   - 支持简体中文、繁体中文、日文、韩文等
   - 支持特殊符号和Emoji（部分终端）

2. 中文日志示例：
   Chinese log examples:
   
   MR_LOG(Core, Display, TEXT("MonsterRender 引擎启动中..."));
   MR_LOG(Temp, Warning, TEXT("警告：配置文件缺少某些选项"));
   MR_LOG(RHI, Error, TEXT("错误：无法创建渲染设备"));

3. 格式化中文输出：
   Formatted Chinese output:
   
   const char* playerName = "张三";
   int level = 42;
   float health = 85.5f;
   MR_LOG(Temp, Display, TEXT("玩家：%s，等级：%d，生命值：%.1f%%"), 
          playerName, level, health);

4. 中文错误消息：
   Chinese error messages:
   
   const char* fileName = "数据/角色/勇者.mesh";
   MR_LOG(Texture, Error, TEXT("加载失败：找不到文件 %s"), fileName);

5. 中文断言：
   Chinese assertions:
   
   MR_ENSURE_MSG(ptr != nullptr, TEXT("指针不能为空！"));
   MR_CHECK_MSG(count > 0, TEXT("数量必须大于0，当前值=%d"), count);

6. 支持的字符集：
   Supported character sets:
   
   - 简体中文：你好，世界！
   - 繁体中文：繁體中文測試
   - 日文：モンスターエンジン
   - 韩文：몬스터 엔진
   - 特殊符号：【】《》""''±×÷≈≠
   - Emoji：✅ ❌ ⚠️ 🎮 🖥️

7. 注意事项：
   Notes:
   
   - 源代码文件应保存为UTF-8编码
   - 使用TEXT宏包裹所有字符串字面量
   - Windows Terminal和PowerShell支持最好
   - 旧版cmd.exe可能显示不完整

================================================================================
兼容性说明 Compatibility Notes
================================================================================

为了向后兼容，保留了旧的简化宏：
For backward compatibility, old simplified macros are retained:

MR_LOG_TRACE(message)   -> MR_LOG(Temp, Verbose, TEXT("%s"), message)
MR_LOG_DEBUG(message)   -> MR_LOG(Temp, Log, TEXT("%s"), message)
MR_LOG_INFO(message)    -> MR_LOG(Temp, Display, TEXT("%s"), message)
MR_LOG_WARNING(message) -> MR_LOG(Temp, Warning, TEXT("%s"), message)
MR_LOG_ERROR(message)   -> MR_LOG(Temp, Error, TEXT("%s"), message)
MR_LOG_FATAL(message)   -> MR_LOG(Temp, Fatal, TEXT("%s"), message)

建议新代码使用完整的MR_LOG语法以获得更好的类别和格式化支持。
It's recommended to use the full MR_LOG syntax in new code for better category and formatting support.

================================================================================
*/

// 本文件仅用于文档目的，不包含可执行代码
// This file is for documentation purposes only and contains no executable code

