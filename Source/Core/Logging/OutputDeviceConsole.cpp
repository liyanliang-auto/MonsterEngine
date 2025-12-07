#include "Core/Logging/OutputDeviceConsole.h"
#include "Core/Logging/LogVerbosity.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>

#if PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <Windows.h>
#endif

namespace MonsterRender {

FOutputDeviceConsole::FOutputDeviceConsole()
    : m_bColorEnabled(true)
    , m_bShown(true)
#if PLATFORM_WINDOWS
    , m_consoleHandle(nullptr)
#endif
{
#if PLATFORM_WINDOWS
    m_consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
#endif
}

FOutputDeviceConsole::~FOutputDeviceConsole() {
    TearDown();
}

void FOutputDeviceConsole::Serialize(const char* Message, ELogVerbosity::Type Verbosity, const char* Category) {
    Serialize(Message, Verbosity, Category, -1.0, nullptr, 0);
}

void FOutputDeviceConsole::Serialize(const char* Message, ELogVerbosity::Type Verbosity, const char* Category, double Time) {
    Serialize(Message, Verbosity, Category, Time, nullptr, 0);
}

void FOutputDeviceConsole::Serialize(const char* Message, ELogVerbosity::Type Verbosity, const char* Category, 
                                     const char* File, int32 Line) {
    Serialize(Message, Verbosity, Category, -1.0, File, Line);
}

void FOutputDeviceConsole::Serialize(const char* Message, ELogVerbosity::Type Verbosity, const char* Category,
                                     double Time, const char* File, int32 Line) {
    std::lock_guard<std::mutex> lock(m_consoleMutex);

    // Set color based on verbosity
    if (m_bColorEnabled) {
        SetColor(Verbosity);
    }

    // Format and output the log line
    String formattedLine = FormatLogLine(Message, Verbosity, Category, Time, File, Line);

    // Use stderr for errors and warnings, stdout for everything else
    if ((Verbosity & ELogVerbosity::VerbosityMask) <= ELogVerbosity::Warning) {
        std::cerr << formattedLine;
        std::cerr.flush();
    } else {
        std::cout << formattedLine;
    }

    // Reset color
    if (m_bColorEnabled) {
        ResetColor();
    }
}

void FOutputDeviceConsole::Flush() {
    std::lock_guard<std::mutex> lock(m_consoleMutex);
    std::cout.flush();
    std::cerr.flush();
}

void FOutputDeviceConsole::TearDown() {
    Flush();
}

void FOutputDeviceConsole::Show(bool bShow) {
#if PLATFORM_WINDOWS
    HWND consoleWindow = GetConsoleWindow();
    if (consoleWindow) {
        ShowWindow(consoleWindow, bShow ? SW_SHOW : SW_HIDE);
        m_bShown = bShow;
    }
#else
    // On non-Windows platforms, console visibility is typically managed by the terminal
    m_bShown = bShow;
#endif
}

void FOutputDeviceConsole::SetColor(ELogVerbosity::Type Verbosity) {
#if PLATFORM_WINDOWS
    if (!m_consoleHandle) {
        return;
    }

    WORD attributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE; // Default: white

    switch (Verbosity & ELogVerbosity::VerbosityMask) {
        case ELogVerbosity::Fatal:
            // Bright red on dark red background
            attributes = FOREGROUND_RED | FOREGROUND_INTENSITY | BACKGROUND_RED;
            break;
        case ELogVerbosity::Error:
            // Bright red
            attributes = FOREGROUND_RED | FOREGROUND_INTENSITY;
            break;
        case ELogVerbosity::Warning:
            // Yellow
            attributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
            break;
        case ELogVerbosity::Display:
            // White
            attributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
            break;
        case ELogVerbosity::Log:
            // Gray
            attributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
            break;
        case ELogVerbosity::Verbose:
            // Dark gray
            attributes = FOREGROUND_INTENSITY;
            break;
        case ELogVerbosity::VeryVerbose:
            // Dark gray
            attributes = FOREGROUND_INTENSITY;
            break;
        default:
            break;
    }

    SetConsoleTextAttribute(static_cast<HANDLE>(m_consoleHandle), attributes);
#else
    // ANSI color codes for Unix-like systems
    const char* colorCode = "\033[0m"; // Reset

    switch (Verbosity & ELogVerbosity::VerbosityMask) {
        case ELogVerbosity::Fatal:
            colorCode = "\033[1;41;37m"; // Bold white on red background
            break;
        case ELogVerbosity::Error:
            colorCode = "\033[1;31m"; // Bold red
            break;
        case ELogVerbosity::Warning:
            colorCode = "\033[1;33m"; // Bold yellow
            break;
        case ELogVerbosity::Display:
            colorCode = "\033[1;37m"; // Bold white
            break;
        case ELogVerbosity::Log:
            colorCode = "\033[0m"; // Default
            break;
        case ELogVerbosity::Verbose:
            colorCode = "\033[90m"; // Dark gray
            break;
        case ELogVerbosity::VeryVerbose:
            colorCode = "\033[90m"; // Dark gray
            break;
        default:
            break;
    }

    std::cout << colorCode;
#endif
}

void FOutputDeviceConsole::ResetColor() {
#if PLATFORM_WINDOWS
    if (m_consoleHandle) {
        SetConsoleTextAttribute(static_cast<HANDLE>(m_consoleHandle), 
                               FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    }
#else
    std::cout << "\033[0m";
#endif
}

String FOutputDeviceConsole::FormatLogLine(const char* Message, ELogVerbosity::Type Verbosity, const char* Category, 
                                           double Time, const char* File, int32 Line) {
    // Use stack-based buffer to avoid heap allocation issues
    char buffer[2048] = {};
    char* ptr = buffer;
    char* end = buffer + sizeof(buffer) - 1;

    // Timestamp with milliseconds: [YYYY/MM/DD HH:MM:SS:mmm]
    auto now = std::chrono::system_clock::now();
    auto time_t_val = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm_buf = {};
    char timeStr[32] = {};
#if PLATFORM_WINDOWS
    errno_t err = localtime_s(&tm_buf, &time_t_val);
    if (err == 0) {
        strftime(timeStr, sizeof(timeStr), "%Y/%m/%d %H:%M:%S", &tm_buf);
    }
#else
    if (localtime_r(&time_t_val, &tm_buf) != nullptr) {
        strftime(timeStr, sizeof(timeStr), "%Y/%m/%d %H:%M:%S", &tm_buf);
    }
#endif
    
    // Format: [timestamp:ms] file(line): [category][verbosity] message
    int written = snprintf(ptr, end - ptr, "[%s:%03d] ", timeStr, static_cast<int>(ms.count()));
    if (written > 0) ptr += written;

    // File and line: filename.cpp(123):
    if (File && Line > 0 && ptr < end) {
        written = snprintf(ptr, end - ptr, "%s(%d): ", File, Line);
        if (written > 0) ptr += written;
    }

    // Category and verbosity: [LogTemp][LOG ]
    if (ptr < end) {
        written = snprintf(ptr, end - ptr, "[%s][%s] ", Category ? Category : "Unknown", ToShortString(Verbosity));
        if (written > 0) ptr += written;
    }

    // Message
    if (ptr < end && Message) {
        written = snprintf(ptr, end - ptr, "%s", Message);
        if (written > 0) ptr += written;
    }

    // Line terminator
    if (GetAutoEmitLineTerminator() && ptr < end) {
        *ptr++ = '\n';
    }
    
    *ptr = '\0';
    return String(buffer);
}

} // namespace MonsterRender
