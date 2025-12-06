#pragma once

/**
 * Log.h
 * 
 * Main logging header for MonsterEngine.
 * This file provides backward compatibility with the old logging API
 * while using the new UE5-style logging system internally.
 * 
 * New Code Should Use:
 *   #include "Core/Logging/Logging.h"
 *   MR_LOG(LogCategory, Verbosity, Format, ...);
 * 
 * Legacy Code Can Still Use:
 *   MR_LOG_INFO("message");
 *   MR_LOG_WARNING("message");
 *   MR_LOG_ERROR("message");
 */

#include "Core/Logging/Logging.h"

// ============================================================================
// Legacy API (Deprecated - Use MR_LOG with categories instead)
// ============================================================================

namespace MonsterRender {

/**
 * @deprecated Use ELogVerbosity instead
 */
enum class ELogLevel : uint8 {
    Trace = ELogVerbosity::VeryVerbose,
    Debug = ELogVerbosity::Verbose,
    Info = ELogVerbosity::Log,
    Warning = ELogVerbosity::Warning,
    Error = ELogVerbosity::Error,
    Fatal = ELogVerbosity::Fatal
};

/**
 * @deprecated Use the new logging system with categories
 * 
 * Legacy Logger class - wraps the new logging system for backward compatibility.
 */
class Logger {
public:
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    template<typename... Args>
    void log(ELogLevel level, const char* message, Args&&... args) {
        ELogVerbosity::Type verbosity = static_cast<ELogVerbosity::Type>(level);
        GLog->Serialize(message, verbosity, "LogTemp");
        
        if (level == ELogLevel::Fatal) {
            GLog->Flush();
            std::abort();
        }
    }

    void log(ELogLevel level, const std::string& message) {
        log(level, message.c_str());
    }

    void log(ELogLevel level, std::string_view message) {
        log(level, std::string(message).c_str());
    }

    void setMinLevel(ELogLevel level) {
        // Map to new verbosity system
        LogTemp.SetVerbosity(static_cast<ELogVerbosity::Type>(level));
    }

private:
    Logger() = default;
};

} // namespace MonsterRender

// ============================================================================
// Legacy Macros (Backward Compatibility)
// ============================================================================

// These macros are provided for backward compatibility.
// New code should use: MR_LOG(Category, Verbosity, Format, ...)

// Note: The new MR_LOG_* macros are defined in LogMacros.h and use LogTemp category
