// Copyright Monster Engine. All Rights Reserved.
#pragma once
#include "Renderer/Splat/LOD/SplatLODTypes.h"
#include "Profiling/MonsterProfiler.h"
namespace MonsterRender::Splat {
/** Independent hard limits; logical residency is not allocator capacity. */
struct FSplatStreamingConfig {
    uint64_t sceneGPUBytes = 512ull << 20; ///< Source pool capacity in bytes.
    uint64_t cpuCacheBytes = 256ull << 20; ///< Encoded plus decoded cache cap.
    uint64_t stagingBytes = 32ull << 20; ///< In-flight staging cap.
    uint64_t temporaryGPUBytes = 512ull << 20; ///< Preprocess/sort/render temporary cap.
    uint64_t maxUploadBytesPerFrame = 8ull << 20; ///< Upload bandwidth per frame.
    uint32_t maxConcurrentReads = 2; ///< Worker and read concurrency cap.
    uint32_t retries = 2; ///< Additional reads after a failure.
    uint32_t retryDelayMilliseconds = 25; ///< Retry backoff base.
};
/** Immutable controller origin; zero ids/generations mean unavailable, never export-frame aliases. */
struct FSplatUpstreamOrigin {
    uint64_t id=0; ///< Scene-scoped demand lifetime; not a pool demandSequence or work id.
    MonsterEngine::Profiling::Context context; ///< Actual originating page/frame/request/scene.
    uint64_t surface=0, config=0, pipeline=0; ///< Known origin generations; zero unavailable.
    uint32_t representationFlags=0; ///< Desired=1, selected fallback=2, mandatory=4, prefetch=8.
};
/** Immutable accepted physical work cause, separate from later coalesced origins. */
struct FSplatPageCausality {
    FSplatUpstreamOrigin origin; ///< First accepted upstream origin; never replaced by coalescing.
    uint64_t workId=0; ///< Scene-scoped monotonic accepted task identity, zero unavailable.
};
/** Generation-tagged scheduler request. */
struct FSplatPageRequest {
    uint32_t pageId = SplatInvalidId; ///< Page to load.
    double priority = 0; ///< Larger values load first.
    uint64_t sceneGeneration = 0; ///< Cancels stale scene work.
    MonsterEngine::Profiling::Context profiling; ///< Original accepted work cause; unchanged by deduplication.
    FSplatUpstreamOrigin upstream; ///< Actual controller-origin lifetime, including resident demand.
};
/** Movable completion; nonempty error indicates a terminal failure. */
struct FSplatPageResult {
    uint32_t pageId = SplatInvalidId; ///< Requested page.
    uint64_t sceneGeneration = 0; ///< Request generation.
    std::vector<uint8_t> bytes; ///< Verified encoded range.
    std::vector<FSplatGaussian> decodedGaussians; ///< Activated page records.
    std::string error; ///< Terminal read/checksum/decode failure.
    MonsterEngine::Profiling::Context profiling; ///< Original cause preserved through worker/upload ownership.
    FSplatPageCausality causality; ///< Immutable accepted work identity through transferred ownership.
};
/** Observable bounded I/O/cache lifecycle counters. */
struct FSplatStreamingStats {
    uint64_t cpuCacheBytes = 0; ///< Current retained encoded/decoded bytes.
    uint64_t queuedBytes = 0; ///< Ready-result bytes awaiting consumption.
    uint64_t bytesRead = 0; ///< Cumulative disk bytes.
    uint64_t requests = 0, cacheHits = 0, retries = 0; ///< Scheduler counters.
    uint64_t completed = 0, failed = 0, cancelled = 0; ///< Terminal counters.
    uint32_t pendingReads = 0, activeReads = 0; ///< Queue and worker occupancy.
};
/** GPU allocator/residency lifecycle counters. */
struct FSplatGPUPagePoolStats {
    uint64_t residentBytes = 0, allocatedBytes = 0; ///< Logical and reserved scene bytes.
    uint64_t stagingBytes = 0, temporaryBytes = 0; ///< Current independent budgets.
    uint64_t uploadedBytes = 0, uploadedPages = 0, evictedPages = 0; ///< Cumulative lifecycle.
    uint64_t pendingUploads = 0, pinnedPages = 0, inFlightPages = 0; ///< Queue and pins.
    uint64_t rejectedUploads = 0; ///< Budget or generation rejection count.
    bool degraded = false; ///< Refinement is blocked by capacity.
};
}
