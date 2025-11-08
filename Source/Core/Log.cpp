#include "Core/Log.h"
#include <mutex>
#include <ctime>

#ifdef _WIN32
    #include <Windows.h>
    #undef ERROR // Windows.h defines ERROR macro
#endif

namespace MonsterRender {

// =============================================================================
// 定义预声明的日志类别 - Define predeclared log categories
// =============================================================================

DEFINE_LOG_CATEGORY_WITH_VERBOSITY(Temp, Log);
DEFINE_LOG_CATEGORY_WITH_VERBOSITY(Core, Log);
DEFINE_LOG_CATEGORY_WITH_VERBOSITY(RHI, Log);
DEFINE_LOG_CATEGORY_WITH_VERBOSITY(Renderer, Log);
DEFINE_LOG_CATEGORY_WITH_VERBOSITY(Memory, Log);
DEFINE_LOG_CATEGORY_WITH_VERBOSITY(Vulkan, Log);
DEFINE_LOG_CATEGORY_WITH_VERBOSITY(D3D12, Log);
DEFINE_LOG_CATEGORY_WITH_VERBOSITY(Shader, Log);
DEFINE_LOG_CATEGORY_WITH_VERBOSITY(Texture, Log);
DEFINE_LOG_CATEGORY_WITH_VERBOSITY(Input, Log);

// =============================================================================
// FOutputDevice 实现 - FOutputDevice Implementation
// =============================================================================

static std::mutex g_LogMutex; // 保护多线程日志输出

void FOutputDevice::Serialize(ELogVerbosity::Type Verbosity,
                              const FLogCategory& Category,
                              const char* Message,
                              const char* File,
                              int Line) {
    std::lock_guard<std::mutex> lock(g_LogMutex);
    
    // 获取当前时间
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    struct tm timeinfo;
#ifdef _WIN32
    localtime_s(&timeinfo, &time_t_now);
#else
    localtime_r(&time_t_now, &timeinfo);
#endif
    
    // 设置控制台颜色（Windows）
#ifdef _WIN32
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    WORD colorAttribute = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    
    switch (Verbosity) {
        case ELogVerbosity::Fatal:
        case ELogVerbosity::Error:
            colorAttribute = FOREGROUND_RED | FOREGROUND_INTENSITY;
            break;
        case ELogVerbosity::Warning:
            colorAttribute = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
            break;
        case ELogVerbosity::Display:
            colorAttribute = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
            break;
        case ELogVerbosity::Log:
            colorAttribute = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
            break;
        case ELogVerbosity::Verbose:
        case ELogVerbosity::VeryVerbose:
            colorAttribute = FOREGROUND_INTENSITY;
            break;
    }
    
    SetConsoleTextAttribute(hConsole, colorAttribute);
#endif
    
    // 输出日志格式：[时间][类别][级别] 消息 (文件:行号)
    std::cout << "[";
    std::cout << std::setfill('0') << std::setw(2) << timeinfo.tm_hour << ":"
              << std::setfill('0') << std::setw(2) << timeinfo.tm_min << ":"
              << std::setfill('0') << std::setw(2) << timeinfo.tm_sec << "."
              << std::setfill('0') << std::setw(3) << ms.count();
    std::cout << "]";
    
    std::cout << "[" << Category.CategoryName << "]";
    std::cout << "[" << GetVerbosityString(Verbosity) << "] ";
    std::cout << Message;
    
    // 在详细级别或错误时输出文件位置
    if (Verbosity <= ELogVerbosity::Warning || Verbosity >= ELogVerbosity::Verbose) {
        if (File != nullptr) {
            // 只显示文件名，不显示完整路径
            const char* fileName = File;
            const char* lastSlash = strrchr(File, '\\');
            if (lastSlash == nullptr) {
                lastSlash = strrchr(File, '/');
            }
            if (lastSlash != nullptr) {
                fileName = lastSlash + 1;
            }
            
            std::cout << " (" << fileName << ":" << Line << ")";
        }
    }
    
    std::cout << std::endl;
    
    // 恢复控制台颜色
#ifdef _WIN32
    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
#endif
}

const char* FOutputDevice::GetVerbosityString(ELogVerbosity::Type Verbosity) const {
    switch (Verbosity) {
        case ELogVerbosity::NoLogging:    return "None";
        case ELogVerbosity::Fatal:        return "FATAL";
        case ELogVerbosity::Error:        return "ERROR";
        case ELogVerbosity::Warning:      return "WARN";
        case ELogVerbosity::Display:      return "DISPLAY";
        case ELogVerbosity::Log:          return "LOG";
        case ELogVerbosity::Verbose:      return "VERBOSE";
        case ELogVerbosity::VeryVerbose:  return "VVERBOSE";
        default:                          return "UNKNOWN";
    }
}

const char* FOutputDevice::GetVerbosityColorCode(ELogVerbosity::Type Verbosity) const {
    // ANSI颜色代码（用于支持的终端）
    switch (Verbosity) {
        case ELogVerbosity::Fatal:
        case ELogVerbosity::Error:        return "\033[1;31m"; // 红色
        case ELogVerbosity::Warning:      return "\033[1;33m"; // 黄色
        case ELogVerbosity::Display:      return "\033[1;36m"; // 青色
        case ELogVerbosity::Log:          return "\033[0;37m"; // 白色
        case ELogVerbosity::Verbose:
        case ELogVerbosity::VeryVerbose:  return "\033[0;90m"; // 灰色
        default:                          return "\033[0m";    // 重置
    }
}

} // namespace MonsterRender
