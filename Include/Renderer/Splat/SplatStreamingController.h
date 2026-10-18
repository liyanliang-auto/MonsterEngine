// Copyright Monster Engine. All Rights Reserved.
#pragma once
#include "Renderer/Splat/SplatViewerTypes.h"
#include "Renderer/Splat/LOD/SplatLODSelector.h"
#include <memory>

namespace MonsterRender::Splat {
/** Immutable owner-thread observation. CPU lease bytes overlap I/O totals and
 * must not be added to them again when charging the aggregate budget. */
struct FSplatStreamingControllerSnapshot {
    FViewerStatus status; ///< Loading, active, cancelled or failed operation state.
    uint64_t sceneGeneration = 0, frameId = 0, submittedFrameId = 0; ///< Original identities.
    RHI::FGPUCompletionToken lastSubmission{}; ///< Last observed new actual submission.
    FSplatStreamingStats io; ///< Bounded scheduler counters and total charged cache.
    FSplatPageStreamerMemoryStats cpu; ///< Transferred ownership remains charged.
    FSplatGPUPagePoolStats pool; ///< Logical residency and current staging usage.
    FSplatGPUPagePoolAllocationStats allocations; ///< Actual source and staging backing.
    uint64_t controllerReservedBytes = 0, minimumProgressBytes = 0; ///< CPU admission reservations.
    uint64_t activeCount = 0, selectedLeafNodes = 0, selectedProxyNodes = 0; ///< Actual complete cut.
    uint64_t selectionVersion = 0, residencyVersion = 0; ///< Cut and physical mapping identities.
    uint32_t terminalPageFailures = 0, deferredPages = 0; ///< Bounded failure/backpressure state.
    bool rootReady = false, loading = true, degraded = false, memoryPressure = false; ///< Observed state.
    std::string degradationReason; ///< Stable reason; absent means no degradation.
};
/** One recording's compact active input; recording alone never implies submission. */
struct FSplatStreamingFrameResult {
    FViewerStatus status; ///< Waiting for root is successful loading with an empty active set.
    FSplatActiveSet activeSet; ///< Complete resident mutually exclusive visible cut only.
    bool uploadsRecorded = false; ///< New or replayed staged uploads await actual submission.
    std::shared_ptr<const FSplatStreamingControllerSnapshot> snapshot; ///< Immutable copied counters.
};
/** Shared D-mode orchestration. All methods except immutable snapshot consumption
 * run on the rendering owner. Workers only read files; no GPU mutation on workers.
 * CancelGeneration stops work immediately; Shutdown is the explicit waiting path. */
class FSplatStreamingController {
public:
    FSplatStreamingController();
    ~FSplatStreamingController();
    FSplatStreamingController(const FSplatStreamingController&) = delete;
    FSplatStreamingController& operator=(const FSplatStreamingController&) = delete;
    /** Published LOD descriptor only. Generation must strictly increase across reopens. */
    bool Initialize(RHI::IRHIDevice* device, const FSceneDescriptor& scene,
                    const FSplatLODRuntimeOptions& runtime, std::string& error);
    /** Worker/startup-only bounded manifest preparation, before GPU mutation. */
    static bool PrepareScene(const FSceneDescriptor&, const FSplatLODRuntimeOptions&,
        FSplatSceneManifest&, std::string& error);
    /** Consume trusted worker-prepared metadata; caller retains its CPU charge
     * until controller admission takes ownership. No manifest parsing/hashing. */
    bool InitializePrepared(RHI::IRHIDevice*, const FSceneDescriptor&, const FSplatLODRuntimeOptions&,
        FSplatSceneManifest, std::string& error);
    /** No disk read or GPU wait. frameId must increase and command must already record. */
    FSplatStreamingFrameResult UpdateAndRecord(RHI::IRHICommandList* command,
                                               const FSplatLODView& camera, uint64_t frameId);
    /** Call only for the new successful submission containing the returned active set/uploads. */
    void OnSubmitted(RHI::FGPUCompletionToken token);
    /** Invalidates this scene and all queued I/O results; GPU ownership retires at Shutdown. */
    void CancelGeneration();
    /** Retains complete coarse coverage, cancels speculative I/O; fixed backing stays accounted. */
    void OnMemoryPressure();
    /** Explicit owner quality update/pressure recovery. Capacity changes require Shutdown/Initialize. */
    bool ApplySettings(const FSplatLODSettings& settings, std::string& error);
    /** Copy of counters, never references live worker or GPU allocator state. */
    FSplatStreamingControllerSnapshot Snapshot() const;
    /** Fixed source slabs. Invalid before initialization or after shutdown. */
    const FSplatGPUData& GetGPUData() const;
    /** Controller-owned validated immutable scene metadata. */
    const FSplatSceneManifest& GetManifest() const;
    /** Borrowed owner hooks invalidated by Shutdown. Never independently drain
     * transfers or Tick a pool while this controller owns it. */
    FSplatGPUPagePool* GetPagePool() const;
    FSplatPageStreamer* GetStreamer() const;
    const FSplatLODSelection& GetSelection() const;
    /** Cancels and joins workers, waits for GPU retirement and closes every CPU transfer lease. */
    void Shutdown();
private:
    struct FImpl;
    std::unique_ptr<FImpl> Impl;
    uint64_t HighestGeneration = 0; ///< Never reset by Shutdown; rejects generation ABA.
};
}
