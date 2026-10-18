// Copyright Monster Engine. All Rights Reserved.
#pragma once
#include "Renderer/Splat/LOD/SplatProxyMath.h"
#include <memory>

namespace MonsterRender::Splat {
/** Streaming writer for one immutable journal artifact. Finish publishes only the
 * artifact, not a checkpoint. Destruction removes an unfinished temporary file.
 * The journal and its control signal must outlive its writers/readers. */
class FSplatBuildChunkWriter {
public:
    ~FSplatBuildChunkWriter();
    /** Append at most config.batchCapacity activated records, encoded as 252B LE. */
    bool Append(std::span<const FSplatGaussian> records, std::string& error);
    /** Append bounded Gaussian+three-F64 records, encoded as 276B LE. */
    bool AppendWeighted(std::span<const FSplatWeightedGaussian> records, std::string& error);
    /** Append opaque page/metadata payload to a recordBytes=1 artifact, <=1MiB per call. */
    bool AppendBytes(std::span<const uint8_t> bytes, std::string& error);
    /** Fsync, checksum and atomically publish artifact; caller then commits its descriptor. */
    bool Finish(FSplatBuildChunk& descriptor, std::string& error);
private:
    struct FImpl;
    explicit FSplatBuildChunkWriter(std::unique_ptr<FImpl>);
    std::unique_ptr<FImpl> impl;
    friend class FSplatBuildJournal;
};

/** Reader validates complete committed artifact/header/payload identity before
 * exposing bounded batches. Native Gaussian padding is never part of the wire. */
class FSplatBuildChunkReader {
public:
    ~FSplatBuildChunkReader();
    /** Copy the committed immutable descriptor. */
    const FSplatBuildChunk& Descriptor() const;
    /** Encoded payload identity, useful for unchanged native page payload exports. */
    const std::string& PayloadSHA256() const;
    /** Number of records not yet consumed. */
    uint64_t RemainingRecords() const;
    /** Read <=config.batchCapacity records; empty success means end of chunk. */
    bool Read(uint32_t maximumRecords, std::vector<FSplatGaussian>& records, std::string& error);
    /** Read weighted records without resetting cumulative errors or original mass. */
    bool ReadWeighted(uint32_t maximumRecords, std::vector<FSplatWeightedGaussian>& records, std::string& error);
    /** Read opaque payload, <=1MiB; empty success means end of chunk. */
    bool ReadBytes(uint32_t maximumBytes, std::vector<uint8_t>& bytes, std::string& error);
private:
    struct FImpl;
    explicit FSplatBuildChunkReader(std::unique_ptr<FImpl>);
    std::unique_ptr<FImpl> impl;
    friend class FSplatBuildJournal;
};

/** Single-worker, cross-process-locked external-memory transaction journal.
 * Logs are checksummed framed events; checkpoints bind exact committed byte
 * prefixes. No task/chunk tree is accumulated in memory. All APIs perform worker
 * I/O and may fail with "paused"/"cancelled" at bounded control safe points.
 * A failed commit poisons this instance; close/reopen to resolve durable state.
 */
class FSplatBuildJournal {
public:
    /** Incremental task observer; false stops with the supplied diagnostic. */
    using TaskVisitor = std::function<bool(const FSplatBuildTask&, std::string& error)>;
    /** Committed creation/retirement event observer; retired=true is a tombstone.
     * Every created descriptor remains in the log; consumers can replay events
     * or use FindChunk for bounded-memory lookup of a specific live artifact. */
    using ChunkVisitor = std::function<bool(const FSplatBuildChunk&, bool retired, std::string& error)>;
    /** Must independently validate manifest/pages in the supplied scene directory. */
    using SceneValidator = std::function<bool(const std::string& directory, std::string& error)>;

    FSplatBuildJournal();
    ~FSplatBuildJournal();
    /** Create/open compatible state, verify source identity and committed prefixes,
     * truncate uncommitted log tails, and discard future-generation artifacts.
     * The root must be app-owned storage. stableSource may be outside the root;
     * its retained bytes then count separately toward the disk live-set cap.
     * Retained source paths cannot overlap journal-owned transaction files. */
    bool Open(const std::string& jobRoot, const std::string& stableSource,
        const FSplatBuildIdentity& expected, const FSplatBoundedBuildConfig& config,
        FSplatBuildControl& control, std::string& error);
    /** Last successfully committed immutable checkpoint; valid only after Open. */
    const FSplatBuildCheckpoint& Checkpoint() const;
    /** Journal-owned metadata/scratch accounting, independent of input size. */
    uint64_t MetadataBytes() const;
    /** Rescan actual live files and disk availability; validate disjoint phase
     * estimates, workspace/metadata, additional bytes and reserve with checked sums. */
    bool CheckAdmission(FSplatBuildAdmission& admission, FSplatBuildControl& control, std::string& error);
    /** Stream task events only through the last committed checkpoint prefix. */
    bool ReadTasks(const TaskVisitor&, FSplatBuildControl& control, std::string& error) const;
    /** Stream creation/retirement events only through committed bytes. */
    bool ReadChunks(const ChunkVisitor&, FSplatBuildControl& control, std::string& error) const;
    /** Look up the latest committed state of one ID without an in-memory index. */
    bool FindChunk(uint64_t id, FSplatBuildChunk& chunk, bool& found, bool& retired,
        FSplatBuildControl& control, std::string& error) const;
    /** Start one artifact for checkpoint generation+1. Strides are 252,276 or 1;
     * IDs must never be reused. Multiple partition writers may coexist on one worker. */
    bool CreateChunk(uint64_t id, uint64_t taskId, uint32_t recordBytes,
        FSplatBuildControl& control, std::unique_ptr<FSplatBuildChunkWriter>& writer, std::string& error);
    /** Open a live committed artifact after verifying full content and payload hashes. */
    bool OpenChunk(uint64_t id, FSplatBuildControl& control,
        std::unique_ptr<FSplatBuildChunkReader>& reader, std::string& error) const;
    /** Atomically advance one work-unit. All outputs must have finished and belong
     * to this task/generation; log fsync precedes atomic checkpoint publication.
     * next supplies stage, cursor, committedRecords and committedPageCount only;
     * identity/generation/log hashes/counts are derived by the journal. */
    bool CommitWorkUnit(FSplatBuildTask task, std::span<const FSplatBuildChunk> outputs,
        const FSplatBuildCheckpoint& next, FSplatBuildControl& control, std::string& error);
    /** Advance stage/cursor without artifacts, using the same durable ordering. */
    bool CommitCheckpoint(const FSplatBuildCheckpoint& next, FSplatBuildControl& control, std::string& error);
    /** Retire a partition parent only after committed direct-child outputs preserve
     * exactly its record count. Tombstone checkpoint precedes parent deletion.
     * No generic deletion API can discard the only durable representation. */
    bool RetirePartitionInput(uint64_t parentChunkId, std::span<const uint64_t> children,
        FSplatBuildControl& control, std::string& error);
    /** Export a committed payload unchanged to a new relative file within jobRoot.
     * Used for ordinary scene pages/metadata; fsync+rename publishes the raw payload. */
    bool MaterializeChunk(uint64_t id, const std::string& relativeFile,
        FSplatBuildControl& control, std::string& error);
    /** Validate and atomically rename a job-relative scene staging directory to a
     * new output directory on the same filesystem. Durable intent makes a crash
     * after rename recoverable; existing unrelated output is always rejected.
     * The staging directory must not contain the retained stable source. */
    bool PublishScene(const std::string& relativeStaging, const std::string& outputDirectory,
        const SceneValidator&, FSplatBuildControl& control, std::string& error);
private:
    struct FImpl;
    std::unique_ptr<FImpl> impl;
};
} // namespace MonsterRender::Splat
