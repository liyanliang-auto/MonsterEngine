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
    void Flush() override {}
    void TearDown() override {}

    bool CanBeUsedOnAnyThread() const override { return true; }
    bool CanBeUsedOnMultipleThreads() const override { return true; }
    bool CanBeUsedOnPanicThread() const override { return true; }

private:
    /** Format a log line */
    String FormatLogLine(const char* Message, ELogVerbosity::Type Verbosity, const char* Category);
};

} // namespace MonsterRender
