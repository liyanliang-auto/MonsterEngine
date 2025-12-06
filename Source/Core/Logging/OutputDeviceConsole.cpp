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
    Serialize(Message, Verbosity, Category, -1.0);
}

void FOutputDeviceConsole::Serialize(const char* Message, ELogVerbosity::Type Verbosity, const char* Category, double Time) {
    std::lock_guard<std::mutex> lock(m_consoleMutex);

    // Set color based on verbosity
    if (m_bColorEnabled) {
        SetColor(Verbosity);
    }

    // Format and output the log line
    String formattedLine = FormatLogLine(Message, Verbosity, Category, Time);

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

String FOutputDeviceConsole::FormatLogLine(const char* Message, ELogVerbosity::Type Verbosity, const char* Category, double Time) {
    std::ostringstream oss;

    // Timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);

    std::tm tm_buf;
#if PLATFORM_WINDOWS
    localtime_s(&tm_buf, &time_t);
#else
    localtime_r(&time_t, &tm_buf);
#endif

    oss << "[" << std::put_time(&tm_buf, "%H:%M:%S") << "]";

    // Category and verbosity
    oss << "[" << Category << "]";
    oss << "[" << ToShortString(Verbosity) << "] ";

    // Message
    oss << Message;

    // Line terminator
    if (GetAutoEmitLineTerminator()) {
        oss << "\n";
    }

    return oss.str();
}

} // namespace MonsterRender
