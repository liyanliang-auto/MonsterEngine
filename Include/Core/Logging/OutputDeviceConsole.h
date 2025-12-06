#pragma once

/**
 * OutputDeviceConsole.h
 * 
 * Console output device - writes logs to stdout/stderr with color support.
 */

#include "Core/CoreTypes.h"
#include "Core/Logging/OutputDevice.h"
#include <mutex>

namespace MonsterRender {

/**
 * Console output device - writes to stdout with optional color coding.
 */
class FOutputDeviceConsole : public FOutputDevice {
public:
    FOutputDeviceConsole();
    ~FOutputDeviceConsole();

    // ========================================================================
    // FOutputDevice Interface
    // ========================================================================

    void Serialize(const char* Message, ELogVerbosity::Type Verbosity, const char* Category) override;
    void Serialize(const char* Message, ELogVerbosity::Type Verbosity, const char* Category, double Time) override;
    void Serialize(const char* Message, ELogVerbosity::Type Verbosity, const char* Category, 
                  const char* File, int32 Line) override;
    void Serialize(const char* Message, ELogVerbosity::Type Verbosity, const char* Category,
                  double Time, const char* File, int32 Line) override;
    void Flush() override;
    void TearDown() override;

    bool CanBeUsedOnAnyThread() const override { return true; }
    bool CanBeUsedOnMultipleThreads() const override { return true; }
    bool CanBeUsedOnPanicThread() const override { return true; }

    // ========================================================================
    // Console-Specific Methods
    // ========================================================================

    /** Enable/disable color output */
    void SetColorEnabled(bool bEnable) { m_bColorEnabled = bEnable; }
    bool IsColorEnabled() const { return m_bColorEnabled; }

    /** Show/hide the console window (Windows only) */
    void Show(bool bShow);

    /** Check if console is visible */
    bool IsShown() const { return m_bShown; }

private:
    /** Set console text color based on verbosity */
    void SetColor(ELogVerbosity::Type Verbosity);

    /** Reset console text color to default */
    void ResetColor();

    /** Format a log line with file and line information */
    String FormatLogLine(const char* Message, ELogVerbosity::Type Verbosity, const char* Category, 
                        double Time, const char* File = nullptr, int32 Line = 0);

    bool m_bColorEnabled;
    bool m_bShown;
    std::mutex m_consoleMutex;

#if PLATFORM_WINDOWS
    void* m_consoleHandle;  // HANDLE
#endif
};

} // namespace MonsterRender
