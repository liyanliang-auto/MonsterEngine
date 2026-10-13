// Copyright Monster Engine. All Rights Reserved.
#pragma once
#include "Renderer/Splat/LOD/SplatLODTypes.h"
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
/** Generation-tagged scheduler request. */
struct FSplatPageRequest {
    uint32_t pageId = SplatInvalidId; ///< Page to load.
    double priority = 0; ///< Larger values load first.
    uint64_t sceneGeneration = 0; ///< Cancels stale scene work.
};
/** Movable completion; nonempty error indicates a terminal failure. */
struct FSplatPageResult {
    uint32_t pageId = SplatInvalidId; ///< Requested page.
    uint64_t sceneGeneration = 0; ///< Request generation.
    std::vector<uint8_t> bytes; ///< Verified encoded range.
    std::vector<FSplatGaussian> decodedGaussians; ///< Activated page records.
    std::string error; ///< Terminal read/checksum/decode failure.
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
