/**
 * 中文日志输出测试
 * Chinese Log Output Test
 */

#include "Core/Log.h"

namespace MonsterRender {

/**
 * 测试中文日志输出
 * Test Chinese log output
 */
void TestChineseLogOutput() {
    MR_LOG(Core, Display, TEXT("========================================"));
    MR_LOG(Core, Display, TEXT("   中文日志输出测试"));
    MR_LOG(Core, Display, TEXT("   Chinese Log Output Test"));
    MR_LOG(Core, Display, TEXT("========================================"));
    MR_LOG(Core, Display, TEXT(""));
    
    // 基本中文输出
    MR_LOG(Temp, Display, TEXT("你好，世界！"));
    MR_LOG(Temp, Display, TEXT("MonsterRender 引擎启动中..."));
    
    // 不同日志级别的中文输出
    MR_LOG(Core, Log, TEXT("普通日志：系统正在初始化"));
    MR_LOG(Core, Display, TEXT("显示信息：核心模块已加载"));
    MR_LOG(Core, Warning, TEXT("警告：配置文件缺少某些选项"));
    MR_LOG(Core, Error, TEXT("错误：无法连接到服务器"));
    MR_LOG(Core, Verbose, TEXT("详细日志：正在处理第 1 个任务"));
    
    // 混合中英文
    MR_LOG(RHI, Display, TEXT("RHI初始化：Vulkan 1.3"));
    MR_LOG(Renderer, Display, TEXT("渲染器：已创建 Device"));
    MR_LOG(Memory, Display, TEXT("内存管理器：分配了 1024MB 内存"));
    
    // 带格式化的中文输出
    int playerLevel = 42;
    const char* playerName = "张三";
    float health = 85.5f;
    
    MR_LOG(Temp, Display, TEXT("玩家信息：姓名=%s, 等级=%d, 生命值=%.1f%%"), 
           playerName, playerLevel, health);
    
    // 中文错误消息
    const char* fileName = "数据/角色/勇者.mesh";
    MR_LOG(Texture, Error, TEXT("加载失败：找不到文件 %s"), fileName);
    
    // 性能监控（中文）
    float frameTime = 18.5f;
    int triangleCount = 150000;
    MR_LOG(Renderer, Warning, TEXT("性能警告：帧时间 %.2fms 超出目标, 三角形数量=%d"), 
           frameTime, triangleCount);
    
    // 系统状态（中文）
    size_t totalMemory = 16384; // MB
    size_t usedMemory = 8192;   // MB
    float usage = (float)usedMemory / totalMemory * 100.0f;
    MR_LOG(Memory, Display, TEXT("内存状态：已使用 %zu MB / %zu MB (%.1f%%)"), 
           usedMemory, totalMemory, usage);
    
    // 多行中文日志
    MR_LOG(Core, Display, TEXT(""));
    MR_LOG(Core, Display, TEXT("系统初始化完成："));
    MR_LOG(Core, Display, TEXT("  - 核心系统：正常"));
    MR_LOG(Core, Display, TEXT("  - 渲染系统：正常"));
    MR_LOG(Core, Display, TEXT("  - 内存系统：正常"));
    MR_LOG(Core, Display, TEXT("  - 资源系统：正常"));
    
    // 特殊字符测试
    MR_LOG(Temp, Display, TEXT("特殊字符：【】《》""''、，。！？"));
    MR_LOG(Temp, Display, TEXT("数学符号：±×÷≈≠≤≥"));
    MR_LOG(Temp, Display, TEXT("单位符号：℃ ℉ Ω μ π"));
    
    // 繁体中文测试
    MR_LOG(Temp, Display, TEXT("繁體中文：遊戲引擎渲染系統"));
    
    // 日文假名测试
    MR_LOG(Temp, Display, TEXT("日本語：モンスターエンジン"));
    
    // 韩文测试
    MR_LOG(Temp, Display, TEXT("한국어: 몬스터 엔진"));
    
    // Emoji测试（部分终端支持）
    MR_LOG(Temp, Display, TEXT("Emoji: ✅ ❌ ⚠️ 🎮 🖥️"));
    
    MR_LOG(Core, Display, TEXT(""));
    MR_LOG(Core, Display, TEXT("========================================"));
    MR_LOG(Core, Display, TEXT("   中文日志测试完成"));
    MR_LOG(Core, Display, TEXT("========================================"));
}

/**
 * 测试中文条件日志
 * Test Chinese conditional logging
 */
void TestChineseConditionalLog() {
    bool isDebugMode = true;
    bool hasError = false;
    
    MR_CLOG(isDebugMode, Core, Display, TEXT("调试模式已启用"));
    MR_CLOG(hasError, Core, Error, TEXT("发现错误（不会显示）"));
    
    int errorCode = 404;
    MR_CLOG(errorCode != 0, Core, Warning, TEXT("警告：错误代码 %d"), errorCode);
}

/**
 * 测试中文断言消息
 * Test Chinese assert messages
 */
void TestChineseAssertMessages() {
    void* validPtr = (void*)0x12345678;
    
    // Ensure with Chinese message
    if (!MR_ENSURE_MSG(validPtr != nullptr, TEXT("指针不能为空！"))) {
        MR_LOG(Core, Error, TEXT("指针验证失败"));
    }
    
    int count = 10;
    MR_ENSURE_MSG(count > 0, TEXT("数量必须大于0，当前值=%d"), count);
}

/**
 * 运行所有中文日志测试
 * Run all Chinese log tests
 */
void RunChineseLogTests() {
    TestChineseLogOutput();
    MR_LOG(Temp, Display, TEXT(""));
    
    TestChineseConditionalLog();
    MR_LOG(Temp, Display, TEXT(""));
    
    TestChineseAssertMessages();
}

} // namespace MonsterRender

