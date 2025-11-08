/**
 * 日志系统测试和使用示例
 * Log System Test and Usage Examples
 */

#include "Core/Log.h"
#include <string>

// 声明自定义日志类别（在头文件中）
// Declare custom log category (in header file)
DECLARE_LOG_CATEGORY_EXTERN(MyGame, Log, All);

// 定义自定义日志类别（在cpp文件中）
// Define custom log category (in cpp file)
DEFINE_LOG_CATEGORY(MyGame);

namespace MonsterRender {

/**
 * 测试基本日志功能
 * Test basic logging functionality
 */
void TestBasicLogging() {
    MR_LOG(Temp, Display, TEXT("=== Testing Basic Logging ==="));
    
    // 不同的日志级别
    // Different log levels
    MR_LOG(Temp, Verbose, TEXT("This is a verbose message"));
    MR_LOG(Temp, Log, TEXT("This is a log message"));
    MR_LOG(Temp, Display, TEXT("This is a display message"));
    MR_LOG(Temp, Warning, TEXT("This is a warning message"));
    MR_LOG(Temp, Error, TEXT("This is an error message"));
    // MR_LOG(Temp, Fatal, TEXT("This is a fatal message")); // 会导致程序终止
}

/**
 * 测试格式化日志
 * Test formatted logging
 */
void TestFormattedLogging() {
    MR_LOG(Temp, Display, TEXT("=== Testing Formatted Logging ==="));
    
    // 整数格式化
    int health = 100;
    MR_LOG(Temp, Display, TEXT("Player health: %d"), health);
    
    // 浮点数格式化
    float temperature = 36.5f;
    MR_LOG(Temp, Display, TEXT("Temperature: %.2f degrees"), temperature);
    
    // 字符串格式化
    const char* playerName = "Hero";
    MR_LOG(Temp, Display, TEXT("Player name: %s"), playerName);
    
    // 多个参数
    int x = 10, y = 20;
    MR_LOG(Temp, Display, TEXT("Position: (%d, %d)"), x, y);
    
    // 十六进制
    uint32_t colorValue = 0xFF00FF;
    MR_LOG(Temp, Display, TEXT("Color value: 0x%X"), colorValue);
    
    // 指针
    void* ptr = &health;
    MR_LOG(Temp, Display, TEXT("Pointer address: %p"), ptr);
}

/**
 * 测试不同的日志类别
 * Test different log categories
 */
void TestLogCategories() {
    MR_LOG(Temp, Display, TEXT("=== Testing Log Categories ==="));
    
    // 使用预定义的日志类别
    MR_LOG(Core, Display, TEXT("Core system initialized"));
    MR_LOG(RHI, Display, TEXT("RHI device created"));
    MR_LOG(Renderer, Display, TEXT("Renderer initialized"));
    MR_LOG(Memory, Display, TEXT("Memory manager started"));
    MR_LOG(Vulkan, Display, TEXT("Vulkan backend active"));
    MR_LOG(Shader, Display, TEXT("Shader compiled successfully"));
    MR_LOG(Texture, Display, TEXT("Texture loaded: %s"), "hero.png");
    
    // 使用自定义日志类别
    MR_LOG(MyGame, Display, TEXT("Game logic initialized"));
    MR_LOG(MyGame, Warning, TEXT("Quest system not ready"));
}

/**
 * 测试条件日志
 * Test conditional logging
 */
void TestConditionalLogging() {
    MR_LOG(Temp, Display, TEXT("=== Testing Conditional Logging ==="));
    
    bool debugMode = true;
    bool releaseMode = false;
    
    // 只在debugMode为true时记录
    MR_CLOG(debugMode, Temp, Display, TEXT("Debug mode is active"));
    MR_CLOG(releaseMode, Temp, Display, TEXT("This won't be logged"));
    
    // 条件检查示例
    int errorCode = 0;
    MR_CLOG(errorCode != 0, Temp, Error, TEXT("Error occurred with code: %d"), errorCode);
}

/**
 * 测试断言和验证
 * Test asserts and verifications
 */
void TestAssertsAndVerifications() {
    MR_LOG(Temp, Display, TEXT("=== Testing Asserts and Verifications ==="));
    
    void* validPointer = malloc(100);
    void* nullPointer = nullptr;
    
    // Ensure - 总是求值，出错时记录但不崩溃
    if (!MR_ENSURE(validPointer != nullptr)) {
        MR_LOG(Temp, Error, TEXT("validPointer is null!"));
    }
    
    if (!MR_ENSURE_MSG(validPointer != nullptr, TEXT("Pointer should be valid, got: %p"), validPointer)) {
        MR_LOG(Temp, Error, TEXT("Pointer check failed"));
    }
    
    // Check - 只在Debug版本中检查
    MR_CHECK(validPointer != nullptr);
    MR_CHECK_MSG(validPointer != nullptr, TEXT("Pointer must be valid"));
    
    // Verify - 在所有版本中检查，失败会导致Fatal错误
    // MR_VERIFY(validPointer != nullptr); // 如果失败会终止程序
    // MR_VERIFY_MSG(validPointer != nullptr, TEXT("Critical pointer is null!"));
    
    free(validPointer);
}

/**
 * 测试复杂场景
 * Test complex scenarios
 */
void TestComplexScenarios() {
    MR_LOG(Temp, Display, TEXT("=== Testing Complex Scenarios ==="));
    
    // 模拟资源加载
    const char* resourcePath = "Assets/Textures/Character.png";
    bool loadSuccess = true;
    int loadTime = 125; // ms
    
    if (loadSuccess) {
        MR_LOG(Texture, Display, TEXT("Resource loaded: %s (took %dms)"), resourcePath, loadTime);
    } else {
        MR_LOG(Texture, Error, TEXT("Failed to load resource: %s"), resourcePath);
    }
    
    // 模拟GPU操作
    int triangleCount = 15000;
    float frameTime = 16.67f;
    MR_LOG(Renderer, Verbose, TEXT("Drew %d triangles in %.2fms"), triangleCount, frameTime);
    
    // 模拟内存分配
    size_t allocSize = 1024 * 1024; // 1MB
    void* memory = malloc(allocSize);
    
    if (MR_ENSURE(memory != nullptr)) {
        MR_LOG(Memory, Verbose, TEXT("Allocated %zu bytes at %p"), allocSize, memory);
        free(memory);
    }
    
    // 性能警告
    if (frameTime > 16.0f) {
        MR_LOG(Renderer, Warning, TEXT("Frame time exceeded target: %.2fms > 16ms"), frameTime);
    }
}

/**
 * 测试运行时日志级别控制
 * Test runtime log level control
 */
void TestRuntimeLogLevelControl() {
    MR_LOG(Temp, Display, TEXT("=== Testing Runtime Log Level Control ==="));
    
    // 显示当前详细级别
    MR_LOG(Temp, Display, TEXT("Default verbosity level test"));
    MR_LOG(Temp, Verbose, TEXT("Verbose message before change"));
    
    // 修改日志类别的详细级别
    LogCategoryTemp.SetVerbosity(ELogVerbosity::Warning);
    MR_LOG(Temp, Display, TEXT("Display message after verbosity change (won't show)"));
    MR_LOG(Temp, Warning, TEXT("Warning message after verbosity change (will show)"));
    
    // 恢复默认级别
    LogCategoryTemp.SetVerbosity(ELogVerbosity::Log);
    MR_LOG(Temp, Display, TEXT("Restored to default verbosity"));
}

/**
 * 兼容旧API测试
 * Test backward compatibility with old API
 */
void TestBackwardCompatibility() {
    MR_LOG(Temp, Display, TEXT("=== Testing Backward Compatibility ==="));
    
    // 使用旧的简化日志宏
    MR_LOG_TRACE("Trace message using old API");
    MR_LOG_DEBUG("Debug message using old API");
    MR_LOG_INFO("Info message using old API");
    MR_LOG_WARNING("Warning message using old API");
    MR_LOG_ERROR("Error message using old API");
    // MR_LOG_FATAL("Fatal message using old API"); // 会导致程序终止
}

/**
 * 运行所有日志测试
 * Run all log tests
 */
void RunLogSystemTests() {
    MR_LOG(Temp, Display, TEXT(""));
    MR_LOG(Temp, Display, TEXT("========================================"));
    MR_LOG(Temp, Display, TEXT("   MonsterRender Log System Tests"));
    MR_LOG(Temp, Display, TEXT("========================================"));
    MR_LOG(Temp, Display, TEXT(""));
    
    TestBasicLogging();
    MR_LOG(Temp, Display, TEXT(""));
    
    TestFormattedLogging();
    MR_LOG(Temp, Display, TEXT(""));
    
    TestLogCategories();
    MR_LOG(Temp, Display, TEXT(""));
    
    TestConditionalLogging();
    MR_LOG(Temp, Display, TEXT(""));
    
    TestAssertsAndVerifications();
    MR_LOG(Temp, Display, TEXT(""));
    
    TestComplexScenarios();
    MR_LOG(Temp, Display, TEXT(""));
    
    TestRuntimeLogLevelControl();
    MR_LOG(Temp, Display, TEXT(""));
    
    TestBackwardCompatibility();
    MR_LOG(Temp, Display, TEXT(""));
    
    MR_LOG(Temp, Display, TEXT("========================================"));
    MR_LOG(Temp, Display, TEXT("   All Log Tests Completed"));
    MR_LOG(Temp, Display, TEXT("========================================"));
}

} // namespace MonsterRender

