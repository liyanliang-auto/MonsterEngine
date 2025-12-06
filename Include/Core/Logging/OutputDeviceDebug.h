#pragma once

/**
 * OutputDeviceDebug.h
 * 
 * Debug output device - writes logs to the debugger output window.
 * On Windows, uses OutputDebugString.
 */

#include "Core/CoreTypes.h"
#include "Core/Logging/OutputDevice.h"

namespace MonsterRender {

/**
 * Debug output device - writes to debugger output (e.g., Visual Studio Output window).
 */
class FOutputDeviceDebug : public FOutputDevice {
public:
    FOutputDeviceDebug() = default;
    ~FOutputDeviceDebug() = default;

    // ========================================================================
    // FOutputDevice Interface
    // ========================================================================

    void Serialize(const char* Message, ELogVerbosity::Type Verbosity, const char* Category) override;
    void Serialize(const char* Message, ELogVerbosity::Type Verbosity, const char* Category, double Time) override;
    void Serialize(const char* Message, ELogVerbosity::Type Verbosity, const char* Category, 
                  const char* File, int32 Line) override;
    void Serialize(const char* Message, ELogVerbosity::Type Verbosity, const char* Category,
                  double Time, const char* File, int32 Line) override;
    void Flush() override {}
    void TearDown() override {}

    bool CanBeUsedOnAnyThread() const override { return true; }
    bool CanBeUsedOnMultipleThreads() const override { return true; }
    bool CanBeUsedOnPanicThread() const override { return true; }

private:
    /** Format a log line with file and line information */
    String FormatLogLine(const char* Message, ELogVerbosity::Type Verbosity, const char* Category,
                        const char* File = nullptr, int32 Line = 0);
};

} // namespace MonsterRender
