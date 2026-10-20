// Copyright Monster Engine. All Rights Reserved.
#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace MonsterEngine::VideoTo3D {

/** Maximum encoded project snapshot size, including all binary framing bytes. */
inline constexpr std::size_t kMaxSnapshotBytes = 16 * 1024 * 1024;
/** Maximum number of historical run records in one project. */
inline constexpr std::size_t kMaxRuns = 1024;
/** Maximum immutable request payload size in bytes. */
inline constexpr std::size_t kMaxRequestBytes = 64 * 1024;
/** Maximum project display name size in UTF-8 bytes. */
inline constexpr std::size_t kMaxProjectNameBytes = 1024;
/** Maximum number of metadata-only evidence paths per run. */
inline constexpr std::size_t kMaxEvidenceReferences = 16;
/** Maximum size of one relative evidence path in UTF-8 bytes. */
inline constexpr std::size_t kMaxEvidenceReferenceBytes = 4096;

/** Persisted lifecycle state; Interrupted, Canceled and Failed are terminal here. */
enum class RunState {
    Pending, ///< Waiting for future reconstruction support.
    Running, ///< A matching worker start was recorded.
    Pausing, ///< A matching pause request was recorded.
    Canceling, ///< A matching cancel request was recorded.
    Interrupted, ///< Active work was found after restart or interrupted by an event.
    Canceled, ///< Cancellation completed or a waiting run was canceled.
    Failed ///< A matching failure event was recorded.
};

/** Immutable run request and identity plus its persisted event position and state. */
struct PlannedRun {
    std::string runId; ///< Canonical lowercase UUID; never reused, including after cancellation.
    std::string stageId; ///< Canonical lowercase UUID unique across project history.
    std::uint64_t generation{1}; ///< Positive immutable generation for event matching.
    std::uint64_t lastEventSequence{0}; ///< Greatest accepted event sequence.
    RunState state{RunState::Pending}; ///< Current persisted lifecycle state.
    std::string requestBytes; ///< Nonempty immutable UTF-8 planning request, not imported media.
    std::vector<std::string> evidenceReferences; ///< Immutable metadata-only relative paths.
};

/** Complete versioned project record; runs retain terminal records in display order. */
struct ProjectSnapshot {
    std::uint32_t schemaVersion{1}; ///< Persisted format version; only v1 is supported.
    std::uint64_t revision{0}; ///< Monotone committed revision, starting at zero.
    std::string projectId; ///< Canonical lowercase UUID, immutable after creation.
    std::string name; ///< Nonempty validated UTF-8 project display name.
    std::vector<PlannedRun> runs; ///< Stable display order, including terminal records.
};

/** Stable category for a rejected project or queue operation. */
enum class ProjectErrorCode {
    InvalidInput, ///< Invalid identifier, text, state, revision or operation.
    IncompatibleSchema, ///< Unsupported persisted format version.
    LimitExceeded, ///< A documented byte or collection limit was exceeded.
    OwnershipConflict, ///< Another process owns the physical project directory.
    UnsafePath, ///< A path escapes project ownership or uses unsafe redirection.
    IoFailure, ///< A filesystem operation or durable commit failed.
    Conflict ///< An existing snapshot or run identity conflicts with the requested change.
};

/** Typed project failure carrying an actionable UTF-8 diagnostic message. */
class ProjectError : public std::runtime_error {
public:
    /** Preserve a stable category alongside the diagnostic text. */
    ProjectError(ProjectErrorCode code, const std::string& message)
        : std::runtime_error(message), code_(code) {}

    /** Return the failure category without allocating. */
    ProjectErrorCode Code() const noexcept { return code_; }

private:
    ProjectErrorCode code_;
};

} // namespace MonsterEngine::VideoTo3D
