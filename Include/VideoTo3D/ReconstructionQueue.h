// Copyright Monster Engine. All Rights Reserved.
#pragma once

#include "VideoTo3D/ProjectRepository.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace MonsterEngine::VideoTo3D {

/** Supported identity-guarded lifecycle events; no success event exists here. */
enum class RunEventKind {
    Started, ///< Pending to Running.
    PauseRequested, ///< Running to Pausing.
    CancelRequested, ///< Running or Pausing to Canceling.
    Interrupted, ///< Active state to Interrupted.
    Canceled, ///< Canceling to Canceled.
    Failed ///< Active state to Failed.
};

/** Event seam for service integration and tests, never a UI execution command. */
struct RunEvent {
    std::string projectId; ///< Exact project identity for rejection of foreign events.
    std::string runId; ///< Exact historical run identity.
    std::string stageId; ///< Exact immutable stage identity.
    std::uint64_t generation{0}; ///< Must match the positive stored generation.
    std::uint64_t sequence{0}; ///< Must be strictly greater than the stored sequence.
    RunEventKind kind{RunEventKind::Started}; ///< Requested guarded lifecycle transition.
};

/** Durable planning queue; no reconstruction worker is launched by these methods. */
class ReconstructionQueue final {
public:
    /** Create a project and its empty planning queue. */
    static std::unique_ptr<ReconstructionQueue> Create(const std::filesystem::path& directory,
                                                       const std::string& name);
    /** Open a project and durably interrupt all persisted active runs. */
    static std::unique_ptr<ReconstructionQueue> Open(const std::filesystem::path& directory);
    /** Release the owned repository and its directory lease. */
    ~ReconstructionQueue();

    /** Current durable project and queue state. */
    const ProjectSnapshot& Snapshot() const noexcept;
    /** Physical project directory held by the owned repository. */
    const std::filesystem::path& Directory() const noexcept;
    /** Persist an immutable Pending request and return its new run ID. */
    std::string AddPlannedRun(const std::string& requestBytes);
    /** Swap with an adjacent Pending run in direction -1 or +1; false at a boundary. */
    bool MoveWaiting(const std::string& runId, int direction);
    /** Persist a Pending run as Canceled; false when already Canceled. */
    bool CancelWaiting(const std::string& runId);
    /** Validate exact identity, sequence and transition before one durable commit. */
    void ApplyEvent(const RunEvent& event);

private:
    explicit ReconstructionQueue(std::unique_ptr<ProjectRepository> repository);
    std::unique_ptr<ProjectRepository> repository_;
};

} // namespace MonsterEngine::VideoTo3D
