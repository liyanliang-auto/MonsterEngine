// Copyright Monster Engine. All Rights Reserved.
#pragma once
#include "Renderer/Splat/Streaming/SplatStreamingTypes.h"
#include <functional>
#include <memory>

namespace MonsterRender::Splat {
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
private:
    struct FImpl;
    std::unique_ptr<FImpl> m_impl;
};
} // namespace MonsterRender::Splat
