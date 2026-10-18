// Copyright Monster Engine. All Rights Reserved.
#pragma once
#include "Renderer/Splat/LOD/SplatLODTypes.h"
#include <atomic>
#include <functional>

namespace MonsterRender::Splat {
/** Independent deterministic external-memory algorithm; not the legacy builder. */
inline constexpr const char* SplatBoundedBuilderVersion = "MonsterEngine-MobileLOD-1";
/** Activation/layout identity shared by the streaming parser and checkpoint. */
inline constexpr const char* SplatBuildParserVersion = "MonsterEngine-PLY-Activated-1";
/** Journal, normalized spool and cumulative-error sidecar schema identity. */
inline constexpr uint32_t SplatBuildSchemaVersion = 1;

/** Persist enum values explicitly as u32 LE, never as a native enum object. */
enum class ESplatBuildStage : uint32_t {
    Created = 0, Copying = 1, Normalizing = 2, Partitioning = 3,
    BuildingLeaves = 4, Reducing = 5, Validating = 6, Publishing = 7,
    Completed = 8, Paused = 9, Cancelled = 10, Failed = 11,
    WaitingForResources = 12
};
/** Cooperative nonblocking operation result; cancellation dominates pause. */
enum class ESplatBuildSignal : uint32_t { Continue = 0, Pause = 1, Cancel = 2 };

/** Thread-safe signal only; progress/journal ownership remains with the worker.
 * Poll before I/O, each bounded batch and inside expensive reduction loops.
 * A pause/cancel request must not wait for a whole input, node, hash or sort.
 * Worker exits at a safe boundary; only durable committed work is resumable.
 * Cancellation is sticky for this object. Resume a cancelled job with a new control.
 */
class FSplatBuildControl {
public:
    /** Request a cooperative pause without blocking the calling thread. */
    void RequestPause() noexcept { paused.store(true, std::memory_order_release); }
    /** Clear a pause; this does not clear cancellation. */
    void Resume() noexcept { paused.store(false, std::memory_order_release); }
    /** Request cancellation; never joins, flushes or performs I/O. */
    void RequestCancel() noexcept { cancelled.store(true, std::memory_order_release); }
    /** Observe cancellation first, then pause; never blocks. */
    ESplatBuildSignal Poll() const noexcept {
        if (cancelled.load(std::memory_order_acquire)) return ESplatBuildSignal::Cancel;
        return paused.load(std::memory_order_acquire) ? ESplatBuildSignal::Pause : ESplatBuildSignal::Continue;
    }
private:
    std::atomic<bool> paused{false}; ///< Independently requested pause.
    std::atomic<bool> cancelled{false}; ///< Sticky cancellation request.
};

/** Resource and algorithm policy; validate before any large allocation.
 * All capacities and live-set sums require checked arithmetic. workingMemoryBytes
 * includes metadataBytes, vector capacity, allocator overhead, parser buffers,
 * sorting/reduction scratch, encoded/decoded page buffers and worker overlap.
 * It is a controlled CPU workspace budget, not an entire-process RSS guarantee.
 * No input-sized in-memory task/page/node index may escape this accounting.
 */
struct FSplatBoundedBuildConfig {
    uint64_t workingMemoryBytes = 256ull << 20; ///< Hard admitted CPU workspace cap.
    uint64_t metadataBytes = 32ull << 20; ///< Metadata sub-budget within workspace.
    uint64_t diskReserveBytes = 256ull << 20; ///< Minimum unconsumed disk headroom.
    uint64_t maxDiskBytes = 32ull << 30; ///< Hard job live-set cap, including retained source.
    uint64_t maxSourceBytes = 16ull << 30; ///< Source copy/import admission cap.
    uint64_t maxSourceRecords = 100000000; ///< Explicit supported input record cap.
    uint64_t maxManifestBytes = 64ull << 20; ///< Encoded/decoded final index must also fit workspace.
    uint32_t leafCapacity = 8192; ///< Positive leaf representation upper bound.
    uint32_t pageCapacity = 8192; ///< Positive independent page record upper bound.
    uint32_t maxProxyCount = 8192; ///< Maximum records in each nonleaf representation.
    uint32_t rootProxyCount = 8192; ///< Positive root cap, no greater than maxProxyCount.
    uint32_t batchCapacity = 8192; ///< Parser and spool bounded record batch capacity.
    uint32_t maxDepth = 32; ///< Finite partition depth; reject unrepresentable input.
    uint32_t maxNodes = 1000000; ///< Protocol and admitted metadata bound.
    uint32_t maxPages = 1000000; ///< Protocol and admitted metadata bound.
    uint32_t workerCount = 1; ///< One or two workers; combined scratch is admitted.
    uint32_t pollIntervalRecords = 256; ///< Maximum CPU record operations between polls.
    uint32_t maxHeaderBytes = 1u << 20; ///< Bounded PLY header parser storage.
    uint32_t maxRecordBytes = 1u << 20; ///< Bounded ASCII record/token and property stride.
    float supportSigma = 3; ///< Conservative support, never less than three.
    float proxyReduction = 0.25f; ///< Requested reduction ratio in (0,1].
    float geometricErrorScale = 1; ///< Conservative error safety scale, at least one.
    float appearanceWeight = 1; ///< Nonnegative appearance metric weight.
    float maxAppearanceError = 0.1f; ///< Nonnegative merge appearance tolerance.
    std::string builderVersion = SplatBoundedBuilderVersion; ///< Exact algorithm identity.
    std::string parserVersion = SplatBuildParserVersion; ///< Exact activation/layout identity.
    std::string builderHash; ///< Lowercase SHA256 of tool build/revision identity.
    uint32_t schemaVersion = SplatBuildSchemaVersion; ///< Exact intermediate schema version.
    bool retainCancelledWork = true; ///< Retain committed work for explicit resume/cleanup.
};

/** Identity checked before adopting any checkpoint or previously produced chunk.
 * Hashes are lowercase 64-character SHA256. Canonical config includes every
 * effective algorithm/resource policy value, not host-specific private paths.
 */
struct FSplatBuildIdentity {
    std::string sourceSHA256; ///< Hash of complete stable private source.
    uint64_t sourceBytes = 0; ///< Exact source byte length.
    uint64_t sourceRecords = 0; ///< Declared and eventually verified source record count.
    std::string parserVersion = SplatBuildParserVersion; ///< Parser semantics identity.
    std::string builderVersion = SplatBoundedBuilderVersion; ///< Independent builder identity.
    std::string builderHash; ///< Exact binary/revision identity.
    std::string configHash; ///< Hash of canonical configuration bytes.
    std::string canonicalConfig; ///< Reproducible effective configuration.
    uint32_t schemaVersion = SplatBuildSchemaVersion; ///< Journal/spool schema identity.
};

/** Cursor always points immediately after a complete source record.
 * ASCII replay starts only at a committed record boundary with the same validated
 * header/parser identity; arbitrary token byte offsets are not valid checkpoints.
 */
struct FSplatBuildSourceCursor {
    uint64_t byteOffset = 0; ///< Absolute next-record offset in stable source.
    uint64_t nextOriginalId = 0; ///< Next original row index, zero-based.
};

/** Immutable copy published by worker; callbacks must not retain worker references. */
struct FSplatBuildProgress {
    ESplatBuildStage stage = ESplatBuildStage::Created; ///< Current observable job stage.
    uint64_t processedBytes = 0; ///< Actual bytes processed within current stage.
    uint64_t processedRecords = 0; ///< Actual records processed within current stage.
    uint64_t totalBytes = 0; ///< Stage byte denominator; zero means unknown.
    uint64_t totalRecords = 0; ///< Stage record denominator; zero means unknown.
    uint64_t workingBytes = 0; ///< Current admitted CPU bytes including metadata.
    uint64_t metadataBytes = 0; ///< Current metadata subset of workingBytes.
    uint64_t peakWorkingBytes = 0; ///< Observed high-water admitted workspace.
    uint64_t diskBytes = 0; ///< Current job live-set on disk.
    uint64_t checkpointGeneration = 0; ///< Last durable committed checkpoint generation.
    std::string status; ///< Bounded human-readable stage/resource reason; no invented ETA.
    std::string error; ///< Bounded terminal failure explanation, empty otherwise.
};
/** Observer invoked on builder worker; marshal a copy for concurrent UI reads. */
using FSplatBuildProgressCallback = std::function<void(const FSplatBuildProgress&)>;

/** Phase live-set admission, recalculated before allocating or committing work.
 * Components are disjoint: metadata is a subset of workingBytes, whereas disk
 * components sum to requiredDiskBytes using checked arithmetic. Parent and child
 * overlap lasts until child durable commit; reserve is additional free space.
 */
struct FSplatBuildAdmission {
    uint64_t workingBytes = 0; ///< Total simultaneous CPU allocation including metadata.
    uint64_t metadataBytes = 0; ///< Task/chunk/page/index/strings subset.
    uint64_t sourceCopyBytes = 0; ///< Retained stable source on disk.
    uint64_t normalizedBytes = 0; ///< Normalized spool files simultaneously retained.
    uint64_t partitionOverlapBytes = 0; ///< Parent plus uncommitted child disk overlap.
    uint64_t leafPageBytes = 0; ///< Encoded final leaf pages.
    uint64_t proxyAndSidecarBytes = 0; ///< Proxy pages and cumulative-error sidecars.
    uint64_t journalBytes = 0; ///< Checkpoints, task records and transaction files.
    uint64_t packageBytes = 0; ///< Optional concurrently materialized export package.
    uint64_t requiredDiskBytes = 0; ///< Checked sum of the disjoint disk components.
    uint64_t availableDiskBytes = 0; ///< Observed free bytes before next operation.
    uint64_t additionalDiskBytes = 0; ///< New bytes required before safe reclamation.
};

/** Stable wire layout, deliberately independent of sizeof(FSplatGaussian).
 * All scalars are explicit little-endian; floats are IEEE754 binary32/binary64.
 * Gaussian order: position[4], scale[4], rotation[4], opacity, sh[48], originalId.
 * Proxy sidecar order: geometryErrorSquared F64, appearanceErrorSquared F64,
 * weight F64. Errors are cumulative squared RMS terms, never reset on reload.
 * A chunk header carries schema/kind/count/byte length/hash; readers validate
 * the header and full payload before adopting it as committed work.
 */
struct FSplatBuildRecordSchema {
    static constexpr uint32_t Version = SplatBuildSchemaVersion; ///< Spool/sidecar version.
    static constexpr uint32_t GaussianBytes = 252; ///< 61 F32 followed by one U64.
    static constexpr uint32_t OriginalIdOffset = 244; ///< U64 source identity offset.
    static constexpr uint32_t ProxyErrorBytes = 24; ///< Three F64 cumulative statistics.
    static constexpr uint32_t WeightedGaussianBytes = GaussianBytes + ProxyErrorBytes; ///< Separate spool schema.
    static constexpr uint32_t MaxIdentityStringBytes = 4096; ///< Identity text limit, excluding canonical config.
    static constexpr uint32_t MaxCanonicalConfigBytes = 65536; ///< Bounded canonical config text.
    static constexpr uint32_t MaxStatusBytes = 4096; ///< Bounded status/error text.
};

/** Transaction work kind; serialize enum values as explicit u32 LE. */
enum class ESplatBuildTaskKind : uint32_t { Normalize = 0, Partition = 1, Leaf = 2, Reduce = 3, Validate = 4, Publish = 5 };
/** Stable partition policy, including progress for coincident/degenerate centers. */
enum class ESplatBuildPartition : uint32_t { SpatialOctants = 0, StableOriginalId = 1 };
/** Incrementally read durable DAG task; never collect all tasks in a vector. */
struct FSplatBuildTask {
    uint64_t id = 0; ///< Durable job-local work-unit ID.
    uint64_t parentId = std::numeric_limits<uint64_t>::max(); ///< Owning parent or sentinel.
    ESplatBuildTaskKind kind = ESplatBuildTaskKind::Normalize; ///< Work-unit operation.
    ESplatBuildPartition partition = ESplatBuildPartition::SpatialOctants; ///< Recorded splitting policy.
    uint32_t depth = 0; ///< Bounded root-relative depth.
    uint64_t inputRecords = 0; ///< Verified record count of this task input.
    uint64_t committedGeneration = 0; ///< Zero until atomically committed.
};
/** One committed chunk descriptor; bounded strings and incremental enumeration. */
struct FSplatBuildChunk {
    uint64_t id = 0; ///< Durable chunk identity.
    uint64_t taskId = 0; ///< Producing work unit.
    std::string relativePath; ///< Validated relative path under the private job root.
    std::string sha256; ///< Encoded file SHA256 including schema header.
    uint64_t recordCount = 0; ///< Exact payload record count.
    uint64_t byteSize = 0; ///< Exact encoded file length.
    uint32_t recordBytes = FSplatBuildRecordSchema::GaussianBytes; ///< Explicit supported wire stride.
    uint64_t committedGeneration = 0; ///< Checkpoint generation admitting this chunk.
};
/** Small durable checkpoint root; task/chunk/page DAG remains streamed on disk.
 * Never publish this root before files and directory entries are durable. Retain
 * previous root until atomic replacement succeeds; discard/adopt partial outputs
 * only after checksum and identity verification. Publication is its own work unit.
 */
struct FSplatBuildCheckpoint {
    FSplatBuildIdentity identity; ///< Exact compatibility guard for all journal records.
    uint64_t generation = 0; ///< Monotonic committed root generation.
    ESplatBuildStage stage = ESplatBuildStage::Created; ///< Last committed computational stage.
    ESplatBuildStage resumeStage = ESplatBuildStage::Created; ///< Computational stage after pause/resource wait.
    FSplatBuildSourceCursor sourceCursor; ///< Complete-record normalization resume boundary.
    uint64_t committedTaskCount = 0; ///< Committed DAG records, not a resident array.
    uint64_t committedChunkCount = 0; ///< Committed chunk descriptors on disk.
    uint64_t committedPageCount = 0; ///< Fully durable encoded output pages.
    uint64_t committedRecords = 0; ///< Verified normalized source rows committed.
    std::string taskLogHash; ///< Hash of committed task-log prefix.
    uint64_t taskLogBytes = 0; ///< Prefix byte boundary eligible for recovery.
    std::string chunkLogHash; ///< Hash of committed chunk-log prefix.
    uint64_t chunkLogBytes = 0; ///< Prefix byte boundary eligible for recovery.
    bool published = false; ///< True only after validated durable scene publication.
};
} // namespace MonsterRender::Splat
