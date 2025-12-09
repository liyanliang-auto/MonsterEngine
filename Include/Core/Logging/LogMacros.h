#pragma once

/**
 * LogMacros.h
 * 
 * Main logging macros following UE5 architecture.
 * Provides compile-time and runtime filtering of log messages.
 * 
 * Usage:
 *   MR_LOG(LogRenderer, Warning, "Failed to load texture: %s", filename);
 *   MR_LOG(LogCore, Log, "Initialization complete");
 */

#include "Core/CoreTypes.h"
#include "Core/Logging/LogVerbosity.h"
#include "Core/Logging/LogCategory.h"
#include "Core/Logging/OutputDeviceRedirector.h"
#include <cstdio>
#include <cstdarg>
#include <string>

namespace MonsterRender {

// ============================================================================
// Compile-Time Configuration
// ============================================================================

/**
 * Minimum verbosity level that will be compiled into the code.
 * Logs with higher verbosity (more verbose) will be completely stripped.
 * 
 * Override by defining COMPILED_IN_MINIMUM_VERBOSITY before including this header.
 */
#ifndef COMPILED_IN_MINIMUM_VERBOSITY
    #if defined(NDEBUG) || defined(MR_SHIPPING)
        // Release/Shipping: Only compile Fatal, Error, Warning, Display, Log
        #define COMPILED_IN_MINIMUM_VERBOSITY ELogVerbosity::Log
    #else
        // Debug: Compile all verbosity levels
        #define COMPILED_IN_MINIMUM_VERBOSITY ELogVerbosity::VeryVerbose
    #endif
#endif

// ============================================================================
// Internal Logging Functions
// ============================================================================

namespace Private {

/**
 * Extract filename from full path (compile-time friendly)
 * @param Path - Full file path
 * @return Pointer to the filename portion
 */
inline const char* ExtractFilename(const char* Path) {
    const char* file = Path;
    while (*Path) {
        if (*Path == '/' || *Path == '\\') {
            file = Path + 1;
        }
        ++Path;
    }
    return file;
}

/**
 * Internal log function - formats and outputs the message.
 * Uses printf-style formatting.
 * 
 * @param Category - Log category
 * @param Verbosity - Log verbosity level
 * @param File - Source file name (__FILE__)
 * @param Line - Source line number (__LINE__)
 * @param Format - printf-style format string
 * @param ... - Format arguments
 */
inline void LogInternal(const FLogCategoryBase& Category, ELogVerbosity::Type Verbosity, 
                        const char* File, int Line, const char* Format, ...) {
    // Format the message
    char buffer[4096];
    va_list args;
    va_start(args, Format);
    vsnprintf(buffer, sizeof(buffer), Format, args);
    va_end(args);

    // Output to GLog with file and line info
    GLog->Serialize(buffer, Verbosity, Category.GetCategoryName(), ExtractFilename(File), Line);
}

/**
 * Fatal log function - logs and then crashes.
 * 
 * @param Category - Log category
 * @param File - Source file name (__FILE__)
 * @param Line - Source line number (__LINE__)
 * @param Format - printf-style format string
 * @param ... - Format arguments
 */
[[noreturn]] inline void FatalLogInternal(const FLogCategoryBase& Category, 
                                          const char* File, int Line, const char* Format, ...) {
    // Format the message
    char buffer[4096];
    va_list args;
    va_start(args, Format);
    vsnprintf(buffer, sizeof(buffer), Format, args);
    va_end(args);

    // Output to GLog with file and line info
    GLog->Serialize(buffer, ELogVerbosity::Fatal, Category.GetCategoryName(), ExtractFilename(File), Line);
    GLog->Flush();

    // Crash
    std::abort();
}

} // namespace Private

} // namespace MonsterRender

// ============================================================================
// Main Logging Macros
// ============================================================================

/**
 * Check if a log category is active at a given verbosity level.
 * 
 * @param CategoryName - Name of the log category
 * @param Verbosity - Verbosity level to check
 * @return true if logging is active at this level
 */
#define MR_LOG_ACTIVE(CategoryName, Verbosity) \
    (((MonsterRender::ELogVerbosity::Verbosity & MonsterRender::ELogVerbosity::VerbosityMask) <= COMPILED_IN_MINIMUM_VERBOSITY) && \
     ((MonsterRender::ELogVerbosity::Verbosity & MonsterRender::ELogVerbosity::VerbosityMask) <= CategoryName.GetCompileTimeVerbosity()) && \
     !CategoryName.IsSuppressed(MonsterRender::ELogVerbosity::Verbosity))

/**
 * Main logging macro with category support.
 * 
 * @param CategoryName - Name of the log category (must be declared with DECLARE_LOG_CATEGORY_EXTERN)
 * @param Verbosity - Verbosity level (Fatal, Error, Warning, Display, Log, Verbose, VeryVerbose)
 * @param Format - printf-style format string
 * @param ... - Format arguments
 * 
 * Example:
 *   MR_LOG(LogRenderer, Warning, "Texture %s not found", textureName);
 */
#define MR_LOG(CategoryName, Verbosity, Format, ...) \
    do { \
        /* Compile-time check: is this verbosity level compiled in? */ \
        if constexpr ((::MonsterRender::ELogVerbosity::Verbosity & ::MonsterRender::ELogVerbosity::VerbosityMask) <= COMPILED_IN_MINIMUM_VERBOSITY) \
        { \
            /* Compile-time check: is this verbosity level within category's compile-time limit? */ \
            if constexpr ((::MonsterRender::ELogVerbosity::Verbosity & ::MonsterRender::ELogVerbosity::VerbosityMask) <= decltype(CategoryName)::CompileTimeVerbosity) \
            { \
                /* Runtime check: is this verbosity level currently enabled? */ \
                if (!CategoryName.IsSuppressed(::MonsterRender::ELogVerbosity::Verbosity)) \
                { \
                    /* Handle Fatal specially */ \
                    if constexpr ((::MonsterRender::ELogVerbosity::Verbosity & ::MonsterRender::ELogVerbosity::VerbosityMask) == ::MonsterRender::ELogVerbosity::Fatal) \
                    { \
                        ::MonsterRender::Private::FatalLogInternal(CategoryName, __FILE__, __LINE__, Format, ##__VA_ARGS__); \
                    } \
                    else \
                    { \
                        ::MonsterRender::Private::LogInternal(CategoryName, ::MonsterRender::ELogVerbosity::Verbosity, __FILE__, __LINE__, Format, ##__VA_ARGS__); \
                    } \
                } \
            } \
        } \
    } while (0)

/**
 * Conditional logging macro.
 * Only evaluates the condition if the log level is active.
 * 
 * @param Condition - Condition to check
 * @param CategoryName - Name of the log category
 * @param Verbosity - Verbosity level
 * @param Format - printf-style format string
 * @param ... - Format arguments
 */
#define MR_CLOG(Condition, CategoryName, Verbosity, Format, ...) \
    do { \
        if constexpr ((::MonsterRender::ELogVerbosity::Verbosity & ::MonsterRender::ELogVerbosity::VerbosityMask) <= COMPILED_IN_MINIMUM_VERBOSITY) \
        { \
            if constexpr ((::MonsterRender::ELogVerbosity::Verbosity & ::MonsterRender::ELogVerbosity::VerbosityMask) <= decltype(CategoryName)::CompileTimeVerbosity) \
            { \
                if (!CategoryName.IsSuppressed(::MonsterRender::ELogVerbosity::Verbosity)) \
                { \
                    if (Condition) \
                    { \
                        ::MonsterRender::Private::LogInternal(CategoryName, ::MonsterRender::ELogVerbosity::Verbosity, __FILE__, __LINE__, Format, ##__VA_ARGS__); \
                    } \
                } \
            } \
        } \
    } while (0)

// ============================================================================
// Default Log Category
// ============================================================================

namespace MonsterRender {
// Declare the default log category (LogTemp equivalent)
DECLARE_LOG_CATEGORY_EXTERN(LogTemp, Log, All)
} // namespace MonsterRender

// Bring LogTemp into global namespace for convenience
using MonsterRender::LogTemp;

// ============================================================================
// Convenience Macros (Backward Compatibility)
// ============================================================================

// These macros use the LogTemp category for simple logging
// They maintain backward compatibility with the old MR_LOG_* macros
// Note: Message can be const char*, std::string, or string expression

// Helper to convert message to const char* (handles both const char* and std::string)
namespace MonsterRender {
namespace Private {
    inline const char* ToLogString(const char* str) { return str; }
    inline const char* ToLogString(const std::string& str) { return str.c_str(); }
} // namespace Private
} // namespace MonsterRender

#define MR_LOG_TRACE(Message)   MR_LOG(LogTemp, VeryVerbose, "%s", ::MonsterRender::Private::ToLogString(Message))
#define MR_LOG_DEBUG(Message)   MR_LOG(LogTemp, Verbose, "%s", ::MonsterRender::Private::ToLogString(Message))
#define MR_LOG_INFO(Message)    MR_LOG(LogTemp, Log, "%s", ::MonsterRender::Private::ToLogString(Message))
#define MR_LOG_WARNING(Message) MR_LOG(LogTemp, Warning, "%s", ::MonsterRender::Private::ToLogString(Message))
#define MR_LOG_ERROR(Message)   MR_LOG(LogTemp, Error, "%s", ::MonsterRender::Private::ToLogString(Message))
#define MR_LOG_FATAL(Message)   MR_LOG(LogTemp, Fatal, "%s", ::MonsterRender::Private::ToLogString(Message))
