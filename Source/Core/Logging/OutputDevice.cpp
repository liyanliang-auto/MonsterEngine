#include "Core/Logging/OutputDevice.h"
#include <cstdio>
#include <cstdarg>

namespace MonsterRender {

void FOutputDevice::Logf(ELogVerbosity::Type Verbosity, const char* Category, const char* Format, ...) {
    char buffer[4096];
    va_list args;
    va_start(args, Format);
    vsnprintf(buffer, sizeof(buffer), Format, args);
    va_end(args);

    Serialize(buffer, Verbosity, Category);
}

} // namespace MonsterRender
