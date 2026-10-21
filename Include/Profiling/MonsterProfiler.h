// Copyright Monster Engine. All Rights Reserved.
#pragma once

#include <cstdint>
#include <string>

namespace MonsterEngine::Profiling {

/** Explicit causal identities. Zero always means unavailable, never inferred. */
struct Context {
    uint64_t requestId = 0; ///< Incoming request identity.
    uint64_t frameId = 0; ///< Reserved native frame identity.
    uint64_t submissionId = 0; ///< Real successful submission token only.
    uint64_t sceneId = 0; ///< Scene/generation identity, if known.
    uint64_t pageId = 0; ///< Page identity, if known; encode zero-based page as index + 1.
};

/** Enable bounded recording and optional Android ATrace mirroring; disabled at startup. */
void SetEnabled(bool enabled) noexcept;
/** A single atomic load; use before constructing expensive diagnostic arguments. */
bool IsEnabled() noexcept;
/** CPU monotonic nanoseconds. Android uses CLOCK_MONOTONIC, not GPU/display time. */
uint64_t NowNs() noexcept;
/** Encode signed statuses (including failed VkResult) losslessly as two's-complement uint64. */
constexpr uint64_t EncodeSignedValue(int64_t value) noexcept { return static_cast<uint64_t>(value); }
/** Record a bounded instant event. Names are copied, truncated to 95 ASCII bytes. */
void Event(const char* name, uint64_t requestId = 0, uint64_t frameId = 0,
    uint64_t submissionId = 0, uint64_t value = 0) noexcept;
/** Record explicit page/scene identities without inventing worker-to-frame joins. */
void EventWithContext(const char* name, const Context& context, uint64_t value = 0) noexcept;
/** Snapshot schema 1 JSON; records copy under lock, serialization happens outside it.
 * Throws on allocation failure. Export timings are totals through the previous export. */
std::string ExportJson();
/** Clear stored rows/counters under lock, preserving globally increasing sequences.
 * Call at a quiescent capture boundary; disabling alone retains available evidence. */
void Clear() noexcept;

/** Same-thread bounded CPU begin/end event pair; a disabled scope remains inactive. */
class Scope {
public:
    Scope(const char* name, uint64_t requestId = 0, uint64_t frameId = 0,
        uint64_t submissionId = 0, uint64_t value = 0) noexcept;
    Scope(const char* name, const Context& context, uint64_t value = 0) noexcept;
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    Scope(Scope&&) = delete;
    Scope& operator=(Scope&&) = delete;
    /** Set end-row result, e.g. EncodeSignedValue(VkResult), without changing identity. */
    void SetValue(uint64_t value) noexcept { Value = value; }
private:
    char Name[96]{}; ///< Owned bounded event name.
    Context Identity{}; ///< Causal identities shared by begin/end.
    uint64_t Value = 0; ///< Final encoded result/counter.
    uint64_t CaptureEpoch = 0; ///< Avoid joining across mode/reset boundaries.
    bool Active = false; ///< True only when constructed while enabled.
    bool ATraceActive = false; ///< Matched begin/end even if capture is disabled mid-scope.
};

} // namespace MonsterEngine::Profiling
