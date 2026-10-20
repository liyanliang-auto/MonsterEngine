// Copyright Monster Engine. All Rights Reserved.

#include "VideoTo3D/ReconstructionQueue.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace MonsterEngine::VideoTo3D {
namespace {

bool IsActive(RunState state) noexcept {
    return state == RunState::Running || state == RunState::Pausing ||
           state == RunState::Canceling;
}

void AdvanceRevision(ProjectSnapshot& next) {
    if (next.revision == std::numeric_limits<std::uint64_t>::max()) {
        throw ProjectError(ProjectErrorCode::LimitExceeded,
                           "Project revision exhausted; no further changes can be saved.");
    }
    ++next.revision;
}

PlannedRun& FindRun(ProjectSnapshot& snapshot, const std::string& runId) {
    const auto it = std::find_if(snapshot.runs.begin(), snapshot.runs.end(),
                                 [&runId](const PlannedRun& run) { return run.runId == runId; });
    if (it == snapshot.runs.end()) {
        throw ProjectError(ProjectErrorCode::InvalidInput,
                           "Run ID does not belong to this project.");
    }
    return *it;
}

RunState NextState(RunState current, RunEventKind kind) {
    switch (kind) {
    case RunEventKind::Started:
        if (current == RunState::Pending) return RunState::Running;
        break;
    case RunEventKind::PauseRequested:
        if (current == RunState::Running) return RunState::Pausing;
        break;
    case RunEventKind::CancelRequested:
        if (current == RunState::Running || current == RunState::Pausing) {
            return RunState::Canceling;
        }
        break;
    case RunEventKind::Interrupted:
        if (IsActive(current)) return RunState::Interrupted;
        break;
    case RunEventKind::Canceled:
        if (current == RunState::Canceling) return RunState::Canceled;
        break;
    case RunEventKind::Failed:
        if (IsActive(current)) return RunState::Failed;
        break;
    }
    throw ProjectError(ProjectErrorCode::InvalidInput,
                       "Run event is not allowed from the current state.");
}

} // namespace

ReconstructionQueue::ReconstructionQueue(std::unique_ptr<ProjectRepository> repository)
    : repository_(std::move(repository)) {}

ReconstructionQueue::~ReconstructionQueue() = default;

std::unique_ptr<ReconstructionQueue> ReconstructionQueue::Create(
    const std::filesystem::path& directory, const std::string& name) {
    return std::unique_ptr<ReconstructionQueue>(
        new ReconstructionQueue(ProjectRepository::Create(directory, name)));
}

std::unique_ptr<ReconstructionQueue> ReconstructionQueue::Open(
    const std::filesystem::path& directory) {
    auto repository = ProjectRepository::Open(directory);
    ProjectSnapshot next = repository->Snapshot();
    bool changed = false;
    for (auto& run : next.runs) {
        if (IsActive(run.state)) {
            run.state = RunState::Interrupted;
            changed = true;
        }
    }
    if (changed) {
        AdvanceRevision(next);
        repository->Commit(next);
    }
    return std::unique_ptr<ReconstructionQueue>(new ReconstructionQueue(std::move(repository)));
}

const ProjectSnapshot& ReconstructionQueue::Snapshot() const noexcept {
    return repository_->Snapshot();
}

const std::filesystem::path& ReconstructionQueue::Directory() const noexcept {
    return repository_->Directory();
}

std::string ReconstructionQueue::AddPlannedRun(const std::string& requestBytes) {
    if (requestBytes.empty()) {
        throw ProjectError(ProjectErrorCode::InvalidInput,
                           "Planned request must contain UTF-8 configuration text.");
    }
    if (requestBytes.size() > kMaxRequestBytes) {
        throw ProjectError(ProjectErrorCode::LimitExceeded,
                           "Planned request exceeds the 64 KiB limit.");
    }

    ProjectSnapshot next = Snapshot();
    if (next.runs.size() >= kMaxRuns) {
        throw ProjectError(ProjectErrorCode::LimitExceeded,
                           "Project has reached the 1024-run history limit.");
    }
    AdvanceRevision(next);

    PlannedRun run;
    do {
        run.runId = ProjectRepository::NewId();
    } while (run.runId == next.projectId ||
             std::any_of(next.runs.begin(), next.runs.end(),
                         [&run](const PlannedRun& previous) {
                             return previous.runId == run.runId ||
                                    previous.stageId == run.runId;
                         }));
    do {
        run.stageId = ProjectRepository::NewId();
    } while (run.stageId == next.projectId || run.stageId == run.runId ||
             std::any_of(next.runs.begin(), next.runs.end(),
                         [&run](const PlannedRun& previous) {
                             return previous.runId == run.stageId ||
                                    previous.stageId == run.stageId;
                         }));
    run.requestBytes = requestBytes;
    const std::string runId = run.runId;
    next.runs.push_back(std::move(run));
    repository_->Commit(next);
    return runId;
}

bool ReconstructionQueue::MoveWaiting(const std::string& runId, int direction) {
    if (direction != -1 && direction != 1) {
        throw ProjectError(ProjectErrorCode::InvalidInput,
                           "Queue move direction must be -1 or +1.");
    }
    ProjectSnapshot next = Snapshot();
    PlannedRun& run = FindRun(next, runId);
    if (run.state != RunState::Pending) {
        throw ProjectError(ProjectErrorCode::InvalidInput,
                           "Only a waiting run can be moved.");
    }
    const auto index = static_cast<std::size_t>(&run - next.runs.data());
    std::size_t neighbor = index;
    while (true) {
        if (direction < 0) {
            if (neighbor == 0) return false;
            --neighbor;
        } else {
            if (neighbor + 1 >= next.runs.size()) return false;
            ++neighbor;
        }
        if (next.runs[neighbor].state == RunState::Pending) break;
    }
    std::swap(next.runs[index], next.runs[neighbor]);
    AdvanceRevision(next);
    repository_->Commit(next);
    return true;
}

bool ReconstructionQueue::CancelWaiting(const std::string& runId) {
    ProjectSnapshot next = Snapshot();
    PlannedRun& run = FindRun(next, runId);
    if (run.state == RunState::Canceled) return false;
    if (run.state != RunState::Pending) {
        throw ProjectError(ProjectErrorCode::InvalidInput,
                           "Only a waiting run can be canceled here.");
    }
    run.state = RunState::Canceled;
    AdvanceRevision(next);
    repository_->Commit(next);
    return true;
}

void ReconstructionQueue::ApplyEvent(const RunEvent& event) {
    ProjectSnapshot next = Snapshot();
    if (event.projectId != next.projectId) {
        throw ProjectError(ProjectErrorCode::InvalidInput,
                           "Event project ID does not match the open project.");
    }
    PlannedRun& run = FindRun(next, event.runId);
    if (event.stageId != run.stageId || event.generation != run.generation) {
        throw ProjectError(ProjectErrorCode::InvalidInput,
                           "Event stage ID or generation does not match the run.");
    }
    if (event.sequence == 0 || event.sequence <= run.lastEventSequence) {
        throw ProjectError(ProjectErrorCode::Conflict,
                           "Event sequence must be positive and greater than the last saved sequence.");
    }
    run.state = NextState(run.state, event.kind);
    run.lastEventSequence = event.sequence;
    AdvanceRevision(next);
    repository_->Commit(next);
}

} // namespace MonsterEngine::VideoTo3D
