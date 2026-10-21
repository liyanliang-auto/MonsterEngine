// Copyright Monster Engine. All Rights Reserved.
#include "Profiling/MonsterProfiler.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <vector>
#if defined(__ANDROID__)
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#if defined(MONSTER_PROFILING_ATRACE)
#include <android/trace.h>
#endif
#elif defined(_WIN32)
#include <windows.h>
#else
#include <functional>
#include <thread>
#endif

namespace MonsterEngine::Profiling {
namespace {
constexpr size_t Capacity = 16384;
struct Row {
    uint64_t eventSequence = 0, timestampNs = 0, threadId = 0, epoch = 0;
    Context context{};
    uint64_t value = 0;
    char name[96]{};
    char phase = 'I';
};
std::atomic<bool> Enabled{false};
std::atomic<uint64_t> Epoch{1}, Sequence{0}, Dropped{0}, Overwritten{0};
std::atomic<uint64_t> RecordCalls{0}, RecordDurationNs{0}, ExportCalls{0}, ExportDurationNs{0};
std::mutex RingMutex;
std::array<Row, Capacity> Ring{};
size_t Head = 0, Count = 0;

void CopyName(char* output, const char* input) noexcept {
    size_t i = 0;
    if (input) {
        for (; i < 95 && input[i]; ++i) {
            const auto c = static_cast<unsigned char>(input[i]);
            output[i] = c >= 32 && c <= 126 ? static_cast<char>(c) : '_';
        }
    }
    output[i] = 0;
}
uint64_t ThreadId() noexcept {
#if defined(__ANDROID__)
    return static_cast<uint64_t>(syscall(SYS_gettid));
#elif defined(_WIN32)
    return static_cast<uint64_t>(GetCurrentThreadId());
#else
    return static_cast<uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
}
bool BeginATrace(const char* name) noexcept {
#if defined(__ANDROID__) && defined(MONSTER_PROFILING_ATRACE)
    if (ATrace_isEnabled()) {
        char label[100]{};
        std::snprintf(label, sizeof(label), "ME/%s", name);
        ATrace_beginSection(label);
        return true;
    }
#else
    (void)name;
#endif
    return false;
}
void EndATrace(bool active) noexcept {
#if defined(__ANDROID__) && defined(MONSTER_PROFILING_ATRACE)
    if (active) ATrace_endSection();
#else
    (void)active;
#endif
}
void Record(const char* name, char phase, const Context& context, uint64_t value,
    uint64_t epoch) noexcept {
    const uint64_t start = NowNs();
    Row row{};
    row.timestampNs = start;
    row.eventSequence = Sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    row.threadId = ThreadId();
    row.epoch = epoch;
    row.context = context;
    row.value = value;
    row.phase = phase;
    CopyName(row.name, name);
    {
        std::unique_lock<std::mutex> lock(RingMutex, std::try_to_lock);
        if (!lock.owns_lock()) {
            Dropped.fetch_add(1, std::memory_order_relaxed);
        } else if (Enabled.load(std::memory_order_relaxed) && Epoch.load(std::memory_order_relaxed) == epoch) {
            Ring[Head] = row;
            Head = (Head + 1) % Capacity;
            if (Count < Capacity) ++Count;
            else Overwritten.fetch_add(1, std::memory_order_relaxed);
        } else {
            Dropped.fetch_add(1, std::memory_order_relaxed);
        }
    }
    RecordCalls.fetch_add(1, std::memory_order_relaxed);
    RecordDurationNs.fetch_add(NowNs() - start, std::memory_order_relaxed);
}
void AppendString(std::string& out, const char* value) {
    out += '"';
    for (; *value; ++value) {
        if (*value == '"' || *value == '\\') out += '\\';
        out += *value;
    }
    out += '"';
}
} // namespace

uint64_t NowNs() noexcept {
#if defined(__ANDROID__)
    timespec ts{};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<uint64_t>(ts.tv_nsec);
#else
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}
bool IsEnabled() noexcept { return Enabled.load(std::memory_order_relaxed); }
void SetEnabled(bool enabled) noexcept {
    std::lock_guard<std::mutex> lock(RingMutex);
    if (Enabled.load(std::memory_order_relaxed) != enabled) {
        Epoch.fetch_add(1, std::memory_order_relaxed);
        Enabled.store(enabled, std::memory_order_release);
    }
}
void Event(const char* name, uint64_t requestId, uint64_t frameId, uint64_t submissionId,
    uint64_t value) noexcept {
    if (!IsEnabled()) return;
    EventWithContext(name, Context{requestId, frameId, submissionId, 0, 0}, value);
}
void EventWithContext(const char* name, const Context& context, uint64_t value) noexcept {
    if (!IsEnabled()) return;
    const uint64_t epoch = Epoch.load(std::memory_order_relaxed);
    Record(name, 'I', context, value, epoch);
    // ATrace instant API is not available at API 26. A same-thread zero-work slice
    // mirrors event time; raw rows retain the full identities and encoded value.
    char boundedName[96]{};
    const uint64_t start = NowNs();
    CopyName(boundedName, name);
    EndATrace(BeginATrace(boundedName));
    RecordDurationNs.fetch_add(NowNs() - start, std::memory_order_relaxed);
}
Scope::Scope(const char* name, uint64_t requestId, uint64_t frameId,
    uint64_t submissionId, uint64_t value) noexcept
    : Scope(name, Context{requestId, frameId, submissionId, 0, 0}, value) {}
Scope::Scope(const char* name, const Context& context, uint64_t value) noexcept {
    if (!IsEnabled()) return;
    Active = true;
    Identity = context;
    Value = value;
    CaptureEpoch = Epoch.load(std::memory_order_relaxed);
    CopyName(Name, name);
    Record(Name, 'B', Identity, Value, CaptureEpoch);
    const uint64_t start = NowNs();
    ATraceActive = BeginATrace(Name);
    RecordDurationNs.fetch_add(NowNs() - start, std::memory_order_relaxed);
}
Scope::~Scope() {
    if (!Active) return;
    const uint64_t start = NowNs();
    EndATrace(ATraceActive);
    RecordDurationNs.fetch_add(NowNs() - start, std::memory_order_relaxed);
    Record(Name, 'E', Identity, Value, CaptureEpoch);
}
void Clear() noexcept {
    std::lock_guard<std::mutex> lock(RingMutex);
    Epoch.fetch_add(1, std::memory_order_relaxed);
    Head = Count = 0;
    Dropped = 0; Overwritten = 0; RecordCalls = 0; RecordDurationNs = 0;
    ExportCalls = 0; ExportDurationNs = 0;
}
std::string ExportJson() {
    const uint64_t start = NowNs();
    std::vector<Row> rows;
    rows.reserve(Capacity);
    uint64_t dropped = 0, overwritten = 0, epoch = 0;
    bool enabled = false;
    {
        std::lock_guard<std::mutex> lock(RingMutex);
        const size_t first = (Head + Capacity - Count) % Capacity;
        for (size_t i = 0; i < Count; ++i) rows.push_back(Ring[(first + i) % Capacity]);
        dropped = Dropped.load(); overwritten = Overwritten.load(); epoch = Epoch.load();
        enabled = Enabled.load();
    }
#if defined(__ANDROID__)
    constexpr const char* clock = "CLOCK_MONOTONIC";
#else
    constexpr const char* clock = "std::chrono::steady_clock";
#endif
    std::string out = "{\"schemaVersion\":1,\"clockDomain\":";
    AppendString(out, clock);
    out += ",\"enabled\":" + std::string(enabled ? "true" : "false");
    out += ",\"captureEpoch\":" + std::to_string(epoch);
    out += ",\"ringCapacity\":" + std::to_string(Capacity);
    out += ",\"droppedEvents\":" + std::to_string(dropped);
    out += ",\"overwrittenEvents\":" + std::to_string(overwritten);
    out += ",\"recordCalls\":" + std::to_string(RecordCalls.load());
    out += ",\"recordDurationNs\":" + std::to_string(RecordDurationNs.load());
    out += ",\"exportCallsBeforeThis\":" + std::to_string(ExportCalls.load());
    out += ",\"exportDurationNsBeforeThis\":" + std::to_string(ExportDurationNs.load());
    out += ",\"enabledBackends\": [";
    if (enabled) {
        out += "\"bounded-ring\"";
#if defined(__ANDROID__) && defined(MONSTER_PROFILING_ATRACE)
        if (ATrace_isEnabled()) out += ",\"android-atrace\"";
#endif
    }
    out += "],\"tracyIntegration\":\"offline-adapter\",\"events\":[";
    bool first = true;
    for (const auto& row : rows) {
        if (!first) out += ',';
        first = false;
        out += "{\"eventSequence\":" + std::to_string(row.eventSequence);
        out += ",\"timestampNs\":" + std::to_string(row.timestampNs);
        out += ",\"clockDomain\":"; AppendString(out, clock);
        out += ",\"threadId\":" + std::to_string(row.threadId);
        out += ",\"captureEpoch\":" + std::to_string(row.epoch);
        out += ",\"name\":"; AppendString(out, row.name);
        out += ",\"phase\":\""; out += row.phase; out += '"';
        out += ",\"requestId\":" + std::to_string(row.context.requestId);
        out += ",\"frameId\":" + std::to_string(row.context.frameId);
        out += ",\"submissionId\":" + std::to_string(row.context.submissionId);
        out += ",\"sceneId\":" + std::to_string(row.context.sceneId);
        out += ",\"pageId\":" + std::to_string(row.context.pageId);
        out += ",\"value\":" + std::to_string(row.value) + '}';
    }
    out += "],\"exportDurationNsThisSnapshot\":" + std::to_string(NowNs() - start) + '}';
    ExportCalls.fetch_add(1, std::memory_order_relaxed);
    ExportDurationNs.fetch_add(NowNs() - start, std::memory_order_relaxed);
    return out;
}
} // namespace MonsterEngine::Profiling
