// Copyright Monster Engine. All Rights Reserved.
#pragma once
#include "Renderer/Splat/SplatStreamingController.h"
#include "Renderer/Splat/LOD/SplatBuildTypes.h"
#include <memory>

namespace MonsterRender::Splat {
/** Encoded internal output, never a screenshot of the Android UI. */
struct FViewerScreenshot {
    std::vector<uint8_t> png; ///< Lossless RGBA8 PNG, encoded only after GPU completion.
    std::string metadataJSON; ///< Original frame/generations, dimensions and color space.
    FViewerStatus status; ///< Explicit encoding/readback result.
    std::optional<FSplatBudgetCoordinator::FReservation> memoryLease; ///< Keeps encoded bytes charged through consumer ownership.
};
/** Bounded immutable telemetry ring. Record/exports are thread safe. Screenshot
 * methods run on the rendering owner with the device alive through Shutdown.
 * Encoding is worker-only; no Activity, Surface or synchronous UI callback. */
class FSplatViewerDiagnostics {
public:
    explicit FSplatViewerDiagnostics(size_t maximumFrames = 240);
    ~FSplatViewerDiagnostics();
    FSplatViewerDiagnostics(const FSplatViewerDiagnostics&) = delete;
    FSplatViewerDiagnostics& operator=(const FSplatViewerDiagnostics&) = delete;
    /** token is an observed successful submission, not a recording/frame ID.
     * presentReturnNanos describes CPU return cadence, never display scanout. */
    void Record(const FViewerDiagnosticsSnapshot& snapshot,
        const FSplatStreamingControllerSnapshot* streaming = nullptr,
        RHI::FGPUCompletionToken token = {}, uint64_t presentReturnNanos = 0);
    /** Versioned privacy-safe diagnostics; unknown values are null with reasons. */
    std::string SnapshotJSON() const;
    /** Numeric-only frame/streaming CSV. Missing values are empty cells. */
    std::string FramesCSV() const;
    /** Privacy-safe durable CPU build report; no source filename/path/config text. */
    static std::string BuildReportJSON(const FSplatBuildIdentity&, const FSplatBuildProgress&);
    /** Record RGBA8 internal output copy while image is GENERAL. Requires actual
     * backend allocation info and shared budget lease. One pending screenshot.
     * Caller must submit these commands or explicitly idle/cancel at Shutdown. */
    bool RecordScreenshot(RHI::IRHIDevice&, RHI::IRHICommandList&,
        MonsterEngine::TSharedPtr<RHI::IRHITexture> image,
        const FViewerDiagnosticsSnapshot&, FSplatBudgetCoordinator&, std::string& error);
    /** Associate copy with the new successful submission containing it. */
    void OnScreenshotSubmitted(RHI::FGPUCompletionToken);
    /** Nonblocking GPU poll; only completed staging bytes reach encoding worker. */
    void PollScreenshot();
    /** Consume completed PNG/status once; false while not ready. */
    bool TakeScreenshot(FViewerScreenshot& result);
    /** Explicit teardown with live device, after stopping submission. Joins worker
     * and retires staging only after actual completion/owner idle. */
    void Shutdown();
private:
    struct FImpl;
    std::unique_ptr<FImpl> Impl;
};
}
