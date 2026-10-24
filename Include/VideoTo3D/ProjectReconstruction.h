// Copyright Monster Engine. All Rights Reserved.
#pragma once
#include "VideoTo3D/ReconstructionQueue.h"
#include <filesystem>
#include <cstdint>
#include <functional>
#include <memory>
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace MonsterEngine::VideoTo3D {
/** Observed execution state, independent of v1 planning records. */
enum class ExecutionState { None, Preparing, Training, Pausing, Saving, Paused, Restoring, Completed, Failed, Stopped, Interrupted };
/** Fixed future-start resource profiles; Advanced preserves an explicit config. */
enum class ReconstructionProfile { Low, Medium, High, Advanced };
/** Only the three training settings overridden by a fixed profile. */
struct ReconstructionProfileSettings {
    std::uint64_t brushSteps; ///< Actual iteration count.
    std::uint64_t maxResolution; ///< Maximum training image edge.
    std::uint64_t maxSplats; ///< Training Gaussian cap.
};
/** Stable label; unknown enum values return Unknown and are rejected on execution. */
const char* ReconstructionProfileName(ReconstructionProfile profile) noexcept;
/** Fixed table; Advanced and unknown values throw. */
ReconstructionProfileSettings GetReconstructionProfileSettings(ReconstructionProfile profile);
/** Worker-only fixed per-user local settings path; does not read config bytes. */
std::filesystem::path DefaultRuntimeConfigurationPath();
/** Worker-only validated environment override, else per-user runtime. Never scans PATH. */
std::filesystem::path DiscoverRuntimeConfiguration();
/** Worker-only explicit trusted setup: validate and atomically persist exact source bytes. */
std::filesystem::path ProvisionRuntimeConfiguration(const std::filesystem::path& source);
/** Source-space camera values from hash-verified sealed preparation inputs. */
struct ReconstructionView {
    std::array<double,3> eye{}, forward{}, up{};
    double verticalFOVDegrees{45};
    double aspectRatio{1};
    std::string camerasSha256, imagesSha256, imageName;
};
/** Actual training observations; absent values remain unknown. */
struct ExecutionProgress {
    std::optional<std::uint64_t> observedIteration; ///< Zero-based pre-step iteration.
    std::optional<std::uint64_t> completedIterations; ///< Proven previous steps; target requires publication.
    std::optional<std::uint64_t> totalIterations; ///< Immutable admitted target.
    std::optional<std::uint64_t> sampleSequence; ///< Actual sample sequence.
};
/** Known active intervals, independent of wall-clock changes. */
struct ExecutionTiming {
    std::optional<std::uint64_t> activeMilliseconds; ///< Monotonic accumulated intervals, excluding paused waiting.
    std::optional<std::string> startedUtc; ///< Recorded start; absent for legacy records.
    std::optional<std::string> completedUtc; ///< Verified publication UTC only.
    bool activeTimeComplete{false}; ///< False when any prior interval is unknown.
    std::optional<std::string> startedBoundary; ///< preparation_start_v1 or execution_start_v1; absent on legacy records.
};
/** Actual caller-captured preparation start paired with a monotonic active prefix. */
struct PreparationTiming {
    std::uint64_t activeMilliseconds{0}; ///< Measured preparation interval, never wall-clock subtraction.
    std::string startedUtc; ///< Actual preparation start in UTC ISO8601 milliseconds with terminal Z.
};
/** Copyable worker observation; helper candidates are never terminal successes. */
struct ExecutionSnapshot {
    ExecutionState state{ExecutionState::None}; ///< Last verified state.
    std::string executionId; ///< Independent execution UUID.
    std::string stage; ///< Actual current runner stage.
    std::string message; ///< Observed diagnostic, never an estimated percentage.
    std::filesystem::path directory; ///< Owned execution evidence directory.
    std::filesystem::path output; ///< Validated published output, if completed.
    std::optional<ReconstructionView> initialView; ///< Absent for older producers without preparation inventory.
    std::uint64_t resultGeneration{0};
    std::string outputSha256;
    std::filesystem::path configurationPath; ///< Owned immutable config, empty for legacy metadata.
    std::string configurationSha256; ///< Exact execution configuration identity.
    std::string profileName; ///< Actual owned profile; empty for legacy metadata.
    ReconstructionProfileSettings effectiveSettings{}; ///< Actual three training settings.
    ExecutionProgress progress; ///< Actual training-only counts.
    ExecutionTiming timing; ///< Optional durable timing.
    bool active{false}; ///< Worker still owns an execution process lifecycle.
    bool pauseAvailable{false}; ///< Actual Brush control protocol is ready.
    bool resumeAvailable{false}; ///< Persisted safe pause can be submitted for full restore validation.
    bool controlPending{false}; ///< A control transition has not resolved.
};
/** Called on the orchestration worker, never the window thread. */
using ExecutionObserver = std::function<void(const ExecutionSnapshot&)>;
/** Stable names used by the production CLI and local execution metadata. */
const char* ExecutionStateName(ExecutionState state) noexcept;
/** Queue-lease-bound synchronous orchestration; controls and snapshots are thread safe.
 * Inspect/RunNew/Resume must run on the queue's owning worker. The caller keeps
 * the queue and service alive until these calls return; destruction never detaches.
 */
class ProjectReconstruction final {
public:
    /** Cheap construction; admission and hashing happen on the owning worker. */
    ProjectReconstruction(ReconstructionQueue& queue, const std::filesystem::path& configPath);
    /** Studio entry: captures profile, owns resolved config; empty path discovers runtime.
     * Resume always prefers the paused execution's original owned configuration. */
    ProjectReconstruction(ReconstructionQueue& queue, const std::filesystem::path& configPath, ReconstructionProfile profile);
    /** Caller must join the execution worker before destruction. */
    ~ProjectReconstruction();
    /** Inspect latest separate execution metadata without reopening the project. */
    ExecutionSnapshot Inspect();
    /** Bounded metadata history; output usability requires InspectExecution. */
    std::vector<ExecutionSnapshot> ListExecutions();
    /** Fully verify selected execution without changing current state or latest. */
    ExecutionSnapshot InspectExecution(const std::string& executionId);
    /** Worker-only measured preparation prefix for the next RunNew; active calls reject. */
    void SetPreparationActiveMilliseconds(std::uint64_t milliseconds);
    /** Worker-only actual preparation timing for the next RunNew; active calls reject.
     * Consumed once, including failed admission; Resume never replaces recorded start. */
    void SetPreparationTiming(const PreparationTiming& timing);
    /** Admit a new input-bound execution and block until the entire tree retires. */
    ExecutionSnapshot RunNew(const ExecutionObserver& observer = {});
    /** Revalidate persisted pause and start a fresh runner/Brush process attempt. */
    ExecutionSnapshot Resume(const ExecutionObserver& observer = {});
    /** Submit an atomic PauseAndSave only when the observed trainer is ready. */
    bool RequestPause();
    /** Schedule the same safe protocol at an exact next-iteration boundary (1..configuredSteps).
     * Rejected when not ready; Brush validates that the boundary is still reachable.
     */
    bool RequestPauseAtIteration(std::uint64_t nextIteration);
    /** Request isolated Job termination; never implies saved work. */
    bool RequestForceStop();
    /** Copy the guarded last observation without accessing the queue or filesystem. */
    ExecutionSnapshot Snapshot() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace MonsterEngine::VideoTo3D
