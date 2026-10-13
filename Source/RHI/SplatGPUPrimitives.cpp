#include "RHI/SplatGPUPrimitives.h"
namespace MonsterRender::RHI {
bool IsValidDispatchIndirectRange(uint64_t bufferBytes, uint64_t byteOffset) {
    return (byteOffset & 3u) == 0 && byteOffset <= bufferBytes && bufferBytes - byteOffset >= 12;
}
}
