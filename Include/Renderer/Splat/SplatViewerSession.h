// Copyright Monster Engine. All Rights Reserved.
#pragma once
#include "Renderer/Splat/SplatStreamingController.h"
#include "Renderer/Splat/SplatViewerDiagnostics.h"
#include "Renderer/Splat/SplatPipeline.h"
#include "Renderer/Splat/SplatResourceProvider.h"
#include <memory>
#include <string>

namespace MonsterRender::Splat {
/** Calibrated pinhole camera in the exported scene's world coordinates. */
struct FCalibratedCameraDesc {
    float viewMatrix[16]{}; ///< Column-major rigid world-to-view: right-handed, -Z forward, +Y up.
    double fx=0, fy=0; ///< Positive focal lengths in resized-image pixels, independently applied.
    double cx=0, cy=0; ///< Top-left principal point; integer coordinates denote pixel centers.
    float nearPlane=0, farPlane=0; ///< Positive ordered view-space clipping distances.
    uint32_t width=0, height=0; ///< Actual image extent, each in [1,16384].
    int32_t shDegree=0; ///< Display SH degree in [0,3].
};
/** Shared platform-independent scene/camera/compute/presentation owner. All methods
 * run on one rendering owner except worker preparation internal to OpenScene.
 * Device/provider and injected budget account must outlive Shutdown. */
class FSplatViewerSession {
public:
    FSplatViewerSession();
    ~FSplatViewerSession();
    FSplatViewerSession(const FSplatViewerSession&) = delete;
    FSplatViewerSession& operator=(const FSplatViewerSession&) = delete;
    /** Inject the process account before Initialize; no independent renderer totals. */
    void SetBudgetCoordinator(FSplatBudgetCoordinator&);
    /** Bind owner/device/resources; no scene parsing, hash or bulk upload. */
    FViewerStatus Initialize(RHI::IRHIDevice&, IResourceProvider&, const FViewerConfig&);
    /** Queue bounded worker preparation; accepted is loading, never ready. */
    FViewerStatus OpenScene(const FSceneDescriptor&);
    /** Touch deltas modify the same orbital camera; returns owner disposition, never submission proof. */
    EViewerCameraInputDisposition ApplyCameraInput(const FCameraInputState&);
    /** Validate an orbit pose without requiring a device. */
    static bool IsValidOrbitCameraPreset(const FViewerOrbitCameraPreset&);
    /** Owner-only startup/reset pose; null restores the original generic orbit. */
    FViewerStatus SetOrbitCameraPreset(std::optional<FViewerOrbitCameraPreset>);
    /** Quality-only changes apply directly; capacity changes require scene re-admission. */
    void ApplySettings(const FViewerConfig&);
    /** Record shared compute and fullscreen presentation into an acquired command list.
     * Does not end render pass/command buffer or submit; platform may append its UI. */
    FViewerFrameResult TickAndRecord(const FViewerFrameRequest&);
    /** Owner sets the incoming request before recording; zero clears unavailable cause. */
    void SetProfilingRequestId(uint64_t requestId);
    /** Associate only a new actual device submission with the preceding recording.
     * Successful active consumption alone publishes the exact native cut fingerprint. */
    void OnSubmitted(RHI::FGPUCompletionToken,const FViewerFrameMeasurement* owner=nullptr);
    /** Clamp to admitted maximum edge/pixels; recreate with old+new overlap charged. */
    FViewerStatus ResizeInternal(uint32_t, uint32_t);
    /** Owner-only metadata for the attached visible view; zero/invalid clears external filtering policy. */
    void SetPresentationDisplayExtent(uint32_t width, uint32_t height);
    /** Owner-provided WSI estimate; never promoted to measured backing. */
    void SetPresentationReservationEstimate(uint64_t bytes);
    /** Pure bounded policy; native upscaling and absent/invalid display extents retain zero extra gain. */
    static FViewerPresentationScalePolicy EvaluatePresentationScale(uint32_t renderWidth, uint32_t renderHeight,
        uint32_t presentationWidth, uint32_t presentationHeight, uint32_t displayWidth, uint32_t displayHeight);
    /** Stop recording; safe scene release remains explicit. */
    void Suspend();
    /** Cancel/join preparation and retire GPU resources while device is alive. */
    void ReleaseScene();
    /** Explicit waiting teardown, retaining resource/account ownership until safe. */
    void Shutdown();
    /** Owner memory pressure uses shared streaming coarse fallback. */
    void OnMemoryPressure();
    /** Read-only immutable last observation; no live renderer references cross threads. */
    std::shared_ptr<const FViewerDiagnosticsSnapshot> Snapshot() const;
    /** Shared diagnostics/screenshot owner hooks. */
    FSplatViewerDiagnostics& Diagnostics();
    FSplatBudgetCoordinator& Budget();
    /** Desktop source/fusion modes transfer GPU buffer ownership references into the
     * same pipeline; shell retains controls, benchmark, FFmpeg and legacy loading. */
    FViewerStatus AdoptDesktopGPUData(const FSplatGPUData&, const FSceneDescriptor&,
        uint32_t width, uint32_t height, bool explicitCapacities);
    /** Startup-only prepared LOD path, sharing the ordinary Android D controller. */
    FViewerStatus OpenPreparedLOD(const FSceneDescriptor&, FSplatSceneManifest);
    /** Owner hooks retain desktop export/camera sequence behavior without renderer duplication. */
    FSplatPipeline* Pipeline() const;
    FSplatStreamingController* Streaming() const;
    const FSplatGPUData& GPUData() const;
    void SetCameraUniforms(const FCameraUniforms&);
    void SetRenderSettings(const FSplatRenderSettings&);
    /** Desktop split-phase hook; shared compute recognizes the already-recorded frame. */
    FSplatStreamingFrameResult UpdateStreaming(RHI::IRHICommandList*, const FSplatLODView&, uint64_t frameId);
    /** Compute-only half for desktop capture hooks; TickAndRecord adds RecordPresent. */
    FViewerFrameResult RecordCompute(const FViewerFrameRequest&, bool frameAlreadyReserved = false);
    /** Shared fullscreen draw; input output remains GENERAL until this call. */
    FViewerStatus RecordPresent(RHI::IRHICommandList&, MonsterEngine::TSharedPtr<RHI::IRHITexture>,
        uint32_t viewportWidth, uint32_t viewportHeight);
    /** Explicit restore of GENERAL after a prior shared presentation. */
    void PrepareOutputForCompute(RHI::IRHICommandList&);
    /** Shared Vulkan column-major -Z camera conversion used by desktop FPS/orbit. */
    static FCameraUniforms BuildCamera(const float eye[3], const float right[3],
        const float up[3], const float forward[3], float verticalFOVDegrees,
        float nearPlane, float farPlane, uint32_t width, uint32_t height, int32_t shDegree);
    /** Pure calibrated construction; failure leaves output unchanged and supplies a reason.
     * Rotation orthonormality/determinant tolerance is 1e-4; affine bottom row is exact.
     * COLMAP +Y-down/+Z-forward poses must first be converted by diag(1,-1,-1,1).
     * Projection targets the compute output's bottom-up rows; no UBO ABI change. */
    static bool BuildCalibratedCamera(const FCalibratedCameraDesc&, FCameraUniforms& output,
        std::string& error);
private:
    struct FImpl;
    std::unique_ptr<FImpl> Impl;
};
} // namespace MonsterRender::Splat
