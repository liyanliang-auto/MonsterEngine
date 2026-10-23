// Copyright Monster Engine. All Rights Reserved.
#pragma once
#include "Renderer/Splat/Streaming/SplatStreamingTypes.h"
#include <functional>
#include <memory>
#include <array>
#include <mutex>
#include <optional>
#include <atomic>

namespace MonsterRender::Splat {
/** Privacy-safe lifecycle stages, independent of profiler string exports. */
enum class ESplatDetailStage : uint8_t { Demand, Prefetch, Queued, CacheHit, ReadStarted, ReadComplete,
    CPUReady, UploadRecorded, UploadSubmitted, GPUComplete, ActiveReady, FirstUse,
    Cancelled, Failed, Evicted, AlreadyResident, UpstreamDemand, UpstreamRetired, WorkCoalesced, Validated, ReadyTransferred, WorkRejected };
/** Original causal identity is retained even when this row is drained on a later frame. */
struct FSplatDetailEvent {
    uint64_t sequence=0, timestampNanoseconds=0; ///< Log sequence and native monotonic clock.
    ESplatDetailStage stage=ESplatDetailStage::Demand; ///< Stable allowlisted stage.
    MonsterEngine::Profiling::Context context; ///< Actual event request/frame/page/submission.
    uint64_t demandFrameId=0, demandRequestId=0, pageLifetime=0; ///< Original demand and physical slot generation.
    uint64_t selectionVersion=0, residencyVersion=0; ///< Actual mapping versions when known.
    uint64_t demandSequence=0; ///< Identified re-selection lifetime; zero when only an upstream origin is known.
    FSplatUpstreamOrigin upstream; ///< Exact immutable upstream lifetime (not pool selection).
    uint64_t workId=0, workOriginId=0; ///< Accepted physical work and original upstream cause, separate namespaces.
    std::optional<uint64_t> surfaceGeneration,configGeneration,pipelineGeneration; ///< Event origin, never export-frame aliases.
    std::optional<uint64_t> demandSurfaceGeneration,demandConfigGeneration,demandPipelineGeneration; ///< Separate original demand identity.
};
/** Shared bounded worker/owner diagnostic queue. No strings, heap allocation or GPU queries in Emit.
 * Capture is cheap while profiling is disabled; retained rows can still be drained afterward. */
class FSplatDetailDiagnostics {
public:
    static constexpr size_t Capacity=256; ///< Fixed pending event bound, distinct from exported frame ring.
    explicit FSplatDetailDiagnostics(size_t capacity=Capacity);
    size_t PendingCapacity() const noexcept { return EventCapacity; }
    uint64_t AllocatedBytes() const noexcept { return sizeof(*this)+EventCapacity*sizeof(FSplatDetailEvent); }
    void Emit(ESplatDetailStage, const MonsterEngine::Profiling::Context&, uint64_t lifetime=0,
        const MonsterEngine::Profiling::Context* demand=nullptr, uint64_t selection=0, uint64_t residency=0, uint64_t demandSequence=0,
        const FSplatPageCausality* work=nullptr, const FSplatUpstreamOrigin* upstream=nullptr) noexcept;
    void Drain(std::vector<FSplatDetailEvent>& output, size_t maximum, uint64_t& overwritten, uint64_t& dropped);
    /** Bounded origin registry. Evicted or ambiguous origins export null rather than a false join. */
    void BindFrame(const MonsterEngine::Profiling::Context&,uint64_t surface,uint64_t config,uint64_t pipeline);
private:
    struct FOrigin { MonsterEngine::Profiling::Context context; uint64_t surface=0,config=0,pipeline=0; }; ///< Registry entry.
    std::mutex Mutex; ///< Serializes worker and owner operations; never held across I/O.
    const size_t EventCapacity; ///< Scene-sized fixed bound, at most Capacity; immutable before workers start.
    std::unique_ptr<FSplatDetailEvent[]> Events; ///< Exact allocated row count; overwritten events are counted.
    size_t Begin=0, Count=0; ///< Pending ring range.
    uint64_t Sequence=0, Overwritten=0; ///< Lifetime counters; never reset at frame boundaries.
    std::atomic<uint64_t> Dropped{0}; ///< Emit failures cannot interrupt rendering or race worker counters.
    std::array<FOrigin,64> Origins{}; ///< Bounded lookup; late workers may explicitly lose generation association.
    size_t NextOrigin=0; ///< Monotonic replacement cursor within the origin registry.
};
/** Worker-side fault decision; no arbitrary blocking reader is installed. */
struct FSplatPageReadFault {
    uint32_t delayMilliseconds = 0; ///< Cancellation-aware latency before read.
    bool failRead = false; ///< Produce a retryable read error.
    bool corruptChecksum = false; ///< Flip an encoded byte after reading.
    uint64_t truncateBytes = 0; ///< Remove bytes to exercise truncated-range errors.
};
/** Breakdown of all retained page payloads, including transferred ownership. */
struct FSplatPageStreamerMemoryStats {
    uint64_t totalReservedBytes = 0; ///< Hard-budget charge including metadata.
    uint64_t encodedCacheBytes = 0; ///< Retained verified encoded cache.
    uint64_t inFlightReservedBytes = 0; ///< Worker encode/decode reservations.
    uint64_t readyBytes = 0; ///< Completed payloads waiting for DrainReady.
    uint64_t transferredBytes = 0; ///< Payload reservations until explicit release.
    uint64_t metadataBytes = 0; ///< Bounded task/lease/terminal-failure history charge.
    uint64_t peakReservedBytes = 0; ///< High-water total hard-budget charge.
    uint32_t transferredPages = 0; ///< Number of outstanding consumer leases.
    uint32_t terminalFailurePages = 0; ///< Released failures deduplicated in this generation.
};
/** Bounded asynchronous local-disk page scheduler. Public calls are thread safe;
 * Open/Shutdown must be serialized by the scene owner. Generation IDs increase
 * monotonically. CancelGeneration invalidates that generation and all older work.
 */
class FSplatPageStreamer {
public:
    FSplatPageStreamer();
    ~FSplatPageStreamer();
    FSplatPageStreamer(const FSplatPageStreamer&) = delete;
    FSplatPageStreamer& operator=(const FSplatPageStreamer&) = delete;
    /** Conservative progress workspace: queued metadata, each worker's encoded/
     * decoded page, one consumer lease and one old encoded cache per worker.
     * Runtime still charges every actual retained payload against cpuCacheBytes. */
    static bool EstimateMinimumProgressBytes(const FSplatSceneManifest&,
        const FSplatStreamingConfig&, uint64_t& bytes, std::string& error);
    /** Validates metadata and starts workers; never reads page payloads.
     * A reopened scene must use generations newer than the previous scene. */
    bool Open(const FSplatSceneManifest& manifest, const std::string& directory,
              const FSplatStreamingConfig& config, std::string& error);
    /** Deduplicates page/generation pairs; greater priorities execute first.
     * Newly accepted work retains profiling context; deduplication does not replace its cause.
     * Saturated metadata requests may be rejected with observable diagnostics.
     * Terminal failures are deduplicated until their generation is cancelled. */
    void Request(const std::vector<FSplatPageRequest>& requests);
    /** Replace current wanted work and priorities. Cancels obsolete queued/ready
     * results; in-progress reads retire their charge when their worker finishes.
     * Consumer transfers remain charged until ReleaseTransferred. */
    void UpdateRequests(const std::vector<FSplatPageRequest>& requests);
    /** Moves completed results out; their reservations remain charged until release. */
    std::vector<FSplatPageResult> DrainReady();
    /** Invalidates older work; transferred results remain charged until released. */
    void CancelGeneration(uint64_t generation);
    FSplatStreamingStats GetStats() const;
    FSplatPageStreamerMemoryStats GetMemoryStats() const;
    std::string GetLastError() const;
    /** Call only after consumer destroys/frees encoded and decoded result vectors.
     * Safe for an old generation; duplicate/unknown release returns false. */
    bool ReleaseTransferred(uint32_t pageId, uint64_t generation);
    /** Drop idle encoded cache entries; workers/transferred payloads stay charged.
     * Stops producing cache copies until the next Open, without cancelling required reads. */
    void OnMemoryPressure();
    /** Stops/cancels workers, joins them and releases owned cache/results. Outstanding
     * transferred reservations survive Shutdown. Release them before reopening. */
    void Shutdown();
    /** Fault decisions run on workers and must return promptly. The delay itself is
     * handled internally and wakes immediately on cancellation/shutdown. */
    void SetFaultInjector(std::function<FSplatPageReadFault(uint32_t pageId, uint32_t attempt)> injector);
    /** Install before Open starts workers; null disables lifecycle collection. */
    void SetDetailDiagnostics(std::shared_ptr<FSplatDetailDiagnostics>);
private:
    struct FImpl;
    std::unique_ptr<FImpl> m_impl;
};
} // namespace MonsterRender::Splat
