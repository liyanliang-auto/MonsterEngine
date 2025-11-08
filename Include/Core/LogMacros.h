#pragma once

#include "LogVerbosity.h"
#include "LogCategory.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <cstdio>
#include <cstdarg>

namespace MonsterRender {

/**
 * 日志输出类
 * Handles actual log output, similar to UE5's FOutputDevice
 */
class FOutputDevice {
public:
    static FOutputDevice& Get() {
        static FOutputDevice instance;
        return instance;
    }
    
    /**
     * 序列化日志消息
     * @param Verbosity - 详细级别
     * @param Category - 日志类别
     * @param Message - 消息内容
     * @param File - 源文件名
     * @param Line - 源文件行号
     */
    void Serialize(ELogVerbosity::Type Verbosity, 
                   const FLogCategory& Category,
                   const char* Message,
                   const char* File = nullptr,
                   int Line = 0);
    
private:
    FOutputDevice() = default;
    
    /** 获取详细级别的字符串表示 */
    const char* GetVerbosityString(ELogVerbosity::Type Verbosity) const;
    
    /** 获取详细级别的颜色代码（Windows控制台） */
    const char* GetVerbosityColorCode(ELogVerbosity::Type Verbosity) const;
};

/**
 * 日志格式化辅助类
 * Helper for formatting log messages
 */
class FLogFormatter {
public:
    template<typename... Args>
    static std::string Format(const char* fmt, Args&&... args) {
        // 计算需要的缓冲区大小
        int size = std::snprintf(nullptr, 0, fmt, std::forward<Args>(args)...);
        if (size <= 0) {
            return fmt;
        }
        
        // 分配缓冲区并格式化
        std::string result(size + 1, '\0');
        std::snprintf(&result[0], size + 1, fmt, std::forward<Args>(args)...);
        result.resize(size); // 移除尾部的null终止符
        
        return result;
    }
    
    // 无参数版本
    static std::string Format(const char* fmt) {
        return std::string(fmt);
    }
};

} // namespace MonsterRender

// =============================================================================
// 日志宏定义 - Log Macro Definitions
// =============================================================================

/**
 * TEXT宏，用于字符串字面量（与UE5兼容）
 * TEXT macro for string literals (compatible with UE5)
 */
#ifndef TEXT
    #define TEXT(x) x
#endif

/**
 * 条件日志宏 - 检查是否应该记录日志
 * Conditional logging - checks if we should log
 */
#define MR_LOG_ACTIVE(CategoryName, Verbosity) \
    (!LogCategory##CategoryName.IsSuppressed(ELogVerbosity::Verbosity))

/**
 * 主日志宏 - MR_LOG (Monster Render Log)
 * Main logging macro, similar to UE5's UE_LOG
 * 
 * 用法 Usage:
 *   MR_LOG(Temp, Warning, TEXT("Hello %s, value: %d"), "World", 42);
 *   MR_LOG(RHI, Error, TEXT("Device creation failed"));
 */
#define MR_LOG(CategoryName, Verbosity, Format, ...) \
    do { \
        if (MR_LOG_ACTIVE(CategoryName, Verbosity)) { \
            std::string formatted = MonsterRender::FLogFormatter::Format(Format, ##__VA_ARGS__); \
            MonsterRender::FOutputDevice::Get().Serialize( \
                MonsterRender::ELogVerbosity::Verbosity, \
                LogCategory##CategoryName, \
                formatted.c_str(), \
                __FILE__, \
                __LINE__ \
            ); \
            if (MonsterRender::ELogVerbosity::Verbosity == MonsterRender::ELogVerbosity::Fatal) { \
                std::abort(); \
            } \
        } \
    } while(0)

/**
 * 条件日志宏 - 仅在条件为真时记录
 * Conditional logging - only logs if condition is true
 * 
 * 用法 Usage:
 *   MR_CLOG(bShouldLog, Temp, Warning, TEXT("Conditional message"));
 */
#define MR_CLOG(Condition, CategoryName, Verbosity, Format, ...) \
    do { \
        if ((Condition) && MR_LOG_ACTIVE(CategoryName, Verbosity)) { \
            std::string formatted = MonsterRender::FLogFormatter::Format(Format, ##__VA_ARGS__); \
            MonsterRender::FOutputDevice::Get().Serialize( \
                MonsterRender::ELogVerbosity::Verbosity, \
                LogCategory##CategoryName, \
                formatted.c_str(), \
                __FILE__, \
                __LINE__ \
            ); \
        } \
    } while(0)

/**
 * 确保宏 - 类似于断言但总是求值
 * Ensure macro - similar to assert but always evaluates
 * 
 * 用法 Usage:
 *   MR_ENSURE(Ptr != nullptr);
 *   MR_ENSURE_MSG(Ptr != nullptr, TEXT("Pointer should not be null"));
 */
#define MR_ENSURE(Condition) \
    (!!(Condition) || \
        (MonsterRender::FOutputDevice::Get().Serialize( \
            MonsterRender::ELogVerbosity::Error, \
            LogCategoryCore, \
            MonsterRender::FLogFormatter::Format("Ensure failed: %s", #Condition).c_str(), \
            __FILE__, \
            __LINE__ \
        ), false))

#define MR_ENSURE_MSG(Condition, Format, ...) \
    (!!(Condition) || \
        (MonsterRender::FOutputDevice::Get().Serialize( \
            MonsterRender::ELogVerbosity::Error, \
            LogCategoryCore, \
            MonsterRender::FLogFormatter::Format(Format, ##__VA_ARGS__).c_str(), \
            __FILE__, \
            __LINE__ \
        ), false))

/**
 * 检查宏 - 在Debug版本中进行断言
 * Check macro - asserts in debug builds
 */
#if defined(_DEBUG) || defined(DEBUG)
    #define MR_CHECK(Condition) \
        do { \
            if (!(Condition)) { \
                MR_LOG(Core, Fatal, TEXT("Check failed: %s"), #Condition); \
            } \
        } while(0)
    
    #define MR_CHECK_MSG(Condition, Format, ...) \
        do { \
            if (!(Condition)) { \
                MR_LOG(Core, Fatal, Format, ##__VA_ARGS__); \
            } \
        } while(0)
#else
    #define MR_CHECK(Condition) do {} while(0)
    #define MR_CHECK_MSG(Condition, Format, ...) do {} while(0)
#endif

/**
 * 验证宏 - 在所有版本中进行检查
 * Verify macro - checks in all builds
 */
#define MR_VERIFY(Condition) \
    do { \
        if (!(Condition)) { \
            MR_LOG(Core, Fatal, TEXT("Verify failed: %s"), #Condition); \
        } \
    } while(0)

#define MR_VERIFY_MSG(Condition, Format, ...) \
    do { \
        if (!(Condition)) { \
            MR_LOG(Core, Fatal, Format, ##__VA_ARGS__); \
        } \
    } while(0)

// =============================================================================
// 兼容旧API的宏定义 - Backward compatibility macros
// =============================================================================

#define MR_LOG_TRACE(message)   MR_LOG(Temp, Verbose, TEXT("%s"), message)
#define MR_LOG_DEBUG(message)   MR_LOG(Temp, Log, TEXT("%s"), message)
#define MR_LOG_INFO(message)    MR_LOG(Temp, Display, TEXT("%s"), message)
#define MR_LOG_WARNING(message) MR_LOG(Temp, Warning, TEXT("%s"), message)
#define MR_LOG_ERROR(message)   MR_LOG(Temp, Error, TEXT("%s"), message)
#define MR_LOG_FATAL(message)   MR_LOG(Temp, Fatal, TEXT("%s"), message)

