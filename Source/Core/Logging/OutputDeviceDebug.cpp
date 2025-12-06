#include "Core/Logging/OutputDeviceDebug.h"
#include "Core/Logging/LogVerbosity.h"
#include <sstream>

#if PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <Windows.h>
#endif

namespace MonsterRender {

void FOutputDeviceDebug::Serialize(const char* Message, ELogVerbosity::Type Verbosity, const char* Category) {
    String formattedLine = FormatLogLine(Message, Verbosity, Category);

#if PLATFORM_WINDOWS
    // Output to Visual Studio Output window
    OutputDebugStringA(formattedLine.c_str());
#else
    // On non-Windows platforms, we could use syslog or just skip
    // For now, we do nothing as console output is handled by FOutputDeviceConsole
#endif
}

void FOutputDeviceDebug::Serialize(const char* Message, ELogVerbosity::Type Verbosity, const char* Category, double Time) {
    // Time is ignored for debug output - just call the simpler version
    Serialize(Message, Verbosity, Category);
}

String FOutputDeviceDebug::FormatLogLine(const char* Message, ELogVerbosity::Type Verbosity, const char* Category) {
    std::ostringstream oss;

    // Format: [Category][Verbosity] Message\n
    oss << "[" << Category << "]";
    oss << "[" << ToShortString(Verbosity) << "] ";
    oss << Message;

    if (GetAutoEmitLineTerminator()) {
        oss << "\n";
    }

    return oss.str();
}

} // namespace MonsterRender
