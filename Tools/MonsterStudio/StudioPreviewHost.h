#pragma once
#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include "VideoTo3D/ProjectReconstruction.h"

/** Immutable request lineage; requestId must never be reused by the UI. */
struct StudioPreviewIdentity {
    std::filesystem::path projectDirectory;
    std::uint64_t projectGeneration{0};
    std::string executionId;
    std::uint64_t requestId{0};
    std::uint64_t resultGeneration{0};
    std::string contentHash;
};
/** Only worker-inspected, lease-protected Completed output is accepted by Studio. */
struct StudioPreviewRequest { StudioPreviewIdentity identity; std::filesystem::path output; std::optional<MonsterEngine::VideoTo3D::ReconstructionView> initialView; };
/** Ready means an actual scene frame was submitted to the embedded surface. */
enum class StudioPreviewState { Unloaded, Loading, Ready, Failed, Retiring, Stopped };
/** Copy-only cross-thread observation; no renderer references escape the owner. */
struct StudioPreviewStatus {
    StudioPreviewState state{StudioPreviewState::Unloaded};
    StudioPreviewIdentity identity;
    std::string error;
    std::uint64_t sequence{0}, retiredToken{0};
    std::uint64_t submittedFrames{0}, deviceCreations{0}, deviceDestructions{0};
    std::uint64_t sessionShutdowns{0}, reservedBytesAfterRetire{0};
    std::uint64_t lastSubmissionToken{0}, lastPresentReturnNanoseconds{0};
    std::int32_t lastPresentResult{0};
    std::uintptr_t childWindow{0};
    std::uint32_t gaussianCount{0}, width{0}, height{0};
    std::string deviceName, diagnosticsJSON;
    std::uint64_t captureId{0};
    bool capturePending{false}, captureSucceeded{false};
    std::string captureError;
};
/** One owner thread for the borrowed child HWND, session, Vulkan device and runtime.
 * Calls are bounded/coalescing and never wait for disk or GPU. The UI retains the
 * child until IsStopped; the destructor joins and must follow that observation. */
class StudioPreviewHost final {
public:
    StudioPreviewHost(HWND child, HWND notificationTarget, UINT notificationMessage);
    ~StudioPreviewHost();
    StudioPreviewHost(const StudioPreviewHost&) = delete;
    StudioPreviewHost& operator=(const StudioPreviewHost&) = delete;
    void RequestLoad(StudioPreviewRequest);
    void RequestRetire(std::uint64_t token);
    void Resize(std::uint32_t width, std::uint32_t height, bool visible);
    void Orbit(float deltaX, float deltaY);
    void Zoom(float wheelSteps);
    void ResetView();
    /** Owner-only internal-output PNG and original frame metadata, one pending command.
     * UI/window presentation capture is separate evidence. Files must be local evidence paths. */
    bool RequestCapture(std::uint64_t id, std::filesystem::path png, std::filesystem::path metadata);
    void RequestStop();
    bool IsStopped() const;
    StudioPreviewStatus Snapshot() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
