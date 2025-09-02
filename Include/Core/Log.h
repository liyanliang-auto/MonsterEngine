#pragma once

#include <iostream>
#include <string>
#include <string_view>
#include <sstream>
#include <chrono>
#include <iomanip>

namespace MonsterRender {
    
    enum class ELogLevel : uint8_t {
        Trace = 0,
        Debug = 1,
        Info = 2,
        Warning = 3,
        Error = 4,
        Fatal = 5
    };
    
    class Logger {
    public:
        static Logger& getInstance() {
            static Logger instance;
            return instance;
        }
        
        template<typename... Args>
        void log(ELogLevel level, std::string_view format, Args&&... args) {
            if (level < m_minLevel) {
                return;
            }
            
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            
            std::ostringstream oss;
            oss << "[" << std::put_time(std::localtime(&time_t), "%H:%M:%S") << "] ";
            oss << "[" << getLevelString(level) << "] ";
            
            // Simple format string replacement (basic implementation)
            std::string message(format);
            oss << message;
            
            std::cout << oss.str() << std::endl;
            
            if (level == ELogLevel::Fatal) {
                std::abort();
            }
        }
        
        void setMinLevel(ELogLevel level) {
            m_minLevel = level;
        }
        
    private:
        Logger() = default;
        ELogLevel m_minLevel = ELogLevel::Info;
        
        const char* getLevelString(ELogLevel level) {
            switch (level) {
                case ELogLevel::Trace: return "TRACE";
                case ELogLevel::Debug: return "DEBUG";
                case ELogLevel::Info: return "INFO";
                case ELogLevel::Warning: return "WARN";
                case ELogLevel::Error: return "ERROR";
                case ELogLevel::Fatal: return "FATAL";
                default: return "UNKNOWN";
            }
        }
    };
}

// Logging macros
#define MR_LOG_TRACE(message)   MonsterRender::Logger::getInstance().log(MonsterRender::ELogLevel::Trace, message)
#define MR_LOG_DEBUG(message)   MonsterRender::Logger::getInstance().log(MonsterRender::ELogLevel::Debug, message)
#define MR_LOG_INFO(message)    MonsterRender::Logger::getInstance().log(MonsterRender::ELogLevel::Info, message)
#define MR_LOG_WARNING(message) MonsterRender::Logger::getInstance().log(MonsterRender::ELogLevel::Warning, message)
#define MR_LOG_ERROR(message)   MonsterRender::Logger::getInstance().log(MonsterRender::ELogLevel::Error, message)
#define MR_LOG_FATAL(message)   MonsterRender::Logger::getInstance().log(MonsterRender::ELogLevel::Fatal, message)
