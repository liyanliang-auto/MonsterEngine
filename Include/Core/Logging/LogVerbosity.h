#pragma once

/**
 * LogVerbosity.h
 * 
 * Defines log verbosity levels following UE5 architecture.
 * Verbosity levels control which log messages are displayed or compiled.
 */

#include "Core/CoreTypes.h"

namespace MonsterRender {

/**
 * Enum that defines the verbosity levels of the logging system.
 * Lower values are more severe/important.
 */
namespace ELogVerbosity {
    enum Type : uint8 {
        /** Not used - logging is disabled */
        NoLogging = 0,

        /** Always prints a fatal error and crashes (even if logging is disabled) */
        Fatal,

        /** Prints an error to console and log file */
        Error,

        /** Prints a warning to console and log file */
        Warning,

        /** Prints a message to console and log file */
        Display,

        /** Prints a message to log file only (not to console) */
        Log,

        /** Verbose logging - detailed information for debugging */
        Verbose,

        /** Very verbose logging - extremely detailed, may spam output */
        VeryVerbose,

        // Special values and masks
        All = VeryVerbose,
        NumVerbosity,
        VerbosityMask = 0x0F,
        SetColor = 0x40,      // Not a verbosity, used to set output color
        BreakOnLog = 0x80     // Break into debugger on this log
    };
}

static_assert(ELogVerbosity::NumVerbosity - 1 < ELogVerbosity::VerbosityMask, "Bad verbosity mask.");
static_assert(!(ELogVerbosity::VerbosityMask & ELogVerbosity::BreakOnLog), "Bad verbosity mask.");

/**
 * Converts verbosity to a string representation
 * @param Verbosity - verbosity enum value
 * @return String representation of the verbosity level
 */
inline const char* ToString(ELogVerbosity::Type Verbosity) {
    switch (Verbosity & ELogVerbosity::VerbosityMask) {
        case ELogVerbosity::NoLogging:   return "NoLogging";
        case ELogVerbosity::Fatal:       return "Fatal";
        case ELogVerbosity::Error:       return "Error";
        case ELogVerbosity::Warning:     return "Warning";
        case ELogVerbosity::Display:     return "Display";
        case ELogVerbosity::Log:         return "Log";
        case ELogVerbosity::Verbose:     return "Verbose";
        case ELogVerbosity::VeryVerbose: return "VeryVerbose";
        default:                         return "Unknown";
    }
}

/**
 * Gets the short display string for verbosity (for log output)
 */
inline const char* ToShortString(ELogVerbosity::Type Verbosity) {
    switch (Verbosity & ELogVerbosity::VerbosityMask) {
        case ELogVerbosity::Fatal:       return "FATAL";
        case ELogVerbosity::Error:       return "ERROR";
        case ELogVerbosity::Warning:     return "WARN ";
        case ELogVerbosity::Display:     return "DISP ";
        case ELogVerbosity::Log:         return "LOG  ";
        case ELogVerbosity::Verbose:     return "VERB ";
        case ELogVerbosity::VeryVerbose: return "VVERB";
        default:                         return "???? ";
    }
}

} // namespace MonsterRender
