// Copyright Monster Engine. All Rights Reserved.
#pragma once
#include "Platform/Android/AndroidCapabilities.h"
#include "Platform/Android/AndroidPerformancePolicy.h"
#include "Renderer/Splat/SplatViewerSession.h"
#include <chrono>
#include <memory>

struct ANativeWindow;
namespace MonsterRender::Splat {
/** Resources are prepared on workers before host creation; shared ownership pins
 * providers/account through owner teardown and outstanding JNI calls. */
struct FAndroidRenderHostCreateInfo {
    std::shared_ptr<IResourceProvider> resources; ///< Validated packaged resource provider.
    std::shared_ptr<FSplatBudgetCoordinator> budget; ///< The process-wide account, never a private renderer account.
    FViewerConfig config; ///< Requested renderer settings.
    FAndroidProbeOptions probe; ///< Packaged manifest and reproducible platform identity.
    FAndroidPerformanceConfig performance; ///< Optional measured policy gates.
    FAndroidExperimentInputs experiment; ///< Frozen performance identity.
};
/** Copied cross-thread state. Pending retirement is distinct from completion. */
struct FAndroidRenderHostSnapshot {
    FViewerStatus status; ///< Last owner operation.
    std::shared_ptr<const FViewerDiagnosticsSnapshot> viewer; ///< Immutable session observation.
    FAndroidCapabilityReport capabilities; ///< Static queries and actual completed execution observations.
    FAndroidPerformanceDecision performance; ///< Requested pressure policy.
    FSplatBudgetSnapshot budget; ///< Shared actual reservation snapshot.
    uint64_t surfaceGeneration = 0; ///< Current owner generation.
    bool resumed = false, windowInUse = false, manualRetryRequired = false; ///< Actual lifecycle state.
    bool pressureRetired = false; ///< True only after requested owner action safely completed.
    uint64_t presentationReservationEstimate = 0; ///< Conservative WSI envelope also bound into original-frame diagnosticsP0; never measured driver backing.
    bool driverPresentationBackingMeasured = false; ///< Opaque WSI allocation remains unavailable.
    std::string cameraInputReceiptsJSON; ///< Version1 fixed64 receipt history; trusted bounded producer only.
    std::string diagnosticsJSON, framesCSV; ///< Privacy-filtered bounded exports cached at most once per second; rows retain their original identities.
};
/** Registry-safe JNI facade; Acquire pins an owner before using any handle.
 * Exactly one native thread owns Vulkan/session mutation. No UI callbacks. */
class FAndroidRenderHost : public std::enable_shared_from_this<FAndroidRenderHost> {
public:
    /** Starts an idle native owner. Returns zero and error on invalid admission. */
    static uint64_t Create(FAndroidRenderHostCreateInfo, std::string& error);
    /** Rejects stale/removed handles; never reinterpret a JNI integer as a pointer. */
    static std::shared_ptr<FAndroidRenderHost> Acquire(uint64_t handle);
    /** Removes registry admission first, then joins the owner outside registry locks. */
    static bool Destroy(uint64_t handle);
    ~FAndroidRenderHost();
    /** Acquires one native reference before queue admission. Bounded to four generations. */
    FViewerStatus Attach(ANativeWindow*, uint64_t generation, uint32_t width, uint32_t height,
        uint32_t displayWidth = 0, uint32_t displayHeight = 0);
    /** Invalidates generation immediately. Success ACK means backend and native
     * reference released; timeout keeps ownership queued for a later retry. */
    FViewerStatus Detach(uint64_t generation, std::chrono::milliseconds timeout);
    /** Coalesced lifecycle state; never joins a parser during Surface suspension. */
    FViewerStatus SetResumed(bool);
    /** Coalesced Choreographer/selected scheduler token; no native timing loop. */
    FViewerStatus RequestFrame(uint64_t nanoseconds, EAndroidFrameScheduler = EAndroidFrameScheduler::Choreographer);
    /** Lease pins private files through parser/GPU retirement and scene replacement. */
    FViewerStatus OpenScene(FSceneDescriptor, std::shared_ptr<void> sceneLease);
    /** Coalesced settings command with monotonic nonzero generation. */
    FViewerStatus ApplySettings(FViewerConfig);
    /** MOVE can merge only with the immediately preceding MOVE for that pointer;
     * DOWN/UP/CANCEL retain order or return explicit queue-full backpressure. */
    FViewerStatus CameraInput(FCameraInputState);
    /** Bounded owner controls. No pressure retirement is reported before completion. */
    FViewerStatus MemoryPressure(int32_t level);
    FViewerStatus ThermalStatus(int32_t status);
    /** Explicitly releases scene resources for exclusive background builder admission. */
    FViewerStatus ReleaseScene(std::chrono::milliseconds timeout);
    /** At most one automatic device-lost retry; manual retry re-admits resources. */
    FViewerStatus Retry();
    /** One coalesced internal-output screenshot request. */
    FViewerStatus RequestScreenshot();
    /** Move completed PNG ownership/accounting to caller; false means unavailable. */
    bool TakeScreenshot(FViewerScreenshot&);
    /** Cross-thread immutable copy; no owner references escape. */
    std::shared_ptr<const FAndroidRenderHostSnapshot> Snapshot() const;
private:
    explicit FAndroidRenderHost(FAndroidRenderHostCreateInfo);
    struct FImpl;
    std::unique_ptr<FImpl> Impl;
};
} // namespace MonsterRender::Splat
