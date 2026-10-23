// Copyright Monster Engine. All Rights Reserved.
#pragma once
#include "Renderer/Splat/LOD/SplatLODBenchmark.h"
#include "Renderer/Splat/SplatBudgetCoordinator.h"
#include <memory>
#include <optional>
#include <array>
#include <map>

namespace MonsterRender::Splat {
/** A result is successful only when explicitly set; stable codes are machine-readable. */
struct FViewerStatus {
    bool ok = false; ///< Default is unperformed, never implicit success.
    std::string code = "not_run"; ///< Stable code independent of localized UI.
    std::string message; ///< Diagnostic detail, scrubbed before user export.
};
/** Published scene representation selected by its descriptor. */
enum class EViewerSceneKind : uint8_t { PLY, LOD };
/** Optional owner-internal orbit pose; never part of user scene schemas. */
struct FViewerOrbitCameraPreset {
    float eye[3]{}, target[3]{};
    float verticalFOVDegrees=45, nearPlane=0.05f, farPlane=100;
    float rollDegrees=0; ///< Rotate the orthonormal view basis about its forward axis.
};
/** Immutable scene-store identity; all paths are stable validated private files. */
struct FSceneDescriptor {
    std::string sceneId; ///< Opaque stable library identifier, not a path.
    std::string directory; ///< Lease-protected published scene directory.
    std::string sourcePLY; ///< Validated PLY path when kind is PLY.
    std::string manifestPath; ///< Validated manifest path when kind is LOD.
    std::string contentHash; ///< Validated source/manifest content identity.
    uint64_t generation = 0; ///< Zero invalid; owner assigns monotonically, never reuses.
    EViewerSceneKind kind = EViewerSceneKind::PLY; ///< Representation to open.
    std::optional<FViewerOrbitCameraPreset> initialCamera; ///< Trusted packaged builtin only.
};
/** Requested settings. Effective settings are copied separately after admission;
 * maximum extents restrict internal output, not the Android Surface dimensions. */
struct FViewerConfig {
    FSplatLODRuntimeOptions runtime = [] {
        FSplatLODRuntimeOptions value;
        value.activeCapacity = 100000;
        value.sortCapacity = 2000000;
        value.streaming.sceneGPUBytes = 128ull << 20;
        value.streaming.stagingBytes = 16ull << 20;
        value.streaming.cpuCacheBytes = 64ull << 20;
        value.streaming.temporaryGPUBytes = 256ull << 20;
        value.streaming.maxUploadBytesPerFrame = 2ull << 20;
        value.streaming.maxConcurrentReads = 2;
        value.lod.qualityPixels = 3;
        value.lod.hysteresis = 0.2f;
        value.lod.activeBudget = value.activeCapacity;
        value.lod.sortBudget = value.sortCapacity;
        value.projectedFusion = false;
        value.shStorageMode = 0;
        value.rasterSharedMemoryMode = 0;
        value.radixSortBlocksPerWorkgroup = 32;
        value.radixSortPhaseTiming = false;
        return value;
    }(); ///< Existing shared runtime settings, with mobile starting budgets.
    uint32_t maximumInternalEdge = 1280; ///< Positive hard longest-edge limit.
    uint64_t maximumInternalPixels = 921600; ///< Positive hard pixel-count limit.
    uint64_t generation = 0; ///< Zero invalid after admission; monotonic settings identity.
    std::string configHash; ///< Canonical requested/effective settings hash respectively.
    std::string shaderIdentity; ///< Packaged SPIR-V inventory hash, never a search directory.
};
/** Pointer lifecycle; cancellation clears gesture history without applying motion. */
enum class EViewerPointerPhase : uint8_t { Begin, Move, End, Cancel };
/** Fixed camera evidence limits; overflow is unavailable, never implicit coverage. */
inline constexpr size_t ViewerCameraContributorCapacity=16, ViewerCameraReceiptCapacity=64;
/** Incremental retained/copying evidence charges inside the existing aggregate cap. */
inline constexpr uint64_t ViewerCameraMetadataBytes=4ull<<20, AndroidCameraReceiptMetadataBytes=128ull<<10;
/** Request acceptance, owner application and successful consumption are independent. */
enum class EViewerCameraInputDisposition : uint8_t {
    Queued, Coalesced, AppliedMotion, AppliedNoMotion, CancelledNoMotion, StaleSurface,
    NoSession, QueueFull, HostStopping, LineageOverflow, UnavailableLegacy,
    CancelledByRelease, HostStopped, InvalidInput
};
/** Stable privacy-safe vocabulary shared by the typed native receipt producer. */
inline const char* ViewerCameraInputDispositionName(EViewerCameraInputDisposition d) {
    switch(d) {
    case EViewerCameraInputDisposition::Queued:return "queued";
    case EViewerCameraInputDisposition::Coalesced:return "coalesced";
    case EViewerCameraInputDisposition::AppliedMotion:return "applied_motion";
    case EViewerCameraInputDisposition::AppliedNoMotion:return "applied_no_motion";
    case EViewerCameraInputDisposition::CancelledNoMotion:return "cancelled_no_motion";
    case EViewerCameraInputDisposition::StaleSurface:return "stale_surface";
    case EViewerCameraInputDisposition::NoSession:return "no_session";
    case EViewerCameraInputDisposition::QueueFull:return "control_queue_full";
    case EViewerCameraInputDisposition::HostStopping:return "host_stopping";
    case EViewerCameraInputDisposition::LineageOverflow:return "lineage_overflow";
    case EViewerCameraInputDisposition::CancelledByRelease:return "control_cancelled_by_release";
    case EViewerCameraInputDisposition::HostStopped:return "host_stopped";
    case EViewerCameraInputDisposition::InvalidInput:return "invalid_input";
    default:return "unavailable_legacy";
    }
}
/** One bounded receipt; queue status is not proof of motion or submission. */
struct FViewerCameraInputReceipt {
    uint64_t requestScope=0,requestId=0,surfaceGeneration=0,applicationGroup=0; ///< Exact input and aggregate identities.
    EViewerPointerPhase phase=EViewerPointerPhase::Move; ///< Original lifecycle, before coalescing.
    EViewerCameraInputDisposition disposition=EViewerCameraInputDisposition::UnavailableLegacy; ///< Actual last disposition.
    bool reset=false; ///< Original reset request, not aggregate OR.
};
static_assert(sizeof(FViewerCameraInputReceipt)<=64,"Camera receipt bound");
/** Owner-queued gesture delta, derived using stable pointer IDs by platform input. */
struct FCameraInputState {
    EViewerPointerPhase phase = EViewerPointerPhase::Move; ///< Gesture lifecycle.
    int32_t pointerId = -1; ///< Stable platform pointer ID, -1 for synthesized gesture.
    float orbitX = 0, orbitY = 0; ///< Orbit delta in viewport-normalized units.
    float panX = 0, panY = 0; ///< Pan delta in viewport-normalized units.
    float pinchScale = 1; ///< Positive multiplicative zoom; one means no change.
    bool reset = false; ///< Reset camera and discard gesture history.
    uint64_t surfaceGeneration = 0; ///< Discard input from a detached/replaced Surface.
    uint64_t requestScope=0,requestId=0,applicationGroup=0,lastResetRequestId=0; ///< Optional validated handle scope and aggregate lineage.
    std::array<uint64_t,ViewerCameraContributorCapacity> contributorIds{}; ///< Exact aggregate members; never an ID range.
    uint32_t contributorCount=0; ///< Total contributors including omitted overflow members.
    bool lineageComplete=false; ///< False for legacy, mixed scope or contributor overflow.
};
static_assert(sizeof(FCameraInputState)<=256,"Queued camera lineage bound");
/** Why a tuple cannot establish request-to-successful-camera matching. */
enum class EViewerCameraProvenanceUnavailable : uint8_t { Unsubmitted, UploadOnly, Legacy, ScopeChange, LineageOverflow };
/** Immutable evidence frozen at compute uniform consumption; only a submitted copy gets a token. */
struct FViewerCameraProvenance {
    uint64_t requestScope=0,cameraRevision=0,lastAppliedRequestId=0,lastResetRequestId=0; ///< Camera-state lineage.
    uint64_t gestureBeginRequestId=0,applicationGroup=0; ///< Lifecycle and coalesced aggregate identity.
    uint64_t frameId=0,submissionToken=0,sceneGeneration=0,surfaceGeneration=0,configGeneration=0,pipelineGeneration=0; ///< Exact original consumer scope.
    std::array<uint64_t,ViewerCameraContributorCapacity> contributorIds{}; ///< Members applied since preceding successful compute.
    uint32_t contributorCount=0; ///< Total members, may exceed the fixed list capacity.
    bool lineageComplete=false,recorded=false,externalCamera=false; ///< Missing lineage and unsubmitted recording remain distinct.
    EViewerPointerPhase phase=EViewerPointerPhase::Move; ///< Most recent applied lifecycle event.
    EViewerCameraProvenanceUnavailable unavailable=EViewerCameraProvenanceUnavailable::Unsubmitted; ///< Explicit association failure reason.
    std::array<uint8_t,176> uniforms{}; ///< Canonical evidence copy; unused camPos.w/pad0 bytes zero, GPU data unchanged.
    std::array<float,3> target{},position{}; ///< Actual consumed orbit target and camera position.
    float yaw=0,pitch=0,distance=0; ///< Frozen orbit state.
};
static_assert(sizeof(FViewerCameraProvenance)<=1024,"Retained camera provenance bound");
/** Partial probes distinguish unknown from observed unsupported/disabled/failed. */
enum class EViewerObservation : uint8_t { Unknown, No, Yes };
/** One capability's support, enablement and execution evidence remain independent. */
struct FViewerCapabilityState {
    std::string name; ///< Stable feature/probe name.
    EViewerObservation supported = EViewerObservation::Unknown; ///< Device query result.
    EViewerObservation enabled = EViewerObservation::Unknown; ///< Logical device enablement.
    EViewerObservation executed = EViewerObservation::Unknown; ///< Actual execution result.
    std::string reason; ///< Failure stage or why a query was not performed.
};
/** Actual counters; nullopt means unavailable, whereas zero is a measured value. */
struct FViewerFrameCounters {
    std::optional<uint64_t> activeGaussians; ///< Submitted frame's active inputs.
    std::optional<uint64_t> sortEntries; ///< Completed GPU sample only.
    std::optional<uint64_t> sortCountFrame; ///< Original recording ID of sortEntries.
    std::optional<uint64_t> residentBytes; ///< Actual retained source-page bytes.
    std::optional<uint64_t> uploadedBytes; ///< Cumulative completed/queued upload definition in diagnostics.
    std::optional<uint64_t> cpuLeaseBytes; ///< Retained streamer reservations.
    std::optional<double> cpuFrameMilliseconds; ///< Monotonic CPU interval, never GPU substitute.
    std::optional<double> gpuMilliseconds; ///< Observed completed GPU interval only.
    std::optional<uint64_t> gpuTimingFrame; ///< Original recording ID of gpuMilliseconds.
};
/** Original-frame observation. GPU status: 0 pending, 1 valid, 2 unsupported, 3 invalid, 4 dropped. */
struct FViewerFrameMeasurement {
    uint64_t pipelineGeneration = 0; ///< Lifetime distinguishes restarted pipeline frame IDs.
    uint64_t ownerStartNanoseconds = 0, presentReturnNanoseconds = 0; ///< Monotonic wall clock.
    std::optional<double> sessionMilliseconds, acquireMilliseconds, presentMilliseconds, ownerWorkMilliseconds; ///< Nested wall scopes in ms; owner work excludes acquire/present.
    std::array<std::optional<double>,7> gpuStages; ///< Preprocess, Scan, AssignKeys, RadixSort, TileBounds, Render, Pipeline ms.
    std::array<uint32_t,7> gpuStatus{}; ///< Independent availability codes.
    uint32_t sortStatus = 0; ///< GPU count availability code.
    std::optional<uint32_t> sortOverflow; ///< Same-origin GPU overflow flag; count saturates on overflow.
    std::array<float,3> cameraPosition{}; ///< Submitted scene coordinates.
    float cameraYaw=0, cameraPitch=0, cameraDistance=0; ///< Orbit state; ignored for external camera.
    bool externalCamera=false; ///< Position remains valid for external cameras.
};
/** Independent privacy-safe cause observations; unavailable is distinct from an observed empty cause list. */
struct FViewerDegradationObservation {
    std::string status="unavailable"; ///< observed, unavailable or stale_identity.
    std::vector<std::string> codes; ///< Allowlisted stable codes, unknown prose becomes unknown_reason.
    std::map<std::string,std::optional<uint64_t>> counters; ///< Source counters and original-frame identity.
};
/** Cached allocation observation. Missing optional values remain explicitly unknown. */
struct FViewerResourceCost {
    std::string kind="unknown"; ///< actual, estimate, unknown or overlapping.
    std::optional<uint64_t> logicalBytes, requiredBytes, backingBytes, alignmentBytes, estimateBytes; ///< Independent byte facts.
    std::string accounting="unavailable"; ///< Stable disjoint or overlapping account description.
};
/** Immutable published telemetry: no references to live pipeline/worker objects. */
struct FViewerDiagnosticsSnapshot {
    uint64_t frameId = 0; ///< Recording associated with this snapshot, zero if no frame.
    uint64_t sceneGeneration = 0, surfaceGeneration = 0, configGeneration = 0; ///< Original identities.
    std::string sceneId, contentHash, buildIdentity; ///< Reproducible resource/build identities.
    FViewerConfig requestedConfig; ///< User settings prior to admission/clamping.
    std::optional<FViewerConfig> effectiveConfig; ///< Absent before successful admission.
    FViewerFrameCounters counters; ///< Actual counters with explicit availability.
    FViewerFrameMeasurement measurement; ///< Per-origin measurement.
    FViewerCameraProvenance cameraProvenance; ///< Additive separately versioned JSON evidence; CSV remains unchanged.
    std::vector<FViewerCapabilityState> capabilities; ///< Partial capability/probe observations.
    FSplatBudgetSnapshot budget; ///< Consistent aggregate backing accounting; quality receipts bind this original row, not later totals.
    bool degraded = false; ///< Fallback image or reduced quality is active.
    uint32_t presentationFormat = 0; ///< Actual selected swapchain VkFormat; zero when unavailable.
    uint32_t presentationColorSpace = 0; ///< Actual selected swapchain VkColorSpaceKHR; zero when unavailable.
    bool presentationFormatSRGB = false; ///< True when attachment writes apply sRGB encoding.
    uint32_t renderWidth = 0, renderHeight = 0; ///< Actual admitted compute extent, zero before a pipeline exists.
    uint32_t presentationWidth = 0, presentationHeight = 0; ///< Actual last recorded WSI viewport, zero before presentation.
    uint32_t displayWidth = 0, displayHeight = 0; ///< Visible Android view pixels, zero when no external compositor extent is supplied.
    float presentationDisplayScale = 1; ///< External compositor magnification, at least one; one when unavailable.
    float presentationSharpeningStrength = 0; ///< Maximum local contrast gain actually passed for external magnification.
    std::optional<uint64_t> vulkanManagedLiveBytes; ///< Manager-tracked live pooled suballocations plus dedicated backing; includes host-visible memory.
    std::optional<uint64_t> vulkanManagedBackingBytes; ///< Live bytes plus unused pools, excluding opaque WSI/driver/fallback allocations.
    std::optional<uint64_t> vulkanPooledUnusedBytes; ///< Reserved pool bytes currently free; not free device VRAM.
    std::string degradationReason; ///< Stable explanation; empty when not degraded.
    FViewerDegradationObservation pipelineDegradation, streamingDegradation; ///< Independent simultaneous causes.
    std::map<std::string,FViewerResourceCost> resourceCosts; ///< Cached queried/allocated facts; no per-frame Vulkan queries.
    uint64_t presentationReservationEstimate=0; ///< Owner-reported opaque WSI estimate, never actual driver backing.
    FViewerStatus status; ///< Last operation/error state.
};
/** External display filtering policy; does not allocate pixels or change internal quality. */
struct FViewerPresentationScalePolicy {
    float displayScale = 1; ///< Valid external magnification, floored at one; invalid extents fall back to one.
    float sharpeningStrength = 0; ///< Bounded [0, 0.22] contrast gain, zero when native upscaling already applies.
};
/** Fragment presentation transfer selector and external filter compensation; matches shader ABI. */
struct FViewerPresentationPushConstants {
    uint32_t decodeSRGBForAttachment = 0; ///< Decode encoded composite only for an sRGB attachment.
    float compositorSharpeningStrength = 0; ///< Maximum bounded gain before Android compositor filtering.
};
static_assert(sizeof(FViewerPresentationPushConstants) == 8, "Presentation push constant ABI");
/** One owner-thread recording request; stale generations must be rejected before use. */
struct FViewerFrameRequest {
    RHI::IRHICommandList* commandList = nullptr; ///< Borrowed only for this recording call.
    uint64_t monotonicNanoseconds = 0; ///< Owner clock timestamp, never wall-clock epoch.
    uint64_t sceneGeneration = 0, surfaceGeneration = 0, configGeneration = 0; ///< Expected identities.
    uint32_t viewportWidth = 0, viewportHeight = 0; ///< Zero means no drawable frame.
};
/** Recording is not submission; caller reports the actual RHI token via OnSubmitted. */
struct FViewerFrameResult {
    FViewerStatus status; ///< Recording/admission result.
    bool recorded = false; ///< True only after commands were actually recorded.
    uint64_t pipelineFrameId = 0; ///< Original pipeline recording ID, zero if none.
    MonsterEngine::TSharedPtr<RHI::IRHITexture> outputTexture; ///< Retained offscreen output, not a Surface.
    FViewerFrameCounters counters; ///< Actual values; delayed values retain their original frame ID.
    std::shared_ptr<const FViewerDiagnosticsSnapshot> diagnostics; ///< Immutable cross-thread snapshot.
};
} // namespace MonsterRender::Splat
