#pragma once
#include <cstdint>
#include <string>
namespace MonsterRender::RHI {
/** Opaque successful queue submission ID. Zero is invalid; IDs are device-local. */
struct FGPUCompletionToken {
    /** Actual submission sequence, never a frame index. */
    uint64_t value = 0;
};
/** Hardware timestamp interval, delivered asynchronously after GPU completion. */
struct FRHIGPUTimestampSample {
    /** Caller-supplied frame ID of the recorded interval. */
    uint64_t frame = 0;
    /** Caller-supplied stage label. */
    std::string label;
    /** GPU elapsed time; never substituted with CPU time. */
    double milliseconds = 0;
};
/** Validate 12-byte indirect dispatch arguments without overflow. */
bool IsValidDispatchIndirectRange(uint64_t bufferBytes, uint64_t byteOffset);
}
