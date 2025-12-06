#include "Core/Logging/OutputDeviceDebug.h"
#include "Core/Logging/LogVerbosity.h"
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

void FOutputDeviceDebug::Serialize(const char* Message, ELogVerbosity::Type Verbosity, const char* Category) {
    Serialize(Message, Verbosity, Category, -1.0, nullptr, 0);
}

void FOutputDeviceDebug::Serialize(const char* Message, ELogVerbosity::Type Verbosity, const char* Category, double Time) {
    Serialize(Message, Verbosity, Category, Time, nullptr, 0);
}

void FOutputDeviceDebug::Serialize(const char* Message, ELogVerbosity::Type Verbosity, const char* Category, 
                                   const char* File, int32 Line) {
    Serialize(Message, Verbosity, Category, -1.0, File, Line);
}

void FOutputDeviceDebug::Serialize(const char* Message, ELogVerbosity::Type Verbosity, const char* Category,
                                   double Time, const char* File, int32 Line) {
    String formattedLine = FormatLogLine(Message, Verbosity, Category, File, Line);

#if PLATFORM_WINDOWS
    // Output to Visual Studio Output window
    OutputDebugStringA(formattedLine.c_str());
#else
    // On non-Windows platforms, we could use syslog or just skip
    // For now, we do nothing as console output is handled by FOutputDeviceConsole
#endif
}

String FOutputDeviceDebug::FormatLogLine(const char* Message, ELogVerbosity::Type Verbosity, const char* Category,
                                         const char* File, int32 Line) {
    std::ostringstream oss;

    // Timestamp with milliseconds: [YYYY/MM/DD HH:MM:SS:mmm]
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm_buf;
#if PLATFORM_WINDOWS
    localtime_s(&tm_buf, &time_t);
#else
    localtime_r(&time_t, &tm_buf);
#endif

    oss << "[" << std::put_time(&tm_buf, "%Y/%m/%d %H:%M:%S");
    oss << ":" << std::setfill('0') << std::setw(3) << ms.count() << "] ";

    // File and line: filename.cpp(123):
    if (File && Line > 0) {
        oss << File << "(" << Line << "): ";
    }

    // Category and verbosity: [LogTemp][LOG ]
    oss << "[" << Category << "]";
    oss << "[" << ToShortString(Verbosity) << "] ";

    // Message
    oss << Message;

    if (GetAutoEmitLineTerminator()) {
        oss << "\n";
    }

    return oss.str();
}

} // namespace MonsterRender
